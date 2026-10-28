/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * Actual Windows admission must allow neutral memory and refuse LowMemory,
 * and pressure reclaim must drop only safe, surplus backing without aging. */
#ifdef __cplusplus
extern "C" {
#endif
#include "render_pool_demand.h"
#include "render_pool_slab.h"
#ifdef __cplusplus
}
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long long ULONGLONG;
typedef void VOID;
typedef void *PDEVICE_OBJECT, *PVOID;
typedef int BOOLEAN;
#define TRUE 1
#define FALSE 0
#define _Use_decl_annotations_
#define _In_
#define _In_opt_
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define G ADMISSION_POOL_DEMAND_GRANULE
#define N 128

typedef struct { long long QuadPart; } LARGE_INTEGER;
typedef struct _ADMISSION_CONTEXT ADMISSION_CONTEXT;
typedef struct {
    ADMISSION_POOL_DEMAND Demand;
    ADMISSION_POOL_SLABS Slabs;
    int *HighMemory, *LowMemory;
    ADMISSION_POOL_RESERVE_FAILURE LastReserveFailure, ReserveAttempt;
    ULONGLONG RefusalCounts[ADMISSION_POOL_REFUSAL_COUNT];
    ADMISSION_CONTEXT *Context;
    int WorkQueued, WorkPending, Stopping;
    ULONGLONG ReserveCalls, ReserveUs, ReserveUsMax, CacheDrops, LastReserveMs;
} ADMISSION_POOL_RUNTIME;
struct _ADMISSION_CONTEXT { ADMISSION_POOL_RUNTIME *PoolDemand; };
static ADMISSION_POOL_RUNTIME pool;
static ADMISSION_CONTEXT context = { &pool };
static ADMISSION_POOL_GRANULE granules[N];
static unsigned long long candidates[N];
static ADMISSION_POOL_SLAB *backing[N];
static unsigned slots[N];
static int high, low, locked, busy[N], broken[N];
static unsigned allocations, releases, unmaps;
static int fail_new, clear_low_after_read;
static unsigned low_on_allocation, fail_on_allocation;
static ULONGLONG now;
static int KeReadStateEvent(int *event) {
    int value = *event;
    if (event == &low && clear_low_after_read) {
        low = 0;
        clear_low_after_read = 0;
    }
    return value;
}
static ULONGLONG AdmissionPoolNowMs(void) { return now; }
static LARGE_INTEGER KeQueryPerformanceCounter(void *frequency) {
    LARGE_INTEGER value = { (long long)now };
    (void)frequency;
    return value;
}
static ULONGLONG AdmissionPoolElapsedUs(LARGE_INTEGER start) {
    return now - (ULONGLONG)start.QuadPart;
}
static int InterlockedExchange(int *p, int v) { int old = *p; *p = v; return old; }
static int InterlockedCompareExchange(int *p, int v, int expected) {
    int old = *p; if (old == expected) *p = v; return old;
}
static int InterlockedDecrement(int *p) { return --*p; }
static ADMISSION_POOL_DEMAND *AdmissionMemoryPoolLock(ADMISSION_CONTEXT *ctx) {
    assert(!locked++);
    return &ctx->PoolDemand->Demand;
}
static void AdmissionMemoryPoolUnlock(ADMISSION_CONTEXT *ctx) {
    assert(ctx == &context && locked-- == 1);
}
#include "pool_policy.inc"

