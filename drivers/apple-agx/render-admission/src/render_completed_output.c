#include "render_completed_output.h"

#define COMPLETED_NULL ((void *)0)

static void completed_zero(void *Data, unsigned int Bytes) {
  unsigned char *data = (unsigned char *)Data;
  while (Bytes-- != 0u)
    *data++ = 0u;
}

static int view_equal(const ADMISSION_BACKEND_OUTPUT_VIEW *A,
                      const ADMISSION_BACKEND_OUTPUT_VIEW *B) {
  return A != COMPLETED_NULL && B != COMPLETED_NULL &&
      A->AllocationCpuAddress == B->AllocationCpuAddress &&
      A->AllocationGpuAddress == B->AllocationGpuAddress &&
      A->AllocationPhysicalAddress == B->AllocationPhysicalAddress &&
      A->AllocationBytes == B->AllocationBytes &&
      A->RenderedCpuAddress == B->RenderedCpuAddress &&
      A->RenderedGpuAddress == B->RenderedGpuAddress &&
      A->RenderedPhysicalAddress == B->RenderedPhysicalAddress &&
      A->RenderedOffset == B->RenderedOffset &&
      A->RenderedBytes == B->RenderedBytes &&
      A->AllocationWidth == B->AllocationWidth &&
      A->AllocationHeight == B->AllocationHeight &&
      A->AllocationPitch == B->AllocationPitch &&
      A->AllocationFormat == B->AllocationFormat &&
      A->RenderWidth == B->RenderWidth &&
      A->RenderHeight == B->RenderHeight &&
      A->RenderPitch == B->RenderPitch &&
      A->ExpectedColor == B->ExpectedColor &&
      A->Framebuffer == B->Framebuffer;
}

static int owner_valid(const ADMISSION_ALLOCATION_OBJECT *Owner,
                       const ADMISSION_BACKEND_OUTPUT_VIEW *View) {
  return Owner != COMPLETED_NULL && View != COMPLETED_NULL &&
      Owner->Magic == ADMISSION_ALLOCATION_OBJECT_MAGIC &&
      Owner->Description.Size == View->AllocationBytes &&
      Owner->Description.Width == View->AllocationWidth &&
      Owner->Description.Height == View->AllocationHeight &&
      Owner->Description.Pitch == View->AllocationPitch &&
      Owner->Description.Format == View->AllocationFormat &&
      View->AllocationCpuAddress != COMPLETED_NULL &&
      View->AllocationGpuAddress != 0ULL &&
      View->AllocationPhysicalAddress != 0ULL &&
      View->AllocationBytes != 0u && View->RenderedCpuAddress != COMPLETED_NULL &&
      View->RenderedBytes != 0u &&
      View->RenderedOffset <= View->AllocationBytes &&
      View->RenderedBytes <= View->AllocationBytes - View->RenderedOffset;
}

void AdmissionCompletedOutputInitialize(ADMISSION_COMPLETED_OUTPUT *State) {
  if (State == COMPLETED_NULL)
    return;
  completed_zero(State, sizeof(*State));
  State->Version = ADMISSION_COMPLETED_OUTPUT_VERSION;
}

void AdmissionDisplayOutputLeaseInitialize(ADMISSION_DISPLAY_OUTPUT_LEASE *Lease) {
  if (Lease == COMPLETED_NULL)
    return;
  completed_zero(Lease, sizeof(*Lease));
  Lease->Version = ADMISSION_COMPLETED_OUTPUT_VERSION;
}

int AdmissionCompletedOutputCapture(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Generation,
    unsigned int Fence, const ADMISSION_BACKEND_OUTPUT_VIEW *View,
    ADMISSION_ALLOCATION_OBJECT *Owner) {
  if (State == COMPLETED_NULL || Generation == 0u || Fence == 0u ||
      !owner_valid(Owner, View) ||
      State->Version != ADMISSION_COMPLETED_OUTPUT_VERSION)
    return 0;
  if (State->Phase != AdmissionCompletedOutputEmpty)
    return State->Generation == Generation && State->Fence == Fence &&
        State->Owner == Owner && view_equal(&State->View, View);
  if (!AdmissionAllocationOpen(Owner))
    return 0;
  State->Phase = AdmissionCompletedOutputCaptured;
  State->Generation = Generation;
  State->Fence = Fence;
  State->CaptureCount = 1u;
  State->View = *View;
  State->Owner = Owner;
  return 1;
}

