#include "apple_agx_state.h"

static bool AppleAgxPhaseMayMap(APPLE_AGX_PHASE Phase) {
  return Phase == AppleAgxPhaseFirmwareOwned ||
         Phase == AppleAgxPhaseQueueReady || Phase == AppleAgxPhaseRunning;
}

static bool AppleAgxRangeValid(uint64_t Base, uint64_t Size) {
  const uint64_t page = J313_AGX_G2_PAGE_SIZE;
  const uint64_t limit = 1ULL << J313_AGX_G2_ADDRESS_BITS;

  if (Size == 0 || (Base % page) != 0 || (Size % page) != 0)
    return false;
  if (Base >= limit || Size > limit - Base)
    return false;
  return true;
}

static bool AppleAgxRangesOverlap(uint64_t LeftBase, uint64_t LeftSize,
                                  uint64_t RightBase, uint64_t RightSize) {
  return LeftBase < RightBase + RightSize && RightBase < LeftBase + LeftSize;
}

void AppleAgxStateInitialize(APPLE_AGX_STATE *State) {
  size_t index;

  if (State == NULL)
    return;
  State->Phase = AppleAgxPhaseOff;
  State->MappingCount = 0;
  State->CompletedFence = 0;
  State->SubmittedFence = 0;
  State->SubmitTimeMs = 0;
  State->FenceOutstanding = false;
  for (index = 0; index < APPLE_AGX_MAX_MAPPINGS; ++index) {
    State->Mappings[index].Base = 0;
    State->Mappings[index].Size = 0;
    State->Mappings[index].Permissions = 0;
    State->Mappings[index].InUse = false;
  }
}

bool AppleAgxStateValidateResources(APPLE_AGX_STATE *State) {
  if (State == NULL || State->MappingCount != 0 || State->FenceOutstanding)
    return false;
  if (State->Phase != AppleAgxPhaseOff && State->Phase != AppleAgxPhaseStopped)
    return false;
  State->Phase = AppleAgxPhaseResourcesValidated;
  return true;
}

bool AppleAgxStateTakeFirmwareOwnership(APPLE_AGX_STATE *State) {
  if (State == NULL || State->Phase != AppleAgxPhaseResourcesValidated)
    return false;
  State->Phase = AppleAgxPhaseFirmwareOwned;
  return true;
}

bool AppleAgxStateMarkQueueReady(APPLE_AGX_STATE *State) {
  if (State == NULL || State->Phase != AppleAgxPhaseFirmwareOwned)
    return false;
  State->Phase = AppleAgxPhaseQueueReady;
  return true;
}

bool AppleAgxStateStart(APPLE_AGX_STATE *State) {
  if (State == NULL || State->Phase != AppleAgxPhaseQueueReady)
    return false;
  State->Phase = AppleAgxPhaseRunning;
  return true;
}

bool AppleAgxStateMap(APPLE_AGX_STATE *State, uint64_t Base, uint64_t Size,
                      uint32_t Permissions) {
  const uint32_t allowed =
      AppleAgxMapRead | AppleAgxMapWrite | AppleAgxMapExecute;
  size_t index;
  size_t free_index = APPLE_AGX_MAX_MAPPINGS;

  if (State == NULL || !AppleAgxPhaseMayMap(State->Phase) ||
      !AppleAgxRangeValid(Base, Size) || Permissions == 0 ||
      (Permissions & ~allowed) != 0 ||
      (Permissions & (AppleAgxMapWrite | AppleAgxMapExecute)) ==
          (AppleAgxMapWrite | AppleAgxMapExecute) ||
      State->MappingCount >= APPLE_AGX_MAX_MAPPINGS)
    return false;

  for (index = 0; index < APPLE_AGX_MAX_MAPPINGS; ++index) {
    APPLE_AGX_MAPPING *mapping = &State->Mappings[index];
    if (!mapping->InUse) {
      if (free_index == APPLE_AGX_MAX_MAPPINGS)
        free_index = index;
      continue;
    }
    if (AppleAgxRangesOverlap(Base, Size, mapping->Base, mapping->Size))
      return false;
  }
  if (free_index == APPLE_AGX_MAX_MAPPINGS)
    return false;
  State->Mappings[free_index].Base = Base;
  State->Mappings[free_index].Size = Size;
  State->Mappings[free_index].Permissions = Permissions;
  State->Mappings[free_index].InUse = true;
  ++State->MappingCount;
  return true;
}

bool AppleAgxStateUnmap(APPLE_AGX_STATE *State, uint64_t Base, uint64_t Size) {
  size_t index;

  if (State == NULL)
    return false;
  for (index = 0; index < APPLE_AGX_MAX_MAPPINGS; ++index) {
    APPLE_AGX_MAPPING *mapping = &State->Mappings[index];
    if (!mapping->InUse || mapping->Base != Base || mapping->Size != Size)
      continue;
    mapping->Base = 0;
    mapping->Size = 0;
    mapping->Permissions = 0;
    mapping->InUse = false;
    --State->MappingCount;
    return true;
  }
  return false;
}

bool AppleAgxStateSubmitFence(APPLE_AGX_STATE *State, uint64_t Fence,
                              uint64_t NowMs) {
  if (State == NULL || State->Phase != AppleAgxPhaseRunning ||
      State->FenceOutstanding || Fence == 0 ||
      Fence != State->CompletedFence + 1)
    return false;
  State->SubmittedFence = Fence;
  State->SubmitTimeMs = NowMs;
  State->FenceOutstanding = true;
  return true;
}

bool AppleAgxStateCompleteFence(APPLE_AGX_STATE *State, uint64_t Fence) {
  if (State == NULL || State->Phase != AppleAgxPhaseRunning ||
      !State->FenceOutstanding || Fence != State->SubmittedFence)
    return false;
  State->CompletedFence = Fence;
  State->SubmitTimeMs = 0;
  State->FenceOutstanding = false;
  return true;
}

bool AppleAgxStateCheckTimeout(APPLE_AGX_STATE *State, uint64_t NowMs) {
  if (State == NULL || State->Phase != AppleAgxPhaseRunning ||
      !State->FenceOutstanding)
    return false;
  if (NowMs < State->SubmitTimeMs) {
    State->Phase = AppleAgxPhaseResetting;
    return true;
  }
  if (NowMs - State->SubmitTimeMs <= J313_AGX_G2_WORK_TIMEOUT_MS)
    return false;
  State->Phase = AppleAgxPhaseResetting;
  return true;
}

bool AppleAgxStateDiscardOutstandingFence(APPLE_AGX_STATE *State) {
  if (State == NULL || State->Phase != AppleAgxPhaseResetting ||
      !State->FenceOutstanding)
    return false;
  State->SubmittedFence = State->CompletedFence;
  State->SubmitTimeMs = 0;
  State->FenceOutstanding = false;
  return true;
}

bool AppleAgxStateBeginReset(APPLE_AGX_STATE *State) {
  if (State == NULL || State->Phase == AppleAgxPhaseOff ||
      State->Phase == AppleAgxPhaseStopped ||
      State->Phase == AppleAgxPhaseResetting)
    return false;
  State->Phase = AppleAgxPhaseResetting;
  return true;
}

bool AppleAgxStateCompleteReset(APPLE_AGX_STATE *State) {
  if (State == NULL || State->Phase != AppleAgxPhaseResetting ||
      State->MappingCount != 0 || State->FenceOutstanding)
    return false;
  State->Phase = AppleAgxPhaseStopped;
  return true;
}

void AppleAgxStateFail(APPLE_AGX_STATE *State) {
  if (State != NULL)
    State->Phase = AppleAgxPhaseFailed;
}
