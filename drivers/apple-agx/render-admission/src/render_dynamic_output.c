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

void AdmissionDynamicOutputSnapshotInitialize(
    ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT *Snapshot) {
  if (Snapshot == OUTPUT_NULL)
    return;
  output_zero(Snapshot, (APPLE_AGX_U32)sizeof(*Snapshot));
  Snapshot->Version = ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT_VERSION;
  Snapshot->Bytes = (APPLE_AGX_U32)sizeof(*Snapshot);
}

int AdmissionDynamicOutputSnapshotCapture(
    ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT *Snapshot, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 Generation, APPLE_AGX_U64 SourceGpuVa,
    APPLE_AGX_U64 SourcePhysical, const unsigned char *Source,
    APPLE_AGX_U32 DataBytes, APPLE_AGX_U32 ExpectedLayout,
    APPLE_AGX_U32 ExpectedForegroundColor) {
  APPLE_AGX_U64 hash = 14695981039346656037ULL;
  APPLE_AGX_U32 index;
  if (Snapshot == OUTPUT_NULL || Source == OUTPUT_NULL || Fence == 0u ||
      Generation == 0u || SourceGpuVa == 0ULL || SourcePhysical == 0ULL ||
      SourceGpuVa >= (1ULL << 40u) || SourcePhysical >= (1ULL << 40u) ||
      (SourceGpuVa & 0x3fffULL) != 0ULL ||
      (SourcePhysical & 0x3fffULL) != 0ULL ||
      DataBytes != ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT_CAPACITY ||
      ExpectedLayout != AdmissionDynamicOutputLayoutAgxTiled64 ||
      ExpectedForegroundColor == 0u ||
      ExpectedForegroundColor == 0xa5a5a5a5u ||
      Snapshot->Version != ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT_VERSION ||
      Snapshot->Bytes != sizeof(*Snapshot) || Snapshot->Valid != 0u)
    return 0;
  for (index = 0u; index < DataBytes; ++index) {
    unsigned char value = Source[index];
    Snapshot->Data[index] = value;
    hash ^= value;
    hash *= 1099511628211ULL;
  }
  Snapshot->Fence = Fence;
  Snapshot->Generation = Generation;
  Snapshot->DataBytes = DataBytes;
  Snapshot->Status = 0u;
  Snapshot->Reserved = 0u;
  Snapshot->ExpectedLayout = ExpectedLayout;
  Snapshot->ExpectedForegroundColor = ExpectedForegroundColor;
  Snapshot->SourceGpuVa = SourceGpuVa;
  Snapshot->SourcePhysical = SourcePhysical;
  Snapshot->Fnv1a = hash;
  Snapshot->Valid = 1u;
  return 1;
}

int AdmissionDynamicOutputSnapshotRecordVerification(
    ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT *Snapshot,
    const ADMISSION_DYNAMIC_OUTPUT_RESULT *Result) {
  if (Snapshot == OUTPUT_NULL || Result == OUTPUT_NULL ||
      Snapshot->Version != ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT_VERSION ||
      Snapshot->Bytes != sizeof(*Snapshot) || Snapshot->Valid != 1u ||
      Snapshot->VerificationValid != 0u ||
      Snapshot->DataBytes != ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT_CAPACITY ||
      Result->BytesExamined != Snapshot->DataBytes ||
      Result->Fnv1a != Snapshot->Fnv1a)
    return 0;
  Snapshot->ObservedForegroundColor = Result->ObservedForegroundColor;
  Snapshot->VerificationValid = Result->Valid ? 1u : 2u;
  return 1;
}

int AdmissionDynamicOutputDescribeExpectation(
    APPLE_AGX_U32 Width, APPLE_AGX_U32 Height, APPLE_AGX_U32 Pitch,
    APPLE_AGX_U32 BackgroundColor, APPLE_AGX_U32 ExpectedForegroundColor,
    APPLE_AGX_U32 Layout,
    ADMISSION_DYNAMIC_OUTPUT_EXPECTATION *Expectation) {
  ADMISSION_DYNAMIC_OUTPUT_EXPECTATION candidate;
  if (Expectation == OUTPUT_NULL)
    return 0;
  output_zero(&candidate, (APPLE_AGX_U32)sizeof(candidate));
  candidate.Width = Width;
  candidate.Height = Height;
  candidate.Pitch = Pitch;
  candidate.BackgroundColor = BackgroundColor;
  candidate.ExpectedForegroundColor = ExpectedForegroundColor;
  candidate.Layout = Layout;
  candidate.PoisonByte = 0xa5u;
  if (ExpectedForegroundColor == 0u ||
      ExpectedForegroundColor == BackgroundColor ||
      ExpectedForegroundColor == 0xa5a5a5a5u) {
    output_zero(Expectation, (APPLE_AGX_U32)sizeof(*Expectation));
    return 0;
  }
  if (Width == 2560u && Height == 1600u && Pitch == 10240u &&
      Layout == AdmissionDynamicOutputLayoutLinear) {
    candidate.InteriorX = 1280u;
    candidate.InteriorY = 800u;
    candidate.MinX = 240u;
    candidate.MinY = 150u;
    candidate.MaxX = 2320u;
    candidate.MaxY = 1450u;
    candidate.MinimumForegroundPixels = 1000000u;
    candidate.MaximumForegroundPixels = 1600000u;
  } else if (Width == 16u && Height == 16u && Pitch == 64u &&
             (Layout == AdmissionDynamicOutputLayoutLinear ||
              Layout == AdmissionDynamicOutputLayoutAgxTiled64)) {
    candidate.InteriorX = 8u;
    candidate.InteriorY = 5u;
    candidate.MinX = 2u;
    candidate.MinY = 3u;
    candidate.MaxX = 14u;
    candidate.MaxY = 14u;
    candidate.MinimumForegroundPixels = 72u;
    candidate.MaximumForegroundPixels = 72u;
  } else {
    output_zero(Expectation, (APPLE_AGX_U32)sizeof(*Expectation));
    return 0;
  }
  *Expectation = candidate;
  return 1;
}

