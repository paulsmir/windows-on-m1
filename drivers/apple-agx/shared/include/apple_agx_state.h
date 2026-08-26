#ifndef APPLE_AGX_STATE_H
#define APPLE_AGX_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "j313_agx_g2.generated.h"

#define APPLE_AGX_MAX_MAPPINGS 64u

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
  uint64_t Base;
  uint64_t Size;
  uint32_t Permissions;
  bool InUse;
} APPLE_AGX_MAPPING;

typedef struct _APPLE_AGX_STATE {
  APPLE_AGX_PHASE Phase;
  APPLE_AGX_MAPPING Mappings[APPLE_AGX_MAX_MAPPINGS];
  size_t MappingCount;
  uint64_t CompletedFence;
  uint64_t SubmittedFence;
  uint64_t SubmitTimeMs;
  bool FenceOutstanding;
} APPLE_AGX_STATE;

void AppleAgxStateInitialize(APPLE_AGX_STATE *State);
bool AppleAgxStateValidateResources(APPLE_AGX_STATE *State);
bool AppleAgxStateTakeFirmwareOwnership(APPLE_AGX_STATE *State);
bool AppleAgxStateMarkQueueReady(APPLE_AGX_STATE *State);
bool AppleAgxStateStart(APPLE_AGX_STATE *State);
bool AppleAgxStateMap(APPLE_AGX_STATE *State, uint64_t Base, uint64_t Size,
                      uint32_t Permissions);
bool AppleAgxStateUnmap(APPLE_AGX_STATE *State, uint64_t Base, uint64_t Size);
bool AppleAgxStateSubmitFence(APPLE_AGX_STATE *State, uint64_t Fence,
                              uint64_t NowMs);
bool AppleAgxStateCompleteFence(APPLE_AGX_STATE *State, uint64_t Fence);
bool AppleAgxStateCheckTimeout(APPLE_AGX_STATE *State, uint64_t NowMs);
bool AppleAgxStateDiscardOutstandingFence(APPLE_AGX_STATE *State);
bool AppleAgxStateBeginReset(APPLE_AGX_STATE *State);
bool AppleAgxStateCompleteReset(APPLE_AGX_STATE *State);
void AppleAgxStateFail(APPLE_AGX_STATE *State);

#endif /* APPLE_AGX_STATE_H */
