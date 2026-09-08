#include "render_qualification.h"

#define QUERY_NULL ((void *)0)

static void query_zero(void *Data, unsigned int Bytes) {
  unsigned char *data = (unsigned char *)Data;
  while (Bytes-- != 0u)
    *data++ = 0u;
}

int AdmissionPresentQueryBuild(
    ADMISSION_PRESENT_QUERY *Record,
    const ADMISSION_PRESENT_VERIFICATION *Verified) {
  if (Record == QUERY_NULL || Verified == QUERY_NULL ||
      Verified->CandidateBuild == 0u || Verified->BootGeneration == 0u ||
      Verified->Index >= ADMISSION_PRESENT_QUERY_CAPACITY ||
      Verified->Purpose != AdmissionPresentPurposeRenderFrame ||
      Verified->Fence == 0u || Verified->AllocationToken == 0ULL ||
      Verified->PixelsExpected == 0u ||
      Verified->PixelsVerified != Verified->PixelsExpected ||
      Verified->Format == 0u || Verified->Width == 0u ||
      Verified->Height == 0u || Verified->Pitch == 0u ||
      Verified->Sequence == 0ULL || Verified->PhysicalAddress == 0ULL ||
      Verified->ContentHash == 0ULL)
    return 0;
  query_zero(Record, sizeof(*Record));
  Record->Magic = ADMISSION_PRESENT_QUERY_MAGIC;
  Record->Version = ADMISSION_PRESENT_QUERY_VERSION;
  Record->CandidateBuild = Verified->CandidateBuild;
  Record->BootGeneration = Verified->BootGeneration;
  Record->Index = Verified->Index;
  Record->PresentCount = Verified->Index + 1u;
  Record->Purpose = Verified->Purpose;
  Record->DestinationIndex = Verified->DestinationIndex;
  Record->Fence = Verified->Fence;
  Record->Status = 0u;
  Record->Valid = 1u;
  Record->Captured = 1u;
  Record->ExpectedColor = Verified->ExpectedColor;
  Record->PixelsExpected = Verified->PixelsExpected;
  Record->PixelsVerified = Verified->PixelsVerified;
  Record->Format = Verified->Format;
  Record->Width = Verified->Width;
  Record->Height = Verified->Height;
  Record->Pitch = Verified->Pitch;
  Record->AllocationToken = Verified->AllocationToken;
  Record->Sequence = Verified->Sequence;
  Record->ActiveOffset = Verified->ActiveOffset;
  Record->PhysicalAddress = Verified->PhysicalAddress;
  Record->ContentHash = Verified->ContentHash;
  return 1;
}

int AdmissionPresentQueryAccept(
    const ADMISSION_PRESENT_QUERY *Record,
    const ADMISSION_PRESENT_EXPECTATION *Expected) {
  unsigned long long poolBase;
  unsigned long long previousPoolBase;
  if (Record == QUERY_NULL || Expected == QUERY_NULL ||
      Record->Magic != ADMISSION_PRESENT_QUERY_MAGIC ||
      Record->Version != ADMISSION_PRESENT_QUERY_VERSION ||
      Record->CandidateBuild != Expected->CandidateBuild ||
      Record->BootGeneration == 0u ||
      (Expected->BootGeneration != 0u &&
       Record->BootGeneration != Expected->BootGeneration) ||
      Record->Index != Expected->Index ||
      Record->PresentCount != Expected->Index + 1u ||
      Record->Purpose != AdmissionPresentPurposeRenderFrame ||
      Record->DestinationIndex != Expected->DestinationIndex ||
      Record->Fence == 0u || Record->Status != 0u || Record->Valid != 1u ||
      Record->Captured != 1u || Record->PublishedToQuery != 1u ||
      Record->ExpectedColor != Expected->ExpectedColor ||
      Record->PixelsExpected != Expected->PixelsExpected ||
      Record->PixelsVerified != Expected->PixelsExpected ||
      Record->Format != Expected->Format || Record->Width != Expected->Width ||
      Record->Height != Expected->Height || Record->Pitch != Expected->Pitch ||
      Record->AllocationToken == 0ULL || Record->Sequence == 0ULL ||
      Record->PhysicalAddress == 0ULL || Record->ContentHash == 0ULL ||
      (Expected->ExpectedContentHash != 0ULL &&
       Record->ContentHash != Expected->ExpectedContentHash) ||
      (Expected->ExpectedAllocationToken != 0ULL &&
       Record->AllocationToken != Expected->ExpectedAllocationToken) ||
      (Expected->ExpectedActiveOffset != 0ULL &&
       Record->ActiveOffset != Expected->ExpectedActiveOffset) ||
      (Expected->ExpectedPhysicalAddress != 0ULL &&
       Record->PhysicalAddress != Expected->ExpectedPhysicalAddress))
    return 0;
  if (Expected->PreviousFence == 0u)
    return Expected->PreviousAllocationToken == 0ULL &&
        Expected->PreviousSequence == 0ULL &&
        Expected->PreviousActiveOffset == 0ULL &&
        Expected->PreviousPhysicalAddress == 0ULL &&
        Expected->PreviousContentHash == 0ULL;
  if (Record->Fence == Expected->PreviousFence ||
      Record->AllocationToken == Expected->PreviousAllocationToken ||
      Record->Sequence <= Expected->PreviousSequence ||
      Record->ActiveOffset == Expected->PreviousActiveOffset ||
      Record->ContentHash == Expected->PreviousContentHash ||
      Record->PhysicalAddress < Record->ActiveOffset ||
      Expected->PreviousPhysicalAddress < Expected->PreviousActiveOffset)
    return 0;
  poolBase = Record->PhysicalAddress - Record->ActiveOffset;
  previousPoolBase = Expected->PreviousPhysicalAddress -
      Expected->PreviousActiveOffset;
  return poolBase == previousPoolBase;
}

