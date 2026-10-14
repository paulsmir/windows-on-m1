#include "render_pool_demand.h"

#include <assert.h>
#include <string.h>

#define G ADMISSION_POOL_DEMAND_GRANULE
#define COUNT 64u /* 4 MiB segment */

static unsigned char mapped[COUNT], busy[COUNT], broken[COUNT];
static unsigned commits, commit_calls, releases, fail_commit_call;

/* One call backs a run of granules, all or nothing. */
static int fake_commit(void *context, unsigned long long first, unsigned long long count)
{
    (void)context;
    assert(count && first < COUNT && count <= COUNT - first);
    for (unsigned long long i = first; i < first + count; ++i)
        assert(!mapped[i]);
    if (++commit_calls == fail_commit_call)
        return 0;
    for (unsigned long long i = first; i < first + count; ++i)
        mapped[i] = 1;
    commits += (unsigned)count;
    return 1;
}

static ADMISSION_POOL_RELEASE fake_release(void *context, unsigned long long index)
{
    (void)context;
    assert(index < COUNT && mapped[index]);
    if (broken[index])
        return AdmissionPoolReleaseFailed;
    if (busy[index])
        return AdmissionPoolReleaseBusy;
    mapped[index] = 0;
    ++releases;
    return AdmissionPoolReleased;
}

static ADMISSION_POOL_GRANULE granules[COUNT];
static unsigned long long candidates[COUNT];
static ADMISSION_POOL_DEMAND pool;

static void reset(void)
{
    static const ADMISSION_POOL_DEMAND_OPS ops = {0, fake_commit, fake_release};
    memset(mapped, 0, sizeof(mapped));
    memset(busy, 0, sizeof(busy));
    memset(broken, 0, sizeof(broken));
    memset(granules, 0, sizeof(granules));
    memset(candidates, 0, sizeof(candidates));
    commits = commit_calls = releases = fail_commit_call = 0;
    assert(AdmissionPoolDemandInit(&pool, granules, candidates, COUNT, &ops));
}

static unsigned mapped_count(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < COUNT; ++i)
        n += mapped[i];
    return n;
}

static void test_place_commits_once_and_destroy_releases(void)
{
    ADMISSION_POOL_PLACEMENT a = {0, 0};
    reset();
    assert(AdmissionPoolDemandPlace(&pool, &a, 2 * G, 3 * G) == AdmissionPoolPlaced);
    assert(a.Base == 2 * G && a.Bytes == 3 * G);
    assert(mapped_count() == 3 && mapped[2] && mapped[4] && commits == 3);
    /* Every later PTE batch of the same placement is free. */
    assert(AdmissionPoolDemandPlace(&pool, &a, 2 * G, 3 * G) == AdmissionPoolPlaced);
    assert(commits == 3 && pool.Stats.CommittedGranules == 3 && pool.Stats.PeakGranules == 3);
    /* Owned granules never reclaim. */
    assert(AdmissionPoolDemandReclaim(&pool) == 0 && mapped_count() == 3);
    AdmissionPoolDemandUnplace(&pool, &a);
    assert(a.Bytes == 0);
    assert(AdmissionPoolDemandReclaim(&pool) == 3 && mapped_count() == 0);
    assert(pool.Stats.CommittedGranules == 0 && pool.Stats.PeakGranules == 3 &&
           pool.Stats.Releases == 3);
    /* Unplacing twice is harmless. */
    AdmissionPoolDemandUnplace(&pool, &a);
    assert(AdmissionPoolDemandReclaim(&pool) == 0);
}

static void test_invalid_placements(void)
{
    ADMISSION_POOL_PLACEMENT a = {0, 0};
    reset();
    assert(AdmissionPoolDemandPlace(&pool, &a, 0x1000, G) == AdmissionPoolPlaceInvalid);
    assert(AdmissionPoolDemandPlace(&pool, &a, 0, G + 0x1000) == AdmissionPoolPlaceInvalid);
    assert(AdmissionPoolDemandPlace(&pool, &a, 0, 0) == AdmissionPoolPlaceInvalid);
    assert(AdmissionPoolDemandPlace(&pool, &a, (COUNT - 1) * G, 2 * G) ==
           AdmissionPoolPlaceInvalid);
    assert(AdmissionPoolDemandPlace(&pool, &a, ~0ULL - G + 1, 2 * G) ==
           AdmissionPoolPlaceInvalid);
    assert(a.Bytes == 0 && mapped_count() == 0);
    /* The last granule is placeable. */
    assert(AdmissionPoolDemandPlace(&pool, &a, (COUNT - 1) * G, G) == AdmissionPoolPlaced);
}

