import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.agx_contract import ContractError, canonical_bytes, load_contract
from tools.agx_inventory import extract_contract, required_paths


ROOT = Path(__file__).resolve().parents[1]
REVIEWED_RAW = ROOT / "tests" / "fixtures" / "j313-agx-adt.json"
REVIEWED_CONTRACT = ROOT / "config" / "j313-agx.json"


def source_commits():
    return {
        "root_commit": "1" * 40,
        "m1n1_commit": "2" * 40,
        "fixture_m1n1_commit": "4" * 40,
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
                "reg": [[0x204000000, 0x2000000]],
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
                "interrupts": [183, 184],
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
    def test_reviewed_raw_fixture_reproduces_reviewed_contract(self):
        reviewed = load_contract(REVIEWED_CONTRACT)
        raw = json.loads(REVIEWED_RAW.read_text())
        extracted = extract_contract(
            raw,
            {
                "root_commit": reviewed.source.root_commit,
                "m1n1_commit": reviewed.source.m1n1_commit,
                "fixture_m1n1_commit": reviewed.source.fixture_m1n1_commit,
                "mu_commit": reviewed.source.mu_commit,
            },
        )
        self.assertEqual(canonical_bytes(extracted), REVIEWED_CONTRACT.read_bytes())

    def test_required_paths_are_explicit_and_stable(self):
        self.assertEqual(required_paths(), ("/arm-io/sgx", "/arm-io/gfx-asc"))

    def test_extracts_required_regions_and_interrupts(self):
        contract = extract_contract(raw_inventory(), source_commits())
        self.assertEqual(contract.platform, "J313")
        self.assertEqual(contract.regions["sgx_mmio"].base, 0x204000000)
        self.assertEqual(contract.regions["asc_mmio"].size, 0x4000)
        self.assertEqual(contract.regions["gpu"].size, 0x40000)
        self.assertEqual(contract.interrupts, (180, 181, 182, 183, 184))
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

    def test_first_register_tuple_is_the_primary_mmio_range(self):
        raw = raw_inventory()
        raw["nodes"]["/arm-io/sgx"]["reg"].append([0x208000000, 0x4000])
        contract = extract_contract(raw, source_commits())
        self.assertEqual(contract.regions["sgx_mmio"].base, 0x204000000)

    def test_missing_primary_register_tuple_is_rejected(self):
        raw = raw_inventory()
        raw["nodes"]["/arm-io/sgx"]["reg"] = []
        with self.assertRaisesRegex(ContractError, "at least one reg"):
            extract_contract(raw, source_commits())

    def test_unknown_raw_top_level_key_is_rejected(self):
        raw = raw_inventory()
        raw["guess"] = 1
        with self.assertRaisesRegex(ContractError, "raw inventory keys"):
            extract_contract(raw, source_commits())

    def test_cli_writes_the_canonical_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            input_path = Path(tmp) / "raw.json"
            output_path = Path(tmp) / "contract.json"
            input_path.write_text(json.dumps(raw_inventory()))
            source = source_commits()
            subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "tools.agx_inventory",
                    "--input",
                    str(input_path),
                    "--output",
                    str(output_path),
                    "--root-commit",
                    source["root_commit"],
                    "--m1n1-commit",
                    source["m1n1_commit"],
                    "--fixture-m1n1-commit",
                    source["fixture_m1n1_commit"],
                    "--mu-commit",
                    source["mu_commit"],
                ],
                check=True,
                cwd=Path(__file__).resolve().parents[1],
            )
            written = output_path.read_text()
        self.assertTrue(written.endswith("\n"))
        self.assertEqual(json.loads(written)["platform"], "J313")
        self.assertEqual(json.loads(written)["source"]["root_commit"], "1" * 40)


if __name__ == "__main__":
    unittest.main()
