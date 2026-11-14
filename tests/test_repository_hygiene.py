import re
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CYRILLIC = re.compile(r"[\u0400-\u04ff]")
PUBLIC_TEXT_SUFFIXES = {".md", ".py", ".sh", ".txt"}
# A USB serial device name carrying a real serial (the redacted form is /dev/cu.usbmodem<SERIAL>NN).
DEVICE_SERIAL = re.compile(r"/dev/(?:cu|tty)\.usbmodem[A-Z0-9]{8,}")


def private_identifiers():
    """The bench serial and home path from the untracked bench config, when this machine has one
    (scripts/bench_env.py); none on a machine without it."""
    sys.path.insert(0, str(ROOT / "scripts"))
    try:
        import bench_env
        serial = bench_env.load(("AGX_HOST_SERIAL",))["AGX_HOST_SERIAL"]
    except (SystemExit, ImportError):
        return []
    home = str(Path.home())
    return [serial] + ([home] if home.startswith("/Users/") else [])


def tracked_files():
    result = subprocess.run(
        ["git", "ls-files"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return [Path(line) for line in result.stdout.splitlines() if line]


def private_patterns(values):
    """The private values as patterns; the home path only where it is not part of a Windows path
    (C:/Users/<name> is the guest account's working folder in guest-side scripts)."""
    return [re.compile((r"(?<![A-Za-z]:)" if value.startswith("/Users/") else "") + re.escape(value)
                       + (r"(?![A-Za-z0-9_-])" if value.startswith("/Users/") else "")) for value in values]


class RepositoryHygieneTests(unittest.TestCase):
    def test_tracked_files_exclude_private_runtime_artifacts(self):
        forbidden_prefixes = (".idea/", ".local/")
        forbidden_exact = {"guest.pid", "logview.out", "single-core-boot.hv.txt"}
        offenders = [
            str(path)
            for path in tracked_files()
            if str(path).startswith(forbidden_prefixes) or str(path) in forbidden_exact
        ]
        self.assertEqual([], offenders)

    def test_public_tooling_is_english_and_location_independent(self):
        candidates = [
            Path("run_uefi.py"),
            Path("probe.py"),
            Path("hang_telemetry.py"),
            Path("logview.py"),
            Path("mu/Silicon/Apple/AppleSiliconPkg/Application/BootLaunchApp/BootLaunchApp.c"),
            *sorted(Path().glob("kd_*.py")),
        ]
        for directory in ("scripts", "tools"):
            root = ROOT / directory
            if root.exists():
                candidates.extend(
                    path.relative_to(ROOT)
                    for path in root.rglob("*")
                    if path.is_file() and path.suffix in PUBLIC_TEXT_SUFFIXES
                )

        offenders = []
        for relative in candidates:
            text = (ROOT / relative).read_text(encoding="utf-8", errors="replace")
            if CYRILLIC.search(text) or any(value in text for value in private_identifiers()):
                offenders.append(str(relative))
        self.assertEqual([], sorted(set(offenders)))

    def test_whole_tree_has_no_device_serial_or_home_path(self):
        """No tracked text file carries a USB device serial or the bench host's serial/home path
        (2026-11-13 redaction; the operator kept the published history)."""
        private = private_identifiers()
        offenders = []
        for relative in tracked_files():
            path = ROOT / relative
            if not path.is_file() or path.is_symlink():
                continue
            data = path.read_bytes()
            if b"\0" in data[:8192]:
                continue
            text = data.decode("utf-8", errors="replace")
            if DEVICE_SERIAL.search(text) or any(pattern.search(text) for pattern in private_patterns(private)):
                offenders.append(str(relative))
        self.assertEqual([], sorted(offenders))


if __name__ == "__main__":
    unittest.main()
