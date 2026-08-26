#ifndef APPLE_AGX_POWER_H
#define APPLE_AGX_POWER_H

#include "j313_agx_g2.generated.h"

typedef unsigned int APPLE_AGX_POWER_U32;
typedef unsigned long long APPLE_AGX_POWER_U64;
typedef int APPLE_AGX_POWER_BOOL;

typedef struct _APPLE_AGX_POWER_IO {
  void *Context;
  APPLE_AGX_POWER_U32 (*Read32)(void *Context, APPLE_AGX_POWER_U32 Offset);
  APPLE_AGX_POWER_U64 (*Read64)(void *Context, APPLE_AGX_POWER_U32 Offset);
  void (*Write32)(void *Context, APPLE_AGX_POWER_U32 Offset,
                  APPLE_AGX_POWER_U32 Value);
  void (*Write64)(void *Context, APPLE_AGX_POWER_U32 Offset,
                  APPLE_AGX_POWER_U64 Value);
} APPLE_AGX_POWER_IO;

APPLE_AGX_POWER_BOOL AppleAgxPowerQualify(const APPLE_AGX_POWER_IO *Io);

#endif /* APPLE_AGX_POWER_H */
