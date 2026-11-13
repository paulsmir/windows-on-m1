/* PL-1 / PL-1b state machine (umd_primary_retention.h): deterministic cases
 * from the architect's specs (EXP2360; PL-1b after EXP2401/2402).
 *
 * PL-1b regression: EXP2402 released the retained mode-setting primary right
 * after SetDisplayMode(new) returned (151 path-2 rows for 152 mode sets) and
 * every release was followed by a hide of the source.  SetDisplayMode(new)
 * now supersedes the retained object instead of returning it; only beyond
 * the bound is the oldest superseded object returned for release. */
#include <stdio.h>
#include <stdlib.h>
#include "umd_primary_retention.h"

static unsigned int Failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++Failures; } } while (0)

int main(void) {
  int p1, p2, p3, s1;
  int q[ADMISSION_UMD_RETENTION_SUPERSEDED_MAX + 2u];
  unsigned int i;
  void *out;
  ADMISSION_UMD_PRIMARY_RETENTION st = {0};
  /* retain on destroy of the current mode-setting primary */
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x100u, 1) == 0);
  CHECK(AdmissionUmdRetentionDestroy(&st, &p1, 0x100u, 1) == 1);
  CHECK(st.Retained == &p1 && st.RetainedAllocation == 0x100u);
  /* PL-1b: the next successful SetDisplayMode of another allocation does NOT
   * release it; it is superseded and the current slot is free */
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x200u, 1) == 0);
  CHECK(st.Retained == 0 && st.RetainedAllocation == 0u &&
        st.ModeAllocation == 0x200u);
  CHECK(st.SupersededCount == 1u && st.Superseded[0] == &p1 &&
        st.SupersededAllocation[0] == 0x100u);
  /* the new mode-setting primary is retained at its destroy while one is
   * superseded */
  CHECK(AdmissionUmdRetentionDestroy(&st, &p2, 0x200u, 1) == 1);
  CHECK(st.Retained == &p2 && st.SupersededCount == 1u);
  /* a failed SetDisplayMode changes nothing */
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x300u, 0) == 0);
  CHECK(st.Retained == &p2 && st.ModeAllocation == 0x200u &&
        st.SupersededCount == 1u);
  /* allocation 0 changes nothing */
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0u, 1) == 0);
  CHECK(st.Retained == &p2 && st.ModeAllocation == 0x200u &&
        st.SupersededCount == 1u);
  /* SetDisplayMode again with the retained allocation changes nothing */
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x200u, 1) == 0);
  CHECK(st.Retained == &p2 && st.ModeAllocation == 0x200u &&
        st.SupersededCount == 1u);
  /* a destroy of a superseded allocation cannot happen (it was retained at
   * its destroy); if it did, it is neither retained twice nor touches the
   * list */
  CHECK(AdmissionUmdRetentionDestroy(&st, &s1, 0x100u, 1) == 0);
  CHECK(st.SupersededCount == 1u && st.Superseded[0] == &p1);
  /* only one retained in the current slot at a time */
  CHECK(AdmissionUmdRetentionDestroy(&st, &p3, 0x200u, 1) == 0);
  CHECK(st.Retained == &p2);
  /* device destroy hands back the current and every superseded object,
   * one per call, then NULL; the state is cleared */
  {
    int seenP1 = 0, seenP2 = 0, calls = 0;
    while ((out = AdmissionUmdRetentionDeviceDestroy(&st)) != 0 && calls < 8) {
      if (out == &p1) ++seenP1;
      if (out == &p2) ++seenP2;
      ++calls;
    }
    CHECK(calls == 2 && seenP1 == 1 && seenP2 == 1);
  }
  CHECK(st.Retained == 0 && st.ModeAllocation == 0u &&
        st.SupersededCount == 0u);
  CHECK(AdmissionUmdRetentionDeviceDestroy(&st) == 0);
  /* no retention for anything else: not the mode allocation, not
   * device-associated (shared / runtime-owned), no mode set yet */
  CHECK(AdmissionUmdRetentionDestroy(&st, &p1, 0x100u, 1) == 0);
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x400u, 1) == 0);
  CHECK(AdmissionUmdRetentionDestroy(&st, &s1, 0x500u, 1) == 0);
  CHECK(AdmissionUmdRetentionDestroy(&st, &s1, 0x400u, 0) == 0);
  CHECK(st.Retained == 0 && st.SupersededCount == 0u);
  /* destroy after replacement: P1 mode-set, P2 mode-set, then P1 destroyed
   * -> released normally (not retained, nothing superseded) */
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x600u, 1) == 0);
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x700u, 1) == 0);
  CHECK(st.SupersededCount == 0u);
  CHECK(AdmissionUmdRetentionDestroy(&st, &p1, 0x600u, 1) == 0);
  CHECK(AdmissionUmdRetentionDestroy(&st, &p2, 0x700u, 1) == 1);
  CHECK(st.Retained == &p2);
  out = AdmissionUmdRetentionDeviceDestroy(&st);
  CHECK(out == &p2 && AdmissionUmdRetentionDeviceDestroy(&st) == 0);
  /* bound: each mode set supersedes the previous retained primary; up to
   * the bound nothing is released, beyond it the OLDEST superseded object is
   * returned, one per mode set */
  for (i = 0u; i < ADMISSION_UMD_RETENTION_SUPERSEDED_MAX + 2u; ++i) {
    unsigned int allocation = 0x1000u + i * 0x40u;
    out = AdmissionUmdRetentionSetDisplayMode(&st, allocation, 1);
    if (i <= ADMISSION_UMD_RETENTION_SUPERSEDED_MAX)
      CHECK(out == 0);
    else
      CHECK(out == &q[i - ADMISSION_UMD_RETENTION_SUPERSEDED_MAX - 1u]);
    CHECK(AdmissionUmdRetentionDestroy(&st, &q[i], allocation, 1) == 1);
  }
  CHECK(st.SupersededCount == ADMISSION_UMD_RETENTION_SUPERSEDED_MAX);
  CHECK(st.Retained == &q[ADMISSION_UMD_RETENTION_SUPERSEDED_MAX + 1u]);
  out = AdmissionUmdRetentionSetDisplayMode(&st, 0x9000u, 1);
  CHECK(out == &q[1]); /* q[0] went at the previous mode set */
  CHECK(st.Retained == 0 &&
        st.Superseded[ADMISSION_UMD_RETENTION_SUPERSEDED_MAX - 1u] ==
            &q[ADMISSION_UMD_RETENTION_SUPERSEDED_MAX + 1u] &&
        st.SupersededAllocation[ADMISSION_UMD_RETENTION_SUPERSEDED_MAX - 1u] ==
            0x1000u + (ADMISSION_UMD_RETENTION_SUPERSEDED_MAX + 1u) * 0x40u);
  /* device destroy returns all held objects (the bound's worth) */
  {
    unsigned int calls = 0u;
    while (AdmissionUmdRetentionDeviceDestroy(&st) != 0 && calls < 16u)
      ++calls;
    CHECK(calls == ADMISSION_UMD_RETENTION_SUPERSEDED_MAX);
  }
  /* null/zero arguments */
  CHECK(AdmissionUmdRetentionSetDisplayMode(0, 0x1u, 1) == 0);
  CHECK(AdmissionUmdRetentionDestroy(0, &p1, 0x1u, 1) == 0);
  CHECK(AdmissionUmdRetentionDeviceDestroy(0) == 0);
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x700u, 1) == 0);
  CHECK(AdmissionUmdRetentionDestroy(&st, 0, 0x700u, 1) == 0);
  if (Failures) { fprintf(stderr, "primary_retention_test: %u failure(s)\n", Failures); return EXIT_FAILURE; }
  printf("primary_retention_test: OK\n");
  return EXIT_SUCCESS;
}
