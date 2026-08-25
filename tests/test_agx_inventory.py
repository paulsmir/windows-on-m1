import unittest

from tools.agx_contract import ContractError
from tools.agx_inventory import extract_contract, required_paths


def source_commits():
    return {
        "root_commit": "1" * 40,
        "m1n1_commit": "2" * 40,
        "mu_commit": "3" * 40,
    }


def raw_inventory():
    return {
        "format_version": 1,
        "platform": "J313",
        "adt_identity": "j313-test-adt",
        "firmware": {"generation": "G13", "version": "V12_3"},
        "uat": {
            "page_size": 0x4000,
            "num_contexts": 64,
            "address_bits": 40,
        },
        "nodes": {
            "/arm-io/sgx": {
                "reg": [[0x204000000, 0x1000000]],
                "interrupts": [180, 181, 182],
                "properties": {
                    "rtkit-private-vm-region-base": 0x500000000,
                    "rtkit-private-vm-region-size": 0x40000,
                    "gpu-region-base": 0x500040000,
                    "gpu-region-size": 0x40000,
                    "gfx-shared-region-base": 0x500080000,
                    "gfx-shared-region-size": 0x40000,
                    "gfx-handoff-base": 0x5000C0000,
                    "gfx-handoff-size": 0x40000,
                },
            },
            "/arm-io/gfx-asc": {
                "reg": [[0x205000000, 0x4000]],
                "interrupts": [],
                "properties": {},
            },
            "/arm-io/dart-sgx": {
                "reg": [[0x206000000, 0x4000]],
                "interrupts": [],
                "properties": {},
            },
            "/arm-io/dcp": {
                "reg": [[0x207000000, 0x4000]],
                "interrupts": [],
                "properties": {},
            },
        },
        "dependencies": ["/arm-io/gfx-asc", "/arm-io/sgx"],
    }


class AgxInventoryTests(unittest.TestCase):
    def test_required_paths_are_explicit_and_stable(self):
        self.assertEqual(required_paths(), ("/arm-io/sgx", "/arm-io/gfx-asc"))

    def test_extracts_required_regions_and_interrupts(self):
        contract = extract_contract(raw_inventory(), source_commits())
        self.assertEqual(contract.platform, "J313")
        self.assertEqual(contract.regions["sgx_mmio"].base, 0x204000000)
        self.assertEqual(contract.regions["asc_mmio"].size, 0x4000)
        self.assertEqual(contract.regions["gpu"].size, 0x40000)
        self.assertEqual(contract.interrupts, (180, 181, 182))
        self.assertEqual(contract.dependencies, tuple(raw_inventory()["dependencies"]))
        self.assertEqual(contract.nodes, tuple(raw_inventory()["nodes"]))

    def test_missing_handoff_size_is_rejected(self):
        raw = raw_inventory()
        del raw["nodes"]["/arm-io/sgx"]["properties"]["gfx-handoff-size"]
        with self.assertRaisesRegex(ContractError, "gfx-handoff-size"):
            extract_contract(raw, source_commits())

    def test_missing_required_node_is_rejected(self):
        raw = raw_inventory()
        del raw["nodes"]["/arm-io/gfx-asc"]
        with self.assertRaisesRegex(ContractError, "/arm-io/gfx-asc"):
            extract_contract(raw, source_commits())

    def test_multiple_register_tuples_are_rejected(self):
        raw = raw_inventory()
        raw["nodes"]["/arm-io/sgx"]["reg"].append([0x208000000, 0x4000])
        with self.assertRaisesRegex(ContractError, "exactly one reg"):
            extract_contract(raw, source_commits())

    def test_unknown_raw_top_level_key_is_rejected(self):
        raw = raw_inventory()
        raw["guess"] = 1
        with self.assertRaisesRegex(ContractError, "raw inventory keys"):
            extract_contract(raw, source_commits())


if __name__ == "__main__":
    unittest.main()
