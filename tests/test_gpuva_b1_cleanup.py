import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class B1CleanupReceiptTests(unittest.TestCase):
    def test_cleanup_receipts_are_durable_and_keep_first_failure(self):
        source = (RENDER / "src" / "receipts.c").read_text()
        start = source.index("void AdmissionRecordB1Cleanup(")
        body = source[start:source.index("void AdmissionRecordB1Qualification(", start)]
        self.assertIn('L"Wom1B1Cleanup%02u"', body)
        self.assertIn("WriteBinary(key, name, Receipt, sizeof(*Receipt))", body)
        self.assertIn("ZwFlushKey(key)", body)
        cleanup = (RENDER / "src" / "gpuva_b1_windows.c").read_text()
        self.assertIn("RootMapped", cleanup)
        self.assertIn("RootGrants", cleanup)
        self.assertIn("RootParents", cleanup)
        self.assertIn("RootTables", cleanup)
        self.assertIn("FirstFailure", cleanup)
        self.assertIn("AdmissionRecordB1Cleanup(context", cleanup)

    def test_context0_table_pages_are_hashed_at_job_boundary(self):
        cleanup = (RENDER / "src" / "gpuva_b1_windows.c").read_text()
        before = cleanup.index("AdmissionB1Context0Hash(context, 0u")
        job = cleanup.index("AdmissionB1LeaseJob(context, state, 0u")
        after = cleanup.index("AdmissionB1Context0Hash(context, 1u")
        self.assertLess(before, job)
        self.assertGreater(after, cleanup.index("AdmissionB1Cleanup(context, state)"))
        self.assertIn("after_hash != state->Context0HashBefore", cleanup)
        source = (RENDER / "src" / "receipts.c").read_text()
        self.assertIn('L"Wom1B1Context0HashBefore"', source)
        self.assertIn('L"Wom1B1Context0HashAfter"', source)


if __name__ == "__main__":
    unittest.main()
