#ifndef APPLE_AGX_MAPPING_H
#define APPLE_AGX_MAPPING_H

#include "apple_agx_uat.h"

typedef struct _APPLE_AGX_MAPPING_STATE {
  unsigned char *SgxBase;
  unsigned char *AscBase;
  unsigned long long SgxPhysicalAddress;
  unsigned int SgxLength;
  unsigned char Active;
} APPLE_AGX_MAPPING_STATE;

typedef struct _APPLE_AGX_MAPPING_IO {
  void *Context;
  unsigned char (*Map)(void *Context, unsigned long long PhysicalAddress,
                       unsigned int Length, unsigned char **VirtualAddress);
  unsigned char (*Unmap)(void *Context, unsigned char *VirtualAddress);
} APPLE_AGX_MAPPING_IO;

APPLE_AGX_UAT_RESULT AppleAgxMappingStart(
    const APPLE_AGX_MAPPING_IO *Io, APPLE_AGX_MAPPING_STATE *State);
APPLE_AGX_UAT_RESULT AppleAgxMappingStop(
    const APPLE_AGX_MAPPING_IO *Io, APPLE_AGX_MAPPING_STATE *State);

#ifdef APPLE_AGX_MAPPING_TEST
APPLE_AGX_UAT_RESULT AppleAgxMappingStartWithRangesForTest(
    const APPLE_AGX_MAPPING_IO *Io, APPLE_AGX_MAPPING_STATE *State,
    unsigned long long SgxPhysicalAddress, unsigned int SgxLength,
    unsigned long long AscPhysicalAddress, unsigned int AscLength);
#endif

#endif /* APPLE_AGX_MAPPING_H */
