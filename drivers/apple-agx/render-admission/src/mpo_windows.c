#include "render_admission.h"
#include "apple_agx_vsync.h" /* HOLD-3: panel rate for the frame period */

/* S2 kernel wiring of the two-plane MPO contract (specification v1,
 * amendments 2026-11-11).  Everything here is effective-only: dxgkrnl calls
 * DxgkDdiGetMultiPlaneOverlayCaps and DxgkDdiCheckMultiPlaneOverlaySupport3
 * only while DRIVERCAPS.SupportMultiPlaneOverlay is TRUE (R22: ADAPTER_
 * DISPLAY::GetMultiPlaneOverlayCaps+0x88 and DXGDEVICE::CheckMultiPlane
 * OverlaySupport3+0x5c return without the miniport otherwise), and
 * DRIVERCAPS sets it only while AdmissionScanoutMpoEffective; the Set3
 * handler in display.c enters AdmissionMpoSetVidPnSourceAddress3 only
 * then.  Not effective, the two DDIs still answer truthfully (one plane). */

/* PITCH-1: Check3/Set3's stride rule is the allocator's display pitch, so
 * every scan-out-capable allocation passes it (render_allocation.h). */
C_ASSERT(ADMISSION_MPO_STRIDE_ALIGNMENT ==
         ADMISSION_ALLOCATION_DISPLAY_PITCH_ALIGNMENT);

/* The reported factors are IEEE-754 singles written as bits
 * (ADMISSION_MPO_FLOAT_BITS_*) so no floating-point instruction is
 * generated in the kernel. */
C_ASSERT(sizeof(float) == sizeof(ULONG));

static ULONG AdmissionMpoFormatClass(ADMISSION_CONTEXT *Context,
                                     const ADMISSION_ALLOCATION_DESCRIPTION
                                         *Description) {
  ULONG code;
  if (Description->BytesPerPixel != ADMISSION_MPO_BYTES_PER_PIXEL)
    return AdmissionMpoFormatOther;
  code = AdmissionScanoutFormatCode(Context,
                                    (D3DDDIFORMAT)Description->Format);
  if (code == APPLE_AGX_SCANOUT_FORMAT_BGRA8888)
    return AdmissionMpoFormatBgra8;
  if (code == APPLE_AGX_SCANOUT_FORMAT_RGBA8888)
    return AdmissionMpoFormatRgba8;
  return AdmissionMpoFormatOther;
}

static ADMISSION_MPO_RECT AdmissionMpoRect(const RECT *Rect) {
  ADMISSION_MPO_RECT rect;
  rect.Left = (int)Rect->left;
  rect.Top = (int)Rect->top;
  rect.Right = (int)Rect->right;
  rect.Bottom = (int)Rect->bottom;
  return rect;
}

/* One WDK plane -> neutral table input.  PlacementKnown planes (Set3) carry
 * their segment address; the pool offset is derived exactly as the V2
 * primary's (AdmissionScanoutQueuePresent: AppleAgxLocalSegmentAddress
 * ToGpuVa over the local segment, size rounded up to 64 KiB). */
