#include "render_completed_output.h"
#include "render_qualification.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static ADMISSION_ALLOCATION_OBJECT make_owner(unsigned int color) {
  ADMISSION_ALLOCATION_DESCRIPTION d;
  ADMISSION_ALLOCATION_OBJECT owner;
  (void)color;
  memset(&d, 0, sizeof(d));
  assert(AdmissionAllocationDescribe(2560u, 1600u, 4u, 1u, 21u, 0u, &d));
  assert(AdmissionAllocationCreate(&d, &owner));
  return owner;
}

static ADMISSION_BACKEND_OUTPUT_VIEW make_full(
    unsigned char *bytes, unsigned long long gpu,
    unsigned long long physical, unsigned int color) {
  ADMISSION_BACKEND_OUTPUT_VIEW view;
  memset(&view, 0, sizeof(view));
  view.AllocationCpuAddress = bytes;
  view.AllocationGpuAddress = gpu;
  view.AllocationPhysicalAddress = physical;
  view.AllocationBytes = APPLE_AGX_EXP208_FRAMEBUFFER_BYTES;
  view.RenderedCpuAddress = bytes;
  view.RenderedGpuAddress = gpu;
  view.RenderedPhysicalAddress = physical;
  view.RenderedBytes = APPLE_AGX_EXP208_FRAMEBUFFER_BYTES;
  view.AllocationWidth = view.RenderWidth = 2560u;
  view.AllocationHeight = view.RenderHeight = 1600u;
  view.AllocationPitch = view.RenderPitch = 10240u;
  view.AllocationFormat = 21u;
  view.ExpectedColor = color;
  view.Framebuffer = APPLE_AGX_TRUE;
  return view;
}

