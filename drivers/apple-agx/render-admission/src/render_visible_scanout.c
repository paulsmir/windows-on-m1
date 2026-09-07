#include "render_visible_scanout.h"

#define VISIBLE_NULL ((void *)0)
#define VISIBLE_BORDER 32u
#define VISIBLE_MARKER_X 96u
#define VISIBLE_MARKER_Y 96u
#define VISIBLE_MARKER_CELL 32u

static unsigned long long visible_hash(
    const unsigned char *Bytes, unsigned long long Count) {
  unsigned long long value = 0xcbf29ce484222325ULL;
  unsigned long long index;
  for (index = 0ULL; index < Count; ++index) {
    value ^= Bytes[index];
    value *= 0x100000001b3ULL;
  }
  return value;
}

static unsigned int visible_pixel(
    unsigned int X, unsigned int Y, unsigned int Frame) {
  unsigned int marker_x;
  unsigned int marker_y;
  unsigned int marker_bit;
  if (X < VISIBLE_BORDER || Y < VISIBLE_BORDER ||
      X >= APPLE_AGX_SCANOUT_J313_WIDTH - VISIBLE_BORDER ||
      Y >= APPLE_AGX_SCANOUT_J313_HEIGHT - VISIBLE_BORDER)
    return 0xffffffffu;
  if (X >= VISIBLE_MARKER_X && X < VISIBLE_MARKER_X + 8u * VISIBLE_MARKER_CELL &&
      Y >= VISIBLE_MARKER_Y && Y < VISIBLE_MARKER_Y + 4u * VISIBLE_MARKER_CELL) {
    marker_x = (X - VISIBLE_MARKER_X) / VISIBLE_MARKER_CELL;
    marker_y = (Y - VISIBLE_MARKER_Y) / VISIBLE_MARKER_CELL;
    marker_bit = marker_y * 8u + marker_x;
    return ((Frame >> marker_bit) & 1u) != 0u ? 0xffffffffu : 0xff000000u;
  }
  if (Y < APPLE_AGX_SCANOUT_J313_HEIGHT / 2u)
    return X < APPLE_AGX_SCANOUT_J313_WIDTH / 2u ? 0xffff2020u : 0xff20ff20u;
  return X < APPLE_AGX_SCANOUT_J313_WIDTH / 2u ? 0xff2020ffu : 0xffffff20u;
}

int AdmissionVisiblePatternFill(
    void *Surface, unsigned long long SurfaceBytes, unsigned int Frame,
    ADMISSION_VISIBLE_PATTERN_RECEIPT *Receipt) {
  unsigned int *pixels = (unsigned int *)Surface;
  unsigned char *receipt_bytes = (unsigned char *)Receipt;
  unsigned char *surface_bytes = (unsigned char *)Surface;
  unsigned int x;
  unsigned int y;
  unsigned int index;
  if (Surface == VISIBLE_NULL || Receipt == VISIBLE_NULL || Frame == 0u ||
      SurfaceBytes != APPLE_AGX_SCANOUT_J313_SURFACE_SIZE)
    return 0;
  for (index = 0u; index < (unsigned int)sizeof(*Receipt); ++index)
    receipt_bytes[index] = 0u;
  for (y = 0u; y < APPLE_AGX_SCANOUT_J313_HEIGHT; ++y)
    for (x = 0u; x < APPLE_AGX_SCANOUT_J313_WIDTH; ++x)
      pixels[y * APPLE_AGX_SCANOUT_J313_WIDTH + x] =
          visible_pixel(x, y, Frame);
  Receipt->Version = ADMISSION_VISIBLE_PATTERN_VERSION;
  Receipt->Bytes = (unsigned int)sizeof(*Receipt);
  Receipt->Frame = Frame;
  Receipt->Width = APPLE_AGX_SCANOUT_J313_WIDTH;
  Receipt->Height = APPLE_AGX_SCANOUT_J313_HEIGHT;
  Receipt->Pitch = APPLE_AGX_SCANOUT_J313_STRIDE;
  Receipt->Format = APPLE_AGX_SCANOUT_FORMAT_BGRA8888;
  Receipt->SurfaceBytes = SurfaceBytes;
  Receipt->ContentHash = visible_hash(surface_bytes, SurfaceBytes);
  for (index = 0u; index < ADMISSION_VISIBLE_PATTERN_PREFIX_BYTES; ++index)
    Receipt->Prefix[index] = surface_bytes[index];
  return 1;
}