static VOID AdmissionMpoPlaneInput(
    ADMISSION_CONTEXT *Context, UINT LayerIndex, BOOLEAN Enabled,
    HANDLE Allocation, const DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES3 *Attributes,
    BOOLEAN PlacementKnown, UINT SegmentId, LONGLONG SegmentAddress,
    ADMISSION_MPO_PLANE_INPUT *Plane) {
  ADMISSION_ALLOCATION_HANDLE *allocation =
      (ADMISSION_ALLOCATION_HANDLE *)Allocation;
  const ADMISSION_ALLOCATION_DESCRIPTION *description;
  ULONGLONG segment_base = 0ULL;
  ULONGLONG pool_bytes = 0ULL;
  APPLE_AGX_U64 offset = 0ULL;

  RtlZeroMemory(Plane, sizeof(*Plane));
  Plane->LayerIndex = LayerIndex;
  Plane->Enabled = Enabled ? 1u : 0u;
  Plane->FormatClass = AdmissionMpoFormatOther;
  Plane->SurfaceIdentity = (unsigned long long)(ULONG_PTR)Allocation;
  if (Attributes != NULL) {
    Plane->ColorSpaceClass =
        Attributes->ColorSpaceType == D3DDDI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
            ? AdmissionMpoColorSpaceSdrSrgb
            : AdmissionMpoColorSpaceOther;
    /* Any reserved blend bit makes the value "not 0/1". */
    Plane->BlendAlpha = Attributes->Blend.Value;
    Plane->Source = AdmissionMpoRect(&Attributes->SrcRect);
    Plane->Destination = AdmissionMpoRect(&Attributes->DstRect);
    Plane->Clip = AdmissionMpoRect(&Attributes->ClipRect);
    Plane->Rotation = Attributes->Rotation == D3DDDI_ROTATION_IDENTITY
                          ? AdmissionMpoRotationIdentity
                          : AdmissionMpoRotationOther;
    /* VerticalFlip / HorizontalFlip; StaticCheck (bit 2) is not a flip. */
    Plane->HasUnsupportedFlags = (Attributes->Flags.Value & 3u) != 0u ? 1u : 0u;
    /* STRETCH-1: examined by the table only for a scaled plane. */
    Plane->StretchQuality = (unsigned int)Attributes->StretchQuality;
  } else {
    Plane->ColorSpaceClass = AdmissionMpoColorSpaceOther;
    Plane->Rotation = AdmissionMpoRotationOther;
  }
  if (allocation == NULL ||
      allocation->Object.Magic != ADMISSION_ALLOCATION_OBJECT_MAGIC)
    return;
  description = &allocation->Object.Description;
  Plane->FormatClass = AdmissionMpoFormatClass(Context, description);
  Plane->SurfaceWidth = description->Width;
  Plane->SurfaceHeight = description->Height;
  Plane->SurfaceStride = description->Pitch;
  /* The broker scans Stride * Height bytes and requires exactly that size
   * in the record.  A valid description's allocation holds at least that
   * (render_allocation.c AdmissionAllocationDescriptionValid; presentation
   * sizes are rounded up to 128 bytes, umd.c AdmissionUmdDescribePrimary),
   * so the scanned extent is the surface; an invalid description keeps its
   * own size and fails the table's size rule. */
  Plane->DescriptionValid =
      AdmissionAllocationDescriptionValid(description) ? 1u : 0u;
  Plane->SurfaceBytes =
      Plane->DescriptionValid != 0u
          ? (unsigned long long)description->Pitch * description->Height
          : description->Size;
  /* Version 3 (one-plane V2 rule): the raw allocation facts. */
  Plane->ScanoutCapable = allocation->ScanoutCapable != 0u ? 1u : 0u;
  Plane->AllocationBytes = description->Size;
  /* allocation_windows.c: a scanout-capable (linear, GPU-only, 32 bpp GDI
   * surface texture) allocation without a Mesa class is placed only in
   * the local segment (ADMISSION_LOCAL_SEGMENT_SET), which is the scanout
   * pool, at the 64 KiB alignment CreateAllocation requests. */
  Plane->InScanoutPool =
      allocation->ScanoutCapable != 0u && allocation->Win32ClassId == 0u
          ? 1u
          : 0u;
  if (!PlacementKnown)
    return;
  Plane->PlacementKnown = 1u;
  if (SegmentId != ADMISSION_MEMORY_LOCAL_SEGMENT || SegmentAddress <= 0 ||
      !AdmissionScanoutMpoPool(Context, &segment_base, &pool_bytes) ||
      AppleAgxLocalSegmentAddressToGpuVa(
          ADMISSION_MEMORY_LOCAL_SEGMENT, SegmentId, segment_base, pool_bytes,
          0ULL, (APPLE_AGX_U64)SegmentAddress,
          (description->Size + (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) &
              ~(APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL),
          0ULL, &offset) != AppleAgxLocalSegmentAddressOk) {
    Plane->InScanoutPool = 0u;
    return;
  }
  Plane->PlacementInPool = 1u;
  Plane->PoolOffset = offset;
}

static VOID AdmissionMpoInput(ADMISSION_CONTEXT *Context,
                              const ADMISSION_MPO_PLANE_INPUT *Planes,
                              ULONG Count, ULONG Path,
                              ADMISSION_MPO_INPUT *Input) {
  ULONGLONG segment_base = 0ULL;
  ULONGLONG pool_bytes = 0ULL;
  RtlZeroMemory(Input, sizeof(*Input));
  Input->Path = Path;
  /* Windows plane rectangles are in source-mode pixels. */
  Input->PanelWidth = Context->CommittedWidth;
  Input->PanelHeight = Context->CommittedHeight;
  Input->PlaneCount = Count;
  Input->Planes = Planes;
  if (AdmissionScanoutMpoPool(Context, &segment_base, &pool_bytes))
    Input->PoolBytes = pool_bytes;
  /* The V2 gate of AdmissionDdiSetVidPnSourceAddress (display.c). */
  Input->PanelStride = Context->CommittedStride;
  Input->PanelModeSupported =
      AdmissionScanoutSourceModeSupported(Context, Context->CommittedWidth,
                                          Context->CommittedHeight,
                                          Context->CommittedStride) &&
              AdmissionScanoutFormatSupported(Context,
                                              Context->CommittedFormat)
          ? 1u
          : 0u;
  /* STRETCH-1: scaled layer records only with broker bit 8; the filter the
   * display scaler applies is not known (STRETCH-1 note). */
  Input->ScalingSupported = AdmissionScanoutMpoScaledSource(Context) ? 1u : 0u;
  Input->ScalerFilter = (unsigned int)ADMISSION_MPO_SCALER_FILTER_CURRENT;
  /* CUR-1 v2: the scan-out scratch was zeroed and read back at start. */
  Input->ScratchAvailable =
      InterlockedCompareExchange(&Context->Mpo.ScratchReady, 0, 0) != 0 ? 1u : 0u;
}

/* STRETCH-1: the plane's source size differs from its destination's. */
static BOOLEAN AdmissionMpoPlaneScaled(const ADMISSION_MPO_PLANE_INPUT *Plane) {
  return (LONGLONG)Plane->Source.Right - Plane->Source.Left !=
                     (LONGLONG)Plane->Destination.Right -
                         Plane->Destination.Left ||
                 (LONGLONG)Plane->Source.Bottom - Plane->Source.Top !=
                     (LONGLONG)Plane->Destination.Bottom -
                         Plane->Destination.Top
             ? TRUE
             : FALSE;
}

/* destination * 1000 / source on one axis, clamped to LONG; 0 for an empty
 * or inverted source.  Receipt only. */
static LONG AdmissionMpoFactorMilli(LONG SourceLow, LONG SourceHigh,
                                    LONG DestinationLow, LONG DestinationHigh) {
  const LONGLONG source = (LONGLONG)SourceHigh - SourceLow;
  const LONGLONG destination = (LONGLONG)DestinationHigh - DestinationLow;
  LONGLONG factor;
  if (source <= 0 || destination < 0)
    return 0;
  factor = destination * 1000LL / source;
  return factor > 0x7fffffffLL ? 0x7fffffff : (LONG)factor;
}

static LONG AdmissionMpoPackSize(const ADMISSION_MPO_RECT *Rect) {
  const ULONG width = (ULONG)((LONGLONG)Rect->Right - Rect->Left) & 0xffffu;
  const ULONG height = (ULONG)((LONGLONG)Rect->Bottom - Rect->Top) & 0xffffu;
  return (LONG)(width | (height << 16));
}

/* STRETCH-1 receipt: one Check3 answer that involved a scaled plane (the
 * first scaled plane in array order). */
static VOID AdmissionMpoRecordScaled(ADMISSION_CONTEXT *Context,
                                     const ADMISSION_MPO_PLANE_INPUT *Planes,
                                     ULONG Count, ULONG Reason) {
  ULONG index;
  const ADMISSION_MPO_PLANE_INPUT *plane = NULL;
  for (index = 0u; index < Count && plane == NULL; ++index)
    if (AdmissionMpoPlaneScaled(&Planes[index]))
      plane = &Planes[index];
  if (plane == NULL)
    return;
  if (Reason == AdmissionMpoSupported)
    InterlockedIncrement(&Context->Mpo.Check3ScaledSupported);
  else
    InterlockedIncrement(&Context->Mpo.Check3ScaledRefused);
  InterlockedExchange(&Context->Mpo.LastScaledReason, (LONG)Reason);
  InterlockedExchange(&Context->Mpo.LastScaledStretchQuality,
                      (LONG)plane->StretchQuality);
  InterlockedExchange(&Context->Mpo.LastScaledSourceSize,
                      AdmissionMpoPackSize(&plane->Source));
  InterlockedExchange(&Context->Mpo.LastScaledDestinationSize,
                      AdmissionMpoPackSize(&plane->Destination));
  InterlockedExchange(
      &Context->Mpo.LastScaledFactorX,
      AdmissionMpoFactorMilli(plane->Source.Left, plane->Source.Right,
                              plane->Destination.Left,
                              plane->Destination.Right));
  InterlockedExchange(
      &Context->Mpo.LastScaledFactorY,
      AdmissionMpoFactorMilli(plane->Source.Top, plane->Source.Bottom,
                              plane->Destination.Top,
                              plane->Destination.Bottom));
}

/* CUR-1 receipt (version 4): one Check3 whose top plane is clipped.
 * Check3 is PASSIVE_LEVEL; rows are claimed lock-free (a lost race only
 * lands the call in another row or ClipOverflow).  Receipt only. */
static BOOLEAN AdmissionMpoRectEqual(const ADMISSION_MPO_RECT *A,
                                     const ADMISSION_MPO_RECT *B) {
  return A->Left == B->Left && A->Top == B->Top && A->Right == B->Right &&
                 A->Bottom == B->Bottom
             ? TRUE
             : FALSE;
}

static VOID AdmissionMpoTicks(volatile LONG *Stats, LONG Ticks) {
  /* Stats: count, min, max, sum (receipt only; races only blur it). */
  LONG count = InterlockedIncrement(&Stats[0]);
  if (count == 1 || Ticks < Stats[1]) InterlockedExchange(&Stats[1], Ticks);
  if (Ticks > Stats[2]) InterlockedExchange(&Stats[2], Ticks);
  InterlockedExchangeAdd(&Stats[3], Ticks);
}

static ULONG AdmissionMpoCrc32(ULONG Crc, const UCHAR *Data, ULONG Bytes) {
  ULONG i, bit;
  for (i = 0u; i < Bytes; ++i) {
    Crc ^= Data[i];
    for (bit = 0u; bit < 8u; ++bit)
      Crc = (Crc >> 1) ^ (0xedb88320u & (0u - (Crc & 1u)));
  }
  return Crc;
}

/* Wom1MpoClipRead: the clip block of a refused clipped top plane, read
 * through the local segment's CPU view.  PASSIVE (Check3).  A copy pass
 * (timed: what a Set3 copy would pay) and an untimed statistics pass. */
static VOID AdmissionMpoReadClip(ADMISSION_CONTEXT *Context,
                                 const ADMISSION_MPO_PLANE_INPUT *Top,
                                 ULONG Reason, BOOLEAN HandleChanged) {
  const ADMISSION_ALLOCATION_HANDLE *allocation =
      (const ADMISSION_ALLOCATION_HANDLE *)(ULONG_PTR)Top->SurfaceIdentity;
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  UCHAR row[64u * 4u];
  const UCHAR *base;
  ULONG width, height, x, y, line, pixel, maxAlpha = 0u, nonZero = 0u;
  ULONG crc = 0xffffffffu, fold = 0u, frequency, index, nowMs;
  BOOLEAN straight = FALSE;
  LONGLONG placement, first, last;
  ULONGLONG offset, end;
  LARGE_INTEGER t0, t1, qpf;
  LONG ticks, count;
  ADMISSION_MPO_CLIP_SAMPLE *sample;

  width = (ULONG)((LONGLONG)Top->Clip.Right - Top->Clip.Left);
  height = (ULONG)((LONGLONG)Top->Clip.Bottom - Top->Clip.Top);
  if (width == 0u || height == 0u || width > 64u || height > 64u ||
      AdmissionMpoPlaneScaled(Top) ||
      (Top->FormatClass != AdmissionMpoFormatBgra8 &&
       Top->FormatClass != AdmissionMpoFormatRgba8) ||
      Top->Clip.Left < Top->Destination.Left ||
      Top->Clip.Top < Top->Destination.Top) {
    InterlockedIncrement(&Context->Mpo.ClipNotEligible);
    return;
  }
  /* Unscaled: surface pixel = source origin + (clip - destination). */
  x = (ULONG)((LONGLONG)Top->Source.Left + Top->Clip.Left -
              Top->Destination.Left);
  y = (ULONG)((LONGLONG)Top->Source.Top + Top->Clip.Top -
              Top->Destination.Top);
  placement = allocation != NULL &&
                      allocation->Object.Magic ==
                          ADMISSION_ALLOCATION_OBJECT_MAGIC
                  ? InterlockedCompareExchange64(
                        (volatile LONGLONG *)&allocation->LocalPlacement, 0, 0)
                  : 0;
  if (placement <= 0 || x + width > Top->SurfaceWidth ||
      y + height > Top->SurfaceHeight ||
      !NT_SUCCESS(AdmissionMemoryRuntimeLocalView(Context, &view)) ||
      view.CpuAddress == NULL) {
    InterlockedIncrement(&Context->Mpo.ClipNotPlaced);
    return;
  }
  offset = (ULONGLONG)(placement - 1) + (ULONGLONG)y * Top->SurfaceStride +
           (ULONGLONG)x * 4u;
  end = offset + (ULONGLONG)(height - 1u) * Top->SurfaceStride +
        (ULONGLONG)width * 4u;
  if (end > view.Bytes || end > (ULONGLONG)(placement - 1) +
                                    Top->SurfaceBytes) {
    InterlockedIncrement(&Context->Mpo.ClipNotPlaced);
    return;
  }
  base = (const UCHAR *)view.CpuAddress + offset;
  t0 = KeQueryPerformanceCounter(&qpf);
  for (line = 0u; line < height; ++line) {
    RtlCopyMemory(row, base + (SIZE_T)line * Top->SurfaceStride,
                  (SIZE_T)width * 4u);
    fold ^= row[0];
  }
  t1 = KeQueryPerformanceCounter(NULL);
  ticks = (LONG)(t1.QuadPart - t0.QuadPart);
  for (line = 0u; line < height; ++line) {
    RtlCopyMemory(row, base + (SIZE_T)line * Top->SurfaceStride,
                  (SIZE_T)width * 4u);
    crc = AdmissionMpoCrc32(crc, row, width * 4u);
    for (pixel = 0u; pixel < width; ++pixel) {
      const UCHAR *p = &row[pixel * 4u];
      if (p[3] > maxAlpha) maxAlpha = p[3];
      if (p[3] != 0u) ++nonZero;
      if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3]) straight = TRUE;
    }
  }
  crc = ~crc;
  UNREFERENCED_PARAMETER(fold);
  InterlockedIncrement(&Context->Mpo.ClipReads);
  if (straight) InterlockedIncrement(&Context->Mpo.ClipStraight);
  frequency = (ULONG)(qpf.QuadPart / 1000);
  InterlockedExchange(&Context->Mpo.QpcFrequencyKhz, (LONG)frequency);
  if (width == 1u && height == 1u) {
    AdmissionMpoTicks(Context->Mpo.ReadTicks1x1, ticks);
    InterlockedIncrement(maxAlpha == 0u ? &Context->Mpo.ClipZero1x1
                                        : &Context->Mpo.ClipNonZero1x1);
  } else {
    AdmissionMpoTicks(Context->Mpo.ReadTicksOther, ticks);
  }
  /* At most one sample per 250 ms. */
  nowMs = frequency != 0u ? (ULONG)(t1.QuadPart / (LONGLONG)frequency) : 0u;
  first = InterlockedCompareExchange(&Context->Mpo.ClipSampleCount, 0, 0);
  last = (LONG)InterlockedCompareExchange(&Context->Mpo.ClipLastSampleMs, 0,
                                          0);
  if (first != 0 && (ULONG)(nowMs - (ULONG)last) < 250u)
    return;
  InterlockedExchange(&Context->Mpo.ClipLastSampleMs, (LONG)nowMs);
  count = InterlockedIncrement(&Context->Mpo.ClipSampleCount);
  index = (ULONG)(count - 1);
  if (index >= ADMISSION_MPO_CLIP_FIXED)
    index = ADMISSION_MPO_CLIP_FIXED +
            (index - ADMISSION_MPO_CLIP_FIXED) %
                (ADMISSION_MPO_CLIP_SAMPLES - ADMISSION_MPO_CLIP_FIXED);
  sample = &Context->Mpo.ClipSamples[index];
  InterlockedExchange(&sample->Origin,
                      (LONG)(((ULONG)Top->Clip.Left & 0xffffu) |
                             (((ULONG)Top->Clip.Top & 0xffffu) << 16)));
  InterlockedExchange(&sample->Size, (LONG)(width | (height << 16)));
  InterlockedExchange(&sample->Info,
                      (LONG)((Top->FormatClass & 0xffu) |
                             ((Top->BlendAlpha & 0xffu) << 8) |
                             ((HandleChanged ? 1u : 0u) << 16) |
                             ((Reason & 0xffu) << 24)));
  InterlockedExchange(&sample->Alpha,
                      (LONG)(maxAlpha | ((nonZero & 0xffffu) << 8)));
  InterlockedExchange(&sample->Crc32, (LONG)crc);
  InterlockedExchange(&sample->Ticks, ticks);
  InterlockedExchange(&sample->Qpc, (LONG)(ULONG)t1.QuadPart);
}

