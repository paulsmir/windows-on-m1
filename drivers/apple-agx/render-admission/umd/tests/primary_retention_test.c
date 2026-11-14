/* PL-1 / PL-1b / PL-1c state machine (umd_primary_retention.h): deterministic
 * cases from the architect's specs (EXP2360; PL-1b after EXP2401/2402; PL-1c
 * after EXP2403).
 *
 * PL-1b regression: EXP2402 released the retained mode-setting primary right
 * after SetDisplayMode(new) returned and every release was followed by a hide
 * of the source; SetDisplayMode(new) supersedes the retained object instead.
 * PL-1c regression: EXP2403 released the oldest superseded object beyond a
 * bound of 4 and the loop returned at that first release; the superseded set
 * is now unbounded and nothing is released before device destroy.
 * Objects carry the pointer-sized chain link as their first field, as
 * ADMISSION_UMD_RETIREMENT::Next does. */
#include <stdio.h>
#include <stdlib.h>
#include "umd_primary_retention.h"

static unsigned int Failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++Failures; } } while (0)

int main(void) {
  typedef struct { void *Link; int Id; } OBJ;
  OBJ p1, p2, p3, s1;
  OBJ q[17];
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
  CHECK(st.SupersededCount == 1u && st.SupersededHead == &p1 &&
        st.SupersededTail == &p1);
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
  CHECK(st.SupersededCount == 1u && st.SupersededHead == &p1);
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
  /* PL-1c: sixteen mode sets after the first, each primary retained at its
   * destroy: no mode set returns anything to release; the chain holds them
   * oldest first; device destroy returns all 16 superseded + the current */
  for (i = 0u; i < 17u; ++i) {
    unsigned int allocation = 0x1000u + i * 0x40u;
    q[i].Id = (int)i;
    CHECK(AdmissionUmdRetentionSetDisplayMode(&st, allocation, 1) == 0);
    CHECK(AdmissionUmdRetentionDestroy(&st, &q[i], allocation, 1) == 1);
  }
  CHECK(st.SupersededCount == 16u && st.Retained == &q[16]);
  CHECK(st.SupersededHead == &q[0] && st.SupersededTail == &q[15]);
  {
    unsigned int calls = 0u, order = 1u;
    OBJ *first = (OBJ *)AdmissionUmdRetentionDeviceDestroy(&st);
    CHECK(first == &q[16]); /* the current slot first */
    if (first != 0) ++calls;
    while ((out = AdmissionUmdRetentionDeviceDestroy(&st)) != 0 && calls < 40u) {
      if (((OBJ *)out)->Id != (int)(calls - 1u)) order = 0u; /* oldest first */
      CHECK(((OBJ *)out)->Link == 0); /* unlinked when handed back */
      ++calls;
    }
    CHECK(calls == 17u && order == 1u);
  }
  CHECK(st.SupersededCount == 0u && st.SupersededHead == 0 && st.SupersededTail == 0);
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
