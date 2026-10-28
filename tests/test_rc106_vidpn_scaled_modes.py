"""rc106: execute the production VidPN DDIs against a fake VidPN manager.

The KMD display DDIs (display.c) and their scanout wrappers
(scanout_windows.c) are extracted verbatim and linked with the real shared
fixed-panel/scanout code; only the dxgkrnl VidPN manager and the broker
registers are fakes.  The WDK build verifies the real ABI.
"""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KMD = ROOT / "drivers" / "apple-agx" / "render-admission"
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


def extract(source, name):
    match = re.search(
        r"^[^\n;{}]*\b" + re.escape(name) + r"\([^;{}]*?\)\s*\{.*?^\}",
        source, re.S | re.M)
    if match is None:
        raise AssertionError(f"{name} definition is missing")
    return match.group(0)


DISPLAY_FUNCTIONS = (
    "AdmissionFillPanelSignalInfo",
    "AdmissionDisplaySourceModeOffered",
    "AdmissionDisplayScalingCofunctional",
    "AdmissionFillScaledSignalInfo",
    "AdmissionDisplayTargetIsNative",
    "AdmissionDisplayTargetOffered",
    "AdmissionDisplayIdentityRequested",
    "AdmissionDisplayPathSupported",
    "AdmissionDisplayAddTargetMode",
    "AdmissionDdiRecommendMonitorModes",
    "AdmissionDisplayScalingSupport",
    "AdmissionDisplayFillScalingSupport",
    "AdmissionDisplayAddSourceMode",
    "AdmissionIsSupportedVidPnImpl",
    "AdmissionDdiEnumVidPnCofuncModality",
    "AdmissionDdiCommitVidPn",
    "AdmissionDdiUpdateActiveVidPnPresentPath",
)
SCANOUT_FUNCTIONS = (
    "AdmissionScanoutGet",
    "AdmissionScanoutFormatSupported",
    "AdmissionScanoutScaledModes",
    "AdmissionScanoutSourceModeAt",
    "AdmissionScanoutSourceModeSupported",
    "AdmissionScanoutScalingSupport",
    "AdmissionScanoutResolveScaling",
    "AdmissionScanoutCommit",
    "AdmissionScanoutSetVisible",
)

SHIM = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apple_agx_fixed_panel.h"

#define _Use_decl_annotations_
#define CONST const
#define VOID void
#define TRUE 1
#define FALSE 0
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define RtlZeroMemory(p, n) memset((p), 0, (n))
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#define ADMISSION_DISPLAY_SCALED_MODES 1
typedef int32_t NTSTATUS;
typedef unsigned int UINT;
typedef unsigned int ULONG;
typedef unsigned long long ULONGLONG;
typedef uintptr_t ULONG_PTR;
typedef size_t SIZE_T;
typedef unsigned char BOOLEAN;
typedef void *HANDLE;
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xC00000A3L)
#define STATUS_DEVICE_HARDWARE_ERROR ((NTSTATUS)0xC0000483L)
#define STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET ((NTSTATUS)0x401E034CL)
#define STATUS_GRAPHICS_MODE_NOT_PINNED ((NTSTATUS)0x401E0307L)
#define STATUS_GRAPHICS_VIDPN_TOPOLOGY_NOT_SUPPORTED ((NTSTATUS)0xC01E0301L)
#define STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE ((NTSTATUS)0xC01E0310L)
#define STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED ((NTSTATUS)0xC01E0306L)
#define STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY ((NTSTATUS)0xC01E0339L)
#define STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE ((NTSTATUS)0xC01E0304L)
#define STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET ((NTSTATUS)0xC01E0305L)
#define STATUS_GRAPHICS_GAMMA_RAMP_NOT_SUPPORTED ((NTSTATUS)0xC01E0348L)
#define DXGK_VIDPN_INTERFACE_VERSION_V1 1u

typedef enum { D3DDDIFMT_A8R8G8B8 = 21, D3DDDIFMT_A8B8G8R8 = 32 } D3DDDIFORMAT;
typedef enum { D3DKMDT_RMT_GRAPHICS = 1, D3DKMDT_RMT_TEXT = 2 } RMT;
typedef enum { D3DKMDT_PVAM_DIRECT = 1 } PVAM;
typedef enum { D3DKMDT_CB_SCRGB = 3 } CB;
typedef enum { D3DKMDT_VSS_OTHER = 255 } VSS;
typedef enum { D3DDDI_VSSLO_PROGRESSIVE = 1 } VSSLO;
typedef enum { D3DKMDT_MP_PREFERRED = 1, D3DKMDT_MP_NOTPREFERRED = 2 } MP;
typedef enum { D3DKMDT_MCO_UNINITIALIZED = 0, D3DKMDT_MCO_DRIVER = 5 } MCO;
#define D3DKMDT_CB_SRGB 2
#define STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET ((NTSTATUS)0xC01E0309L)
#define APPLE_AGX_SCANOUT_J313_WIDTH 2560u
#define APPLE_AGX_SCANOUT_J313_HEIGHT 1600u
typedef enum { D3DDDI_GAMMARAMP_DEFAULT = 1, D3DDDI_GAMMARAMP_RGB256x3x16 = 2 } GAMMA;
typedef enum {
  D3DKMDT_VPPS_UNINITIALIZED = 0, D3DKMDT_VPPS_IDENTITY = 1,
  D3DKMDT_VPPS_CENTERED = 2, D3DKMDT_VPPS_STRETCHED = 3,
  D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX = 4, D3DKMDT_VPPS_CUSTOM = 5,
  D3DKMDT_VPPS_RESERVED1 = 253, D3DKMDT_VPPS_UNPINNED = 254,
  D3DKMDT_VPPS_NOTSPECIFIED = 255
} D3DKMDT_VIDPN_PRESENT_PATH_SCALING;
typedef enum {
  D3DKMDT_VPPR_UNINITIALIZED = 0, D3DKMDT_VPPR_IDENTITY = 1,
  D3DKMDT_VPPR_UNPINNED = 254, D3DKMDT_VPPR_NOTSPECIFIED = 255
} D3DKMDT_VIDPN_PRESENT_PATH_ROTATION;
typedef enum {
  D3DKMDT_EPT_UNINITIALIZED, D3DKMDT_EPT_VIDPNSOURCE, D3DKMDT_EPT_VIDPNTARGET,
  D3DKMDT_EPT_SCALING, D3DKMDT_EPT_ROTATION, D3DKMDT_EPT_NOPIVOT
} D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE;

typedef struct { UINT cx, cy; } D3DKMDT_2DREGION;
typedef struct {
  D3DKMDT_2DREGION PrimSurfSize, VisibleRegionSize;
  ULONG Stride;
  D3DDDIFORMAT PixelFormat;
  CB ColorBasis;
  PVAM PixelValueAccessMode;
} D3DKMDT_GRAPHICS_RENDERING_FORMAT;
typedef UINT D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID;
typedef struct {
  D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID Id;
  RMT Type;
  union { D3DKMDT_GRAPHICS_RENDERING_FORMAT Graphics; } Format;
} D3DKMDT_VIDPN_SOURCE_MODE;
typedef struct { uint32_t Numerator, Denominator; } RATIONAL;
typedef struct {
  VSS VideoStandard; D3DKMDT_2DREGION TotalSize, ActiveSize;
  RATIONAL VSyncFreq, HSyncFreq; uint64_t PixelRate; VSSLO ScanLineOrdering;
} D3DKMDT_VIDEO_SIGNAL_INFO;
typedef struct {
  UINT Id; D3DKMDT_VIDEO_SIGNAL_INFO VideoSignalInfo; MP Preference;
} D3DKMDT_VIDPN_TARGET_MODE;
typedef struct { UINT FirstChannel, SecondChannel, ThirdChannel, FourthChannel; }
    D3DKMDT_COLOR_COEFF_DYNAMIC_RANGES;
