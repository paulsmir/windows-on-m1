#ifndef APPLE_AGX_G13_COMPUTE_WORK_H
#define APPLE_AGX_G13_COMPUTE_WORK_H

#include "apple_agx_backend_runtime.h"

/* Source-derived from pinned Asahi fw/compute.rs for G13/V13_5.  The firmware
 * ABI uses packed pointer/U64 fields inside otherwise 32-bit-aligned structs. */
#define APPLE_AGX_G13_COMPUTE_WORK_BYTES 0x31cu
#define APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES 0x7fa0u
#define APPLE_AGX_G13_COMPUTE_TAG 3u

typedef struct _APPLE_AGX_G13_COMPUTE_WORK_INPUT {
  APPLE_AGX_BACKEND_U64 Counter;
  APPLE_AGX_BACKEND_U32 VmSlot;
  APPLE_AGX_BACKEND_U64 NotifierGpuAddress;
  APPLE_AGX_BACKEND_U64 PreemptionGpuAddress;
  APPLE_AGX_BACKEND_U64 CdmStreamBase;
  APPLE_AGX_BACKEND_U64 CdmStreamEnd;
  APPLE_AGX_BACKEND_U64 UscExecutionBase;
  APPLE_AGX_BACKEND_U32 HelperProgram;
  APPLE_AGX_BACKEND_U64 HelperArgument;
  APPLE_AGX_BACKEND_U32 HelperConfig;
  APPLE_AGX_BACKEND_U64 MicrosequenceGpuAddress;
  APPLE_AGX_BACKEND_U32 MicrosequenceBytes;
  APPLE_AGX_BACKEND_U64 StampGpuAddress;
  APPLE_AGX_BACKEND_U64 FirmwareStampGpuAddress;
  APPLE_AGX_BACKEND_U32 StampValue;
  APPLE_AGX_BACKEND_U32 StampSlot;
  APPLE_AGX_BACKEND_U32 EventControlIndex;
  APPLE_AGX_BACKEND_U32 EventSequence;
  APPLE_AGX_BACKEND_U32 ClientSequence;
} APPLE_AGX_G13_COMPUTE_WORK_INPUT;

APPLE_AGX_BACKEND_BOOL AppleAgxG13ComputeWorkBuild(
    const APPLE_AGX_G13_COMPUTE_WORK_INPUT *Input,
    unsigned char Work[APPLE_AGX_G13_COMPUTE_WORK_BYTES]);

#endif
