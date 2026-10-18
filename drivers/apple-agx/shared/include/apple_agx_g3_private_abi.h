#ifndef APPLE_AGX_G3_PRIVATE_ABI_H
#define APPLE_AGX_G3_PRIVATE_ABI_H
#include "apple_agx_g4_submit.h"
#define APPLE_AGX_G3_PRIVATE_MAGIC 0x33565041u
#define APPLE_AGX_G3_PRIVATE_VERSION 1u
#define APPLE_AGX_G3_PRIVATE_ACQUIRE 1u
#define APPLE_AGX_G3_PRIVATE_PREPARE 2u
#define APPLE_AGX_G3_PRIVATE_RELEASE 3u
/* No addresses or handles are accepted from the payload. The runtime escape
 * handles authenticate the owner. Ranges are output only; tokens are opaque. */
typedef struct {
  unsigned int Magic, Version, Bytes, Operation;
  unsigned int Width, Height, UtileWidth, UtileHeight, Layers, Samples;
  unsigned int Reserved[2];
  unsigned long long ManagerId, ManagerGeneration, SceneId, SceneGeneration;
  APPLE_AGX_G4_PROCESS_RANGE Ranges[9];
} APPLE_AGX_G3_PRIVATE_REQUEST;
#endif
