#ifndef APPLE_AGX_MPO_CONTRACT_H
#define APPLE_AGX_MPO_CONTRACT_H

/* S2: pure multi-plane overlay rule table.  Decides whether a plane
 * configuration can be shown by the two display slots and, when it can,
 * which plane goes to which slot.  Inputs are neutral integers so the same
 * file compiles in the kernel (MSVC /W4 /WX) and in a host C test; no
 * Windows header is included.
 *
 * Wired (S2 step 4, contract version 2): DxgkDdiCheckMultiPlaneOverlaySupport3
 * answers with this table, and the two-plane form of
 * DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3 re-evaluates it with
 * the known placement before it sends records (mpo_layers.h) to the
 * scanout broker.  Both run only while the Wom1MpoTwoPlanes switch is
 * effective (scanout_windows.c AdmissionScanoutMpoEffective).
 *
 * Windows numbering: LayerIndex 0 is the TOP plane.  Display slot 0 is the
 * bottom slot, slot 1 the top slot.
 *
 * One plane (mirrors the one-plane rules of
 * AdmissionDdiSetVidPnSourceAddressWithMultiPlaneOverlay3 in display.c):
 * LayerIndex must be 0; an enabled or a disabled plane is accepted (a
 * disabled plane is the plane-off path).  No format, colour, rect, stride,
 * rotation or flag rule applies, because the handler applies none.  The
 * handler's VidPnSourceId == 0 and "enabled plane carries context data"
 * rules have no field in this neutral input and stay in the handler.
 *
 * Two planes: see ADMISSION_MPO_REASON.  Order: first the input rule
 * PanelGeometry (24, no failing plane); then, per plane in array order, the
 * per-plane rules 3..17, then (version 4, scaled planes only) 32..34, then
 * 18..21 and 25 (the failing plane is the first plane that violates any of
 * them, the reason its first violated rule); then the pair rules
 * SameSurface (22) and SurfaceOverlap (23), charged to array index 1.
 *
 * Version 4 (STRETCH-1): a plane is scaled when its source width or height
 * differs from its destination's.  Without Input->ScalingSupported a scaled
 * plane is refused with Scaling (10) exactly as before.  With it, the
 * scaled plane must be within ADMISSION_MPO_MAX_SCALE on each axis (32) and
 * carry a StretchQuality this hardware honours (33, 34); every other rule is
 * unchanged.  The LayerIndex 0 plane stays full panel (8), so at most the
 * LayerIndex 1 plane is scaled in a two-plane configuration.
 *
 * Version 2 adds the scanout broker's surface rules (ABI V3 refusals
 * 16/17/18/22/23 of m1n1 gpu/s2-broker-v3) as explicit inputs, so a
 * configuration the table supports with a known placement is one the broker
 * accepts (tests/mpo_broker_reference_test.c).  Check3 has no placement
 * (PlacementKnown 0): it can only require the allocation class that lives
 * in the scanout pool and the 64 KiB placement alignment CreateAllocation
 * requests; Set3 evaluates again with the actual offset. */

#define ADMISSION_MPO_CONTRACT_VERSION 5u
#define ADMISSION_MPO_MAX_PLANES 2u
#define ADMISSION_MPO_SLOT_COUNT 2u
#define ADMISSION_MPO_MIN_EXTENT 32u
#define ADMISSION_MPO_STRIDE_ALIGNMENT 64u
#define ADMISSION_MPO_NO_PLANE 0xffffffffu
/* Scanout broker geometry (J313 panel, 4-byte pixels, pool alignment). */
#define ADMISSION_MPO_SCANOUT_WIDTH 2560u
#define ADMISSION_MPO_SCANOUT_HEIGHT 1600u
#define ADMISSION_MPO_BYTES_PER_PIXEL 4u
#define ADMISSION_MPO_POOL_ALIGNMENT 0x10000ULL
/* Version 4 (STRETCH-1): the per-axis scale limit the scanout broker and the
 * DCP swap builder enforce (m1n1 gpu/s2-broker-v3 hv_agx_scanout_broker.h:61
 * HV_AGX_SCANOUT_V3_MAX_SCALE, hv_agx_scanout_broker.c:107-110 and :156-157,
 * dcp_iomfb_present.c:176-180): destination <= 2 x source and source <= 2 x
 * destination on each axis.  A project choice (the conservative intersection
 * of Asahi plane.c:92-95), not a measured DCP limit. */
