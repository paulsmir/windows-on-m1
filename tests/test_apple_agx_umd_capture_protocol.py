import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = ROOT / "tools" / "verify_apple_agx_umd_capture.py"


class AppleAgxUmdCaptureProtocolTests(unittest.TestCase):
    def validate(self, lines, pid=17, token="abc"):
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / "trace.txt"
            trace.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return subprocess.run(
                [
                    sys.executable,
                    "-B",
                    str(VALIDATOR),
                    "--trace",
                    str(trace),
                    "--expected-pid",
                    str(pid),
                    "--token",
                    token,
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )

    def assert_incomplete(self, lines, reason):
        result = self.validate(lines)
        self.assertEqual(result.returncode, 1, result.stderr)
        value = json.loads(result.stdout)
        self.assertFalse(value["complete"])
        self.assertIn(reason, value["reasons"])

    def test_late_listener_is_incomplete(self):
        """Catches accepting producer markers when listener-ready is absent."""
        self.assert_incomplete(
            [
                "timestamp_ms=1 pid=17 message=AppleAgxProbe: START token=abc",
                "timestamp_ms=2 pid=17 message=AppleAgxProbe: END token=abc",
                "capture=summary ready=0 records=2 lost=0",
            ],
            "listener-not-ready",
        )

    def test_no_marker_is_incomplete(self):
        """Catches treating a ready empty collector as proof of probe coverage."""
        self.assert_incomplete(
            ["capture=summary ready=1 records=0 lost=0"], "start-marker-missing"
        )

    def test_different_pid_is_incomplete(self):
        """Catches correlating another process's debug messages to the probe."""
        self.assert_incomplete(
            [
                "timestamp_ms=1 pid=18 message=AppleAgxProbe: START token=abc",
                "timestamp_ms=2 pid=18 message=AppleAgxProbe: END token=abc",
                "capture=summary ready=1 records=2 lost=0",
            ],
            "start-marker-missing",
        )

    def test_collector_end_before_producer_end_is_incomplete(self):
        """Catches using a trace whose listener stopped before probe completion."""
        self.assert_incomplete(
            [
                "timestamp_ms=1 pid=17 message=AppleAgxProbe: START token=abc",
                "capture=summary ready=1 records=1 lost=0",
            ],
            "end-marker-missing",
        )

    def test_loss_from_slow_consumer_is_incomplete(self):
        """Catches declaring a ring-overflowed capture complete."""
        self.assert_incomplete(
            [
                "timestamp_ms=1 pid=17 message=AppleAgxProbe: START token=abc",
                "timestamp_ms=2 pid=17 message=AppleAgxProbe: END token=abc",
                "capture=summary ready=1 records=2 lost=4",
            ],
            "records-lost",
        )

    def test_exact_markers_are_complete(self):
        """Catches rejecting an exact ready, lossless, same-PID capture interval."""
        result = self.validate(
            [
                "timestamp_ms=1 pid=17 message=AppleAgxProbe: START token=abc",
                "timestamp_ms=2 pid=17 message=AppleAgxUMD: OpenAdapter10_2 ENTER",
                "timestamp_ms=3 pid=17 message=AppleAgxProbe: END token=abc",
                "capture=summary ready=1 records=3 lost=0",
            ]
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            json.loads(result.stdout),
            {
                "complete": True,
                "end_count": 1,
                "expected_pid": 17,
                "lost": 0,
                "ready": True,
                "reasons": [],
                "records": 3,
                "start_count": 1,
                "token": "abc",
            },
        )


if __name__ == "__main__":
    unittest.main()
