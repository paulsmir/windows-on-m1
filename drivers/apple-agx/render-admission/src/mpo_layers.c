#include "mpo_layers.h"

/* S2: see mpo_layers.h. */

#define MPO_LAYERS_NULL ((void *)0)

static unsigned int MpoLayerFormat(unsigned int FormatClass) {
  if (FormatClass == AdmissionMpoFormatBgra8)
    return APPLE_AGX_SCANOUT_FORMAT_BGRA8888;
  if (FormatClass == AdmissionMpoFormatRgba8)
    return APPLE_AGX_SCANOUT_FORMAT_RGBA8888;
  return 0u;
}

/* A rect whose edges fit the broker's 16-bit packing, not inverted. */
static unsigned int MpoLayerRectPacks(const ADMISSION_MPO_RECT *Rect) {
  return Rect->Left >= 0 && Rect->Top >= 0 && Rect->Right >= Rect->Left &&
                 Rect->Bottom >= Rect->Top && Rect->Right <= 0xffff &&
                 Rect->Bottom <= 0xffff
             ? 1u
             : 0u;
}

static void MpoLayerFill(APPLE_AGX_SCANOUT_LAYER *Record,
                         unsigned long long PoolOffset,
                         unsigned long long SurfaceBytes, unsigned int Width,
                         unsigned int Height, unsigned int Stride,
                         unsigned int Format, const ADMISSION_MPO_RECT *Source,
                         const ADMISSION_MPO_RECT *Destination,
                         unsigned int Flags) {
  Record->SurfaceOffset = PoolOffset;
  Record->SurfaceSize = SurfaceBytes;
  Record->Width = Width;
  Record->Height = Height;
  Record->Stride = Stride;
  Record->Format = Format;
  Record->SrcOrigin = APPLE_AGX_SCANOUT_PACK((unsigned int)Source->Left,
                                             (unsigned int)Source->Top);
  Record->SrcSize = APPLE_AGX_SCANOUT_PACK(
      (unsigned int)(Source->Right - Source->Left),
      (unsigned int)(Source->Bottom - Source->Top));
  Record->DstOrigin = APPLE_AGX_SCANOUT_PACK(
      (unsigned int)Destination->Left, (unsigned int)Destination->Top);
  Record->DstSize = APPLE_AGX_SCANOUT_PACK(
      (unsigned int)(Destination->Right - Destination->Left),
      (unsigned int)(Destination->Bottom - Destination->Top));
  Record->Flags = Flags;
}

unsigned int AdmissionMpoBuildCommit(const ADMISSION_MPO_RESULT *Result,
                                     APPLE_AGX_SCANOUT_LAYER Records[2]) {
  unsigned int slot;
  if (Result == MPO_LAYERS_NULL || Records == MPO_LAYERS_NULL ||
      Result->Supported != 1u || Result->SlotCount != 2u)
    return 0u;
  for (slot = 0u; slot < 2u; ++slot) {
    const ADMISSION_MPO_SLOT *s = &Result->Slots[slot];
    if (MpoLayerFormat(s->FormatClass) == 0u ||
        !MpoLayerRectPacks(&s->Source) || !MpoLayerRectPacks(&s->Destination))
      return 0u;
  }
  for (slot = 0u; slot < 2u; ++slot) {
    const ADMISSION_MPO_SLOT *s = &Result->Slots[slot];
    MpoLayerFill(&Records[slot], s->PoolOffset, s->SurfaceBytes,
                 s->SurfaceWidth, s->SurfaceHeight, s->SurfaceStride,
                 MpoLayerFormat(s->FormatClass), &s->Source, &s->Destination,
                 APPLE_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED);
  }
  return 2u;
}

unsigned int AdmissionMpoBuildRemoval(const ADMISSION_MPO_PLANE_INPUT *Plane,
                                      APPLE_AGX_SCANOUT_LAYER *Record) {
  if (Plane == MPO_LAYERS_NULL || Record == MPO_LAYERS_NULL ||
      Plane->LayerIndex != 0u || Plane->InScanoutPool == 0u ||
      Plane->PlacementKnown == 0u ||
      MpoLayerFormat(Plane->FormatClass) == 0u ||
      !MpoLayerRectPacks(&Plane->Source) ||
      !MpoLayerRectPacks(&Plane->Destination))
    return 0u;
  MpoLayerFill(Record, Plane->PoolOffset, Plane->SurfaceBytes,
               Plane->SurfaceWidth, Plane->SurfaceHeight, Plane->SurfaceStride,
               MpoLayerFormat(Plane->FormatClass), &Plane->Source,
               &Plane->Destination, 0u);
  return 1u;
}
