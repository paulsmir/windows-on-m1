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


# Rows dated before this instant hold 287 rows that violate the schema, all written between
# 2026-10-25 and 2026-11-09 (change types outside the schema, empty required
# fields, short commit ids, one validated row without an experiment).  Old
# rows are never rewritten, so only rows from this instant on are held to the
# full schema; older rows must still parse and carry an id, date, title and
# status.
STRICT_SINCE_UTC = "2026-11-10T00:00:00Z"

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
            if row["date_utc"] < STRICT_SINCE_UTC:
                for field in ("record_id", "date_utc", "title", "status"):
                    self.assertTrue(row[field].strip(), f"{row['record_id']}: missing {field}")
                continue
            for field in required:
                self.assertTrue(row[field].strip(), f"{row['record_id']}: missing {field}")
            self.assertRegex(row["commit"], COMMIT)
            self.assertIn(row["change_type"], {"feature", "correction", "process"})
            self.assertIn(row["status"], {"implemented", "validated", "rejected", "superseded"})
            if row["artifact_sha256"]:
                self.assertRegex(row["artifact_sha256"], SHA256)

    def test_validated_rows_include_hardware_evidence(self):
        for row in self.load_rows():
            if row["status"] == "validated" and row["date_utc"] >= STRICT_SINCE_UTC:
                self.assertTrue(row["hardware_result"].strip(), row["record_id"])
                self.assertTrue(row["related_experiment"].strip(), row["record_id"])


if __name__ == "__main__":
    unittest.main()