static VOID AdmissionMpoRecordClipped(ADMISSION_CONTEXT *Context,
                                      const ADMISSION_MPO_PLANE_INPUT *Planes,
                                      ULONG Count, ULONG Reason) {
  const ADMISSION_MPO_PLANE_INPUT *top;
  ULONG width, height, key, index;
  LONG handle;
  BOOLEAN handleChanged;
  if (Count == 1u) {
    if (!AdmissionMpoRectEqual(&Planes[0].Clip, &Planes[0].Destination))
      InterlockedIncrement(&Context->Mpo.ClipOnePlane);
    return;
  }
  if (Count != 2u)
    return;
  top = Planes[1].LayerIndex > Planes[0].LayerIndex ? &Planes[1] : &Planes[0];
  if (AdmissionMpoRectEqual(&top->Clip, &top->Destination))
    return;
  width = (ULONG)((LONGLONG)top->Clip.Right - top->Clip.Left) & 0xfffu;
  height = (ULONG)((LONGLONG)top->Clip.Bottom - top->Clip.Top) & 0xfffu;
  key = width | (height << 12) | ((top->FormatClass & 0xfu) << 24) |
        ((top->BlendAlpha & 0xfu) << 28);
  if (key == 0u)
    key = 1u << 31;
  handle = (LONG)(ULONG_PTR)top->SurfaceIdentity;
  handleChanged =
      InterlockedExchange(&Context->Mpo.LastClipHandle, handle) != handle
          ? TRUE
          : FALSE;
  InterlockedIncrement(&Context->Mpo.ClipChecks);
  if (handleChanged)
    InterlockedIncrement(&Context->Mpo.ClipHandleChanges);
  if (Reason != AdmissionMpoSupported &&
      InterlockedCompareExchange(&Context->Mpo.ClipReadSwitch, 0, 0) != 0)
    AdmissionMpoReadClip(Context, top, Reason, handleChanged);
  for (index = 0u; index < ADMISSION_MPO_CLIP_ROWS; ++index) {
    ADMISSION_MPO_CLIP_ROW *row = &Context->Mpo.ClipRows[index];
    LONG seen = InterlockedCompareExchange(&row->Key, (LONG)key, 0);
    if (seen != 0 && seen != (LONG)key)
      continue;
    InterlockedIncrement(&row->Count);
    if (Reason == AdmissionMpoSupported)
      InterlockedIncrement(&row->Supported);
    if (handleChanged)
      InterlockedIncrement(&row->HandleChanges);
    InterlockedExchange(&row->LastOrigin,
                        (LONG)(((ULONG)top->Clip.Left & 0xffffu) |
                               (((ULONG)top->Clip.Top & 0xffffu) << 16)));
    InterlockedExchange(&row->LastDstSize,
                        AdmissionMpoPackSize(&top->Destination));
    return;
  }
  InterlockedIncrement(&Context->Mpo.ClipOverflow);
}

/* ---- DxgkDdiGetMultiPlaneOverlayCaps ---- */

_Use_decl_annotations_ NTSTATUS AdmissionDdiGetMultiPlaneOverlayCaps(
    CONST HANDLE hAdapter, DXGKARG_GETMULTIPLANEOVERLAYCAPS *Caps) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)hAdapter;
  ADMISSION_MPO_CAPS derived;
  BOOLEAN effective;

  if (context == NULL || Caps == NULL)
    return STATUS_INVALID_PARAMETER;
  effective = AdmissionScanoutMpoEffective(context);
  /* Specification v1: MaxPlanes 2, MaxRGBPlanes 2, MaxYUVPlanes 0.  Not
   * effective (dxgkrnl does not call then, R22): one plane, the values
   * dxgkrnl itself substitutes (R22 Q1, defaults at +0x58..+0x78).
   * STRETCH-1 (AdmissionMpoCapsFor): with broker bit 8, StretchRGB and
   * factors 2.0 / 0.5 (the broker's per-axis limit); otherwise no stretch
   * bit and factors 1.0 as before.  No filter bit: the display scaler's
   * filter is not known. */
  AdmissionMpoCapsFor(effective ? 1u : 0u,
                      AdmissionScanoutMpoScaledSource(context) ? 1u : 0u,
                      (unsigned int)ADMISSION_MPO_SCALER_FILTER_CURRENT,
                      (unsigned int)Caps->VidPnSourceId, &derived);
  Caps->MaxPlanes = derived.MaxPlanes;
  Caps->MaxRGBPlanes = Caps->MaxPlanes;
  Caps->MaxYUVPlanes = 0u;
  Caps->OverlayCaps.Value = 0u;
  Caps->OverlayCaps.StretchRGB = derived.StretchRgb & 1u;
  Caps->OverlayCaps.BilinearFilter = derived.BilinearFilter & 1u;
  Caps->OverlayCaps.HighFilter = derived.HighFilter & 1u;
  RtlCopyMemory(&Caps->MaxStretchFactor, &derived.MaxStretchBits,
                sizeof(Caps->MaxStretchFactor));
  RtlCopyMemory(&Caps->MaxShrinkFactor, &derived.MaxShrinkBits,
                sizeof(Caps->MaxShrinkFactor));
  if (effective) {
    InterlockedIncrement(&context->Mpo.CapsCalls);
    InterlockedExchange(&context->Mpo.CapsStretchRgb,
                        (LONG)derived.StretchRgb);
    InterlockedExchange(&context->Mpo.CapsMaxStretchBits,
                        (LONG)derived.MaxStretchBits);
    InterlockedExchange(&context->Mpo.CapsMaxShrinkBits,
                        (LONG)derived.MaxShrinkBits);
  }
  /* S2-1b: every call, ungated; persisted on the first calls only. */
  {
    const LONG all = InterlockedIncrement(&context->MpoGate.CapsCallsAll);
    if (all == 1)
      context->MpoGate.CapsFirstQpc = AdmissionMpoGateQpc(context);
    InterlockedExchange(&context->MpoGate.CapsLastVidPnSourceId,
                        (LONG)Caps->VidPnSourceId);
    InterlockedExchange(&context->MpoGate.CapsLastMaxPlanes,
                        (LONG)Caps->MaxPlanes);
    if (all <= 8 || (all & (all - 1)) == 0)
      AdmissionRecordMpoGate(context);
  }
  return STATUS_SUCCESS;
}

/* ---- DxgkDdiCheckMultiPlaneOverlaySupport3 ---- */

/* Post-composition transforms the panel cannot apply (no
 * DxgkDdiGetPostCompositionCaps; scaling, flips, rotation). */
static BOOLEAN AdmissionMpoPostCompositionIdentity(
    const DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3 *Args) {
  UINT index;
  const DXGK_MULTIPLANE_OVERLAY_POST_COMPOSITION *post;
  if (Args->PostCompositionCount == 0u)
    return TRUE;
  if (Args->ppPostComposition == NULL)
    return FALSE;
  for (index = 0u; index < Args->PostCompositionCount; ++index) {
    if (Args->ppPostComposition[index] == NULL)
      return FALSE;
    post = &Args->ppPostComposition[index]->PostComposition;
    if (post->Flags.Value != 0u ||
        (post->Rotation != D3DDDI_ROTATION_IDENTITY && post->Rotation != 0) ||
        post->SrcRect.right - post->SrcRect.left !=
            post->DstRect.right - post->DstRect.left ||
        post->SrcRect.bottom - post->SrcRect.top !=
            post->DstRect.bottom - post->DstRect.top)
      return FALSE;
  }
  return TRUE;
}