static void test_commit_failure_owns_nothing(void)
{
    ADMISSION_POOL_PLACEMENT a = {0, 0}, b = {0, 0};
    reset();
    /* A failed run commits nothing. */
    fail_commit_call = 1;
    assert(AdmissionPoolDemandPlace(&pool, &a, 0, 4 * G) == AdmissionPoolPlaceNoMemory);
    assert(a.Bytes == 0 && pool.Stats.CommitFailures == 1 && mapped_count() == 0);
    /* Granule 1 is already committed: the placement needs runs [0] and [2, 3];
     * the second run fails and the first run's granule is not leaked. */
    reset();
    assert(AdmissionPoolDemandPlace(&pool, &b, G, G) == AdmissionPoolPlaced);
    AdmissionPoolDemandUnplace(&pool, &b);
    fail_commit_call = 3;
    assert(AdmissionPoolDemandPlace(&pool, &a, 0, 4 * G) == AdmissionPoolPlaceNoMemory);
    assert(a.Bytes == 0 && mapped[0] && mapped[1] && !mapped[2]);
    assert(AdmissionPoolDemandReclaim(&pool) == 2 && mapped_count() == 0);
}

/* M2-f3c: one commit call per run of uncommitted granules (EXP1258: one
 * MmAllocatePagesForMdlEx per 64 KiB cost 460-875 us). */
static void test_commit_runs(void)
{
    ADMISSION_POOL_PLACEMENT a = {0, 0}, b = {0, 0}, c = {0, 0};
    reset();
    assert(AdmissionPoolDemandPlace(&pool, &a, 2 * G, 6 * G) == AdmissionPoolPlaced);
    assert(commit_calls == 1 && commits == 6 && pool.Stats.CommitCalls == 1 &&
           pool.Stats.Commits == 6);
    /* Granules 4 and 5 stay committed; a placement over [0, 10) needs the
     * runs [0, 4) and [6, 10) around them... after A's release of the rest. */
    AdmissionPoolDemandUnplace(&pool, &a);
    assert(AdmissionPoolDemandPlace(&pool, &b, 4 * G, 2 * G) == AdmissionPoolPlaced);
    assert(AdmissionPoolDemandReclaim(&pool) == 4 && mapped_count() == 2);
    commit_calls = 0;
    assert(AdmissionPoolDemandPlace(&pool, &c, 0, 10 * G) == AdmissionPoolPlaced);
    assert(commit_calls == 2 && mapped_count() == 10 && b.Bytes == 0 && pool.Stats.Kills == 1);
    /* A run is capped so no single call backs an unbounded size. */
    reset();
    pool.MaxRun = 4;
    assert(AdmissionPoolDemandPlace(&pool, &a, 0, 10 * G) == AdmissionPoolPlaced);
    assert(commit_calls == 3 && mapped_count() == 10);
}

static void test_reused_range_kills_the_stale_placement(void)
{
    ADMISSION_POOL_PLACEMENT a = {0, 0}, b = {0, 0};
    reset();
    /* VidMm freed A's range without telling us and placed B over part of it. */
    assert(AdmissionPoolDemandPlace(&pool, &a, 4 * G, 4 * G) == AdmissionPoolPlaced);
    assert(AdmissionPoolDemandPlace(&pool, &b, 6 * G, 4 * G) == AdmissionPoolPlaced);
    assert(a.Bytes == 0 && pool.Stats.Kills == 1);
    /* B reused A's committed granules 6 and 7; only 8 and 9 were new. */
    assert(commits == 6 && mapped_count() == 6);
    /* A's granules 4 and 5 are no longer owned. */
    assert(AdmissionPoolDemandReclaim(&pool) == 2 && !mapped[4] && !mapped[5] && mapped[6]);
    /* A's later destroy changes nothing of B's. */
    AdmissionPoolDemandUnplace(&pool, &a);
    assert(AdmissionPoolDemandReclaim(&pool) == 0 && mapped_count() == 4);
}

static void test_move_releases_the_old_range(void)
{
    ADMISSION_POOL_PLACEMENT a = {0, 0};
    reset();
    assert(AdmissionPoolDemandPlace(&pool, &a, 0, 2 * G) == AdmissionPoolPlaced);
    assert(AdmissionPoolDemandPlace(&pool, &a, 10 * G, 2 * G) == AdmissionPoolPlaced);
    assert(a.Base == 10 * G && pool.Stats.Moves == 1 && mapped_count() == 4);
    assert(AdmissionPoolDemandReclaim(&pool) == 2 && !mapped[0] && mapped[10]);
    /* A move onto an overlapping range keeps the shared granules. */
    assert(AdmissionPoolDemandPlace(&pool, &a, 11 * G, 2 * G) == AdmissionPoolPlaced);
    assert(AdmissionPoolDemandReclaim(&pool) == 1 && !mapped[10] && mapped[11] && mapped[12]);
}

