#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpo_hold.h"

static unsigned int Failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); ++Failures; } } while (0)

/* EXP2375 pattern: layer 1 flips while the gate is held, then layer 0. */
static void TestMergeOfTwoLayersIntoOneSet(void) {
  ADMISSION_MPO_HOLD hold, taken;
  unsigned int l1 = 1u, l0 = 0u;
  unsigned long long id1 = 392u, id0 = 6428u;
  AdmissionMpoHoldClear(&hold);
  CHECK(AdmissionMpoHoldAdmit(&hold, 2u) == 1u);
  AdmissionMpoHoldMerge(&hold, 2u, 1u, &l1, &id1);
  CHECK(hold.Valid == 1u && hold.ListedMask == 2u && hold.Count == 1u);
  CHECK(AdmissionMpoHoldAdmit(&hold, 1u) == 1u);
  AdmissionMpoHoldMerge(&hold, 1u, 1u, &l0, &id0);
  CHECK(hold.ListedMask == 3u && hold.Count == 2u);
  /* One commit at the latch carries both, each with the accepted id. */
  CHECK(AdmissionMpoHoldTake(&hold, &taken) == 1u);
  CHECK(taken.Count == 2u && taken.Layer[0] == 1u && taken.PresentId[0] == 392u &&
        taken.Layer[1] == 0u && taken.PresentId[1] == 6428u);
  /* Exactly once. */
  CHECK(AdmissionMpoHoldTake(&hold, &taken) == 0u && hold.Valid == 0u);
}

static void TestSameLayerSecondFlipIsRefused(void) {
  ADMISSION_MPO_HOLD hold;
  unsigned int l1 = 1u;
  unsigned long long a = 10u, b = 11u;
  AdmissionMpoHoldClear(&hold);
  AdmissionMpoHoldMerge(&hold, 2u, 1u, &l1, &a);
  CHECK(AdmissionMpoHoldAdmit(&hold, 2u) == 0u);   /* -> DEVICE_BUSY */
  CHECK(AdmissionMpoHoldAdmit(&hold, 3u) == 0u);   /* any overlap */
  AdmissionMpoHoldMerge(&hold, 2u, 1u, &l1, &b);   /* ignored */
  CHECK(hold.Count == 1u && hold.PresentId[0] == 10u);
  /* Invalid masks. */
  CHECK(AdmissionMpoHoldAdmit(&hold, 0u) == 0u && AdmissionMpoHoldAdmit(&hold, 4u) == 0u);
}

static void TestRestoreAndClear(void) {
  ADMISSION_MPO_HOLD hold, taken;
  unsigned int layers[2] = {1u, 0u};
  unsigned long long ids[2] = {5u, 6u};
  AdmissionMpoHoldClear(&hold);
  AdmissionMpoHoldMerge(&hold, 3u, 2u, layers, ids);   /* a two-layer flip held */
  CHECK(AdmissionMpoHoldTake(&hold, &taken) == 1u);
  AdmissionMpoHoldRestore(&hold, &taken);              /* its commit met the gate */
  CHECK(hold.Valid == 1u && hold.Count == 2u && hold.PresentId[1] == 6u);
  CHECK(AdmissionMpoHoldAdmit(&hold, 1u) == 0u);       /* both layers pending */
  AdmissionMpoHoldClear(&hold);
  CHECK(hold.Valid == 0u && AdmissionMpoHoldAdmit(&hold, 3u) == 1u);
}


/* ---- HOLD-2 sequence model: Set3 never submits; the commit is issued by a
 * DPC queued from Set3 (after dxgkrnl's pass) or by the latch handler. ---- */