#define ADMISSION_MPO_MAX_SCALE 2u
/* DXGK_MULTIPLANE_OVERLAY_STRETCH_QUALITY (d3dkmddi.h:368-372). */
#define ADMISSION_MPO_STRETCH_QUALITY_BILINEAR 1u
#define ADMISSION_MPO_STRETCH_QUALITY_HIGH 2u
/* IEEE-754 single bit patterns of the reported factors (the kernel writes
 * them as bits so no floating-point instruction is generated). */
#define ADMISSION_MPO_FLOAT_BITS_HALF 0x3f000000u
#define ADMISSION_MPO_FLOAT_BITS_ONE 0x3f800000u
#define ADMISSION_MPO_FLOAT_BITS_TWO 0x40000000u

/* Stable numeric values; receipts and tests depend on them.  Never
 * renumber; append new codes at the end. */
typedef enum _ADMISSION_MPO_REASON {
  AdmissionMpoSupported = 0,
  /* NULL input/output, or PlaneCount != 0 with a NULL plane array. */
  AdmissionMpoReasonArgument = 1,
  /* PlaneCount is 0 or greater than ADMISSION_MPO_MAX_PLANES. */
  AdmissionMpoReasonPlaneCount = 2,
  /* One plane: LayerIndex != 0.  Two planes: LayerIndex not 0 or 1, or
   * both planes carry the same LayerIndex. */
  AdmissionMpoReasonLayerIndex = 3,
  /* Two planes: a plane is not enabled. */
  AdmissionMpoReasonDisabled = 4,
  /* Two planes: format class is neither BGRA8 nor RGBA8. */
  AdmissionMpoReasonFormat = 5,
  /* Two planes: colour space class is not SDR sRGB. */
  AdmissionMpoReasonColorSpace = 6,
  /* Two planes: LayerIndex 0 (top) plane has BlendAlpha != 1. */
  AdmissionMpoReasonTopBlend = 7,
  /* Two planes: LayerIndex 0 (top) plane source or destination is not
   * exactly (0,0,PanelWidth,PanelHeight). */
  AdmissionMpoReasonTopNotFullPanel = 8,
  /* Two planes: LayerIndex 1 (bottom) plane has BlendAlpha != 0. */
  AdmissionMpoReasonBottomBlend = 9,
  /* Source size differs from destination size while scaling is not
   * offered (Input->ScalingSupported 0: the scanout broker did not
   * advertise CAP_SCALED_SOURCE).  Before version 4: any scaling. */
  AdmissionMpoReasonScaling = 10,
  /* Two planes: destination is inverted or not fully inside the panel. */
  AdmissionMpoReasonDestinationOutsidePanel = 11,
  /* Two planes: destination narrower or shorter than 32 pixels. */
  AdmissionMpoReasonDestinationTooSmall = 12,
  /* Two planes: source is inverted or not fully inside the surface. */
  AdmissionMpoReasonSourceOutsideSurface = 13,
  /* Two planes: stride is 0 or not a multiple of 64 bytes. */
  AdmissionMpoReasonStride = 14,
  /* Two planes: rotation is not identity. */
  AdmissionMpoReasonRotation = 15,
  /* Two planes: a flip, stereo or other unsupported flag is set. */
  AdmissionMpoReasonUnsupportedFlags = 16,
  /* Two planes: clip neither equals nor contains the destination. */
  AdmissionMpoReasonClip = 17,
  /* ---- version 2: the scanout broker's surface rules ---- */
  /* Two planes: the allocation cannot live in the scanout pool, or (known
   * placement) its bytes do not lie inside the pool. */
  AdmissionMpoReasonSurfaceNotInPool = 18,
  /* Two planes: known pool offset not a multiple of 64 KiB. */
  AdmissionMpoReasonSurfaceAlignment = 19,
  /* Two planes: SurfaceBytes != SurfaceStride * SurfaceHeight. */
  AdmissionMpoReasonSurfaceSize = 20,
  /* Two planes: surface width/height 0 or larger than the scanout panel. */
  AdmissionMpoReasonSurfaceLargerThanPanel = 21,
  /* Two planes: both planes name one surface (identity or pool offset). */
  AdmissionMpoReasonSameSurface = 22,
  /* Two planes: the two surfaces' pool byte ranges overlap. */
  AdmissionMpoReasonSurfaceOverlap = 23,
  /* Two planes: the source mode is not the scanout panel geometry, so
   * Windows destination coordinates are not panel coordinates. */
  AdmissionMpoReasonPanelGeometry = 24,
  /* Two planes: stride smaller than SurfaceWidth * 4 bytes. */
  AdmissionMpoReasonStrideBelowRow = 25,
  /* ---- version 3: one plane, AdmissionMpoEvaluatePrimary only ---- */
  /* The allocation is not a scanout-capable KMD allocation. */
  AdmissionMpoReasonPrimaryNotScanoutCapable = 26,
  /* The committed source mode or its pixel format is not scanned out. */
  AdmissionMpoReasonPrimarySourceMode = 27,
  /* Description invalid, or width/height/pitch differ from the committed
   * source mode. */
  AdmissionMpoReasonPrimaryGeometry = 28,
  /* Allocation bytes != committed stride * committed height. */
  AdmissionMpoReasonPrimarySize = 29,
  /* Not a 4-byte format the broker scans (BGRA8, or RGBA8 with the cap). */
  AdmissionMpoReasonPrimaryFormat = 30,
  /* Known placement that does not translate into the scanout pool. */
  AdmissionMpoReasonPrimaryPlacement = 31,
  /* ---- version 4 (STRETCH-1): scaled planes ---- */
  /* Scaled plane: on one axis the destination is more than
   * ADMISSION_MPO_MAX_SCALE x the source, or the source more than
   * ADMISSION_MPO_MAX_SCALE x the destination (broker refusal 21). */
  AdmissionMpoReasonScaleBeyondLimit = 32,
  /* Scaled plane: StretchQuality is neither BILINEAR (1) nor HIGH (2). */
  AdmissionMpoReasonStretchQuality = 33,
  /* Scaled plane: StretchQuality BILINEAR while the display scaler's filter
   * is not known to be bilinear or better (Input->ScalerFilter). */
  AdmissionMpoReasonStretchFilter = 34,
  /* One plane sent as a layer record (AdmissionMpoEvaluateOne): BlendAlpha
   * is not 0 or 1. */
  AdmissionMpoReasonOnePlaneBlend = 35,
  ADMISSION_MPO_REASON_COUNT
} ADMISSION_MPO_REASON;

