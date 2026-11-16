"""Packing the standalone working profile (bench Mu FD + working-profile manifest)."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from bootstrap_image import parse_bootstrap  # noqa: E402
from guest_layout import DEFAULT_LAYOUT_PATH, load_layout  # noqa: E402
from launch_profile import profile_from_manifest_flags  # noqa: E402
from standalone_image import parse_image  # noqa: E402

PACK = ROOT / "tools/pack_boot.py"
# Bench standard Mu (BENCH-M1N1.json standard_mu, validated EXP2481/2491/2492).
BENCH_MU_SHA256 = "d9fe3a3483663f355e341c79f52f5a7a6eca25d6e726b9bce9138552765e8238"


def bench_mu_fd():
    """Return the local bench FD if this checkout's common root carries it."""
    explicit = os.environ.get("J313_BENCH_MU_FD")
    if explicit:
        return Path(explicit)
    common = subprocess.run(
        ["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute", "--git-common-dir"],
        capture_output=True, text=True, check=False).stdout.strip()
    if not common:
        return None
    bench = Path(common).parent / ".local/experiments/BENCH-M1N1.json"
    try:
        return Path(json.loads(bench.read_text())["standard_mu"]["fd"])
    except (OSError, KeyError, ValueError):
        return None


def run_pack(directory, firmware, *extra):
    stage0 = directory / "stage0.bin"
    stage1 = directory / "stage1.bin"
    stage0.write_bytes(b"0" * 0x4000)
    stage1.write_bytes(b"1" * 0x8000)
    output = directory / "boot.bin"
    result = subprocess.run(
        [sys.executable, str(PACK), "--stage0-m1n1", str(stage0), "--stage1-m1n1", str(stage1),
         "--firmware", str(firmware), "--layout", str(DEFAULT_LAYOUT_PATH),
         "--output", str(output), *extra],
        capture_output=True, text=True, check=False)
    return result, output


class PackWorkingProfileTests(unittest.TestCase):
    def test_working_profile_flags_reach_both_manifests(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            firmware = directory / "fw.fd"
            firmware.write_bytes(b"MU" * 0x1000)
            digest = hashlib.sha256(firmware.read_bytes()).hexdigest()
            result, output = run_pack(directory, firmware, "--working-profile",
                                      "--firmware-sha256", digest)
            self.assertEqual(result.returncode, 0, result.stderr)
            outer, inner = parse_bootstrap(output.read_bytes())
            nested, payload = parse_image(inner)
            self.assertEqual(outer.flags, 0xC1E1)
            self.assertEqual(nested.flags, 0xC1E1)
            self.assertEqual(nested.layout_version, 1)
            self.assertEqual(payload, firmware.read_bytes())
            metadata = json.loads((directory / "BUILD-METADATA.json").read_text())
            self.assertEqual(metadata["manifest_flags"], "0xc1e1")
            self.assertEqual(metadata["firmware"]["sha256"], digest)
            self.assertIn("pcpu_pstate=12", result.stdout)

    def test_default_profile_is_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            firmware = directory / "fw.fd"
            firmware.write_bytes(b"MU" * 0x1000)
            result, output = run_pack(directory, firmware)
            self.assertEqual(result.returncode, 0, result.stderr)
            outer, _ = parse_bootstrap(output.read_bytes())
            self.assertEqual(outer.flags, 0x1)

    def test_pinned_firmware_hash_mismatch_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            firmware = directory / "fw.fd"
            firmware.write_bytes(b"MU" * 0x1000)
            result, output = run_pack(directory, firmware, "--working-profile",
                                      "--firmware-sha256", "0" * 64)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("firmware hash mismatch", result.stderr)
            self.assertFalse(output.exists())

    def test_firmware_larger_than_the_layout_window_is_refused(self):
        layout = load_layout(DEFAULT_LAYOUT_PATH)
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            firmware = directory / "fw.fd"
            firmware.write_bytes(b"\0" * (layout.firmware_max_size + 0x4000))
            result, output = run_pack(directory, firmware, "--working-profile")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("larger than the layout window", result.stderr)
            self.assertFalse(output.exists())

    def test_bench_mu_fd_packs_with_the_matching_layout_version(self):
        fd = bench_mu_fd()
        if fd is None or not fd.is_file():
            self.skipTest("bench Mu FD (BENCH-M1N1.json standard_mu) not present on this host")
        data = fd.read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), BENCH_MU_SHA256)
        layout = load_layout(DEFAULT_LAYOUT_PATH)
        self.assertLessEqual(len(data), layout.firmware_max_size)
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            result, output = run_pack(directory, fd, "--working-profile",
                                      "--firmware-sha256", BENCH_MU_SHA256)
            self.assertEqual(result.returncode, 0, result.stderr)
            outer, inner = parse_bootstrap(output.read_bytes())
            nested, payload = parse_image(inner)
            self.assertEqual(nested.layout_version, layout.layout_version)
            self.assertEqual(hashlib.sha256(payload).hexdigest(), BENCH_MU_SHA256)
            profile = profile_from_manifest_flags(nested.flags)
            self.assertTrue(profile.ans_direct and profile.agx_g2_broker and profile.cppc
                            and profile.expanded_ram)
            self.assertEqual(profile.pcpu_pstate, 12)
            self.assertEqual(outer.flags, nested.flags)


if __name__ == "__main__":
    unittest.main()
