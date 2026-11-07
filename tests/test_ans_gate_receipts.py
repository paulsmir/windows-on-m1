"""A later gate run must not destroy evidence already referenced by the ledger."""
import json
from pathlib import Path
import tempfile
import unittest
from tools import test_ans_offline as runner

class AnsGateReceiptTests(unittest.TestCase):
    def test_distinct_runs_preserve_both_original_receipts(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            first=runner.persist_receipt(root, {'run': 1, 'passed': True})
            original=first.read_bytes()
            second=runner.persist_receipt(root, {'run': 2, 'passed': True})
            self.assertNotEqual(first, second)
            self.assertEqual(first.read_bytes(), original)
            self.assertEqual(json.loads(second.read_bytes())['run'], 2)
            self.assertEqual((root/'receipt.json').read_bytes(), second.read_bytes())

    def test_existing_archive_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); record={'passed': True}
            archive=runner.persist_receipt(root, record)
            archive.write_bytes(b'tampered')
            with self.assertRaises(RuntimeError):
                runner.persist_receipt(root, record)
            self.assertEqual(archive.read_bytes(), b'tampered')

if __name__ == '__main__': unittest.main()
