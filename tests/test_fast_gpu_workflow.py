import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CURRENT_STATE = ROOT / "investigation" / "CURRENT_STATE.md"
CYCLE_RUNNER = (
    ROOT / "drivers" / "apple-agx" / "windows" / "scripts" /
    "cycle-lifecycle-driver.ps1"
)


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

    def test_hot_cycle_waits_for_final_startdevice_receipt(self):
        text = CYCLE_RUNNER.read_text(encoding="utf-8")
        self.assertIn("CompletionTimeoutSeconds", text)
        self.assertIn("PollIntervalMilliseconds", text)
        self.assertIn("Wait-StartDeviceCompletion", text)
        self.assertIn('ContainsKey("Wom1StartDeviceStatus")', text)
        self.assertIn("ElapsedMilliseconds", text)
        self.assertIn('Outcome = "Completed"', text)
        self.assertIn('Outcome = "Timeout"', text)
        self.assertNotIn("Start-Sleep -Seconds 8", text)
        self.assertNotIn("/force", text.lower())


if __name__ == "__main__":
    unittest.main()
