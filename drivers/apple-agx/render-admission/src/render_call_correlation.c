#include "render_call_correlation.h"

#define CORRELATION_NULL ((void *)0)

static void correlation_zero(void *Data, unsigned int Bytes) {
  unsigned char *data = (unsigned char *)Data;
  while (Bytes-- != 0u)
    *data++ = 0u;
}

static ADMISSION_RENDER_CORRELATION_SLOT *correlation_sequence(
    ADMISSION_RENDER_CORRELATION_STATE *State, unsigned int Sequence) {
  if (State == CORRELATION_NULL || Sequence == 0u ||
      Sequence > State->Count || Sequence > ADMISSION_RENDER_CORRELATION_CAPACITY)
    return CORRELATION_NULL;
  return &State->Slot[Sequence - 1u];
}

static ADMISSION_RENDER_CORRELATION_SLOT *correlation_context(
    ADMISSION_RENDER_CORRELATION_STATE *State,
    unsigned long long ContextToken) {
  unsigned int index;
  if (State == CORRELATION_NULL || ContextToken == 0ULL)
    return CORRELATION_NULL;
  for (index = 0u; index < State->Count; ++index)
    if (State->Slot[index].ContextToken == ContextToken &&
        !(State->Slot[index].ValidMask & ADMISSION_RENDER_CAPTURE_WORKER_EXIT))
      return &State->Slot[index];
  return CORRELATION_NULL;
}

int AdmissionRenderCorrelationInitialize(
    ADMISSION_RENDER_CORRELATION_STATE *State,
    unsigned int CandidateBuild, unsigned int BootGeneration) {
  if (State == CORRELATION_NULL || CandidateBuild == 0u || BootGeneration == 0u)
    return 0;
  if (State->Version == ADMISSION_RENDER_CORRELATION_VERSION &&
      State->Bytes == sizeof(*State) &&
      State->CandidateBuild == CandidateBuild &&
      State->BootGeneration == BootGeneration)
    return 1;
  correlation_zero(State, sizeof(*State));
  State->Version = ADMISSION_RENDER_CORRELATION_VERSION;
  State->Bytes = sizeof(*State);
  State->CandidateBuild = CandidateBuild;
  State->BootGeneration = BootGeneration;
  return 1;
}

int AdmissionRenderCorrelationBegin(
    ADMISSION_RENDER_CORRELATION_STATE *State,
    unsigned long long Timestamp, unsigned long long AdapterToken,
    unsigned long long ContextToken, unsigned int CommandLength,
    unsigned int *CallSequence) {
  ADMISSION_RENDER_CORRELATION_SLOT *slot;
  if (State == CORRELATION_NULL || CallSequence == CORRELATION_NULL ||
      Timestamp == 0ULL ||
      AdapterToken == 0ULL || ContextToken == 0ULL || CommandLength == 0u ||
      State->Version != ADMISSION_RENDER_CORRELATION_VERSION)
    return 0;
  if (State->Count >= ADMISSION_RENDER_CORRELATION_CAPACITY) {
    State->Overflow = 1u;
    ++State->CapturedGeneration;
    return 0;
  }
  slot = &State->Slot[State->Count];
  correlation_zero(slot, sizeof(*slot));
  slot->Version = ADMISSION_RENDER_CORRELATION_VERSION;
  slot->Bytes = sizeof(*slot);
  slot->CandidateBuild = State->CandidateBuild;
  slot->BootGeneration = State->BootGeneration;
  slot->CallSequence = State->Count + 1u;
  slot->ValidMask = ADMISSION_RENDER_CAPTURE_ENTRY;
  slot->EntryTimestamp = Timestamp;
  slot->AdapterToken = AdapterToken;
  slot->ContextToken = ContextToken;
  slot->CommandLength = CommandLength;
  ++State->Count;
  ++State->CapturedGeneration;
  *CallSequence = slot->CallSequence;
  return 1;
}

int AdmissionRenderCorrelationValidated(
    ADMISSION_RENDER_CORRELATION_STATE *State, unsigned int CallSequence,
    unsigned long long CommandHash, unsigned int AllocationCount,
    unsigned int DestinationIndex, unsigned int DestinationSegment,
    const unsigned long long AllocationToken[2]) {
  ADMISSION_RENDER_CORRELATION_SLOT *slot =
      correlation_sequence(State, CallSequence);
  if (slot == CORRELATION_NULL || CommandHash == 0ULL ||
      AllocationToken == CORRELATION_NULL || AllocationCount != 2u ||
      DestinationIndex >= AllocationCount || AllocationToken[0] == 0ULL ||
      AllocationToken[1] == 0ULL ||
      (slot->ValidMask & ADMISSION_RENDER_CAPTURE_EXIT) != 0u)
    return 0;
  slot->CommandHash = CommandHash;
  slot->AllocationCount = AllocationCount;
  slot->DestinationIndex = DestinationIndex;
  slot->DestinationSegment = DestinationSegment;
  slot->AllocationToken[0] = AllocationToken[0];
  slot->AllocationToken[1] = AllocationToken[1];
  slot->ValidMask |= ADMISSION_RENDER_CAPTURE_VALIDATED;
  ++State->CapturedGeneration;
  return 1;
}