typedef struct {
  ADMISSION_MPO_HOLD Hold;
  unsigned int Outstanding, DpcQueued, Commits, DpcCommits, LatchCommits, TwoLayerCommits;
  unsigned int InFlightCount, InFlightLayer[2];
  unsigned long long InFlightId[2];
  unsigned long long Accepted[16], Reported[16];
  unsigned int AcceptedN, ReportedN;
} MODEL;
static void ModelInit(MODEL *M) { memset(M, 0, sizeof(*M)); AdmissionMpoHoldClear(&M->Hold); }
static unsigned int ModelSet3(MODEL *M, unsigned int Layer, unsigned long long Id) {
  unsigned int q = 9u;
  unsigned int r = AdmissionMpoHoldSet3(&M->Hold, 1u << Layer, 1u, 1u, &Layer, &Id,
                                        M->Outstanding == 0u ? 1u : 0u, &q);
  if (r != ADMISSION_MPO_SET3_BUSY) { M->Accepted[M->AcceptedN++] = Id; if (q) M->DpcQueued = 1u; }
  else CHECK(q == 0u);
  return r;
}
static void ModelCommit(MODEL *M, unsigned int FromDpc) {
  ADMISSION_MPO_HOLD taken;
  unsigned int i;
  if (M->Outstanding || !AdmissionMpoHoldTake(&M->Hold, &taken)) return;
  M->Outstanding = 1u; M->Commits++; if (FromDpc) M->DpcCommits++; else M->LatchCommits++;
  if (taken.ListedMask == 3u) M->TwoLayerCommits++;
  M->InFlightCount = taken.Count;
  for (i = 0u; i < taken.Count; ++i) { M->InFlightLayer[i] = taken.Layer[i]; M->InFlightId[i] = taken.PresentId[i]; }
}
static void ModelDpc(MODEL *M) { if (!M->DpcQueued) return; M->DpcQueued = 0u; ModelCommit(M, 1u); }
static void ModelLatch(MODEL *M) {
  unsigned int i;
  if (!M->Outstanding) return;
  for (i = 0u; i < M->InFlightCount; ++i) M->Reported[M->ReportedN++] = M->InFlightId[i];
  M->Outstanding = 0u; M->InFlightCount = 0u;
  ModelCommit(M, 0u); /* the latch handler commits a set pending behind it */
}
static void ExpectReportedEqualsAccepted(const MODEL *M) {
  unsigned int i, j, found;
  CHECK(M->ReportedN == M->AcceptedN);
  for (i = 0u; i < M->AcceptedN; ++i) {
    for (found = 0u, j = 0u; j < M->ReportedN; ++j) found += M->Reported[j] == M->Accepted[i];
    CHECK(found == 1u);
  }
}

static void TestHold2TwoSet3InOnePassOneCommit(void) {
  MODEL m; ModelInit(&m);
  CHECK(ModelSet3(&m, 0u, 100u) == ADMISSION_MPO_SET3_MERGED_FIRST);
  CHECK(ModelSet3(&m, 1u, 200u) == ADMISSION_MPO_SET3_MERGED_SECOND);
  CHECK(m.Commits == 0u && m.DpcQueued == 1u);           /* nothing submitted inside the pass */
  ModelDpc(&m);                                         /* after the pass */
  CHECK(m.Commits == 1u && m.DpcCommits == 1u && m.TwoLayerCommits == 1u && m.InFlightCount == 2u);
  ModelLatch(&m);
  CHECK(m.ReportedN == 2u && m.Commits == 1u);
  ExpectReportedEqualsAccepted(&m);
}

static void TestHold2LoneSet3CommitsFromTheDpc(void) {
  MODEL m; ModelInit(&m);
  CHECK(ModelSet3(&m, 1u, 7u) == ADMISSION_MPO_SET3_MERGED_FIRST && m.DpcQueued == 1u);
  ModelDpc(&m);
  CHECK(m.DpcCommits == 1u && m.TwoLayerCommits == 0u);
  ModelLatch(&m); ExpectReportedEqualsAccepted(&m);
}

static void TestHold2Set3WhileOutstandingCommitsAtTheLatch(void) {
  MODEL m; ModelInit(&m);
  ModelSet3(&m, 1u, 1u); ModelDpc(&m);                  /* one swap outstanding */
  CHECK(ModelSet3(&m, 1u, 2u) == ADMISSION_MPO_SET3_MERGED_FIRST);
  CHECK(m.DpcQueued == 0u);                             /* the latch will commit it */
  CHECK(ModelSet3(&m, 0u, 3u) == ADMISSION_MPO_SET3_MERGED_SECOND && m.DpcQueued == 0u);
  ModelLatch(&m);
  CHECK(m.LatchCommits == 1u && m.TwoLayerCommits == 1u && m.InFlightCount == 2u);
  ModelLatch(&m); ExpectReportedEqualsAccepted(&m);
}

static void TestHold2SameLayerSecondFlipIsBusy(void) {
  MODEL m; ModelInit(&m);
  CHECK(ModelSet3(&m, 1u, 10u) == ADMISSION_MPO_SET3_MERGED_FIRST);
  CHECK(ModelSet3(&m, 1u, 11u) == ADMISSION_MPO_SET3_BUSY); /* re-offered by dxgkrnl */
  ModelDpc(&m); ModelLatch(&m);
  CHECK(ModelSet3(&m, 1u, 11u) == ADMISSION_MPO_SET3_MERGED_FIRST);
  ModelDpc(&m); ModelLatch(&m);
  ExpectReportedEqualsAccepted(&m);
  { ADMISSION_MPO_HOLD h; unsigned int q = 5u, l = 0u; unsigned long long id = 1u; AdmissionMpoHoldClear(&h);
    CHECK(AdmissionMpoHoldSet3(&h, 1u, 0u, 1u, &l, &id, 1u, &q) == ADMISSION_MPO_SET3_BUSY && q == 0u && !h.Valid); }
}

