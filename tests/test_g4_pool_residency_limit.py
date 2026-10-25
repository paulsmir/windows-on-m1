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

    def check_registry_reader(self, reader_name, value_name):
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
static ULONG kind = REG_DWORD, length = 4, payload = 1;
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
    for (unsigned i = 0; i < 4; ++i) {
        payload = i;
        assert(AdmissionPoolHalfSizeEnabled(&ctx) == (i == 1));
    }
    payload = 1; kind = 3;
    assert(!AdmissionPoolHalfSizeEnabled(&ctx));
    kind = REG_DWORD; length = 1;
    assert(!AdmissionPoolHalfSizeEnabled(&ctx));
    length = 8;
    assert(!AdmissionPoolHalfSizeEnabled(&ctx));
    assert(closes == queries);
    return 0;
}
'''
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
static int high = 1;
static ADMISSION_POOL_SLAB *alloc_slab(void *ctx) {
    (void)ctx; ++news; return calloc(1, sizeof(ADMISSION_POOL_SLAB));
}
static void free_slab(void *ctx, ADMISSION_POOL_SLAB *s) {
    (void)ctx; assert(!s->Live); free(s);
}
static int may_grow(void *ctx) { (void)ctx; return high; }
int main(void) {
    const ULONGLONG m = 1ULL << 20;
    /* Independent arithmetic: 5988/2 - 256, aligned down to 64 = 2688. */
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 4096*m, 4608*m, 0) == 2688*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 4096*m, 4608*m, 1) == 1344*m);
    assert(AdmissionPoolSegmentBytes(0, 4096*m, 4608*m, 0) == 1024*m);
    assert(AdmissionPoolSegmentBytes(0, 4096*m, 4608*m, 1) == 512*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 4096*m, 1024*m, 1) == 384*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 640*m, 4608*m, 1) == 320*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 576*m, 4608*m, 1) == 256*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 256*m, 4608*m, 1) == 256*m);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 255*m, 4608*m, 1) == 0);
    assert(AdmissionPoolSegmentBytes(6279270400ULL, 4096*m, 256*m, 1) == 0);
    /* Use the real sizing result as the reservation cap, as pool start does. */
    ADMISSION_POOL_SLABS pool = {0};
    pool.NewSlab = alloc_slab; pool.FreeSlab = free_slab; pool.MayGrow = may_grow;
    pool.ReserveCap = AdmissionPoolSegmentBytes(6279270400ULL, 512*m, 4608*m, 1) /
                      ADMISSION_POOL_DEMAND_GRANULE;
    assert(pool.ReserveCap == 4096);
    assert(AdmissionPoolSlabReserve(&pool, 4096));
    assert(news == 128);
    high = 0;
    assert(AdmissionPoolSlabReserve(&pool, 8192));
    assert(pool.Reserved == 12288 && news == 128);
    ADMISSION_POOL_SLAB *slab[4096]; unsigned slot[4096];
    assert(AdmissionPoolSlabTake(&pool, 4096, slab, slot));
    assert(news == 128 && !pool.Stats.NewSlabFailures && !pool.Stats.FloorRefusals);
    for (unsigned i = 0; i < 4096; ++i) AdmissionPoolSlabGive(&pool, slab[i], slot[i]);
    assert(pool.Stats.Slabs == 128); /* evicted allocations still promised */
    assert(AdmissionPoolSlabTake(&pool, 4096, slab, slot));
    assert(news == 128); /* a different full resident set uses the same backing */
    for (unsigned i = 0; i < 4096; ++i) AdmissionPoolSlabGive(&pool, slab[i], slot[i]);
    AdmissionPoolSlabUnreserve(&pool, 12288);
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
        self.assertIn('ULONGLONG count = bytes / ADMISSION_POOL_DEMAND_GRANULE;', start)
        self.assertIn('pool->Slabs.ReserveCap = count;', start)
        self.assertIn('Context->PoolBytes = bytes;', start)
        self.assertLess(start.index('AdmissionPoolSegmentBytes('),
                        start.index('ExAllocatePool2('))