static ADMISSION_POOL_SLAB *new_slab(void *opaque) {
    (void)opaque;
    ++allocations;
    if (fail_new || allocations == fail_on_allocation) return NULL;
    if (allocations == low_on_allocation) low = 1;
    return (ADMISSION_POOL_SLAB *)calloc(1, sizeof(ADMISSION_POOL_SLAB));
}
static void free_slab(void *opaque, ADMISSION_POOL_SLAB *slab) {
    (void)opaque;
    assert(slab->Live == 0);
    ++releases;
    free(slab);
}
static int commit(void *opaque, ULONGLONG first, ULONGLONG count) {
    (void)opaque;
    return AdmissionPoolSlabTake(&pool.Slabs, count, backing + first, slots + first);
}
static ADMISSION_POOL_RELEASE release(void *opaque, ULONGLONG index) {
    (void)opaque;
    assert(locked);
    ++unmaps;
    if (broken[index]) return AdmissionPoolReleaseFailed;
    if (busy[index]) return AdmissionPoolReleaseBusy;
    AdmissionPoolSlabGive(&pool.Slabs, backing[index], slots[index]);
    backing[index] = NULL;
    return AdmissionPoolReleased;
}
static void reset(void) {
    AdmissionPoolSlabDrain(&pool.Slabs);
    memset(&pool, 0, sizeof(pool));
    memset(granules, 0, sizeof(granules));
    memset(backing, 0, sizeof(backing));
    memset(busy, 0, sizeof(busy));
    memset(broken, 0, sizeof(broken));
    ADMISSION_POOL_DEMAND_OPS ops = { &pool, commit, release };
    assert(AdmissionPoolDemandInit(&pool.Demand, granules, candidates, N, &ops));
    pool.Demand.ReleaseDelay = 5000;
    pool.Slabs.Context = &pool;
    pool.Slabs.NewSlab = new_slab;
    pool.Slabs.FreeSlab = free_slab;
    pool.Slabs.MayGrow = AdmissionPoolMayGrow;
    pool.Slabs.SpareLimit = 1;
    pool.Slabs.ReserveCap = N;
    pool.HighMemory = &high;
    pool.LowMemory = &low;
    pool.Context = &context;
    high = 1; low = 0; now = 100;
    allocations = releases = unmaps = 0;
    fail_new = clear_low_after_read = 0;
    low_on_allocation = fail_on_allocation = 0;
}

/* Execute one periodic wake; there is no fake paging completion. */
static void worker_tick(void) {
    now += 500;
    pool.WorkQueued = pool.WorkPending = 1;
    AdmissionPoolReclaimWork(NULL, &pool);
    assert(!locked && !pool.WorkQueued && !pool.WorkPending);
}

/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * Process graph teardown can remove the final broker hold without executing
 * another paging record. Caching Busy until that unrelated epoch changes
 * leaks the backing forever. A timer wake retries, but only acknowledged
 * unmaps may free slots, and surviving allocations keep their promises. */
static void test_idle_worker_after_broker_release(void) {
    for (int pressure = 0; pressure != 2; ++pressure) {
        ADMISSION_POOL_PLACEMENT dead = {0}, live = {0};
        reset();
        assert(AdmissionMemoryPoolReserve(&context, 64 * G));
        assert(AdmissionMemoryPoolReserve(&context, 32 * G));
        assert(AdmissionPoolDemandPlace(&pool.Demand, &dead, 0, 64 * G) == AdmissionPoolPlaced);
        assert(AdmissionPoolDemandPlace(&pool.Demand, &live, 64 * G, 32 * G) == AdmissionPoolPlaced);
        AdmissionPoolDemandRecord(&pool.Demand, 0, 64 * G, 7);
        AdmissionMemoryPoolExecuted(&context, 7);
        AdmissionPoolDemandUnplace(&pool.Demand, &dead);
        AdmissionMemoryPoolUnreserve(&context, 64 * G);
        for (unsigned i = 0; i != 64; ++i) busy[i] = 1;
        high = !pressure;
        AdmissionMemoryPoolReclaim(&context);
        now += pool.Demand.ReleaseDelay;
        AdmissionMemoryPoolReclaim(&context);
        assert(unmaps == 64 && pool.Demand.Stats.BusyRetries == 64);
        assert(pool.Demand.Candidates == 64 && pool.Slabs.Stats.Slabs == 3);
        /* Frequent reserve/execution/destroy reclaim calls do not spin. */
        for (unsigned i = 0; i != 100; ++i) AdmissionMemoryPoolReclaim(&context);
        assert(unmaps == 64);
        worker_tick();
        assert(unmaps == 128 && releases == 0); /* still genuinely busy */
        for (unsigned i = 0; i != 64; ++i) busy[i] = 0;
        fail_new = 1;
        worker_tick();
        assert(pool.Demand.ExecutedSeq == 7 && pool.Slabs.Reserved == 32);
        assert(pool.Demand.Stats.CommittedGranules == 32 && !pool.Demand.Candidates);
        assert(unmaps == 192 && allocations == 3);
        assert(pool.Slabs.Stats.Slabs == (pressure ? 1u : 2u));
        assert(releases == (pressure ? 2u : 1u)); /* at most one empty spare */
        for (unsigned i = 64; i != 96; ++i)
            assert(backing[i] && granules[i].Owner == &live);
        /* A surviving, evicted allocation restores from its promise even
         * with physical allocation denied, then final destruction shrinks. */
        AdmissionPoolDemandUnplace(&pool.Demand, &live);
        high = 0;
        AdmissionMemoryPoolReclaim(&context);
        assert(pool.Slabs.Stats.Slabs == 1 && pool.Slabs.Reserved == 32);
        assert(AdmissionPoolDemandPlace(&pool.Demand, &live, 96 * G, 32 * G) == AdmissionPoolPlaced);
        assert(allocations == 3);
        AdmissionPoolDemandUnplace(&pool.Demand, &live);
        AdmissionMemoryPoolUnreserve(&context, 32 * G);
        worker_tick();
        assert(!pool.Slabs.Reserved && !pool.Slabs.Stats.Slabs && releases == 3);
    }
}

