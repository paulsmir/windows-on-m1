import importlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BootstrapImageTests(unittest.TestCase):
    def load_api(self):
        try:
            return importlib.import_module("bootstrap_image")
        except ModuleNotFoundError as exc:
            self.fail(f"bootstrap image API is missing: {exc}")

    def test_pack_round_trip_preserves_inner_image_and_profile(self):
        api = self.load_api()

        image = api.pack_bootstrap(b"s" * 0x4000, b"inner-image", flags=0x11)
        manifest, inner = api.parse_bootstrap(image)

        self.assertEqual(manifest.flags, 0x11)
        self.assertEqual(manifest.manifest_offset, 0x4000)
        self.assertEqual(manifest.payload_offset, 0x4000)
        self.assertEqual(inner, b"inner-image")

    def test_pack_rejects_empty_unaligned_or_unknown_inputs(self):
        api = self.load_api()

        cases = (
            (b"", b"inner", 0x11, "Stage 0"),
            (b"unaligned", b"inner", 0x11, "aligned"),
            (b"s" * 0x4000, b"", 0x11, "inner"),
            (b"s" * 0x4000, b"inner", 0x20, "flags"),
        )
        for stage0, inner, flags, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(api.BootstrapImageError, message):
                    api.pack_bootstrap(stage0, inner, flags)

    def test_parse_rejects_compressed_payload_corruption(self):
        api = self.load_api()
        image = bytearray(api.pack_bootstrap(b"s" * 0x4000, b"inner", flags=0x11))
        image[-1] ^= 1

        with self.assertRaisesRegex(api.BootstrapImageError, "payload|decompression|CRC"):
            api.parse_bootstrap(bytes(image))

    def test_parse_rejects_crc_mismatch_after_valid_decompression(self):
        api = self.load_api()
        image = bytearray(api.pack_bootstrap(b"s" * 0x4000, b"inner", flags=0x11))
        crc_offset = 0x4000 + 44
        struct.pack_into("<I", image, crc_offset, struct.unpack_from("<I", image, crc_offset)[0] ^ 1)

        with self.assertRaisesRegex(api.BootstrapImageError, "CRC"):
            api.parse_bootstrap(bytes(image))

    def test_parse_rejects_invalid_header_fields_and_bounds(self):
        api = self.load_api()
        valid = api.pack_bootstrap(b"s" * 0x4000, b"inner", flags=0x11)
        cases = []

        def changed(offset, fmt, value, message):
            image = bytearray(valid)
            struct.pack_into(fmt, image, 0x4000 + offset, value)
            cases.append((bytes(image), message))

        changed(8, "<H", 2, "version")
        changed(10, "<H", 63, "header")
        changed(12, "<I", 0x20, "flags")
        changed(16, "<I", 1, "reserved")
        changed(20, "<Q", 65, "aligned")
        changed(28, "<Q", 0, "size")
        changed(36, "<Q", 0, "size")
        changed(20, "<Q", ((1 << 64) - 1) & -0x4000, "bounds")

        padded = bytearray(valid)
        padded[0x4000 + 64] = 1
        cases.append((bytes(padded), "padding"))
        cases.append((valid[: 0x4000 + 63], "truncated"))

        for image, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(api.BootstrapImageError, message):
                    api.parse_bootstrap(image)

    def test_parse_rejects_absent_or_multiple_bootstrap_manifests(self):
        api = self.load_api()
        valid = api.pack_bootstrap(b"s" * 0x4000, b"inner", flags=0x11)
        second_offset = (len(valid) + api.BOOTSTRAP_ALIGNMENT - 1) & -api.BOOTSTRAP_ALIGNMENT
        ambiguous = valid + b"\0" * (second_offset - len(valid)) + valid[0x4000 : 0x4040]

        with self.assertRaisesRegex(api.BootstrapImageError, "manifest"):
            api.parse_bootstrap(b"no bootstrap")
        with self.assertRaisesRegex(api.BootstrapImageError, "multiple"):
            api.parse_bootstrap(ambiguous)

    def test_outer_and_inner_formats_do_not_accept_each_other(self):
        api = self.load_api()
        from standalone_image import ImageError, pack_image, parse_image

        inner = pack_image(b"m1n1", b"firmware", layout_version=1, flags=0x11)
        outer = api.pack_bootstrap(b"s" * 0x4000, inner, flags=0x11)

        with self.assertRaisesRegex(ImageError, "standalone manifest"):
            parse_image(outer)
        with self.assertRaisesRegex(api.BootstrapImageError, "bootstrap manifest"):
            api.parse_bootstrap(inner)

    def test_cli_packs_and_validates_both_stages(self):
        api = self.load_api()
        from standalone_image import parse_image

        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            stage0 = directory / "stage0.bin"
            stage1 = directory / "stage1.bin"
            firmware = directory / "firmware.fd"
            output = directory / "boot.bin"
            stage0.write_bytes(b"0" * 0x4000)
            stage1.write_bytes(b"1" * 0x4000)
            firmware.write_bytes(b"firmware")

            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/pack_boot.py"),
                    "--stage0-m1n1", str(stage0),
                    "--stage1-m1n1", str(stage1),
                    "--firmware", str(firmware),
                    "--layout", str(ROOT / "config/j313-guest-layout.json"),
                    "--output", str(output),
                    "--display", "physical",
                    "--debug", "monitor",
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            outer, inner = api.parse_bootstrap(output.read_bytes())
            inner_manifest, decoded_firmware = parse_image(inner)
            self.assertEqual(outer.flags, 0x11)
            self.assertEqual(inner_manifest.flags, 0x11)
            self.assertEqual(decoded_firmware, b"firmware")
            self.assertIn("outer manifest offset", result.stdout)
            self.assertIn("inner manifest offset", result.stdout)


if __name__ == "__main__":
    unittest.main()