int main(void) {
  unsigned char *pool = (unsigned char *)malloc(0x03800000u);
  unsigned char *first = pool == NULL ? NULL : pool + 0x00fa0000u;
  unsigned char *second = pool == NULL ? NULL : pool + 0x01f40000u;
  ADMISSION_ALLOCATION_OBJECT owner1 = make_owner(0xff112233u);
  ADMISSION_ALLOCATION_OBJECT owner2 = make_owner(0xffcc8844u);
  ADMISSION_ALLOCATION_OBJECT fallbackOwner = make_owner(0u);
  ADMISSION_BACKEND_OUTPUT_VIEW fallbackView =
      make_full(pool, 0x1500000000ULL, 0x9bc060000ULL, 0u);
  ADMISSION_BACKEND_OUTPUT_VIEW view1 =
      make_full(first, 0x1500fa0000ULL, 0x9bd000000ULL, 0xff112233u);
  ADMISSION_BACKEND_OUTPUT_VIEW view2 =
      make_full(second, 0x1501f40000ULL, 0x9bdfa0000ULL, 0xffcc8844u);
  ADMISSION_COMPLETED_OUTPUT completed;
  ADMISSION_DISPLAY_OUTPUT_LEASE display;
  ADMISSION_DISPLAY_OUTPUT_LEASE fallback;
  ADMISSION_PRESENT_QUERY history;
  ADMISSION_PRESENT_VERIFICATION verified;

  assert(first != NULL && second != NULL);
  assert(AdmissionCompletedOutputPlatformRangeValid(
      &view1, pool, 0x1500000000ULL, 0x9bc060000ULL, 0x03800000u));
  assert(!AdmissionCompletedOutputPlatformRangeValid(
      &view1, pool, 0x1500000000ULL, 0x9bc050000ULL, 0x03800000u));
  assert(AdmissionCompletedOutputReceiptMatchesView(
      &view1, view1.AllocationGpuAddress, view1.AllocationPhysicalAddress,
      view1.AllocationBytes, view1.RenderedBytes));
  {
    ADMISSION_BACKEND_OUTPUT_VIEW band = view1;
    band.RenderedOffset = APPLE_AGX_EXP208_FRAMEBUFFER_BAND_OFFSET;
    band.RenderedCpuAddress = first + band.RenderedOffset;
    band.RenderedGpuAddress = band.AllocationGpuAddress + band.RenderedOffset;
    band.RenderedPhysicalAddress =
        band.AllocationPhysicalAddress + band.RenderedOffset;
    band.RenderedBytes = APPLE_AGX_EXP208_FRAMEBUFFER_BAND_BYTES;
    band.RenderHeight = APPLE_AGX_EXP208_FRAMEBUFFER_BAND_HEIGHT;
    assert(AdmissionCompletedOutputReceiptMatchesView(
        &band, band.AllocationGpuAddress, band.AllocationPhysicalAddress,
        band.AllocationBytes, band.RenderedBytes));
    assert(!AdmissionCompletedOutputReceiptMatchesView(
        &band, band.AllocationGpuAddress, band.RenderedPhysicalAddress,
        band.AllocationBytes, band.RenderedBytes));
    assert(!AdmissionCompletedOutputReceiptMatchesView(
        &band, band.AllocationGpuAddress, band.AllocationPhysicalAddress,
        band.RenderedBytes, band.RenderedBytes));
    assert(!AdmissionCompletedOutputReceiptMatchesView(
        &band, band.AllocationGpuAddress, band.AllocationPhysicalAddress,
        band.AllocationBytes, band.AllocationBytes));
    ++band.RenderedPhysicalAddress;
    assert(!AdmissionCompletedOutputReceiptMatchesView(
        &band, band.AllocationGpuAddress, band.AllocationPhysicalAddress,
        band.AllocationBytes, band.RenderedBytes));
  }
  {
    ADMISSION_BACKEND_OUTPUT_VIEW outside = view1;
    outside.AllocationBytes = 0x03000000u;
    assert(!AdmissionCompletedOutputPlatformRangeValid(
        &outside, pool, 0x1500000000ULL, 0x9bc060000ULL, 0x03800000u));
  }
  AdmissionCompletedOutputInitialize(&completed);
  AdmissionDisplayOutputLeaseInitialize(&display);
  AdmissionDisplayOutputLeaseInitialize(&fallback);
  assert(AdmissionCompletedOutputCapture(&completed, 1u, 256u,
                                         &view1, &owner1));
  assert(owner1.OpenCount == 1u && completed.CaptureCount == 1u);
  assert(AdmissionCompletedOutputCapture(&completed, 1u, 256u,
                                         &view1, &owner1));
  assert(owner1.OpenCount == 1u); /* retry never acquires twice */
  assert(!AdmissionCompletedOutputCapture(&completed, 2u, 257u,
                                          &view2, &owner2));
  assert(AdmissionCompletedOutputContains(
      &completed, 256u, first + 64u, 128u));
  assert(!AdmissionCompletedOutputContains(
      &completed, 257u, first, 4u));
  assert(!AdmissionCompletedOutputContains(
      &completed, 256u, second, 4u));
  assert(AdmissionCompletedOutputMarkReleased(&completed, 256u));
  assert(AdmissionCompletedOutputMarkReleased(&completed, 256u));
  assert(completed.ReleaseCount == 1u);
  assert(AdmissionCompletedOutputMarkPacketRetired(&completed, 256u));
  assert(AdmissionCompletedOutputMarkNotified(&completed, 256u));
  assert(AdmissionCompletedOutputRecordAccess(&completed, 256u, 0u));
  assert(AdmissionCompletedOutputBeginPresent(&completed, 256u));
  assert(AdmissionCompletedOutputMarkPublished(&completed, 256u, 9u));
  assert(AdmissionCompletedOutputMarkLatched(&completed, 256u, 9u));
  assert(AdmissionCompletedOutputRecordPresentation(&completed, 256u, 0u));
  memset(&verified, 0, sizeof(verified));
  verified.CandidateBuild = 631u;
  verified.BootGeneration = 7u;
  verified.Index = 0u;
  verified.Purpose = AdmissionPresentPurposeRenderFrame;
  verified.Fence = 256u;
  verified.AllocationToken = 0x12340000ULL;
  verified.ExpectedColor = view1.ExpectedColor;
  verified.PixelsExpected = 4096000u;
  verified.PixelsVerified = 4096000u;
  verified.Format = view1.AllocationFormat;
  verified.Width = view1.AllocationWidth;
  verified.Height = view1.AllocationHeight;
  verified.Pitch = view1.AllocationPitch;
  verified.Sequence = 9u;
  verified.ActiveOffset = 0x00fa0000ULL;
  verified.PhysicalAddress = view1.AllocationPhysicalAddress;
  verified.ContentHash = 0x1122334455667788ULL;
  assert(AdmissionPresentQueryBuild(&history, &verified));
  assert(AdmissionCompletedOutputTransferToDisplay(
      &completed, &display, 256u));
  assert(history.ExpectedColor == 0xff112233u);
  assert(history.PixelsExpected == 4096000u);
  assert(history.PixelsVerified == 4096000u);
  assert(history.Format == 21u && history.Fence == 256u);
  assert(display.Active && owner1.OpenCount == 1u);
  assert(!AdmissionDisplayOutputLeaseAllowsRender(&display, &owner1));
  assert(AdmissionDisplayOutputLeaseAllowsRender(&display, &owner2));

  assert(AdmissionCompletedOutputCapture(&completed, 2u, 257u,
                                         &view2, &owner2));
  assert(AdmissionCompletedOutputMarkReleased(&completed, 257u));
  assert(AdmissionCompletedOutputMarkPacketRetired(&completed, 257u));
  assert(AdmissionCompletedOutputMarkNotified(&completed, 257u));
  assert(AdmissionCompletedOutputRecordAccess(&completed, 257u, 0u));
  assert(AdmissionCompletedOutputBeginPresent(&completed, 257u));
  assert(AdmissionCompletedOutputMarkPublished(&completed, 257u, 10u));
  assert(AdmissionCompletedOutputMarkLatched(&completed, 257u, 10u));
  assert(AdmissionCompletedOutputRecordPresentation(&completed, 257u, 0u));
  assert(AdmissionCompletedOutputTransferToDisplay(
      &completed, &display, 257u));
  assert(owner1.OpenCount == 0u && owner2.OpenCount == 1u);
  assert(AdmissionDisplayOutputLeaseMatches(&display, &owner2));
  assert(AdmissionDisplayOutputLeaseRetire(&display));
  assert(owner2.OpenCount == 0u);

  /* The original Windows primary is an owned fallback, not a bare offset.
   * Moving it active after an exact replacement latch retires only the old
   * render surface and preserves one reference to the displayed primary. */
  assert(AdmissionDisplayOutputLeaseCapture(
      &fallback, 7u, 0u, &fallbackView, &fallbackOwner));
  assert(fallbackOwner.OpenCount == 1u);
  assert(!AdmissionDisplayOutputLeaseCapture(
      &fallback, 8u, 0u, &view1, &owner1));
  assert(owner1.OpenCount == 0u);
  assert(AdmissionCompletedOutputCapture(&completed, 8u, 262u,
                                         &view2, &owner2));
  assert(AdmissionCompletedOutputMarkReleased(&completed, 262u));
  assert(AdmissionCompletedOutputMarkPacketRetired(&completed, 262u));
  assert(AdmissionCompletedOutputMarkNotified(&completed, 262u));
  assert(AdmissionCompletedOutputRecordAccess(&completed, 262u, 0u));
  assert(AdmissionCompletedOutputBeginPresent(&completed, 262u));
  assert(AdmissionCompletedOutputMarkPublished(&completed, 262u, 13u));
  assert(AdmissionCompletedOutputMarkLatched(&completed, 262u, 13u));
  assert(AdmissionCompletedOutputRecordPresentation(&completed, 262u, 0u));
  assert(AdmissionCompletedOutputTransferToDisplay(
      &completed, &display, 262u));
  assert(owner2.OpenCount == 1u && fallbackOwner.OpenCount == 1u);
  assert(AdmissionDisplayOutputLeaseMove(&display, &fallback));
  assert(owner2.OpenCount == 0u);
  assert(!fallback.Active && fallback.Owner == NULL);
  assert(display.Active && display.Owner == &fallbackOwner);
  assert(fallbackOwner.OpenCount == 1u);
  assert(AdmissionDisplayOutputLeaseRetire(&display));
  assert(fallbackOwner.OpenCount == 0u);

  /* A band lease permits only the exact rendered subrange while preserving
   * allocation-base identity for later presentation. */
  view1.RenderedOffset = APPLE_AGX_EXP208_FRAMEBUFFER_BAND_OFFSET;
  view1.RenderedCpuAddress = first + 0x100u;
  view1.RenderedGpuAddress += APPLE_AGX_EXP208_FRAMEBUFFER_BAND_OFFSET;
  view1.RenderedPhysicalAddress += APPLE_AGX_EXP208_FRAMEBUFFER_BAND_OFFSET;
  view1.RenderedBytes = APPLE_AGX_EXP208_FRAMEBUFFER_BAND_BYTES;
  view1.RenderHeight = APPLE_AGX_EXP208_FRAMEBUFFER_BAND_HEIGHT;
  assert(AdmissionCompletedOutputCapture(&completed, 3u, 258u,
                                         &view1, &owner1));
  assert(AdmissionCompletedOutputContains(
      &completed, 258u, first + 0x100u, 4u));
  assert(!AdmissionCompletedOutputContains(
      &completed, 258u, first, 4u));
  assert(AdmissionCompletedOutputMarkReleased(&completed, 258u));
  assert(AdmissionCompletedOutputMarkPacketRetired(&completed, 258u));
  assert(AdmissionCompletedOutputMarkNotified(&completed, 258u));
  assert(AdmissionCompletedOutputRecordAccess(
      &completed, 258u, 0xc0000141u));
  assert(completed.AccessAttempted == 1u &&
         completed.AccessStatus == 0xc0000141u);
  assert(!AdmissionCompletedOutputBeginPresent(&completed, 258u));
  assert(AdmissionCompletedOutputAbort(&completed, 258u));
  assert(owner1.OpenCount == 0u);

  view1.RenderedOffset = 0u;
  view1.RenderedCpuAddress = first;
  view1.RenderedGpuAddress = view1.AllocationGpuAddress;
  view1.RenderedPhysicalAddress = view1.AllocationPhysicalAddress;
  view1.RenderedBytes = APPLE_AGX_EXP208_FRAMEBUFFER_BYTES;
  view1.RenderHeight = APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT;
  assert(AdmissionCompletedOutputCapture(&completed, 4u, 259u,
                                         &view1, &owner1));
  assert(AdmissionCompletedOutputMarkReleased(&completed, 259u));
  assert(AdmissionCompletedOutputMarkPacketRetired(&completed, 259u));
  assert(AdmissionCompletedOutputMarkNotified(&completed, 259u));
  assert(AdmissionCompletedOutputRecordAccess(&completed, 259u, 0u));
  assert(AdmissionCompletedOutputBeginPresent(&completed, 259u));
  assert(AdmissionCompletedOutputRecordPresentation(
      &completed, 259u, 0xc000003eu));
  assert(!AdmissionCompletedOutputTransferToDisplay(
      &completed, &display, 259u));
  assert(AdmissionCompletedOutputAbort(&completed, 259u));
  assert(owner1.OpenCount == 0u);

  /* Once DCP accepted publication, a diagnostic timeout cannot release the
   * owner. Only an exact later resolution may retire it. */
  assert(AdmissionCompletedOutputCapture(&completed, 5u, 260u,
                                         &view1, &owner1));
  assert(AdmissionCompletedOutputMarkReleased(&completed, 260u));
  assert(AdmissionCompletedOutputMarkPacketRetired(&completed, 260u));
  assert(AdmissionCompletedOutputMarkNotified(&completed, 260u));
  assert(AdmissionCompletedOutputRecordAccess(&completed, 260u, 0u));
  assert(AdmissionCompletedOutputBeginPresent(&completed, 260u));
  assert(AdmissionCompletedOutputMarkPublished(&completed, 260u, 11u));
  assert(AdmissionCompletedOutputMarkOwnershipUnknown(
      &completed, 260u, 11u, 0x00000102u));
  assert(!AdmissionCompletedOutputAbort(&completed, 260u));
  assert(owner1.OpenCount == 1u);
  assert(AdmissionCompletedOutputResolveUnknown(&completed, 260u, 11u));
  assert(owner1.OpenCount == 0u);

  assert(AdmissionCompletedOutputCapture(&completed, 6u, 261u,
                                         &view1, &owner1));
  assert(AdmissionCompletedOutputMarkReleased(&completed, 261u));
  assert(AdmissionCompletedOutputMarkPacketRetired(&completed, 261u));
  assert(AdmissionCompletedOutputMarkNotified(&completed, 261u));
  assert(AdmissionCompletedOutputRecordAccess(&completed, 261u, 0u));
  assert(AdmissionCompletedOutputBeginPresent(&completed, 261u));
  assert(AdmissionCompletedOutputMarkPublished(&completed, 261u, 12u));
  assert(AdmissionCompletedOutputMarkLatched(&completed, 261u, 12u));
  assert(AdmissionCompletedOutputRecordPresentation(&completed, 261u, 0u));
  assert(AdmissionCompletedOutputTransferToDisplay(
      &completed, &display, 261u));
  assert(display.Active && display.Fence == 261u && owner1.OpenCount == 1u);
  assert(AdmissionDisplayOutputLeaseRetire(&display));
  assert(owner1.OpenCount == 0u);
  free(pool);
  return 0;
}
