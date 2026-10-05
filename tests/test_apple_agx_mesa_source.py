from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "drivers" / "apple-agx" / "mesa" / "mesa-source.lock.json"
TOOL = ROOT / "tools" / "verify_apple_agx_mesa_source.py"
CHECKOUT = ROOT / ".local" / "reference" / "mesa"


class AppleAgxMesaSourceTests(unittest.TestCase):
    def run_verifier(self, lock=LOCK):
        return subprocess.run(
            [
                sys.executable,
                str(TOOL),
                "--lock",
                str(lock),
                "--source",
                str(CHECKOUT),
            ],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )

    def test_pinned_mesa_checkout_and_mit_sources(self):
        """Catches accepting a different Mesa tree or incomplete source set."""
        run = self.run_verifier()
        self.assertEqual(run.returncode, 0, run.stderr)
        result = json.loads(run.stdout)
        self.assertEqual(
            result["commit"],
            "9aa1215f878b504f66159dd2ead4c7973142126e",
        )
        self.assertEqual(
            result["license_sha256"],
            "323c587d0ccf10e376f8bf9a7f31fb4ca6078105194b42e0b1e0ee2bc9bde71f",
        )
        self.assertEqual(result["required_paths"], sorted(result["required_paths"]))

    def test_wrong_commit_in_lock_is_rejected(self):
        """Catches silently building a checkout other than the locked commit."""
        data = {
            "repository": "https://gitlab.freedesktop.org/mesa/mesa.git",
            "commit": "0" * 40,
            "license_path": "licenses/MIT",
            "license_sha256": (
                "323c587d0ccf10e376f8bf9a7f31fb4ca6078105194b42e0b1e0ee2bc9bde71f"
            ),
            "required_paths": ["licenses/MIT"],
        }
        with tempfile.TemporaryDirectory() as tmp:
            wrong = Path(tmp) / "wrong.lock.json"
            wrong.write_text(json.dumps(data))
            run = self.run_verifier(wrong)
            self.assertNotEqual(run.returncode, 0)
            self.assertIn("commit", run.stderr.lower())


if __name__ == "__main__":
    unittest.main()
