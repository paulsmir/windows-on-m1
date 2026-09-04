#ifndef APPLE_AGX_GFX_HANDOFF_H
#define APPLE_AGX_GFX_HANDOFF_H

#include "j313_agx_g2.generated.h"

#define APPLE_AGX_GFX_HANDOFF_MAGIC_AP_OFFSET 0x000u
#define APPLE_AGX_GFX_HANDOFF_MAGIC_FW_OFFSET 0x008u
#define APPLE_AGX_GFX_HANDOFF_LOCK_AP_OFFSET 0x010u
#define APPLE_AGX_GFX_HANDOFF_LOCK_FW_OFFSET 0x011u
#define APPLE_AGX_GFX_HANDOFF_TURN_OFFSET 0x014u
#define APPLE_AGX_GFX_HANDOFF_FLUSH_STATE_OFFSET 0x020u
#define APPLE_AGX_GFX_HANDOFF_FLUSH_ADDR_OFFSET 0x028u
#define APPLE_AGX_GFX_HANDOFF_FLUSH_SIZE_OFFSET 0x030u
#define APPLE_AGX_GFX_HANDOFF_FLUSH_STRIDE 0x018u
#define APPLE_AGX_GFX_HANDOFF_FLUSH_COUNT 0x041u
#define APPLE_AGX_GFX_HANDOFF_PPL_MAGIC 0x4b1d000000000002ULL
#define APPLE_AGX_GFX_HANDOFF_MINIMUM_SIZE 0x648u

typedef enum _APPLE_AGX_GFX_HANDOFF_RESULT {
  AppleAgxGfxHandoffResultOk = 0,
  AppleAgxGfxHandoffResultInvalidArgument,
  AppleAgxGfxHandoffResultInvalidGeometry,
  AppleAgxGfxHandoffResultInvalidState,
  AppleAgxGfxHandoffResultAccessFailed,
  AppleAgxGfxHandoffResultTimeout,
} APPLE_AGX_GFX_HANDOFF_RESULT;

typedef struct _APPLE_AGX_GFX_HANDOFF_REGION {
  unsigned long long PhysicalBase;
  unsigned int Length;
} APPLE_AGX_GFX_HANDOFF_REGION;

typedef struct _APPLE_AGX_GFX_HANDOFF_IO {
  void *Context;
  unsigned char (*Read8)(void *, unsigned int, unsigned char *);
  unsigned char (*Read32)(void *, unsigned int, unsigned int *);
  unsigned char (*Read64)(void *, unsigned int, unsigned long long *);
  unsigned char (*Write8)(void *, unsigned int, unsigned char);
  unsigned char (*Write32)(void *, unsigned int, unsigned int);
  unsigned char (*Write64)(void *, unsigned int, unsigned long long);
  void (*Barrier)(void *);
  void (*Relax)(void *);
  unsigned long long (*Now)(void *);
} APPLE_AGX_GFX_HANDOFF_IO;

typedef struct _APPLE_AGX_GFX_HANDOFF_STATE {
  APPLE_AGX_GFX_HANDOFF_REGION Region;
  APPLE_AGX_GFX_HANDOFF_IO Io;
  unsigned char Bound;
  unsigned char Locked;
  unsigned char Initialized;
} APPLE_AGX_GFX_HANDOFF_STATE;

APPLE_AGX_GFX_HANDOFF_RESULT AppleAgxGfxHandoffBindJ313(
    APPLE_AGX_GFX_HANDOFF_STATE *, const APPLE_AGX_GFX_HANDOFF_REGION *,
    const APPLE_AGX_GFX_HANDOFF_IO *);
APPLE_AGX_GFX_HANDOFF_RESULT AppleAgxGfxHandoffAcquire(
    APPLE_AGX_GFX_HANDOFF_STATE *, unsigned long long);
APPLE_AGX_GFX_HANDOFF_RESULT AppleAgxGfxHandoffInitialize(
    APPLE_AGX_GFX_HANDOFF_STATE *, unsigned long long);
APPLE_AGX_GFX_HANDOFF_RESULT AppleAgxGfxHandoffRelease(
    APPLE_AGX_GFX_HANDOFF_STATE *);

#endif
