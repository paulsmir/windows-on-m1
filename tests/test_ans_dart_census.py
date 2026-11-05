"""Offline invariants of the read-only ANS DART census (no proxy access).

Real bug class caught: a census that silently reports "no retained table in
guest RAM" while it skipped a DART it could not read, or that misses an L2
table hanging off a valid L1 entry.
"""
import unittest

from tools import ans_dart_census as c

GUEST = (0x850000000, 0x18F708000)


class Census(unittest.TestCase):
    def fake(self, ttbrs, memory):
        return (lambda a: ttbrs.get(a, 0),
                lambda pa, n: (memory.get(pa, []) + [0] * n)[:n])

    def test_ttbr_layout_matches_proxyclient_map(self):
        regs = c.ttbr_addresses(0x1000)
        self.assertEqual(regs[0], (0, 0, 0x1200))
        self.assertEqual(regs[5], (1, 1, 0x1214))
        self.assertEqual(len(regs), 64)
        self.assertEqual(c.decode_ttbr(0x80000000 | (0x9d71a8000 >> 12)), 0x9d71a8000)
        self.assertIsNone(c.decode_ttbr(0x9d71a8))

    def test_l2_tables_follow_valid_l1_entries(self):
        r32, blk = self.fake({0x1200: 0x80000000 | (0x800100000 >> 12)},
                             {0x800100000: [0x900004003, 0, 0x900004003, 0x9e0000001]})
        rec = c.census([dict(path='d', compatible='dart,t8020', base=0x1000, powered=True)],
                       r32, blk, GUEST, [])
        pas = [(t['level'], t['pa'], t['verdict']) for t in rec['darts'][0]['tables']]
        self.assertEqual(pas, [('L1', '0x800100000', 'OUTSIDE_GUEST_RAM'),
                               ('L2', '0x900004000', 'GUEST_RAM_CONFLICT'),
                               ('L2', '0x9e0000000', 'OUTSIDE_GUEST_RAM')])
        self.assertEqual(rec['verdict'], 'CONFLICT')

    def test_reserved_cover_and_clean_verdict(self):
        r32, blk = self.fake({0x1200: 0x80000000 | (0x900000000 >> 12)}, {})
        rec = c.census([dict(path='d', compatible='dart,t8020', base=0x1000, powered=True)],
                       r32, blk, GUEST, [(0x900000000, 0x4000)])
        self.assertEqual(rec['darts'][0]['tables'][0]['verdict'], 'RESERVED')
        self.assertEqual(rec['verdict'], 'NO_RETAINED_TABLE_IN_GUEST_RAM')

    def test_unreadable_darts_make_census_incomplete_not_empty(self):
        r32, blk = self.fake({}, {})
        for d in (dict(compatible='dart,t8110', powered=True),
                  dict(compatible='dart,t8020', powered=None)):
            rec = c.census([dict(path='d', base=0x1000, **d)], r32, blk, GUEST, [])
            self.assertEqual(rec['verdict'], 'INCOMPLETE')

    def test_unpowered_dart_registers_are_never_read(self):
        def boom(_):
            raise AssertionError('unpowered DART MMIO read')
        rec = c.census([dict(path='d', compatible='dart,t8020', base=0x1000, powered=False)],
                       boom, boom, GUEST, [])
        self.assertEqual(rec['darts'][0]['status'], 'UNPOWERED_NOT_READ')
        self.assertEqual(rec['verdict'], 'NO_RETAINED_TABLE_IN_GUEST_RAM')

    def test_power_requires_every_gate_active(self):
        devs = {1: dict(psreg=0, psidx=0), 2: dict(psreg=0, psidx=1)}
        addr = lambda d: c.pmgr_ps_address([0x1000], [dict(reg=0, offset=0x58)], d)
        self.assertEqual(addr(devs[2]), 0x1060)
        on = {0x1058: 0xff, 0x1060: 0xff}
        self.assertTrue(c.powered([1, 2], devs, addr, on.get))
        self.assertFalse(c.powered([1, 2], devs, addr, {0x1058: 0xff, 0x1060: 0x0f}.get))
        self.assertIsNone(c.powered([3], devs, addr, on.get))
        self.assertIsNone(c.powered([], devs, addr, on.get))

    def test_cli_refuses_without_experiment_id_before_proxy(self):
        with self.assertRaises(SystemExit):
            c.main(['--guest-ram-base', '0', '--guest-ram-end', '1', '--output', '/nonexistent/x'])


if __name__ == '__main__':
    unittest.main()
