#ifndef APPLE_AGX_REGIONB_H
#define APPLE_AGX_REGIONB_H

#include "apple_agx_uat.h"

typedef struct _APPLE_AGX_REGIONB_INPUT {
  unsigned long long StatsTaAddress;
  unsigned long long Stats3dAddress;
  unsigned long long StatsCpAddress;
  unsigned long long HwdataAAddress;
  unsigned long long FaultInfoAddress;
  unsigned long long TimestampAddress;
  unsigned long long HwdataBAddress;
  unsigned long long FwlogRingAddress;
  unsigned long long Unknown1b8Address;
  unsigned long long Unknown1c0Address;
  unsigned long long Unknown1c8Address;
  unsigned long long BufferManagerGpuAddress;
  unsigned long long BufferManagerCpuAddress;
} APPLE_AGX_REGIONB_INPUT;

typedef struct _APPLE_AGX_REGIONB_MANIFEST {
  unsigned int PointerCount;
  unsigned int FirstOffset;
  unsigned int LastOffset;
} APPLE_AGX_REGIONB_MANIFEST;

typedef enum _APPLE_AGX_REGIONB_RESULT {
  AppleAgxRegionBResultOk = 0,
  AppleAgxRegionBResultInvalidArgument,
  AppleAgxRegionBResultUnsupportedVersion,
  AppleAgxRegionBResultDestinationSize,
  AppleAgxRegionBResultDestinationNotZero,
  AppleAgxRegionBResultAddress,
} APPLE_AGX_REGIONB_RESULT;

APPLE_AGX_REGIONB_RESULT AppleAgxRegionBEncodePointersG13V13_5(
    const APPLE_AGX_REGIONB_INPUT *Input, unsigned char *Destination,
    unsigned int DestinationSize, APPLE_AGX_REGIONB_MANIFEST *Manifest);

#endif /* APPLE_AGX_REGIONB_H */
