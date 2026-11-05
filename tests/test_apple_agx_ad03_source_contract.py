import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT / "drivers/apple-agx/mesa/mesa-ad03-source-contract.json"
VERIFY = ROOT / "tools/verify_apple_agx_ad03_source_contract.py"
MESA = ROOT / ".local/reference/mesa"


class AppleAgxAd03SourceContractTests(unittest.TestCase):
    def run_contract(self, data, expected=0):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "contract.json"
            path.write_text(json.dumps(data))
            result = subprocess.run(
                ["python3", str(VERIFY), "--contract", str(path),
                 "--mesa-root", str(MESA)],
                cwd=ROOT, text=True, capture_output=True,
            )
            self.assertEqual(result.returncode, expected, result.stderr)
            return result

    def test_pinned_frontend_compiler_encoder_are_reused_but_targets_replaced(self):
        """Catches treating Mesa's software/DRM target as Windows AGX hardware."""
        contract = json.loads(CONTRACT.read_text())
        result = self.run_contract(contract)
        summary = json.loads(result.stdout)
        self.assertEqual(summary["mesa_commit"], contract["mesa_commit"])
        self.assertEqual(summary["reused"], 6)
        self.assertEqual(summary["replaced"], 6)
        self.assertFalse(summary["pipeline_advertised"])

    def test_software_target_cannot_be_marked_reusable(self):
        contract = json.loads(CONTRACT.read_text())
        entry = next(x for x in contract["sources"]
                     if x["classification"] == "SOFTWARE_ONLY")
        entry["action"] = "reuse"
        result = self.run_contract(contract, expected=1)
        self.assertIn("SOFTWARE_ONLY action", result.stderr)

    def test_drm_owner_must_be_replaced_by_wddm(self):
        contract = json.loads(CONTRACT.read_text())
        entry = next(x for x in contract["sources"]
                     if x["classification"] == "DRM_ONLY")
        entry["action"] = "reuse"
        result = self.run_contract(contract, expected=1)
        self.assertIn("DRM_ONLY action", result.stderr)

    def test_hash_or_source_signature_drift_fails_closed(self):
        contract = json.loads(CONTRACT.read_text())
        contract["sources"][0]["sha256"] = "00" * 32
        result = self.run_contract(contract, expected=1)
        self.assertIn("source hash", result.stderr)

    def test_nonzero_pipeline_is_forbidden_in_ad03(self):
        contract = json.loads(CONTRACT.read_text())
        contract["pipeline_advertised"] = True
        result = self.run_contract(contract, expected=1)
        self.assertIn("pipeline_advertised", result.stderr)


if __name__ == "__main__":
    unittest.main()