static VOID AdmissionMpoCheckAnswer(ADMISSION_CONTEXT *Context,
                                    BOOLEAN Effective,
                                    DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3 *Args,
                                    ULONG Reason, ULONG FailingPlane) {
  LONG calls;
  BOOLEAN changed;
  Args->Supported = Reason == AdmissionMpoSupported ? TRUE : FALSE;
  Args->ReturnInfo.Value = 0u;
  /* FailingPlane is a 4-bit index of the first unsupported plane. */
  if (Reason != AdmissionMpoSupported && FailingPlane < 16u)
    Args->ReturnInfo.FailingPlane = FailingPlane;
  if (!Effective)
    return;
  calls = InterlockedIncrement(&Context->Mpo.Check3Calls);
  if (Reason == AdmissionMpoSupported) {
    InterlockedIncrement(&Context->Mpo.Check3Supported);
  } else {
    InterlockedIncrement(&Context->Mpo.Check3Unsupported);
    if (Reason < ADMISSION_MPO_RECEIPT_REASONS)
      InterlockedIncrement(&Context->Mpo.Check3Reasons[Reason]);
  }
  changed = InterlockedExchange(&Context->Mpo.LastCheck3Reason,
                                (LONG)Reason) != (LONG)Reason;
  InterlockedExchange(&Context->Mpo.LastCheck3FailingPlane,
                      (LONG)FailingPlane);
  InterlockedExchange(&Context->Mpo.LastCheck3Planes,
                      (LONG)Args->PlaneCount);
  /* Check3 is PASSIVE_LEVEL (Microsoft Learn); persist on an answer change
   * and on power-of-two call counts, not on every call. */
  if (changed || (calls & (calls - 1)) == 0)
    AdmissionRecordMpo(Context);
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiCheckMultiPlaneOverlaySupport3(
    CONST HANDLE hAdapter, DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3 *Args) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)hAdapter;
  ADMISSION_MPO_PLANE_INPUT planes[ADMISSION_MPO_MAX_PLANES];
  ADMISSION_MPO_INPUT input;
  ADMISSION_MPO_RESULT result;
  const DXGK_MULTIPLANE_OVERLAY_PLANE_WITH_SOURCE2 *plane;
  BOOLEAN effective;
  UINT index;

  if (context == NULL || Args == NULL)
    return STATUS_INVALID_PARAMETER;
  /* S2-1b: every Check3 call, ungated. */
  {
    const LONG all = InterlockedIncrement(&context->MpoGate.Check3CallsAll);
    if (all == 1)
      context->MpoGate.Check3FirstQpc = AdmissionMpoGateQpc(context);
    if (all <= 8)
      AdmissionRecordMpoGate(context);
  }
  effective = AdmissionScanoutMpoEffective(context);
  if (Args->PlaneCount == 0u ||
      Args->PlaneCount > (effective ? ADMISSION_MPO_MAX_PLANES : 1u)) {
    AdmissionMpoCheckAnswer(context, effective, Args,
                            AdmissionMpoReasonPlaneCount,
                            ADMISSION_MPO_NO_PLANE);
    return STATUS_SUCCESS;
  }
  if (Args->ppPlanes == NULL) {
    AdmissionMpoCheckAnswer(context, effective, Args,
                            AdmissionMpoReasonArgument,
                            ADMISSION_MPO_NO_PLANE);
    return STATUS_SUCCESS;
  }
  for (index = 0u; index < Args->PlaneCount; ++index) {
    plane = Args->ppPlanes[index];
    /* One source (VidPnSourceId 0); dxgkrnl merges every source's planes
     * into one call (R22 Q3), so another source is a failing plane. */
    if (plane == NULL || plane->VidPnSourceId != 0u) {
      AdmissionMpoCheckAnswer(context, effective, Args,
                              AdmissionMpoReasonArgument, index);
      return STATUS_SUCCESS;
    }
    AdmissionMpoPlaneInput(context, plane->LayerIndex, TRUE,
                           plane->hAllocation, &plane->PlaneAttributes,
                           FALSE, 0u, 0LL, &planes[index]);
  }
  if (Args->PlaneCount == 2u && !AdmissionMpoPostCompositionIdentity(Args)) {
    AdmissionMpoCheckAnswer(context, effective, Args,
                            AdmissionMpoReasonArgument,
                            ADMISSION_MPO_NO_PLANE);
    return STATUS_SUCCESS;
  }
  AdmissionMpoInput(context, planes, Args->PlaneCount,
                    ADMISSION_MPO_PATH_CHECK3, &input);
  /* A1.1: while effective, one plane is answered with the V2 path's own
   * acceptance rule, so Check3 never promises a flip Set3 refuses.  Not
   * effective: the version-1 answer, unchanged. */
  /* STRETCH-1: a one-plane configuration that is not the full-screen
   * primary goes as one layer record (AdmissionMpoEvaluateOne); the
   * full-screen primary keeps the V2 rule. */
  if (effective && Args->PlaneCount == 1u)
    (void)AdmissionMpoEvaluateOne(&input, &result);
  else
    (void)AdmissionMpoEvaluate(&input, &result);
  /* FS-1 receipt: one-plane answers for the application plane alone. */
  if (effective && Args->PlaneCount == 1u && planes[0].LayerIndex == 1u)
    InterlockedIncrement(result.Reason == AdmissionMpoSupported
                             ? &context->Mpo.LoneChecksSupported
                             : &context->Mpo.LoneChecksRefused);
  if (effective) {
    AdmissionMpoRecordScaled(context, planes, Args->PlaneCount,
                             result.Reason);
    AdmissionMpoRecordClipped(context, planes, Args->PlaneCount,
                              result.Reason);
  }
  AdmissionMpoCheckAnswer(context, effective, Args, result.Reason,
                          result.FailingPlane);
  return STATUS_SUCCESS;
}

/* ---- DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3, effective ---- */

/* S3-C receipt (Busy): the PresentIds of the last Set3 the scan-out gate
 * refused with STATUS_DEVICE_BUSY, the PresentIds of the next successful
 * Set3, and whether one of the refused ids latched later (dxgkrnl re-issued
 * the same flip) or not (a later flip superseded it).  Low 32 bits. */
static NTSTATUS AdmissionMpoBusyNote(ADMISSION_CONTEXT *Context,
                                     const ADMISSION_MPO_COMPLETION *Completion,
                                     NTSTATUS Status) {
  LONG id = Completion->Count != 0u ? (LONG)(ULONG)Completion->PresentId[0] : 0;
  if (Status == STATUS_DEVICE_BUSY) {
    /* S3-B freeze detector: consecutive Busy answers with no accepted Set3
     * between them (the Set3-level PresentId is renumbered per offer, so
     * it cannot identify a flip; EXP2375). */
    LONG run = 1;
    if (InterlockedCompareExchange(&Context->Mpo.BusyOutstanding, 0, 0) != 0)
      run = InterlockedIncrement(&Context->Mpo.ConsecutiveBusy);
    else
      InterlockedExchange(&Context->Mpo.ConsecutiveBusy, 1);
    if (run > Context->Mpo.MaxConsecutiveBusy)
      InterlockedExchange(&Context->Mpo.MaxConsecutiveBusy, run);
    InterlockedExchange(&Context->Mpo.LastBusyPresentId, id);
    InterlockedExchange(&Context->Mpo.LastBusyPresentId1,
                        Completion->Count > 1u
                            ? (LONG)(ULONG)Completion->PresentId[1]
                            : 0);
    InterlockedExchange(&Context->Mpo.BusyOutstanding, 1);
    return Status;
  }
  if (NT_SUCCESS(Status)) {
    /* HOLD-1 receipt: every PresentId of an accepted Set3, per layer. */
    ULONG index;
    for (index = 0u; index < Completion->Count && index < 2u; ++index)
      InterlockedIncrement(Completion->LayerIndex[index] != 0u
                               ? &Context->Mpo.AcceptedIds1
                               : &Context->Mpo.AcceptedIds0);
  }
  if (!NT_SUCCESS(Status) ||
      InterlockedCompareExchange(&Context->Mpo.BusyOutstanding, 0, 0) == 0)
    return Status;
  InterlockedExchange(&Context->Mpo.BusyOutstanding, 0);
  InterlockedExchange(&Context->Mpo.ConsecutiveBusy, 0);
  InterlockedExchange(&Context->Mpo.NextSuccessPresentId, id);
  /* BusyThenSameLatched / BusyThenSuperseded stay 0: they compared the
   * Set3-level PresentId, which dxgkrnl renumbers per offer (EXP2375: the
   * per-layer flip identity was continuous, every Busy flip re-offered). */
  return Status;
}

/* FS-1 receipt: the scan-out form after an accepted commit, and the
 * transitions into and out of the lone application plane. */
static VOID AdmissionMpoNoteForm(ADMISSION_CONTEXT *Context, LONG Form) {
  const LONG previous = InterlockedExchange(&Context->Mpo.LastForm, Form);
  if (previous == Form)
    return;
  if (Form == ADMISSION_MPO_FORM_LONE)
    InterlockedIncrement(&Context->Mpo.ToLone);
  else if (previous == ADMISSION_MPO_FORM_LONE)
    InterlockedIncrement(&Context->Mpo.FromLone);
}

/* CUR-1 v2: fill the free scan-out scratch with the visible block of the
 * clipped top plane and set Result->ScratchOffset.  Set3 runs up to DIRQL:
 * only reads through the nonpaged write-combined pool view, writes into the
 * scratch, no wait, no mapping.  The scratch DCP scans is the last one
 * committed, because one commit is outstanding until its latch; so the
 * write waits for the gate to be free (else DEVICE_BUSY, nothing written).
 * Returns STATUS_SUCCESS with *Index/*Zero for AdmissionMpoScratchCommitted. */
