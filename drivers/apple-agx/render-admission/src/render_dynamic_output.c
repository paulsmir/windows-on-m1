#include "render_dynamic_output.h"

#define OUTPUT_NULL ((void *)0)

static APPLE_AGX_U32 output_read_u32(const unsigned char *Bytes) {
  return (APPLE_AGX_U32)Bytes[0] | ((APPLE_AGX_U32)Bytes[1] << 8u) |
         ((APPLE_AGX_U32)Bytes[2] << 16u) |
         ((APPLE_AGX_U32)Bytes[3] << 24u);
}

static void output_zero(void *Data, APPLE_AGX_U32 Bytes) {
  unsigned char *data = (unsigned char *)Data;
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    data[index] = 0u;
}

int AdmissionDynamicOutputVerify(
    const unsigned char *Bytes, APPLE_AGX_U32 ByteCount,
    const ADMISSION_DYNAMIC_OUTPUT_EXPECTATION *Expectation,
    APPLE_AGX_U32 ChunkBytes,
    ADMISSION_DYNAMIC_OUTPUT_PROGRESS Progress, void *ProgressContext,
    ADMISSION_DYNAMIC_OUTPUT_RESULT *Result) {
  ADMISSION_DYNAMIC_OUTPUT_RESULT candidate;
  APPLE_AGX_U32 targetBytes;
  APPLE_AGX_U32 poison;
  APPLE_AGX_U32 foreground;
  APPLE_AGX_U32 y;
  APPLE_AGX_U64 hash = 14695981039346656037ULL;
  if (Result != OUTPUT_NULL)
    output_zero(Result, (APPLE_AGX_U32)sizeof(*Result));
  if (Bytes == OUTPUT_NULL || Expectation == OUTPUT_NULL ||
      Result == OUTPUT_NULL || Expectation->Width == 0u ||
      Expectation->Height == 0u ||
      Expectation->Width > ~0u / 4u ||
      Expectation->Pitch != Expectation->Width * 4u ||
      Expectation->Height > ~0u / Expectation->Pitch ||
      Expectation->InteriorX >= Expectation->Width ||
      Expectation->InteriorY >= Expectation->Height ||
      Expectation->MinX >= Expectation->MaxX ||
      Expectation->MinY >= Expectation->MaxY ||
      Expectation->MaxX > Expectation->Width ||
      Expectation->MaxY > Expectation->Height ||
      Expectation->MinimumForegroundPixels == 0u ||
      Expectation->MinimumForegroundPixels >
          Expectation->MaximumForegroundPixels ||
      Expectation->MaximumForegroundPixels >
          Expectation->Width * Expectation->Height ||
      ((Progress == OUTPUT_NULL) != (ChunkBytes == 0u)) ||
      (Progress != OUTPUT_NULL &&
       (ChunkBytes == 0u || (ChunkBytes & 3u) != 0u)))
    return 0;
  targetBytes = Expectation->Pitch * Expectation->Height;
  if (ByteCount < targetBytes)
    return 0;
  output_zero(&candidate, (APPLE_AGX_U32)sizeof(candidate));
  candidate.FirstInvalidPixel = 0xffffffffu;
  poison = Expectation->PoisonByte * 0x01010101u;
  foreground = output_read_u32(
      Bytes + Expectation->InteriorY * Expectation->Pitch +
      Expectation->InteriorX * 4u);
  candidate.ForegroundColor = foreground;
  if (foreground == Expectation->BackgroundColor || foreground == poison)
    candidate.FirstInvalidPixel =
        Expectation->InteriorY * Expectation->Width +
        Expectation->InteriorX;
  for (y = 0u; y < Expectation->Height; ++y) {
    APPLE_AGX_U32 x;
    for (x = 0u; x < Expectation->Width; ++x) {
      APPLE_AGX_U32 index = y * Expectation->Width + x;
      const unsigned char *pixel =
          Bytes + y * Expectation->Pitch + x * 4u;
      APPLE_AGX_U32 actual = output_read_u32(pixel);
      APPLE_AGX_U32 byte;
      int outside = x < Expectation->MinX || x >= Expectation->MaxX ||
                    y < Expectation->MinY || y >= Expectation->MaxY;
      if (actual == Expectation->BackgroundColor)
        ++candidate.BackgroundPixels;
      else if (actual == foreground)
        ++candidate.ForegroundPixels;
      else if (candidate.FirstInvalidPixel == 0xffffffffu) {
        candidate.FirstInvalidPixel = index;
        candidate.FirstInvalidValue = actual;
      }
      if (actual == poison)
        ++candidate.PoisonPixels;
      if (outside && actual != Expectation->BackgroundColor &&
          candidate.FirstInvalidPixel == 0xffffffffu) {
        candidate.FirstInvalidPixel = index;
        candidate.FirstInvalidValue = actual;
      }
      for (byte = 0u; byte < 4u; ++byte) {
        hash ^= pixel[byte];
        hash *= 1099511628211ULL;
      }
      candidate.BytesExamined += 4u;
      if (Progress != OUTPUT_NULL &&
          candidate.BytesExamined != targetBytes &&
          (candidate.BytesExamined % ChunkBytes) == 0u &&
          !Progress(ProgressContext))
        return 0;
    }
  }
  candidate.Fnv1a = hash;
  candidate.Valid =
      candidate.FirstInvalidPixel == 0xffffffffu &&
              candidate.PoisonPixels == 0u &&
              candidate.BackgroundPixels + candidate.ForegroundPixels ==
                  Expectation->Width * Expectation->Height &&
              candidate.ForegroundPixels >=
                  Expectation->MinimumForegroundPixels &&
              candidate.ForegroundPixels <=
                  Expectation->MaximumForegroundPixels
          ? 1u
          : 0u;
  *Result = candidate;
  return 1;
}

#undef OUTPUT_NULL