static APPLE_AGX_U32 output_tiled_64_index(APPLE_AGX_U32 X,
                                            APPLE_AGX_U32 Y) {
  return ((X & 1u) << 0u) | ((Y & 1u) << 1u) |
         ((X & 2u) << 1u) | ((Y & 2u) << 2u) |
         ((X & 4u) << 2u) | ((Y & 4u) << 3u) |
         ((X & 8u) << 3u) | ((Y & 8u) << 4u) |
         ((X & 16u) << 4u) | ((Y & 16u) << 5u) |
         ((X & 32u) << 5u) | ((Y & 32u) << 6u);
}

static const unsigned char *output_pixel(
    const unsigned char *Bytes,
    const ADMISSION_DYNAMIC_OUTPUT_EXPECTATION *Expectation,
    APPLE_AGX_U32 X, APPLE_AGX_U32 Y) {
  APPLE_AGX_U32 index =
      Expectation->Layout == AdmissionDynamicOutputLayoutAgxTiled64
          ? output_tiled_64_index(X, Y)
          : Y * Expectation->Width + X;
  return Bytes + index * 4u;
}

static int output_expected_triangle(APPLE_AGX_U32 X, APPLE_AGX_U32 Y) {
  APPLE_AGX_U32 halfWidth;
  APPLE_AGX_U32 left;
  APPLE_AGX_U32 right;
  if (Y < 3u || Y > 13u)
    return 0;
  halfWidth = (Y - 3u) / 2u + 1u;
  left = 8u - halfWidth;
  right = 8u + halfWidth;
  return X >= left && X < right;
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
  APPLE_AGX_U32 y;
  APPLE_AGX_U64 hash = 14695981039346656037ULL;
  if (Result != OUTPUT_NULL)
    output_zero(Result, (APPLE_AGX_U32)sizeof(*Result));
  if (Bytes == OUTPUT_NULL || Expectation == OUTPUT_NULL ||
      Result == OUTPUT_NULL || Expectation->Width == 0u ||
      Expectation->Height == 0u ||
      Expectation->ExpectedForegroundColor == 0u ||
      Expectation->ExpectedForegroundColor == Expectation->BackgroundColor ||
      Expectation->ExpectedForegroundColor == 0xa5a5a5a5u ||
      (Expectation->Layout != AdmissionDynamicOutputLayoutLinear &&
       Expectation->Layout != AdmissionDynamicOutputLayoutAgxTiled64) ||
      (Expectation->Layout == AdmissionDynamicOutputLayoutAgxTiled64 &&
       (Expectation->Width > 64u || Expectation->Height > 64u)) ||
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
  for (y = 0u; y < Expectation->Height; ++y) {
    APPLE_AGX_U32 x;
    for (x = 0u; x < Expectation->Width; ++x) {
      APPLE_AGX_U32 index = y * Expectation->Width + x;
      const unsigned char *pixel = output_pixel(Bytes, Expectation, x, y);
      APPLE_AGX_U32 actual = output_read_u32(pixel);
      int outside = x < Expectation->MinX || x >= Expectation->MaxX ||
                    y < Expectation->MinY || y >= Expectation->MaxY;
      int expectedForeground =
          Expectation->Width == 16u && Expectation->Height == 16u
              ? output_expected_triangle(x, y)
              : actual == Expectation->ExpectedForegroundColor;
      if (expectedForeground) {
        if (candidate.ObservedForegroundColor == 0u)
          candidate.ObservedForegroundColor = actual;
        if (actual == Expectation->ExpectedForegroundColor)
          ++candidate.ForegroundPixels;
        else if (candidate.FirstInvalidPixel == 0xffffffffu) {
          candidate.FirstInvalidPixel = index;
          candidate.FirstInvalidValue = actual;
        }
      } else if (actual == Expectation->BackgroundColor) {
        ++candidate.BackgroundPixels;
      } else if (candidate.FirstInvalidPixel == 0xffffffffu) {
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
    }
  }
  for (y = 0u; y < targetBytes; ++y) {
    hash ^= Bytes[y];
    hash *= 1099511628211ULL;
    candidate.BytesExamined = y + 1u;
    if (Progress != OUTPUT_NULL && candidate.BytesExamined != targetBytes &&
        (candidate.BytesExamined % ChunkBytes) == 0u &&
        !Progress(ProgressContext))
      return 0;
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