static NTSTATUS AdmissionMpoScratchPrepare(ADMISSION_CONTEXT *Context,
                                           const ADMISSION_MPO_PLANE_INPUT *Top,
                                           ADMISSION_MPO_RESULT *Result,
                                           ULONG *Index, ULONG *Zero) {
  const ADMISSION_MPO_SLOT *slot = &Result->Slots[1];
  const PUCHAR pool = Context->MpoScratchCpu != NULL
                          ? Context->MpoScratchCpu - Context->MpoScratchPoolOffset
                          : NULL;
  ADMISSION_MPO_RECT layer;
  unsigned int ox, oy, write = 1u;
  ULONG w, h, index;
  const UCHAR *source;
  LARGE_INTEGER t0, t1;
  LONG ticks;

  if (pool == NULL || Top->PlacementInPool == 0u ||
      !AdmissionMpoScratchPlace(&slot->Clip, (unsigned int)slot->Destination.Right,
                                (unsigned int)slot->Destination.Bottom, &layer,
                                &ox, &oy)) {
    InterlockedIncrement(&Context->Mpo.S3BusyBuild);
    return STATUS_DEVICE_BUSY;
  }
  if (!AdmissionScanoutMpoGateFree(Context)) {
    InterlockedIncrement(&Context->Mpo.Busy);
    return STATUS_DEVICE_BUSY;
  }
  w = (ULONG)(slot->Clip.Right - slot->Clip.Left);
  h = (ULONG)(slot->Clip.Bottom - slot->Clip.Top);
  /* Src = Dst = panel (rule 8): surface pixel = panel pixel. */
  source = pool + Top->PoolOffset + (ULONGLONG)(ULONG)slot->Clip.Top * Top->SurfaceStride +
           (ULONGLONG)(ULONG)slot->Clip.Left * 4u;
  t0 = KeQueryPerformanceCounter(NULL);
  /* CUR-1c: one wide-load pass over the write-combined source into the
   * cached stage; the zero test and the fill read the stage. */
  *Zero = AdmissionMpoScratchStage(Context->MpoScratchStage, source,
                                   Top->SurfaceStride, w, h);
  index = AdmissionMpoScratchSelect(&Context->MpoScratch, *Zero, &write);
  if (write != 0u)
    AdmissionMpoScratchFill(Context->MpoScratchCpu + index * ADMISSION_SCANOUT_SCRATCH_STEP,
                            Context->MpoScratchStage, w * 4u, w, h, ox, oy);
  else
    InterlockedIncrement(&Context->Mpo.ScratchSkippedWrites);
  KeMemoryBarrier(); /* the scratch bytes before COMMIT_LAYERS */
  t1 = KeQueryPerformanceCounter(NULL);
  ticks = (LONG)(t1.QuadPart - t0.QuadPart);
  AdmissionMpoTicks(Context->Mpo.ScratchCopyTicks, ticks);
  Result->ScratchOffset = Context->MpoScratchPoolOffset +
                          (ULONGLONG)index * ADMISSION_SCANOUT_SCRATCH_STEP;
  *Index = index;
  return STATUS_SUCCESS;
}

/* A Set3 table refusal.  STRETCH-1b: one that follows a supported Check3
 * answer is a contract defect (Check3 promised what Set3 refuses) and is
 * counted with its reason. */
static VOID AdmissionMpoSet3TableRefusal(ADMISSION_CONTEXT *Context,
                                         ULONG Reason) {
  InterlockedIncrement(&Context->Mpo.Set3TableRefusals);
  InterlockedExchange(&Context->Mpo.LastSet3TableReason, (LONG)Reason);
  if (InterlockedCompareExchange(&Context->Mpo.LastCheck3Reason, 0, 0) ==
      (LONG)AdmissionMpoSupported) {
    InterlockedIncrement(&Context->Mpo.Set3RefusedAfterSupport);
    InterlockedExchange(&Context->Mpo.LastSet3RefusedAfterSupportReason,
                        (LONG)Reason);
  }
}

/* STRETCH-1b receipt: the raw StretchQuality of every scaled plane Set3
 * evaluates (0, 1, 2, other), the last one, next to Check3's
 * LastScaledStretchQuality. */
static VOID AdmissionMpoSet3Quality(ADMISSION_CONTEXT *Context,
                                    const ADMISSION_MPO_PLANE_INPUT *Plane) {
  const ULONG q = Plane->StretchQuality;
  if (!AdmissionMpoPlaneScaled(Plane))
    return;
  InterlockedIncrement(&Context->Mpo.Set3StretchQuality[q < 3u ? q : 3u]);
  InterlockedExchange(&Context->Mpo.LastSet3StretchQuality, (LONG)q);
}



static VOID AdmissionMpoCompletionAdd(ADMISSION_MPO_COMPLETION *Completion,
                                      ULONG LayerIndex, ULONGLONG PresentId) {
  if (Completion->Count >= 2u)
    return;
  Completion->LayerIndex[Completion->Count] = LayerIndex;
  Completion->PresentId[Completion->Count] = PresentId;
  ++Completion->Count;
}

static VOID AdmissionMpoRefuseSet3(ADMISSION_CONTEXT *Context) {
  InterlockedIncrement(&Context->Mpo.Set3Refusals);
}

/* Planes a Set3 does not list keep their state (ADMISSION_MPO_PLANE_STATE);
 * the call's planes update a copy, which becomes current only when the
 * resulting flip was accepted.
 *   both planes enabled          -> one COMMIT_LAYERS with two records;
 *   LayerIndex 0 only, slot 1 may
 *   be on screen                 -> COMMIT_LAYERS with one record (removal);
 *   LayerIndex 0 only, slot 1 off-> today's V2 PRESENT path, unchanged;
 *   no plane enabled             -> today's plane-off completion (or the
 *                                   removal while slot 1 may be shown);
 *   LayerIndex 1 alone           -> refused (no Check3 answer admits it). */
/* HOLD-1: realise a complete plane set (next, the layers it lists, their
 * completion entries): the dispatch Set3 always did, shared with the latch
 * handler that commits a held set.  DryRun evaluates and builds every
 * record without committing or changing state (a set may be held only if
 * it would be accepted now).  Runs at device IRQL: MMIO and memory only. */
