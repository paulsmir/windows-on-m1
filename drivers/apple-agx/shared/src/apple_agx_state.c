#include "apple_agx_state.h"

#define APPLE_AGX_NULL ((void *)0)

static APPLE_AGX_BOOL AppleAgxPhaseMayMap(APPLE_AGX_PHASE Phase) {
  return Phase == AppleAgxPhaseFirmwareOwned ||
         Phase == AppleAgxPhaseQueueReady || Phase == AppleAgxPhaseRunning;
}

static APPLE_AGX_BOOL AppleAgxRangeValid(APPLE_AGX_U64 Base,
                                         APPLE_AGX_U64 Size) {
  const APPLE_AGX_U64 page = J313_AGX_G2_PAGE_SIZE;
  const APPLE_AGX_U64 limit = 1ULL << J313_AGX_G2_ADDRESS_BITS;

  if (Size == 0 || (Base % page) != 0 || (Size % page) != 0)
    return APPLE_AGX_FALSE;
  if (Base >= limit || Size > limit - Base)
    return APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
}

static APPLE_AGX_BOOL AppleAgxRangesOverlap(APPLE_AGX_U64 LeftBase,
                                            APPLE_AGX_U64 LeftSize,
                                            APPLE_AGX_U64 RightBase,
                                            APPLE_AGX_U64 RightSize) {
  return LeftBase < RightBase + RightSize && RightBase < LeftBase + LeftSize;
}

void AppleAgxStateInitialize(APPLE_AGX_STATE *State) {
  APPLE_AGX_COUNT index;

  if (State == APPLE_AGX_NULL)
    return;
  State->Phase = AppleAgxPhaseOff;
  State->MappingCount = 0;
  State->CompletedFence = 0;
  State->SubmittedFence = 0;
  State->SubmitTimeMs = 0;
  State->FenceOutstanding = APPLE_AGX_FALSE;
  for (index = 0; index < APPLE_AGX_MAX_MAPPINGS; ++index) {
    State->Mappings[index].Base = 0;
    State->Mappings[index].Size = 0;
    State->Mappings[index].Permissions = 0;
    State->Mappings[index].InUse = APPLE_AGX_FALSE;
  }
}

