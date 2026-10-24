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


class Dump1938Round3Tests(unittest.TestCase):
    # Pass 2 stays immutable; these regressions cover the replacement script.
    script = ROOT / "investigation/analysis/EXP1938-dump/cdb-commands-3.txt"

    def test_driver_object_uses_installed_service_name(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Pass 2 queried the SYS basename as a driver object and found nothing.
        The debugger lookup must use the service identity registered by the INF.
        This checks two independently maintained artifacts, not a magic string.
        """
        inf = (ROOT / "drivers/apple-agx/render-admission/"
               "AppleAgxRenderAdmission.inf").read_text()
        services = re.findall(r"(?im)^AddService\s*=\s*([^,\s]+)", inf)
        lookups = re.findall(r"(?im)^!drvobj\s+\\Driver\\(\S+)\s+7$",
                             self.script.read_text())
        self.assertTrue(lookups, "Missing independent driver-object discovery")
        for lookup in lookups:
            self.assertIn(lookup, services)

    def test_masm_conditions_do_not_use_cpp_boolean_operators(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Pass 2's MASM .if rejected && with 'Numeric expression missing'.
        Ban the same C++ boolean syntax in MASM control expressions; this is
        a static guard, not a claim that the rest of CDB syntax was executed.
        """
        for line in self.script.read_text().splitlines():
            if line.startswith(".if"):
                self.assertNotRegex(line, r"&&|\|\|")


if __name__ == "__main__":
    unittest.main()