static NTSTATUS AdmissionMpoApply(ADMISSION_CONTEXT *Context,
                                  ADMISSION_MPO_PLANE_STATE *next,
                                  const BOOLEAN *listed,
                                  ADMISSION_MPO_COMPLETION *completion,
                                  BOOLEAN DryRun) {
  ADMISSION_MPO_PLANE_INPUT pair[2];
  ULONG scratch_index = 0u, scratch_zero = 0u;
  ADMISSION_MPO_INPUT input;
  ADMISSION_MPO_RESULT result;
  APPLE_AGX_SCANOUT_LAYER records[2];
  DXGKARG_SETVIDPNSOURCEADDRESS legacy;
  BOOLEAN slot1;
  NTSTATUS status;

  if (next[0].Enabled && next[1].Enabled) {
    pair[0] = next[0].Input;
    pair[1] = next[1].Input;
    AdmissionMpoInput(Context, pair, 2u, ADMISSION_MPO_PATH_SET3, &input);
    if (AdmissionMpoEvaluate(&input, &result) != AdmissionMpoSupported) {
      /* Check3 cannot see placement; Set3 can.  Do not send records the
       * table rejects.  S3-A/S3-B: for a configuration Check3 accepted this
       * is an invariant violation (counted); DEVICE_BUSY either way. */
      /* HOLD-2: Set3 validates by dry run; its refusal is the Set3 view. */
      AdmissionMpoSet3TableRefusal(Context, result.Reason);
      return STATUS_DEVICE_BUSY;
    }
    if (result.Slots[1].ClipMode == AdmissionMpoClipScratch) {
      /* CUR-1 v2: the clipped top plane through the free scratch.  A dry run
       * (HOLD-1 merge check) writes nothing: it validates the records with
       * scratch 0's offset; the real commit prepares the free one. */
      if (DryRun) {
        if (Context->MpoScratchCpu == NULL)
          return STATUS_DEVICE_BUSY;
        result.ScratchOffset = Context->MpoScratchPoolOffset;
      } else {
        status = AdmissionMpoScratchPrepare(
            Context, &pair[result.Slots[1].PlaneIndex], &result, &scratch_index,
            &scratch_zero);
        if (!NT_SUCCESS(status))
          return status;
      }
    }
    if (AdmissionMpoBuildCommit(&result, records) != 2u) {
      if (!DryRun)
        InterlockedIncrement(&Context->Mpo.S3BusyBuild); /* D2 invariant */
      return STATUS_DEVICE_BUSY;
    }
    status = DryRun ? STATUS_SUCCESS : AdmissionScanoutMpoCommit(Context, records, 2u, completion,
                                       next[0].PrimaryAddress);
    if (NT_SUCCESS(status) && !DryRun) {
      /* CUR-1 v2: which scratch DCP scans from this commit on. */
      if (result.Slots[1].ClipMode == AdmissionMpoClipScratch) {
        AdmissionMpoScratchCommitted(&Context->MpoScratch, scratch_index,
                                     scratch_zero);
        InterlockedIncrement(&Context->Mpo.ScratchCommits);
      } else {
        AdmissionMpoScratchReset(&Context->MpoScratch);
        if (result.Slots[1].ClipMode == AdmissionMpoClipCrop)
          InterlockedIncrement(&Context->Mpo.CropCommits);
      }
      RtlCopyMemory(Context->MpoPlanes, next, sizeof(Context->MpoPlanes));
      InterlockedIncrement(&Context->Mpo.TwoPlaneCommits);
      AdmissionMpoNoteForm(Context, ADMISSION_MPO_FORM_TWO);
      if (AdmissionMpoPlaneScaled(&pair[0]) || AdmissionMpoPlaneScaled(&pair[1]))
        InterlockedIncrement(&Context->Mpo.ScaledTwoPlaneCommits);
    }
    return status;
  }
  if (!next[0].Enabled && next[1].Enabled) {
    /* FS-1: the application plane alone (DWM's top plane off): one layer
     * record in slot 0, slot 1 off; the rule one-plane Check3 answered
     * with (AdmissionMpoEvaluateOne). */
    pair[0] = next[1].Input;
    AdmissionMpoInput(Context, pair, 1u, ADMISSION_MPO_PATH_SET3, &input);
    if (AdmissionMpoEvaluateOne(&input, &result) != AdmissionMpoSupported) {
      /* HOLD-2: Set3 validates by dry run; its refusal is the Set3 view. */
      AdmissionMpoSet3TableRefusal(Context, result.Reason);
      return STATUS_DEVICE_BUSY;
    }
    if (AdmissionMpoBuildLone(&next[1].Input, &records[0]) != 1u) {
      if (!DryRun)
        InterlockedIncrement(&Context->Mpo.S3BusyBuild);
      return STATUS_DEVICE_BUSY;
    }
    status = DryRun ? STATUS_SUCCESS : AdmissionScanoutMpoCommit(Context, records, 1u, completion,
                                       next[1].PrimaryAddress);
    if (NT_SUCCESS(status) && !DryRun) {
      RtlCopyMemory(Context->MpoPlanes, next, sizeof(Context->MpoPlanes));
      InterlockedIncrement(&Context->Mpo.LoneCommits);
      AdmissionMpoScratchReset(&Context->MpoScratch); /* CUR-1 v2: slot 1 off */
      if (AdmissionMpoPlaneScaled(&next[1].Input))
        InterlockedIncrement(&Context->Mpo.ScaledLoneCommits);
      AdmissionMpoNoteForm(Context, ADMISSION_MPO_FORM_LONE);
    }
    return status;
  }
  slot1 = AdmissionScanoutMpoSlot1MayShow(Context);
  if (slot1) {
    /* Two planes -> one (or none): the removal swap keeps the LayerIndex 0
     * surface in slot 0 and takes slot 1 off at the same latch. */
    if (!next[0].Valid ||
        AdmissionMpoBuildRemoval(&next[0].Input, &records[0]) != 1u) {
      if (!DryRun)
        InterlockedIncrement(&Context->Mpo.S3BusyBuild); /* G1 invariant */
      return STATUS_DEVICE_BUSY;
    }
    /* WP-1: after the removal LayerIndex 0 is the full-screen scan-out, so
     * its allocation must have the committed mode's geometry (the rule the
     * one-plane path applies through AdmissionMpoEvaluatePrimary).  User
     * mode no longer refuses non-mode-size primaries at CreateResource.
     * STRETCH-1: a LayerIndex 0 plane that is not the full-screen primary
     * (scaled, cropped, positioned) is checked with the layer rules
     * instead; the removal record carries its rectangles either way. */
    pair[0] = next[0].Input;
    AdmissionMpoInput(Context, pair, 1u, ADMISSION_MPO_PATH_SET3, &input);
    if (AdmissionMpoEvaluateOne(&input, &result) != AdmissionMpoSupported) {
      /* HOLD-2: Set3 validates by dry run; its refusal is the Set3 view. */
      AdmissionMpoSet3TableRefusal(Context, result.Reason);
      return STATUS_DEVICE_BUSY;
    }
    status = DryRun ? STATUS_SUCCESS : AdmissionScanoutMpoCommit(Context, records, 1u, completion,
                                       next[0].PrimaryAddress);
    if (NT_SUCCESS(status) && !DryRun) {
      RtlCopyMemory(Context->MpoPlanes, next, sizeof(Context->MpoPlanes));
      InterlockedIncrement(&Context->Mpo.RemovalCommits);
      AdmissionMpoScratchReset(&Context->MpoScratch); /* CUR-1 v2: slot 1 off */
      AdmissionMpoNoteForm(Context, ADMISSION_MPO_FORM_ONE);
      if (result.OnePlaneRecord != 0u &&
          AdmissionMpoPlaneScaled(&next[0].Input))
        InterlockedIncrement(&Context->Mpo.ScaledOnePlaneCommits);
    }
    return status;
  }
  if (next[0].Enabled && listed[0]) {
    /* One plane, slot 1 not on screen: the full-screen primary takes
     * today's V2 PRESENT, same bytes, after the same rule one-plane Check3
     * answered with (A1.1).  STRETCH-1: any other plane (scaled, cropped,
     * positioned) goes as one layer record, the rule Check3 answered with
     * (AdmissionMpoEvaluateOne). */
    pair[0] = next[0].Input;
    AdmissionMpoInput(Context, pair, 1u, ADMISSION_MPO_PATH_SET3, &input);
    if (AdmissionMpoEvaluateOne(&input, &result) != AdmissionMpoSupported) {
      /* HOLD-2: Set3 validates by dry run; its refusal is the Set3 view. */
      AdmissionMpoSet3TableRefusal(Context, result.Reason);
      return STATUS_DEVICE_BUSY;
    }
    if (result.OnePlaneRecord != 0u) {
      if (AdmissionMpoBuildRemoval(&next[0].Input, &records[0]) != 1u) {
        if (!DryRun)
          InterlockedIncrement(&Context->Mpo.S3BusyBuild); /* G1 invariant */
        return STATUS_DEVICE_BUSY;
      }
      status = DryRun ? STATUS_SUCCESS : AdmissionScanoutMpoCommit(Context, records, 1u, completion,
                                         next[0].PrimaryAddress);
      if (NT_SUCCESS(status) && !DryRun) {
        RtlCopyMemory(Context->MpoPlanes, next, sizeof(Context->MpoPlanes));
        InterlockedIncrement(&Context->Mpo.OnePlaneRecordCommits);
        AdmissionMpoScratchReset(&Context->MpoScratch); /* CUR-1 v2: slot 1 off */
        AdmissionMpoNoteForm(Context, ADMISSION_MPO_FORM_ONE);
        if (AdmissionMpoPlaneScaled(&next[0].Input))
          InterlockedIncrement(&Context->Mpo.ScaledOnePlaneCommits);
      }
      return status;
    }
    RtlZeroMemory(&legacy, sizeof(legacy));
    legacy.VidPnSourceId = 0;
    legacy.PrimarySegment = next[0].PrimarySegment;
    legacy.PrimaryAddress.QuadPart = next[0].PrimaryAddress;
    legacy.hAllocation = next[0].hAllocation;
    legacy.Flags.FlipImmediate = next[0].FlipImmediate & 1u;
    legacy.Flags.FlipOnNextVSync = next[0].FlipOnNextVSync & 1u;
    if (DryRun)
      return STATUS_SUCCESS; /* the V2 path validates at its own call */
    AdmissionScanoutTagMpoFlipSet(Context, completion);
    status = AdmissionDdiSetVidPnSourceAddress(Context, &legacy);
    if (!NT_SUCCESS(status)) {
      AdmissionScanoutClearMpoFlip(Context);
      /* S3-B (H2): the V2 path's runtime statuses (mode change in flight,
       * address outside the view, panel error, lease state) are DEVICE_BUSY
       * through Set3; only a foreign handle stays a contract failure. */
      if (status != STATUS_DEVICE_BUSY && status != STATUS_INVALID_HANDLE) {
        InterlockedIncrement(&Context->Mpo.S3BusyLegacy);
        InterlockedExchange(&Context->Mpo.LastLegacyStatus, (LONG)status);
        status = STATUS_DEVICE_BUSY;
      }
      return status;
    }
    RtlCopyMemory(Context->MpoPlanes, next, sizeof(Context->MpoPlanes));
    InterlockedIncrement(&Context->Mpo.V2Presents);
    AdmissionMpoScratchReset(&Context->MpoScratch); /* CUR-1 v2: slot 1 off */
    AdmissionMpoNoteForm(Context, ADMISSION_MPO_FORM_ONE);
    return status;
  }
  /* Nothing to swap (planes switched off, or only LayerIndex 1 disabled
   * while it was never on screen): the scanning surface stays and the
   * listed PresentIds complete on the next vsync, as today's plane-off. */
  status = DryRun ? STATUS_SUCCESS : AdmissionScanoutMpoPlaneOffSet(Context, completion);
  if (NT_SUCCESS(status) && !DryRun) {
    RtlCopyMemory(Context->MpoPlanes, next, sizeof(Context->MpoPlanes));
    InterlockedIncrement(&Context->Mpo.PlaneOffCompletions);
  }
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionMpoSetVidPnSourceAddress3(
    ADMISSION_CONTEXT *Context,
    DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3 *Args) {
  ADMISSION_MPO_PLANE_STATE next[2];
  ADMISSION_MPO_COMPLETION completion;
  DXGK_MULTIPLANE_OVERLAY_PLANE3 *plane;
  const DXGK_PRIMARYCONTEXTDATA *primary;
  BOOLEAN listed[2] = {FALSE, FALSE};
  NTSTATUS status;
  UINT index;
  UINT layer;

  if (Args == NULL || Args->VidPnSourceId != 0 ||
      Args->PlaneCount > ADMISSION_MPO_MAX_PLANES ||
      (Args->PlaneCount != 0u && Args->ppPlanes == NULL)) {
    AdmissionMpoRefuseSet3(Context);
    return STATUS_INVALID_PARAMETER;
  }
  InterlockedIncrement(&Context->Mpo.Set3Calls);
  /* NP-1: publish the receipt set from the Set3 path too (rate-limited). */
  AdmissionQueueMpoPublish(Context);
  Args->OutputFlags.Value = 0u;
  RtlCopyMemory(next, Context->MpoPlanes, sizeof(next));
  RtlZeroMemory(&completion, sizeof(completion));
  for (index = 0u; index < Args->PlaneCount; ++index) {
    plane = Args->ppPlanes[index];
    if (plane == NULL || plane->LayerIndex > 1u || listed[plane->LayerIndex]) {
      AdmissionMpoRefuseSet3(Context);
      return STATUS_INVALID_PARAMETER;
    }
    layer = plane->LayerIndex;
    listed[layer] = TRUE;
    plane->OutputFlags.Value = 0u;
    AdmissionMpoCompletionAdd(&completion, layer, plane->PresentId);
    if (!plane->InputFlags.Enabled) {
      /* The plane's last surface stays recorded for a removal record. */
      next[layer].Enabled = 0u;
      continue;
    }
    if (plane->ContextCount == 0u || plane->ppContextData == NULL ||
        plane->ppContextData[0] == NULL) {
      AdmissionMpoRefuseSet3(Context);
      return STATUS_INVALID_PARAMETER;
    }
    primary = plane->ppContextData[0];
    next[layer].Valid = 1u;
    next[layer].Enabled = 1u;
    next[layer].PrimarySegment = primary->SegmentId;
    next[layer].PrimaryAddress = primary->SegmentAddress.QuadPart;
    next[layer].hAllocation = primary->hAllocation;
    next[layer].FlipImmediate = plane->InputFlags.FlipImmediate;
    next[layer].FlipOnNextVSync = plane->InputFlags.FlipOnNextVSync;
    AdmissionMpoPlaneInput(Context, layer, TRUE, primary->hAllocation,
                           &plane->PlaneAttributes, TRUE, primary->SegmentId,
                           primary->SegmentAddress.QuadPart,
                           &next[layer].Input);
    AdmissionMpoSet3Quality(Context, &next[layer].Input);
  }
  if (Args->PlaneCount == 0u) {
    /* dxgkrnl ForcePlanesOff (EXP955): every plane off. */
    next[0].Enabled = 0u;
    next[1].Enabled = 0u;
    AdmissionMpoCompletionAdd(&completion, 0u, 0ULL);
  }

  if (Args->PlaneCount == 0u) {
    /* ForcePlanesOff is not a swap (plane-off completion at the next vsync):
     * direct when nothing is pending, else re-offered. */
    if (!AdmissionScanoutMpoGateFree(Context) || Context->MpoHold.Valid)
      return AdmissionMpoBusyNote(Context, &completion, STATUS_DEVICE_BUSY);
    status = AdmissionMpoApply(Context, next, listed, &completion, FALSE);
    return AdmissionMpoBusyNote(Context, &completion, status);
  }
  /* HOLD-2: Set3 never submits.  The flip joins the pending set (HOLD-1
   * rules: one flip per layer, a same-layer second flip DEVICE_BUSY) when
   * the merged set passes the Set3 table; the commit is issued by the commit
   * DPC queued here when no swap is outstanding (it runs after dxgkrnl's
   * vsync DPC pass, so both layers of a pass travel in one swap), or by the
   * latch handler when one is. */
  {
    const ULONG mask = (listed[0] ? 1u : 0u) | (listed[1] ? 2u : 0u);
    ADMISSION_MPO_PLANE_STATE candidate[2];
    BOOLEAN merged[2];
    unsigned int layers[2] = {0u, 0u};
    unsigned long long ids[2] = {0ULL, 0ULL};
    unsigned int acceptable = 0u, queue = 0u, outcome, first;
    RtlZeroMemory(candidate, sizeof(candidate));
    RtlZeroMemory(merged, sizeof(merged));
    if (InterlockedCompareExchange(&Context->MpoHoldLock, 1, 0) != 0) {
      InterlockedIncrement(&Context->Mpo.HoldContended);
      AdmissionMpoTimingRecord(Context, ADMISSION_MPO_TIMING_SET3_BUSY, mask,
                               completion.Count != 0u ? completion.PresentId[0] : 0ULL);
      return AdmissionMpoBusyNote(Context, &completion, STATUS_DEVICE_BUSY);
    }
    if (AdmissionMpoHoldAdmit(&Context->MpoHold, mask)) {
      for (layer = 0u; layer < 2u; ++layer) {
        candidate[layer] = listed[layer] || !Context->MpoHold.Valid
                               ? next[layer]
                               : Context->MpoHoldNext[layer];
        merged[layer] = listed[layer] ||
                        (Context->MpoHold.Valid &&
                         (Context->MpoHold.ListedMask & (1u << layer)) != 0u);
      }
      acceptable = NT_SUCCESS(AdmissionMpoApply(Context, candidate, merged,
                                                &completion, TRUE)) ? 1u : 0u;
    }
    for (index = 0u; index < completion.Count && index < 2u; ++index) {
      layers[index] = (unsigned int)completion.LayerIndex[index];
      ids[index] = (unsigned long long)completion.PresentId[index];
    }
    first = Context->MpoHold.Valid == 0u ? 1u : 0u;
    outcome = AdmissionMpoHoldSet3(&Context->MpoHold, mask, acceptable,
                                   completion.Count, layers, ids,
                                   AdmissionScanoutMpoGateFree(Context) ? 1u : 0u,
                                   &queue);
    if (outcome == ADMISSION_MPO_SET3_BUSY) {
      InterlockedIncrement(&Context->Mpo.HoldBusy);
      status = STATUS_DEVICE_BUSY;
    } else {
      RtlCopyMemory(Context->MpoHoldNext, candidate, sizeof(candidate));
      if (first != 0u)
        Context->MpoHoldFirstQpc = KeQueryPerformanceCounter(NULL).QuadPart;
      InterlockedIncrement(&Context->Mpo.HoldMerges);
      InterlockedIncrement(outcome == ADMISSION_MPO_SET3_MERGED_FIRST
                               ? &Context->Mpo.H2Set3MergedFirst
                               : &Context->Mpo.H2Set3MergedSecond);
      {
        /* HOLD-3 receipt: the Set3 phase after the last vsync, per layer. */
        LARGE_INTEGER f;
        const LONGLONG now = KeQueryPerformanceCounter(&f).QuadPart;
        const LONGLONG last = InterlockedCompareExchange64(&Context->MpoLastVsyncQpc, 0, 0);
        if (last != 0 && now >= last && f.QuadPart > 0) {
          LONGLONG bin = ((now - last) * 1000LL / f.QuadPart) / 2;
          if (bin > 8) bin = 8;
          if (listed[0]) InterlockedIncrement(&Context->Mpo.H3PhaseL0[bin]);
          if (listed[1]) InterlockedIncrement(&Context->Mpo.H3PhaseL1[bin]);
        }
      }
      status = STATUS_SUCCESS;
    }
    InterlockedExchange(&Context->MpoHoldLock, 0);
    AdmissionMpoTimingRecord(Context,
        outcome == ADMISSION_MPO_SET3_BUSY ? ADMISSION_MPO_TIMING_SET3_BUSY
        : outcome == ADMISSION_MPO_SET3_MERGED_FIRST ? ADMISSION_MPO_TIMING_SET3_FIRST
                                                     : ADMISSION_MPO_TIMING_SET3_SECOND,
        mask, ids[0]);
    if (queue != 0u &&
        InterlockedCompareExchange(&Context->MpoCommitDpcReady, 0, 0) != 0)
      (void)KeInsertQueueDpc(&Context->MpoCommitDpc, NULL, NULL);
    return AdmissionMpoBusyNote(Context, &completion, status);
  }
}

/* HOLD-1: the latch handler (scan-out ISR, the gate just released) and the
 * vertical tick commit the held set exactly once; a commit that meets the
 * gate or a runtime obstacle again keeps the set for the next tick. */
_Use_decl_annotations_ VOID AdmissionMpoCommitPending(ADMISSION_CONTEXT *Context) {
  AdmissionMpoCommitPendingSource(Context, ADMISSION_MPO_TIMING_COMMIT_LATCH);
}

/* HOLD-2: the one commit path.  Source = ADMISSION_MPO_TIMING_COMMIT_DPC
 * (the commit DPC, through DxgkCbSynchronizeExecution), _LATCH (the latch
 * handler) or _TICK (the vertical tick, which also drives the retry of a
 * commit that met an obstacle with nothing outstanding).  Device IRQL,
 * serialised with the scan-out ISR. */
_Use_decl_annotations_ VOID AdmissionMpoCommitPendingSource(
    ADMISSION_CONTEXT *Context, ULONG Source) {
  ADMISSION_MPO_HOLD taken;
  ADMISSION_MPO_PLANE_STATE next[2];
  ADMISSION_MPO_COMPLETION completion;
  BOOLEAN listed[2];
  NTSTATUS status;
  ULONG i;
  if (Context == NULL || !Context->MpoHold.Valid ||
      InterlockedCompareExchange(&Context->MpoHoldLock, 1, 0) != 0)
    return;
  if (AdmissionMpoHoldTake(&Context->MpoHold, &taken)) {
    RtlCopyMemory(next, Context->MpoHoldNext, sizeof(next));
    listed[0] = (taken.ListedMask & 1u) != 0u ? TRUE : FALSE;
    listed[1] = (taken.ListedMask & 2u) != 0u ? TRUE : FALSE;
    RtlZeroMemory(&completion, sizeof(completion));
    for (i = 0u; i < taken.Count && i < 2u; ++i)
      AdmissionMpoCompletionAdd(&completion, taken.Layer[i], taken.PresentId[i]);
    status = AdmissionMpoApply(Context, next, listed, &completion, FALSE);
    if (status == STATUS_DEVICE_BUSY) {
      AdmissionMpoHoldRestore(&Context->MpoHold, &taken);
      InterlockedIncrement(&Context->Mpo.HoldCommitRetries);
    } else if (NT_SUCCESS(status)) {
      LONGLONG frequency = 0, now;
      LARGE_INTEGER f;
      now = KeQueryPerformanceCounter(&f).QuadPart;
      frequency = f.QuadPart;
      InterlockedIncrement(&Context->Mpo.HoldCommits);
      InterlockedIncrement(Source == ADMISSION_MPO_TIMING_COMMIT_DPC
                               ? &Context->Mpo.H2CommitsFromDpc
                           : Source == ADMISSION_MPO_TIMING_COMMIT_TICK
                               ? &Context->Mpo.H2CommitsFromTick
                           : Source == ADMISSION_MPO_TIMING_COMMIT_DEADLINE
                               ? &Context->Mpo.H3CommitsDeadline
                               : &Context->Mpo.H2CommitsFromLatch);
      if (Source == ADMISSION_MPO_TIMING_COMMIT_DPC)
        InterlockedIncrement(&Context->Mpo.H3CommitsLate);
      InterlockedExchange64(&Context->MpoLastCommitQpc, now);
      if (taken.ListedMask == 3u)
        InterlockedIncrement(&Context->Mpo.H2TwoLayerCommits);
      if (frequency > 0 && Context->MpoHoldFirstQpc != 0 &&
          now > Context->MpoHoldFirstQpc) {
        const LONGLONG us = (now - Context->MpoHoldFirstQpc) * 1000000LL / frequency;
        const LONG v = us > 0x7fffffffLL ? 0x7fffffff : (LONG)us;
        if (v > Context->Mpo.H2MaxSet3ToCommitUs)
          InterlockedExchange(&Context->Mpo.H2MaxSet3ToCommitUs, v);
      }
      Context->MpoHoldFirstQpc = 0;
      AdmissionMpoTimingRecord(Context, Source, taken.ListedMask,
                               taken.Count != 0u ? taken.PresentId[0] : 0ULL);
    } else {
      InterlockedIncrement(&Context->Mpo.HoldDropped);
    }
  }
  InterlockedExchange(&Context->MpoHoldLock, 0);
}

/* HOLD-2 receipt: the Set3 / commit / latch timing ring (receipt-only). */
_Use_decl_annotations_ VOID AdmissionMpoTimingRecord(ADMISSION_CONTEXT *Context,
                                                     ULONG Kind, ULONG Mask,
                                                     ULONGLONG Id) {
  ADMISSION_MPO_TIMING_ENTRY *e;
  ULONG slot;
  if (Context == NULL)
    return;
  slot = (ULONG)InterlockedIncrement(&Context->MpoTiming.Next) - 1u;
  e = &Context->MpoTiming.Entries[slot % ADMISSION_MPO_TIMING_ENTRIES];
  e->Qpc = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
  e->Id = (ULONG)Id;
  e->Kind = (UCHAR)Kind;
  e->Cpu = (UCHAR)KeGetCurrentProcessorNumber();
  e->Irql = (UCHAR)KeGetCurrentIrql();
  e->Mask = (UCHAR)Mask;
}

static BOOLEAN AdmissionMpoCommitSync(PVOID Opaque) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Opaque;
  /* A swap outstanding: the latch handler commits the set. */
  if (context != NULL && AdmissionScanoutMpoGateFree(context))
    AdmissionMpoCommitPendingSource(context, ADMISSION_MPO_TIMING_COMMIT_DPC);
  return TRUE;
}

