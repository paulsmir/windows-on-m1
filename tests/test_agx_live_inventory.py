import os
from pathlib import Path
import tempfile
import unittest

from tools.agx_live_inventory import (ensure_guest_inactive, node_record,
                                      platform_name)


ROOT = Path(__file__).resolve().parents[1]
LIVE_INVENTORY = ROOT / "tools" / "agx_live_inventory.py"


class FakeNode:
    _path = "/device-tree/arm-io/sgx"
    _properties = {
        "name": "sgx",
        "gpu-region-base": 0x500040000,
        "gpu-region-size": 0x40000,
    }
    interrupts = [180, 181, 182]
    reg = [object()]

    def get_reg(self, index):
        if index != 0:
            raise IndexError(index)
        return 0x204000000, 0x1000000


class FakeRoot:
    target_type = "J313"


class FakeAdt:
    def __getitem__(self, path):
        if path != "/":
            raise KeyError(path)
        return FakeRoot()


class AgxLiveInventoryTests(unittest.TestCase):
    def test_live_inventory_refuses_active_guest(self):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "guest.pid").write_text(str(os.getpid()))
            with self.assertRaisesRegex(RuntimeError, "guest runner"):
                ensure_guest_inactive(Path(tmp), process_lines=())

    def test_stale_guest_pid_is_safe(self):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "guest.pid").write_text("99999999")
            ensure_guest_inactive(Path(tmp), process_lines=())

    def test_live_inventory_refuses_runner_without_pid_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            command = f"19672 python -u {Path(tmp).resolve()}/run_uefi.py firmware.fd"
            with self.assertRaisesRegex(RuntimeError, "active guest process 19672"):
                ensure_guest_inactive(Path(tmp), process_lines=(command,))

    def test_node_record_preserves_exact_read_only_values(self):
        self.assertEqual(
            node_record(FakeNode()),
            {
                "reg": [[0x204000000, 0x1000000]],
                "interrupts": [180, 181, 182],
                "properties": {
                    "gpu-region-base": 0x500040000,
                    "gpu-region-size": 0x40000,
                },
            },
        )

    def test_platform_uses_device_tree_root_target_type(self):
        self.assertEqual(platform_name(FakeAdt()), "J313")

    def test_live_inventory_source_has_no_write_capable_api(self):
        source = LIVE_INVENTORY.read_text()
        self.assertIn("def ensure_guest_inactive(root, process_lines=None):", source)
        self.assertIn("ensure_guest_inactive(ROOT)\n    data = capture_raw()", source)
        self.assertNotIn("\nfrom m1n1.setup import u", source)
        self.assertIn("    from m1n1.setup import u", source)
        for forbidden in (
            "u.proxy",
            "import p",
            "p.",
            "write32",
            "write64",
            "writemem",
            "pmgr_adt_clocks_enable",
            "iomap",
            "DART",
            "AGX(",
        ):
            self.assertNotIn(forbidden, source)


if __name__ == "__main__":
    unittest.main()
