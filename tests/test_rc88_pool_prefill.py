"""rc88: the empty-slab cache is filled by a work item, not inside a creation.

EXP2019a (GravityMark FHD 100k): Wom1M2PoolDemand v11 shows 1849
MmAllocatePagesForMdlEx calls taking 37.2 s (20 ms each) against 35.5 s of
reservations, all under the pool lock: a 128 MiB pfnAllocateCb is Windows
page allocation, not VidMm, and the first frame's six 64-128 MiB creations
spent 5.3 s there.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_apple_agx_pool_headroom import function
import test_g4_pool_residency_limit as residency_limit

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
POOL = RENDER / 'src/pool_windows.c'

SLAB_HARNESS = r'''
#include "render_pool_slab.h"
#include <assert.h>
#include <stdlib.h>
static unsigned news, frees, fail_new;
static ADMISSION_POOL_SLAB *new_slab(void *ctx) {
  (void)ctx; if (fail_new) return 0; ++news; return calloc(1, sizeof(ADMISSION_POOL_SLAB));
}
static void free_slab(void *ctx, ADMISSION_POOL_SLAB *s) { (void)ctx; assert(!s->Live); ++frees; free(s); }
static ADMISSION_POOL_SLAB *outside(void) { return calloc(1, sizeof(ADMISSION_POOL_SLAB)); }
int main(void) {
  ADMISSION_POOL_SLABS pool = {0};
  ADMISSION_POOL_SLAB *slab[256]; unsigned slot[256];
  pool.NewSlab = new_slab; pool.FreeSlab = free_slab;
  pool.SpareLimit = 4; pool.ReserveCap = 1u << 20;
  assert(AdmissionPoolSlabPrefillWants(0) == 0);
  assert(AdmissionPoolSlabPrefillWants(&pool) == 4);
  /* Up to SpareLimit outside-made slabs join as clean empty spares. */
  for (unsigned i = 0; i < 4; ++i) assert(AdmissionPoolSlabAdopt(&pool, outside()));
  assert(pool.Empty == 4 && pool.Stats.Slabs == 4 && pool.Stats.AdoptedSlabs == 4);
  assert(pool.List->FreeMask == 0xffffffffu && pool.List->CleanMask == 0xffffffffu);
  assert(AdmissionPoolSlabPrefillWants(&pool) == 0);
  ADMISSION_POOL_SLAB *extra = outside();
  assert(!AdmissionPoolSlabAdopt(&pool, extra));
  assert(pool.Stats.AdoptRefusals == 1 && pool.Stats.Slabs == 4);
  free(extra);
  /* A creation's promise is covered by them: no slab from Windows. */
  fail_new = 1;
  assert(AdmissionPoolSlabReserve(&pool, 4 * ADMISSION_POOL_SLAB_SLOTS));
  assert(news == 0 && pool.Stats.ReserveNoGrowth == 1);
  /* Placement takes them; the cache is short again. */
  assert(AdmissionPoolSlabTake(&pool, 4 * ADMISSION_POOL_SLAB_SLOTS, slab, slot));
  assert(pool.Empty == 0 && AdmissionPoolSlabPrefillWants(&pool) == 4);
  for (unsigned i = 0; i < 2; ++i) assert(AdmissionPoolSlabAdopt(&pool, outside()));
  assert(AdmissionPoolSlabPrefillWants(&pool) == 2);
  /* Adopted spares stay within what the ordinary trim keeps. */
  for (unsigned i = 0; i < 4 * ADMISSION_POOL_SLAB_SLOTS; ++i) AdmissionPoolSlabGive(&pool, slab[i], slot[i]);
  AdmissionPoolSlabUnreserve(&pool, 4 * ADMISSION_POOL_SLAB_SLOTS);
  assert(pool.Stats.Slabs == 4 && pool.Empty == 4 && frees == 2);
  /* A later promise beyond the cache still grows from Windows as before. */
  fail_new = 0;
  assert(AdmissionPoolSlabReserve(&pool, 6 * ADMISSION_POOL_SLAB_SLOTS) && news == 2);
  AdmissionPoolSlabUnreserve(&pool, 6 * ADMISSION_POOL_SLAB_SLOTS);
  pool.SpareLimit = 0;
  AdmissionPoolSlabUnreserve(&pool, 0);
  assert(pool.Stats.Slabs == 0 && frees == 6 + 2);
  return 0;
}
'''

POLICY_HARNESS = r'''
#include <assert.h>
typedef unsigned long long ULONGLONG;
typedef unsigned int ULONG;
typedef int BOOLEAN;
#define TRUE 1
#define FALSE 0
#define ADMISSION_POOL_RELEASE_DELAY_MS 2000ULL
@@POLICY@@
int main(void) {
  /* The cache's own condition: on, running, High set, Low clear, a recent
   * creation and a short cache. */
  assert(AdmissionPoolPrefillAllowed(1, 0, 1, 0, 5000, 4000, 3));
  assert(!AdmissionPoolPrefillAllowed(0, 0, 1, 0, 5000, 4000, 3)); /* switch off */
  assert(!AdmissionPoolPrefillAllowed(1, 1, 1, 0, 5000, 4000, 3)); /* stopping */
  assert(!AdmissionPoolPrefillAllowed(1, 0, 0, 0, 5000, 4000, 3)); /* High clear */
  assert(!AdmissionPoolPrefillAllowed(1, 0, 2, 0, 5000, 4000, 3)); /* High unknown */
  assert(!AdmissionPoolPrefillAllowed(1, 0, 1, 1, 5000, 4000, 3)); /* Low set */
  assert(!AdmissionPoolPrefillAllowed(1, 0, 1, 2, 5000, 4000, 3)); /* Low unknown */
  assert(!AdmissionPoolPrefillAllowed(1, 0, 1, 0, 5000, 0, 3));    /* never reserved */
  assert(!AdmissionPoolPrefillAllowed(1, 0, 1, 0, 6000, 4000, 3)); /* idle 2 s */
  assert(AdmissionPoolPrefillAllowed(1, 0, 1, 0, 5999, 4000, 3));
  assert(!AdmissionPoolPrefillAllowed(1, 0, 1, 0, 3000, 4000, 3)); /* clock order */
  assert(!AdmissionPoolPrefillAllowed(1, 0, 1, 0, 5000, 4000, 0)); /* cache full */
  return 0;
}
'''


def build_and_run(test, source, extra=()):
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = Path(tmp) / 't.c', Path(tmp) / 't'
        src.write_text(source)
        subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-I', str(RENDER / 'include'), str(src),
                        *map(str, extra), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)


class PoolPrefill(unittest.TestCase):
    def test_adopted_slabs_are_the_cache_trim_keeps(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Slabs the prefill made without the pool lock must join only while
        fewer than SpareLimit empty slabs exist (so Trim never frees them at
        once and the cache never grows past the rc86 bound), must be clean
        empty spares that cover the next creation's promise without a Windows
        allocation, and must leave ordinary growth and return unchanged.
        """
        build_and_run(self, SLAB_HARNESS, [RENDER / 'src/render_pool_slab.c'])

    def test_prefill_runs_only_under_the_cache_condition(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        The prefill takes memory from Windows ahead of need. It must never do
        so with the switch off, while the pool stops, without
        HighMemoryCondition, with LowMemoryCondition set or either signal
        missing, after the cache's idle delay, or once the cache is full.
        """
        policy = function(POOL.read_text(), 'AdmissionPoolPrefillAllowed')
        build_and_run(self, POLICY_HARNESS.replace('@@POLICY@@', policy))

    def test_prefill_is_opt_in(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        rc91: the prefill work item and the paging worker
        (AdmissionPagingQueueActive) share the system DelayedWorkQueue. While
        the prefill's MmAllocatePagesForMdlEx calls run (up to 1.6 s each),
        queued paging packets wait to start: Wom1G3PagingProfile
        DispatchToWorker max was 7.8-20 ms in every receipt EXP2015-EXP2021
        and 1.06-1.55 s in every one since rc88 (EXP2022-EXP2024; EXP2024
        10.5 s total). VidMm residency waits on those packets, so GravityMark
        D3D11's first frames grew from 1.8 s to 11.7 s (2529 -> 2279).
        The prefill must therefore be off unless REG_DWORD Wom1PoolPrefill=1
        asks for it (absent, mistyped or other values: off), every opened key
        is closed, and the pool start uses that reader.
        """
        residency_limit.PoolResidencyLimit.check_registry_reader(self, 'AdmissionPoolPrefillEnabled',
                                                 'Wom1PoolPrefill', 1)
        start = POOL.read_text()
        self.assertIn('pool->PrefillEnabled = AdmissionPoolPrefillEnabled(Context);', start)
        self.assertNotIn('AdmissionPoolPrefillDisabled', start)

    def test_worker_allocates_without_the_lock_and_stop_drains_it(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        The point of the prefill is that page allocation leaves the pool
        lock (paging and creations wait on it). Its work item must be counted
        in WorkPending before the lock is released, and Stop must take the
        lock once after setting Stopping, so no item is queued after the
        drain and the runtime is never freed under a running item.
        """
        source = POOL.read_text()
        work = function(source, 'AdmissionPoolPrefillWork')
        released = work.index('ExReleaseFastMutex(&pool->Lock);\n    if (!wanted) break;')
        alloc = work.index('slab = AdmissionPoolAllocSlab(&cost, pool->ConstructLargePages, ladder);')
        reacquired = work.index('ExAcquireFastMutex(&pool->Lock);', alloc)
        self.assertLess(released, alloc)
        self.assertLess(alloc, reacquired)
        self.assertIn('AdmissionPoolSlabAdopt(&pool->Slabs, slab)', work[reacquired:])
        self.assertIn('InterlockedDecrement(&pool->WorkPending);', work)
        queue = function(source, 'AdmissionPoolPrefillQueueLocked')
        self.assertLess(queue.index('InterlockedIncrement(&Pool->WorkPending);'),
                        queue.index('IoQueueWorkItem('))
        unlock = function(source, 'AdmissionMemoryPoolUnlock')
        self.assertLess(unlock.index('AdmissionPoolPrefillQueueLocked(pool);'),
                        unlock.index('ExReleaseFastMutex(&pool->Lock);'))
        stop = function(source, 'AdmissionPoolDemandStop')
        barrier = stop.index('ExAcquireFastMutex(&pool->Lock);')
        self.assertLess(stop.index('InterlockedExchange(&pool->Stopping, 1);'), barrier)
        self.assertLess(barrier, stop.index('InterlockedCompareExchange(&pool->WorkPending, 0, 0)'))
        locked = function(source, 'AdmissionPoolNewSlab')
        self.assertIn('AdmissionPoolAddSlabCost(pool, &cost);', locked)


if __name__ == '__main__':
    unittest.main()