/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * Retrying the broker must not discard CPU/paging/owner/stop safety gates,
 * shorten idle aging, or reuse a slot after an unacknowledged hard failure. */
static void test_idle_worker_keeps_all_release_guards(void) {
    ADMISSION_POOL_PLACEMENT dead = {0}, live = {0};
    reset();
    assert(AdmissionPoolDemandPlace(&pool.Demand, &dead, 0, 7 * G) == AdmissionPoolPlaced);
    AdmissionPoolDemandUnplace(&pool.Demand, &dead);
    for (unsigned i = 0; i != 7; ++i) busy[i] = 1;
    high = 0;
    AdmissionMemoryPoolReclaim(&context);
    assert(unmaps == 7);
    for (unsigned i = 0; i != 7; ++i) busy[i] = 0;
    assert(AdmissionPoolDemandPin(&pool.Demand, 0, G));
    AdmissionPoolDemandPagingRef(&pool.Demand, G, 1);
    AdmissionPoolDemandRecord(&pool.Demand, 2 * G, G, 9);
    assert(AdmissionPoolDemandPlace(&pool.Demand, &live, 3 * G, G) == AdmissionPoolPlaced);
    broken[4] = 1;
    busy[5] = 1;
    worker_tick();
    assert(unmaps == 10 && pool.Demand.Stats.CommittedGranules == 6);
    assert(!backing[6] && granules[4].Pinned);
    worker_tick();
    assert(unmaps == 11 && pool.Demand.Stats.ReleaseFailures == 1);
    assert(backing[0] && backing[1] && backing[2] && backing[3] && backing[4] && backing[5]);
    pool.Stopping = 1;
    busy[5] = 0;
    worker_tick();
    assert(unmaps == 11 && backing[5]);

    reset();
    dead.Base = dead.Bytes = 0;
    assert(AdmissionPoolDemandPlace(&pool.Demand, &dead, 0, G) == AdmissionPoolPlaced);
    AdmissionPoolDemandUnplace(&pool.Demand, &dead);
    worker_tick();
    ULONGLONG first_idle = now;
    while (now - first_idle < pool.Demand.ReleaseDelay - 500) worker_tick();
    assert(unmaps == 0 && backing[0]);
    worker_tick();
    assert(unmaps == 1 && !backing[0]);
}
static void test_event_policy(void) {
    reset();
    /* HighMemory controls reclaim, not admission; LowMemory must be known. */
    for (int h = -1; h <= 1; ++h) {
        for (int l = -1; l <= 1; ++l) {
            high = h; low = l;
            pool.HighMemory = h < 0 ? NULL : &high;
            pool.LowMemory = l < 0 ? NULL : &low;
            assert(AdmissionPoolMayGrow(&pool) == (l == 0));
        }
    }
    reset();
    assert(AdmissionPoolSlabTake(&pool.Slabs, 1, backing, slots));
    AdmissionPoolSlabGive(&pool.Slabs, backing[0], slots[0]);
    high = 0;
    /* Do not discard a reusable spare before deciding whether creation
     * needs new physical pages. This promise fits the backing already held. */
    assert(AdmissionMemoryPoolReserve(&context, G));
    assert(allocations == 1 && releases == 0 && pool.Slabs.Reserved == 1);
    reset();
    assert(AdmissionMemoryPoolReserve(&context, 32 * G));
    high = 0; /* Neutral memory must permit fully backed creation. */
    assert(AdmissionMemoryPoolReserve(&context, G));
    assert(allocations == 2 && pool.Slabs.Reserved == 33);
    low = 1;
    assert(!AdmissionMemoryPoolReserve(&context, 32 * G));
    assert(allocations == 2 && pool.Slabs.Reserved == 33);
    assert(pool.Slabs.Stats.FloorRefusals == 1);
    assert(pool.Slabs.Stats.NewSlabFailures == 0);
    high = 1; low = 0;
    assert(AdmissionMemoryPoolReserve(&context, 32 * G));
    assert(allocations == 3);
}
static void test_admitted_paging_under_pressure(void) {
    ADMISSION_POOL_PLACEMENT owner = {0}, overlap = {0};
    reset();
    assert(AdmissionMemoryPoolReserve(&context, 32 * G));
    high = 0;
    assert(AdmissionPoolDemandPlace(&pool.Demand, &owner, 0, 32 * G) == AdmissionPoolPlaced);
    assert(allocations == 1); /* promised pages need no Windows allocation */
    /* A paging move's overlap can still take a new slab; gating this path
     * turns a recoverable creation refusal into a poisoned process. */
    assert(AdmissionPoolDemandPlace(&pool.Demand, &overlap, 32 * G, G) == AdmissionPoolPlaced);
    assert(allocations == 2 && pool.Slabs.Stats.FloorRefusals == 0);
}
static void test_pressure_reclaim(void) {
    ADMISSION_POOL_PLACEMENT owner = {0};
    reset();
    assert(AdmissionPoolDemandPlace(&pool.Demand, &owner, 0, 64 * G) == AdmissionPoolPlaced);
    AdmissionPoolDemandUnplace(&pool.Demand, &owner);
    AdmissionMemoryPoolReclaim(&context);
    assert(pool.Demand.Stats.CommittedGranules == 64 && releases == 0);
    high = 0;
    AdmissionMemoryPoolReclaim(&context);
    assert(pool.Demand.Stats.CommittedGranules == 0 && releases == 2);
    assert(pool.Slabs.SpareLimit == 1);
    /* Also drop an empty spare when there are no demand candidates. */
    reset();
    assert(AdmissionPoolSlabTake(&pool.Slabs, 1, backing, slots));
    AdmissionPoolSlabGive(&pool.Slabs, backing[0], slots[0]);
    high = 0;
    AdmissionMemoryPoolReclaim(&context);
    assert(pool.Slabs.Stats.Slabs == 0);
}
static void test_safety_and_promises(void) {
    ADMISSION_POOL_PLACEMENT owner = {0}, active = {0};
    reset();
    assert(AdmissionMemoryPoolReserve(&context, 63 * G));
    assert(AdmissionPoolDemandPlace(&pool.Demand, &owner, 0, 64 * G) == AdmissionPoolPlaced);
    assert(AdmissionPoolDemandPin(&pool.Demand, 0, G));
    AdmissionPoolDemandPagingRef(&pool.Demand, G, 1);
    AdmissionPoolDemandRecord(&pool.Demand, 2 * G, G, 7);
    busy[3] = 1;
    AdmissionPoolDemandUnplace(&pool.Demand, &owner);
    assert(AdmissionPoolDemandPlace(&pool.Demand, &active, 4 * G, G) == AdmissionPoolPlaced);
    high = 0;
    AdmissionMemoryPoolReclaim(&context);
    assert(pool.Demand.Stats.CommittedGranules == 5);
    for (int i = 0; i < 5; ++i) assert(granules[i].Committed);
    assert(pool.Slabs.Stats.Slabs == 2); /* promised backing stays */
    AdmissionMemoryPoolExecuted(&context, 6);
    assert(granules[2].Committed);
    AdmissionMemoryPoolExecuted(&context, 7);
    assert(!granules[2].Committed);
    busy[3] = 0;
    /* A BUSY broker unmap is retried only after paging makes progress.
     * Clearing the fixture's busy flag alone is not a new paging epoch. */
    AdmissionMemoryPoolExecuted(&context, 8);
    AdmissionPoolDemandPagingRef(&pool.Demand, G, -1);
    AdmissionPoolDemandUnplace(&pool.Demand, &active);
    /* Reserve also performs pressure reclaim, even if already backed. */
    assert(AdmissionMemoryPoolReserve(&context, G));
    assert(pool.Demand.Stats.CommittedGranules == 1 && granules[0].Pinned);
    assert(pool.Slabs.Reserved == 64 && allocations == 2);
}
/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * A notification can change between the admission check and failure capture.
 * The refusal receipt must retain the value actually used by the veto. */
