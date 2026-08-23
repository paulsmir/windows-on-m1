import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

from tools.verify_apple_input_acpi_aml import AmlContractError, verify_aml


ROOT = pathlib.Path(__file__).resolve().parents[1]
DSDT = ROOT / "mu/Platform/MacBookAirMid2020Pkg/AcpiTables/DSDT.asl"
GENERATED = ROOT / "mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleInput.asl.inc"
TRIM = ROOT / "mu/MU_BASECORE/BaseTools/Source/Python/Trim/Trim.py"
BASETOOLS_PYTHON = ROOT / "mu/MU_BASECORE/BaseTools/Source/Python"
BUILD_STANDALONE = ROOT / "scripts/build-standalone.sh"


class AppleInputAcpiTests(unittest.TestCase):
    def test_binary_aml_gate_accepts_one_exact_generated_contract(self):
        aml = b"prefix" + b"AINP" + b"APPL0001"
        for value in (0x23510C000, 0x23C100000, 0x23D1F0000):
            aml += value.to_bytes(8, "little")
        aml += (0x361).to_bytes(4, "little") + b"suffix"
        verify_aml(aml)

    def test_binary_aml_gate_rejects_missing_or_duplicated_contract(self):
        with self.assertRaises(AmlContractError):
            verify_aml(b"no generated device")

        aml = b"AINPAPPL0001" * 2
        for value in (0x23510C000, 0x23C100000, 0x23D1F0000):
            aml += value.to_bytes(8, "little")
        aml += (0x361).to_bytes(4, "little")
        with self.assertRaises(AmlContractError):
            verify_aml(aml)

    def test_standalone_build_gates_the_compiled_dsdt_before_packaging(self):
        source = BUILD_STANDALONE.read_text(encoding="utf-8")
        gate = 'tools/verify_apple_input_acpi_aml.py" "$DSDT_AML"'
        self.assertEqual(source.count(gate), 1)
        self.assertLess(source.index(gate), source.rindex('pack_boot.py'))

    def test_dsdt_includes_the_generated_device_exactly_once(self):
        source = DSDT.read_text(encoding="utf-8")
        include = 'Include ("J313AppleInput.asl.inc")'
        self.assertEqual(source.count(include), 1)
        self.assertEqual(source.count("Device (AINP)"), 0)

    def test_edk2_asl_trim_inlines_the_generated_device_before_cpp(self):
        with tempfile.TemporaryDirectory() as tmp:
            flattened = pathlib.Path(tmp) / "DSDT.i"
            result = subprocess.run(
                [sys.executable, str(TRIM), "--asl-file", "-o",
                 str(flattened), str(DSDT)],
                cwd=ROOT,
                env={"PYTHONPATH": str(BASETOOLS_PYTHON)},
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            source = flattened.read_text(encoding="utf-8")
            self.assertEqual(source.count('Name (_HID, "APPL0001")'), 1)
            self.assertNotIn('Include ("J313AppleInput.asl.inc")', source)
            self.assertNotIn('#include "J313AppleInput.asl.inc"', source)

    def test_generated_node_exposes_the_exact_read_write_contract(self):
        source = GENERATED.read_text(encoding="utf-8")
        self.assertIn('Name (_HID, "APPL0001")', source)
        self.assertIn("Name (_UID, Zero)", source)
        self.assertIn("Name (_CCA, One)", source)
        self.assertIn("Name (_STA, 0x0F)", source)

        expected = (
            (0x23510C000, 0x4000),
            (0x23C100000, 0x100000),
            (0x23D1F0000, 0x4000),
        )
        ranges = []
        for minimum, maximum, length in re.findall(
            r"(0x[0-9A-F]{16}),\s*(0x[0-9A-F]{16}),\s*0x0,\s*"
            r"(0x[0-9A-F]{16})",
            source,
        ):
            start = int(minimum, 16)
            end = int(maximum, 16)
            size = int(length, 16)
            ranges.append((start, size))
            self.assertEqual(end, start + size - 1)
        self.assertEqual(tuple(ranges), expected)
        self.assertEqual(source.count("NonCacheable, ReadWrite"), 3)

    def test_interrupt_and_dsd_properties_match_the_generated_contract(self):
        source = GENERATED.read_text(encoding="utf-8")
        self.assertRegex(
            source,
            r"Interrupt \(ResourceConsumer, Level, ActiveLow, Shared,.*?\)\s*"
            r"\{\s*0x00000361\s*\}",
        )
        for property_name, value in (
            ("contract-version", 1),
            ("spi-bus-hz", 8000000),
            ("ap-gpio-pin", 195),
            ("nub-gpio-pin", 13),
            ("physical-parent-irq", 330),
            ("transfer-timeout-us", 200000),
        ):
            self.assertIn(f'Package () {{ "{property_name}", {value} }}', source)

    def test_input_resources_do_not_overlap_published_guest_ranges(self):
        input_ranges = (
            (0x23510C000, 0x235110000),
            (0x23C100000, 0x23C200000),
            (0x23D1F0000, 0x23D1F4000),
        )
        reserved = (
            (0x400000000, 0x420000000),  # PCI BAR aperture
            (0x502280000, 0x502380000),  # xHCI
            (0x690000000, 0x691000000),  # ECAM
            (0x85F000000, 0x860000000),  # physical/virtual framebuffer reservation
        )
        for start, end in input_ranges:
            for other_start, other_end in reserved:
                self.assertTrue(end <= other_start or start >= other_end)


if __name__ == "__main__":
    unittest.main()
