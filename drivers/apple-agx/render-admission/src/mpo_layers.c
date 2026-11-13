#include "mpo_layers.h"
#include <string.h>

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
  /* CUR-1 v2: a scratch slot needs the caller's scratch placement. */
  if (Result->Slots[1].ClipMode == AdmissionMpoClipScratch &&
      Result->ScratchOffset == ADMISSION_MPO_SCRATCH_NONE)
    return 0u;
  if (Result->Slots[1].ClipMode != AdmissionMpoClipNone &&
      !MpoLayerRectPacks(&Result->Slots[1].Clip))
    return 0u;
  for (slot = 0u; slot < 2u; ++slot) {
    const ADMISSION_MPO_SLOT *s = &Result->Slots[slot];
    if (slot == 1u && s->ClipMode == AdmissionMpoClipCrop) {
      /* Src = Dst = the visible part (Src and Dst are the panel, 1:1). */
      MpoLayerFill(&Records[slot], s->PoolOffset, s->SurfaceBytes,
                   s->SurfaceWidth, s->SurfaceHeight, s->SurfaceStride,
                   MpoLayerFormat(s->FormatClass), &s->Clip, &s->Clip,
                   APPLE_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED);
    } else if (slot == 1u && s->ClipMode == AdmissionMpoClipScratch) {
      ADMISSION_MPO_RECT source = {0, 0, (int)ADMISSION_MPO_SCRATCH_EXTENT,
                                   (int)ADMISSION_MPO_SCRATCH_EXTENT};
      ADMISSION_MPO_RECT layer;
      unsigned int ox, oy;
      if (!AdmissionMpoScratchPlace(&s->Clip,
                                    (unsigned int)s->Destination.Right,
                                    (unsigned int)s->Destination.Bottom,
                                    &layer, &ox, &oy))
        return 0u;
      MpoLayerFill(&Records[slot], Result->ScratchOffset,
                   ADMISSION_MPO_SCRATCH_BYTES, ADMISSION_MPO_SCRATCH_EXTENT,
                   ADMISSION_MPO_SCRATCH_EXTENT, ADMISSION_MPO_SCRATCH_STRIDE,
                   MpoLayerFormat(s->FormatClass), &source, &layer,
                   APPLE_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED);
    } else {
      MpoLayerFill(&Records[slot], s->PoolOffset, s->SurfaceBytes,
                   s->SurfaceWidth, s->SurfaceHeight, s->SurfaceStride,
                   MpoLayerFormat(s->FormatClass), &s->Source,
                   &s->Destination, APPLE_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED);
    }
  }
  return 2u;
}

/* CUR-1 v2: see mpo_layers.h. */
unsigned int AdmissionMpoScratchPlace(const ADMISSION_MPO_RECT *Clip,
                                      unsigned int PanelWidth,
                                      unsigned int PanelHeight,
                                      ADMISSION_MPO_RECT *Layer,
                                      unsigned int *OffsetX,
                                      unsigned int *OffsetY) {
  const int extent = (int)ADMISSION_MPO_SCRATCH_EXTENT;
  int x, y;
  if (Clip == MPO_LAYERS_NULL || Layer == MPO_LAYERS_NULL ||
      OffsetX == MPO_LAYERS_NULL || OffsetY == MPO_LAYERS_NULL ||
      PanelWidth < ADMISSION_MPO_SCRATCH_EXTENT ||
      PanelHeight < ADMISSION_MPO_SCRATCH_EXTENT || PanelWidth > 0xffffu ||
      PanelHeight > 0xffffu || Clip->Left < 0 || Clip->Top < 0 ||
      Clip->Right <= Clip->Left || Clip->Bottom <= Clip->Top ||
      Clip->Right > (int)PanelWidth || Clip->Bottom > (int)PanelHeight ||
      Clip->Right - Clip->Left > extent || Clip->Bottom - Clip->Top > extent)
    return 0u;
  /* The 64x64 layer at the clip origin, pulled back inside the panel; the
   * block keeps its panel position, so it sits at (clip - layer) in the
   * scratch, and fits because the clip ends inside the panel. */
  x = Clip->Left < (int)PanelWidth - extent ? Clip->Left : (int)PanelWidth - extent;
  y = Clip->Top < (int)PanelHeight - extent ? Clip->Top : (int)PanelHeight - extent;
  Layer->Left = x;
  Layer->Top = y;
  Layer->Right = x + extent;
  Layer->Bottom = y + extent;
  *OffsetX = (unsigned int)(Clip->Left - x);
  *OffsetY = (unsigned int)(Clip->Top - y);
  return 1u;
}

