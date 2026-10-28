"""Exercise the boot-time resident bound and its physical backing promise."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_apple_agx_pool_headroom import function

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'


class PoolResidencyLimit(unittest.TestCase):
    def test_only_dword_one_enables_the_boot_policy(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Missing, wrong-type, short or non-one values must not silently opt a
        machine into a different segment capacity; every opened key is closed.
        This executes the real Windows reader with registry API substitutes.
        """
        self.check_registry_reader('AdmissionPoolHalfSizeEnabled', 'Wom1PoolHalfSize')

    def test_only_fraction_three_enables_three_quarter_policy(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Invalid registry values must not change the published residency limit.
        Only the explicitly requested fraction 3 opts into the new policy.
        """
        self.check_registry_reader('AdmissionPoolThreeQuarterEnabled', 'Wom1PoolFraction', 3)

    def check_registry_reader(self, reader_name, value_name, enabled_value=1, source=None):
        if source is None:
            source = (RENDER / 'src/pool_windows.c').read_text()
        reader = function(source, reader_name)
        body = r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
typedef unsigned long long ULONGLONG;
typedef unsigned int ULONG;
typedef unsigned char UCHAR;
typedef int BOOLEAN;
typedef void *HANDLE;
typedef struct { const wchar_t *text; } UNICODE_STRING;
typedef struct { ULONG TitleIndex, Type, DataLength; UCHAR Data[1]; }
    KEY_VALUE_PARTIAL_INFORMATION, *PKEY_VALUE_PARTIAL_INFORMATION;
typedef struct { void *PhysicalDeviceObject; } ADMISSION_CONTEXT;
#define TRUE 1
#define FALSE 0
#define REG_DWORD 4
#define PLUGPLAY_REGKEY_DEVICE 1
#define KEY_QUERY_VALUE 1
#define KeyValuePartialInformation 2
#define NT_SUCCESS(s) ((s) >= 0)
static int open_result, query_result, opens, queries, closes;
#define ENABLE_VALUE 1
static ULONG kind = REG_DWORD, length = 4, payload = ENABLE_VALUE;
static int IoOpenDeviceRegistryKey(void *pdo, int which, int access, HANDLE *key) {
    assert(pdo && which == PLUGPLAY_REGKEY_DEVICE && access == KEY_QUERY_VALUE);
    ++opens; if (!open_result) *key = pdo; return open_result;
}
static void RtlInitUnicodeString(UNICODE_STRING *s, const wchar_t *text) { s->text = text; }
static int ZwQueryValueKey(HANDLE key, UNICODE_STRING *name, int which,
                          PKEY_VALUE_PARTIAL_INFORMATION value, size_t cap, ULONG *bytes) {
    assert(key && which == KeyValuePartialInformation);
    assert(!wcscmp(name->text, L"Wom1PoolHalfSize")); ++queries;
    assert(cap >= offsetof(KEY_VALUE_PARTIAL_INFORMATION, Data) + 4);
    value->Type = kind; value->DataLength = length;
    memcpy(value->Data, &payload, 4); *bytes = 16;
    return query_result;
}
static void ZwClose(HANDLE key) { assert(key); ++closes; }
'''+reader+r'''
int main(void) {
    ADMISSION_CONTEXT ctx = {0};
    assert(!AdmissionPoolHalfSizeEnabled(&ctx) && !opens);
    ctx.PhysicalDeviceObject = &ctx;
    open_result = -1;
    assert(!AdmissionPoolHalfSizeEnabled(&ctx) && !closes && !queries);
    open_result = 0; query_result = -1;
    assert(!AdmissionPoolHalfSizeEnabled(&ctx) && closes == 1);
    query_result = 0;
    for (unsigned i = 0; i < 6; ++i) {
        payload = i;
        assert(AdmissionPoolHalfSizeEnabled(&ctx) == (i == ENABLE_VALUE));
    }
    payload = ENABLE_VALUE; kind = 3;
    assert(!AdmissionPoolHalfSizeEnabled(&ctx));
    kind = REG_DWORD; length = 1;
    assert(!AdmissionPoolHalfSizeEnabled(&ctx));
    length = 8;
    assert(!AdmissionPoolHalfSizeEnabled(&ctx));
    assert(closes == queries);
    return 0;
}
'''
        body = body.replace('#define ENABLE_VALUE 1', f'#define ENABLE_VALUE {enabled_value}')
        body = body.replace('AdmissionPoolHalfSizeEnabled', reader_name).replace('Wom1PoolHalfSize', value_name)
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / 'registry.c', Path(tmp) / 'registry'
            src.write_text(body)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                            '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)

    def test_smaller_segment_allows_overcommit_without_new_locked_pages(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        A smaller resident bound must also bound creation-time backing promises:
        evicted/live allocations may exceed it without requesting more locked
        pages, while every resident slot remains backed with high memory clear.
        The opt-in must never enlarge a broker/window/RAM limit or alter default.
        """
        source = (RENDER / 'src/pool_windows.c').read_text()
        policy = function(source, 'AdmissionPoolSegmentBytes')
        body = r'''
#include "render_pool_demand.h"
#include "render_pool_slab.h"
#include <assert.h>
#include <stdlib.h>
typedef unsigned long long ULONGLONG;
typedef int BOOLEAN;
'''+policy+r'''
static unsigned news;
static int fail_new;
static int high = 1;
static ADMISSION_POOL_SLAB *alloc_slab(void *ctx) {
    (void)ctx; if (fail_new) return NULL; ++news; return calloc(1, sizeof(ADMISSION_POOL_SLAB));
}
static void free_slab(void *ctx, ADMISSION_POOL_SLAB *s) {
    (void)ctx; assert(!s->Live); free(s);
}
static int may_grow(void *ctx) { (void)ctx; return high; }
int main(void) {
    const ULONGLONG m = 1ULL << 20;
    /* Independent arithmetic: 5988/2 - 256, aligned down to 64 = 2688. */
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 4096*m, 4608*m, 0, 0, 256*m) == 2688*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 4096*m, 4608*m, 1, 0, 256*m) == 1344*m);
    assert(AdmissionPoolSegmentBytes(0, 4096*m, 4608*m, 0, 0, 256*m) == 1024*m);
    assert(AdmissionPoolSegmentBytes(0, 4096*m, 4608*m, 1, 0, 256*m) == 512*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 4096*m, 1024*m, 1, 0, 256*m) == 384*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 640*m, 4608*m, 1, 0, 256*m) == 320*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 576*m, 4608*m, 1, 0, 256*m) == 256*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 256*m, 4608*m, 1, 0, 256*m) == 256*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 255*m, 4608*m, 1, 0, 256*m) == 0);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 4096*m, 256*m, 1, 0, 256*m) == 0);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 4096*m, 4608*m, 0, 1, 256*m) == 2016*m);
    /* HalfSize remains the conservative winner if both switches are set. */
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 4096*m, 4608*m, 1, 1, 256*m) == 1344*m);
    assert(AdmissionPoolSegmentBytes(0, 4096*m, 4608*m, 0, 1, 256*m) == 768*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 640*m, 4608*m, 0, 1, 256*m) == 480*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 256*m, 4608*m, 0, 1, 256*m) == 256*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 255*m, 4608*m, 0, 1, 256*m) == 0);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 4096*m, 1024*m, 0, 1, 256*m) == 576*m);
    /* rc88: Wom1PoolApertureReserveHalf moves 128 MiB of the 50% share from
     * the aperture reserve to segment 3: J313 expanded RAM (6884.4 MiB,
     * EXP2019 Wom1M2PoolDemand v11) 3442.2 - 256 -> 3136 MiB, - 128 -> 3264
     * MiB; never above the window or the broker table, and the fractions
     * still apply to the larger bound. */
    assert(AdmissionPoolSegmentBytes(7218794496ULL, 4608*m, 4608*m, 0, 0, 256*m) == 3136*m);
    assert(AdmissionPoolSegmentBytes(7218794496ULL, 4608*m, 4608*m, 0, 0, 128*m) == 3264*m);
    assert(AdmissionPoolSegmentBytes(7218794496ULL, 3200*m, 4608*m, 0, 0, 128*m) == 3200*m);
    assert(AdmissionPoolSegmentBytes(7218794496ULL, 4608*m, 3456*m, 0, 0, 128*m) == 3200*m);
    assert(AdmissionPoolSegmentBytes(7218794496ULL, 4608*m, 4608*m, 1, 0, 128*m) == 1600*m);
    assert(AdmissionPoolSegmentBytes(0, 4096*m, 4608*m, 0, 0, 128*m) == 1024*m);
    assert(AdmissionPoolDemandSegmentBytesEx(7218794496ULL, 4608*m, 4608*m) ==
           AdmissionPoolDemandSegmentBytesReserve(7218794496ULL, 4608*m, 4608*m,
                                                  ADMISSION_POOL_DEMAND_APERTURE_RESERVE));
    assert(ADMISSION_POOL_DEMAND_APERTURE_RESERVE_HALF == 128*m);
    /* Use the real sizing result as the reservation cap, as pool start does. */
    ADMISSION_POOL_SLABS pool = {0};
    pool.NewSlab = alloc_slab; pool.FreeSlab = free_slab; pool.MayGrow = may_grow;
    pool.ReserveCap = AdmissionPoolSegmentBytes(6279270400ULL, 4096*m, 4608*m, 0, 1, 256*m) /
                      ADMISSION_POOL_DEMAND_GRANULE;
    assert(pool.ReserveCap == 32256);
    /* A physical allocation failure rolls back only the attempted promise. */
    assert(AdmissionPoolSlabReserve(&pool, 32));
    fail_new = 1;
    assert(!AdmissionPoolSlabReserve(&pool, 32));
    assert(pool.Reserved == 32 && pool.Stats.Slabs == 1);
    assert(pool.Stats.NewSlabFailures == 1 && pool.Stats.ReserveFailures == 1);
    fail_new = 0;
    assert(AdmissionPoolSlabReserve(&pool, 32224));
    assert(news == 1008);
    high = 0;
    assert(AdmissionPoolSlabReserve(&pool, 8192));
    assert(pool.Reserved == 40448 && news == 1008);
    ADMISSION_POOL_SLAB *slab[32256]; unsigned slot[32256];
    assert(AdmissionPoolSlabTake(&pool, 32256, slab, slot));
    assert(news == 1008 && pool.Stats.NewSlabFailures == 1 && !pool.Stats.FloorRefusals);
    for (unsigned i = 0; i < 32256; ++i) AdmissionPoolSlabGive(&pool, slab[i], slot[i]);
    assert(pool.Stats.Slabs == 1008); /* evicted allocations still promised */
    assert(AdmissionPoolSlabTake(&pool, 32256, slab, slot));
    assert(news == 1008); /* a different full resident set uses the same backing */
    for (unsigned i = 0; i < 32256; ++i) AdmissionPoolSlabGive(&pool, slab[i], slot[i]);
    AdmissionPoolSlabUnreserve(&pool, 40448);
    assert(pool.Stats.Slabs == 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'policy.c'
            exe = Path(tmp) / 'policy'
            src.write_text(body)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                            '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            '-I', str(RENDER / 'include'), str(src),
                            str(RENDER / 'src/render_pool_demand.c'),
                            str(RENDER / 'src/render_pool_slab.c'), '-o', str(exe)],
                           check=True)
            subprocess.run([str(exe)], check=True, timeout=10)

    def test_only_dword_one_halves_the_aperture_reserve(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        rc88: a missing, mistyped or non-one Wom1PoolApertureReserveHalf must
        keep the 256 MiB aperture reserve (and so the NON_LOCAL budget the
        native staging arenas and DWM's aperture surfaces live in); only an
        explicit DWORD 1 moves 128 MiB to segment 3. Every key is closed.
        """
        self.check_registry_reader('AdmissionPoolApertureReserveHalfEnabled',
                                   'Wom1PoolApertureReserveHalf')

    def test_start_uses_one_bound_for_backing_and_vidmm(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Publishing the old segment but lowering only ReserveCap breaks the
        paging guarantee. Guard that the start transaction derives both from
        the policy result before allocating metadata or publishing its view.
        """
        source = (RENDER / 'src/pool_windows.c').read_text()
        start = function(source, 'AdmissionPoolDemandStart')
        self.assertIn('ULONGLONG bytes = AdmissionPoolSegmentBytes(', start)
        self.assertIn('AdmissionPoolHalfSizeEnabled(Context)', start)
        self.assertIn('AdmissionPoolThreeQuarterEnabled(Context)', start)
        self.assertIn('AdmissionPoolApertureReserveHalfEnabled(Context) ?', start)
        self.assertIn('ULONGLONG count = bytes / ADMISSION_POOL_DEMAND_GRANULE;', start)
        self.assertIn('pool->Slabs.ReserveCap = count;', start)
        self.assertIn('Context->PoolBytes = bytes;', start)
        self.assertLess(start.index('AdmissionPoolSegmentBytes('),
                        start.index('ExAllocatePool2('))
