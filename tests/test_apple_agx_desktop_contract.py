import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = ROOT / "tools" / "verify_apple_agx_desktop_contract.py"
LOCK = ROOT / "drivers" / "apple-agx" / "mesa" / "mesa-source.lock.json"
LOCKED_COMMIT = "9aa1215f878b504f66159dd2ead4c7973142126e"


def requirement(identifier="required-draw", implemented=False, tested=False):
    return {
        "id": identifier,
        "class": "UMD",
        "source": ".local/reference/wdk26100/d3d10umddi.h",
        "implementation_file": "drivers/apple-agx/mesa/d3d10umd/Device.cpp",
        "test_id": "desktop-contract-draw",
        "mandatory": True,
        "implemented": implemented,
        "test_passed": tested,
    }


def contract(**changes):
    value = {
        "schema_version": 1,
        "goal": "accelerated-windows-desktop",
        "kmd_model": "WDDM3.0",
        "umd_ddi": "D3D10_0_DDI_INTERFACE_VERSION",
        "feature_level": "D3D_FEATURE_LEVEL_10_0",
        "frontend": "mesa-d3d-reuse",
        "mesa_commit": LOCKED_COMMIT,
        "mandatory_inventory_reviewed": True,
        "advertised_pipeline_mask": 0,
        "selected_pipeline_mask": 1,
        "requirements": [requirement()],
    }
    value.update(changes)
    return value


class AppleAgxDesktopContractTests(unittest.TestCase):
    def run_validator(self, value, lock=LOCK):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "contract.json"
            path.write_text(json.dumps(value), encoding="utf-8")
            return subprocess.run(
                [
                    sys.executable,
                    "-B",
                    str(VALIDATOR),
                    "--contract",
                    str(path),
                    "--mesa-lock",
                    str(lock),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )

    def assert_contract_error(self, result, detail):
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(f"desktop contract error: {detail}", result.stderr)

    def test_valid_incomplete_contract_keeps_pipeline_disabled(self):
        """Catches rejecting an honest incomplete matrix with a zero mask."""
        result = self.run_validator(contract())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            json.loads(result.stdout),
            {
                "advertised_pipeline_mask": 0,
                "implementation_complete": False,
                "mandatory_requirements": 1,
                "selected_pipeline_mask": 1,
            },
        )

    def test_unimplemented_requirement_cannot_advertise_pipeline(self):
        """Catches publishing a feature level while one mandatory row is false."""
        result = self.run_validator(contract(advertised_pipeline_mask=1))
        self.assert_contract_error(result, "pipeline advertised")

    def test_complete_synthetic_matrix_requires_exact_selected_mask(self):
        """Catches accepting an arbitrary nonzero mask for a complete matrix."""
        complete = contract(
            requirements=[requirement(implemented=True, tested=True)],
            advertised_pipeline_mask=2,
        )
        result = self.run_validator(complete)
        self.assert_contract_error(result, "selected pipeline mask")

        complete["advertised_pipeline_mask"] = 1
        result = self.run_validator(complete)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout)["implementation_complete"])

    def test_missing_source_path_is_rejected(self):
        """Catches a matrix row that cannot be traced to its primary source."""
        value = contract()
        value["requirements"][0]["source"] = ""
        result = self.run_validator(value)
        self.assert_contract_error(result, "source")

    def test_duplicate_requirement_is_rejected(self):
        """Catches an inventory whose row count hides a duplicated obligation."""
        value = contract(requirements=[requirement(), requirement()])
        result = self.run_validator(value)
        self.assert_contract_error(result, "duplicate requirement")

    def test_wrong_mesa_commit_is_rejected(self):
        """Catches auditing one Mesa revision and later building another."""
        result = self.run_validator(contract(mesa_commit="0" * 40))
        self.assert_contract_error(result, "mesa_commit")

    def test_empty_mandatory_inventory_is_rejected(self):
        """Catches declaring the audit reviewed without mandatory obligations."""
        optional = requirement()
        optional["mandatory"] = False
        result = self.run_validator(contract(requirements=[optional]))
        self.assert_contract_error(result, "mandatory inventory")

    def test_invalid_boolean_is_rejected(self):
        """Catches truthy strings masquerading as reviewed or implemented state."""
        value = contract(mandatory_inventory_reviewed="true")
        result = self.run_validator(value)
        self.assert_contract_error(result, "mandatory_inventory_reviewed")


if __name__ == "__main__":
    unittest.main()
