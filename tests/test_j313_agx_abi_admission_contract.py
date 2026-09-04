import csv
from pathlib import Path
import unittest

from tools.generate_j313_agx_abi_admission import (
    load_admission_contract,
    render_asl_include,
    render_m1n1_header,
    render_windows_header,
)


ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT / "config" / "j313-agx-abi-admission.json"
G2 = ROOT / "config" / "j313-agx-g2.json"
WINDOWS_HEADER = (
    ROOT
    / "drivers"
    / "apple-agx"
    / "render-admission"
    / "include"
    / "j313_agx_abi_admission.generated.h"
)
M1N1_HEADER = ROOT / "m1n1_windows" / "src" / "hv_agx_abi_admission.generated.h"
ASL_INCLUDE = (
    ROOT
    / "mu"
    / "Platform"
    / "MacBookAirMid2020Pkg"
    / "AcpiTables"
    / "J313AppleAgxAbiAdmission.asl.inc"
)
MATRIX = ROOT / "investigation" / "EXP406_FULL_GRAPHICS_ABI_MATRIX.csv"
MU_PACKAGE = ROOT / "mu" / "Platform" / "MacBookAirMid2020Pkg"
MU_DSC = MU_PACKAGE / "MacBookAirMid2020.dsc"
MU_FDF = MU_PACKAGE / "MacBookAirMid2020.fdf"
MU_SSDT = MU_PACKAGE / "AcpiTables" / "J313AppleAgxAbiAdmissionSsdt.asl"
MU_INF = MU_PACKAGE / "AcpiTables" / "DeviceAcpiTablesAgxAbiAdmission.inf"
M1N1_POWER = ROOT / "m1n1_windows" / "src" / "hv_agx_power_mmio.c"


class J313AgxAbiAdmissionContractTests(unittest.TestCase):
    def test_contract_is_synthetic_only_and_bound_to_current_g2(self):
        contract = load_admission_contract(CONTRACT, G2)
        self.assertEqual(contract.contract_version, 1)
        self.assertEqual(contract.platform, "J313")
        self.assertEqual(contract.acpi_hid, "APPL0002")
        self.assertEqual(contract.synthetic_scanout_guest_interrupt, 889)
        self.assertEqual(contract.memory_resources, (
            ("sgx_mmio", 0x204000000, 0x4000000),
            ("gpu", 0x9FFFB8000, 0x4000),
            ("handoff", 0x9FFF70000, 0x4000),
            ("power_broker", 0x300000000, 0x1000),
        ))
        self.assertNotIn(
            contract.synthetic_scanout_guest_interrupt,
            tuple(range(880, 889)),
        )

    def test_all_generated_outputs_are_checked_in_and_exact(self):
        contract = load_admission_contract(CONTRACT, G2)
        self.assertEqual(render_windows_header(contract), WINDOWS_HEADER.read_text())
        self.assertEqual(render_m1n1_header(contract), M1N1_HEADER.read_text())
        self.assertEqual(render_asl_include(contract), ASL_INCLUDE.read_text())
        for rendered, prefix in (
            (WINDOWS_HEADER.read_text(), "J313_AGX_ABI_ADMISSION"),
            (M1N1_HEADER.read_text(), "HV_AGX_ABI_ADMISSION"),
        ):
            self.assertIn(f"{prefix}_SYNTHETIC_SCANOUT_GUEST_INTID 889u", rendered)
            self.assertNotIn("PHYSICAL_INTID", rendered)

    def test_admission_asl_has_one_edge_irq_and_no_physical_agx_irq(self):
        rendered = render_asl_include(load_admission_contract(CONTRACT, G2))
        self.assertEqual(rendered.count("QWordMemory ("), 4)
        self.assertEqual(
            rendered.count(
                "Interrupt (ResourceConsumer, Edge, ActiveHigh, Exclusive)"
            ),
            1,
        )
        self.assertIn("{ 889 }", rendered)
        for guest in range(880, 889):
            self.assertNotIn(f"{{ {guest} }}", rendered)

    def test_matrix_covers_every_compiled_wddm3_field_once(self):
        with MATRIX.open(newline="", encoding="utf-8") as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(len(rows), 162)
        self.assertEqual(len({row["ddi"] for row in rows}), 162)
        for reserved in ("Reserved", "Reserved1", "Reserved2", "Reserved3"):
            row = next(row for row in rows if row["ddi"] == reserved)
            self.assertEqual(row["current_implementation"], "zero")
            self.assertEqual(row["safe_to_register"], "required zero")

    def test_candidate_mu_profile_is_opt_in_and_keeps_normal_g2_separate(self):
        dsc = MU_DSC.read_text()
        fdf = MU_FDF.read_text()
        admission_module = (
            "MacBookAirMid2020Pkg/AcpiTables/"
            "DeviceAcpiTablesAgxAbiAdmission.inf"
        )
        g2_module = "MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTablesG2.inf"
        stable_module = "MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTables.inf"
        self.assertIn("J313_AGX_ABI_ADMISSION_PROFILE            = FALSE", dsc)
        self.assertEqual(dsc.count(admission_module), 1)
        self.assertEqual(fdf.count(admission_module), 1)
        for text in (dsc, fdf):
            admission_index = text.index(admission_module)
            g2_index = text.index(g2_module)
            stable_index = text.index(stable_module)
            self.assertLess(admission_index, g2_index)
            self.assertLess(g2_index, stable_index)
        inf = MU_INF.read_text()
        self.assertIn("J313AppleAgxAbiAdmissionSsdt.asl", inf)
        for baseline in ("DBG2.aslc", "MCFG.aslc", "DSDT.asl"):
            self.assertIn(baseline, inf)
        wrapper = MU_SSDT.read_text()
        self.assertIn('Include ("J313AppleAgxAbiAdmission.asl.inc")', wrapper)
        self.assertNotIn('Include ("J313AppleAgx.asl.inc")', wrapper)

    def test_m1n1_scanout_interrupt_is_synthetic_only(self):
        source = M1N1_POWER.read_text()
        self.assertIn('#include "hv_agx_abi_admission.generated.h"', source)
        self.assertIn(
            "HV_AGX_ABI_ADMISSION_SYNTHETIC_SCANOUT_GUEST_INTID",
            source,
        )
        self.assertNotIn("HV_AGX_SCANOUT_PHYSICAL_INTID", source)
        self.assertNotIn("completion_route", source)
        self.assertIn("hv_agx_scanout_broker_take_irq_edge", source)
        self.assertIn("hv_vgic3_inject_irq", source)

    def test_candidate_ssdt_uses_synthetic_resource_contract(self):
        wrapper = MU_SSDT.read_text()
        generated = ASL_INCLUDE.read_text()
        self.assertNotIn("APPL0002", wrapper)
        self.assertIn("APPL0002", generated)
        self.assertIn(
            "Interrupt (ResourceConsumer, Edge, ActiveHigh, Exclusive)",
            generated,
        )
        self.assertIn("{ 889 }", generated)
        for guest in range(880, 889):
            self.assertNotIn(f"{{ {guest} }}", generated)


if __name__ == "__main__":
    unittest.main()