typedef struct {
  UINT Id; D3DKMDT_VIDEO_SIGNAL_INFO VideoSignalInfo; int ColorBasis;
  D3DKMDT_COLOR_COEFF_DYNAMIC_RANGES ColorCoeffDynamicRanges; MCO Origin;
  MP Preference;
} D3DKMDT_MONITOR_SOURCE_MODE;
typedef struct FakeMonitorSet *D3DKMDT_HMONITORSOURCEMODESET;
typedef struct {
  NTSTATUS (*pfnCreateNewModeInfo)(D3DKMDT_HMONITORSOURCEMODESET,
                                   D3DKMDT_MONITOR_SOURCE_MODE **);
  NTSTATUS (*pfnAddMode)(D3DKMDT_HMONITORSOURCEMODESET,
                         const D3DKMDT_MONITOR_SOURCE_MODE *);
  NTSTATUS (*pfnReleaseModeInfo)(D3DKMDT_HMONITORSOURCEMODESET,
                                 const D3DKMDT_MONITOR_SOURCE_MODE *);
} DXGK_MONITORSOURCEMODESET_INTERFACE;
typedef struct {
  UINT VideoPresentTargetId;
  D3DKMDT_HMONITORSOURCEMODESET hMonitorSourceModeSet;
  const DXGK_MONITORSOURCEMODESET_INTERFACE *pMonitorSourceModeSetInterface;
} DXGKARG_RECOMMENDMONITORMODES;
typedef struct {
  UINT Identity : 1; UINT Centered : 1; UINT Stretched : 1;
  UINT AspectRatioCenteredMax : 1; UINT Custom : 1;
} D3DKMDT_VIDPN_PRESENT_PATH_SCALING_SUPPORT;
typedef struct {
  UINT Identity : 1; UINT Rotate90 : 1; UINT Rotate180 : 1; UINT Rotate270 : 1;
  UINT Offset0 : 1; UINT Offset90 : 1; UINT Offset180 : 1; UINT Offset270 : 1;
} D3DKMDT_VIDPN_PRESENT_PATH_ROTATION_SUPPORT;
typedef struct {
  D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling;
  D3DKMDT_VIDPN_PRESENT_PATH_SCALING_SUPPORT ScalingSupport;
  D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
  D3DKMDT_VIDPN_PRESENT_PATH_ROTATION_SUPPORT RotationSupport;
} D3DKMDT_VIDPN_PRESENT_PATH_TRANSFORMATION;
typedef struct { GAMMA Type; } D3DKMDT_GAMMA_RAMP;
typedef struct {
  UINT VidPnSourceId, VidPnTargetId;
  D3DKMDT_VIDPN_PRESENT_PATH_TRANSFORMATION ContentTransformation;
  D3DKMDT_GAMMA_RAMP GammaRamp;
} D3DKMDT_VIDPN_PRESENT_PATH;

typedef struct FakeVidPn *D3DKMDT_HVIDPN;
typedef struct FakeVidPn *D3DKMDT_HVIDPNTOPOLOGY;
typedef struct FakeSourceSet *D3DKMDT_HVIDPNSOURCEMODESET;
typedef struct FakeTargetSet *D3DKMDT_HVIDPNTARGETMODESET;

typedef struct {
  NTSTATUS (*pfnGetNumPathsFromSource)(D3DKMDT_HVIDPNTOPOLOGY, UINT, SIZE_T *);
  NTSTATUS (*pfnAcquireFirstPathInfo)(D3DKMDT_HVIDPNTOPOLOGY,
                                      const D3DKMDT_VIDPN_PRESENT_PATH **);
  NTSTATUS (*pfnAcquireNextPathInfo)(D3DKMDT_HVIDPNTOPOLOGY,
                                     const D3DKMDT_VIDPN_PRESENT_PATH *,
                                     const D3DKMDT_VIDPN_PRESENT_PATH **);
  NTSTATUS (*pfnUpdatePathSupportInfo)(D3DKMDT_HVIDPNTOPOLOGY,
                                       const D3DKMDT_VIDPN_PRESENT_PATH *);
  NTSTATUS (*pfnReleasePathInfo)(D3DKMDT_HVIDPNTOPOLOGY,
                                 const D3DKMDT_VIDPN_PRESENT_PATH *);
} DXGK_VIDPNTOPOLOGY_INTERFACE;
typedef struct {
  NTSTATUS (*pfnAcquirePinnedModeInfo)(D3DKMDT_HVIDPNSOURCEMODESET,
                                       const D3DKMDT_VIDPN_SOURCE_MODE **);
  NTSTATUS (*pfnCreateNewModeInfo)(D3DKMDT_HVIDPNSOURCEMODESET,
                                   D3DKMDT_VIDPN_SOURCE_MODE **);
  NTSTATUS (*pfnAddMode)(D3DKMDT_HVIDPNSOURCEMODESET,
                         const D3DKMDT_VIDPN_SOURCE_MODE *);
  NTSTATUS (*pfnReleaseModeInfo)(D3DKMDT_HVIDPNSOURCEMODESET,
                                 const D3DKMDT_VIDPN_SOURCE_MODE *);
} DXGK_VIDPNSOURCEMODESET_INTERFACE;
typedef struct {
  NTSTATUS (*pfnAcquirePinnedModeInfo)(D3DKMDT_HVIDPNTARGETMODESET,
                                       const D3DKMDT_VIDPN_TARGET_MODE **);
  NTSTATUS (*pfnCreateNewModeInfo)(D3DKMDT_HVIDPNTARGETMODESET,
                                   D3DKMDT_VIDPN_TARGET_MODE **);
  NTSTATUS (*pfnAddMode)(D3DKMDT_HVIDPNTARGETMODESET,
                         const D3DKMDT_VIDPN_TARGET_MODE *);
  NTSTATUS (*pfnReleaseModeInfo)(D3DKMDT_HVIDPNTARGETMODESET,
                                 const D3DKMDT_VIDPN_TARGET_MODE *);
} DXGK_VIDPNTARGETMODESET_INTERFACE;
typedef struct {
  NTSTATUS (*pfnGetTopology)(D3DKMDT_HVIDPN, D3DKMDT_HVIDPNTOPOLOGY *,
                             const DXGK_VIDPNTOPOLOGY_INTERFACE **);
  NTSTATUS (*pfnAcquireSourceModeSet)(D3DKMDT_HVIDPN, UINT,
                                      D3DKMDT_HVIDPNSOURCEMODESET *,
                                      const DXGK_VIDPNSOURCEMODESET_INTERFACE **);
  NTSTATUS (*pfnReleaseSourceModeSet)(D3DKMDT_HVIDPN, D3DKMDT_HVIDPNSOURCEMODESET);
  NTSTATUS (*pfnCreateNewSourceModeSet)(D3DKMDT_HVIDPN, UINT,
                                        D3DKMDT_HVIDPNSOURCEMODESET *,
                                        const DXGK_VIDPNSOURCEMODESET_INTERFACE **);
  NTSTATUS (*pfnAssignSourceModeSet)(D3DKMDT_HVIDPN, UINT, D3DKMDT_HVIDPNSOURCEMODESET);
  NTSTATUS (*pfnAcquireTargetModeSet)(D3DKMDT_HVIDPN, UINT,
                                      D3DKMDT_HVIDPNTARGETMODESET *,
                                      const DXGK_VIDPNTARGETMODESET_INTERFACE **);
  NTSTATUS (*pfnReleaseTargetModeSet)(D3DKMDT_HVIDPN, D3DKMDT_HVIDPNTARGETMODESET);
  NTSTATUS (*pfnCreateNewTargetModeSet)(D3DKMDT_HVIDPN, UINT,
                                        D3DKMDT_HVIDPNTARGETMODESET *,
                                        const DXGK_VIDPNTARGETMODESET_INTERFACE **);
  NTSTATUS (*pfnAssignTargetModeSet)(D3DKMDT_HVIDPN, UINT, D3DKMDT_HVIDPNTARGETMODESET);
} DXGK_VIDPN_INTERFACE;