typedef enum _ADMISSION_MPO_FORMAT_CLASS {
  AdmissionMpoFormatOther = 0,
  AdmissionMpoFormatBgra8 = 1,
  AdmissionMpoFormatRgba8 = 2,
} ADMISSION_MPO_FORMAT_CLASS;

typedef enum _ADMISSION_MPO_COLOR_SPACE_CLASS {
  AdmissionMpoColorSpaceOther = 0,
  AdmissionMpoColorSpaceSdrSrgb = 1,
} ADMISSION_MPO_COLOR_SPACE_CLASS;

/* Version 4: what is known about the filter the display scaler applies.
 * Unknown until measured on the panel (STRETCH-1 note, WHAT IS STILL
 * UNKNOWN); neither Asahi nor m1n1 sets or documents a DCP filter. */
typedef enum _ADMISSION_MPO_SCALER_FILTER {
  AdmissionMpoScalerFilterUnknown = 0,
  AdmissionMpoScalerFilterPoint = 1,
  AdmissionMpoScalerFilterBilinearOrBetter = 2,
} ADMISSION_MPO_SCALER_FILTER;
/* The value the kernel passes today. */
#define ADMISSION_MPO_SCALER_FILTER_CURRENT AdmissionMpoScalerFilterUnknown

typedef enum _ADMISSION_MPO_ROTATION_CLASS {
  AdmissionMpoRotationIdentity = 0,
  AdmissionMpoRotationOther = 1,
} ADMISSION_MPO_ROTATION_CLASS;

/* Edges in pixels, right/bottom exclusive (RECT semantics). */
typedef struct _ADMISSION_MPO_RECT {
  int Left;
  int Top;
  int Right;
  int Bottom;
} ADMISSION_MPO_RECT;

typedef struct _ADMISSION_MPO_PLANE_INPUT {
  unsigned int LayerIndex;
  unsigned int Enabled;          /* 0 or nonzero */
  unsigned int FormatClass;      /* ADMISSION_MPO_FORMAT_CLASS */
  unsigned int ColorSpaceClass;  /* ADMISSION_MPO_COLOR_SPACE_CLASS */
  unsigned int BlendAlpha;       /* 0 or 1; any other value is "not 0/1" */
  ADMISSION_MPO_RECT Source;
  ADMISSION_MPO_RECT Destination;
  ADMISSION_MPO_RECT Clip;
  unsigned int Rotation;         /* ADMISSION_MPO_ROTATION_CLASS */
  unsigned int HasUnsupportedFlags;
  unsigned int SurfaceWidth;
  unsigned int SurfaceHeight;
  unsigned int SurfaceStride;    /* bytes */
  /* ---- version 2 ---- */
  /* Nonzero: the allocation can only be resident in the scanout pool. */
  unsigned int InScanoutPool;
  /* Nonzero: PoolOffset is the plane's actual pool offset (Set3). */
  unsigned int PlacementKnown;
  unsigned long long PoolOffset;      /* bytes from the pool start */
  unsigned long long SurfaceBytes;    /* the allocation's linear size */
  /* Distinct per allocation (the KMD allocation handle); equal values
   * name one surface. */
  unsigned long long SurfaceIdentity;
  /* ---- version 3 (AdmissionMpoEvaluatePrimary) ---- */
  unsigned int ScanoutCapable;        /* KMD allocation ScanoutCapable */
  unsigned int DescriptionValid;      /* AdmissionAllocationDescriptionValid */
  unsigned int PlacementInPool;       /* known placement translated */
  unsigned long long AllocationBytes; /* the description's Size */
  /* ---- version 4 ---- */
  /* DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES3.StretchQuality, raw; examined only
   * when the plane is scaled. */
  unsigned int StretchQuality;
} ADMISSION_MPO_PLANE_INPUT;

