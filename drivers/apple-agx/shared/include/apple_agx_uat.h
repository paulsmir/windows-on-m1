#ifndef APPLE_AGX_UAT_H
#define APPLE_AGX_UAT_H

#include "j313_agx_g2.generated.h"

typedef enum _APPLE_AGX_UAT_RESULT {
  AppleAgxUatResultOk = 0,
  AppleAgxUatResultInvalidArgument,
  AppleAgxUatResultUnsupportedContext,
  AppleAgxUatResultMisaligned,
  AppleAgxUatResultOutOfRange,
  AppleAgxUatResultOverflow,
  AppleAgxUatResultUnsupportedProtection,
  AppleAgxUatResultAlreadyMapped,
  AppleAgxUatResultCapacity,
  AppleAgxUatResultAllocationFailed,
} APPLE_AGX_UAT_RESULT;

typedef enum _APPLE_AGX_UAT_PROTECTION {
  AppleAgxUatFirmwareDeviceReadWrite = 1,
  AppleAgxUatFirmwareSharedReadWrite,
  AppleAgxUatFirmwarePrivateReadWrite,
  AppleAgxUatFirmwareGpuPrivateReadWrite,
  AppleAgxUatFirmwareReadWriteGpuReadOnly,
  AppleAgxUatGpuSharedReadOnly,
  AppleAgxUatGpuSharedWriteOnly,
  AppleAgxUatGpuSharedReadWrite,
} APPLE_AGX_UAT_PROTECTION;

typedef enum _APPLE_AGX_UAT_HALF {
  AppleAgxUatTtbr0 = 0,
  AppleAgxUatTtbr1 = 1,
} APPLE_AGX_UAT_HALF;

typedef struct _APPLE_AGX_UAT_ROOTS {
  unsigned long long Ttbr0PhysicalAddress;
  unsigned long long Ttbr1PhysicalAddress;
} APPLE_AGX_UAT_ROOTS;

typedef struct _APPLE_AGX_UAT_TTBR_PAIR {
  unsigned long long Ttbr0;
  unsigned long long Ttbr1;
} APPLE_AGX_UAT_TTBR_PAIR;

APPLE_AGX_UAT_RESULT AppleAgxUatValidateRange(
    unsigned int Context, unsigned long long VirtualAddress,
    unsigned long long PhysicalAddress, unsigned long long Length,
    APPLE_AGX_UAT_PROTECTION Protection, APPLE_AGX_UAT_HALF *Half);
APPLE_AGX_UAT_RESULT AppleAgxUatEncodeTableDescriptor(
    unsigned long long PhysicalAddress, unsigned long long *Descriptor);
APPLE_AGX_UAT_RESULT AppleAgxUatEncodePageDescriptor(
    unsigned int Context, unsigned long long PhysicalAddress,
    APPLE_AGX_UAT_PROTECTION Protection,
    unsigned long long *Descriptor);
APPLE_AGX_UAT_RESULT AppleAgxUatEncodeTtbrPair(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    APPLE_AGX_UAT_TTBR_PAIR *Pair);
void AppleAgxUatClearTtbrPair(APPLE_AGX_UAT_TTBR_PAIR *Pair);

#endif /* APPLE_AGX_UAT_H */