typedef struct {
  D3DKMDT_HVIDPN hDesiredVidPn;
  BOOLEAN IsVidPnSupported;
} DXGKARG_ISSUPPORTEDVIDPN;
typedef struct {
  D3DKMDT_HVIDPN hConstrainingVidPn;
  D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE EnumPivotType;
  struct { UINT VidPnSourceId, VidPnTargetId; } EnumPivot;
} DXGKARG_ENUMVIDPNCOFUNCMODALITY;
typedef struct {
  D3DKMDT_HVIDPN hFunctionalVidPn;
  UINT AffectedVidPnSourceId;
  UINT MonitorConnectivityChecks;
  HANDLE hPrimaryAllocation;
  struct { UINT PathPowerTransition : 1; UINT PathPoweredOff : 1; } Flags;
} DXGKARG_COMMITVIDPN;
typedef struct {
  D3DKMDT_VIDPN_PRESENT_PATH VidPnPresentPathInfo;
} DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH;

typedef struct _ADMISSION_CONTEXT ADMISSION_CONTEXT;
typedef struct {
  ADMISSION_CONTEXT *Adapter;
  APPLE_AGX_FIXED_PANEL Panel;
} ADMISSION_SCANOUT_RUNTIME;
typedef int32_t LONG;
static LONG InterlockedIncrement(volatile LONG *p) { return ++*p; }
static LONG InterlockedExchange(volatile LONG *p, LONG v) { LONG o = *p; *p = v; return o; }
RECEIPT_STRUCT
struct _ADMISSION_CONTEXT {
  BOOLEAN Started, DisplayActive, SourceVisible;
  ULONG CommittedWidth, CommittedHeight, CommittedStride;
  D3DDDIFORMAT CommittedFormat;
  ULONG CommittedScaling;
  ADMISSION_DISPLAY_MODES_RECEIPT DisplayModes;
  void *ScanoutRuntime;
  struct {
    NTSTATUS (*DxgkCbQueryVidPnInterface)(D3DKMDT_HVIDPN, UINT,
                                          const DXGK_VIDPN_INTERFACE **);
  } Interface;
};

static void AdmissionDisplayRingRecord(ADMISSION_CONTEXT *Context, ULONG Kind,
    ULONG Flags, ULONG Detail, ULONGLONG Address, ULONGLONG Allocation) {
  (void)Context; (void)Kind; (void)Flags; (void)Detail; (void)Address;
  (void)Allocation;
}
BOOLEAN AdmissionScanoutFormatSupported(ADMISSION_CONTEXT *, D3DDDIFORMAT);
BOOLEAN AdmissionScanoutScaledModes(ADMISSION_CONTEXT *);
BOOLEAN AdmissionScanoutSourceModeAt(ADMISSION_CONTEXT *, ULONG, ULONG *,
                                     ULONG *, ULONG *);
BOOLEAN AdmissionScanoutSourceModeSupported(ADMISSION_CONTEXT *, ULONG, ULONG,
                                            ULONG);
ULONG AdmissionScanoutScalingSupport(ADMISSION_CONTEXT *, ULONG, ULONG);
BOOLEAN AdmissionScanoutResolveScaling(ADMISSION_CONTEXT *, ULONG, ULONG,
    D3DKMDT_VIDPN_PRESENT_PATH_SCALING, D3DKMDT_VIDPN_PRESENT_PATH_SCALING *);
NTSTATUS AdmissionScanoutCommit(ADMISSION_CONTEXT *, ULONG, ULONG, ULONG,
    D3DDDIFORMAT, D3DKMDT_VIDPN_PRESENT_PATH_SCALING);
NTSTATUS AdmissionScanoutSetVisible(ADMISSION_CONTEXT *, BOOLEAN);
'''

FAKE = r'''
/* ---- fake broker (registers only) ---- */
typedef struct { uint32_t caps, state; uint64_t req; } FAKE_BROKER;
static FAKE_BROKER broker;
static APPLE_AGX_SCANOUT_U64 now_ms(void *c) { (void)c; return 1; }
static APPLE_AGX_SCANOUT_BOOL rd32(void *c, APPLE_AGX_SCANOUT_U32 o,
                                   APPLE_AGX_SCANOUT_U32 *v) {
  (void)c;
  switch (o - APPLE_AGX_SCANOUT_MMIO_OFFSET) {
  case APPLE_AGX_SCANOUT_REG_MAGIC: *v = APPLE_AGX_SCANOUT_MAGIC; return 1;
  case APPLE_AGX_SCANOUT_REG_ABI_VERSION: *v = 2u; return 1;
  case APPLE_AGX_SCANOUT_REG_CAPABILITIES: *v = broker.caps; return 1;
  case APPLE_AGX_SCANOUT_REG_STATE: *v = broker.state; return 1;
  case APPLE_AGX_SCANOUT_REG_RESULT: *v = 0u; return 1;
  default: return 0;
  }
}
static APPLE_AGX_SCANOUT_BOOL rd64(void *c, APPLE_AGX_SCANOUT_U32 o,
                                   APPLE_AGX_SCANOUT_U64 *v) {
  (void)c;
  switch (o - APPLE_AGX_SCANOUT_MMIO_OFFSET) {
  case APPLE_AGX_SCANOUT_REG_RECEIPT_SEQUENCE:
  case APPLE_AGX_SCANOUT_REG_APPLIED_SEQUENCE: *v = broker.req; return 1;
  default: return 0;
  }
}
static APPLE_AGX_SCANOUT_BOOL wr32(void *c, APPLE_AGX_SCANOUT_U32 o,
                                   APPLE_AGX_SCANOUT_U32 v) {
  (void)c; (void)v;
  if (o - APPLE_AGX_SCANOUT_MMIO_OFFSET == APPLE_AGX_SCANOUT_REG_COMMAND)
    broker.state = APPLE_AGX_SCANOUT_STATE_READY;
  return 1;
}
static APPLE_AGX_SCANOUT_BOOL wr64(void *c, APPLE_AGX_SCANOUT_U32 o,
                                   APPLE_AGX_SCANOUT_U64 v) {
  (void)c;
  if (o - APPLE_AGX_SCANOUT_MMIO_OFFSET == APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE)
    broker.req = v;
  return 1;
}

/* ---- fake VidPN manager ---- */
#define MAX_MODES 64
struct FakeSourceSet {
  D3DKMDT_VIDPN_SOURCE_MODE modes[MAX_MODES];
  unsigned count; int pinned; unsigned next_id;
};
struct FakeTargetSet {
  D3DKMDT_VIDPN_TARGET_MODE modes[MAX_MODES]; unsigned count; int pinned;
};
struct FakeMonitorSet {
  D3DKMDT_MONITOR_SOURCE_MODE modes[MAX_MODES]; unsigned count, live;
};
struct FakeVidPn {
  D3DKMDT_VIDPN_PRESENT_PATH path;
  unsigned paths;
  struct FakeSourceSet *source; struct FakeTargetSet *target;
  int acquired; /* outstanding acquisitions: must return to 0 */
  int assigned_source, assigned_target, path_updates;
};
static struct FakeVidPn *vidpn_of_set; /* single-VidPN model */

