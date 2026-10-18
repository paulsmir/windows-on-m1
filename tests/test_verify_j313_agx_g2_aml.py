from pathlib import Path
import unittest

from tools.generate_j313_agx_g2_contract import load_g2_contract
from tools.verify_j313_agx_g2_aml import AmlContractError, verify_dsl


ROOT = Path(__file__).resolve().parents[1]
ASL = (ROOT / "mu" / "Platform" / "MacBookAirMid2020Pkg" /
       "AcpiTables" / "J313AppleAgx.asl.inc")


class J313AgxG2AmlVerifierTests(unittest.TestCase):
    def setUp(self):
        self.contract = load_g2_contract()
        self.valid = ASL.read_text()

    def assert_rejected(self, text, reason):
        with self.assertRaisesRegex(AmlContractError, reason):
            verify_dsl(text, self.contract)

    def test_exact_generated_device_is_accepted(self):
        verify_dsl(self.valid, self.contract)

    def test_iasl_disassembled_package_arities_are_accepted(self):
        disassembled = self.valid.replace(
            "Name (_DSD, Package ()",
            "Name (_DSD, Package (0x02)",
            1,
        ).replace(
            "        Package ()\n        {",
            "        Package (0x04)\n        {",
            1,
        ).replace("Package () {", "Package (0x02) {")
        verify_dsl(disassembled, self.contract)
        self.assert_rejected(
            disassembled.replace("Name (_DSD, Package (0x02)",
                                 "Name (_DSD, Package (0x03)", 1),
            "_DSD package",
        )

    def test_wrong_or_partial_hid_is_rejected(self):
        self.assert_rejected(
            self.valid.replace('"APPL0002"', '"APPL000"'),
            "_HID",
        )

    def test_duplicate_or_changed_mmio_resource_is_rejected(self):
        first = self.valid.index("        QWordMemory")
        second = self.valid.index("        QWordMemory", first + 1)
        qword = self.valid[first:second]
        self.assert_rejected(
            self.valid.replace(qword, qword + qword),
            "QWordMemory",
        )
        self.assert_rejected(
            self.valid.replace("0x0000000204000000", "0x0000000204004000"),
            "MMIO",
        )
        self.assert_rejected(
            self.valid.replace("0x00000009FFFB8000", "0x00000009FFFB4000"),
            "MMIO",
        )
        self.assert_rejected(
            self.valid.replace("0x0000000300000000", "0x0000000300001000"),
            "MMIO",
        )

    def test_interrupt_count_order_value_and_flags_are_exact(self):
        self.assert_rejected(
            self.valid.replace(
                "        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)\n"
                "        { 888 }\n",
                "",
            ),
            "interrupt",
        )
        self.assert_rejected(
            self.valid.replace("{ 880 }", "{ 881 }", 1),
            "interrupt",
        )
        self.assert_rejected(
            self.valid.replace("Level, ActiveHigh, Exclusive", "Edge, ActiveHigh, Exclusive", 1),
            "flags",
        )

    def test_contract_properties_are_exact(self):
        self.assert_rejected(
            self.valid.replace(
                self.contract.source_contract_sha256,
                "0" * 64,
            ),
            "source contract",
        )
        self.assert_rejected(
            self.valid.replace('"G13"', '"G14"'),
            "firmware generation",
        )
        self.assert_rejected(
            self.valid.replace('"V13_5"', '"V13_4"'),
            "firmware version",
        )

    def test_duplicate_device_is_rejected(self):
        self.assert_rejected(self.valid + self.valid, "Device.*exactly once")


if __name__ == "__main__":
    unittest.main()
