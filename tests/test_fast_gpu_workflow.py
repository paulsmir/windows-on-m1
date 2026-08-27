import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CURRENT_STATE = ROOT / "investigation" / "CURRENT_STATE.md"


class FastGpuWorkflowTests(unittest.TestCase):
    def test_current_state_is_compact_and_actionable(self):
        self.assertTrue(CURRENT_STATE.is_file(), "CURRENT_STATE.md is required")
        text = CURRENT_STATE.read_text(encoding="utf-8")
        for heading in (
            "## Stable recovery",
            "## Repository identity",
            "## Live machine",
            "## Last confirmed boundary",
            "## Active hypothesis",
            "## Single next action",
            "## Rollback",
            "## Context budget",
        ):
            self.assertIn(heading, text)
        self.assertIn("EXP-123", text)
        self.assertIn("EXP-20260827-136", text)
        self.assertLessEqual(len(text.splitlines()), 180)


if __name__ == "__main__":
    unittest.main()
