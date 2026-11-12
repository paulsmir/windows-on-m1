/* PL-1 state machine (umd_primary_retention.h): deterministic cases from the
 * architect's spec (EXP2360). */
#include <stdio.h>
#include <stdlib.h>
#include "umd_primary_retention.h"

static unsigned int Failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++Failures; } } while (0)

int main(void) {
  int p1, p2, s1;
  ADMISSION_UMD_PRIMARY_RETENTION st = {0u, 0, 0u};
  /* retain on destroy of the current mode-setting primary */
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x100u, 1) == 0);
  CHECK(AdmissionUmdRetentionDestroy(&st, &p1, 0x100u, 1) == 1);
  CHECK(st.Retained == &p1 && st.RetainedAllocation == 0x100u);
  /* release on the next successful SetDisplayMode of another allocation */
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x200u, 1) == &p1);
  CHECK(st.Retained == 0 && st.ModeAllocation == 0x200u);
  /* a failed SetDisplayMode changes nothing */
  CHECK(AdmissionUmdRetentionDestroy(&st, &p2, 0x200u, 1) == 1);
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x300u, 0) == 0);
  CHECK(st.Retained == &p2 && st.ModeAllocation == 0x200u);
  /* SetDisplayMode again with the retained allocation keeps it */
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x200u, 1) == 0);
  CHECK(st.Retained == &p2);
  /* release at device destroy */
  CHECK(AdmissionUmdRetentionDeviceDestroy(&st) == &p2);
  CHECK(st.Retained == 0 && st.ModeAllocation == 0u);
  CHECK(AdmissionUmdRetentionDeviceDestroy(&st) == 0);
  /* no retention for anything else: not the mode allocation, not
   * device-associated (shared / runtime-owned), no mode set yet */
  CHECK(AdmissionUmdRetentionDestroy(&st, &p1, 0x100u, 1) == 0);
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x400u, 1) == 0);
  CHECK(AdmissionUmdRetentionDestroy(&st, &s1, 0x500u, 1) == 0);
  CHECK(AdmissionUmdRetentionDestroy(&st, &s1, 0x400u, 0) == 0);
  CHECK(st.Retained == 0);
  /* destroy after replacement: P1 mode-set, P2 mode-set, then P1 destroyed
   * -> released normally (not retained) */
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x600u, 1) == 0);
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0x700u, 1) == 0);
  CHECK(AdmissionUmdRetentionDestroy(&st, &p1, 0x600u, 1) == 0);
  CHECK(AdmissionUmdRetentionDestroy(&st, &p2, 0x700u, 1) == 1);
  /* only one retained at a time */
  CHECK(AdmissionUmdRetentionDestroy(&st, &p1, 0x700u, 1) == 0);
  CHECK(st.Retained == &p2);
  /* null/zero arguments */
  CHECK(AdmissionUmdRetentionSetDisplayMode(0, 0x1u, 1) == 0);
  CHECK(AdmissionUmdRetentionSetDisplayMode(&st, 0u, 1) == 0 && st.ModeAllocation == 0x700u);
  CHECK(AdmissionUmdRetentionDestroy(&st, 0, 0x700u, 1) == 0);
  if (Failures) { fprintf(stderr, "primary_retention_test: %u failure(s)\n", Failures); return EXIT_FAILURE; }
  printf("primary_retention_test: OK\n");
  return EXIT_SUCCESS;
}
