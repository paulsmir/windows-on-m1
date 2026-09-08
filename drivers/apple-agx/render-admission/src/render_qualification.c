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
       Record->ContentHash != Expected->ExpectedContentHash))
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
    ADMISSION_PRESENT_PRODUCER_STATE *State, int HoldNoCleanup) {
  if (State == QUERY_NULL)
    return;
  query_zero(State, sizeof(*State));
  State->HoldNoCleanup = HoldNoCleanup ? 1u : 0u;
  State->CleanupAllowed = HoldNoCleanup ? 0u : 1u;
}

ADMISSION_PRESENT_PRODUCER_ACTION AdmissionPresentProducerAfterWait(
    ADMISSION_PRESENT_PRODUCER_STATE *State,
    ADMISSION_PRESENT_WAIT_RESULT Result) {
  if (State == QUERY_NULL || State->Terminal != 0u)
    return AdmissionPresentProducerPreserveForRecovery;
  if (Result != AdmissionPresentWaitCompleted) {
    State->Terminal = 1u;
    return State->HoldNoCleanup
        ? AdmissionPresentProducerPreserveForRecovery
        : AdmissionPresentProducerCleanup;
  }
  ++State->CompletedFrames;
  if (State->CompletedFrames == 1u)
    return AdmissionPresentProducerSubmitNextFrame;
  State->Terminal = 1u;
  if (State->CompletedFrames == ADMISSION_PRESENT_QUERY_CAPACITY &&
      State->HoldNoCleanup)
    return AdmissionPresentProducerBeginHold;
  return AdmissionPresentProducerCleanup;
}

#undef QUERY_NULL