static NTSTATUS t_num(D3DKMDT_HVIDPNTOPOLOGY t, UINT s, SIZE_T *n) {
  if (s != 0u) return STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY;
  *n = t->paths; return STATUS_SUCCESS;
}
static NTSTATUS t_first(D3DKMDT_HVIDPNTOPOLOGY t,
                        const D3DKMDT_VIDPN_PRESENT_PATH **p) {
  if (t->paths == 0u) { *p = NULL; return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET; }
  *p = &t->path; t->acquired++; return STATUS_SUCCESS;
}
static NTSTATUS t_next(D3DKMDT_HVIDPNTOPOLOGY t, const D3DKMDT_VIDPN_PRESENT_PATH *c,
                       const D3DKMDT_VIDPN_PRESENT_PATH **p) {
  (void)c; *p = NULL;
  if (t->paths > 1u) { *p = &t->path; t->acquired++; return STATUS_SUCCESS; }
  return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
}
static NTSTATUS t_update(D3DKMDT_HVIDPNTOPOLOGY t, const D3DKMDT_VIDPN_PRESENT_PATH *p) {
  t->path.ContentTransformation.ScalingSupport = p->ContentTransformation.ScalingSupport;
  t->path.ContentTransformation.RotationSupport = p->ContentTransformation.RotationSupport;
  t->path_updates++; return STATUS_SUCCESS;
}
static NTSTATUS t_release(D3DKMDT_HVIDPNTOPOLOGY t, const D3DKMDT_VIDPN_PRESENT_PATH *p) {
  (void)p; t->acquired--; return STATUS_SUCCESS;
}
static const DXGK_VIDPNTOPOLOGY_INTERFACE topology_if = {
  t_num, t_first, t_next, t_update, t_release };

static NTSTATUS s_pinned(D3DKMDT_HVIDPNSOURCEMODESET s, const D3DKMDT_VIDPN_SOURCE_MODE **m) {
  if (s->pinned < 0) { *m = NULL; return STATUS_GRAPHICS_MODE_NOT_PINNED; }
  *m = &s->modes[s->pinned]; vidpn_of_set->acquired++; return STATUS_SUCCESS;
}
static NTSTATUS s_new(D3DKMDT_HVIDPNSOURCEMODESET s, D3DKMDT_VIDPN_SOURCE_MODE **m) {
  D3DKMDT_VIDPN_SOURCE_MODE *mode = malloc(sizeof(*mode));
  memset(mode, 0xa5, sizeof(*mode));
  mode->Id = ++s->next_id; *m = mode; vidpn_of_set->acquired++;
  return STATUS_SUCCESS;
}
static NTSTATUS s_add(D3DKMDT_HVIDPNSOURCEMODESET s, const D3DKMDT_VIDPN_SOURCE_MODE *m) {
  for (unsigned i = 0; i < s->count; ++i)
    if (s->modes[i].Id == m->Id) return (NTSTATUS)0xC01E0326L; /* not unique */
  assert(s->count < MAX_MODES);
  s->modes[s->count++] = *m;
  free((void *)m); vidpn_of_set->acquired--;
  return STATUS_SUCCESS;
}
static NTSTATUS s_release(D3DKMDT_HVIDPNSOURCEMODESET s, const D3DKMDT_VIDPN_SOURCE_MODE *m) {
  if (m < &s->modes[0] || m >= &s->modes[MAX_MODES]) free((void *)m);
  vidpn_of_set->acquired--; return STATUS_SUCCESS;
}
static const DXGK_VIDPNSOURCEMODESET_INTERFACE source_if = {
  s_pinned, s_new, s_add, s_release };
static NTSTATUS g_pinned(D3DKMDT_HVIDPNTARGETMODESET s, const D3DKMDT_VIDPN_TARGET_MODE **m) {
  if (s->pinned < 0) { *m = NULL; return STATUS_GRAPHICS_MODE_NOT_PINNED; }
  *m = &s->modes[s->pinned]; vidpn_of_set->acquired++; return STATUS_SUCCESS;
}
static NTSTATUS g_new(D3DKMDT_HVIDPNTARGETMODESET s, D3DKMDT_VIDPN_TARGET_MODE **m) {
  D3DKMDT_VIDPN_TARGET_MODE *mode = calloc(1, sizeof(*mode));
  mode->Id = 100u + s->count; *m = mode; vidpn_of_set->acquired++;
  return STATUS_SUCCESS;
}
static NTSTATUS g_add(D3DKMDT_HVIDPNTARGETMODESET s, const D3DKMDT_VIDPN_TARGET_MODE *m) {
  s->modes[s->count++] = *m; free((void *)m); vidpn_of_set->acquired--;
  return STATUS_SUCCESS;
}
static NTSTATUS g_release(D3DKMDT_HVIDPNTARGETMODESET s, const D3DKMDT_VIDPN_TARGET_MODE *m) {
  if (m < &s->modes[0] || m >= &s->modes[MAX_MODES]) free((void *)m);
  vidpn_of_set->acquired--; return STATUS_SUCCESS;
}
static const DXGK_VIDPNTARGETMODESET_INTERFACE target_if = {
  g_pinned, g_new, g_add, g_release };

static NTSTATUS v_topology(D3DKMDT_HVIDPN v, D3DKMDT_HVIDPNTOPOLOGY *t,
                           const DXGK_VIDPNTOPOLOGY_INTERFACE **i) {
  *t = v; *i = &topology_if; return STATUS_SUCCESS;
}
static NTSTATUS v_acq_source(D3DKMDT_HVIDPN v, UINT id, D3DKMDT_HVIDPNSOURCEMODESET *s,
                             const DXGK_VIDPNSOURCEMODESET_INTERFACE **i) {
  assert(id == 0u); *s = v->source; *i = &source_if; v->acquired++;
  return STATUS_SUCCESS;
}
static NTSTATUS v_rel_source(D3DKMDT_HVIDPN v, D3DKMDT_HVIDPNSOURCEMODESET s) {
  if (s != v->source) free(s);
  v->acquired--; return STATUS_SUCCESS;
}
static NTSTATUS v_new_source(D3DKMDT_HVIDPN v, UINT id, D3DKMDT_HVIDPNSOURCEMODESET *s,
                             const DXGK_VIDPNSOURCEMODESET_INTERFACE **i) {
  struct FakeSourceSet *set = calloc(1, sizeof(*set));
  assert(id == 0u); set->pinned = -1; set->next_id = 1000u;
  *s = set; *i = &source_if; v->acquired++; return STATUS_SUCCESS;
}
static NTSTATUS v_assign_source(D3DKMDT_HVIDPN v, UINT id, D3DKMDT_HVIDPNSOURCEMODESET s) {
  assert(id == 0u);
  if (v->source != s) free(v->source);
  v->source = s; v->assigned_source++; v->acquired--; return STATUS_SUCCESS;
}
static NTSTATUS v_acq_target(D3DKMDT_HVIDPN v, UINT id, D3DKMDT_HVIDPNTARGETMODESET *s,
                             const DXGK_VIDPNTARGETMODESET_INTERFACE **i) {
  assert(id == 0u); *s = v->target; *i = &target_if; v->acquired++;
  return STATUS_SUCCESS;
}
static NTSTATUS v_rel_target(D3DKMDT_HVIDPN v, D3DKMDT_HVIDPNTARGETMODESET s) {
  if (s != v->target) free(s);
  v->acquired--; return STATUS_SUCCESS;
}
static NTSTATUS v_new_target(D3DKMDT_HVIDPN v, UINT id, D3DKMDT_HVIDPNTARGETMODESET *s,
                             const DXGK_VIDPNTARGETMODESET_INTERFACE **i) {
  struct FakeTargetSet *set = calloc(1, sizeof(*set));
  assert(id == 0u); set->pinned = -1; *s = set; *i = &target_if; v->acquired++;
  return STATUS_SUCCESS;
}
static NTSTATUS v_assign_target(D3DKMDT_HVIDPN v, UINT id, D3DKMDT_HVIDPNTARGETMODESET s) {
  assert(id == 0u);
  if (v->target != s) free(v->target);
  v->target = s; v->assigned_target++; v->acquired--; return STATUS_SUCCESS;
}
static const DXGK_VIDPN_INTERFACE vidpn_if = {
  v_topology, v_acq_source, v_rel_source, v_new_source, v_assign_source,
  v_acq_target, v_rel_target, v_new_target, v_assign_target };
