"""An armed package can only enter the pinned full-owner launch."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/g3-launchers/cold-after-arm.py"


class ColdAfterArmTest(unittest.TestCase):
    def test_host_receipt_binds_experiment_manifest(self):
        source = SCRIPT.read_text()
        self.assertIn('receipt.get("ExperimentManifestSha256"', source)
        self.assertNotIn('receipt.get("ManifestSha256", "").lower() != sha256(args.manifest)', source)

    def test_transition_rejects_ordinary_and_unverified_launch(self):
        spec = importlib.util.spec_from_file_location("cold_after_arm", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            launcher = root / "full-owner.sh"
            launcher.write_text("#!/bin/sh\nexit 0\n")
            firmware = root / "m1n1.macho"
            firmware.write_bytes(b"full-owner")
            receipt = {"Arm": 1, "OrderedShutdown": "PowerOff",
                       "NextProfile": "cold-full-owner", "InfSha256": "a" * 64}
            artifacts = {"full-owner.sh": module.sha256(launcher),
                         "m1n1.macho": module.sha256(firmware)}
            module.validate_transition(root, receipt, artifacts, "a" * 64)
            for field, value in (("Arm", 0), ("OrderedShutdown", "Restart"),
                                 ("NextProfile", "ordinary"), ("InfSha256", "b" * 64)):
                bad = dict(receipt)
                bad[field] = value
                with self.assertRaises(ValueError):
                    module.validate_transition(root, bad, artifacts, "a" * 64)
            with self.assertRaises(ValueError):
                module.validate_transition(root, receipt,
                                           {**artifacts, "m1n1.macho": "0" * 64},
                                           "a" * 64)


if __name__ == "__main__":
    unittest.main()
