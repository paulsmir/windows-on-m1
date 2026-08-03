import signal
import time
import unittest

from tools.kd.kd_watchdog import deadline


class DeadlineTests(unittest.TestCase):
    def test_raises_timeout_and_restores_alarm_state(self):
        old_handler = signal.getsignal(signal.SIGALRM)

        with self.assertRaisesRegex(TimeoutError, "KD diagnostic deadline"):
            with deadline(0.02):
                time.sleep(0.2)

        self.assertIs(signal.getsignal(signal.SIGALRM), old_handler)
        remaining, interval = signal.getitimer(signal.ITIMER_REAL)
        self.assertEqual(interval, 0.0)
        self.assertLess(remaining, 0.01)


if __name__ == "__main__":
    unittest.main()
