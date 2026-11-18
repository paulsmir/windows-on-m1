#!/usr/bin/env python3
# SDZ guard boot (L5): an opt-in long host session (WOM1_SDZG_HOLD_MS) inside the existing
# diagnostic SIGINT path of run_uefi: report, hold, report; default off; never changes the
# recovery decision (the handler still returns HANDLED / UNHANDLED as before).
import re
import unittest
from pathlib import Path

SRC = (Path(__file__).resolve().parents[1] / "run_uefi.py").read_text()


class SdzgHold(unittest.TestCase):
    def test_hold_is_opt_in_and_bounded_to_the_snapshot_path(self):
        fn = SRC[SRC.index("def sdzg_hold_session():"):SRC.index("def unattended_control(")]
        self.assertIn('int(os.environ.get("WOM1_SDZG_HOLD_MS", "0"))', fn)
        self.assertRegex(fn, r"if hold_ms <= 0:\s+return")
        self.assertEqual(fn.count("hv.p.hv_sdzg_report(clear=(phase == \"before\"))"), 1)  # report+clear, hold, report
        self.assertIn('for phase in ("before", "after"):', fn)
        self.assertIn("time.sleep(hold_ms / 1000.0)", fn)
        self.assertIn("except Exception as error:", fn)  # an image without 0xc4b

    def test_called_once_on_the_continue_path_only(self):
        uc = SRC[SRC.index("def unattended_control("):SRC.index("hv.run_shell = unattended_control")]
        self.assertEqual(uc.count("sdzg_hold_session()"), 1)
        reboot = uc.index("return EXC_RET.UNHANDLED")
        self.assertLess(reboot, uc.index("sdzg_hold_session()"))  # not on the snapshot-reboot path
        self.assertLess(uc.index("sdzg_hold_session()"), uc.index("return EXC_RET.HANDLED"))


if __name__ == "__main__":
    unittest.main()
