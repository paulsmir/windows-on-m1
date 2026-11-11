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
    _Static_assert(offsetof(ADMISSION_POOL_DEMAND_RECEIPT, SlabAllocCalls) == 448,
                   "v10 prefix changed");
    _Static_assert(offsetof(ADMISSION_POOL_DEMAND_RECEIPT, PrefillSlabs) == 576,
                   "v11 prefix changed");
    _Static_assert(offsetof(ADMISSION_POOL_DEMAND_RECEIPT, SlabFastMisses) == 608,
                   "v12 prefix changed");
    _Static_assert(offsetof(ADMISSION_POOL_DEMAND_RECEIPT, PressureBudgetMs) == 640,
                   "v13 prefix changed");
    _Static_assert(offsetof(ADMISSION_POOL_DEMAND_RECEIPT, GraphicsShareBytes) == 704,
                   "v14 prefix changed");
    _Static_assert(offsetof(ADMISSION_POOL_DEMAND_RECEIPT, HeadroomRefusals) == 744,
                   "v15 layout changed");
    _Static_assert(offsetof(ADMISSION_POOL_DEMAND_RECEIPT, Rung256Calls) == 752,
                   "v15 prefix changed");
    _Static_assert(offsetof(ADMISSION_POOL_DEMAND_RECEIPT, PoolLadder) == 800,
                   "v16 layout changed");
    _Static_assert(sizeof(r) == 808, "v16 wire ABI changed");
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
    r.SlabAllocCalls = 70; r.SlabAllocUs = 812345; r.SlabAllocUsMax = 33000;
    r.SlabCheckUs = 1200; r.SlabFreeUs = 4500; r.SlabFreeUsMax = 90;
    r.ReserveCalls = 11; r.ReserveUs = 830000; r.ReserveUsMax = 700000;
    r.ReserveNoGrowth = 6; r.LeaseZeroCount = 3; r.LeaseZeroUs = 300;
    r.LeaseZeroSkips = 29; r.SpareLimit = 64; r.EmptySlabs = 12; r.CacheDrops = 2;
    r.PrefillSlabs = 41; r.PrefillUs = 917000; r.PrefillDiscards = 3; r.SlabFailUs = 52000;
    r.SlabContiguous = 40; r.SlabChunked = 30;
    r.SlabFastMisses = 31; r.SlabContiguousUs = 16000; r.SlabChunkedUs = 90000;
    r.ConstructLargePages = 0;
    r.PressureBudgetMs = 2000; r.PressureWaits = 5; r.PressureWaitMs = 1450;
    r.PressureWaitMsMax = 600; r.PressureRecovered = 4; r.PressureFailed = 1;
    r.PressureFastFails = 2; r.ReservedAllocations = 812;
    r.GraphicsShareBytes = 5414095872ULL; r.BaseSegmentBytes = 3288334336ULL;
    r.PoolPercent = 75; r.PolicyPercent = 75; r.PolicyMaxPercent = 80;
    r.PolicyCpuOnlyMiB = 16384; r.PolicyFlags = 1u | 2u | 8u;
    r.HeadroomRefusals = 6;
    r.Rung256Calls = 12; r.Rung256Ok = 8; r.Rung256Us = 30000; r.Rung256UsMax = 5000;
    r.Rung256FailUs = 6000; r.Rung256Skipped = 64; r.PoolLadder = 1;
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
        self.assertEqual(got['version'], 16)
        ladder = {k: v for k, v in got['pool_ladder'].items() if k != 'meaning'}
        # nonfast: (chunked_us 90000 + rung256_fail_us 6000) / 30 chunked slabs.
        self.assertEqual(ladder, dict(
            rung256_calls=12, rung256_ok=8, rung256_us=30000, rung256_us_max=5000,
            rung256_fail_us=6000, rung256_skipped=64, enabled=True,
            rung256_ms_per_slab=3.0, nonfast_ms_per_slab=3.2))
        v15 = bytearray(raw[:752])
        struct.pack_into('<II', v15, 0, 15, 752)
        self.assertIsNone(decoder.decode_pool(v15)['pool_ladder'])
        self.assertEqual(decoder.decode_pool(v15)['graphics_policy']['headroom_refusals'], 6)
        policy = {k: v for k, v in got['graphics_policy'].items() if k != 'meaning'}
        self.assertEqual(policy, dict(
            graphics_share_bytes=5414095872, base_segment_bytes=3288334336,
            extension_bytes=0, pool_percent=75,
            system_partition_commit_limit_percentage=75,
            system_partition_commit_limit_percentage_max=80,
            system_partition_cpu_only_memory_mib=16384, memory_manager_key=True,
            percentage_set=True, written_after_boot=False, pool_percent_set=True,
            headroom_refusals=6))
        headroom = bytearray(raw)
        struct.pack_into('<I', headroom, 360, 5)
        self.assertEqual(decoder.decode_pool(headroom)['last_reserve_failure']['reason'],
                         'headroom')
        v14 = bytearray(raw[:704])
        struct.pack_into('<II', v14, 0, 14, 704)
        struct.pack_into('<I', v14, 360, 5)
        self.assertEqual(decoder.decode_pool(v14)['last_reserve_failure']['reason'],
                         'unknown(5)')
        struct.pack_into('<I', v14, 360, 1)
        self.assertIsNone(decoder.decode_pool(v14)['graphics_policy'])
        self.assertEqual(decoder.decode_pool(v14)['pressure_wait']['budget_ms'], 2000)
        self.assertEqual({k: v for k, v in got['pressure_wait'].items() if k != 'meaning'},
                         dict(budget_ms=2000, waits=5, wait_ms=1450, wait_ms_max=600,
                              recovered=4, failed=1, fast_fails=2,
                              reserved_allocations=812))
        v13 = bytearray(raw[:640])
        struct.pack_into('<II', v13, 0, 13, 640)
        self.assertIsNone(decoder.decode_pool(v13)['pressure_wait'])
        self.assertEqual(decoder.decode_pool(v13)['large_pages']['fast_misses'], 31)
        self.assertEqual({k: v for k, v in got['large_pages'].items() if k != 'meaning'},
                         dict(fast_misses=31, contiguous_us=16000, chunked_us=90000,
                              construct_large_pages=0, contiguous_ms_per_slab=0.4,
                              chunked_ms_per_slab=3.0))
        v12 = bytearray(raw[:608])
        struct.pack_into('<II', v12, 0, 12, 608)
        self.assertIsNone(decoder.decode_pool(v12)['large_pages'])
        self.assertEqual(decoder.decode_pool(v12)['prefill']['slab_fail_us'], 52000)
        self.assertEqual({k: v for k, v in got['prefill'].items() if k != 'meaning'},
                         dict(prefill_slabs=41, prefill_us=917000, prefill_discards=3,
                              slab_fail_us=52000))
        v11 = bytearray(raw[:576])
        struct.pack_into('<II', v11, 0, 11, 576)
        self.assertIsNone(decoder.decode_pool(v11)['prefill'])
        self.assertEqual(decoder.decode_pool(v11)['creation_cost']['cache_drops'], 2)
        cost = got['creation_cost']
        self.assertEqual((cost['slab_alloc_calls'], cost['slab_alloc_us'],
                          cost['slab_alloc_us_max'], cost['slab_check_us'],
                          cost['slab_free_us'], cost['slab_free_us_max'],
                          cost['reserve_calls'], cost['reserve_us'],
                          cost['reserve_us_max'], cost['reserve_no_growth'],
                          cost['lease_zero_count'], cost['lease_zero_us'],
                          cost['lease_zero_skips'], cost['spare_limit_slabs'],
                          cost['empty_slabs'], cost['cache_drops']),
                         (70, 812345, 33000, 1200, 4500, 90, 11, 830000, 700000,
                          6, 3, 300, 29, 64, 12, 2))
        v10 = bytearray(raw[:448])
        struct.pack_into('<II', v10, 0, 10, 448)
        self.assertIsNone(decoder.decode_pool(v10)['creation_cost'])
        self.assertEqual(decoder.decode_pool(v10)['native_staging_bytes'], 134217728)
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
