import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CURRENT_STATE = ROOT / "investigation" / "CURRENT_STATE.md"
CYCLE_RUNNER = (
    ROOT / "drivers" / "apple-agx" / "windows" / "scripts" /
    "cycle-lifecycle-driver.ps1"
)
CONTEXT_HELPER = ROOT / "scripts" / "gpu-dev-context.sh"


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
        self.assertIn("EXP-20260827-137", text)
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

    def test_context_helper_is_bounded_and_read_only(self):
        self.assertTrue(CONTEXT_HELPER.is_file(), "gpu-dev-context.sh is required")
        text = CONTEXT_HELPER.read_text(encoding="utf-8")
        self.assertIn("git -C \"$ROOT\" rev-parse --show-toplevel", text)
        self.assertIn("GPU_DEV_ROOT", text)
        self.assertIn("investigation/CURRENT_STATE.md", text)
        self.assertIn("git rev-parse", text)
        self.assertIn("git status --short", text)
        self.assertIn("git submodule status", text)
        self.assertIn("investigation/CHANGES.csv", text)
        self.assertIn("tail -n 6", text)
        self.assertIn("sed -n '1,220p'", text)
        for forbidden in ("git pull", "git fetch", "git reset", "git clean", "rm -"):
            self.assertNotIn(forbidden, text)


if __name__ == "__main__":
    unittest.main()