static void test_failure_snapshot(void) {
    reset();
    assert(AdmissionMemoryPoolReserve(&context, 32 * G));
    high = 0; low = 1; clear_low_after_read = 1;
    assert(!AdmissionMemoryPoolReserve(&context, G));
    assert(pool.Slabs.Stats.FloorRefusals == 1);
    assert(pool.LastReserveFailure.RequestBytes == G);
    assert(pool.LastReserveFailure.ReservedBytes == 32 * G);
    assert(pool.LastReserveFailure.HeldBytes == 32 * G);
    assert(pool.LastReserveFailure.CommittedBytes == 0);
    assert(pool.LastReserveFailure.TimeMs == 100);
    assert(pool.LastReserveFailure.HighMemory == 0);
    assert(pool.LastReserveFailure.LowMemory == 1);
    assert(pool.LastReserveFailure.Reason == ADMISSION_POOL_REFUSAL_LOW_MEMORY);
    assert(pool.LastReserveFailure.RequiredGranules == 33);
    assert(pool.LastReserveFailure.HeldGranules == 32);
    assert(pool.LastReserveFailure.ReserveCapGranules == N);
    assert(pool.LastReserveFailure.PrivateGranules == 0);
    assert(pool.RefusalCounts[ADMISSION_POOL_REFUSAL_LOW_MEMORY - 1] == 1);
    high = 1; low = 0; now = 200;
    assert(AdmissionMemoryPoolReserve(&context, G));
    assert(pool.LastReserveFailure.TimeMs == 100); /* success preserves evidence */
    assert(pool.LastReserveFailure.Reason == ADMISSION_POOL_REFUSAL_LOW_MEMORY);
    assert(pool.RefusalCounts[ADMISSION_POOL_REFUSAL_LOW_MEMORY - 1] == 1);
    fail_new = 1; now = 300;
    assert(!AdmissionMemoryPoolReserve(&context, 64 * G));
    assert(pool.Slabs.Stats.FloorRefusals == 1);
    assert(pool.Slabs.Stats.NewSlabFailures == 1);
    assert(pool.LastReserveFailure.RequestBytes == 64 * G);
    assert(pool.LastReserveFailure.ReservedBytes == 33 * G);
    assert(pool.LastReserveFailure.TimeMs == 300);
    assert(pool.LastReserveFailure.HighMemory == 1);
    assert(pool.LastReserveFailure.Reason == ADMISSION_POOL_REFUSAL_BACKING);
    assert(pool.RefusalCounts[ADMISSION_POOL_REFUSAL_BACKING - 1] == 1);
    reset();
    pool.HighMemory = pool.LowMemory = NULL;
    assert(!AdmissionMemoryPoolReserve(&context, G));
    assert(pool.LastReserveFailure.HighMemory == 2);
    assert(pool.LastReserveFailure.LowMemory == 2);
    assert(pool.LastReserveFailure.HeldBytes == 0);
    assert(pool.LastReserveFailure.Reason == ADMISSION_POOL_REFUSAL_NO_SIGNAL);
    assert(pool.RefusalCounts[ADMISSION_POOL_REFUSAL_NO_SIGNAL - 1] == 1);
}

