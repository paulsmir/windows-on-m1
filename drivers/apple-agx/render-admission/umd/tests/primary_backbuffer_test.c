/* BP-1 mapping (umd_primary_backbuffer.h) and its interaction with the PL-1
 * retention (umd_primary_retention.h): every input combination, host-only. */
#include <assert.h>
#include <stdio.h>

#include "umd_primary_backbuffer.h"
#include "umd_primary_retention.h"

#define A8R8G8B8 21u /* D3DDDIFMT_A8R8G8B8, wdk26100 d3dukmdt.h */
#define A8B8G8R8 32u /* D3DDDIFMT_A8B8G8R8 */

int main(void) {
  int desc, shared, misc, request;
  unsigned int displayable;
  /* Displayable is a property of the storage format alone. */
  assert(AdmissionUmdStorageDisplayable(A8R8G8B8) == 1u);
  assert(AdmissionUmdStorageDisplayable(A8B8G8R8) == 0u);
  assert(AdmissionUmdStorageDisplayable(0u) == 0u);
  for (desc = 0; desc < 2; ++desc)
    for (shared = 0; shared < 2; ++shared)
      for (misc = 0; misc < 2; ++misc)
        for (displayable = 0u; displayable < 2u; ++displayable)
          for (request = 0; request < 2; ++request) {
            const unsigned int flags =
                request ? ADMISSION_UMD_CREATE_RUNTIME_PRIMARY : 0u;
            const ADMISSION_UMD_PRIMARY_MAPPING m =
                AdmissionUmdPrimaryMap(desc, shared, misc, displayable, flags);
            const ADMISSION_UMD_PRIMARY_MAPPING before =
                AdmissionUmdPrimaryMap(desc, shared, misc, displayable, 0u);
            /* Pre-BP-1 behaviour for every resource without the request. */
            assert(before.AllocationPrimary == (desc ? 1u : 0u));
            assert(before.VidPnSourceId == 0u);
            assert(before.WrittenPrimary ==
                   (displayable && (desc || misc) ? 1u : 0u));
            assert(before.DeviceAssociated == (desc && !shared ? 1u : 0u));
            assert(before.RetirementPrimary == (desc ? 1u : 0u));
            /* The D3D12 HEAP_FLAG_PRIMARY request: a kernel primary on the
             * single source ... */
            if (request) {
              assert(m.AllocationPrimary == 1u);
              assert(m.VidPnSourceId == 0u);
              /* ... a written primary only when scan-out storage ... */
              assert(m.WrittenPrimary == displayable);
            } else {
              assert(m.AllocationPrimary == before.AllocationPrimary &&
                     m.WrittenPrimary == before.WrittenPrimary);
            }
            /* ... and never a mode-setting primary: allocation ownership and
             * the release path are those without the request. */
            assert(m.DeviceAssociated == before.DeviceAssociated);
            assert(m.RetirementPrimary == before.RetirementPrimary);
            /* WRITTEN_PRIMARY never without scan-out storage (EXP1038). */
            assert(!m.WrittenPrimary || displayable);
          }
  /* GravityMark (EXP2361): RGBA8 back buffer, heap Flags 0x30, no
   * pPrimaryDesc, not shared, no DISPLAYABLE misc flag. */
  {
    const ADMISSION_UMD_PRIMARY_MAPPING gm = AdmissionUmdPrimaryMap(
        0, 0, 0, AdmissionUmdStorageDisplayable(A8B8G8R8),
        ADMISSION_UMD_CREATE_RUNTIME_PRIMARY);
    ADMISSION_UMD_PRIMARY_RETENTION state = {0u, 0, 0u};
    int object = 0;
    assert(gm.AllocationPrimary == 1u && gm.VidPnSourceId == 0u &&
           gm.WrittenPrimary == 0u && gm.DeviceAssociated == 0u &&
           gm.RetirementPrimary == 0u);
    /* PL-1 must not capture it, even if its allocation were the one named
     * by the last SetDisplayMode. */
    assert(AdmissionUmdRetentionSetDisplayMode(&state, 0x40u, 1) == 0);
    assert(AdmissionUmdRetentionDestroy(&state, &object, 0x40u,
                                        (int)gm.DeviceAssociated) == 0);
    assert(state.Retained == 0);
  }
  /* The ddisplay mode-setting primary (pPrimaryDesc, not shared) is still
   * the one PL-1 retains. */
  {
    const ADMISSION_UMD_PRIMARY_MAPPING mode = AdmissionUmdPrimaryMap(
        1, 0, 0, AdmissionUmdStorageDisplayable(A8R8G8B8), 0u);
    ADMISSION_UMD_PRIMARY_RETENTION state = {0u, 0, 0u};
    int object = 0;
    assert(mode.DeviceAssociated == 1u && mode.RetirementPrimary == 1u);
    assert(AdmissionUmdRetentionSetDisplayMode(&state, 0x80u, 1) == 0);
    assert(AdmissionUmdRetentionDestroy(&state, &object, 0x80u,
                                        (int)mode.DeviceAssociated) == 1);
  }
  printf("primary_backbuffer_test: OK\n");
  return 0;
}
