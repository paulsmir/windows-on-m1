import re
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CYRILLIC = re.compile(r"[\u0400-\u04ff]")
PUBLIC_TEXT_SUFFIXES = {".md", ".py", ".sh", ".txt"}


def tracked_files():
    result = subprocess.run(
        ["git", "ls-files"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return [Path(line) for line in result.stdout.splitlines() if line]


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
            if (
                CYRILLIC.search(text)
                or "/Users/pavel" in text
                or "C02HDNCCQ6L" in text
            ):
                offenders.append(str(relative))
        self.assertEqual([], sorted(set(offenders)))


if __name__ == "__main__":
    unittest.main()
