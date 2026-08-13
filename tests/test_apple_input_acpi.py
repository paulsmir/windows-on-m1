import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
DSDT = ROOT / "mu/Platform/MacBookAirMid2020Pkg/AcpiTables/DSDT.asl"
GENERATED = ROOT / "mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleInput.asl.inc"


class AppleInputAcpiTests(unittest.TestCase):
    def test_dsdt_includes_the_generated_device_exactly_once(self):
        source = DSDT.read_text(encoding="utf-8")
        include = '#include "J313AppleInput.asl.inc"'
        self.assertEqual(source.count(include), 1)
        self.assertEqual(source.count("Device (AINP)"), 0)

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
