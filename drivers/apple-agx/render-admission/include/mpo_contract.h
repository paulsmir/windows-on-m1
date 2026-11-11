#ifndef APPLE_AGX_MPO_CONTRACT_H
#define APPLE_AGX_MPO_CONTRACT_H

/* S2: pure multi-plane overlay rule table.  Decides whether a plane
 * configuration can be shown by the two display slots and, when it can,
 * which plane goes to which slot.  Inputs are neutral integers so the same
 * file compiles in the kernel (MSVC /W4 /WX) and in a host C test; no
 * Windows header is included.
 *
 * NOT WIRED: nothing in the driver calls this table yet.  It does not
 * advertise MPO, change MaxOverlayPlanes, register a caps DDI or change the
 * answers of AdmissionDdiSetVidPnSourceAddressWithMultiPlaneOverlay3.
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
 * Two planes: see ADMISSION_MPO_REASON; each rule is checked in that
 * numeric order.  The failing plane is the first plane in array order that
 * violates any rule; the reason is that plane's first violated rule. */

#define ADMISSION_MPO_CONTRACT_VERSION 1u
#define ADMISSION_MPO_MAX_PLANES 2u
#define ADMISSION_MPO_SLOT_COUNT 2u
#define ADMISSION_MPO_MIN_EXTENT 32u
#define ADMISSION_MPO_STRIDE_ALIGNMENT 64u
#define ADMISSION_MPO_NO_PLANE 0xffffffffu

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
  /* Two planes: source size differs from destination size (scaling). */
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
} ADMISSION_MPO_PLANE_INPUT;

typedef struct _ADMISSION_MPO_INPUT {
  unsigned int PanelWidth;
  unsigned int PanelHeight;
  unsigned int PlaneCount;
  const ADMISSION_MPO_PLANE_INPUT *Planes;
} ADMISSION_MPO_INPUT;

typedef struct _ADMISSION_MPO_SLOT {
  unsigned int PlaneIndex;       /* index into Planes[], or NO_PLANE */
  unsigned int Enabled;          /* copied from the plane */
  unsigned int Premultiplied;
  ADMISSION_MPO_RECT Source;
  ADMISSION_MPO_RECT Destination;
} ADMISSION_MPO_SLOT;

typedef struct _ADMISSION_MPO_RESULT {
  unsigned int Supported;        /* 1 iff Reason == AdmissionMpoSupported */
  unsigned int Reason;           /* ADMISSION_MPO_REASON */
  unsigned int FailingPlane;     /* array index, or NO_PLANE */
  unsigned int SlotCount;        /* 0 when not supported */
  ADMISSION_MPO_SLOT Slots[ADMISSION_MPO_SLOT_COUNT];
} ADMISSION_MPO_RESULT;

/* Fills *Result (when non-NULL) and returns Result->Reason. */
unsigned int AdmissionMpoEvaluate(const ADMISSION_MPO_INPUT *Input,
                                  ADMISSION_MPO_RESULT *Result);

#endif