static void test_paging_refs_and_records_hold_a_released_range(void)
{
    ADMISSION_POOL_PLACEMENT a = {0, 0};
    reset();
    assert(AdmissionPoolDemandPlace(&pool, &a, 0, 2 * G) == AdmissionPoolPlaced);
    /* Eviction: the paging process maps the source, a transfer reads it, and the
     * placement is dropped before the transfer has executed. */
    AdmissionPoolDemandPagingRef(&pool, 0x3000, 1);
    AdmissionPoolDemandPagingRef(&pool, G + 0x1000, 1);
    AdmissionPoolDemandRecord(&pool, 0, 2 * G, 41);
    AdmissionPoolDemandUnplace(&pool, &a);
    assert(AdmissionPoolDemandReclaim(&pool) == 0 && mapped_count() == 2);
    AdmissionPoolDemandPagingRef(&pool, 0x3000, -1);
    AdmissionPoolDemandPagingRef(&pool, G + 0x1000, -1);
    /* Still held by record 41 until it executes. */
    AdmissionPoolDemandExecuted(&pool, 40);
    assert(AdmissionPoolDemandReclaim(&pool) == 0 && mapped_count() == 2);
    AdmissionPoolDemandExecuted(&pool, 41);
    assert(AdmissionPoolDemandReclaim(&pool) == 2 && mapped_count() == 0);
    /* An older Executed never moves the mark back. */
    AdmissionPoolDemandExecuted(&pool, 7);
    assert(pool.ExecutedSeq == 41);
}

static void test_pin_and_ref_underflow(void)
{
    reset();
    /* A reference outside the placement contract pins its granule. */
    assert(AdmissionPoolDemandPin(&pool, 3 * G + 0x2000, 0x1000));
    assert(mapped[3] && pool.Stats.PinnedGranules == 1);
    assert(AdmissionPoolDemandReclaim(&pool) == 0);
    /* Underflow is counted, never wraps. */
    AdmissionPoolDemandPagingRef(&pool, 5 * G, -1);
    assert(pool.Stats.RefUnderflows == 1 && granules[5].PagingRefs == 0);
}

static void test_busy_and_failed_releases(void)
{
    ADMISSION_POOL_PLACEMENT a = {0, 0};
    reset();
    assert(AdmissionPoolDemandPlace(&pool, &a, 0, 3 * G) == AdmissionPoolPlaced);
    AdmissionPoolDemandUnplace(&pool, &a);
    busy[1] = 1;
    broken[2] = 1;
    assert(AdmissionPoolDemandReclaim(&pool) == 1);
    assert(pool.Stats.BusyRetries == 1 && pool.Stats.ReleaseFailures == 1);
    assert(mapped[1] && mapped[2] && granules[2].Pinned);
    /* The busy granule retries; the failed one is never retried. */
    assert(AdmissionPoolDemandReclaim(&pool) == 0 && pool.Stats.BusyRetries == 2 &&
           pool.Stats.ReleaseFailures == 1);
    busy[1] = 0;
    assert(AdmissionPoolDemandReclaim(&pool) == 1 && !mapped[1] && mapped[2]);
    assert(pool.Candidates == 0);
}

static void test_recommit_after_release(void)
{
    ADMISSION_POOL_PLACEMENT a = {0, 0}, b = {0, 0};
    reset();
    assert(AdmissionPoolDemandPlace(&pool, &a, 0, G) == AdmissionPoolPlaced);
    AdmissionPoolDemandRecord(&pool, 0, G, 5);
    AdmissionPoolDemandExecuted(&pool, 5);
    AdmissionPoolDemandUnplace(&pool, &a);
    assert(AdmissionPoolDemandReclaim(&pool) == 1);
    /* A released granule commits again, with no stale record holding it. */
    assert(AdmissionPoolDemandPlace(&pool, &b, 0, G) == AdmissionPoolPlaced);
    assert(mapped[0] && commits == 2 && !granules[0].Recorded);
    AdmissionPoolDemandUnplace(&pool, &b);
    assert(AdmissionPoolDemandReclaim(&pool) == 1);
    /* A candidate that is placed again before reclaim leaves the list. */
    assert(AdmissionPoolDemandPlace(&pool, &a, 0, G) == AdmissionPoolPlaced);
    AdmissionPoolDemandUnplace(&pool, &a);
    assert(AdmissionPoolDemandPlace(&pool, &b, 0, G) == AdmissionPoolPlaced);
    assert(AdmissionPoolDemandReclaim(&pool) == 0 && pool.Candidates == 0 && mapped[0]);
}

