"""Static regressions for the saved-dump script, not a CDB execution test."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "investigation/analysis/EXP1938-dump/cdb-commands-2.txt"


class Dump1938CommandsTests(unittest.TestCase):
    def test_failed_lookup_cannot_close_the_dump(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Pass 1 executed q on a missing adapter, so every remaining section ran
        without a target. No command in this evidence script may end a session.
        """
        text = SCRIPT.read_text()
        self.assertNotRegex(text, r"(?im)(?:^|[;{}])\s*(?:q\w*|\.kill|\.detach)(?:\s|$)")

    def test_conditionals_use_only_masm(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        The $$< pass lost the nested @@c++ prefix inside .if. Keep typed
        expressions on independent top-level lines, outside control blocks.
        """
        for number, line in enumerate(SCRIPT.read_text().splitlines(), 1):
            if line.startswith(".if"):
                with self.subTest(line=number):
                    self.assertNotIn("@@", line)
                    self.assertNotIn("??", line)
                    self.assertNotIn("->", line)

    def test_adapter_discovery_does_not_depend_on_receipt_context(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        The old receipt context led to an unreadable device in the actual
        dump. Discover a current pointer by symbol and keep driver/TDR discovery
        independent, even if that optional trace global is absent or disarmed.
        """
        text = SCRIPT.read_text()
        self.assertNotIn("ffffd684d8ff7cd0", text.lower())
        self.assertRegex(text, r"(?m)^r @\$t\d+ = poi\(AppleAgxRenderAdmission!AdmissionUmdRenderTraceAdapter\)$")
        self.assertRegex(text, r"(?m)^!drvobj \\Driver\\AppleAgxRenderAdmission 7$")
        self.assertRegex(text, r"(?m)^dt -r2 dxgkrnl!_TDR_RECOVERY_CONTEXT ")
        # Failed expressions leave pseudo-registers unchanged: reset before
        # every potentially failing assignment, including repeated queue reads.
        lines = text.splitlines()
        for i, line in enumerate(lines):
            match = re.match(r"r (@\$t\d+) = (?:poi\(|@@c\+\+\()", line)
            if match:
                with self.subTest(line=i + 1):
                    self.assertEqual(lines[i - 1], f"r {match[1]} = 0")


if __name__ == "__main__":
    unittest.main()