static NTSTATUS query_vidpn(D3DKMDT_HVIDPN v, UINT version,
                            const DXGK_VIDPN_INTERFACE **i) {
  assert(version == DXGK_VIDPN_INTERFACE_VERSION_V1);
  vidpn_of_set = v; *i = &vidpn_if; return STATUS_SUCCESS;
}

static ADMISSION_SCANOUT_RUNTIME runtime;
static NTSTATUS m_new(D3DKMDT_HMONITORSOURCEMODESET s, D3DKMDT_MONITOR_SOURCE_MODE **m) {
  D3DKMDT_MONITOR_SOURCE_MODE *mode = calloc(1, sizeof(*mode));
  mode->Id = 500u + s->count; *m = mode; s->live++; return STATUS_SUCCESS;
}
static NTSTATUS m_add(D3DKMDT_HMONITORSOURCEMODESET s, const D3DKMDT_MONITOR_SOURCE_MODE *m) {
  unsigned i;
  for (i = 0; i < s->count; i++)
    if (s->modes[i].VideoSignalInfo.ActiveSize.cx == m->VideoSignalInfo.ActiveSize.cx &&
        s->modes[i].VideoSignalInfo.ActiveSize.cy == m->VideoSignalInfo.ActiveSize.cy)
      return STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET;
  s->modes[s->count++] = *m; free((void *)m); s->live--; return STATUS_SUCCESS;
}
static NTSTATUS m_release(D3DKMDT_HMONITORSOURCEMODESET s, const D3DKMDT_MONITOR_SOURCE_MODE *m) {
  free((void *)m); s->live--; return STATUS_SUCCESS;
}
static const DXGK_MONITORSOURCEMODESET_INTERFACE monitor_if = { m_new, m_add, m_release };

static ADMISSION_CONTEXT context;

static void start(uint32_t extra_caps) {
  APPLE_AGX_SCANOUT_IO io;
  memset(&broker, 0, sizeof(broker));
  memset(&runtime, 0, sizeof(runtime));
  memset(&context, 0, sizeof(context));
  broker.caps = APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES | extra_caps;
  memset(&io, 0, sizeof(io));
  io.NowMs = now_ms; io.Read32 = rd32; io.Read64 = rd64;
  io.Write32 = wr32; io.Write64 = wr64;
  assert(AppleAgxFixedPanelInitialize(&runtime.Panel, &io, 0x80000000ULL,
                                      0x3b800000ULL, 1ULL, 4u) ==
         AppleAgxFixedPanelOk);
  assert(AppleAgxFixedPanelStart(&runtime.Panel, 100ULL) ==
         AppleAgxFixedPanelOk);
  runtime.Adapter = &context;
  context.ScanoutRuntime = &runtime;
  context.Started = TRUE;
  context.Interface.DxgkCbQueryVidPnInterface = query_vidpn;
  /* lifecycle.c: the inherited native mode. */
  context.DisplayActive = TRUE;
  context.SourceVisible = TRUE;
  context.CommittedWidth = 2560u; context.CommittedHeight = 1600u;
  context.CommittedStride = 10240u; context.CommittedFormat = D3DDDIFMT_A8R8G8B8;
  context.CommittedScaling = D3DKMDT_VPPS_IDENTITY;
  assert(AdmissionScanoutCommit(&context, 2560u, 1600u, 10240u,
                                D3DDDIFMT_A8R8G8B8, D3DKMDT_VPPS_IDENTITY) ==
         STATUS_SUCCESS);
}

static struct FakeVidPn *vidpn(D3DKMDT_VIDPN_PRESENT_PATH_SCALING scaling) {
  struct FakeVidPn *v = calloc(1, sizeof(*v));
  v->paths = 1u;
  v->path.ContentTransformation.Scaling = scaling;
  v->path.ContentTransformation.Rotation = D3DKMDT_VPPR_IDENTITY;
  v->path.GammaRamp.Type = D3DDDI_GAMMARAMP_DEFAULT;
  v->source = calloc(1, sizeof(*v->source));
  v->source->pinned = -1; v->source->next_id = 1u;
  v->target = calloc(1, sizeof(*v->target));
  v->target->pinned = -1;
  return v;
}

static void pin_source(struct FakeVidPn *v, UINT w, UINT h, ULONG stride,
                       D3DDDIFORMAT format) {
  D3DKMDT_VIDPN_SOURCE_MODE *m = &v->source->modes[v->source->count];
  memset(m, 0, sizeof(*m));
  m->Id = 77u + v->source->count; m->Type = D3DKMDT_RMT_GRAPHICS;
  m->Format.Graphics.PrimSurfSize.cx = w; m->Format.Graphics.PrimSurfSize.cy = h;
  m->Format.Graphics.VisibleRegionSize = m->Format.Graphics.PrimSurfSize;
  m->Format.Graphics.Stride = stride; m->Format.Graphics.PixelFormat = format;
  m->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
  m->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;
  v->source->pinned = (int)v->source->count++;
}

static void pin_target(struct FakeVidPn *v, UINT w, UINT h) {
  D3DKMDT_VIDPN_TARGET_MODE *m = &v->target->modes[v->target->count];
  memset(m, 0, sizeof(*m));
  m->Id = 300u + v->target->count;
  m->VideoSignalInfo.ActiveSize.cx = w; m->VideoSignalInfo.ActiveSize.cy = h;
  m->Preference = D3DKMDT_MP_PREFERRED;
  v->target->pinned = (int)v->target->count++;
}

static NTSTATUS recommend_monitor_modes(struct FakeMonitorSet *set) {
  DXGKARG_RECOMMENDMONITORMODES args;
  memset(&args, 0, sizeof(args));
  args.hMonitorSourceModeSet = set; args.pMonitorSourceModeSetInterface = &monitor_if;
  return AdmissionDdiRecommendMonitorModes(&context, &args);
}

static void done(struct FakeVidPn *v) {
  assert(v->acquired == 0);
  free(v->source); free(v->target); free(v);
}

static NTSTATUS enum_cofunc(struct FakeVidPn *v) {
  DXGKARG_ENUMVIDPNCOFUNCMODALITY args;
  memset(&args, 0, sizeof(args));
  args.hConstrainingVidPn = v; args.EnumPivotType = D3DKMDT_EPT_NOPIVOT;
  return AdmissionDdiEnumVidPnCofuncModality(&context, &args);
}
static BOOLEAN supported(struct FakeVidPn *v) {
  DXGKARG_ISSUPPORTEDVIDPN args;
  args.hDesiredVidPn = v; args.IsVidPnSupported = 0xa5;
  assert(AdmissionIsSupportedVidPnImpl(&context, &args) == STATUS_SUCCESS);
  return args.IsVidPnSupported;
}
static NTSTATUS commit(struct FakeVidPn *v) {
  DXGKARG_COMMITVIDPN args;
  memset(&args, 0, sizeof(args));
  args.hFunctionalVidPn = v; args.AffectedVidPnSourceId = 0u;
  return AdmissionDdiCommitVidPn(&context, &args);
}
static D3DKMDT_VIDPN_PRESENT_PATH_SCALING_SUPPORT support_of(struct FakeVidPn *v) {
  return v->path.ContentTransformation.ScalingSupport;
}

