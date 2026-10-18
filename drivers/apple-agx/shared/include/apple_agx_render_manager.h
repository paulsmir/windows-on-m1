#ifndef APPLE_AGX_RENDER_MANAGER_H
#define APPLE_AGX_RENDER_MANAGER_H
#include "apple_agx_state.h"
#include "apple_agx_g4_submit.h"

/* G13/V13_5 manager-owned state. Queue and scene bytes are deliberately absent.
 * The caller pins this CPU snapshot with the R137 private manager lifetime. */
typedef struct _APPLE_AGX_RENDER_MANAGER_KEY {
  APPLE_AGX_U64 Owner, Generation, RootIpa;
  APPLE_AGX_G4_PROCESS_RANGE Backing[3];
} APPLE_AGX_RENDER_MANAGER_KEY;
typedef struct _APPLE_AGX_RENDER_MANAGER_STATE {
  APPLE_AGX_RENDER_MANAGER_KEY Key;
  unsigned char Info[188], BlockControl[64], Counter[64], Stats[64];
  APPLE_AGX_U32 PendingFence, SavedFence;
  APPLE_AGX_BOOL Valid;
} APPLE_AGX_RENDER_MANAGER_STATE;

#endif