/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * Mid-request pressure/allocator failure must record the failing slab's
 * decision before rollback, return every new slab, and keep old promises.
 * Arithmetic refusal is not pressure and must not inherit stale samples. */
static void test_partial_growth_and_arithmetic_receipts(void) {
    for (int physical = 0; physical != 2; ++physical) {
        reset();
        assert(AdmissionMemoryPoolReserve(&context, 32 * G));
        high = 0;
        if (physical) fail_on_allocation = 3;
        else low_on_allocation = 2;
        assert(!AdmissionMemoryPoolReserve(&context, 64 * G));
        unsigned reason = physical ? ADMISSION_POOL_REFUSAL_BACKING :
                                     ADMISSION_POOL_REFUSAL_LOW_MEMORY;
        assert(pool.LastReserveFailure.Reason == reason);
        assert(pool.LastReserveFailure.RequiredGranules == 96);
        assert(pool.LastReserveFailure.HeldGranules == 64);
        assert(pool.LastReserveFailure.ReservedBytes == 32 * G);
        assert(pool.LastReserveFailure.HeldBytes == 32 * G);
        assert(pool.LastReserveFailure.HighMemory == 0);
        assert(pool.LastReserveFailure.LowMemory == (physical ? 0u : 1u));
        assert(pool.Slabs.Reserved == 32 && pool.Slabs.Stats.Slabs == 1);
        assert(releases == 1 && pool.RefusalCounts[reason - 1] == 1);
        /* Fully backed creation under pressure does not discard last refusal. */
        assert(AdmissionMemoryPoolReserve(&context, 0));
        assert(pool.LastReserveFailure.Reason == reason);
    }
    reset();
    pool.Slabs.Reserved = ~0ULL;
    assert(!AdmissionMemoryPoolReserve(&context, G));
    assert(pool.Slabs.Reserved == ~0ULL && allocations == 0);
    assert(pool.LastReserveFailure.Reason == ADMISSION_POOL_REFUSAL_ARITHMETIC);
    assert(pool.LastReserveFailure.RequiredGranules == 0);
    assert(pool.LastReserveFailure.HighMemory == 2);
    assert(pool.LastReserveFailure.LowMemory == 2);
    assert(pool.RefusalCounts[ADMISSION_POOL_REFUSAL_ARITHMETIC - 1] == 1);
    pool.Slabs.Reserved = 0;
}