typedef struct _ADMISSION_MPO_INPUT {
  unsigned int PanelWidth;
  unsigned int PanelHeight;
  unsigned int PlaneCount;
  const ADMISSION_MPO_PLANE_INPUT *Planes;
  /* ---- version 2 ---- */
  unsigned long long PoolBytes;       /* registered scanout pool size */
  /* ---- version 3: the committed source mode (one-plane V2 rule) ---- */
  unsigned int PanelStride;
  /* Nonzero: the committed mode is an offered scanout source mode and its
   * pixel format is scanned (display.c AdmissionDdiSetVidPnSourceAddress
   * gate before AdmissionScanoutQueuePresent). */
  unsigned int PanelModeSupported;
  /* ---- version 4 ---- */
  /* Nonzero: the scanout broker advertised CAP_SCALED_SOURCE (layer
   * records may carry a source size different from the destination). */
  unsigned int ScalingSupported;
  unsigned int ScalerFilter;          /* ADMISSION_MPO_SCALER_FILTER */
  /* ---- version 5 (STRETCH-1b) ---- */
  /* ADMISSION_MPO_PATH_*: which DDI evaluates.  The Check3-only rules
   * (ADMISSION_MPO_CHECK3_ONLY_REASONS) keep the Check3 answer truthful
   * and are not applied on the Set3 path: nothing Set3 realises depends on
   * them, and a Set3 refusal of a configuration Check3 accepted is a
   * defect (EXP2374: StretchQuality re-judged at Set3, ~450 refused Set3/s). */
  unsigned int Path;
} ADMISSION_MPO_INPUT;
#define ADMISSION_MPO_PATH_CHECK3 0u
#define ADMISSION_MPO_PATH_SET3 1u
/* Bit n set: reason n is applied on the Check3 path only. */
#define ADMISSION_MPO_CHECK3_ONLY_REASONS                                     \
  ((1ull << AdmissionMpoReasonStretchQuality) |                               \
   (1ull << AdmissionMpoReasonStretchFilter))

typedef struct _ADMISSION_MPO_SLOT {
  unsigned int PlaneIndex;       /* index into Planes[], or NO_PLANE */
  unsigned int Enabled;          /* copied from the plane */
  unsigned int Premultiplied;
  ADMISSION_MPO_RECT Source;
  ADMISSION_MPO_RECT Destination;
  /* ---- version 2: copied from the plane for the broker record ---- */
  unsigned int FormatClass;
  unsigned int SurfaceWidth;
  unsigned int SurfaceHeight;
  unsigned int SurfaceStride;
  unsigned long long PoolOffset;
  unsigned long long SurfaceBytes;
} ADMISSION_MPO_SLOT;

typedef struct _ADMISSION_MPO_RESULT {
  unsigned int Supported;        /* 1 iff Reason == AdmissionMpoSupported */
  unsigned int Reason;           /* ADMISSION_MPO_REASON */
  unsigned int FailingPlane;     /* array index, or NO_PLANE */
  unsigned int SlotCount;        /* 0 when not supported */
  ADMISSION_MPO_SLOT Slots[ADMISSION_MPO_SLOT_COUNT];
  /* ---- version 4 ---- */
  /* AdmissionMpoEvaluateOne only: 1 when the supported plane must be sent
   * as one layer record (COMMIT_LAYERS), 0 for the V2 PRESENT path. */
  unsigned int OnePlaneRecord;
} ADMISSION_MPO_RESULT;

/* Fills *Result (when non-NULL) and returns Result->Reason. */
unsigned int AdmissionMpoEvaluate(const ADMISSION_MPO_INPUT *Input,
                                  ADMISSION_MPO_RESULT *Result);

