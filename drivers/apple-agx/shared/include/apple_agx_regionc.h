#ifndef APPLE_AGX_REGIONC_H
#define APPLE_AGX_REGIONC_H

#include "apple_agx_config_snapshot.h"
#include "j313_agx_g2.generated.h"

typedef struct _APPLE_AGX_REGIONC_MANIFEST {
  unsigned int EncodedSize;
  unsigned int NonzeroWordCount;
  unsigned long long OracleFnv1a64;
} APPLE_AGX_REGIONC_MANIFEST;

typedef enum _APPLE_AGX_REGIONC_RESULT {
  AppleAgxRegionCResultOk = 0,
  AppleAgxRegionCResultInvalidArgument,
  AppleAgxRegionCResultDestinationSize,
  AppleAgxRegionCResultDestinationNotZero,
  AppleAgxRegionCResultSnapshotMismatch,
} APPLE_AGX_REGIONC_RESULT;

APPLE_AGX_REGIONC_RESULT AppleAgxRegionCEncodeJ313G13V13_5(
    const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot, unsigned char *Destination,
    unsigned int DestinationSize, APPLE_AGX_REGIONC_MANIFEST *Manifest);

#endif /* APPLE_AGX_REGIONC_H */
