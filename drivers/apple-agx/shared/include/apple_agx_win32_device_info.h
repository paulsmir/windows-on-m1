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
#define AGX_WIN32_DEVICE_FEATURES_KNOWN AGX_WIN32_DEVICE_FEATURE_SOFT_FAULTS

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