/* Without APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE nothing may change: one
 * native mode per format, identity only, and a smaller pin is unsupported
 * and uncommittable (m1n1 0d3a75b3 would reject its PRESENT). */
static void test_fixed_broker_offers_only_native(void) {
  struct FakeVidPn *v;
  D3DKMDT_VIDPN_PRESENT_PATH_SCALING_SUPPORT s;
  start(0u);
  v = vidpn(D3DKMDT_VPPS_UNPINNED);
  assert(enum_cofunc(v) == STATUS_SUCCESS);
  assert(v->assigned_source == 1 && v->source->count == 1u);
  assert(v->source->modes[0].Format.Graphics.PrimSurfSize.cx == 2560u);
  assert(v->source->modes[0].Format.Graphics.Stride == 10240u);
  assert(v->source->modes[0].Format.Graphics.PixelFormat == D3DDDIFMT_A8R8G8B8);
  assert(v->target->count == 1u &&
         v->target->modes[0].VideoSignalInfo.ActiveSize.cx == 2560u);
  {
    /* rc110: without the cap the monitor set is the native signal alone. */
    struct FakeMonitorSet monitor; memset(&monitor, 0, sizeof(monitor));
    assert(recommend_monitor_modes(&monitor) == STATUS_SUCCESS);
    assert(monitor.count == 1u && monitor.live == 0u);
    assert(monitor.modes[0].VideoSignalInfo.ActiveSize.cx == 2560u);
    assert(monitor.modes[0].Preference == D3DKMDT_MP_PREFERRED);
    assert(recommend_monitor_modes(&monitor) == STATUS_SUCCESS);
    assert(monitor.count == 1u && monitor.live == 0u);
    assert(context.DisplayModes.LastMonitorModesAdded == 0);
  }
  s = support_of(v);
  assert(s.Identity && !s.Centered && !s.Stretched && !s.AspectRatioCenteredMax);
  done(v);

  v = vidpn(D3DKMDT_VPPS_UNPINNED);
  pin_source(v, 1920u, 1080u, 7680u, D3DDDIFMT_A8R8G8B8);
  assert(!supported(v));
  assert(commit(v) == STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE);
  assert(context.CommittedWidth == 2560u);
  done(v);

  v = vidpn(D3DKMDT_VPPS_IDENTITY);
  pin_source(v, 2560u, 1600u, 10240u, D3DDDIFMT_A8R8G8B8);
  assert(supported(v));
  assert(commit(v) == STATUS_SUCCESS);
  assert(context.CommittedWidth == 2560u &&
         context.CommittedScaling == D3DKMDT_VPPS_IDENTITY);
  done(v);

  /* A persisted non-identity pin keeps the native mode, as before rc106. */
  v = vidpn(D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX);
  assert(supported(v));
  assert(enum_cofunc(v) == STATUS_SUCCESS);
  assert(v->source->count == 1u &&
         v->source->modes[0].Format.Graphics.PrimSurfSize.cx == 2560u);
  done(v);
}

/* With the scaled broker: the full laptop list with unique Ids (EXP1884),
 * union scaling support while unpinned, and the WDK pinned rules. */
static void test_scaled_broker_offers_modes_and_scaling(void) {
  struct FakeVidPn *v;
  D3DKMDT_VIDPN_PRESENT_PATH_SCALING_SUPPORT s;
  unsigned i, j, native = 0u, fhd = 0u;
  start(APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE | APPLE_AGX_SCANOUT_CAP_RGBA8888);
  v = vidpn(D3DKMDT_VPPS_UNPINNED);
  assert(enum_cofunc(v) == STATUS_SUCCESS);
  assert(v->source->count ==
         2u * AppleAgxFixedPanelSourceModeCount(APPLE_AGX_SCANOUT_TRUE));
  for (i = 0; i < v->source->count; ++i) {
    const D3DKMDT_GRAPHICS_RENDERING_FORMAT *g = &v->source->modes[i].Format.Graphics;
    assert(v->source->modes[i].Type == D3DKMDT_RMT_GRAPHICS);
    assert(g->Stride == g->PrimSurfSize.cx * 4u);
    assert(g->VisibleRegionSize.cx == g->PrimSurfSize.cx);
    assert(g->PixelValueAccessMode == D3DKMDT_PVAM_DIRECT);
    for (j = 0; j < i; ++j)
      assert(v->source->modes[j].Id != v->source->modes[i].Id);
    native += g->PrimSurfSize.cx == 2560u && g->PrimSurfSize.cy == 1600u;
    fhd += g->PrimSurfSize.cx == 1920u && g->PrimSurfSize.cy == 1080u;
  }
  assert(native == 2u && fhd == 2u);
  assert(v->source->modes[0].Format.Graphics.PrimSurfSize.cx == 2560u);
  s = support_of(v);
  assert(s.Identity && s.Centered && s.Stretched && s.AspectRatioCenteredMax &&
         !s.Custom);
  /* rc110: one target (and monitor) mode of every offered size; the native
   * signal is preferred and first, the others not preferred. */
  assert(v->target->count == 11u);
  assert(v->target->modes[0].VideoSignalInfo.ActiveSize.cx == 2560u &&
         v->target->modes[0].Preference == D3DKMDT_MP_PREFERRED);
  assert(v->target->modes[3].VideoSignalInfo.ActiveSize.cx == 1920u &&
         v->target->modes[3].VideoSignalInfo.ActiveSize.cy == 1080u &&
         v->target->modes[3].VideoSignalInfo.TotalSize.cx == 2002u &&
         v->target->modes[3].Preference == D3DKMDT_MP_NOTPREFERRED);
  assert(v->target->modes[3].VideoSignalInfo.VSyncFreq.Numerator == 266630000u &&
         v->target->modes[3].VideoSignalInfo.VSyncFreq.Denominator == 2642u * 1682u);
  assert(v->target->modes[3].VideoSignalInfo.HSyncFreq.Numerator == 69719u);
  assert(v->target->modes[3].VideoSignalInfo.PixelRate == 69719ull * 2002ull);
  assert(context.DisplayModes.LastEnumTargetModes == 11);
  {
    struct FakeMonitorSet monitor; memset(&monitor, 0, sizeof(monitor));
    assert(recommend_monitor_modes(&monitor) == STATUS_SUCCESS);
    assert(monitor.count == 11u && monitor.live == 0u);
    assert(monitor.modes[0].Preference == D3DKMDT_MP_PREFERRED);
    assert(monitor.modes[3].VideoSignalInfo.ActiveSize.cx == 1920u &&
           monitor.modes[3].Preference == D3DKMDT_MP_NOTPREFERRED &&
           monitor.modes[3].Origin == D3DKMDT_MCO_DRIVER);
    assert(context.DisplayModes.LastMonitorModesAdded == 11);
    /* A second recommendation finds every mode already present. */
    assert(recommend_monitor_modes(&monitor) == STATUS_SUCCESS);
    assert(monitor.count == 11u && monitor.live == 0u);
  }
  done(v);

  /* Pinned identity: only the native size is cofunctional. */
  v = vidpn(D3DKMDT_VPPS_IDENTITY);
  assert(enum_cofunc(v) == STATUS_SUCCESS);
  assert(v->source->count == 2u);
  for (i = 0; i < v->source->count; ++i)
    assert(v->source->modes[i].Format.Graphics.PrimSurfSize.cx == 2560u);
  done(v);

  /* Pinned stretched: every size; the native one commits as identity
   * (CommitVidPn accepted it before rc106, so the set must keep it). */
  v = vidpn(D3DKMDT_VPPS_STRETCHED);
  assert(enum_cofunc(v) == STATUS_SUCCESS);
  assert(v->source->count ==
         2u * AppleAgxFixedPanelSourceModeCount(APPLE_AGX_SCANOUT_TRUE));
  done(v);

  /* Pinned custom: no cofunctional source mode. */
  v = vidpn(D3DKMDT_VPPS_CUSTOM);
  assert(enum_cofunc(v) == STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED);
  assert(!supported(v));
  done(v);

  /* Pinned 1920x1080 source: scaled transforms only. */
  v = vidpn(D3DKMDT_VPPS_UNPINNED);
  pin_source(v, 1920u, 1080u, 7680u, D3DDDIFMT_A8R8G8B8);
  assert(enum_cofunc(v) == STATUS_SUCCESS);
  assert(v->assigned_source == 0 && v->source->count == 1u);
  s = support_of(v);
  assert(!s.Identity && s.Centered && s.Stretched && s.AspectRatioCenteredMax);
  done(v);

  /* Pinned native source: identity only. */
  v = vidpn(D3DKMDT_VPPS_UNPINNED);
  pin_source(v, 2560u, 1600u, 10240u, D3DDDIFMT_A8B8G8R8);
  assert(enum_cofunc(v) == STATUS_SUCCESS);
  s = support_of(v);
  assert(s.Identity && !s.Centered && !s.Stretched && !s.AspectRatioCenteredMax);
  done(v);
}

