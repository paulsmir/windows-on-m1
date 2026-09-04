from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
PKG = ROOT / "mu" / "Platform" / "MacBookAirMid2020Pkg"
DSC = PKG / "MacBookAirMid2020.dsc"
FDF = PKG / "MacBookAirMid2020.fdf"
DSDT = PKG / "AcpiTables" / "DSDT.asl"
SSDT = PKG / "AcpiTables" / "J313AppleAgxSsdt.asl"
DEVICE_INF = PKG / "AcpiTables" / "DeviceAcpiTables.inf"
G2_DEVICE_INF = PKG / "AcpiTables" / "DeviceAcpiTablesG2.inf"
DEVICE_MODULE = "MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTables.inf"
G2_DEVICE_MODULE = "MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTablesG2.inf"
WORKFLOW = ROOT / ".github" / "workflows" / "j313-agx-g2-acpi.yml"


def _conditional_body(text, module):
    pattern = re.compile(
        r"!if\s+\$\(J313_AGX_G2_PROFILE\)\s*==\s*TRUE\s*\n"
        r"(?P<body>.*?)"
        r"!endif(?:\s*#.*)?",
        re.DOTALL,
    )
    bodies = [match.group("body") for match in pattern.finditer(text)]
    return [body for body in bodies if module in body]


class J313AgxG2MuProfileTests(unittest.TestCase):
    def test_g2_replaces_device_storage_file_with_same_live_guid(self):
        stable_inf = DEVICE_INF.read_text()
        g2_inf = G2_DEVICE_INF.read_text()
        storage_guid = re.search(
            r"(?m)^\s*FILE_GUID\s*=\s*([^\s]+)\s*$", stable_inf
        ).group(1)

        self.assertIn(f"FILE_GUID                      = {storage_guid}", g2_inf)
        for source in ("DBG2.aslc", "MCFG.aslc", "DSDT.asl"):
            self.assertIn(source, g2_inf)
        self.assertEqual(g2_inf.count("J313AppleAgxSsdt.asl"), 1)

        for platform_file in (DSC, FDF):
            text = platform_file.read_text()
            profile = re.search(
                r"!if\s+\$\(J313_AGX_G2_PROFILE\)\s*==\s*TRUE\s*\n"
                r"(?P<true>.*?)"
                r"!else\s*\n"
                r"(?P<false>.*?)"
                r"!endif(?:\s*#.*)?",
                text,
                re.DOTALL,
            )
            self.assertIsNotNone(profile)
            self.assertIn(G2_DEVICE_MODULE, profile.group("true"))
            self.assertNotIn(DEVICE_MODULE, profile.group("true"))
            self.assertIn(DEVICE_MODULE, profile.group("false"))
            self.assertNotIn(G2_DEVICE_MODULE, profile.group("false"))

    def test_profile_defaults_false_and_component_is_opt_in(self):
        dsc = DSC.read_text()
        self.assertRegex(
            dsc,
            r"(?m)^\s*J313_AGX_G2_PROFILE\s*=\s*FALSE\s*$",
        )
        self.assertEqual(len(_conditional_body(dsc, G2_DEVICE_MODULE)), 1)
        self.assertEqual(dsc.count(G2_DEVICE_MODULE), 1)

    def test_fdf_packages_exactly_one_opt_in_acpi_module(self):
        fdf = FDF.read_text()
        packaged = "INF RuleOverride=ACPITABLE " + G2_DEVICE_MODULE
        self.assertEqual(len(_conditional_body(fdf, packaged)), 1)
        self.assertEqual(fdf.count(G2_DEVICE_MODULE), 1)

    def test_stable_dsdt_remains_free_of_agx(self):
        dsdt = DSDT.read_text()
        self.assertNotIn("J313AppleAgx", dsdt)
        self.assertNotIn("APPL0002", dsdt)
        self.assertNotIn("Device (AGX0)", dsdt)

    def test_ssdt_is_a_standalone_wrapper_for_generated_resources(self):
        ssdt = SSDT.read_text()
        self.assertIn('DefinitionBlock ("", "SSDT", 0x02', ssdt)
        self.assertIn("Scope (\\_SB)", ssdt)
        self.assertIn('Include ("J313AppleAgx.asl.inc")', ssdt)
        self.assertNotIn("0x204000000", ssdt)
        self.assertNotIn("APPL0002", ssdt)

    def test_obsolete_standalone_storage_module_is_not_referenced(self):
        obsolete = "MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxAcpiTables.inf"
        self.assertNotIn(obsolete, DSC.read_text())
        self.assertNotIn(obsolete, FDF.read_text())

    def test_ci_builds_and_checks_stable_and_g2_profiles(self):
        workflow = WORKFLOW.read_text()
        self.assertIn("acpica-tools", workflow)
        self.assertRegex(
            workflow,
            r"apt-get install[^\n]*\bllvm\b",
            "CLANGPDB requires the llvm package that provides llvm-lib",
        )
        self.assertIn("tools/generate_j313_agx_g2_contract.py --check", workflow)
        self.assertIn('g2: "FALSE"', workflow)
        self.assertIn('g2: "TRUE"', workflow)
        self.assertIn("BLD_*_J313_AGX_G2_PROFILE=${G2_PROFILE}", workflow)
        self.assertIn("tools/verify_j313_agx_g2_aml.py", workflow)
        self.assertIn("J313AppleAgxSsdt.aml", workflow)
        self.assertIn("J313-EFI-AGX-G2", workflow)
        self.assertIn("actions/upload-artifact@v4", workflow)
        self.assertIn("J313MACBOOKAIRMID2020_EFI.fd", workflow)
        self.assertNotIn("AppleAgx.sys", workflow)


if __name__ == "__main__":
    unittest.main()
