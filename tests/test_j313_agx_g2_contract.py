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
        lifecycle = contract.firmware_lifecycle
        self.assertEqual(lifecycle.management_endpoint, 0)
        self.assertEqual(lifecycle.firmware_endpoint, 0x20)
        self.assertEqual(lifecycle.doorbell_endpoint, 0x21)
        self.assertEqual(lifecycle.iop_boot_request_state, 0x220)
        self.assertEqual(lifecycle.running_state, 0x20)
        self.assertEqual(lifecycle.stopped_state, 0x10)
        self.assertEqual(lifecycle.asc_cpu_control_offset, 0x44)
        self.assertEqual(lifecycle.asc_cpu_status_offset, 0x48)
        self.assertEqual(lifecycle.asc_inbox_control_offset, 0x8110)
        self.assertEqual(lifecycle.asc_outbox_control_offset, 0x8114)
        self.assertEqual(lifecycle.asc_inbox0_offset, 0x8800)
        self.assertEqual(lifecycle.asc_inbox1_offset, 0x8808)
        self.assertEqual(lifecycle.asc_outbox0_offset, 0x8830)
        self.assertEqual(lifecycle.asc_outbox1_offset, 0x8838)
        self.assertEqual(lifecycle.asc_boot_timeout_ms, 3000)
        self.assertEqual(lifecycle.endpoint_timeout_ms, 500)
        self.assertEqual(lifecycle.initdata_timeout_ms, 500)
        self.assertEqual(lifecycle.heartbeat_timeout_ms, 500)
        self.assertEqual(lifecycle.stop_timeout_ms, 1000)
        self.assertEqual(contract.acpi_mmio, (
            ("sgx_mmio", 0x204000000, 0x4000000),
        ))
        self.assertEqual(contract.mmio_subregions, (
            ("asc_mmio", 0x206400000, 0x6C000),
        ))
        self.assertEqual(contract.synthetic_mmio, (
            ("power_broker", 0x300000000, 0x1000),
        ))
        self.assertEqual(contract.firmware_regions, (
            ("gpu", 0x9FFFB8000, 0x4000),
            ("shared", 0x9FFF78000, 0x40000),
            ("handoff", 0x9FFF70000, 0x4000),
            ("rtkit_private", 0xFFFFFF8000000000, 0x2000000000),
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
        for line in (
            "#define J313_AGX_G2_ASC_CPU_CONTROL_OFFSET 0x44u",
            "#define J313_AGX_G2_ASC_CPU_STATUS_OFFSET 0x48u",
            "#define J313_AGX_G2_ASC_INBOX_CTRL_OFFSET 0x8110u",
            "#define J313_AGX_G2_ASC_OUTBOX_CTRL_OFFSET 0x8114u",
            "#define J313_AGX_G2_ASC_INBOX0_OFFSET 0x8800u",
            "#define J313_AGX_G2_ASC_INBOX1_OFFSET 0x8808u",
            "#define J313_AGX_G2_ASC_OUTBOX0_OFFSET 0x8830u",
            "#define J313_AGX_G2_ASC_OUTBOX1_OFFSET 0x8838u",
            "#define J313_AGX_G2_MANAGEMENT_ENDPOINT 0x0u",
            "#define J313_AGX_G2_FIRMWARE_ENDPOINT 0x20u",
            "#define J313_AGX_G2_DOORBELL_ENDPOINT 0x21u",
            "#define J313_AGX_G2_IOP_BOOT_REQUEST_STATE 0x220u",
            "#define J313_AGX_G2_RUNNING_STATE 0x20u",
            "#define J313_AGX_G2_STOPPED_STATE 0x10u",
            "#define J313_AGX_G2_ASC_BOOT_TIMEOUT_MS 3000u",
            "#define J313_AGX_G2_ENDPOINT_TIMEOUT_MS 500u",
            "#define J313_AGX_G2_INITDATA_TIMEOUT_MS 500u",
            "#define J313_AGX_G2_HEARTBEAT_TIMEOUT_MS 500u",
            "#define J313_AGX_G2_STOP_TIMEOUT_MS 1000u",
            "#define J313_AGX_G2_UAT_INPUT_ADDRESS_BITS 39u",
            "#define J313_AGX_G2_UAT_OUTPUT_ADDRESS_BITS 40u",
            "#define J313_AGX_G2_UAT_PAGE_BITS 14u",
            "#define J313_AGX_G2_UAT_LEVEL_COUNT 3u",
            "#define J313_AGX_G2_UAT_LEVEL0_SHIFT 36u",
            "#define J313_AGX_G2_UAT_LEVEL0_ENTRIES 8u",
            "#define J313_AGX_G2_UAT_LEVEL1_SHIFT 25u",
            "#define J313_AGX_G2_UAT_LEVEL1_ENTRIES 2048u",
            "#define J313_AGX_G2_UAT_LEVEL2_SHIFT 14u",
            "#define J313_AGX_G2_UAT_LEVEL2_ENTRIES 2048u",
            "#define J313_AGX_G2_UAT_CONTEXT_COUNT 64u",
            "#define J313_AGX_G2_UAT_FIRMWARE_CONTEXT 0u",
            "#define J313_AGX_G2_UAT_RENDER_CONTEXT_MIN 1u",
            "#define J313_AGX_G2_UAT_RENDER_CONTEXT_MAX 62u",
            "#define J313_AGX_G2_UAT_QUALIFICATION_CONTEXT 63u",
            "#define J313_AGX_G2_INITDATA_SIZE 0xbcu",
            "#define J313_AGX_G2_INITDATA_VERSION_WORD0 0x6ba0u",
            "#define J313_AGX_G2_INITDATA_VERSION_WORD1 0x1f28u",
            "#define J313_AGX_G2_INITDATA_VERSION_WORD2 0x601u",
            "#define J313_AGX_G2_INITDATA_VERSION_WORD3 0xb0u",
            "#define J313_AGX_G2_GPU_BASE 0x9fffb8000ULL",
            "#define J313_AGX_G2_GPU_SIZE 0x4000ULL",
            "#define J313_AGX_G2_SHARED_BASE 0x9fff78000ULL",
            "#define J313_AGX_G2_SHARED_SIZE 0x40000ULL",
            "#define J313_AGX_G2_HANDOFF_BASE 0x9fff70000ULL",
            "#define J313_AGX_G2_HANDOFF_SIZE 0x4000ULL",
            "#define J313_AGX_G2_RTKIT_PRIVATE_BASE 0xffffff8000000000ULL",
            "#define J313_AGX_G2_RTKIT_PRIVATE_SIZE 0x2000000000ULL",
            "#define J313_AGX_G2_KERNEL_VA_BASE 0xffffffa000000000ULL",
            "#define J313_AGX_G2_INITDATA_REGION_A_SIZE 0x4000u",
            "#define J313_AGX_G2_INITDATA_REGION_B_SIZE 0x6bc0u",
            "#define J313_AGX_G2_INITDATA_REGION_C_SIZE 0x12394u",
            "#define J313_AGX_G2_INITDATA_FW_STATUS_SIZE 0x80u",
            "#define J313_AGX_G2_FWCTL_STATE_SIZE 0x30u",
            "#define J313_AGX_G2_FWCTL_MESSAGE_SIZE 0x14u",
            "#define J313_AGX_G2_FWCTL_RING_ENTRY_COUNT 0x100u",
            "#define J313_AGX_G2_FWCTL_RING_SIZE 0x1400u",
            "#define J313_AGX_G2_CHANNEL_INFO_SIZE 0x10u",
            "#define J313_AGX_G2_CHANNEL_INFO_COUNT 0x11u",
            "#define J313_AGX_G2_CHANNEL_INFO_SET_SIZE 0x110u",
            "#define J313_AGX_G2_CMD_QUEUE_CHANNEL_COUNT 0xcu",
            "#define J313_AGX_G2_CMD_QUEUE_RING_SIZE 0x3000u",
            "#define J313_AGX_G2_DEVCTRL_RING_SIZE 0x3000u",
            "#define J313_AGX_G2_EVENT_RING_SIZE 0x3800u",
            "#define J313_AGX_G2_FWLOG_RING_COUNT 0x6u",
            "#define J313_AGX_G2_FWLOG_RING_SIZE 0x51000u",
            "#define J313_AGX_G2_KTRACE_RING_SIZE 0x7000u",
            "#define J313_AGX_G2_STATS_RING_SIZE 0x4000u",
            "#define J313_AGX_G2_REGIONB_STATS_TA_SIZE 0x690u",
            "#define J313_AGX_G2_REGIONB_STATS_3D_SIZE 0x748u",
            "#define J313_AGX_G2_REGIONB_STATS_CP_SIZE 0x1180u",
            "#define J313_AGX_G2_REGIONB_HWDATA_A_SIZE 0x421cu",
            "#define J313_AGX_G2_REGIONB_FAULT_INFO_SIZE 0x80u",
            "#define J313_AGX_G2_REGIONB_TIMESTAMP_SIZE 0xc0u",
            "#define J313_AGX_G2_REGIONB_HWDATA_B_SIZE 0x1884u",
            "#define J313_AGX_G2_REGIONB_BUFFER_MGR_CTL_SIZE 0x7f0u",
        ):
            self.assertIn(line, rendered)

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

    def test_firmware_lifecycle_values_are_exact(self):
        exact = {
            "management_endpoint": 0,
            "firmware_endpoint": 0x20,
            "doorbell_endpoint": 0x21,
            "iop_boot_request_state": 0x220,
            "running_state": 0x20,
            "stopped_state": 0x10,
            "asc_cpu_control_offset": 0x44,
            "asc_cpu_status_offset": 0x48,
            "asc_inbox_control_offset": 0x8110,
            "asc_outbox_control_offset": 0x8114,
            "asc_inbox0_offset": 0x8800,
            "asc_inbox1_offset": 0x8808,
            "asc_outbox0_offset": 0x8830,
            "asc_outbox1_offset": 0x8838,
            "asc_boot_timeout_ms": 3000,
            "endpoint_timeout_ms": 500,
            "initdata_timeout_ms": 500,
            "heartbeat_timeout_ms": 500,
            "stop_timeout_ms": 1000,
        }
        for key, value in exact.items():
            data = json.loads(G2.read_text())
            data["firmware_lifecycle"][key] = value + 1
            with tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp) / "g2.json"
                path.write_text(json.dumps(data))
                with self.assertRaisesRegex(G2ContractError, key):
                    load_g2_contract(path, G1)

    def test_firmware_lifecycle_rejects_unknown_key(self):
        data = json.loads(G2.read_text())
        data["firmware_lifecycle"]["unknown"] = 1
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "g2.json"
            path.write_text(json.dumps(data))
            with self.assertRaisesRegex(G2ContractError,
                                        "firmware_lifecycle keys"):
                load_g2_contract(path, G1)


if __name__ == "__main__":
    unittest.main()
