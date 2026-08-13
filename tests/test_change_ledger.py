import csv
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LEDGER = ROOT / "investigation" / "CHANGES.csv"
EXPECTED_COLUMNS = (
    "record_id",
    "date_utc",
    "repository",
    "branch",
    "commit",
    "change_type",
    "title",
    "description",
    "reason",
    "problem_fixed",
    "reproduction",
    "implementation",
    "verification",
    "hardware_result",
    "artifact",
    "artifact_sha256",
    "status",
    "related_experiment",
    "notes",
)
COMMIT = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")


class ChangeLedgerTests(unittest.TestCase):
    def load_rows(self):
        self.assertTrue(LEDGER.is_file(), "investigation/CHANGES.csv is required")
        with LEDGER.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            self.assertEqual(EXPECTED_COLUMNS, tuple(reader.fieldnames or ()))
            return list(reader)

    def test_rows_are_machine_readable_and_complete(self):
        rows = self.load_rows()
        self.assertGreaterEqual(len(rows), 2)
        self.assertEqual(len(rows), len({row["record_id"] for row in rows}))

        required = (
            "record_id",
            "date_utc",
            "repository",
            "branch",
            "commit",
            "change_type",
            "title",
            "description",
            "reason",
            "problem_fixed",
            "reproduction",
            "implementation",
            "verification",
            "status",
        )
        for row in rows:
            for field in required:
                self.assertTrue(row[field].strip(), f"{row['record_id']}: missing {field}")
            self.assertRegex(row["commit"], COMMIT)
            self.assertIn(row["change_type"], {"feature", "correction", "process"})
            self.assertIn(row["status"], {"implemented", "validated", "rejected", "superseded"})
            if row["artifact_sha256"]:
                self.assertRegex(row["artifact_sha256"], SHA256)

    def test_validated_rows_include_hardware_evidence(self):
        for row in self.load_rows():
            if row["status"] == "validated":
                self.assertTrue(row["hardware_result"].strip(), row["record_id"])
                self.assertTrue(row["related_experiment"].strip(), row["record_id"])


if __name__ == "__main__":
    unittest.main()
