#include "render_visible_scanout.h"

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
  free(surface);
  return 0;
}