int AdmissionVisibleScanoutReceiptValid(
    const ADMISSION_VISIBLE_SCANOUT_RECEIPT *Receipt) {
  if (Receipt == VISIBLE_NULL ||
      Receipt->Version != ADMISSION_VISIBLE_SCANOUT_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) || Receipt->Stage != 4u ||
      Receipt->Status != 0u ||
      Receipt->Pattern.Version != ADMISSION_VISIBLE_PATTERN_VERSION ||
      Receipt->Pattern.Bytes != sizeof(Receipt->Pattern) ||
      Receipt->Pattern.Frame == 0u ||
      Receipt->Pattern.Width != APPLE_AGX_SCANOUT_J313_WIDTH ||
      Receipt->Pattern.Height != APPLE_AGX_SCANOUT_J313_HEIGHT ||
      Receipt->Pattern.Pitch != APPLE_AGX_SCANOUT_J313_STRIDE ||
      Receipt->Pattern.Format != APPLE_AGX_SCANOUT_FORMAT_BGRA8888 ||
      Receipt->Pattern.SurfaceBytes != APPLE_AGX_SCANOUT_J313_SURFACE_SIZE ||
      Receipt->Pattern.ContentHash == 0ULL || Receipt->CpuAddress == 0ULL ||
      Receipt->GuestIpaAddress == 0ULL ||
      Receipt->HostPhysicalAddress == 0ULL ||
      Receipt->PoolPhysicalAddress != Receipt->HostPhysicalAddress ||
      Receipt->PoolPhysicalAddress >= (1ULL << 40u) ||
      (Receipt->SurfaceOffset & (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL ||
      Receipt->SurfaceOffset > APPLE_AGX_SCANOUT_J313_POOL_SIZE -
                                   APPLE_AGX_SCANOUT_J313_SURFACE_SIZE ||
      Receipt->RequestedSequence == 0ULL ||
      Receipt->AppliedSequence != Receipt->RequestedSequence ||
      Receipt->LatchedSequence != Receipt->RequestedSequence ||
      Receipt->ActiveOffset != Receipt->SurfaceOffset ||
      Receipt->SwapId == 0u || Receipt->SourceVisible != 1u)
    return 0;
  return 1;
}

int AdmissionVisibleAgxScale16x16(
    const void *Source, unsigned long long SourceBytes,
    void *Destination, unsigned long long DestinationBytes,
    ADMISSION_VISIBLE_AGX_RECEIPT *Receipt) {
  const unsigned int *source = (const unsigned int *)Source;
  unsigned int *destination = (unsigned int *)Destination;
  unsigned int x;
  unsigned int y;
  unsigned int index;
  if (Source == VISIBLE_NULL || Destination == VISIBLE_NULL ||
      Receipt == VISIBLE_NULL || SourceBytes < 16ULL * 16ULL * 4ULL ||
      DestinationBytes != APPLE_AGX_SCANOUT_J313_SURFACE_SIZE)
    return 0;
  Receipt->SourceWidth = 16u;
  Receipt->SourceHeight = 16u;
  Receipt->SourcePitch = 64u;
  Receipt->SourceBytes = 16ULL * 16ULL * 4ULL;
  Receipt->DestinationBytes = DestinationBytes;
  Receipt->SourceHash = visible_hash(
      (const unsigned char *)Source, Receipt->SourceBytes);
  for (index = 0u; index < 64u; ++index)
    Receipt->SourcePrefix[index] = ((const unsigned char *)Source)[index];
  for (y = 0u; y < APPLE_AGX_SCANOUT_J313_HEIGHT; ++y)
    for (x = 0u; x < APPLE_AGX_SCANOUT_J313_WIDTH; ++x)
      destination[y * APPLE_AGX_SCANOUT_J313_WIDTH + x] =
          source[(y * 16u / APPLE_AGX_SCANOUT_J313_HEIGHT) * 16u +
                 (x * 16u / APPLE_AGX_SCANOUT_J313_WIDTH)];
  Receipt->DestinationHash = visible_hash(
      (const unsigned char *)Destination, DestinationBytes);
  return Receipt->SourceHash != 0ULL && Receipt->DestinationHash != 0ULL;
}

int AdmissionVisibleAgxReceiptValid(
    const ADMISSION_VISIBLE_AGX_RECEIPT *Receipt) {
  return Receipt != VISIBLE_NULL &&
                 Receipt->Version == ADMISSION_VISIBLE_AGX_RECEIPT_VERSION &&
                 Receipt->Bytes == sizeof(*Receipt) && Receipt->Stage == 3u &&
                 Receipt->Status == 0u && Receipt->Fence != 0u &&
                 Receipt->SourceWidth == 16u && Receipt->SourceHeight == 16u &&
                 Receipt->SourcePitch == 64u && Receipt->SourceBytes == 1024ULL &&
                 Receipt->SourceHash != 0ULL &&
                 Receipt->DestinationCpuAddress != 0ULL &&
                 Receipt->DestinationGuestIpa != 0ULL &&
                 Receipt->DestinationPhysicalAddress != 0ULL &&
                 Receipt->DestinationAllocationToken != 0ULL &&
                 Receipt->DestinationBytes == APPLE_AGX_SCANOUT_J313_SURFACE_SIZE &&
                 Receipt->DestinationHash != 0ULL &&
                 (Receipt->DestinationOffset &
                  (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) == 0ULL &&
                 Receipt->DestinationOffset <= APPLE_AGX_SCANOUT_J313_POOL_SIZE -
                                                   APPLE_AGX_SCANOUT_J313_SURFACE_SIZE &&
                 Receipt->ActiveOffsetBefore != Receipt->DestinationOffset &&
                 Receipt->RequestedSequence != 0ULL &&
                 Receipt->AppliedSequence == Receipt->RequestedSequence &&
                 Receipt->LatchedSequence == Receipt->RequestedSequence &&
                 Receipt->ActiveOffsetAfter == Receipt->DestinationOffset &&
                 Receipt->SwapId != 0u
             ? 1
             : 0;
}

#undef VISIBLE_NULL
