#ifndef APPLE_AGX_WIN32_DEVICE_INFO_H
#define APPLE_AGX_WIN32_DEVICE_INFO_H

#include "apple_agx_win32_abi.h"

#define AGX_WIN32_DEVICE_INFO_MAGIC 0x49445741u /* AWDI */
/* Version 2 (EXP1317): the SGX core topology (apple_agx_core_topology.h).
 * Version 3 (rc82): Features. */
#define AGX_WIN32_DEVICE_INFO_VERSION 3u
#define AGX_WIN32_BUFFER_CLASS_COUNT 3u

typedef enum _AGX_WIN32_GPU_VARIANT {
  AgxWin32GpuG13G = 1u,
} AGX_WIN32_GPU_VARIANT;

typedef enum _AGX_WIN32_BUFFER_CLASS {
  AgxWin32BufferClassGeneral = 1u,
  AgxWin32BufferClassShader = 2u,
  AgxWin32BufferClassEncoder = 3u,
} AGX_WIN32_BUFFER_CLASS;

typedef enum _AGX_WIN32_BUFFER_FLAGS {
  AppleAgxWin32BufferCpuRead = 0x1u,
  AppleAgxWin32BufferCpuWrite = 0x2u,
  AppleAgxWin32BufferGpuRead = 0x4u,
  AppleAgxWin32BufferGpuWrite = 0x8u,
  AppleAgxWin32BufferShareable = 0x10u,
  /* T7h: General GPU-only backing; incompatible with CPU/shareable access. */
  AppleAgxWin32BufferGpuOnly = 0x20u,
} AGX_WIN32_BUFFER_FLAGS;

/* rc82: AGX_WIN32_DEVICE_INFO.Features.
 * SOFT_FAULTS: the firmware runs with Asahi's soft-fault mode (RegionC
 * fault_control 0xb, Asahi file.rs:333-335 sets DRM_ASAHI_FEATURE_SOFT_FAULTS
 * for exactly that value): shader loads of unmapped memory return zero and
 * shader stores to it are dropped (asahi_drm.h:204-215; texture sampling
 * and the other units still fault).  The KMD sets it only with the device
 * value Wom1SoftFaults = 1 and the published RegionC word 0xb; the UMD then
 * lets the compiler speculate shader memory loads. */
#define AGX_WIN32_DEVICE_FEATURE_SOFT_FAULTS 0x1u
/* OV2 (investigation/analysis/OV1-render-compute-overlap.md): a compute
 * envelope may name no render-subqueue wait (VdmBarrier =
 * APPLE_AGX_G4_BARRIER_NONE, Asahi DRM_ASAHI_BARRIER_NONE) when the D3D12
 * UMD proved it independent of the render job submitted just before it; the
 * KMD then waives the depth-two lead Barrier's wait on that render's 3D end.
 * Set only with REG_DWORD Wom1Ov2NoHazardLead = 1 and Q2c
 * (Wom1ComputeOwnQueue, compute on its own Asahi Queue); the KMD parser
 * refuses NONE without it. */
#define AGX_WIN32_DEVICE_FEATURE_INDEPENDENT_SUBQUEUE 0x2u
/* OV5 (investigation/analysis/OV1-render-compute-overlap.md): a render
 * packet without a compute prelude command may carry render-header
 * CdmBarrier = APPLE_AGX_G4_BARRIER_NONE when the D3D12 UMD proved its
 * vertex/TA work independent of the compute job submitted just before it;
 * the KMD then drops the TA-ring compute-stamp Barrier and puts it at the
 * head of the render's 3D ring entry (the 3D still waits). Published only
 * behind its own KMD switch; the parser refuses the flag without it. */
#define AGX_WIN32_DEVICE_FEATURE_RENDER_TA_INDEPENDENT 0x4u
#define AGX_WIN32_DEVICE_FEATURES_KNOWN \
  (AGX_WIN32_DEVICE_FEATURE_SOFT_FAULTS | AGX_WIN32_DEVICE_FEATURE_INDEPENDENT_SUBQUEUE | \
   AGX_WIN32_DEVICE_FEATURE_RENDER_TA_INDEPENDENT)

typedef struct _AGX_WIN32_BUFFER_CLASS_INFO {
  APPLE_AGX_U32 ClassId;
  APPLE_AGX_U32 MinimumAlignment;
  APPLE_AGX_U64 MaximumBytes;
  APPLE_AGX_U32 Flags;
} AGX_WIN32_BUFFER_CLASS_INFO;

typedef struct _AGX_WIN32_DEVICE_INFO {
  APPLE_AGX_U32 Magic, Version, Bytes, BootGeneration;
  APPLE_AGX_U32 GpuGeneration, GpuVariant, PageBytes, ClassCount;
  AGX_WIN32_BUFFER_CLASS_INFO Classes[AGX_WIN32_BUFFER_CLASS_COUNT];
  /* Cores per cluster and the present-core mask of the one G13G cluster as
   * the SGX reports them, or both 0 while the KMD has not read them. */
  APPLE_AGX_U32 CoresPerCluster, CoreMask;
  /* AGX_WIN32_DEVICE_FEATURE_* (version 3); Reserved is 0. */
  APPLE_AGX_U32 Features, Reserved;
} AGX_WIN32_DEVICE_INFO;

#endif