ADMISSION_PRESENT_WAIT_RESULT AdmissionPresentWaitClassify(
    int QuerySucceeded, int RecordAccepted, int SawInvalidRecord,
    int DeadlineExpired) {
  if (!QuerySucceeded)
    return AdmissionPresentWaitQueryFailed;
  if (RecordAccepted)
    return AdmissionPresentWaitCompleted;
  if (!DeadlineExpired)
    return AdmissionPresentWaitTimedOut;
  return SawInvalidRecord ? AdmissionPresentWaitInvalidRecord
                          : AdmissionPresentWaitTimedOut;
}

void AdmissionPresentProducerInitialize(
    ADMISSION_PRESENT_PRODUCER_STATE *State, int HoldNoCleanup,
    unsigned int TargetFrames) {
  if (State == QUERY_NULL)
    return;
  query_zero(State, sizeof(*State));
  State->HoldNoCleanup = HoldNoCleanup ? 1u : 0u;
  State->TargetFrames = TargetFrames;
  State->CleanupAllowed = HoldNoCleanup ? 0u : 1u;
  if (TargetFrames == 0u || TargetFrames > ADMISSION_PRESENT_QUERY_CAPACITY)
    State->Terminal = 1u;
}

ADMISSION_PRESENT_PRODUCER_ACTION AdmissionPresentProducerAfterWait(
    ADMISSION_PRESENT_PRODUCER_STATE *State,
    ADMISSION_PRESENT_WAIT_RESULT Result) {
  if (State == QUERY_NULL || State->Terminal != 0u ||
      State->TargetFrames == 0u ||
      State->TargetFrames > ADMISSION_PRESENT_QUERY_CAPACITY)
    return AdmissionPresentProducerPreserveForRecovery;
  if (Result != AdmissionPresentWaitCompleted) {
    State->Terminal = 1u;
    return State->HoldNoCleanup
        ? AdmissionPresentProducerPreserveForRecovery
        : AdmissionPresentProducerCleanup;
  }
  ++State->CompletedFrames;
  if (State->CompletedFrames < State->TargetFrames)
    return AdmissionPresentProducerSubmitNextFrame;
  State->Terminal = 1u;
  if (State->CompletedFrames == State->TargetFrames &&
      State->HoldNoCleanup)
    return AdmissionPresentProducerBeginHold;
  return AdmissionPresentProducerCleanup;
}

int AdmissionPresentProducerCanCleanup(
    const ADMISSION_PRESENT_PRODUCER_STATE *State, int HasAllocations) {
  if (State == QUERY_NULL)
    return 0;
  return !HasAllocations || !State->HoldNoCleanup || State->CleanupAllowed;
}

int AdmissionPresentProducerRetirementComplete(
    ADMISSION_PRESENT_PRODUCER_STATE *State) {
  if (State == QUERY_NULL || !State->HoldNoCleanup || !State->Terminal ||
      State->CompletedFrames != State->TargetFrames ||
      State->CleanupAllowed)
    return 0;
  State->CleanupAllowed = 1u;
  return 1;
}

int AdmissionRetirementQueryBuild(
    ADMISSION_RETIREMENT_QUERY *Record, unsigned int CandidateBuild,
    unsigned int BootGeneration, unsigned long long Sequence,
    unsigned long long AllocationToken, unsigned long long ActiveOffset,
    unsigned long long PhysicalAddress) {
  if (Record == QUERY_NULL || CandidateBuild == 0u || BootGeneration == 0u ||
      Sequence == 0ULL || AllocationToken == 0ULL || ActiveOffset != 0ULL ||
      PhysicalAddress == 0ULL)
    return 0;
  query_zero(Record, sizeof(*Record));
  Record->Magic = ADMISSION_RETIREMENT_QUERY_MAGIC;
  Record->Version = ADMISSION_RETIREMENT_QUERY_VERSION;
  Record->Command = AdmissionRetirementCommandExecute;
  Record->Status = 0u;
  Record->CandidateBuild = CandidateBuild;
  Record->BootGeneration = BootGeneration;
  Record->Purpose = AdmissionPresentPurposeFallback;
  Record->Valid = 1u;
  Record->Sequence = Sequence;
  Record->AllocationToken = AllocationToken;
  Record->ActiveOffset = ActiveOffset;
  Record->PhysicalAddress = PhysicalAddress;
  return 1;
}

int AdmissionRetirementQueryAccept(
    const ADMISSION_RETIREMENT_QUERY *Record,
    const ADMISSION_RETIREMENT_EXPECTATION *Expected) {
  return Record != QUERY_NULL && Expected != QUERY_NULL &&
      Record->Magic == ADMISSION_RETIREMENT_QUERY_MAGIC &&
      Record->Version == ADMISSION_RETIREMENT_QUERY_VERSION &&
      Record->Command == AdmissionRetirementCommandExecute &&
      Record->Status == 0u && Record->CandidateBuild == Expected->CandidateBuild &&
      Record->BootGeneration == Expected->BootGeneration &&
      Record->Purpose == AdmissionPresentPurposeFallback && Record->Valid == 1u &&
      Record->Sequence > Expected->PreviousSequence &&
      Record->AllocationToken != 0ULL &&
      Record->AllocationToken != Expected->RenderAllocation0 &&
      Record->AllocationToken != Expected->RenderAllocation1 &&
      Record->ActiveOffset == 0ULL &&
      Record->PhysicalAddress == Expected->ExpectedPoolPhysical;
}

#undef QUERY_NULL
