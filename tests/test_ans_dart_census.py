"""Offline invariants of the read-only ANS DART census v2 (no proxy access).

Real bug classes caught (both seen on J313 in ANSDART001-R2):
* reading a DART register block whose accessibility was not proven during
  this boot (dart-scaler aborted at EL2 although PMGR said "active");
* following a bogus TTBR/L2 value into a memory read outside DRAM.
"""
import unittest

from tools import ans_dart_census as c

# Verbatim m1n1 boot banner lines captured by ANSDART001 (console.log, 00:12Z).
BANNER = """TTY> dart: dart /arm-io/dart-dcp at 0x23130c000 is a t8020 (locked)
TTY> dart: dart /arm-io/dart-disp0 at 0x231304000 is a t8020
TTY> dart: dart /arm-io/dart-disp0.0.0 L1 0 is real-time at 0x9fff34000
TTY> dart: dart /arm-io/dart-disp0.0.0 L1 1 is real-time at 0x80501c000
TTY> dart: dart /arm-io/dart-usb0 at 0x382f80000 is a t8020
TTY> dart: dart /arm-io/dart-usb1 at 0x502f80000 is a t8020
"""
# ADT reg instances from ANSDART001-R2 adt.bin (plus the faulting scaler).
ADT_REGS = {'/arm-io/dart-dcp': [0x23130C000],
            '/arm-io/dart-disp0': [0x231304000, 0x231300000],
            '/arm-io/dart-usb0': [0x382F00000, 0x382F80000],
            '/arm-io/dart-usb1': [0x502F00000, 0x502F80000],
            '/arm-io/dart-scaler': [0x263200000]}
DRAM = (0x800000000, 0x200000000)
GUEST = (0x818000000, 0x9DF708000 - 0x818000000)


class CensusV2(unittest.TestCase):
    def test_banner_parse_and_target_selection(self):
        darts, l1 = c.parse_banner(BANNER)
        self.assertTrue(darts['/arm-io/dart-dcp']['locked'])
        self.assertEqual(l1[0], dict(path='/arm-io/dart-disp0', ttbr=0, pa=0x9FFF34000))
        targets = c.select_targets(darts, ADT_REGS)
        self.assertEqual(targets, [('/arm-io/dart-dcp', 0x23130C000),
                                   ('/arm-io/dart-disp0', 0x231304000),
                                   ('/arm-io/dart-usb0', 0x382F80000),
                                   ('/arm-io/dart-usb1', 0x502F80000)])

    def test_scaler_is_never_a_target_even_if_bannered(self):
        darts, _ = c.parse_banner(BANNER + 'dart: dart /arm-io/dart-scaler at 0x263200000 is a t8020\n')
        self.assertNotIn('/arm-io/dart-scaler', [p for p, _ in c.select_targets(darts, ADT_REGS)])

    def test_banner_base_must_match_adt(self):
        darts, _ = c.parse_banner('dart: dart /arm-io/dart-dcp at 0x23130d000 is a t8020\n')
        self.assertEqual(c.select_targets(darts, ADT_REGS), [])

    def test_no_banner_means_no_mmio(self):
        def boom(*_):
            raise AssertionError('MMIO without banner')
        r = c.census([('/arm-io/dart-dcp', 0x23130C000)], boom, boom, DRAM, GUEST, [],
                     banner_seen=False)
        self.assertEqual((r['verdict'], r['stop']), ('INCOMPLETE', 'NO_BOOT_BANNER'))
        r = c.census([], boom, boom, DRAM, GUEST, [])
        self.assertEqual(r['verdict'], 'INCOMPLETE')

    def test_fault_stops_everything(self):
        reads = []
        def r32(a):
            reads.append(a)
            raise c.Fault('MMIO fault')
        r = c.census([('/arm-io/dart-dcp', 0x23130C000), ('/arm-io/dart-disp0', 0x231304000)],
                     r32, None, DRAM, GUEST, [])
        self.assertEqual(len(reads), 1)
        self.assertEqual(r['verdict'], 'INCOMPLETE')
        self.assertEqual([d['status'] for d in r['darts']], ['FAULT'])

    def test_bogus_ttbr_outside_dram_is_never_followed(self):
        def tbl(_):
            raise AssertionError('memory read of an out-of-DRAM table')
        ttbr = {0x23130C200: 0x80000000 | (0x263200000 >> 12)}
        r = c.census([('/arm-io/dart-dcp', 0x23130C000)], lambda a: ttbr.get(a, 0), tbl,
                     DRAM, GUEST, [])
        self.assertEqual(r['verdict'], 'INCOMPLETE')
        self.assertIn('outside DRAM', r['stop'])

    def test_l2_listing_and_classification(self):
        ttbr = {0x231304200: 0x80000000 | (0x9FFF34000 >> 12)}
        l1 = [0x9FFF38003, 0, 0x900004003] + [0] * (c.L1_ENTRIES - 3)
        r = c.census([('/arm-io/dart-disp0', 0x231304000)], lambda a: ttbr.get(a, 0),
                     lambda pa: l1, DRAM, GUEST, [])
        got = [(t['level'], t['pa'], t['verdict']) for t in r['darts'][0]['tables']]
        self.assertEqual(got, [('L1', '0x9fff34000', 'OUTSIDE_GUEST_RAM'),
                               ('L2', '0x900004000', 'GUEST_RAM_CONFLICT'),
                               ('L2', '0x9fff38000', 'OUTSIDE_GUEST_RAM')])
        self.assertEqual(r['verdict'], 'CONFLICT')

    def test_l2_outside_dram_stops(self):
        ttbr = {0x231304200: 0x80000000 | (0x9FFF34000 >> 12)}
        l1 = [0xA00004003] + [0] * (c.L1_ENTRIES - 1)
        r = c.census([('/arm-io/dart-disp0', 0x231304000)], lambda a: ttbr.get(a, 0),
                     lambda pa: l1, DRAM, GUEST, [])
        self.assertEqual(r['verdict'], 'INCOMPLETE')

    def test_guarded_reader_detects_exception_counter(self):
        class P:
            n = 0
            def set_exc_guard(self, m): self.mode = m
            def get_exc_count(self): return self.n
            def read32(self, a):
                if a == 0xbad: self.n += 1
                return 0xffffffff
        r32 = c.guarded_reader(P(), 0x101)
        self.assertEqual(r32(0x10), 0xffffffff)
        with self.assertRaises(c.Fault):
            r32(0xbad)

    def test_cli_refuses_without_experiment_id_before_proxy(self):
        with self.assertRaises(SystemExit):
            c.main(['--guest-ram-base', '0', '--output', '/nonexistent/x'])


if __name__ == '__main__':
    unittest.main()