int AdmissionCompletedOutputMarkReleased(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence) {
  if (State == COMPLETED_NULL || State->Fence != Fence)
    return 0;
  if (State->Phase == AdmissionCompletedOutputBackendReleased)
    return State->ReleaseCount == 1u;
  if (State->Phase != AdmissionCompletedOutputCaptured)
    return 0;
  State->Phase = AdmissionCompletedOutputBackendReleased;
  State->ReleaseCount = 1u;
  return 1;
}

int AdmissionCompletedOutputMarkPacketRetired(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence) {
  if (State == COMPLETED_NULL || State->Fence != Fence)
    return 0;
  if (State->Phase == AdmissionCompletedOutputPacketRetired)
    return 1;
  if (State->Phase != AdmissionCompletedOutputBackendReleased)
    return 0;
  State->Phase = AdmissionCompletedOutputPacketRetired;
  return 1;
}

int AdmissionCompletedOutputMarkNotified(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence) {
  if (State == COMPLETED_NULL || State->Fence != Fence)
    return 0;
  if (State->Phase == AdmissionCompletedOutputNotified)
    return State->NotifyCount == 1u;
  if (State->Phase != AdmissionCompletedOutputPacketRetired)
    return 0;
  State->Phase = AdmissionCompletedOutputNotified;
  State->NotifyCount = 1u;
  return 1;
}

int AdmissionCompletedOutputBeginPresent(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence) {
  if (State == COMPLETED_NULL || State->Fence != Fence ||
      State->Phase != AdmissionCompletedOutputNotified ||
      State->PresentCount != 0u || State->AccessAttempted != 1u ||
      State->AccessStatus != 0u)
    return 0;
  State->Phase = AdmissionCompletedOutputPresenting;
  State->PresentCount = 1u;
  return 1;
}

int AdmissionCompletedOutputMarkPublished(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence,
    unsigned long long Sequence) {
  if (State == COMPLETED_NULL || State->Fence != Fence || Sequence == 0ULL ||
      State->Phase != AdmissionCompletedOutputPresenting ||
      State->PresentationSequence != 0ULL)
    return 0;
  State->Phase = AdmissionCompletedOutputPublishedPending;
  State->PresentationSequence = Sequence;
  return 1;
}

int AdmissionCompletedOutputMarkLatched(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence,
    unsigned long long Sequence) {
  if (State == COMPLETED_NULL || State->Fence != Fence || Sequence == 0ULL ||
      State->Phase != AdmissionCompletedOutputPublishedPending ||
      State->PresentationSequence != Sequence)
    return 0;
  State->Phase = AdmissionCompletedOutputLatched;
  return 1;
}

int AdmissionCompletedOutputMarkOwnershipUnknown(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence,
    unsigned long long Sequence, unsigned int Status) {
  if (State == COMPLETED_NULL || State->Fence != Fence || Sequence == 0ULL ||
      Status == 0u ||
      (State->Phase != AdmissionCompletedOutputPresenting &&
       State->Phase != AdmissionCompletedOutputPublishedPending &&
       State->Phase != AdmissionCompletedOutputLatched) ||
      (State->PresentationSequence != 0ULL &&
       State->PresentationSequence != Sequence))
    return 0;
  State->Phase = AdmissionCompletedOutputOwnershipUnknown;
  State->PresentationSequence = Sequence;
  State->PresentationAttempted = 1u;
  State->PresentationStatus = Status;
  return 1;
}

int AdmissionCompletedOutputResolveUnknown(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence,
    unsigned long long Sequence) {
  if (State == COMPLETED_NULL || State->Fence != Fence || Sequence == 0ULL ||
      State->Phase != AdmissionCompletedOutputOwnershipUnknown ||
      State->PresentationSequence != Sequence || State->Owner == COMPLETED_NULL ||
      !AdmissionAllocationClose(State->Owner))
    return 0;
  AdmissionCompletedOutputInitialize(State);
  return 1;
}

