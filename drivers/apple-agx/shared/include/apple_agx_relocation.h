#ifndef APPLE_AGX_RELOCATION_H
#define APPLE_AGX_RELOCATION_H

#include "apple_agx_exp208_relocation.h"

typedef enum _APPLE_AGX_RELOCATION_ENCODING {
  AppleAgxRelocationExactU64 = 0,
  AppleAgxRelocationTaFlaggedGpuVa,
  AppleAgxRelocationGpuVaPage32kU32,
} APPLE_AGX_RELOCATION_ENCODING;

typedef enum _APPLE_AGX_RELOCATION_SPACE {
  AppleAgxRelocationGpuVa = 0,
  AppleAgxRelocationPhysical,
} APPLE_AGX_RELOCATION_SPACE;

/* The generic checked writer operates directly on the exact EXP208 manifest
 * records.  Aliasing the types prevents an unsafe layout cast or a second
 * relocation inventory in a caller such as render-admission. */
typedef APPLE_AGX_EXP208_RELOCATION_OBJECT APPLE_AGX_RELOCATION_OBJECT;
typedef APPLE_AGX_EXP208_RELOCATION APPLE_AGX_RELOCATION;

APPLE_AGX_BOOL AppleAgxApplyRelocations(
    APPLE_AGX_RELOCATION_OBJECT *Objects, APPLE_AGX_U32 ObjectCount,
    const APPLE_AGX_RELOCATION *Relocations,
    APPLE_AGX_U32 RelocationCount);

#endif /* APPLE_AGX_RELOCATION_H */