static void test_scaled_commit_and_pinned_validation(void) {
  struct FakeVidPn *v;
  DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH update;
  start(APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE);

  /* IsSupported: identity on a smaller source, an unoffered size or a
   * padded stride are refused; aspect-centered on FHD is accepted. */
  v = vidpn(D3DKMDT_VPPS_IDENTITY);
  pin_source(v, 1920u, 1080u, 7680u, D3DDDIFMT_A8R8G8B8);
  assert(!supported(v));
  assert(commit(v) == STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED);
  assert(context.CommittedWidth == 2560u);
  done(v);
  v = vidpn(D3DKMDT_VPPS_UNPINNED);
  pin_source(v, 1366u, 768u, 5464u, D3DDDIFMT_A8R8G8B8);
  assert(!supported(v));
  done(v);
  v = vidpn(D3DKMDT_VPPS_UNPINNED);
  pin_source(v, 1920u, 1080u, 8192u, D3DDDIFMT_A8R8G8B8);
  assert(!supported(v));
  done(v);
  /* RGBA without the RGBA capability. */
  v = vidpn(D3DKMDT_VPPS_UNPINNED);
  pin_source(v, 1920u, 1080u, 7680u, D3DDDIFMT_A8B8G8R8);
  assert(!supported(v));
  done(v);

  /* Commit FHD, scaling not specified: the driver picks aspect-centered. */
  v = vidpn(D3DKMDT_VPPS_NOTSPECIFIED);
  pin_source(v, 1920u, 1080u, 7680u, D3DDDIFMT_A8R8G8B8);
  assert(supported(v));
  assert(commit(v) == STATUS_SUCCESS);
  assert(context.DisplayActive);
  assert(context.CommittedWidth == 1920u && context.CommittedHeight == 1080u &&
         context.CommittedStride == 7680u &&
         context.CommittedScaling == D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX);
  assert(runtime.Panel.Scanout.PresentWidth == 1920u &&
         runtime.Panel.Scanout.PresentSurfaceBytes == 7680ull * 1080u);
  assert(runtime.Panel.Scanout.PresentDestOrigin == (80u << 16u));
  assert(runtime.Panel.Scanout.PresentDestSize == (2560u | (1440u << 16u)));
  done(v);

  /* The active path keeps its committed transform. */
  memset(&update, 0, sizeof(update));
  update.VidPnPresentPathInfo.GammaRamp.Type = D3DDDI_GAMMARAMP_DEFAULT;
  update.VidPnPresentPathInfo.ContentTransformation.Rotation = D3DKMDT_VPPR_IDENTITY;
  update.VidPnPresentPathInfo.ContentTransformation.Scaling =
      D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX;
  assert(AdmissionDdiUpdateActiveVidPnPresentPath(&context, &update) ==
         STATUS_SUCCESS);
  update.VidPnPresentPathInfo.ContentTransformation.Scaling = D3DKMDT_VPPS_NOTSPECIFIED;
  assert(AdmissionDdiUpdateActiveVidPnPresentPath(&context, &update) ==
         STATUS_SUCCESS);
  update.VidPnPresentPathInfo.ContentTransformation.Scaling = D3DKMDT_VPPS_STRETCHED;
  assert(AdmissionDdiUpdateActiveVidPnPresentPath(&context, &update) ==
         STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED);
  update.VidPnPresentPathInfo.ContentTransformation.Scaling = D3DKMDT_VPPS_IDENTITY;
  assert(AdmissionDdiUpdateActiveVidPnPresentPath(&context, &update) ==
         STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED);

  /* Centered 1280x720. */
  v = vidpn(D3DKMDT_VPPS_CENTERED);
  pin_source(v, 1280u, 720u, 5120u, D3DDDIFMT_A8R8G8B8);
  assert(commit(v) == STATUS_SUCCESS);
  assert(context.CommittedScaling == D3DKMDT_VPPS_CENTERED);
  assert(runtime.Panel.Scanout.PresentDestOrigin == (640u | (440u << 16u)));
  done(v);

  /* Back to native; a persisted aspect-centered pin still commits as
   * identity, exactly as before rc106. */
  v = vidpn(D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX);
  pin_source(v, 2560u, 1600u, 10240u, D3DDDIFMT_A8R8G8B8);
  assert(supported(v));
  assert(commit(v) == STATUS_SUCCESS);
  assert(context.CommittedWidth == 2560u &&
         context.CommittedScaling == D3DKMDT_VPPS_IDENTITY);
  assert(runtime.Panel.Scanout.PresentSurfaceBytes ==
         APPLE_AGX_SCANOUT_J313_SURFACE_SIZE);
  assert(runtime.Panel.Scanout.PresentDestSize == (2560u | (1600u << 16u)));
  done(v);
}

/* rc110: a target pinned at a scaled size (the mode Windows chose from the
 * monitor set) takes the source of its size with identity scaling; the
 * panel fits it aspect-ratio-centered. */