int AdmissionRenderCorrelationExit(
    ADMISSION_RENDER_CORRELATION_STATE *State, unsigned int CallSequence,
    unsigned long long Timestamp, unsigned int Guard, unsigned int Status,
    unsigned int DmaBytesProduced, unsigned int PatchesProduced,
    unsigned int Prepatched) {
  ADMISSION_RENDER_CORRELATION_SLOT *slot =
      correlation_sequence(State, CallSequence);
  if (slot == CORRELATION_NULL || Timestamp < slot->EntryTimestamp ||
      (slot->ValidMask & ADMISSION_RENDER_CAPTURE_EXIT) != 0u ||
      Prepatched > 1u)
    return 0;
  slot->ExitTimestamp = Timestamp;
  slot->RenderGuard = Guard;
  slot->RenderStatus = Status;
  slot->DmaBytesProduced = DmaBytesProduced;
  slot->PatchesProduced = PatchesProduced;
  slot->Prepatched = Prepatched;
  slot->ValidMask |= ADMISSION_RENDER_CAPTURE_EXIT;
  ++State->CapturedGeneration;
  return 1;
}

int AdmissionRenderCorrelationPatch(
    ADMISSION_RENDER_CORRELATION_STATE *State,
    unsigned long long ContextToken, unsigned int Entry,
    unsigned int Guard, unsigned int Status) {
  ADMISSION_RENDER_CORRELATION_SLOT *slot =
      correlation_context(State, ContextToken);
  if (slot == CORRELATION_NULL || Entry > 1u)
    return 0;
  if (Entry)
    slot->ValidMask |= ADMISSION_RENDER_CAPTURE_PATCH_ENTRY;
  else {
    slot->PatchGuard = Guard;
    slot->PatchStatus = Status;
    slot->ValidMask |= ADMISSION_RENDER_CAPTURE_PATCH_EXIT;
  }
  ++State->CapturedGeneration;
  return 1;
}

int AdmissionRenderCorrelationSubmit(
    ADMISSION_RENDER_CORRELATION_STATE *State,
    unsigned long long ContextToken, unsigned int Entry,
    unsigned int Fence, unsigned int Guard, unsigned int Status) {
  ADMISSION_RENDER_CORRELATION_SLOT *slot =
      correlation_context(State, ContextToken);
  if (slot == CORRELATION_NULL || Entry > 1u || Fence == 0u)
    return 0;
  slot->Fence = Fence;
  if (Entry)
    slot->ValidMask |= ADMISSION_RENDER_CAPTURE_SUBMIT_ENTRY;
  else {
    slot->SubmitGuard = Guard;
    slot->SubmitStatus = Status;
    slot->ValidMask |= ADMISSION_RENDER_CAPTURE_SUBMIT_EXIT;
  }
  ++State->CapturedGeneration;
  return 1;
}

int AdmissionRenderCorrelationWorker(
    ADMISSION_RENDER_CORRELATION_STATE *State, unsigned int Fence,
    unsigned int Entry, unsigned int Status) {
  unsigned int index;
  if (State == CORRELATION_NULL || Fence == 0u || Entry > 1u)
    return 0;
  for (index = 0u; index < State->Count; ++index) {
    if (State->Slot[index].Fence != Fence)
      continue;
    State->Slot[index].WorkerStatus = Status;
    State->Slot[index].ValidMask |= Entry
        ? ADMISSION_RENDER_CAPTURE_WORKER_ENTRY
        : ADMISSION_RENDER_CAPTURE_WORKER_EXIT;
    ++State->CapturedGeneration;
    return 1;
  }
  return 0;
}

int AdmissionRenderCorrelationOutput(
    ADMISSION_RENDER_CORRELATION_STATE *State, unsigned int Fence,
    unsigned int Stage, unsigned int Status, unsigned int Processor,
    unsigned int Irql, unsigned long long Timestamp) {
  unsigned int index;
  if (State == CORRELATION_NULL || Fence == 0u ||
      State->Version != ADMISSION_RENDER_CORRELATION_VERSION ||
      State->Count > ADMISSION_RENDER_CORRELATION_CAPACITY ||
      Stage >= ADMISSION_OUTPUT_TRACE_COUNT || Timestamp == 0ULL)
    return 0;
  for (index = 0u; index < State->Count; ++index) {
    ADMISSION_RENDER_CORRELATION_SLOT *slot = &State->Slot[index];
    ADMISSION_OUTPUT_TRACE *record = &slot->Output[Stage];
    if (slot->Fence != Fence)
      continue;
    if (record->Valid || (Stage != AdmissionOutputTraceEntry &&
        (!slot->Output[Stage - 1u].Valid ||
         Timestamp < slot->Output[Stage - 1u].Timestamp)))
      return 0;
    record->Status = Status;
    record->Processor = Processor;
    record->Irql = Irql;
    record->Timestamp = Timestamp;
    record->Valid = 1u;
    ++State->CapturedGeneration;
    return 1;
  }
  return 0;
}

int AdmissionRenderCorrelationMarkExport(
    ADMISSION_RENDER_CORRELATION_STATE *State,
    unsigned int CapturedGeneration, unsigned int Status,
    unsigned int Durable) {
  if (State == CORRELATION_NULL || CapturedGeneration == 0u ||
      CapturedGeneration > State->CapturedGeneration || Durable > 1u)
    return 0;
  State->ExportAttempted = 1u;
  State->ExportStatus = Status;
  State->Durable = Status == 0u ? Durable : 0u;
  if (Status == 0u)
    State->ExportedGeneration = CapturedGeneration;
  return 1;
}