static BOOLEAN AdmissionMpoCommitSyncDeadline(PVOID Opaque) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Opaque;
  if (context != NULL && AdmissionScanoutMpoGateFree(context))
    AdmissionMpoCommitPendingSource(context, ADMISSION_MPO_TIMING_COMMIT_DEADLINE);
  return TRUE;
}

/* HOLD-3: the frame deadline fired (DISPATCH_LEVEL). */
static EXT_CALLBACK AdmissionMpoDeadlineCallback;
static VOID AdmissionMpoDeadlineCallback(_In_ PEX_TIMER Timer, _In_opt_ PVOID Opaque) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Opaque;
  LARGE_INTEGER f;
  LONGLONG now, us;
  LONG err;
  BOOLEAN done = FALSE;
  UNREFERENCED_PARAMETER(Timer);
  if (context == NULL)
    return;
  now = KeQueryPerformanceCounter(&f).QuadPart;
  InterlockedExchange(&context->MpoDeadlineArmed, 0);
  if (f.QuadPart > 0) {
    us = (now - context->MpoDeadlineQpc) * 1000000LL / f.QuadPart;
    err = us > 0x7fffffffLL ? 0x7fffffff : us < -0x7fffffffLL ? -0x7fffffff : (LONG)us;
    if (InterlockedIncrement(&context->Mpo.H3DeadlineErrN) == 1 ||
        err < context->Mpo.H3DeadlineErrMinUs)
      InterlockedExchange(&context->Mpo.H3DeadlineErrMinUs, err);
    if (err > context->Mpo.H3DeadlineErrMaxUs)
      InterlockedExchange(&context->Mpo.H3DeadlineErrMaxUs, err);
    InterlockedExchangeAdd(&context->Mpo.H3DeadlineErrSumUs, err);
  }
  if (InterlockedCompareExchange(&context->MpoCommitDpcReady, 0, 0) == 0 ||
      !context->InterfaceValid ||
      context->Interface.DxgkCbSynchronizeExecution == NULL)
    return;
  (void)context->Interface.DxgkCbSynchronizeExecution(
      context->Interface.DeviceHandle, AdmissionMpoCommitSyncDeadline, context,
      0u, &done);
}

