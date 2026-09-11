#ifndef APPLE_AGX_CHANNEL_INFO_H
#define APPLE_AGX_CHANNEL_INFO_H

#include "apple_agx_uat.h"

typedef struct _APPLE_AGX_CHANNEL_INFO_ENTRY {
  unsigned long long StateAddress;
  unsigned long long RingAddress;
} APPLE_AGX_CHANNEL_INFO_ENTRY;

typedef struct _APPLE_AGX_CHANNEL_INFO_INPUT {
  APPLE_AGX_CHANNEL_INFO_ENTRY Entries[J313_AGX_G2_CHANNEL_INFO_COUNT];
} APPLE_AGX_CHANNEL_INFO_INPUT;

typedef struct _APPLE_AGX_CHANNEL_INFO_MANIFEST {
  unsigned int EncodedSize;
  unsigned int ChannelCount;
} APPLE_AGX_CHANNEL_INFO_MANIFEST;

typedef enum _APPLE_AGX_CHANNEL_INFO_RESULT {
  AppleAgxChannelInfoResultOk = 0,
  AppleAgxChannelInfoResultInvalidArgument,
  AppleAgxChannelInfoResultUnsupportedVersion,
  AppleAgxChannelInfoResultDestinationSize,
  AppleAgxChannelInfoResultDestinationNotZero,
  AppleAgxChannelInfoResultAddress,
} APPLE_AGX_CHANNEL_INFO_RESULT;

APPLE_AGX_CHANNEL_INFO_RESULT AppleAgxChannelInfoEncodeG13V13_5(
    const APPLE_AGX_CHANNEL_INFO_INPUT *Input,
    unsigned char *Destination, unsigned int DestinationSize,
    APPLE_AGX_CHANNEL_INFO_MANIFEST *Manifest);

#endif /* APPLE_AGX_CHANNEL_INFO_H */
