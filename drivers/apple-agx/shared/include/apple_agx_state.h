#ifndef APPLE_AGX_STATE_H
#define APPLE_AGX_STATE_H

#include "j313_agx_g2.generated.h"

#define APPLE_AGX_MAX_MAPPINGS 64u

/* Keep this shared state usable in both freestanding WDK C and host tests. */
typedef unsigned char APPLE_AGX_BOOL;
typedef unsigned int APPLE_AGX_U32;
typedef unsigned long long APPLE_AGX_U64;
typedef unsigned int APPLE_AGX_COUNT;

#define APPLE_AGX_FALSE ((APPLE_AGX_BOOL)0u)
#define APPLE_AGX_TRUE ((APPLE_AGX_BOOL)1u)

typedef enum _APPLE_AGX_PHASE {
  AppleAgxPhaseOff = 0,
  AppleAgxPhaseResourcesValidated,
  AppleAgxPhaseFirmwareOwned,
  AppleAgxPhaseQueueReady,
  AppleAgxPhaseRunning,
  AppleAgxPhaseResetting,
  AppleAgxPhaseStopped,
  AppleAgxPhaseFailed,
} APPLE_AGX_PHASE;

typedef enum _APPLE_AGX_MAP_PERMISSION {
  AppleAgxMapRead = 1u << 0,
  AppleAgxMapWrite = 1u << 1,
  AppleAgxMapExecute = 1u << 2,
} APPLE_AGX_MAP_PERMISSION;

typedef struct _APPLE_AGX_MAPPING {
  APPLE_AGX_U64 Base;
  APPLE_AGX_U64 Size;
  APPLE_AGX_U32 Permissions;
  APPLE_AGX_BOOL InUse;
} APPLE_AGX_MAPPING;

typedef struct _APPLE_AGX_STATE {
  APPLE_AGX_PHASE Phase;
  APPLE_AGX_MAPPING Mappings[APPLE_AGX_MAX_MAPPINGS];
  APPLE_AGX_COUNT MappingCount;
  APPLE_AGX_U64 CompletedFence;
  APPLE_AGX_U64 SubmittedFence;
  APPLE_AGX_U64 SubmitTimeMs;
  APPLE_AGX_BOOL FenceOutstanding;
} APPLE_AGX_STATE;

void AppleAgxStateInitialize(APPLE_AGX_STATE *State);
APPLE_AGX_BOOL AppleAgxStateValidateResources(APPLE_AGX_STATE *State);
APPLE_AGX_BOOL AppleAgxStateTakeFirmwareOwnership(APPLE_AGX_STATE *State);
APPLE_AGX_BOOL AppleAgxStateMarkQueueReady(APPLE_AGX_STATE *State);
APPLE_AGX_BOOL AppleAgxStateStart(APPLE_AGX_STATE *State);
APPLE_AGX_BOOL AppleAgxStateMap(APPLE_AGX_STATE *State, APPLE_AGX_U64 Base,
                                APPLE_AGX_U64 Size, APPLE_AGX_U32 Permissions);
APPLE_AGX_BOOL AppleAgxStateUnmap(APPLE_AGX_STATE *State, APPLE_AGX_U64 Base,
                                  APPLE_AGX_U64 Size);
APPLE_AGX_BOOL AppleAgxStateSubmitFence(APPLE_AGX_STATE *State,
                                        APPLE_AGX_U64 Fence,
                                        APPLE_AGX_U64 NowMs);
APPLE_AGX_BOOL AppleAgxStateCompleteFence(APPLE_AGX_STATE *State,
                                          APPLE_AGX_U64 Fence);
APPLE_AGX_BOOL AppleAgxStateCheckTimeout(APPLE_AGX_STATE *State,
                                         APPLE_AGX_U64 NowMs);
APPLE_AGX_BOOL AppleAgxStateDiscardOutstandingFence(APPLE_AGX_STATE *State);
APPLE_AGX_BOOL AppleAgxStateBeginReset(APPLE_AGX_STATE *State);
APPLE_AGX_BOOL AppleAgxStateCompleteReset(APPLE_AGX_STATE *State);
void AppleAgxStateFail(APPLE_AGX_STATE *State);

#endif /* APPLE_AGX_STATE_H */
