#ifndef APPLE_AGX_INITDATA_H
#define APPLE_AGX_INITDATA_H

#include "apple_agx_uat.h"

typedef struct _APPLE_AGX_INITDATA_INPUT {
  unsigned long long TaggedBufferAddress;
  unsigned long long RuntimePointersAddress;
  unsigned long long GlobalsAddress;
  unsigned long long FirmwareStatusAddress;
} APPLE_AGX_INITDATA_INPUT;

typedef struct _APPLE_AGX_INITDATA_MANIFEST {
  unsigned int EncodedSize;
  unsigned short VersionWords[4];
  unsigned long long ReferencedAddresses[4];
} APPLE_AGX_INITDATA_MANIFEST;

typedef enum _APPLE_AGX_INITDATA_RESULT {
  AppleAgxInitdataResultOk = 0,
  AppleAgxInitdataResultInvalidArgument,
  AppleAgxInitdataResultUnsupportedVersion,
  AppleAgxInitdataResultDestinationSize,
  AppleAgxInitdataResultDestinationNotZero,
  AppleAgxInitdataResultAddress,
  AppleAgxInitdataResultOverlap,
} APPLE_AGX_INITDATA_RESULT;

APPLE_AGX_INITDATA_RESULT AppleAgxInitdataEncodeG13V13_5(
    const APPLE_AGX_INITDATA_INPUT *Input, unsigned char *Destination,
    unsigned int DestinationSize, APPLE_AGX_INITDATA_MANIFEST *Manifest);

#endif /* APPLE_AGX_INITDATA_H */
