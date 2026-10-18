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

void AdmissionStandardPresentTraceInitialize(
    ADMISSION_STANDARD_PRESENT_TRACE *Trace,
    ADMISSION_STANDARD_PRESENT_TRACE_COMMAND Command,
    unsigned int CandidateBuild, unsigned int BootGeneration) {
  if (Trace == QUERY_NULL)
    return;
  query_zero(Trace, sizeof(*Trace));
  Trace->Magic = ADMISSION_STANDARD_PRESENT_TRACE_MAGIC;
  Trace->Version = ADMISSION_STANDARD_PRESENT_TRACE_VERSION;
  Trace->Bytes = sizeof(*Trace);
  Trace->Command = Command;
  Trace->CandidateBuild = CandidateBuild;
  Trace->BootGeneration = BootGeneration;
}

int AdmissionStandardPresentTraceAppend(
    ADMISSION_STANDARD_PRESENT_TRACE *Trace,
    const ADMISSION_STANDARD_PRESENT_EVENT *Event) {
  ADMISSION_STANDARD_PRESENT_EVENT copy;
  if (Trace == QUERY_NULL || Event == QUERY_NULL ||
      Trace->Magic != ADMISSION_STANDARD_PRESENT_TRACE_MAGIC ||
      Trace->Version != ADMISSION_STANDARD_PRESENT_TRACE_VERSION ||
      Trace->Bytes != sizeof(*Trace) || Trace->CandidateBuild == 0u ||
      Trace->BootGeneration == 0u || Event->Sequence == 0u ||
      Event->Kind < AdmissionStandardPresentEventPresent ||
      Event->Kind > AdmissionStandardPresentEventSourceAddress ||
      Event->Phase < AdmissionStandardPresentPhaseEntry ||
      Event->Phase > AdmissionStandardPresentPhaseExit)
    return 0;
  if (Trace->EventCount >= ADMISSION_STANDARD_PRESENT_TRACE_CAPACITY) {
    Trace->Overflow = 1u;
    return 0;
  }
  copy = *Event;
  copy.Valid = 1u;
  Trace->Events[Trace->EventCount++] = copy;
  return 1;
}

int AdmissionStandardPresentTraceAccept(
    const ADMISSION_STANDARD_PRESENT_TRACE *Trace,
    const ADMISSION_STANDARD_PRESENT_EXPECTATION *Expected,
    unsigned int *PresentSequence,
    unsigned int *SourceAddressSequence) {
  unsigned int present = 0u;
  unsigned int source = 0u;
  unsigned int presentEntry = 0u;
  unsigned int sourceEntry = 0u;
  unsigned int previousSequence = 0u;
  unsigned long long presentAllocation = 0ULL;
  unsigned long long sourceAddress = 0ULL;
  unsigned int index;
  if (Trace == QUERY_NULL || Expected == QUERY_NULL ||
      PresentSequence == QUERY_NULL || SourceAddressSequence == QUERY_NULL ||
      Trace->Magic != ADMISSION_STANDARD_PRESENT_TRACE_MAGIC ||
      Trace->Version != ADMISSION_STANDARD_PRESENT_TRACE_VERSION ||
      Trace->Bytes != sizeof(*Trace) ||
      Trace->CandidateBuild != Expected->CandidateBuild ||
      Trace->BootGeneration != Expected->BootGeneration ||
      Trace->EventCount == 0u ||
      Trace->EventCount > ADMISSION_STANDARD_PRESENT_TRACE_CAPACITY ||
      Trace->Overflow != 0u)
    return 0;
  for (index = 0u; index < Trace->EventCount; ++index) {
    const ADMISSION_STANDARD_PRESENT_EVENT *event = &Trace->Events[index];
    if (event->Valid != 1u || event->Sequence <= previousSequence)
      return 0;
    previousSequence = event->Sequence;
    if (event->Kind == AdmissionStandardPresentEventPresent &&
        event->Phase == AdmissionStandardPresentPhaseEntry &&
        event->Flags == Expected->Flags && event->ContextToken != 0ULL &&
        (Expected->ContextToken == 0ULL ||
         event->ContextToken == Expected->ContextToken) &&
        event->NumSrc == Expected->NumSrc &&
        event->NumDst == Expected->NumDst)
      presentEntry = event->Sequence;
    if (event->Kind == AdmissionStandardPresentEventPresent &&
        event->Phase == AdmissionStandardPresentPhaseExit &&
        presentEntry != 0u && event->Sequence > presentEntry &&
        event->Status == 0u && event->Flags == Expected->Flags &&
        event->ContextToken != 0ULL && event->AllocationToken != 0ULL &&
        (Expected->ContextToken == 0ULL ||
         event->ContextToken == Expected->ContextToken) &&
        (Expected->AllocationToken == 0ULL ||
         event->AllocationToken == Expected->AllocationToken) &&
        event->NumSrc == Expected->NumSrc &&
        event->NumDst == Expected->NumDst)
      present = event->Sequence, presentAllocation = event->AllocationToken;
    if (present != 0u && event->Sequence > present &&
        event->Kind == AdmissionStandardPresentEventSourceAddress &&
        event->Phase == AdmissionStandardPresentPhaseEntry &&
        event->SourceId == Expected->SourceId &&
        event->Segment == Expected->Segment &&
        event->AllocationToken == presentAllocation &&
        event->PrimaryAddress != 0ULL) {
      sourceEntry = event->Sequence;
      sourceAddress = event->PrimaryAddress;
    }
    if (present != 0u && event->Sequence > present &&
        event->Kind == AdmissionStandardPresentEventSourceAddress &&
        event->Phase == AdmissionStandardPresentPhaseExit &&
        sourceEntry != 0u && event->Sequence > sourceEntry &&
        event->Status == 0u && event->SourceId == Expected->SourceId &&
        event->Segment == Expected->Segment &&
        event->AllocationToken == presentAllocation &&
        (Expected->AllocationToken == 0ULL ||
         event->AllocationToken == Expected->AllocationToken) &&
        event->PrimaryAddress == sourceAddress) {
      source = event->Sequence;
      break;
    }
  }
  if (present == 0u || source == 0u)
    return 0;
  *PresentSequence = present;
  *SourceAddressSequence = source;
  return 1;
}