static void TestHold2StopWithAQueuedDpc(void) {
  MODEL m; ModelInit(&m);
  ModelSet3(&m, 0u, 1u); ModelSet3(&m, 1u, 2u);
  CHECK(m.DpcQueued == 1u);
  m.DpcQueued = 0u; AdmissionMpoHoldClear(&m.Hold);    /* KeRemoveQueueDpc + flush, held set cleared */
  ModelDpc(&m);
  CHECK(m.Commits == 0u && !m.Hold.Valid);
}

static void TestHold2LongSequenceReportsEveryAcceptedIdOnce(void) {
  MODEL m; unsigned int f; unsigned long long id = 1000u;
  ModelInit(&m);
  for (f = 0u; f < 5u; ++f) {                           /* both layers per frame, interleaved phases */
    if (ModelSet3(&m, f & 1u, id) != ADMISSION_MPO_SET3_BUSY) id++;
    if (ModelSet3(&m, (f + 1u) & 1u, id) != ADMISSION_MPO_SET3_BUSY) id++;
    if (f & 1u) ModelLatch(&m);
    ModelDpc(&m);
    if (!(f & 1u)) ModelLatch(&m);
  }
  ModelLatch(&m); ModelDpc(&m); ModelLatch(&m);
  ExpectReportedEqualsAccepted(&m);
}


/* ---- HOLD-3 deadline model: period 1000, lead 300 -> deadline at 700 of
 * each frame; the commit is issued at the deadline (one swap per frame), at
 * once for a flip after it, at the latch for a swap outstanding. ---- */
static void TestHold3DeadlineFunction(void) {
  long long d = -1, w = -1;
  CHECK(AdmissionMpoHoldDeadline(1100, 1000, 1000, 300, &d, &w) == 1u && d == 1700 && w == 600);
  CHECK(AdmissionMpoHoldDeadline(1699, 1000, 1000, 300, &d, &w) == 1u && w == 1);
  CHECK(AdmissionMpoHoldDeadline(1700, 1000, 1000, 300, &d, &w) == 0u && d == 1700 && w == 0);
  CHECK(AdmissionMpoHoldDeadline(3200, 1000, 1000, 300, &d, &w) == 1u && d == 3700 && w == 500); /* missed ticks */
  CHECK(AdmissionMpoHoldDeadline(1100, 0, 1000, 300, &d, &w) == 0u);   /* no vsync yet: now */
  CHECK(AdmissionMpoHoldDeadline(900, 1000, 1000, 300, &d, &w) == 0u);  /* clock before vsync: now */
  CHECK(AdmissionMpoHoldDeadline(1100, 1000, 1000, 1000, &d, &w) == 0u); /* lead >= period */
}

typedef struct { MODEL M; long long Now, LastVsync, TimerAt; unsigned int TimerArmed, DeadlineCommits, LateCommits; } MODEL3;
static void M3Dpc(MODEL3 *T) {                          /* the HOLD-2 DPC, HOLD-3 rules */
  long long d, w;
  if (!T->M.DpcQueued) return;
  T->M.DpcQueued = 0u;
  if (T->M.Outstanding || !T->M.Hold.Valid) return;
  if (AdmissionMpoHoldDeadline(T->Now, T->LastVsync, 1000, 300, &d, &w)) {
    if (!T->TimerArmed) { T->TimerArmed = 1u; T->TimerAt = d; }
  } else { ModelCommit(&T->M, 1u); T->LateCommits++; }
}
static void M3Advance(MODEL3 *T, long long To) {        /* timer fires at its time */
  T->Now = To;
  if (T->TimerArmed && T->Now >= T->TimerAt) {
    T->TimerArmed = 0u;
    if (!T->M.Outstanding && T->M.Hold.Valid) { ModelCommit(&T->M, 1u); T->DeadlineCommits++; }
  }
}
static void M3Latch(MODEL3 *T, long long At) { M3Advance(T, At); T->LastVsync = At; ModelLatch(&T->M); }
static void M3Init(MODEL3 *T) { memset(T, 0, sizeof(*T)); ModelInit(&T->M); T->LastVsync = 1000; T->Now = 1000; }