/* M2-f3b (EXP1257: churn cost 8k commits/releases in one benchmark): an idle
 * granule stays committed for ReleaseDelay, so VidMm's reuse of a freed range
 * costs nothing; only granules idle that long go back to Windows. */
static void test_aged_release(void)
{
    ADMISSION_POOL_PLACEMENT a = {0, 0}, b = {0, 0};
    reset();
    pool.ReleaseDelay = 100;
    assert(AdmissionPoolDemandPlace(&pool, &a, 0, 2 * G) == AdmissionPoolPlaced);
    AdmissionPoolDemandUnplace(&pool, &a);
    /* First seen idle at 1000; not old enough until 1100. */
    assert(AdmissionPoolDemandReclaimAged(&pool, 1000) == 0 && mapped_count() == 2);
    assert(AdmissionPoolDemandReclaimAged(&pool, 1099) == 0 && mapped_count() == 2);
    /* Reused within the delay: no commit, no release. */
    assert(AdmissionPoolDemandPlace(&pool, &b, 0, G) == AdmissionPoolPlaced);
    assert(commits == 2);
    assert(AdmissionPoolDemandReclaimAged(&pool, 1100) == 1 && mapped[0] && !mapped[1]);
    /* Idle again: the clock restarts at the next pass. */
    AdmissionPoolDemandUnplace(&pool, &b);
    assert(AdmissionPoolDemandReclaimAged(&pool, 1150) == 0);
    assert(AdmissionPoolDemandReclaimAged(&pool, 1249) == 0 && mapped[0]);
    assert(AdmissionPoolDemandReclaimAged(&pool, 1250) == 1 && mapped_count() == 0);
    assert(pool.Candidates == 0);
}

static void test_reuse_between_passes_restarts_the_clock(void)
{
    ADMISSION_POOL_PLACEMENT a = {0, 0}, b = {0, 0};
    reset();
    pool.ReleaseDelay = 100;
    assert(AdmissionPoolDemandPlace(&pool, &a, 0, G) == AdmissionPoolPlaced);
    AdmissionPoolDemandUnplace(&pool, &a);
    assert(AdmissionPoolDemandReclaimAged(&pool, 1000) == 0);
    /* Used and freed again between two passes: idle only since 1101. */
    assert(AdmissionPoolDemandPlace(&pool, &b, 0, G) == AdmissionPoolPlaced);
    AdmissionPoolDemandUnplace(&pool, &b);
    assert(AdmissionPoolDemandReclaimAged(&pool, 1101) == 0 && mapped[0]);
    assert(AdmissionPoolDemandReclaimAged(&pool, 1200) == 0 && mapped[0]);
    assert(AdmissionPoolDemandReclaimAged(&pool, 1201) == 1 && !mapped[0]);
    /* A paging reference restarts it too. */
    assert(AdmissionPoolDemandPlace(&pool, &a, 0, G) == AdmissionPoolPlaced);
    AdmissionPoolDemandUnplace(&pool, &a);
    assert(AdmissionPoolDemandReclaimAged(&pool, 2000) == 0);
    AdmissionPoolDemandPagingRef(&pool, 0x1000, 1);
    AdmissionPoolDemandPagingRef(&pool, 0x1000, -1);
    assert(AdmissionPoolDemandReclaimAged(&pool, 2100) == 0 && mapped[0]);
    assert(AdmissionPoolDemandReclaimAged(&pool, 2200) == 1);
    /* The immediate pass ignores age. */
    assert(AdmissionPoolDemandPlace(&pool, &a, 0, G) == AdmissionPoolPlaced);
    AdmissionPoolDemandUnplace(&pool, &a);
    assert(AdmissionPoolDemandReclaim(&pool) == 1);
}

int main(void)
{
    test_place_commits_once_and_destroy_releases();
    test_invalid_placements();
    test_commit_failure_owns_nothing();
    test_commit_runs();
    test_reused_range_kills_the_stale_placement();
    test_move_releases_the_old_range();
    test_paging_refs_and_records_hold_a_released_range();
    test_pin_and_ref_underflow();
    test_busy_and_failed_releases();
    test_recommit_after_release();
    test_aged_release();
    test_reuse_between_passes_restarts_the_clock();
    return 0;
}