APPLE_AGX_BOOL AppleAgxStateValidateResources(APPLE_AGX_STATE *State) {
  if (State == APPLE_AGX_NULL || State->MappingCount != 0 ||
      State->FenceOutstanding)
    return APPLE_AGX_FALSE;
  if (State->Phase != AppleAgxPhaseOff && State->Phase != AppleAgxPhaseStopped)
    return APPLE_AGX_FALSE;
  State->Phase = AppleAgxPhaseResourcesValidated;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxStateTakeFirmwareOwnership(APPLE_AGX_STATE *State) {
  if (State == APPLE_AGX_NULL ||
      State->Phase != AppleAgxPhaseResourcesValidated)
    return APPLE_AGX_FALSE;
  State->Phase = AppleAgxPhaseFirmwareOwned;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxStateMarkQueueReady(APPLE_AGX_STATE *State) {
  if (State == APPLE_AGX_NULL || State->Phase != AppleAgxPhaseFirmwareOwned)
    return APPLE_AGX_FALSE;
  State->Phase = AppleAgxPhaseQueueReady;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxStateStart(APPLE_AGX_STATE *State) {
  if (State == APPLE_AGX_NULL || State->Phase != AppleAgxPhaseQueueReady)
    return APPLE_AGX_FALSE;
  State->Phase = AppleAgxPhaseRunning;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxStateMap(APPLE_AGX_STATE *State, APPLE_AGX_U64 Base,
                                APPLE_AGX_U64 Size, APPLE_AGX_U32 Permissions) {
  const APPLE_AGX_U32 allowed =
      AppleAgxMapRead | AppleAgxMapWrite | AppleAgxMapExecute;
  APPLE_AGX_COUNT index;
  APPLE_AGX_COUNT free_index = APPLE_AGX_MAX_MAPPINGS;

  if (State == APPLE_AGX_NULL || !AppleAgxPhaseMayMap(State->Phase) ||
      !AppleAgxRangeValid(Base, Size) || Permissions == 0 ||
      (Permissions & ~allowed) != 0 ||
      (Permissions & (AppleAgxMapWrite | AppleAgxMapExecute)) ==
          (AppleAgxMapWrite | AppleAgxMapExecute) ||
      State->MappingCount >= APPLE_AGX_MAX_MAPPINGS)
    return APPLE_AGX_FALSE;

  for (index = 0; index < APPLE_AGX_MAX_MAPPINGS; ++index) {
    APPLE_AGX_MAPPING *mapping = &State->Mappings[index];
    if (!mapping->InUse) {
      if (free_index == APPLE_AGX_MAX_MAPPINGS)
        free_index = index;
      continue;
    }
    if (AppleAgxRangesOverlap(Base, Size, mapping->Base, mapping->Size))
      return APPLE_AGX_FALSE;
  }
  if (free_index == APPLE_AGX_MAX_MAPPINGS)
    return APPLE_AGX_FALSE;
  State->Mappings[free_index].Base = Base;
  State->Mappings[free_index].Size = Size;
  State->Mappings[free_index].Permissions = Permissions;
  State->Mappings[free_index].InUse = APPLE_AGX_TRUE;
  ++State->MappingCount;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxStateUnmap(APPLE_AGX_STATE *State, APPLE_AGX_U64 Base,
                                  APPLE_AGX_U64 Size) {
  APPLE_AGX_COUNT index;

  if (State == APPLE_AGX_NULL)
    return APPLE_AGX_FALSE;
  for (index = 0; index < APPLE_AGX_MAX_MAPPINGS; ++index) {
    APPLE_AGX_MAPPING *mapping = &State->Mappings[index];
    if (!mapping->InUse || mapping->Base != Base || mapping->Size != Size)
      continue;
    mapping->Base = 0;
    mapping->Size = 0;
    mapping->Permissions = 0;
    mapping->InUse = APPLE_AGX_FALSE;
    --State->MappingCount;
    return APPLE_AGX_TRUE;
  }
  return APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxStateSubmitFence(APPLE_AGX_STATE *State,
                                        APPLE_AGX_U64 Fence,
                                        APPLE_AGX_U64 NowMs) {
  if (State == APPLE_AGX_NULL || State->Phase != AppleAgxPhaseRunning ||
      State->FenceOutstanding || Fence == 0 ||
      Fence != State->CompletedFence + 1)
    return APPLE_AGX_FALSE;
  State->SubmittedFence = Fence;
  State->SubmitTimeMs = NowMs;
  State->FenceOutstanding = APPLE_AGX_TRUE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxStateCompleteFence(APPLE_AGX_STATE *State,
                                          APPLE_AGX_U64 Fence) {
  if (State == APPLE_AGX_NULL || State->Phase != AppleAgxPhaseRunning ||
      !State->FenceOutstanding || Fence != State->SubmittedFence)
    return APPLE_AGX_FALSE;
  State->CompletedFence = Fence;
  State->SubmitTimeMs = 0;
  State->FenceOutstanding = APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxStateCheckTimeout(APPLE_AGX_STATE *State,
                                         APPLE_AGX_U64 NowMs) {
  if (State == APPLE_AGX_NULL || State->Phase != AppleAgxPhaseRunning ||
      !State->FenceOutstanding)
    return APPLE_AGX_FALSE;
  if (NowMs < State->SubmitTimeMs) {
    State->Phase = AppleAgxPhaseResetting;
    return APPLE_AGX_TRUE;
  }
  if (NowMs - State->SubmitTimeMs <= J313_AGX_G2_WORK_TIMEOUT_MS)
    return APPLE_AGX_FALSE;
  State->Phase = AppleAgxPhaseResetting;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxStateDiscardOutstandingFence(APPLE_AGX_STATE *State) {
  if (State == APPLE_AGX_NULL || State->Phase != AppleAgxPhaseResetting ||
      !State->FenceOutstanding)
    return APPLE_AGX_FALSE;
  State->SubmittedFence = State->CompletedFence;
  State->SubmitTimeMs = 0;
  State->FenceOutstanding = APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxStateBeginReset(APPLE_AGX_STATE *State) {
  if (State == APPLE_AGX_NULL || State->Phase == AppleAgxPhaseOff ||
      State->Phase == AppleAgxPhaseStopped ||
      State->Phase == AppleAgxPhaseResetting)
    return APPLE_AGX_FALSE;
  State->Phase = AppleAgxPhaseResetting;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxStateCompleteReset(APPLE_AGX_STATE *State) {
  if (State == APPLE_AGX_NULL || State->Phase != AppleAgxPhaseResetting ||
      State->MappingCount != 0 || State->FenceOutstanding)
    return APPLE_AGX_FALSE;
  State->Phase = AppleAgxPhaseStopped;
  return APPLE_AGX_TRUE;
}

void AppleAgxStateFail(APPLE_AGX_STATE *State) {
  if (State != APPLE_AGX_NULL)
    State->Phase = AppleAgxPhaseFailed;
}