static void TestHold3TwoPhasesOneCommitAtTheDeadline(void) {
  MODEL3 t; M3Init(&t);
  M3Advance(&t, 1150); CHECK(ModelSet3(&t.M, 1u, 1u) == ADMISSION_MPO_SET3_MERGED_FIRST); M3Dpc(&t);
  CHECK(t.TimerArmed && t.TimerAt == 1700 && t.M.Commits == 0u);
  M3Advance(&t, 1400); CHECK(ModelSet3(&t.M, 0u, 2u) == ADMISSION_MPO_SET3_MERGED_SECOND); M3Dpc(&t);
  CHECK(t.M.Commits == 0u);                             /* still before the deadline */
  M3Advance(&t, 1700);
  CHECK(t.M.Commits == 1u && t.DeadlineCommits == 1u && t.M.TwoLayerCommits == 1u);
  M3Latch(&t, 2000); ExpectReportedEqualsAccepted(&t.M);
}
static void TestHold3FlipAfterTheDeadlineCommitsAtOnce(void) {
  MODEL3 t; M3Init(&t);
  M3Advance(&t, 1800); ModelSet3(&t.M, 1u, 5u); M3Dpc(&t);
  CHECK(t.LateCommits == 1u && !t.TimerArmed && t.M.Commits == 1u);
  M3Latch(&t, 2000); ExpectReportedEqualsAccepted(&t.M);
}
static void TestHold3OutstandingAtTheDeadlineCommitsAtTheLatch(void) {
  MODEL3 t; M3Init(&t);
  M3Advance(&t, 1800); ModelSet3(&t.M, 1u, 1u); M3Dpc(&t);  /* late: outstanding */
  M3Advance(&t, 1900); ModelSet3(&t.M, 1u, 2u); M3Dpc(&t);  /* outstanding: the latch commits */
  CHECK(t.M.Commits == 1u && !t.TimerArmed);
  M3Latch(&t, 2000);
  CHECK(t.M.Commits == 2u && t.M.LatchCommits == 1u);
  M3Latch(&t, 3000); ExpectReportedEqualsAccepted(&t.M);
}
static void TestHold3SameLayerBeforeTheDeadlineIsBusy(void) {
  MODEL3 t; M3Init(&t);
  M3Advance(&t, 1100); ModelSet3(&t.M, 1u, 1u); M3Dpc(&t);
  M3Advance(&t, 1200); CHECK(ModelSet3(&t.M, 1u, 2u) == ADMISSION_MPO_SET3_BUSY);
  M3Advance(&t, 1700); M3Latch(&t, 2000);
  CHECK(ModelSet3(&t.M, 1u, 2u) == ADMISSION_MPO_SET3_MERGED_FIRST);
  M3Dpc(&t); M3Advance(&t, 2700); M3Latch(&t, 3000);
  ExpectReportedEqualsAccepted(&t.M);
}
static void TestHold3IdleArmsNoTimer(void) {
  MODEL3 t; unsigned int f; M3Init(&t);
  for (f = 1u; f <= 5u; ++f) { M3Dpc(&t); M3Latch(&t, 1000 + 1000 * (long long)f); }
  CHECK(!t.TimerArmed && t.M.Commits == 0u);
}
static void TestHold3StopWithAnArmedTimer(void) {
  MODEL3 t; M3Init(&t);
  M3Advance(&t, 1100); ModelSet3(&t.M, 0u, 1u); M3Dpc(&t);
  CHECK(t.TimerArmed);
  t.TimerArmed = 0u; AdmissionMpoHoldClear(&t.M.Hold);  /* ExDeleteTimer(cancel, wait) + clear */
  M3Advance(&t, 1700);
  CHECK(t.M.Commits == 0u);
}

int main(void) {
  TestMergeOfTwoLayersIntoOneSet();
  TestSameLayerSecondFlipIsRefused();
  TestRestoreAndClear();
  TestHold2TwoSet3InOnePassOneCommit();
  TestHold2LoneSet3CommitsFromTheDpc();
  TestHold2Set3WhileOutstandingCommitsAtTheLatch();
  TestHold2SameLayerSecondFlipIsBusy();
  TestHold2StopWithAQueuedDpc();
  TestHold2LongSequenceReportsEveryAcceptedIdOnce();
  TestHold3DeadlineFunction();
  TestHold3TwoPhasesOneCommitAtTheDeadline();
  TestHold3FlipAfterTheDeadlineCommitsAtOnce();
  TestHold3OutstandingAtTheDeadlineCommitsAtTheLatch();
  TestHold3SameLayerBeforeTheDeadlineIsBusy();
  TestHold3IdleArmsNoTimer();
  TestHold3StopWithAnArmedTimer();
  if (Failures) { fprintf(stderr, "mpo_hold_test: %u failure(s)\n", Failures); return EXIT_FAILURE; }
  printf("mpo_hold_test: OK\n");
  return EXIT_SUCCESS;
}
