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

int main(void) {
  TestMergeOfTwoLayersIntoOneSet();
  TestSameLayerSecondFlipIsRefused();
  TestRestoreAndClear();
  if (Failures) { fprintf(stderr, "mpo_hold_test: %u failure(s)\n", Failures); return EXIT_FAILURE; }
  printf("mpo_hold_test: OK\n");
  return EXIT_SUCCESS;
}
