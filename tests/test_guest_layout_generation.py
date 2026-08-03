import importlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "config/j313-guest-layout.json"
C_HEADER = ROOT / "m1n1_windows/src/hv_autonomous_layout.generated.h"
DSC_INCLUDE = ROOT / "mu/Platform/MacBookAirMid2020Pkg/J313GuestLayout.dsc.inc"
FDF = ROOT / "mu/Platform/MacBookAirMid2020Pkg/MacBookAirMid2020.fdf"


class GuestLayoutGenerationTests(unittest.TestCase):
    def load_api(self):
        try:
            return importlib.import_module("guest_layout")
        except ModuleNotFoundError as exc:
            self.fail(f"guest layout API is missing: {exc}")

    def test_checked_in_outputs_are_generated_from_the_canonical_layout(self):
        api = self.load_api()
        try:
            layout = api.load_layout(CONFIG)
        except FileNotFoundError as exc:
            self.fail(f"canonical layout is missing: {exc}")

        self.assertEqual(layout.layout_version, 1)
        self.assertEqual(layout.phys_base, 0x850000000)
        self.assertEqual(layout.boot_args_base, 0x854000000)
        self.assertEqual(layout.adt_base, 0x851000000)
        self.assertEqual(layout.low_mem_ipa, 0x100000)
        self.assertEqual(layout.low_mem_pa, 0x8A0100000)
        self.assertEqual(layout.low_mem_size, 0x3FF00000)
        self.assertEqual(layout.cpu_count, 8)
        self.assertEqual(api.render_c(layout), C_HEADER.read_text())
        self.assertEqual(api.render_dsc(layout), DSC_INCLUDE.read_text())

    def test_invalid_layout_rejects_overlapping_reserved_regions(self):
        api = self.load_api()
        source = CONFIG.read_text().replace('"0x85f000000"', '"0x851000000"')

        with tempfile.TemporaryDirectory() as directory:
            invalid = Path(directory) / "layout.json"
            invalid.write_text(source)
            with self.assertRaisesRegex(ValueError, "overlap"):
                api.load_layout(invalid)

    def test_invalid_layout_rejects_unsafe_values(self):
        api = self.load_api()
        source = json.loads(CONFIG.read_text())
        cases = (
            ("layout_version", 2, "unsupported layout version"),
            ("cpu_count", 4, "requires 8 CPUs"),
            ("phys_base", source["ram_end"], "RAM range is empty"),
            ("virtual_fb_stride", 4, "stride is too small"),
            ("firmware_base", "0x8510b4001", "aligned"),
            ("virtual_fb_base", "0x84f000000", "outside guest RAM"),
            ("low_mem_ipa", 0, "null guard page"),
        )

        for name, value, message in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                raw = dict(source)
                raw[name] = value
                invalid = Path(directory) / "layout.json"
                invalid.write_text(json.dumps(raw))
                with self.assertRaisesRegex(ValueError, message):
                    api.load_layout(invalid)

    def test_layout_schema_rejects_missing_and_unknown_keys_as_values(self):
        api = self.load_api()
        source = json.loads(CONFIG.read_text())
        invalid_documents = []
        missing = dict(source)
        missing.pop("cpu_count")
        invalid_documents.append(missing)
        extra = dict(source)
        extra["developer_only_address"] = "0x1234"
        invalid_documents.append(extra)

        for raw in invalid_documents:
            with self.subTest(keys=sorted(raw)), tempfile.TemporaryDirectory() as directory:
                invalid = Path(directory) / "layout.json"
                invalid.write_text(json.dumps(raw))
                try:
                    api.load_layout(invalid)
                except Exception as exc:  # The public API must normalize schema errors.
                    self.assertIsInstance(exc, ValueError)
                    self.assertIn("layout keys differ", str(exc))
                else:
                    self.fail("invalid layout schema was accepted")

    def test_run_uefi_dry_run_consumes_the_selected_layout(self):
        raw = json.loads(CONFIG.read_text())
        raw["virtual_fb_width"] = 1024
        raw["virtual_fb_height"] = 768
        raw["virtual_fb_stride"] = 8192

        with tempfile.TemporaryDirectory() as directory:
            selected = Path(directory) / "layout.json"
            selected.write_text(json.dumps(raw))
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "run_uefi.py"),
                    "--dry-run",
                    "--layout",
                    str(selected),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("1024x768 / 8192 / 0x600000", result.stdout)

    def test_air_dsc_consumes_the_generated_layout_include(self):
        dsc = (ROOT / "mu/Platform/MacBookAirMid2020Pkg/MacBookAirMid2020.dsc").read_text()
        family = (
            ROOT / "mu/Silicon/Apple/T810XFamilyPkg/T810XFamilyPkg.dsc.inc"
        ).read_text()
        include = "!include MacBookAirMid2020Pkg/J313GuestLayout.dsc.inc"

        self.assertEqual(dsc.count(include), 1)
        for pcd in (
            "PcdBootArgsPointer",
            "PcdAdtPointer",
            "PcdPreloadedRamdiskBase",
            "PcdPreloadedRamdiskMaxSize",
            "PcdLowMemoryWindowBase",
            "PcdLowMemoryWindowSize",
            "PcdLowMemoryWindowBackingBase",
        ):
            self.assertNotIn(pcd, family)

    def test_mu_firmware_volume_matches_the_canonical_layout(self):
        api = self.load_api()
        layout = api.load_layout(CONFIG)
        try:
            api.validate_fdf(layout, FDF.read_text())
        except AttributeError as exc:
            self.fail(f"FDF layout validation is missing: {exc}")

        mutations = (
            ("BaseAddress   = 0x8510B4000", "BaseAddress   = 0x8510C0000", "base"),
            ("Size          = 0x001E00000", "Size          = 0x001D00000", "size"),
        )
        for old, new, message in mutations:
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                api.validate_fdf(layout, FDF.read_text().replace(old, new))


if __name__ == "__main__":
    unittest.main()
