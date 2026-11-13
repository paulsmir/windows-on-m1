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
  if (Failures) { fprintf(stderr, "mpo_hold_test: %u failure(s)\n", Failures); return EXIT_FAILURE; }
  printf("mpo_hold_test: OK\n");
  return EXIT_SUCCESS;
}
