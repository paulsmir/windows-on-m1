#include "render_pool_demand.h"

#include <assert.h>
#include <string.h>

#define G ADMISSION_POOL_DEMAND_GRANULE
#define COUNT 4096u /* 256 MiB segment, including multi-run placements */

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

/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * C75b: a large placement must own and commit its entire range, including
 * the tail past 16 MiB and past one bounded commit run. Reclaim must not
 * remove that CPU-accessible tail while the allocation owns it. */
static void test_large_placement_backs_the_tail(void)
{
    static const unsigned counts[] = {257u, 400u, 1024u, 1025u};
    for (unsigned c = 0; c < sizeof(counts) / sizeof(counts[0]); ++c) {
        ADMISSION_POOL_PLACEMENT a = {0, 0};
        unsigned n = counts[c];
        reset();
        pool.MaxRun = 1024u;
        assert(AdmissionPoolDemandPlace(&pool, &a, 7 * G, n * G) == AdmissionPoolPlaced);
        assert(commits == n && commit_calls == (n == 1025u ? 2u : 1u));
        for (unsigned i = 7; i < 7 + n; ++i)
            assert(mapped[i] && granules[i].Owner == &a);
        assert(!mapped[6] && !mapped[7 + n]);
        assert(AdmissionPoolDemandReclaim(&pool) == 0);
        assert(mapped[7 + n - 1]);
        AdmissionPoolDemandUnplace(&pool, &a);
        assert(AdmissionPoolDemandReclaim(&pool) == n && mapped_count() == 0);
    }
    /* A failure in the second run never advertises a partial placement. */
    {
        ADMISSION_POOL_PLACEMENT a = {0, 0};
        reset();
        pool.MaxRun = 1024u;
        fail_commit_call = 2u;
        assert(AdmissionPoolDemandPlace(&pool, &a, 7 * G, 1025 * G) == AdmissionPoolPlaceNoMemory);
        assert(a.Bytes == 0 && mapped_count() == 1024 && !mapped[1031]);
        assert(AdmissionPoolDemandReclaim(&pool) == 1024 && mapped_count() == 0);
    }
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
    /* Busy cannot spin on the same completed paging epoch. */
    assert(AdmissionPoolDemandReclaim(&pool) == 0 && pool.Stats.BusyRetries == 1 &&
           pool.Stats.ReleaseFailures == 1);
    for (unsigned i = 0; i < 1000; ++i) {
        assert(AdmissionPoolDemandReclaimAged(&pool, i) == 0);
        assert(AdmissionPoolDemandReclaim(&pool) == 0);
    }
    assert(pool.Stats.BusyRetries == 1);
    AdmissionPoolDemandExecuted(&pool, 0); /* first executed sequence */
    assert(AdmissionPoolDemandReclaim(&pool) == 0);
    assert(pool.Stats.BusyRetries == 2);
    AdmissionPoolDemandExecuted(&pool, 0); /* same: no progress */
    assert(AdmissionPoolDemandReclaim(&pool) == 0);
    assert(pool.Stats.BusyRetries == 2);
    AdmissionPoolDemandExecuted(&pool, 7);
    assert(AdmissionPoolDemandReclaim(&pool) == 0);
    assert(pool.Stats.BusyRetries == 3);
    AdmissionPoolDemandExecuted(&pool, 6); /* stale: no progress */
    busy[1] = 0;
    assert(AdmissionPoolDemandReclaim(&pool) == 0);
    AdmissionPoolDemandExecuted(&pool, 8);
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

/* M2-f4a: segment-3 size from Windows' own graphics-memory formula. */
static void test_segment_size(void)
{
    const unsigned long long MiB = 1ULL << 20, GiB = 1ULL << 30, win = 4 * GiB;
    /* J313: 6,279,270,400 B -> 50% = 2.92 GiB - 256 MiB, capped by the broker
     * at 2 GiB - 256 MiB. */
    assert(AdmissionPoolDemandSegmentBytes(6279270400ULL, win) == 1792 * MiB);
    /* 4 GiB of RAM: 2 GiB - 256 MiB = the broker cap too. */
    assert(AdmissionPoolDemandSegmentBytes(4 * GiB, win) == 1792 * MiB);
    /* 2 GiB: 1 GiB - 256 MiB. */
    assert(AdmissionPoolDemandSegmentBytes(2 * GiB, win) == 768 * MiB);
    /* 3 GiB - 1 MiB: 1.5 GiB - 0.5 MiB - 256 MiB, rounded down to 64 MiB. */
    assert(AdmissionPoolDemandSegmentBytes(3 * GiB - MiB, win) == 1216 * MiB);
    /* 1 GiB: 512 MiB - 256 MiB = 256 MiB, the minimum. */
    assert(AdmissionPoolDemandSegmentBytes(GiB, win) == 256 * MiB);
    /* Smaller machines get no pool. */
    assert(AdmissionPoolDemandSegmentBytes(GiB - 64 * MiB, win) == 0);
    /* 32 GiB: MIN(80%, MAX(16 GiB, 50%)) = 16 GiB, still broker-capped. */
    assert(AdmissionPoolDemandSegmentBytes(32 * GiB, win) == 1792 * MiB);
    /* M2-f6: with the broker table m1n1 sized from RAM (REG_BROKER_PAGES),
     * the formula and the window bound segment 3, not the 2 GiB table. J313:
     * 2.92 GiB - 256 MiB = 2738 MiB, in 64 MiB steps 2688 MiB. */
    assert(AdmissionPoolDemandSegmentBytesEx(6279270400ULL, win, 4608 * MiB) ==
           2688 * MiB);
    /* 16 and 32 GiB Macs: the 4 GiB window bounds it. */
    assert(AdmissionPoolDemandSegmentBytesEx(16 * GiB, win, 4608 * MiB) == 4 * GiB);
    assert(AdmissionPoolDemandSegmentBytesEx(32 * GiB, win, 4608 * MiB) == 4 * GiB);
    /* An unpublished table (0) is the built-in 2 GiB one; a small one caps. */
    assert(AdmissionPoolDemandSegmentBytesEx(6279270400ULL, win, 0) == 1792 * MiB);
    assert(AdmissionPoolDemandSegmentBytesEx(6279270400ULL, win, GiB) == 768 * MiB);
    assert(AdmissionPoolDemandSegmentBytesEx(6279270400ULL, win, 256 * MiB) == 0);
    /* A smaller window caps it. */
    assert(AdmissionPoolDemandSegmentBytes(8 * GiB, GiB) == GiB);
    assert(AdmissionPoolDemandSegmentBytes(8 * GiB, GiB + MiB) == GiB);
    /* Unknown RAM keeps the validated 1 GiB (within the window). */
    assert(AdmissionPoolDemandSegmentBytes(0, win) == GiB);
    assert(AdmissionPoolDemandSegmentBytes(0, 512 * MiB) == 512 * MiB);
}

/* rc109: an independent transcription of dxgmms2 0x74830 (system partition,
 * feature path) after the 0xaa3c8 clamps, in bytes. */
static unsigned long long vidmm_share(unsigned long long ram, unsigned p,
                                      unsigned m, unsigned c_mib)
{
    unsigned long long x9, x8, x11, cpu = (unsigned long long)c_mib << 20;
    unsigned g360;
    if (p <= 5) p = 5;
    if (p >= 100) p = 100;
    g360 = (m < 100 ? m : 100) <= p ? p : (m < 100 ? m : 100);
    x9 = p * ram / 100;
    x8 = g360 * ram / 100;
    x11 = ram > cpu && ram - cpu > x9 ? ram - cpu : x9;
    return x8 < x11 ? x8 : x11;
}

/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * dxgmms2 refuses the adapter (STATUS_UNSUCCESSFUL, "system policy
 * violated") when segment 3 exceeds VidMm's graphics share less 8 MiB, so a
 * larger pool must never outrun the machine policy VidMm actually holds;
 * Wom1PoolPercent=0 and Windows' default policy must keep the exact pre-rc109
 * size; a key written after boot must not enlarge the segment. */
static void test_graphics_policy(void)
{
    const unsigned long long MiB = 1ULL << 20, GiB = 1ULL << 30;
    const unsigned long long win = 4 * GiB, broker = 4608 * MiB;
    const unsigned long long j313 = 7218794496ULL; /* EXP2046 ram_bytes */
    static const unsigned percents[] = {0, 3, 5, 30, 49, 50, 60, 75, 80, 99, 100, 150};
    static const unsigned maxes[] = {0, 50, 80, 100, 150};
    static const unsigned cpu[] = {0, 4096, 16384};
    static const unsigned pool[] = {0, 1, 50, 67, 75, 100, 250};
    ADMISSION_POOL_GRAPHICS_POLICY policy;
    AdmissionPoolGraphicsPolicyDefault(&policy);
    assert(policy.Percent == 50 && policy.MaxPercent == 80 &&
           policy.CpuOnlyMiB == 16384 && policy.WrittenAfterBoot == 0);
    /* The defaults are the documented formula the pre-rc109 code used. */
    for (unsigned long long ram = 512 * MiB; ram <= 160 * GiB; ram += 333 * MiB) {
        unsigned long long pre = AdmissionPoolDemandSegmentBytesReserve(
            ram, win, broker, ADMISSION_POOL_DEMAND_APERTURE_RESERVE);
        assert(AdmissionPoolGraphicsShareBytes(ram, 0) ==
               vidmm_share(ram, 50, 80, 16384));
        assert(AdmissionPoolGraphicsShareBytes(ram, &policy) ==
               AdmissionPoolGraphicsShareBytes(ram, 0));
        for (unsigned i = 0; i != sizeof(pool) / sizeof(pool[0]); ++i)
            assert(AdmissionPoolDemandSegmentBytesShare(ram, win, broker,
                       ADMISSION_POOL_DEMAND_APERTURE_RESERVE,
                       AdmissionPoolDemandShareBytes(ram, &policy, pool[i])) == pre);
    }
    /* Never past VidMm's share less its margin, whatever the inputs. */
    for (unsigned long long ram = GiB; ram <= 96 * GiB; ram += 1777 * MiB)
    for (unsigned a = 0; a != sizeof(percents) / sizeof(percents[0]); ++a)
    for (unsigned b = 0; b != sizeof(maxes) / sizeof(maxes[0]); ++b)
    for (unsigned c = 0; c != sizeof(cpu) / sizeof(cpu[0]); ++c)
    for (unsigned d = 0; d != sizeof(pool) / sizeof(pool[0]); ++d)
    for (unsigned after = 0; after != 2; ++after) {
        unsigned long long vidmm = vidmm_share(ram, percents[a], maxes[b], cpu[c]);
        unsigned long long boot = vidmm_share(ram, 50, 80, 16384);
        unsigned long long share, seg;
        policy.Percent = percents[a]; policy.MaxPercent = maxes[b];
        policy.CpuOnlyMiB = cpu[c]; policy.WrittenAfterBoot = after;
        share = AdmissionPoolDemandShareBytes(ram, &policy, pool[d]);
        assert(AdmissionPoolGraphicsShareBytes(ram, &policy) == vidmm);
        seg = AdmissionPoolDemandSegmentBytesShare(ram, 1ULL << 40, 1ULL << 40,
                  ADMISSION_POOL_DEMAND_APERTURE_RESERVE_HALF, share);
        if (pool[d] == 0) {
            assert(share == boot); /* kill switch: the pre-rc109 formula */
            continue;
        }
        assert(share <= vidmm);
        assert(seg == 0 || seg + ADMISSION_POOL_POLICY_SEGMENT_MARGIN <= vidmm);
        if (after) assert(share <= boot);
        /* Wom1PoolPercent only grows the segment. */
        if (!after || vidmm >= boot)
            assert(share >= (vidmm < boot ? vidmm : boot));
    }
    /* J313 (6884 MiB): Windows' default policy keeps 3136 MiB. */
    AdmissionPoolGraphicsPolicyDefault(&policy);
    assert(AdmissionPoolDemandSegmentBytesShare(j313, win, broker,
               ADMISSION_POOL_DEMAND_APERTURE_RESERVE,
               AdmissionPoolDemandShareBytes(j313, &policy,
                                             ADMISSION_POOL_PERCENT_DEFAULT)) ==
           3136 * MiB);
    /* SystemPartitionCommitLimitPercentage=75: 5163 - 256 MiB, the 4 GiB
     * window binds; 60: 4130 - 256 MiB in 64 MiB steps. */
    policy.Percent = 75;
    assert(AdmissionPoolDemandSegmentBytesShare(j313, win, broker,
               ADMISSION_POOL_DEMAND_APERTURE_RESERVE,
               AdmissionPoolDemandShareBytes(j313, &policy, 75)) == 4 * GiB);
    assert(AdmissionPoolDemandSegmentBytesShare(j313, 8 * GiB, 8 * GiB,
               ADMISSION_POOL_DEMAND_APERTURE_RESERVE,
               AdmissionPoolDemandShareBytes(j313, &policy, 75)) == 4864 * MiB);
    policy.Percent = 60;
    assert(AdmissionPoolDemandSegmentBytesShare(j313, win, broker,
               ADMISSION_POOL_DEMAND_APERTURE_RESERVE,
               AdmissionPoolDemandShareBytes(j313, &policy, 75)) == 3840 * MiB);
    /* Written after boot: VidMm still holds the boot value. */
    policy.Percent = 75; policy.WrittenAfterBoot = 1;
    assert(AdmissionPoolDemandSegmentBytesShare(j313, win, broker,
               ADMISSION_POOL_DEMAND_APERTURE_RESERVE,
               AdmissionPoolDemandShareBytes(j313, &policy, 75)) == 3136 * MiB);
    /* A lowered policy is honoured (the pre-rc109 size would fail start). */
    policy.Percent = 40; policy.WrittenAfterBoot = 0;
    assert(AdmissionPoolDemandSegmentBytesShare(j313, win, broker,
               ADMISSION_POOL_DEMAND_APERTURE_RESERVE,
               AdmissionPoolDemandShareBytes(j313, &policy, 75)) == 2496 * MiB);
    /* Even with no aperture reserve the 8 MiB margin holds. */
    assert(AdmissionPoolDemandSegmentBytesShare(8 * GiB, win, 8 * GiB, 0,
                                                GiB + 4 * MiB) == 960 * MiB);
    /* Unknown RAM keeps the validated 1 GiB. */
    assert(AdmissionPoolDemandSegmentBytesShare(0, win, broker,
               ADMISSION_POOL_DEMAND_APERTURE_RESERVE, 0) == GiB);
}

int main(void)
{
    test_large_placement_backs_the_tail();
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
    test_segment_size();
    test_graphics_policy();
    return 0;
}
