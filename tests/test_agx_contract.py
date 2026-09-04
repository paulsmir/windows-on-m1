import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools.agx_contract import (ContractError, canonical_bytes,
                                contract_sha256, load_contract,
                                validate_contract)


ROOT = Path(__file__).resolve().parents[1]
REVIEWED_CONTRACT = ROOT / "config" / "j313-agx.json"


def valid_contract_dict():
    return {
        "contract_version": 2,
        "platform": "J313",
        "source": {
            "root_commit": "1" * 40,
            "m1n1_commit": "2" * 40,
            "fixture_m1n1_commit": "4" * 40,
            "mu_commit": "3" * 40,
            "adt_identity": "j313-test-adt",
        },
        "firmware": {
            "generation": "G13",
            "version": "V12_3",
        },
        "nodes": [
            "/arm-io/sgx",
            "/arm-io/gfx-asc",
            "/arm-io/dart-sgx",
            "/arm-io/dcp",
        ],
        "regions": {
            "sgx_mmio": {"base": 0x204000000, "size": 0x2000000},
            "asc_mmio": {"base": 0x205000000, "size": 0x4000},
            "rtkit_private": {"base": 0x500000000, "size": 0x40000},
            "gpu": {"base": 0x500040000, "size": 0x40000},
            "shared": {"base": 0x500080000, "size": 0x40000},
            "handoff": {"base": 0x5000C0000, "size": 0x40000},
        },
        "interrupts": [180, 181, 182],
        "dependencies": ["/arm-io/gfx-asc", "/arm-io/sgx"],
        "uat": {
            "page_size": 0x4000,
            "num_contexts": 64,
            "address_bits": 40,
        },
    }


class AgxContractTests(unittest.TestCase):
    def test_reviewed_j313_contract_has_exact_live_resources(self):
        contract = load_contract(REVIEWED_CONTRACT)
        self.assertNotEqual(
            contract.source.m1n1_commit,
            contract.source.fixture_m1n1_commit,
        )
        self.assertEqual(contract.firmware.generation, "G13")
        self.assertEqual(contract.firmware.version, "V13_5")
        self.assertEqual(
            {
                name: (region.base, region.size)
                for name, region in contract.regions.items()
            },
            {
                "sgx_mmio": (0x204000000, 0x4000000),
                "asc_mmio": (0x206400000, 0x6C000),
                "rtkit_private": (0xFFFFFF8000000000, 0x2000000000),
                "gpu": (0x9FFFB8000, 0x4000),
                "shared": (0x9FFF78000, 0x40000),
                "handoff": (0x9FFF70000, 0x4000),
            },
        )
        self.assertEqual(
            contract.interrupts,
            (563, 564, 565, 566, 579, 576, 575, 578, 577),
        )
        self.assertEqual(contract.uat.page_size, 0x4000)
        self.assertEqual(contract.uat.num_contexts, 64)
        self.assertEqual(contract.uat.address_bits, 40)

    def test_valid_contract_round_trips_canonically(self):
        contract = validate_contract(valid_contract_dict())
        encoded = canonical_bytes(contract)
        self.assertTrue(encoded.endswith(b"\n"))
        self.assertEqual(
            encoded,
            canonical_bytes(validate_contract(json.loads(encoded))),
        )
        self.assertEqual(
            contract_sha256(contract), hashlib.sha256(encoded).hexdigest()
        )

    def test_load_contract_reads_canonical_shape(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "j313-agx.json"
            path.write_bytes(canonical_bytes(validate_contract(valid_contract_dict())))
            loaded = load_contract(path)
        self.assertEqual(loaded.platform, "J313")
        self.assertEqual(loaded.regions["gpu"].size, 0x40000)
        self.assertEqual(loaded.interrupts, (180, 181, 182))

    def test_unknown_top_level_key_is_rejected(self):
        data = valid_contract_dict()
        data["surprise"] = True
        with self.assertRaisesRegex(ContractError, "keys must be exactly"):
            validate_contract(data)

    def test_overlapping_regions_are_rejected(self):
        data = valid_contract_dict()
        data["regions"]["shared"] = data["regions"]["gpu"].copy()
        with self.assertRaisesRegex(ContractError, "overlap"):
            validate_contract(data)

    def test_asc_mmio_is_an_explicit_subrange_of_sgx_aperture(self):
        contract = validate_contract(valid_contract_dict())
        self.assertLessEqual(
            contract.regions["asc_mmio"].base
            + contract.regions["asc_mmio"].size,
            contract.regions["sgx_mmio"].base
            + contract.regions["sgx_mmio"].size,
        )

    def test_asc_mmio_outside_sgx_aperture_is_rejected(self):
        data = valid_contract_dict()
        data["regions"]["asc_mmio"]["base"] = 0x203000000
        with self.assertRaisesRegex(ContractError, "inside sgx_mmio"):
            validate_contract(data)

    def test_misaligned_region_is_rejected(self):
        data = valid_contract_dict()
        data["regions"]["gpu"]["base"] += 0x1000
        with self.assertRaisesRegex(ContractError, "16 KiB aligned"):
            validate_contract(data)

    def test_unsupported_version_is_rejected(self):
        data = valid_contract_dict()
        data["contract_version"] = 3
        with self.assertRaisesRegex(ContractError, "contract_version"):
            validate_contract(data)

    def test_noncanonical_source_commit_is_rejected(self):
        data = valid_contract_dict()
        data["source"]["root_commit"] = "A" * 40
        with self.assertRaisesRegex(ContractError, "root_commit"):
            validate_contract(data)

    def test_duplicate_interrupt_is_rejected(self):
        data = valid_contract_dict()
        data["interrupts"] = [180, 180]
        with self.assertRaisesRegex(ContractError, "unique"):
            validate_contract(data)

    def test_boolean_is_not_accepted_as_integer(self):
        data = valid_contract_dict()
        data["uat"]["num_contexts"] = True
        with self.assertRaisesRegex(ContractError, "num_contexts"):
            validate_contract(data)


if __name__ == "__main__":
    unittest.main()
