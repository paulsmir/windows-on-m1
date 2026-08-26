import json
from pathlib import Path
import tempfile
import unittest

from tools.generate_j313_agx_g2_contract import (
    G2ContractError,
    load_g2_contract,
    render_asl_include,
    render_m1n1_header,
    render_windows_header,
)


ROOT = Path(__file__).resolve().parents[1]
G1 = ROOT / "config" / "j313-agx.json"
G2 = ROOT / "config" / "j313-agx-g2.json"
HEADER = (ROOT / "drivers" / "apple-agx" / "shared" / "include" /
          "j313_agx_g2.generated.h")
ASL_INCLUDE = (ROOT / "mu" / "Platform" / "MacBookAirMid2020Pkg" /
               "AcpiTables" / "J313AppleAgx.asl.inc")
M1N1_HEADER = ROOT / "m1n1_windows" / "src" / "hv_agx_g2.generated.h"


class J313AgxG2ContractTests(unittest.TestCase):
    def test_reviewed_contract_is_bound_to_accepted_g1r_resources(self):
        contract = load_g2_contract(G2, G1)
        self.assertEqual(contract.acpi_hid, "APPL0002")
        self.assertEqual(contract.context_id, 63)
        self.assertEqual(contract.queue_index, 1)
        self.assertEqual(contract.page_size, 0x4000)
        self.assertEqual(contract.work_timeout_ms, 500)
        self.assertEqual(contract.acpi_mmio, (
            ("sgx_mmio", 0x204000000, 0x4000000),
        ))
        self.assertEqual(contract.mmio_subregions, (
            ("asc_mmio", 0x206400000, 0x6C000),
        ))
        self.assertEqual(contract.synthetic_mmio, (
            ("power_broker", 0x300000000, 0x1000),
        ))
        self.assertEqual(
            tuple(route.physical for route in contract.interrupt_routes),
            (563, 564, 565, 566, 579, 576, 575, 578, 577),
        )
        self.assertEqual(
            tuple(route.guest for route in contract.interrupt_routes),
            tuple(range(880, 889)),
        )

    def test_generated_header_is_checked_in_and_deterministic(self):
        rendered = render_windows_header(load_g2_contract(G2, G1))
        self.assertEqual(rendered, HEADER.read_text())
        self.assertIn("J313_AGX_G2_SOURCE_CONTRACT_SHA256", rendered)
        self.assertIn("J313_AGX_G2_INTERRUPT_ROUTE_VALUES", rendered)
        self.assertNotIn("UINT64_C", rendered)

    def test_generated_asl_is_exact_and_deterministic(self):
        contract = load_g2_contract(G2, G1)
        rendered = render_asl_include(contract)
        self.assertEqual(rendered, ASL_INCLUDE.read_text())
        self.assertIn('Name (_HID, "APPL0002")', rendered)
        self.assertIn("Name (_UID, Zero)", rendered)
        self.assertIn("Name (_CCA, One)", rendered)
        self.assertIn("Name (_STA, 0x0F)", rendered)
        self.assertIn("0x0000000204000000", rendered)
        self.assertIn("0x0000000207FFFFFF", rendered)
        self.assertIn("0x0000000004000000", rendered)
        self.assertIn("0x0000000300000000", rendered)
        self.assertIn("0x0000000000001000", rendered)
        self.assertEqual(rendered.count("Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)"), 9)
        for guest in range(880, 889):
            self.assertIn(f"{{ {guest} }}", rendered)
        self.assertIn('"agx-contract-version", 0x02', rendered)
        self.assertIn(
            f'"agx-source-contract-sha256", "{contract.source_contract_sha256}"',
            rendered,
        )
        self.assertIn(
            f'"agx-firmware-generation", "{contract.firmware_generation}"',
            rendered,
        )
        self.assertIn(
            f'"agx-firmware-version", "{contract.firmware_version}"',
            rendered,
        )

    def test_generated_m1n1_policy_header_is_exact_and_deterministic(self):
        contract = load_g2_contract(G2, G1)
        rendered = render_m1n1_header(contract)
        self.assertEqual(rendered, M1N1_HEADER.read_text())
        self.assertIn('#define HV_AGX_G2_PROFILE_IDENTITY "agx-g2"', rendered)
        self.assertIn(contract.source_contract_sha256, rendered)
        self.assertIn("#define HV_AGX_G2_SGX_MMIO_BASE 0x204000000ULL", rendered)
        self.assertIn("#define HV_AGX_G2_SGX_MMIO_SIZE 0x4000000ULL", rendered)
        self.assertIn("#define HV_AGX_G2_POWER_BROKER_BASE 0x300000000ULL", rendered)
        self.assertIn("#define HV_AGX_G2_POWER_BROKER_SIZE 0x1000ULL", rendered)
        self.assertIn("#define HV_AGX_G2_INTERRUPT_ROUTE_COUNT 9u", rendered)
        for route in contract.interrupt_routes:
            self.assertIn(
                f"{{{route.physical}u, {route.guest}u}}", rendered
            )

    def test_source_hash_mismatch_is_rejected(self):
        data = json.loads(G2.read_text())
        data["source_contract_sha256"] = "0" * 64
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "g2.json"
            path.write_text(json.dumps(data))
            with self.assertRaisesRegex(G2ContractError, "source contract"):
                load_g2_contract(path, G1)

    def test_unreviewed_physical_interrupt_is_rejected(self):
        data = json.loads(G2.read_text())
        data["interrupt_routes"][0]["physical"] = 999
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "g2.json"
            path.write_text(json.dumps(data))
            with self.assertRaisesRegex(G2ContractError, "physical interrupt"):
                load_g2_contract(path, G1)

    def test_duplicate_or_reserved_guest_interrupt_is_rejected(self):
        data = json.loads(G2.read_text())
        data["interrupt_routes"][1]["guest"] = data["interrupt_routes"][0]["guest"]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "g2.json"
            path.write_text(json.dumps(data))
            with self.assertRaisesRegex(G2ContractError, "guest interrupts"):
                load_g2_contract(path, G1)

        data = json.loads(G2.read_text())
        data["interrupt_routes"][0]["guest"] = 865
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "g2.json"
            path.write_text(json.dumps(data))
            with self.assertRaisesRegex(G2ContractError, "reserved guest"):
                load_g2_contract(path, G1)

    def test_gpu_virtual_regions_cannot_be_exposed_as_acpi_mmio(self):
        data = json.loads(G2.read_text())
        data["acpi_mmio_regions"].append("shared")
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "g2.json"
            path.write_text(json.dumps(data))
            with self.assertRaisesRegex(G2ContractError, "ACPI MMIO regions"):
                load_g2_contract(path, G1)

    def test_asc_is_an_alias_not_a_second_overlapping_acpi_resource(self):
        contract = load_g2_contract(G2, G1)
        self.assertEqual(len(contract.acpi_mmio), 1)
        _, aperture_base, aperture_size = contract.acpi_mmio[0]
        _, asc_base, asc_size = contract.mmio_subregions[0]
        self.assertGreaterEqual(asc_base, aperture_base)
        self.assertLessEqual(asc_base + asc_size, aperture_base + aperture_size)

    def test_power_broker_is_fixed_page_in_the_reviewed_guest_hole(self):
        data = json.loads(G2.read_text())
        for key, bad in (("base", 0x204000000), ("base", 0x300001000),
                         ("size", 0x2000)):
            mutated = json.loads(json.dumps(data))
            mutated["synthetic_mmio_regions"]["power_broker"][key] = bad
            with tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp) / "g2.json"
                path.write_text(json.dumps(mutated))
                with self.assertRaisesRegex(G2ContractError, "power broker"):
                    load_g2_contract(path, G1)

    def test_runtime_context_and_queue_are_fixed(self):
        for key, bad in (("context_id", 0), ("queue_index", 2),
                         ("work_timeout_ms", 501)):
            data = json.loads(G2.read_text())
            data["runtime"][key] = bad
            with tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp) / "g2.json"
                path.write_text(json.dumps(data))
                with self.assertRaisesRegex(G2ContractError, key):
                    load_g2_contract(path, G1)


if __name__ == "__main__":
    unittest.main()
