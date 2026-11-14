"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A performance regression
that the correctness suite does not see (EXP2204/2208: CS 1.6 at 5 and 18.6 fps
with the suite 119/0) must fail the package gate, and a missing measurement
must not pass silently."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import agx_perf_gate as g  # noqa: E402

BASE = {"cs16_benchmark": {"fps": 45.457, "source": "t"}, "gravitymark_hd_perf": {"fps": 40.291, "source": "t"}}


class PerfGate(unittest.TestCase):
    def test_cs_regression_fails(self):
        ok, lines = g.gate(BASE, {"cs16_benchmark": [18.632]}, 2.0)
        self.assertFalse(ok)
        self.assertIn("FAIL", lines[0])

    def test_within_tolerance_passes(self):
        ok, _ = g.gate(BASE, {"cs16_benchmark": [44.8], "gravitymark_hd_perf": [39.9, 40.6]}, 2.0)
        self.assertTrue(ok)

    def test_worst_arm_decides(self):
        ok, _ = g.gate(BASE, {"gravitymark_hd_perf": [41.0, 39.0]}, 2.0)
        self.assertFalse(ok)

    def test_missing_measurement_fails(self):
        ok, lines = g.gate(BASE, {"cs16_benchmark": [None]}, 2.0)
        self.assertFalse(ok)
        self.assertIn("missing-measurement", lines[0])

    def test_cs_log_utf16_and_utf8(self):
        line = "console: 3771 frames 82.957 seconds 45.457 fps\r\n"
        with tempfile.TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.log"; b = Path(tmp) / "b.log"
            a.write_bytes("﻿".encode("utf-16-le") + line.encode("utf-16-le"))
            b.write_text(line, encoding="utf-8")
            self.assertEqual(g.cs_fps(a), 45.457)
            self.assertEqual(g.cs_fps(b), 45.457)

    def test_real_evidence_if_present(self):
        cs = Path("~/public_windows/.local/experiments/EXP2208-cs16-nqrefusal/evidence/EXP2208-B/cs-bench.log").expanduser()
        if not cs.exists():
            self.skipTest("evidence not on this machine")
        ok, lines = g.gate(BASE, {"cs16_benchmark": [g.cs_fps(cs)]}, 2.0)
        self.assertFalse(ok, lines)  # EXP2208 (18.6 fps) must fail the gate


if __name__ == "__main__":
    unittest.main()