/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * RC28: returning native backing on eviction while allocations remain live
 * makes the third serial page-in fail after a two-batch reserve is consumed.
 * Creation-only admission cannot repair this. Run the real Windows reserve,
 * reclaim and unreserve wrappers with every later OS allocation denied. */
static void test_serial_restore_and_destruction_shrink(void) {
    ADMISSION_POOL_PLACEMENT owners[3] = {{0}};
    reset();
    for (unsigned i = 0; i != 3; ++i) {
        assert(AdmissionMemoryPoolReserve(&context, 32 * G));
        assert(AdmissionPoolDemandPlace(&pool.Demand, &owners[i],
            i * 32 * G, 32 * G) == AdmissionPoolPlaced);
    }
    for (unsigned i = 0; i != 3; ++i)
        AdmissionPoolDemandUnplace(&pool.Demand, &owners[i]);
    high = 0; fail_new = 1;
    AdmissionMemoryPoolReclaim(&context);
    assert(pool.Demand.Stats.CommittedGranules == 0);
    assert(pool.Slabs.Stats.Slabs == 3 && pool.Slabs.Reserved == 96);
    unsigned before = allocations;
    for (unsigned i = 0; i != 3; ++i)
        assert(AdmissionPoolDemandPlace(&pool.Demand, &owners[i],
            i * 32 * G, 32 * G) == AdmissionPoolPlaced);
    assert(allocations == before && !pool.Demand.Stats.CommitFailures);
    /* Destroy one by one: remaining live objects keep their promise, and
     * acknowledged safe empty backing above that promise returns to the OS. */
    for (unsigned i = 0; i != 3; ++i) {
        AdmissionPoolDemandUnplace(&pool.Demand, &owners[i]);
        AdmissionMemoryPoolUnreserve(&context, 32 * G);
        AdmissionMemoryPoolReclaim(&context);
        assert(pool.Slabs.Reserved == (2 - i) * 32);
        assert(pool.Slabs.Stats.Slabs == 2 - i);
        assert(pool.Demand.Stats.CommittedGranules == (2 - i) * 32);
    }
    assert(releases == 3 && allocations == before);
}

/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * min(live,cap) must allow overcommitted allocations without extra locked
 * pages, yet retain a full segment across eviction and a change of residents.
 * Shrink begins only when destruction lowers the promise below the cap. */
