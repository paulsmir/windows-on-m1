import json
from pathlib import Path
import tempfile
import unittest

from tools.apple_input_contract import ContractError, load_contract
from tools.apple_input_inventory import (extract_inventory, nodes_with_reg,
                                         select_parent_irq,
                                         select_startup_parent_irq,
                                         write_inventory)
from tools.generate_apple_input_contract import render_asl, render_m1n1, render_windows
from tools.apple_input_live_inventory import json_value


ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT / "config" / "j313-apple-input.json"
FIXTURE = ROOT / "tests" / "fixtures" / "j313-apple-input-adt.json"
M1N1_HEADER = ROOT / "m1n1_windows" / "src" / "hv_apple_input.generated.h"
ASL_INCLUDE = ROOT / "mu" / "Platform" / "MacBookAirMid2020Pkg" / "AcpiTables" / "J313AppleInput.asl.inc"
WINDOWS_HEADER = ROOT / "drivers" / "apple-input" / "windows" / "include" / "j313_apple_input.generated.h"
LIVE_INVENTORY = ROOT / "tools" / "apple_input_live_inventory.py"


class AppleInputContractTests(unittest.TestCase):
    def test_live_function_properties_preserve_values_not_field_names(self):
        class FunctionValue:
            phandle = 108
            name = "GPIO"
            args = [195, 1]

        self.assertEqual(json_value(FunctionValue()), {
            "phandle": 108,
            "name": "GPIO",
            "args": [195, 1],
        })

    def test_live_node_lookup_uses_stable_mmio_identity_not_node_name(self):
        class Node:
            def __init__(self, base, size):
                self.base = base
                self.size = size

            def get_reg(self, index):
                if index:
                    raise IndexError(index)
                return self.base, self.size

        expected = Node(0x23D1F0000, 0x4000)
        nodes = [Node(0x23C100000, 0x100000), expected]
        self.assertEqual(nodes_with_reg(nodes, 0x23D1F0000, 0x4000), [expected])

    def test_j313_contract_has_safe_exact_resources(self):
        c = load_contract(CONTRACT)
        self.assertEqual(c.contract_version, 1)
        self.assertEqual(c.acpi_hid, "APPL0001")
        self.assertEqual(c.spi.base, 0x23510C000)
        self.assertEqual(c.spi.size, 0x4000)
        self.assertEqual(c.spi.bus_hz, 8_000_000)
        self.assertEqual(c.ap_gpio.pin, 195)
        self.assertEqual(c.nub_gpio.pin, 13)
        self.assertEqual(c.interrupt.guest_vintid, 865)
        self.assertEqual(c.interrupt.parent_candidates, tuple(range(330, 337)))
        self.assertEqual(c.interrupt.startup_group, 0)
        self.assertEqual(c.interrupt.startup_parent, 330)

    def test_inventory_fixture_selects_the_pin13_parent_group(self):
        inventory = extract_inventory(json.loads(FIXTURE.read_text()))
        self.assertIn(inventory["selected_parent_irq"], range(330, 337))
        self.assertEqual(inventory["spi"]["compatible"], "spi-1,spimc")
        self.assertEqual(inventory["device"]["compatible"], "hid-transport,spi")

    def test_gpio_pin_group_selects_matching_parent_interrupt(self):
        interrupts = list(range(330, 337))
        pin_register = 3 << 16
        self.assertEqual(select_parent_irq(interrupts, pin_register,
                                           tuple(interrupts)), 333)

    def test_gpio_parent_selection_rejects_unreviewed_or_missing_group(self):
        with self.assertRaises(ValueError):
            select_parent_irq([330, 331], 3 << 16, tuple(range(330, 337)))
        with self.assertRaises(ValueError):
            select_parent_irq([430, 431, 432, 433], 3 << 16,
                              tuple(range(330, 337)))

    def test_gpio_startup_uses_explicit_group_zero_not_stale_pin_group(self):
        interrupts = list(range(330, 337))
        stale_pin_register = 7 << 16
        self.assertEqual(select_startup_parent_irq(interrupts,
                                                   stale_pin_register,
                                                   tuple(interrupts)),
                         (330, 0, 7))

    def test_unknown_contract_keys_are_rejected(self):
        data = json.loads(CONTRACT.read_text())
        data["surprise"] = True
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.json"
            path.write_text(json.dumps(data))
            with self.assertRaises(ContractError):
                load_contract(path)

    def test_unsafe_guest_interrupt_is_rejected(self):
        data = json.loads(CONTRACT.read_text())
        data["interrupt"]["guest_vintid"] = 31
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.json"
            path.write_text(json.dumps(data))
            with self.assertRaises(ContractError):
                load_contract(path)

    def test_inventory_writer_is_deterministic(self):
        inventory = extract_inventory(json.loads(FIXTURE.read_text()))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "inventory.json"
            write_inventory(path, inventory)
            self.assertEqual(json.loads(path.read_text()), inventory)
            self.assertTrue(path.read_text().endswith("\n"))

    def test_checked_in_consumers_are_generated(self):
        c = load_contract(CONTRACT)
        self.assertEqual(render_m1n1(c), M1N1_HEADER.read_text())
        self.assertEqual(render_asl(c), ASL_INCLUDE.read_text())
        self.assertEqual(render_windows(c), WINDOWS_HEADER.read_text())

    def test_c_consumers_do_not_depend_on_stdint_uint64_c(self):
        c = load_contract(CONTRACT)
        for rendered in (render_m1n1(c), render_windows(c)):
            self.assertNotIn("UINT64_C", rendered)
            self.assertIn("0x23510c000ULL", rendered)

    def test_asl_contains_versioned_coherent_resources(self):
        asl = render_asl(load_contract(CONTRACT))
        self.assertIn('Name (_HID, "APPL0001")', asl)
        self.assertIn('Name (_CCA, One)', asl)
        self.assertIn('0x000000023510C000', asl)
        self.assertIn('0x00000361', asl)
        self.assertIn('"physical-parent-irq", 330', asl)

    def test_live_inventory_has_no_write_capable_proxy_api(self):
        source = LIVE_INVENTORY.read_text()
        self.assertIn("def ensure_guest_inactive(root):", source)
        self.assertIn("ensure_guest_inactive(ROOT)\n    data = capture()", source)
        self.assertNotIn("\nfrom m1n1.setup import u", source)
        self.assertIn("    from m1n1.setup import u", source)
        self.assertIn("from tools.apple_input_inventory import (first_reg,", source)
        for forbidden in ("import p", "p.", "writemem", "write32", "write64",
                          "pmgr_adt_clocks_enable", "GPIOTracer", "SPITracer"):
            self.assertNotIn(forbidden, source)

    def test_live_inventory_includes_all_exact_contract_mmio_nodes(self):
        source = LIVE_INVENTORY.read_text()
        self.assertIn('u.adt["/arm-io/gpio0"]', source)
        self.assertIn("contract.ap_gpio.base", source)
        self.assertIn("contract.ap_gpio.size", source)
        self.assertIn("related = [spi, hid, ap_gpio, nub]", source)

    def test_kmdf_build_uses_kernel_compatible_portable_headers(self):
        project = (ROOT / "drivers" / "apple-input" / "windows" /
                   "AppleInput.vcxproj").read_text()
        protocol = (ROOT / "drivers" / "apple-input" / "protocol" /
                    "include" / "apple_spihid.h").read_text()
        hardware = (ROOT / "drivers" / "apple-input" / "windows" /
                    "include" / "apple_input_hw.h").read_text()
        self.assertIn("AI_KERNEL_MODE", project)
        self.assertIn("#ifdef AI_KERNEL_MODE", protocol)
        self.assertIn("#ifdef AI_KERNEL_MODE", hardware)
        self.assertIn("RtlCopyMemory", protocol)
        self.assertIn("AI_KERNEL_FIXED_WIDTH_TYPES", protocol)
        self.assertIn("typedef LONG int32_t;", protocol)
        self.assertLess(protocol.index("typedef LONG int32_t;"),
                        protocol.index("struct ai_trackpad_dimensions"))
        self.assertIn("AI_KERNEL_FIXED_WIDTH_TYPES", hardware)
        self.assertNotIn("#include <string.h>",
                         "\n".join(path.read_text() for path in
                                   (ROOT / "drivers" / "apple-input" /
                                    "protocol" / "src").glob("*.c")))

    def test_wdk_ci_selects_native_x64_msbuild_host(self):
        workflow = (ROOT / ".github" / "workflows" /
                    "apple-input-wdk.yml").read_text()
        self.assertIn("msbuild-architecture: x64", workflow)


if __name__ == "__main__":
    unittest.main()