/* HOLD-3: a latch or a vertical tick (scan-out ISR).  A latch more than 1.5
 * frames after its commit is a missed vblank. */
_Use_decl_annotations_ VOID AdmissionMpoNoteVsync(ADMISSION_CONTEXT *Context,
                                                  BOOLEAN Latch) {
  const LONGLONG now = KeQueryPerformanceCounter(NULL).QuadPart;
  if (Context == NULL)
    return;
  if (Latch) {
    const LONGLONG commit = InterlockedExchange64(&Context->MpoLastCommitQpc, 0);
    if (commit != 0 && Context->MpoPeriodQpc > 0 &&
        now - commit > Context->MpoPeriodQpc + Context->MpoPeriodQpc / 2)
      InterlockedIncrement(&Context->Mpo.H3MissedVblanks);
  }
  InterlockedExchange64(&Context->MpoLastVsyncQpc, now);
}

static KDEFERRED_ROUTINE AdmissionMpoCommitDpcRoutine;
static VOID AdmissionMpoCommitDpcRoutine(_In_ PKDPC Dpc,
                                         _In_opt_ PVOID DeferredContext,
                                         _In_opt_ PVOID Argument1,
                                         _In_opt_ PVOID Argument2) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)DeferredContext;
  BOOLEAN done = FALSE;
  UNREFERENCED_PARAMETER(Dpc);
  UNREFERENCED_PARAMETER(Argument1);
  UNREFERENCED_PARAMETER(Argument2);
  if (context == NULL ||
      InterlockedCompareExchange(&context->MpoCommitDpcReady, 0, 0) == 0 ||
      !context->InterfaceValid ||
      context->Interface.DxgkCbSynchronizeExecution == NULL)
    return;
  /* HOLD-3: a swap outstanding -> the latch commits; before this frame's
   * deadline -> arm the deadline timer (once); after it -> commit now. */
  if (!AdmissionScanoutMpoGateFree(context) || !context->MpoHold.Valid)
    return;
  if (context->MpoDeadlineTimer != NULL) {
    LARGE_INTEGER f;
    long long deadline = 0, wait = 0;
    const LONGLONG now = KeQueryPerformanceCounter(&f).QuadPart;
    if (AdmissionMpoHoldDeadline(now, InterlockedCompareExchange64(
                                          &context->MpoLastVsyncQpc, 0, 0),
                                 context->MpoPeriodQpc, context->MpoLeadQpc,
                                 &deadline, &wait)) {
      if (InterlockedCompareExchange(&context->MpoDeadlineArmed, 1, 0) == 0 &&
          f.QuadPart > 0) {
        context->MpoDeadlineQpc = deadline;
        InterlockedIncrement(&context->Mpo.H3TimerArms);
        (void)ExSetTimer(context->MpoDeadlineTimer,
                         -(LONGLONG)((wait * 10000000LL) / f.QuadPart), 0LL, NULL);
      }
      return;
    }
  }
  (void)context->Interface.DxgkCbSynchronizeExecution(
      context->Interface.DeviceHandle, AdmissionMpoCommitSync, context, 0u,
      &done);
}

_Use_decl_annotations_ VOID AdmissionMpoCommitDpcStart(ADMISSION_CONTEXT *Context) {
  if (Context == NULL)
    return;
  KeInitializeDpc(&Context->MpoCommitDpc, AdmissionMpoCommitDpcRoutine, Context);
  Context->MpoHoldFirstQpc = 0;
  /* HOLD-3: the deadline timer; without it Set3 commits through the DPC at
   * once (HOLD-2 behaviour). */
  Context->MpoDeadlineArmed = 0;
  Context->MpoLastVsyncQpc = 0;
  Context->MpoLastCommitQpc = 0;
  if (Context->MpoDeadlineTimer == NULL)
    Context->MpoDeadlineTimer = ExAllocateTimer(AdmissionMpoDeadlineCallback,
                                                Context, EX_TIMER_HIGH_RESOLUTION);
  {
    LARGE_INTEGER f;
    (void)KeQueryPerformanceCounter(&f);
    /* Period from the panel rate (fixed 16.16 Hz, apple_agx_vsync.h). */
    Context->MpoPeriodQpc = (LONGLONG)(((ULONGLONG)f.QuadPart * 65536ULL) /
                                       APPLE_AGX_VSYNC_RATE_NUMERATOR);
    Context->MpoLeadQpc = (LONGLONG)(((ULONGLONG)f.QuadPart * Context->MpoLeadUs) /
                                     1000000ULL);
    Context->Mpo.H3LeadUs = (LONG)Context->MpoLeadUs;
  }
  RtlZeroMemory(&Context->MpoTiming, sizeof(Context->MpoTiming));
  Context->MpoTiming.Version = ADMISSION_MPO_TIMING_VERSION;
  {
    LARGE_INTEGER f;
    (void)KeQueryPerformanceCounter(&f);
    Context->MpoTiming.Frequency = f.QuadPart;
  }
  InterlockedExchange(&Context->MpoCommitDpcReady, 1);
}

/* NP-2 order: no new queueing, remove a queued one, drain a running one. */
_Use_decl_annotations_ VOID AdmissionMpoCommitDpcStop(ADMISSION_CONTEXT *Context) {
  if (Context == NULL ||
      InterlockedExchange(&Context->MpoCommitDpcReady, 0) == 0)
    return;
  /* HOLD-3: cancel the deadline timer and wait for a running callback. */
  if (Context->MpoDeadlineTimer != NULL) {
    (void)ExDeleteTimer(Context->MpoDeadlineTimer, TRUE, TRUE, NULL);
    Context->MpoDeadlineTimer = NULL;
  }
  InterlockedExchange(&Context->MpoDeadlineArmed, 0);
  (void)KeRemoveQueueDpc(&Context->MpoCommitDpc);
  KeFlushQueuedDpcs();
}
