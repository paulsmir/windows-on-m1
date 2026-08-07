import importlib
import re
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
C_HEADER = ROOT / "m1n1_windows/src/hv_autonomous_manifest.h"


class StandaloneImageTests(unittest.TestCase):
    def load_api(self):
        try:
            return importlib.import_module("standalone_image")
        except ModuleNotFoundError as exc:
            self.fail(f"standalone image API is missing: {exc}")

    def test_pack_round_trip_and_alignment(self):
        api = self.load_api()
        firmware = b"firmware" * 4096

        image = api.pack_image(b"m1n1", firmware, layout_version=1)
        manifest, unpacked = api.parse_image(image)

        self.assertEqual(manifest.manifest_offset % api.IMAGE_ALIGNMENT, 0)
        self.assertEqual(manifest.payload_offset % api.IMAGE_ALIGNMENT, 0)
        self.assertEqual(manifest.layout_version, 1)
        self.assertEqual(manifest.flags, 1)
        self.assertEqual(unpacked, firmware)

    def test_pack_preserves_supported_launch_profile_flags(self):
        api = self.load_api()

        image = api.pack_image(b"m1n1", b"firmware", layout_version=1, flags=0xA)
        manifest, _ = api.parse_image(image)

        self.assertEqual(manifest.flags, 0xA)

    def test_pack_preserves_physical_monitor_profile(self):
        api = self.load_api()

        image = api.pack_image(b"m1n1", b"firmware", layout_version=1, flags=0x11)
        manifest, _ = api.parse_image(image)

        self.assertEqual(manifest.flags, 0x11)

    def test_pack_and_parse_reject_unknown_or_ambiguous_profile_flags(self):
        api = self.load_api()

        for flags in (0xC, 0x14, 0x18, 0x1C, 0x20):
            with self.subTest(flags=flags):
                with self.assertRaisesRegex(api.ImageError, "flags"):
                    api.pack_image(b"m1n1", b"firmware", layout_version=1, flags=flags)

    def test_parse_rejects_payload_corruption(self):
        api = self.load_api()
        image = bytearray(api.pack_image(b"m1n1", b"firmware", layout_version=1))
        image[-1] ^= 1

        with self.assertRaisesRegex(api.ImageError, "payload|CRC"):
            api.parse_image(bytes(image))

    def test_parse_rejects_crc_mismatch_after_valid_decompression(self):
        api = self.load_api()
        image = bytearray(api.pack_image(b"m1n1", b"firmware", layout_version=1))
        manifest_offset = api.IMAGE_ALIGNMENT
        crc_offset = manifest_offset + 48
        crc = struct.unpack_from("<I", image, crc_offset)[0]
        struct.pack_into("<I", image, crc_offset, crc ^ 1)

        with self.assertRaisesRegex(api.ImageError, "CRC"):
            api.parse_image(bytes(image))

    def test_parse_rejects_invalid_or_ambiguous_images(self):
        api = self.load_api()
        valid = api.pack_image(b"m1n1", b"firmware", layout_version=1)
        cases = (
            (b"m1n1 only", "manifest"),
            (api.pack_image(valid, b"second", layout_version=1), "multiple"),
        )

        for image, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(api.ImageError, message):
                    api.parse_image(image)

    def test_c_and_python_abi_constants_match(self):
        api = self.load_api()
        try:
            header = C_HEADER.read_text()
        except FileNotFoundError as exc:
            self.fail(f"native manifest ABI header is missing: {exc}")

        def integer(name):
            match = re.search(rf"^#define {name} (0x[0-9a-fA-F]+|[0-9]+)u?$", header, re.M)
            self.assertIsNotNone(match, f"missing C constant {name}")
            return int(match.group(1), 0)

        magic = re.search(r'^#define HV_AUTONOMOUS_MAGIC "([A-Z]+)"$', header, re.M)
        self.assertIsNotNone(magic, "missing C manifest magic")
        self.assertEqual(magic.group(1).encode("ascii"), api.IMAGE_MAGIC)
        self.assertEqual(integer("HV_AUTONOMOUS_FORMAT_VERSION"), api.FORMAT_VERSION)
        self.assertEqual(integer("HV_AUTONOMOUS_IMAGE_ALIGNMENT"), api.IMAGE_ALIGNMENT)
        self.assertEqual(integer("HV_AUTONOMOUS_MANIFEST_SIZE"), api.MANIFEST_SIZE)

    def test_cli_rejects_a_non_raw_m1n1_input(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            m1n1 = directory / "m1n1.bin"
            firmware = directory / "firmware.fd"
            output = directory / "boot.bin"
            m1n1.write_bytes(b"not a raw aligned image")
            firmware.write_bytes(b"firmware")
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/pack_boot.py"),
                    "--m1n1",
                    str(m1n1),
                    "--firmware",
                    str(firmware),
                    "--layout",
                    str(ROOT / "config/j313-guest-layout.json"),
                    "--output",
                    str(output),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )

            self.assertFalse(output.exists())

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("_payload_start", result.stderr)

    def test_cli_writes_a_publicly_readable_atomic_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            m1n1 = directory / "m1n1.bin"
            firmware = directory / "firmware.fd"
            output = directory / "boot.bin"
            m1n1.write_bytes(b"m" * 0x4000)
            firmware.write_bytes(b"firmware")
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/pack_boot.py"),
                    "--m1n1",
                    str(m1n1),
                    "--firmware",
                    str(firmware),
                    "--layout",
                    str(ROOT / "config/j313-guest-layout.json"),
                    "--output",
                    str(output),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.stat().st_mode & 0o777, 0o644)

    def test_cli_embeds_requested_display_and_debug_profile(self):
        api = self.load_api()
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            m1n1 = directory / "m1n1.bin"
            firmware = directory / "firmware.fd"
            output = directory / "boot.bin"
            m1n1.write_bytes(b"m" * 0x4000)
            firmware.write_bytes(b"firmware")

            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/pack_boot.py"),
                    "--m1n1", str(m1n1),
                    "--firmware", str(firmware),
                    "--layout", str(ROOT / "config/j313-guest-layout.json"),
                    "--output", str(output),
                    "--display", "both",
                    "--debug", "full",
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            manifest, _ = api.parse_image(output.read_bytes())
            self.assertEqual(manifest.flags, 0xB)
            self.assertIn("profile: display=both debug=full", result.stdout)


if __name__ == "__main__":
    unittest.main()
