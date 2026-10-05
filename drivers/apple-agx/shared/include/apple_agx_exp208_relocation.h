#ifndef APPLE_AGX_EXP208_RELOCATION_H
#define APPLE_AGX_EXP208_RELOCATION_H

#include "apple_agx_state.h"

typedef enum _APPLE_AGX_EXP208_RELOCATION_ENCODING {
  AppleAgxExp208RelocationExactU64 = 0,
  AppleAgxExp208RelocationTaFlaggedGpuVa,
  AppleAgxExp208RelocationGpuVaPage32kU32,
} APPLE_AGX_EXP208_RELOCATION_ENCODING;

typedef enum _APPLE_AGX_EXP208_RELOCATION_SPACE {
  AppleAgxExp208RelocationGpuVa = 0,
  AppleAgxExp208RelocationPhysical,
} APPLE_AGX_EXP208_RELOCATION_SPACE;

typedef struct _APPLE_AGX_EXP208_RELOCATION_OBJECT {
  APPLE_AGX_U64 GpuVa;
  APPLE_AGX_U64 PhysicalAddress;
  APPLE_AGX_U32 Size;
  unsigned char *Data;
} APPLE_AGX_EXP208_RELOCATION_OBJECT;

typedef struct _APPLE_AGX_EXP208_RELOCATION {
  APPLE_AGX_U32 SourceObject;
  APPLE_AGX_U32 SourceOffset;
  APPLE_AGX_U32 TargetObject;
  APPLE_AGX_U32 TargetOffset;
  APPLE_AGX_U32 AddressSpace;
  APPLE_AGX_U32 Encoding;
  APPLE_AGX_U64 EncodingBits;
} APPLE_AGX_EXP208_RELOCATION;

#endif /* APPLE_AGX_EXP208_RELOCATION_H */
