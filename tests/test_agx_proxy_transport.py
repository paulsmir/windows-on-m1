from pathlib import Path
import json
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/agx-capture-container/probe-proxy-identity.py"


class ProxyTransportReceiptTests(unittest.TestCase):
    def write_identity(self, path: Path, *, base: int, platform="J313") -> None:
        path.write_text(json.dumps({
            "format_version": 1,
            "platform": platform,
            "firmware": "V13_5",
            "m1n1_base": base,
        }))

    def run_receipt(self, before: Path, after: Path, output: Path):
        return subprocess.run(
            [
                str(TOOL), "receipt",
                "--before", str(before),
                "--after", str(after),
                "--output", str(output),
            ],
            text=True,
            capture_output=True,
            check=False,
        )

    def test_changed_base_produces_atomic_fresh_proxy_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before, after, output = root / "before.json", root / "after.json", root / "receipt.json"
            self.write_identity(before, base=0x804000000)
            self.write_identity(after, base=0x805000000)
            result = self.run_receipt(before, after, output)
            receipt = json.loads(output.read_text())

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(receipt["fresh_proxy"])
        self.assertEqual(receipt["before"]["m1n1_base"], 0x804000000)
        self.assertEqual(receipt["after"]["m1n1_base"], 0x805000000)

    def test_same_base_is_rejected_without_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before, after, output = root / "before.json", root / "after.json", root / "receipt.json"
            self.write_identity(before, base=0x804000000)
            self.write_identity(after, base=0x804000000)
            result = self.run_receipt(before, after, output)
            exists = output.exists()

        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(exists)
        self.assertIn("base did not change", result.stderr)

    def test_platform_change_is_rejected_without_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before, after, output = root / "before.json", root / "after.json", root / "receipt.json"
            self.write_identity(before, base=0x804000000)
            self.write_identity(after, base=0x805000000, platform="J274")
            result = self.run_receipt(before, after, output)
            exists = output.exists()

        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(exists)
        self.assertIn("platform changed", result.stderr)


if __name__ == "__main__":
    unittest.main()
