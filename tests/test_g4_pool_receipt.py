"""Offline pool receipts must preserve the actual memory-pressure boundary."""
import importlib.util
import base64
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
spec = importlib.util.spec_from_file_location('pool_decoder', ROOT / 'tools/decode_wom1_gpu_fault.py')
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


class PoolReceipt(unittest.TestCase):
    def test_native_receipt_and_legacy_prefix(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Decoder offsets must match the KMD C ABI; a v7 receipt cannot report
        measured floor failures/event state that it never captured. Values
        written by the real declaration are decoded, not mirrored offsets.
        """
        source = r'''
#include "render_pool_demand.h"
#include <stdio.h>
#include <stddef.h>
int main(void) {
    ADMISSION_POOL_DEMAND_RECEIPT r = {0};
    _Static_assert(offsetof(ADMISSION_POOL_DEMAND_RECEIPT, FloorRefusals) == 304,
                   "v7 prefix changed");
    _Static_assert(offsetof(ADMISSION_POOL_DEMAND_RECEIPT, LastReserveFailure.Reason) == 360,
                   "v8 prefix changed");
    _Static_assert(offsetof(ADMISSION_POOL_DEMAND_RECEIPT, NativeStagingBytes) == 432,
                   "v9 prefix changed");
    _Static_assert(sizeof(r) == 448, "v10 wire ABI changed");
    r.Version = ADMISSION_POOL_DEMAND_RECEIPT_VERSION; r.Bytes = sizeof(r);
    r.GranuleBytes = 65536; r.LastWindowResult = 9;
    r.SegmentBytes = 1409286144ULL;
    r.Stats.CommittedGranules = 201; r.Stats.PeakGranules = 213;
    r.Stats.CommitFailures = 3; r.Stats.ReleaseFailures = 4;
    r.HeldGranules = 224; r.NewSlabFailures = 2;
    r.RamBytes = 6279270400ULL;
    r.ReservedBytes = 201*65536ULL; r.PeakReservedBytes = 1409286144ULL;
    r.ReserveFailures = 7; r.BrokerBytes = 4831838208ULL; r.FloorRefusals = 5;
    r.LastReserveFailure.RequestBytes = 134217728;
    r.LastReserveFailure.ReservedBytes = 1409286144ULL;
    r.LastReserveFailure.HeldBytes = 1411383296ULL;
    r.LastReserveFailure.CommittedBytes = 1405157376ULL;
    r.LastReserveFailure.TimeMs = 12345;
    r.LastReserveFailure.HighMemory = 0; r.LastReserveFailure.LowMemory = 1;
    r.LastReserveFailure.Reason = ADMISSION_POOL_REFUSAL_LOW_MEMORY;
    r.LastReserveFailure.RequiredGranules = 23552;
    r.LastReserveFailure.HeldGranules = 21536;
    r.LastReserveFailure.ReserveCapGranules = 50176;
    r.LastReserveFailure.PrivateGranules = 7;
    r.RefusalCounts[0] = 4; r.RefusalCounts[1] = 1;
    r.RefusalCounts[2] = 2; r.RefusalCounts[3] = 0;
    r.NativeStagingBytes = 134217728;
    r.TotalPromisedBytes = 134217728 + 201*65536ULL;
    return fwrite(&r, sizeof(r), 1, stdout) == 1 ? 0 : 1;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / 'receipt.c', Path(tmp) / 'receipt'
            src.write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                            '-Wextra', '-Werror', '-I', str(RENDER / 'include'),
                            str(src), '-o', str(exe)], check=True)
            raw = subprocess.check_output([str(exe)])
        got = decoder.decode_pool(raw)
        self.assertEqual(got['version'], 10)
        self.assertEqual(got['native_staging_bytes'], 134217728)
        self.assertEqual(got['total_promised_bytes'], 134217728 + 201*65536)
        self.assertEqual(got['total_held_bytes'], 134217728 + 224*65536)
        self.assertEqual(got['segment_bytes'], 1409286144)
        self.assertEqual(got['committed_bytes'], 13172736)
        self.assertEqual(got['held_bytes'], 14680064)
        self.assertEqual(got['commit_failures'], 3)
        self.assertEqual(got['new_slab_failures'], 2)
        self.assertEqual(got['reserve_failures'], 7)
        self.assertEqual(got['floor_refusals'], 5)
        self.assertEqual(got['last_reserve_failure'], {
            'request_bytes': 134217728, 'reserved_bytes': 1409286144,
            'held_bytes': 1411383296, 'committed_bytes': 1405157376,
            'time_ms': 12345, 'high_memory': 'clear', 'low_memory': 'set',
            'reason': 'low_memory', 'required_granules': 23552,
            'held_granules': 21536, 'reserve_cap_granules': 50176,
            'private_granules': 7})
        self.assertEqual(got['refusal_counts'], dict(low_memory=4,
            signal_unavailable=1, backing_allocation=2, arithmetic=0))
        self.assertEqual(got['event_sample'], 'admission_decision')
        v9 = bytearray(raw[:432])
        struct.pack_into('<II', v9, 0, 9, 432)
        self.assertIsNone(decoder.decode_pool(v9)['native_staging_bytes'])
        v8 = bytearray(raw[:360])
        struct.pack_into('<II', v8, 0, 8, 360)
        previous = decoder.decode_pool(v8)
        self.assertEqual(previous['event_sample'], 'post_failure')
        self.assertIsNone(previous['refusal_counts'])
        self.assertNotIn('reason', previous['last_reserve_failure'])
        legacy = bytearray(raw[:304])
        struct.pack_into('<II', legacy, 0, 7, 304)
        old = decoder.decode_pool(legacy)
        self.assertEqual(old['reserved_bytes'], 13172736)
        self.assertIsNone(old['floor_refusals'])
        self.assertIsNone(old['last_reserve_failure'])

    def test_rejects_unknown_or_truncated_layout(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        A truncated, incompatible or zero-granule receipt must not become a
        plausible memory-pressure report; event codes must retain uncertainty.
        """
        good = struct.pack('<4I36Q', 7, 304, 65536, 0, *([0]*36))
        for raw in (b'', good[:8], good[:-1], good+b'\0',
                    struct.pack('<I', 10)+good[4:],
                    good[:4]+struct.pack('<I', 360)+good[8:],
                    good[:8]+struct.pack('<I', 0)+good[12:]):
            with self.subTest(size=len(raw)), self.assertRaises(ValueError):
                decoder.decode_pool(raw)
        v8 = bytearray(360)
        struct.pack_into('<4I', v8, 0, 8, 360, 65536, 0)
        self.assertIsNone(decoder.decode_pool(v8)['last_reserve_failure'])
        struct.pack_into('<Q', v8, 288, 1)  # ReserveFailures in the stable v7 prefix
        struct.pack_into('<2I', v8, 352, 99, 1)
        self.assertEqual(decoder.decode_pool(v8)['last_reserve_failure']['high_memory'],
                         'unknown(99)')
        v9 = bytearray(432)
        struct.pack_into('<4I', v9, 0, 9, 432, 65536, 0)
        struct.pack_into('<Q', v9, 288, 1)
        struct.pack_into('<I', v9, 360, 99)
        self.assertEqual(decoder.decode_pool(v9)['last_reserve_failure']['reason'],
                         'unknown(99)')
        for raw in (v8[:304], v8[:-1], v8+b'\0', v9[:360], v9[:-1], v9+b'\0'):
            with self.subTest(size=len(raw)), self.assertRaises(ValueError):
                decoder.decode_pool(raw)

    def test_old_collectors_keep_gpu_decode_while_pool_is_explicit(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Adding pool support must not make an old fault collection fail because
        it also contains a historical, unsupported pool version. Only explicit
        raw pool input opts into decoding that schema.
        """
        recovery = struct.pack('<8I', *range(8))
        old_pool = struct.pack('<4I', 6, 16, 65536, 0)
        with tempfile.TemporaryDirectory() as tmp:
            collector = Path(tmp) / 'state.json'
            collector.write_text(json.dumps({'Receipts': {
                'Wom1FirmwareRecovery': base64.b64encode(recovery).decode(),
                'Wom1M2PoolDemand': base64.b64encode(old_pool).decode()}}))
            selected = decoder.read_receipts(collector)
            self.assertEqual(set(selected), {'Wom1FirmwareRecovery'})
            self.assertEqual(decoder.DECODERS['Wom1FirmwareRecovery'](selected['Wom1FirmwareRecovery']),
                             decoder.decode_recovery(recovery))
            raw = Path(tmp) / 'Wom1M2PoolDemand.bin'
            raw.write_bytes(old_pool)
            explicit = decoder.read_receipts(raw)
            self.assertEqual(explicit, {'Wom1M2PoolDemand': old_pool})
            with self.assertRaises(ValueError):
                decoder.decode_pool(explicit['Wom1M2PoolDemand'])


if __name__ == '__main__':
    unittest.main()
