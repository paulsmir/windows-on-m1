#ifndef APPLE_AGX_G13_COMPUTE_WORK_H
#define APPLE_AGX_G13_COMPUTE_WORK_H

#include "apple_agx_backend_runtime.h"

/* Source-derived from pinned Asahi fw/compute.rs for G13/V13_5.  The firmware
 * ABI uses packed pointer/U64 fields inside otherwise 32-bit-aligned structs. */
#define APPLE_AGX_G13_COMPUTE_WORK_BYTES 0x31cu
#define APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES 0x7fa0u
#define APPLE_AGX_G13_COMPUTE_TAG 3u
#define APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_BYTES 0x1f0u
/* GPU timestamps (Asahi queue/compute.rs): Timestamp(start) and
 * Timestamp(end) around WaitForIdle(Compute); microseq::Timestamp::ver
 * <G13,V13_5> is 0x3c packed bytes. The block the firmware writes is the
 * job's own start/end (timestamp_pointers) then the requested start/end
 * (user_timestamp_pointers), 8 bytes each. */
#define APPLE_AGX_G13_TIMESTAMP_OP_BYTES 0x3cu
#define APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_TIMESTAMP_BYTES \
  (APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_BYTES+2u*APPLE_AGX_G13_TIMESTAMP_OP_BYTES)
#define APPLE_AGX_G13_TIMESTAMP_BLOCK_BYTES 0x20u

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
  /* 0, or the 8-byte aligned timestamp block (APPLE_AGX_G13_TIMESTAMP_
   * BLOCK_BYTES) the work's timestamp_pointers (0x2b8/0x2c0) and
   * user_timestamp_pointers (0x2c8/0x2d0) name. */
  APPLE_AGX_BACKEND_U64 TimestampGpuAddress;
} APPLE_AGX_G13_COMPUTE_WORK_INPUT;

typedef struct _APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_INPUT {
  APPLE_AGX_BACKEND_U64 WorkGpuAddress;
  APPLE_AGX_BACKEND_U64 StatisticsGpuAddress;
  APPLE_AGX_BACKEND_U64 QueueInfoGpuAddress;
  APPLE_AGX_BACKEND_U64 NotifierBufferGpuAddress;
  APPLE_AGX_BACKEND_U64 FirmwareStampGpuAddress;
  APPLE_AGX_BACKEND_U64 Counter;
  APPLE_AGX_BACKEND_U64 EventSequence;
  APPLE_AGX_BACKEND_U32 EventGeneration;  /* the notifier generation (queue id) */
  APPLE_AGX_BACKEND_U32 VmSlot;
  APPLE_AGX_BACKEND_U32 StampValue;
} APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_INPUT;

APPLE_AGX_BACKEND_BOOL AppleAgxG13ComputeWorkBuild(
    const APPLE_AGX_G13_COMPUTE_WORK_INPUT *Input,
    unsigned char Work[APPLE_AGX_G13_COMPUTE_WORK_BYTES]);
APPLE_AGX_BACKEND_BOOL AppleAgxG13ComputeMicrosequenceBuild(
    const APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_INPUT *Input,
    unsigned char Sequence[APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_BYTES]);
/* The same sequence with the two Timestamp operations; the work it runs
 * must carry a TimestampGpuAddress. */
APPLE_AGX_BACKEND_BOOL AppleAgxG13ComputeMicrosequenceBuildTimestamps(
    const APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_INPUT *Input,
    unsigned char Sequence[APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_TIMESTAMP_BYTES]);

#endif