unsigned int AdmissionMpoBlockIsZero(const unsigned char *Source,
                                     unsigned int SourcePitch,
                                     unsigned int Width, unsigned int Height) {
  unsigned int x, y;
  for (y = 0u; y < Height; ++y)
    for (x = 0u; x < Width * 4u; ++x)
      if (Source[(unsigned long long)y * SourcePitch + x] != 0u)
        return 0u;
  return 1u;
}

unsigned int AdmissionMpoScratchStage(unsigned char *Stage,
                                      const unsigned char *Source,
                                      unsigned int SourcePitch,
                                      unsigned int Width, unsigned int Height) {
  const unsigned int row = Width * 4u;
  unsigned long long bits = 0ULL;
  unsigned int y, i;
  for (y = 0u; y < Height; ++y)
    memcpy(Stage + (unsigned long long)y * row,
           Source + (unsigned long long)y * SourcePitch, row);
  /* Rows are whole pixels (4 bytes); the stage is contiguous. */
  for (i = 0u; i + 8u <= row * Height; i += 8u) {
    unsigned long long w;
    memcpy(&w, Stage + i, sizeof(w));
    bits |= w;
  }
  for (; i < row * Height; ++i)
    bits |= Stage[i];
  return bits == 0ULL ? 1u : 0u;
}

void AdmissionMpoScratchFill(unsigned char *Scratch,
                             const unsigned char *Source,
                             unsigned int SourcePitch, unsigned int Width,
                             unsigned int Height, unsigned int OffsetX,
                             unsigned int OffsetY) {
  const unsigned int left = OffsetX * 4u, row = Width * 4u;
  unsigned int y;
  /* CUR-1c: whole-row stores (memset / memcpy) instead of byte stores. */
  for (y = 0u; y < ADMISSION_MPO_SCRATCH_EXTENT; ++y) {
    unsigned char *out = Scratch + (unsigned long long)y * ADMISSION_MPO_SCRATCH_STRIDE;
    if (y < OffsetY || y >= OffsetY + Height) {
      memset(out, 0, ADMISSION_MPO_SCRATCH_STRIDE);
      continue;
    }
    if (left != 0u)
      memset(out, 0, left);
    memcpy(out + left, Source + (unsigned long long)(y - OffsetY) * SourcePitch, row);
    if (left + row < ADMISSION_MPO_SCRATCH_STRIDE)
      memset(out + left + row, 0, ADMISSION_MPO_SCRATCH_STRIDE - left - row);
  }
}

unsigned int AdmissionMpoScratchSelect(const ADMISSION_MPO_SCRATCH_STATE *State,
                                       unsigned int BlockZero,
                                       unsigned int *Write) {
  if (State->Shown < 2u && BlockZero != 0u && State->ShownZero != 0u) {
    *Write = 0u; /* zero-skip: the scanned scratch already shows nothing */
    return State->Shown;
  }
  *Write = 1u;
  return State->Shown < 2u ? State->Shown ^ 1u : 0u;
}

void AdmissionMpoScratchCommitted(ADMISSION_MPO_SCRATCH_STATE *State,
                                  unsigned int Index, unsigned int BlockZero) {
  State->Shown = Index;
  State->ShownZero = BlockZero != 0u ? 1u : 0u;
}

void AdmissionMpoScratchReset(ADMISSION_MPO_SCRATCH_STATE *State) {
  State->Shown = 2u;
  State->ShownZero = 0u;
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

/* FS-1: see mpo_layers.h. */
unsigned int AdmissionMpoBuildLone(const ADMISSION_MPO_PLANE_INPUT *Plane,
                                   APPLE_AGX_SCANOUT_LAYER *Record) {
  if (Plane == MPO_LAYERS_NULL || Record == MPO_LAYERS_NULL ||
      Plane->LayerIndex != 1u || Plane->InScanoutPool == 0u ||
      Plane->PlacementKnown == 0u ||
      MpoLayerFormat(Plane->FormatClass) == 0u ||
      !MpoLayerRectPacks(&Plane->Source) ||
      !MpoLayerRectPacks(&Plane->Destination))
    return 0u;
  /* Nothing beneath: opaque, not premultiplied (as the one-plane swap). */
  MpoLayerFill(Record, Plane->PoolOffset, Plane->SurfaceBytes,
               Plane->SurfaceWidth, Plane->SurfaceHeight, Plane->SurfaceStride,
               MpoLayerFormat(Plane->FormatClass), &Plane->Source,
               &Plane->Destination, 0u);
  return 1u;
}