int AdmissionStandardPresentTraceAcceptPresent(
    const ADMISSION_STANDARD_PRESENT_TRACE *Trace,
    const ADMISSION_STANDARD_PRESENT_EXPECTATION *Expected,
    unsigned int *PresentSequence,
    unsigned long long *ContextToken,
    unsigned long long *AllocationToken) {
  unsigned int entry = 0u;
  unsigned int previousSequence = 0u;
  unsigned long long entryContext = 0ULL;
  unsigned int index;
  if (Trace == QUERY_NULL || Expected == QUERY_NULL ||
      PresentSequence == QUERY_NULL || ContextToken == QUERY_NULL ||
      AllocationToken == QUERY_NULL ||
      Trace->Magic != ADMISSION_STANDARD_PRESENT_TRACE_MAGIC ||
      Trace->Version != ADMISSION_STANDARD_PRESENT_TRACE_VERSION ||
      Trace->Bytes != sizeof(*Trace) ||
      Trace->CandidateBuild != Expected->CandidateBuild ||
      Trace->BootGeneration != Expected->BootGeneration ||
      Trace->EventCount == 0u ||
      Trace->EventCount > ADMISSION_STANDARD_PRESENT_TRACE_CAPACITY ||
      Trace->Overflow != 0u)
    return 0;
  for (index = 0u; index < Trace->EventCount; ++index) {
    const ADMISSION_STANDARD_PRESENT_EVENT *event = &Trace->Events[index];
    if (event->Valid != 1u || event->Sequence <= previousSequence)
      return 0;
    previousSequence = event->Sequence;
    if (event->Kind == AdmissionStandardPresentEventPresent &&
        event->Phase == AdmissionStandardPresentPhaseEntry &&
        event->Flags == Expected->Flags && event->ContextToken != 0ULL &&
        (Expected->ContextToken == 0ULL ||
         event->ContextToken == Expected->ContextToken) &&
        event->NumSrc == Expected->NumSrc &&
        event->NumDst == Expected->NumDst) {
      entry = event->Sequence;
      entryContext = event->ContextToken;
    }
    if (entry != 0u && event->Sequence > entry &&
        event->Kind == AdmissionStandardPresentEventPresent &&
        event->Phase == AdmissionStandardPresentPhaseExit &&
        event->Status == 0u && event->Flags == Expected->Flags &&
        event->ContextToken == entryContext &&
        event->AllocationToken != 0ULL &&
        (Expected->AllocationToken == 0ULL ||
         event->AllocationToken == Expected->AllocationToken) &&
        event->NumSrc == Expected->NumSrc &&
        event->NumDst == Expected->NumDst) {
      *PresentSequence = event->Sequence;
      *ContextToken = event->ContextToken;
      *AllocationToken = event->AllocationToken;
      return 1;
    }
  }
  return 0;
}

void AdmissionStandardPresentProducerInitialize(
    ADMISSION_STANDARD_PRESENT_PRODUCER_STATE *State) {
  if (State != QUERY_NULL)
    query_zero(State, sizeof(*State));
}

int AdmissionStandardPresentProducerAdvance(
    ADMISSION_STANDARD_PRESENT_PRODUCER_STATE *State,
    ADMISSION_STANDARD_PRESENT_PRODUCER_STAGE Stage) {
  if (State == QUERY_NULL || State->Invalid != 0u ||
      State->OwnerReleased != 0u)
    return 0;
  switch (Stage) {
  case AdmissionStandardPresentOwnerAcquired:
    if (State->OwnerAcquired != 0u)
      break;
    State->OwnerAcquired = 1u;
    return 1;
  case AdmissionStandardPresentModeSet:
    if (State->OwnerAcquired == 0u || State->ModeSet != 0u)
      break;
    State->ModeSet = 1u;
    return 1;
  case AdmissionStandardPresentFrameConfirmed:
    if (State->ModeSet == 0u || State->FrameConfirmed != 0u)
      break;
    State->FrameConfirmed = 1u;
    return 1;
  case AdmissionStandardPresentFlipBackConfirmed:
    if (State->FrameConfirmed == 0u || State->FlipBackConfirmed != 0u)
      break;
    State->FlipBackConfirmed = 1u;
    return 1;
  case AdmissionStandardPresentOwnerReleased:
    if (State->OwnerAcquired == 0u ||
        (State->FrameConfirmed != 0u &&
         State->FlipBackConfirmed == 0u))
      break;
    State->OwnerReleased = 1u;
    return 1;
  default:
    State->Invalid = 1u;
    return 0;
  }
  return 0;
}

int AdmissionStandardPresentProducerCanCleanup(
    const ADMISSION_STANDARD_PRESENT_PRODUCER_STATE *State) {
  return State != QUERY_NULL && State->Invalid == 0u &&
      (State->OwnerAcquired == 0u || State->OwnerReleased != 0u);
}

#undef QUERY_NULL
