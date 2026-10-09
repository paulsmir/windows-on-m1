"""J313 P-cluster operating point request (tools/j313_cpufreq.py).

Invariants: the command keeps unrelated bits, writes the P-state into both
T8103 desired fields with SET and without BUSY, refuses turbo levels 13-15
and level 0, waits for BUSY to clear and checks the retained request.
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import j313_cpufreq as c  # noqa: E402


class FakeProxy:
    def __init__(self, value, busy_reads=2):
        self.value, self.busy_reads, self.writes = value, busy_reads, []

    def read64(self, addr):
        assert addr == 0x211E20020
        if self.writes and self.busy_reads:
            self.busy_reads -= 1
            return self.value | c.PSTATE_BUSY
        return self.value

    def write64(self, addr, value):
        assert addr == 0x211E20020
        self.writes.append(value)
        self.value = value & ~c.PSTATE_SET


class J313Cpufreq(unittest.TestCase):
    def test_command_encoding(self):
        current = (1 << 20) | (7 << 12) | 7 | (1 << 40)
        value = c.pcpu_pstate_command(current, 12)
        self.assertEqual(value & 0x1F, 12)
        self.assertEqual((value >> 12) & 0xF, 12)
        self.assertTrue(value & c.PSTATE_SET)
        self.assertFalse(value & c.PSTATE_BUSY)
        self.assertEqual(value & ((1 << 20) | (1 << 40)), (1 << 20) | (1 << 40))

    def test_turbo_and_invalid_levels_are_refused(self):
        for level in (0, 13, 14, 15, 7.0, None):
            with self.assertRaises(ValueError):
                c.pcpu_pstate_command(0, level)

    def test_set_waits_for_busy_and_checks_request(self):
        proxy = FakeProxy((1 << 20) | (7 << 12) | 7)
        before, after = c.set_pcpu_pstate(proxy, 12)
        self.assertEqual(before & 0x1F, 7)
        self.assertEqual(c.requested_pstate(after), 12)
        self.assertEqual(len(proxy.writes), 1)

    def test_timeout_is_an_error(self):
        proxy = FakeProxy(7, busy_reads=10**6)
        with self.assertRaises(RuntimeError):
            c.set_pcpu_pstate(proxy, 12, polls=5)


if __name__ == "__main__":
    unittest.main()
