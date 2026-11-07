"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Composition evidence must not confuse sampled flips with all frames, repeated
snapshots with new latches, a reboot with a continuous clock, or missing serial
text with a proved DCP fault. Timing is computed only from compatible evidence.
"""
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CompositionAnalysisTests(unittest.TestCase):
    def setUp(self):
        path = ROOT / 'tools/agx_composition_analyze.py'
        self.assertTrue(path.exists(), 'composition discriminator missing')
        spec = importlib.util.spec_from_file_location('composition', path)
        self.tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.tool)

    def test_untimed_serial_does_not_invent_cadence_or_order_fault(self):
        result = self.tool.serial_summary([
            'TTY> dcp-iomfb: exact D589 latch swap_id=8',
            'TTY> dcp-iomfb: A408 APPLIED swap_id=8; awaiting exact D589 latch',
            'TTY> dcp-iomfb: exact D589 latch swap_id=9',
        ])
        self.assertEqual(result['paired_ids'], 1)
        self.assertEqual(result['latch_without_applied_text'], [9])
        self.assertIsNone(result['cadence_ms'])

    def test_display_ring_wrap_and_filter_are_explicit(self):
        data = bytearray(10288)
        struct.pack_into('<4I', data, 0, 2, len(data), 258, 999)
        for n in range(2, 258):
            struct.pack_into('<3Q4I', data, 48+(n % 256)*40,
                             n*1000, 0x1000, 0x2000, 3, 4, 2, 100000)
        result = self.tool.display_summary(data, 1000)
        self.assertEqual(result['retained_records'], 256)
        self.assertEqual(result['overwritten_records'], 2)
        self.assertEqual(result['first_qpc'], 2000)
        self.assertEqual(result['last_qpc'], 257000)
        self.assertEqual(result['kinds']['3']['recorded_gap_ms']['median'], 100)
        self.assertTrue(result['filtered'])
        with self.assertRaises(ValueError):
            self.tool.display_summary(data[:-1], 1000)
        data[0] = 3
        with self.assertRaises(ValueError):
            self.tool.display_summary(data, 1000)

    def test_umd_only_dwm_and_known_phases_with_reported_frequency(self):
        result = self.tool.umd_summary([
            'measure-g4-phase hr=0x00000000 pid=4 tid=1 qpc=100 00000005 00000064 00000000 000003e8 1 0 0',
            'measure-g4-phase hr=0x00000000 pid=5 tid=1 qpc=200 00000005 000000ff 00000000 000003e8 1 0 0',
            'measure-g4-phase hr=0x00000000 pid=4 tid=1 qpc=400 00000005 000000c8 00000000 000003e8 2 0 0',
        ], 4, (150, 500))
        self.assertEqual(result['before']['5']['duration_ms']['median'], 100)
        self.assertEqual(result['app']['5']['duration_ms']['median'], 200)
        self.assertEqual(result['app']['5']['count'], 1)

    @staticmethod
    def event(seq, kind, time, pending=0, active=0, generation=7):
        return (f'VSYNC_EVENT generation={generation} seq={seq} time100ns={time} '
                f'kind={kind} pending_seq={pending} pending=0x1000 '
                f'active_seq={active} active=0x1000')

    def test_pairs_exact_identity_deduplicates_and_splits_boots(self):
        e = self.event
        lines = [e(1, 8, 10000, 1), e(2, 3, 30000, 1, 1),
                 e(3, 6, 30000), e(4, 8, 100000, 2),
                 e(5, 3, 130000, 2, 2)]
        result = self.tool.vsync_summary(lines + lines + [e(1, 3, 1, 1, 1, 8)])
        self.assertEqual(result['generations']['7']['queue_to_latch_ms']['median'], 2.5)
        self.assertEqual(result['generations']['7']['latch_gap_ms']['median'], 10)
        self.assertEqual(result['generations']['7']['matched_queue_latches'], 2)
        self.assertEqual(result['generations']['8']['unpaired_latches'], 1)
        self.assertEqual(result['duplicate_events'], 5)

    def test_missing_capture_and_wrong_address_cannot_make_latency(self):
        e = self.event
        result = self.tool.vsync_summary([
            e(1, 3, 10000, 1, 1), e(8, 8, 20000, 2),
            e(9, 3, 40000, 2, 2).replace('active=0x1000', 'active=0x2000'),
        ])['generations']['7']
        self.assertEqual(result['missing_events'], 6)
        self.assertIsNone(result['latch_gap_ms']['median'])
        self.assertEqual(result['matched_queue_latches'], 0)
        self.assertEqual(result['identity_mismatches'], 1)

    def test_conflicting_duplicate_is_not_silently_accepted(self):
        with self.assertRaises(ValueError):
            self.tool.vsync_summary([self.event(1, 3, 1), self.event(1, 3, 2)])

    def test_truncated_sample_tail_and_queue_without_latch_are_explicit(self):
        result = self.tool.vsync_summary([
            'VSYNC_SAMPLE generation=7 events=100', self.event(1, 8, 1, 1),
            self.event(2, 6, 2), 'VSYNC_SAMPLE generation=8 events=12',
        ])['generations']
        self.assertEqual(result['7']['missing_events'], 98)
        self.assertEqual(result['7']['unemitted_sample_tail'], 98)
        self.assertEqual(result['7']['unpaired_queue_events'], 1)
        self.assertEqual(result['8']['missing_events'], 12)
        self.assertEqual(result['8']['unemitted_sample_tail'], 12)

    def test_same_frequency_clock_reset_is_rejected_before_sorting(self):
        lines = [f'measure-g4-phase hr=0x00000000 pid=4 tid=1 qpc={qpc} '
                 '00000005 00000064 00000000 000003e8 1 0 0'
                 for qpc in (100000, 200000, 100, 200)]
        with self.assertRaisesRegex(ValueError, 'clock'):
            self.tool.umd_summary(lines, 4)