static void test_over_cap_restore_uses_same_backing(void) {
    ADMISSION_POOL_PLACEMENT a = {0}, b = {0}, c = {0};
    reset();
    assert(AdmissionMemoryPoolReserve(&context, 64 * G));
    assert(AdmissionMemoryPoolReserve(&context, 64 * G));
    high = 0; fail_new = 1;
    assert(AdmissionMemoryPoolReserve(&context, 64 * G));
    assert(pool.Slabs.Reserved == 192 && pool.Slabs.Stats.Slabs == 4);
    assert(AdmissionPoolDemandPlace(&pool.Demand, &a, 0, 64 * G) == AdmissionPoolPlaced);
    assert(AdmissionPoolDemandPlace(&pool.Demand, &b, 64 * G, 64 * G) == AdmissionPoolPlaced);
    AdmissionPoolDemandUnplace(&pool.Demand, &a);
    AdmissionMemoryPoolReclaim(&context);
    assert(AdmissionPoolDemandPlace(&pool.Demand, &c, 0, 64 * G) == AdmissionPoolPlaced);
    AdmissionMemoryPoolUnreserve(&context, 64 * G); /* destroy evicted A */
    assert(pool.Slabs.Reserved == 128 && pool.Slabs.Stats.Slabs == 4);
    AdmissionPoolDemandUnplace(&pool.Demand, &b);
    AdmissionPoolDemandUnplace(&pool.Demand, &c);
    AdmissionMemoryPoolUnreserve(&context, 128 * G);
    AdmissionMemoryPoolReclaim(&context);
    assert(allocations == 4 && releases == 4);
    assert(!pool.Slabs.Reserved && !pool.Slabs.Stats.Slabs);
}
/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * rc86 (EXP2015b: 3116 slabs allocated for a 1632-slab peak): in high memory
 * a destroyed creation's empty slabs must serve the next one without asking
 * Windows again, bounded by the cache size. The M2-f7c/RC53 early reclaim
 * must still return every surplus slab as soon as HighMemory clears (on the
 * destroy itself), an idle cache must age out, and live promises stay. */
static void test_high_memory_slab_cache(void) {
    ADMISSION_POOL_PLACEMENT owner = {0};
    reset();
    pool.Slabs.SpareLimit = ADMISSION_POOL_CACHE_SLABS;
    assert(AdmissionMemoryPoolReserve(&context, 32 * G));     /* survivor */
    assert(AdmissionMemoryPoolReserve(&context, 96 * G));
    assert(AdmissionPoolDemandPlace(&pool.Demand, &owner, 32 * G, 96 * G) ==
           AdmissionPoolPlaced);
    assert(allocations == 4);
    AdmissionPoolDemandUnplace(&pool.Demand, &owner);
    AdmissionMemoryPoolReclaim(&context);
    now += pool.Demand.ReleaseDelay;
    AdmissionMemoryPoolReclaim(&context);                     /* aged out */
    assert(pool.Demand.Stats.CommittedGranules == 0);
    AdmissionMemoryPoolUnreserve(&context, 96 * G);
    assert(releases == 0 && pool.Slabs.Stats.Slabs == 4);     /* cached */
    assert(AdmissionMemoryPoolReserve(&context, 96 * G));
    assert(allocations == 4 && pool.Slabs.Stats.ReserveNoGrowth >= 1);
    /* HighMemory clears: the destroy returns all surplus at once. */
    high = 0;
    AdmissionMemoryPoolUnreserve(&context, 96 * G);
    assert(releases == 3 && pool.Slabs.Stats.Slabs == 1 && pool.Slabs.Reserved == 32);
    assert(pool.Slabs.SpareLimit == ADMISSION_POOL_CACHE_SLABS && pool.CacheDrops == 1);
    /* High again: the cache refills from use and ages out when idle. */
    high = 1;
    assert(AdmissionMemoryPoolReserve(&context, 96 * G));
    assert(allocations == 7);
    AdmissionMemoryPoolUnreserve(&context, 96 * G);
    assert(pool.Slabs.Stats.Slabs == 4);
    worker_tick();
    assert(pool.Slabs.Stats.Slabs == 4);                      /* recent use */
    now += ADMISSION_POOL_RELEASE_DELAY_MS;
    worker_tick();
    assert(pool.Slabs.Stats.Slabs == 1); /* the promised slab is the spare */
    assert(pool.Slabs.Reserved == 32 && pool.CacheDrops == 2);
    /* The cache never holds more than its bound of empty slabs. */
    reset();
    pool.Slabs.SpareLimit = 2;
    assert(AdmissionMemoryPoolReserve(&context, 128 * G));
    AdmissionMemoryPoolUnreserve(&context, 128 * G);
    assert(pool.Slabs.Stats.Slabs == 2 && releases == 2);
}
int main(void) {
    test_high_memory_slab_cache();
    test_idle_worker_after_broker_release();
    test_idle_worker_keeps_all_release_guards();
    test_event_policy();
    test_admitted_paging_under_pressure();
    test_pressure_reclaim();
    test_safety_and_promises();
    test_failure_snapshot();
    test_partial_growth_and_arithmetic_receipts();
    test_serial_restore_and_destruction_shrink();
    test_over_cap_restore_uses_same_backing();
    AdmissionPoolSlabDrain(&pool.Slabs);
    return 0;
}
