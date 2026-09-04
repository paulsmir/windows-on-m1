#ifndef APPLE_AGX_FIRMWARE_STATUS_H
#define APPLE_AGX_FIRMWARE_STATUS_H

#include "apple_agx_uat.h"

typedef struct _APPLE_AGX_FIRMWARE_STATUS_INPUT {
  unsigned long long StateAddress;
  unsigned long long RingAddress;
} APPLE_AGX_FIRMWARE_STATUS_INPUT;

typedef struct _APPLE_AGX_FIRMWARE_STATUS_MANIFEST {
  unsigned int EncodedSize;
  unsigned long long StateAddress;
  unsigned long long RingAddress;
} APPLE_AGX_FIRMWARE_STATUS_MANIFEST;

typedef enum _APPLE_AGX_FIRMWARE_STATUS_RESULT {
  AppleAgxFirmwareStatusResultOk = 0,
  AppleAgxFirmwareStatusResultInvalidArgument,
  AppleAgxFirmwareStatusResultUnsupportedVersion,
  AppleAgxFirmwareStatusResultDestinationSize,
  AppleAgxFirmwareStatusResultDestinationNotZero,
  AppleAgxFirmwareStatusResultAddress,
} APPLE_AGX_FIRMWARE_STATUS_RESULT;

APPLE_AGX_FIRMWARE_STATUS_RESULT AppleAgxFirmwareStatusEncodeG13V13_5(
    const APPLE_AGX_FIRMWARE_STATUS_INPUT *Input,
    unsigned char *Destination, unsigned int DestinationSize,
    APPLE_AGX_FIRMWARE_STATUS_MANIFEST *Manifest);

#endif /* APPLE_AGX_FIRMWARE_STATUS_H */