int AdmissionCompletedOutputRecordAccess(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence,
    unsigned int Status) {
  if (State == COMPLETED_NULL || State->Fence != Fence ||
      State->Phase != AdmissionCompletedOutputNotified ||
      State->AccessAttempted != 0u)
    return 0;
  State->AccessAttempted = 1u;
  State->AccessStatus = Status;
  return 1;
}

int AdmissionCompletedOutputRecordPresentation(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence,
    unsigned int Status) {
  if (State == COMPLETED_NULL || State->Fence != Fence ||
      State->PresentationAttempted != 0u ||
      (Status == 0u
           ? State->Phase != AdmissionCompletedOutputLatched
           : State->Phase != AdmissionCompletedOutputPresenting))
    return 0;
  State->PresentationAttempted = 1u;
  State->PresentationStatus = Status;
  return 1;
}

int AdmissionCompletedOutputContains(
    const ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence,
    const void *Address, unsigned int Bytes) {
  const unsigned char *base;
  const unsigned char *value;
  unsigned long long offset;
  if (State == COMPLETED_NULL || Address == COMPLETED_NULL || Bytes == 0u ||
      State->Version != ADMISSION_COMPLETED_OUTPUT_VERSION ||
      State->Phase == AdmissionCompletedOutputEmpty || State->Fence != Fence ||
      !owner_valid(State->Owner, &State->View) || State->Owner->OpenCount == 0u)
    return 0;
  base = (const unsigned char *)State->View.RenderedCpuAddress;
  value = (const unsigned char *)Address;
  if (value < base)
    return 0;
  offset = value - base;
  return offset <= State->View.RenderedBytes &&
      Bytes <= State->View.RenderedBytes - offset;
}

int AdmissionCompletedOutputPlatformRangeValid(
    const ADMISSION_BACKEND_OUTPUT_VIEW *View, const void *PoolCpuAddress,
    unsigned long long PoolGpuAddress,
    unsigned long long PoolPhysicalAddress,
    unsigned long long PoolBytes) {
  const unsigned char *cpu;
  const unsigned char *pool;
  unsigned long long cpuOffset;
  unsigned long long gpuOffset;
  unsigned long long physicalOffset;
  if (View == COMPLETED_NULL || PoolCpuAddress == COMPLETED_NULL ||
      View->AllocationCpuAddress == COMPLETED_NULL || PoolBytes == 0ULL ||
      (const unsigned char *)View->AllocationCpuAddress <
          (const unsigned char *)PoolCpuAddress ||
      View->AllocationGpuAddress < PoolGpuAddress ||
      View->AllocationPhysicalAddress < PoolPhysicalAddress)
    return 0;
  cpu = (const unsigned char *)View->AllocationCpuAddress;
  pool = (const unsigned char *)PoolCpuAddress;
  cpuOffset = (unsigned long long)(cpu - pool);
  gpuOffset = View->AllocationGpuAddress - PoolGpuAddress;
  physicalOffset = View->AllocationPhysicalAddress - PoolPhysicalAddress;
  return cpuOffset == gpuOffset && cpuOffset == physicalOffset &&
      cpuOffset <= PoolBytes &&
      View->AllocationBytes <= PoolBytes - cpuOffset;
}

int AdmissionCompletedOutputTransferToDisplay(
    ADMISSION_COMPLETED_OUTPUT *State,
    ADMISSION_DISPLAY_OUTPUT_LEASE *Lease, unsigned int Fence) {
  ADMISSION_DISPLAY_OUTPUT_LEASE next;
  if (State == COMPLETED_NULL || Lease == COMPLETED_NULL ||
      State->Phase != AdmissionCompletedOutputLatched ||
      State->Fence != Fence || State->Owner == COMPLETED_NULL ||
      State->PresentationAttempted != 1u ||
      State->PresentationStatus != 0u ||
      Lease->Version != ADMISSION_COMPLETED_OUTPUT_VERSION ||
      (Lease->Active && Lease->Owner == State->Owner))
    return 0;
  next.Version = ADMISSION_COMPLETED_OUTPUT_VERSION;
  next.Active = 1u;
  next.Generation = State->Generation;
  next.Fence = State->Fence;
  next.View = State->View;
  next.Owner = State->Owner;
  if (Lease->Active && !AdmissionAllocationClose(Lease->Owner))
    return 0;
  *Lease = next;
  AdmissionCompletedOutputInitialize(State);
  return 1;
}

