#include "render_visible_scanout.h"
#include "apple_agx_exp208_framebuffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static unsigned int pixel(const unsigned int *surface, unsigned int x,
                          unsigned int y) {
  return surface[y * APPLE_AGX_SCANOUT_J313_WIDTH + x];
}

int main(void) {
  unsigned int *surface = malloc(APPLE_AGX_SCANOUT_J313_SURFACE_SIZE);
  ADMISSION_VISIBLE_PATTERN_RECEIPT first;
  ADMISSION_VISIBLE_PATTERN_RECEIPT second;
  ADMISSION_VISIBLE_SCANOUT_RECEIPT scanout;
  ADMISSION_VISIBLE_AGX_RECEIPT agx;
  unsigned int source[256];
  unsigned int before = 0xa5a5a5a5u;
  unsigned int companion = 99u;
  assert(AdmissionVisibleAgxCompanionIndex(0u, 2u, &companion));
  assert(companion == 1u);
  assert(AdmissionVisibleAgxCompanionIndex(1u, 2u, &companion));
  assert(companion == 0u);
  assert(!AdmissionVisibleAgxCompanionIndex(2u, 2u, &companion));
  assert(!AdmissionVisibleAgxCompanionIndex(0u, 1u, &companion));
  assert(!AdmissionVisibleAgxCompanionIndex(0u, 2u, NULL));
  assert(surface != NULL);
  memset(surface, 0xa5, APPLE_AGX_SCANOUT_J313_SURFACE_SIZE);
  assert(!AdmissionVisiblePatternFill(surface,
      APPLE_AGX_SCANOUT_J313_SURFACE_SIZE - 4u, 590u, &first));
  assert(surface[0] == before);
  assert(AdmissionVisiblePatternFill(surface,
      APPLE_AGX_SCANOUT_J313_SURFACE_SIZE, 590u, &first));
  assert(first.Version == ADMISSION_VISIBLE_PATTERN_VERSION);
  assert(first.Frame == 590u && first.Width == 2560u && first.Height == 1600u);
  assert(first.Pitch == 10240u && first.Format == APPLE_AGX_SCANOUT_FORMAT_BGRA8888);
  assert(first.SurfaceBytes == APPLE_AGX_SCANOUT_J313_SURFACE_SIZE);
  assert(first.ContentHash != 0ULL);
  assert(pixel(surface, 0u, 0u) == 0xffffffffu);
  assert(pixel(surface, 1000u, 700u) == 0xffff2020u);
  assert(pixel(surface, 1800u, 700u) == 0xff20ff20u);
  assert(pixel(surface, 1000u, 1200u) == 0xff2020ffu);
  assert(pixel(surface, 1800u, 1200u) == 0xffffff20u);
  assert(AdmissionVisiblePatternFill(surface,
      APPLE_AGX_SCANOUT_J313_SURFACE_SIZE, 591u, &second));
  assert(second.ContentHash != first.ContentHash);
  memset(&scanout, 0, sizeof(scanout));
  scanout.Version = ADMISSION_VISIBLE_SCANOUT_RECEIPT_VERSION;
  scanout.Bytes = sizeof(scanout);
  scanout.Stage = 4u;
  scanout.Pattern = second;
  scanout.CpuAddress = 0xffff800012340000ULL;
  scanout.GuestIpaAddress = 0x850000000ULL;
  scanout.HostPhysicalAddress = 0x950000000ULL;
  scanout.SurfaceOffset = 0ULL;
  scanout.RequestedSequence = 9ULL;
  scanout.AppliedSequence = 9ULL;
  scanout.LatchedSequence = 9ULL;
  scanout.ActiveOffset = 0ULL;
  scanout.PoolPhysicalAddress = 0x950000000ULL;
  scanout.SwapId = 12u;
  scanout.SourceVisible = 1u;
  assert(AdmissionVisibleScanoutReceiptValid(&scanout));
  scanout.LatchedSequence = 8ULL;
  assert(!AdmissionVisibleScanoutReceiptValid(&scanout));
  scanout.LatchedSequence = 9ULL;
  scanout.PoolPhysicalAddress += APPLE_AGX_SCANOUT_ALIGNMENT;
  assert(!AdmissionVisibleScanoutReceiptValid(&scanout));
  memset(&agx, 0, sizeof(agx));
  for (before = 0u; before < 256u; ++before)
    source[before] = 0xff112233u;
  assert(AdmissionVisibleAgxScale16x16(
      source, sizeof(source), surface, APPLE_AGX_SCANOUT_J313_SURFACE_SIZE,
      &agx));
  assert(agx.SourceWidth == 16u && agx.SourceHeight == 16u &&
         agx.SourcePitch == 64u && agx.SourceBytes == 1024u);
  assert(agx.SourceHash != 0ULL && agx.DestinationHash != 0ULL);
  assert(pixel(surface, 0u, 0u) == 0xff112233u);
  assert(pixel(surface, 2559u, 1599u) == 0xff112233u);
  agx.Version = ADMISSION_VISIBLE_AGX_RECEIPT_VERSION;
  agx.Bytes = sizeof(agx);
  agx.Stage = 3u;
  agx.Guard = AdmissionVisibleAgxGuardComplete;
  agx.CapturedValid = 1u;
  agx.CapturedFence = 17u;
  agx.Status = 0u;
  agx.Fence = 17u;
  agx.SourceGpuAddress = 0x1500fa0000ULL;
  agx.SourcePhysicalAddress = 0x9c0fa0000ULL;
  agx.DestinationCpuAddress = 0xffff800012340000ULL;
  agx.DestinationGuestIpa = 0x8c1f40000ULL;
  agx.DestinationPhysicalAddress = 0x9c1f40000ULL;
  agx.DestinationOffset = 0x1f40000ULL;
  agx.DestinationAllocationToken = 0xffff800045670000ULL;
  agx.ActiveOffsetBefore = 0u;
  agx.RequestedSequence = 3u;
  agx.AppliedSequence = 3u;
  agx.LatchedSequence = 3u;
  agx.ActiveOffsetAfter = agx.DestinationOffset;
  agx.SwapId = 12u;
  assert(AdmissionVisibleAgxReceiptValid(&agx));
  agx.Guard = AdmissionVisibleAgxGuardDestinationIdentity;
  assert(!AdmissionVisibleAgxReceiptValid(&agx));
  agx.Guard = AdmissionVisibleAgxGuardComplete;
  agx.DestinationAllocationToken = 0u;
  assert(!AdmissionVisibleAgxReceiptValid(&agx));

  memset(surface, 0, APPLE_AGX_SCANOUT_J313_SURFACE_SIZE);
  for (before = 0u;
       before < APPLE_AGX_SCANOUT_J313_SURFACE_SIZE / 4u; ++before)
    surface[before] = 0xff112233u;
  memset(&agx, 0, sizeof(agx));
  assert(AdmissionVisibleAgxUseFramebuffer(
      surface, APPLE_AGX_SCANOUT_J313_SURFACE_SIZE, &agx));
  assert(agx.SourceWidth == APPLE_AGX_SCANOUT_J313_WIDTH);
  assert(agx.SourceHeight == APPLE_AGX_SCANOUT_J313_HEIGHT);
  assert(agx.SourcePitch == APPLE_AGX_SCANOUT_J313_STRIDE);
  assert(agx.SourceBytes == APPLE_AGX_SCANOUT_J313_SURFACE_SIZE);
  assert(agx.DestinationBytes == APPLE_AGX_SCANOUT_J313_SURFACE_SIZE);
  assert(agx.SourceHash != 0ULL &&
         agx.DestinationHash == agx.SourceHash);
  for (before = APPLE_AGX_EXP208_FRAMEBUFFER_BAND_TOP *
                    APPLE_AGX_SCANOUT_J313_WIDTH;
       before < APPLE_AGX_SCANOUT_J313_SURFACE_SIZE / 4u; ++before)
    surface[before] = APPLE_AGX_EXP208_FRAMEBUFFER_BAND_COLOR;
  memset(&agx, 0, sizeof(agx));
  assert(AdmissionVisibleAgxUseFramebuffer(
      surface, APPLE_AGX_SCANOUT_J313_SURFACE_SIZE, &agx));
  assert(pixel(surface, 10u, 10u) ==
         APPLE_AGX_EXP208_FRAMEBUFFER_BASE_COLOR);
  assert(pixel(surface, 10u, 1200u) ==
         APPLE_AGX_EXP208_FRAMEBUFFER_BAND_COLOR);
  assert(agx.SourceHash != 0ULL &&
         agx.DestinationHash == agx.SourceHash);
  for (before = 0u;
       before < APPLE_AGX_SCANOUT_J313_SURFACE_SIZE / 4u; ++before)
    surface[before] = APPLE_AGX_EXP208_FRAMEBUFFER_BAND_COLOR;
  memset(&agx, 0, sizeof(agx));
  assert(AdmissionVisibleAgxUseFramebuffer(
      surface, APPLE_AGX_SCANOUT_J313_SURFACE_SIZE, &agx));
  assert(pixel(surface, 10u, 10u) ==
         APPLE_AGX_EXP208_FRAMEBUFFER_BAND_COLOR);
  assert(pixel(surface, 10u, 1200u) ==
         APPLE_AGX_EXP208_FRAMEBUFFER_BAND_COLOR);
  assert(agx.SourceHash != 0ULL &&
         agx.DestinationHash == agx.SourceHash);
  agx.Version = ADMISSION_VISIBLE_AGX_RECEIPT_VERSION;
  agx.Bytes = sizeof(agx);
  agx.Stage = 3u;
  agx.Guard = AdmissionVisibleAgxGuardComplete;
  agx.CapturedValid = 1u;
  agx.CapturedFence = 18u;
  agx.Status = 0u;
  agx.Fence = 18u;
  agx.SourceGpuAddress = 0x1500fa0000ULL;
  agx.SourcePhysicalAddress = 0x9c0fa0000ULL;
  agx.DestinationCpuAddress = 0xffff800012340000ULL;
  agx.DestinationGuestIpa = 0x8c0fa0000ULL;
  agx.DestinationPhysicalAddress = agx.SourcePhysicalAddress;
  agx.DestinationOffset = 0xfa0000ULL;
  agx.DestinationAllocationToken = 0xffff800045670000ULL;
  agx.ActiveOffsetBefore = 0u;
  agx.RequestedSequence = 4u;
  agx.AppliedSequence = 4u;
  agx.LatchedSequence = 4u;
  agx.ActiveOffsetAfter = agx.DestinationOffset;
  agx.SwapId = 13u;
  assert(AdmissionVisibleAgxReceiptValid(&agx));
  free(surface);
  return 0;
}