static void test_scaled_target_takes_the_source_of_its_size(void) {
  struct FakeVidPn *v;
  D3DKMDT_VIDPN_PRESENT_PATH_SCALING_SUPPORT s;

  start(APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE);
  /* Unpinned source: only the target's size, both formats. */
  v = vidpn(D3DKMDT_VPPS_UNPINNED);
  pin_target(v, 1920u, 1080u);
  assert(enum_cofunc(v) == STATUS_SUCCESS);
  assert(v->source->count == 1u);
  assert(v->source->modes[0].Format.Graphics.PrimSurfSize.cx == 1920u &&
         v->source->modes[0].Format.Graphics.PrimSurfSize.cy == 1080u &&
         v->source->modes[0].Format.Graphics.Stride == 7680u);
  s = support_of(v);
  assert(s.Identity && !s.Centered && !s.Stretched && !s.AspectRatioCenteredMax);
  assert(v->target->count == 1u && v->assigned_target == 0);
  assert(supported(v));
  done(v);

  /* Pinned source of the target's size: identity commit, ACMax panel fit. */
  v = vidpn(D3DKMDT_VPPS_IDENTITY);
  pin_target(v, 1920u, 1080u);
  pin_source(v, 1920u, 1080u, 7680u, D3DDDIFMT_A8R8G8B8);
  assert(enum_cofunc(v) == STATUS_SUCCESS);
  assert(supported(v));
  assert(commit(v) == STATUS_SUCCESS);
  assert(context.CommittedWidth == 1920u && context.CommittedHeight == 1080u &&
         context.CommittedScaling == D3DKMDT_VPPS_IDENTITY);
  assert(runtime.Panel.Scanout.PresentDestOrigin == (0u | (80u << 16)));
  assert(runtime.Panel.Scanout.PresentDestSize == (2560u | (1440u << 16)));
  assert(context.DisplayModes.LastCommitTargetWidth == 1920 &&
         context.DisplayModes.LastCommitTargetHeight == 1080);
  done(v);

  /* Not specified resolves to identity as well. */
  v = vidpn(D3DKMDT_VPPS_NOTSPECIFIED);
  pin_target(v, 1280u, 720u);
  pin_source(v, 1280u, 720u, 5120u, D3DDDIFMT_A8R8G8B8);
  assert(supported(v));
  assert(commit(v) == STATUS_SUCCESS);
  assert(context.CommittedWidth == 1280u && context.CommittedScaling == D3DKMDT_VPPS_IDENTITY);
  assert(runtime.Panel.Scanout.PresentDestOrigin == (0u | (80u << 16)));
  done(v);

  /* A source of another size, or a non-identity transform, is refused. */
  v = vidpn(D3DKMDT_VPPS_IDENTITY);
  pin_target(v, 1920u, 1080u);
  pin_source(v, 1280u, 720u, 5120u, D3DDDIFMT_A8R8G8B8);
  assert(!supported(v));
  assert(commit(v) == STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED);
  assert(context.CommittedWidth == 1280u);
  done(v);
  v = vidpn(D3DKMDT_VPPS_STRETCHED);
  pin_target(v, 1920u, 1080u);
  pin_source(v, 1920u, 1080u, 7680u, D3DDDIFMT_A8R8G8B8);
  assert(!supported(v));
  assert(commit(v) == STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED);
  done(v);
  v = vidpn(D3DKMDT_VPPS_UNPINNED);
  pin_target(v, 1920u, 1080u);
  pin_source(v, 2560u, 1600u, 10240u, D3DDDIFMT_A8R8G8B8);
  assert(enum_cofunc(v) == STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED);
  done(v);
  /* A target that is not an offered size. */
  v = vidpn(D3DKMDT_VPPS_UNPINNED);
  pin_target(v, 1366u, 768u);
  assert(enum_cofunc(v) == STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED);
  assert(!supported(v));
  done(v);

  /* The native target keeps the rc106 source-scaling rules. */
  v = vidpn(D3DKMDT_VPPS_NOTSPECIFIED);
  pin_target(v, 2560u, 1600u);
  pin_source(v, 1920u, 1080u, 7680u, D3DDDIFMT_A8R8G8B8);
  assert(supported(v));
  assert(commit(v) == STATUS_SUCCESS);
  assert(context.CommittedScaling == D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX);
  assert(runtime.Panel.Scanout.PresentDestOrigin == (0u | (80u << 16)));
  done(v);

  /* Without the cap a scaled target is never offered nor accepted. */
  start(0u);
  v = vidpn(D3DKMDT_VPPS_UNPINNED);
  pin_target(v, 1920u, 1080u);
  assert(enum_cofunc(v) == STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED);
  assert(!supported(v));
  done(v);
}

int main(void) {
  test_fixed_broker_offers_only_native();
  test_scaled_target_takes_the_source_of_its_size();
  test_scaled_broker_offers_modes_and_scaling();
  test_scaled_commit_and_pinned_validation();
  return 0;
}
'''


class VidPnScaledModeTests(unittest.TestCase):
    def test_production_vidpn_ddis_against_fake_manager(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        GravityMark -Fullscreen 1 fails because ChangeDisplaySettingsExW
        finds only 2560x1600.  This runs the real EnumVidPnCofuncModality,
        IsSupportedVidPn, CommitVidPn and UpdateActiveVidPnPresentPath: with
        a scaling broker they must offer the laptop source modes with the
        D3DKMDT scaling rules (identity only at native size) and commit a
        smaller source with its destination rectangle; without it they must
        keep the single native mode, so a broker that cannot scale is never
        asked to.  Unbalanced acquire/release of VidPN objects also fails.
        """
        display = (KMD / "src" / "display.c").read_text()
        scanout = (KMD / "src" / "scanout_windows.c").read_text()
        body = "\n".join(extract(scanout, name) for name in SCANOUT_FUNCTIONS)
        body += "\n" + "\n".join(
            extract(display, name) for name in DISPLAY_FUNCTIONS)
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / "vidpn.c"
            header = (KMD / "include" / "render_admission.h").read_text()
            receipt = re.search(
                r"typedef struct _ADMISSION_DISPLAY_MODES_RECEIPT \{.*?\} "
                r"ADMISSION_DISPLAY_MODES_RECEIPT;", header, re.S).group(0)
            program.write_text(
                SHIM.replace("RECEIPT_STRUCT", receipt) + body + FAKE)
            binary = Path(tmp) / "vidpn"
            subprocess.run(
                [os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                 "-Werror", "-Wno-unused-function",
                 "-ftrivial-auto-var-init=pattern",
                 "-fsanitize=address,undefined",
                 "-I", str(SHARED / "include"), str(program),
                 str(SHARED / "src" / "apple_agx_fixed_panel.c"),
                 str(SHARED / "src" / "apple_agx_scanout.c"),
                 "-o", str(binary)],
                check=True)
            subprocess.run([str(binary)], check=True)


    def test_primary_and_flip_validators_follow_the_committed_mode(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        A committed 1920x1080 mode is useless if any later gate still
        demands a 2560x1600 primary: the shared-primary allocation, the
        FlipOnVSyncMmIo present, SetVidPnSourceAddress and the scanout
        queue must all compare against the committed geometry instead.
        """
        allocation = (KMD / "src" / "allocation_windows.c").read_text()
        callbacks = (KMD / "src" / "callbacks.c").read_text()
        display = (KMD / "src" / "display.c").read_text()
        scanout = (KMD / "src" / "scanout_windows.c").read_text()
        primary = allocation[allocation.index(
            "D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE) {"):]
        primary = primary[:primary.index("} else if")]
        self.assertIn("AdmissionScanoutSourceModeSupported(", primary)
        self.assertNotIn("APPLE_AGX_SCANOUT_J313_WIDTH", primary)
        flip = callbacks[callbacks.index("FlipOnVSyncMmIo flip: no DMA buffer"):]
        flip = flip[:flip.index("return STATUS_SUCCESS;")]
        self.assertNotIn("2560u", flip)
        self.assertIn("adapter->CommittedWidth", flip)
        address = extract(display, "AdmissionDdiSetVidPnSourceAddress")
        self.assertNotIn("== 2560", address)
        self.assertIn("AdmissionScanoutSourceModeSupported(context", address)
        queue = extract(scanout, "AdmissionScanoutQueuePresent")
        self.assertNotIn("APPLE_AGX_SCANOUT_J313", queue)
        self.assertIn("description->Width != Context->CommittedWidth", queue)
        # EXP2055: 1920x1080 (0x7e9000 bytes) is not a 64 KiB multiple; the
        # segment translation must get the aligned span or every flip fails.
        translate = queue[queue.index("AppleAgxLocalSegmentAddressToGpuVa("):]
        translate = translate[:translate.index("&surface_offset")]
        self.assertIn("APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL", translate)


if __name__ == "__main__":
    unittest.main()
