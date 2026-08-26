from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
PKG = ROOT / "mu" / "Platform" / "MacBookAirMid2020Pkg"
DSC = PKG / "MacBookAirMid2020.dsc"
FDF = PKG / "MacBookAirMid2020.fdf"
DSDT = PKG / "AcpiTables" / "DSDT.asl"
SSDT = PKG / "AcpiTables" / "J313AppleAgxSsdt.asl"
INF = PKG / "AcpiTables" / "J313AppleAgxAcpiTables.inf"
MODULE = "MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxAcpiTables.inf"
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
    def test_profile_defaults_false_and_component_is_opt_in(self):
        dsc = DSC.read_text()
        self.assertRegex(
            dsc,
            r"(?m)^\s*J313_AGX_G2_PROFILE\s*=\s*FALSE\s*$",
        )
        self.assertEqual(len(_conditional_body(dsc, MODULE)), 1)
        self.assertEqual(dsc.count(MODULE), 1)

    def test_fdf_packages_exactly_one_opt_in_acpi_module(self):
        fdf = FDF.read_text()
        packaged = "INF RuleOverride=ACPITABLE " + MODULE
        self.assertEqual(len(_conditional_body(fdf, packaged)), 1)
        self.assertEqual(fdf.count(MODULE), 1)

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

    def test_acpi_module_builds_only_the_standalone_ssdt(self):
        inf = INF.read_text()
        self.assertIn("MODULE_TYPE                    = USER_DEFINED", inf)
        self.assertIn("J313AppleAgxSsdt.asl", inf)
        self.assertNotIn("DSDT.asl", inf)
        self.assertEqual(inf.count("J313AppleAgxSsdt.asl"), 1)

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
        self.assertNotIn("AppleAgx.sys", workflow)


if __name__ == "__main__":
    unittest.main()
