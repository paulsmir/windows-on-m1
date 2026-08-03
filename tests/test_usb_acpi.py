import pathlib
import re
import unittest


DSDT = (
    pathlib.Path(__file__).resolve().parents[1]
    / "mu/Platform/MacBookAirMid2020Pkg/AcpiTables/DSDT.asl"
)


class UsbAcpiContractTest(unittest.TestCase):
    def test_free_typec_port_is_published_as_standard_xhci(self):
        source = DSDT.read_text()
        match = re.search(r"Device\s*\(XHC1\)\s*\{(?P<body>.*?)\n\s*\}", source, re.S)
        self.assertIsNotNone(match)
        body = match.group("body")

        # Apple DWC3 exposes no standard xHCI debug capability.  PNP0D10 marks the
        # controller DebuggerSafe in usbxhci.inf; with KD active Windows then preserves
        # a debugger-owned RUN state instead of building normal command/event rings.
        self.assertIn('Name(_HID, EISAID("PNP0D15"))', body)
        self.assertIn("0x0000000502280000", body)
        self.assertIn("0x000000050237FFFF", body)
        self.assertIn("0x0000000000100000", body)
        self.assertRegex(
            body,
            r"Interrupt\(ResourceConsumer,\s*Level,\s*ActiveHigh,\s*Exclusive\)\s*\{\s*857\s*\}",
        )


if __name__ == "__main__":
    unittest.main()