int AdmissionCompletedOutputAbort(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence) {
  if (State == COMPLETED_NULL || State->Phase == AdmissionCompletedOutputEmpty ||
      State->Phase == AdmissionCompletedOutputPublishedPending ||
      State->Phase == AdmissionCompletedOutputLatched ||
      State->Phase == AdmissionCompletedOutputOwnershipUnknown ||
      State->Fence != Fence || State->Owner == COMPLETED_NULL ||
      !AdmissionAllocationClose(State->Owner))
    return 0;
  AdmissionCompletedOutputInitialize(State);
  return 1;
}

int AdmissionDisplayOutputLeaseAllowsRender(
    const ADMISSION_DISPLAY_OUTPUT_LEASE *Lease,
    const ADMISSION_ALLOCATION_OBJECT *Owner) {
  return Lease != COMPLETED_NULL && Owner != COMPLETED_NULL &&
      Lease->Version == ADMISSION_COMPLETED_OUTPUT_VERSION &&
      (!Lease->Active || Lease->Owner != Owner);
}

int AdmissionDisplayOutputLeaseMatches(
    const ADMISSION_DISPLAY_OUTPUT_LEASE *Lease,
    const ADMISSION_ALLOCATION_OBJECT *Owner) {
  return Lease != COMPLETED_NULL && Owner != COMPLETED_NULL && Lease->Active &&
      Lease->Version == ADMISSION_COMPLETED_OUTPUT_VERSION &&
      Lease->Owner == Owner;
}

int AdmissionDisplayOutputLeaseRetire(ADMISSION_DISPLAY_OUTPUT_LEASE *Lease) {
  if (Lease == COMPLETED_NULL || !Lease->Active || Lease->Owner == COMPLETED_NULL ||
      !AdmissionAllocationClose(Lease->Owner))
    return 0;
  AdmissionDisplayOutputLeaseInitialize(Lease);
  return 1;
}

int AdmissionDisplayOutputLeaseCapture(
    ADMISSION_DISPLAY_OUTPUT_LEASE *Lease, unsigned int Generation,
    unsigned int Fence, const ADMISSION_BACKEND_OUTPUT_VIEW *View,
    ADMISSION_ALLOCATION_OBJECT *Owner) {
  if (Lease == COMPLETED_NULL || Generation == 0u ||
      Lease->Version != ADMISSION_COMPLETED_OUTPUT_VERSION || Lease->Active ||
      !owner_valid(Owner, View) || !AdmissionAllocationOpen(Owner))
    return 0;
  Lease->Active = 1u;
  Lease->Generation = Generation;
  Lease->Fence = Fence;
  Lease->View = *View;
  Lease->Owner = Owner;
  return 1;
}

int AdmissionDisplayOutputLeaseMove(
    ADMISSION_DISPLAY_OUTPUT_LEASE *Destination,
    ADMISSION_DISPLAY_OUTPUT_LEASE *Source) {
  ADMISSION_DISPLAY_OUTPUT_LEASE next;
  if (Destination == COMPLETED_NULL || Source == COMPLETED_NULL ||
      Destination == Source || !Source->Active || Source->Owner == COMPLETED_NULL ||
      Source->Version != ADMISSION_COMPLETED_OUTPUT_VERSION ||
      Destination->Version != ADMISSION_COMPLETED_OUTPUT_VERSION ||
      (Destination->Active && Destination->Owner == Source->Owner))
    return 0;
  next = *Source;
  if (Destination->Active && !AdmissionAllocationClose(Destination->Owner))
    return 0;
  *Destination = next;
  AdmissionDisplayOutputLeaseInitialize(Source);
  return 1;
}

#undef COMPLETED_NULL