/* Version 3: the acceptance rule of today's one-plane V2 PRESENT path
 * (display.c AdmissionDdiSetVidPnSourceAddress and scanout_windows.c
 * AdmissionScanoutQueuePresent), as a pure function of the plane.  Exactly
 * one plane; LayerIndex 0; a disabled plane is the plane-off path and is
 * supported.  For an enabled plane, in numeric order: 26 not scanout
 * capable, 27 committed mode/format not scanned, 28 geometry differs from
 * the committed mode, 29 size != committed stride * height, 30 format,
 * 31 known placement outside the pool (PlacementKnown only; Check3 cannot
 * see placement).  Rectangles, rotation, blend, colour space and flags are
 * not examined because the V2 path ignores them (it scans the whole
 * surface).  While Wom1MpoTwoPlanes is effective, one-plane Check3 answers
 * with this function and Set3 checks it before the V2 path. */
unsigned int AdmissionMpoEvaluatePrimary(const ADMISSION_MPO_INPUT *Input,
                                         ADMISSION_MPO_RESULT *Result);

/* Version 4 (STRETCH-1): one plane while two-layer scan-out is effective.
 * A plane whose source and destination are both exactly the panel
 * (0,0,PanelWidth,PanelHeight) is the full-screen primary: the answer is
 * AdmissionMpoEvaluatePrimary's and OnePlaneRecord is 0 (V2 PRESENT, as
 * before).  Any other enabled plane (scaled, cropped or positioned) can be
 * shown only as one layer record, because the V2 PRESENT path scans the
 * whole surface onto the whole panel: in numeric order Format (5),
 * ColorSpace (6), OnePlaneBlend (35), then the layer rules every two-plane
 * plane meets (see AdmissionMpoEvaluate: 10..17, 32..34, 18..21, 25), and
 * OnePlaneRecord is 1 with Slots[0] filled (Premultiplied 0: nothing lies
 * beneath slot 0 except the black background).  A disabled plane is the
 * plane-off path (supported, OnePlaneRecord 0). */
unsigned int AdmissionMpoEvaluateOne(const ADMISSION_MPO_INPUT *Input,
                                     ADMISSION_MPO_RESULT *Result);

/* Version 4: the plane capabilities DxgkDdiGetMultiPlaneOverlayCaps and the
 * D3D11 UMD (DXGI group caps) report.  Pure so the derivation is tested. */
typedef struct _ADMISSION_MPO_CAPS {
  unsigned int MaxPlanes;
  unsigned int StretchRgb;       /* DXGK_MULTIPLANEOVERLAYCAPS.StretchRGB */
  unsigned int BilinearFilter;   /* .BilinearFilter */
  unsigned int HighFilter;       /* .HighFilter */
  unsigned int MaxStretchBits;   /* IEEE-754 single */
  unsigned int MaxShrinkBits;    /* IEEE-754 single */
} ADMISSION_MPO_CAPS;

/* Effective: two-layer scan-out effective (AdmissionScanoutMpoEffective).
 * Scaling: the broker advertised CAP_SCALED_SOURCE.  Without both, or for
 * another VidPnSource, the version-3 answer: one plane (two when effective
 * on source 0), no stretch bit, factors 1.0.  With both on source 0:
 * StretchRGB, MaxStretchFactor 2.0, MaxShrinkFactor 0.5 (the DXGI DDI's
 * definition: final size / original size), BilinearFilter only when the
 * filter is known to be bilinear or better, HighFilter never. */
void AdmissionMpoCapsFor(unsigned int Effective, unsigned int Scaling,
                         unsigned int ScalerFilter, unsigned int VidPnSourceId,
                         ADMISSION_MPO_CAPS *Caps);

/* PF-1 (EXP2355/2356): the no-DMA flip DxgkDdiPresent (no DMA buffer, no
 * source or destination allocation, no private data; SetVidPnSourceAddress
 * or the Set3 that follows performs the flip).  Accepts DXGK_PRESENTFLAGS
 * Flip (0x4), optionally with FlipWithNoWait (0x8); FlipWithMultiPlaneOverlay
 * (0x1000, "a flip operation occurs between an overlay plane and the primary
 * surface", Microsoft DXGK_PRESENTFLAGS) only while MpoReported is nonzero,
 * the same live state DRIVERCAPS reports MPO from.  Every other bit refused.
 * Returns 1 when accepted, else 0. */
unsigned int AdmissionMpoNoDmaFlipAccepted(unsigned int Flags,
                                           unsigned int MpoReported);

#endif
