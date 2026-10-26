#ifndef APPLE_AGX_G4_PREFLIGHT_H
#define APPLE_AGX_G4_PREFLIGHT_H

#include "apple_agx_g4_submit.h"

/* Buffered, read-only validation before SubmitCommandCb. This is not a
 * submission ticket: the scheduled path and BeginJob must still validate.
 * No user pointers, physical addresses, fence, or ownership transfer. */
#define APPLE_AGX_G4_PREFLIGHT_MAGIC 0x46504741u /* AGPF */
#define APPLE_AGX_G4_PREFLIGHT_VERSION 1u
#define APPLE_AGX_G4_PREFLIGHT_PRIVATE_MAX \
  (sizeof(APPLE_AGX_G4_PRIVATE_HEADER_V5) + APPLE_AGX_G4_NATIVE_MAX_BYTES)
typedef struct {
  unsigned int Magic, Version, Bytes, PrivateBytes;
  unsigned long long CommandVa;
  unsigned int CommandBytes, Ready, Status, ParseResult;
  APPLE_AGX_G4_FAILURE Failure;
  unsigned char Private[APPLE_AGX_G4_PREFLIGHT_PRIVATE_MAX];
} APPLE_AGX_G4_PREFLIGHT;

#endif
