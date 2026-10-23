/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * Actual Windows admission must refuse new locked pages before LowMemory,
 * and pressure reclaim must drop only safe, surplus backing without aging. */
#include "render_pool_demand.h"
#include "render_pool_slab.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long long ULONGLONG;
typedef void VOID;
typedef int BOOLEAN;
#define TRUE 1
#define FALSE 0
#define _Use_decl_annotations_
#define G ADMISSION_POOL_DEMAND_GRANULE
#define N 128

typedef struct {
    ADMISSION_POOL_DEMAND Demand;
    ADMISSION_POOL_SLABS Slabs;
    int *HighMemory, *LowMemory;
} ADMISSION_POOL_RUNTIME;
typedef struct { ADMISSION_POOL_RUNTIME *PoolDemand; } ADMISSION_CONTEXT;
static ADMISSION_POOL_RUNTIME pool;
static ADMISSION_CONTEXT context = { &pool };
static ADMISSION_POOL_GRANULE granules[N];
static unsigned long long candidates[N];
static ADMISSION_POOL_SLAB *backing[N];
static unsigned slots[N];
static int high, low, locked, busy[N];
static unsigned allocations, releases;
static ULONGLONG now;
static int KeReadStateEvent(int *event) { return *event; }
static ULONGLONG AdmissionPoolNowMs(void) { return now; }
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
    return calloc(1, sizeof(ADMISSION_POOL_SLAB));
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
    high = 1; low = 0; now = 100;
    allocations = releases = 0;
}
static void test_event_policy(void) {
    reset();
    /* Missing high is unknown, not permission to consume more pages. */
    for (int h = -1; h <= 1; ++h) {
        for (int l = -1; l <= 1; ++l) {
            high = h; low = l;
            pool.HighMemory = h < 0 ? NULL : &high;
            pool.LowMemory = l < 0 ? NULL : &low;
            assert(AdmissionPoolMayGrow(&pool) == (h == 1 && l != 1));
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
    high = 0; /* low is still clear: C73 wrongly admits another slab here */
    assert(!AdmissionMemoryPoolReserve(&context, G));
    assert(allocations == 1 && pool.Slabs.Reserved == 32);
    assert(pool.Slabs.Stats.FloorRefusals == 1);
    assert(pool.Slabs.Stats.NewSlabFailures == 0);
    high = 1;
    assert(AdmissionMemoryPoolReserve(&context, G));
    assert(allocations == 2);
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
    AdmissionPoolDemandPagingRef(&pool.Demand, G, -1);
    AdmissionPoolDemandUnplace(&pool.Demand, &active);
    /* Reserve also performs pressure reclaim, even if already backed. */
    assert(AdmissionMemoryPoolReserve(&context, G));
    assert(pool.Demand.Stats.CommittedGranules == 1 && granules[0].Pinned);
    assert(pool.Slabs.Reserved == 64 && allocations == 2);
}
int main(void) {
    test_event_policy();
    test_admitted_paging_under_pressure();
    test_pressure_reclaim();
    test_safety_and_promises();
    AdmissionPoolSlabDrain(&pool.Slabs);
    return 0;
}
