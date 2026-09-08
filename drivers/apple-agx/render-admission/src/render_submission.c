#include "render_submission.h"

#define ADMISSION_RENDER_NULL ((void *)0)

static void AdmissionRenderPacketClear(ADMISSION_RENDER_PACKET *Packet) {
  unsigned char *bytes = (unsigned char *)Packet;
  unsigned int index;

  for (index = 0u; index < (unsigned int)sizeof(*Packet); ++index)
    bytes[index] = 0u;
  Packet->Magic = ADMISSION_RENDER_PACKET_MAGIC;
  Packet->State = AdmissionRenderPacketEmpty;
}

static void AdmissionPrepatchedClear(ADMISSION_PREPATCHED_RENDER *State) {
  unsigned char *bytes = (unsigned char *)State;
  unsigned int index;
  for (index = 0u; index < (unsigned int)sizeof(*State); ++index)
    bytes[index] = 0u;
}

static int AdmissionVisibleDescriptionComplete(
    const ADMISSION_RENDER_PACKET_DESCRIPTION *Description) {
  unsigned int present = 0u;
  present += Description->VisibleDestinationCpuToken != 0ULL;
  present += Description->VisibleDestinationGpuVa != 0ULL;
  present += Description->VisibleDestinationPhysical != 0ULL;
  present += Description->VisibleDestinationAllocationToken != 0ULL;
  present += Description->VisibleDestinationBytes != 0u;
  return present == 0u || present == 5u;
}

int AdmissionNonPagingPrivateRangeCovers(
    unsigned int PrivateBytesUsed, unsigned int SubmissionStart,
    unsigned int SubmissionEnd, unsigned int PrivateBufferBytes) {
  if (PrivateBytesUsed == 0u || PrivateBytesUsed > PrivateBufferBytes ||
      SubmissionStart != 0u || SubmissionEnd > PrivateBufferBytes)
    return 0;
  return SubmissionEnd == 0u || SubmissionEnd >= PrivateBytesUsed;
}

static int AdmissionRenderFenceAtOrAfter(unsigned int Candidate,
                                         unsigned int Reference) {
  unsigned int distance = Candidate - Reference;
  return Candidate == Reference ||
         (distance != 0u && distance < 0x80000000u);
}

void AdmissionRenderPacketInitialize(ADMISSION_RENDER_PACKET *Packet) {
  if (Packet != ADMISSION_RENDER_NULL)
    AdmissionRenderPacketClear(Packet);
}

ADMISSION_RENDER_PACKET_STATE AdmissionRenderPacketState(
    const ADMISSION_RENDER_PACKET *Packet) {
  return Packet == ADMISSION_RENDER_NULL ||
                 Packet->Magic != ADMISSION_RENDER_PACKET_MAGIC
             ? AdmissionRenderPacketEmpty
             : Packet->State;
}

int AdmissionRenderPacketPrepare(
    ADMISSION_RENDER_PACKET *Packet,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *Description) {
  if (Packet == ADMISSION_RENDER_NULL ||
      Description == ADMISSION_RENDER_NULL ||
      Packet->Magic != ADMISSION_RENDER_PACKET_MAGIC ||
      Packet->State != AdmissionRenderPacketEmpty ||
      Description->Fence == 0u || Description->ContextToken == 0ULL ||
      Description->AllocationToken == 0ULL ||
      Description->PrivateDataToken == 0ULL ||
      Description->PrivateDataBytes == 0u ||
      Description->PrivateDataStart >= Description->PrivateDataEnd ||
      Description->PrivateDataEnd > Description->PrivateDataBytes ||
      Description->DmaStart >= Description->DmaEnd ||
      Description->DestinationCpuToken == 0ULL ||
      Description->DestinationGpuVa == 0ULL ||
      Description->DestinationPhysical == 0ULL ||
      Description->DestinationBytes == 0u || Description->DestinationIndex >= 2u)
    return 0;
  Packet->Description = *Description;
  Packet->State = AdmissionRenderPacketPrepared;
  return 1;
}

int AdmissionRenderPacketMatches(
    const ADMISSION_RENDER_PACKET *Packet,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *Description,
    ADMISSION_RENDER_PACKET_STATE ExpectedState) {
  const ADMISSION_RENDER_PACKET_DESCRIPTION *current;

  if (Packet == ADMISSION_RENDER_NULL ||
      Description == ADMISSION_RENDER_NULL ||
      Packet->Magic != ADMISSION_RENDER_PACKET_MAGIC ||
      Packet->State != ExpectedState ||
      ExpectedState == AdmissionRenderPacketEmpty)
    return 0;
  current = &Packet->Description;
  return current->Fence == Description->Fence &&
         current->ContextToken == Description->ContextToken &&
         current->AllocationToken == Description->AllocationToken &&
         current->PrivateDataToken == Description->PrivateDataToken &&
         current->PrivateDataBytes == Description->PrivateDataBytes &&
         current->PrivateDataStart == Description->PrivateDataStart &&
         current->PrivateDataEnd == Description->PrivateDataEnd &&
         current->DmaStart == Description->DmaStart &&
         current->DmaEnd == Description->DmaEnd &&
         current->PatchOffset == Description->PatchOffset &&
         current->DestinationCpuToken ==
             Description->DestinationCpuToken &&
         current->DestinationGpuVa ==
             Description->DestinationGpuVa &&
         current->DestinationPhysical ==
             Description->DestinationPhysical &&
         current->DestinationBytes ==
             Description->DestinationBytes &&
         current->DestinationIndex == Description->DestinationIndex &&
         current->VisibleDestinationCpuToken ==
             Description->VisibleDestinationCpuToken &&
         current->VisibleDestinationGpuVa ==
             Description->VisibleDestinationGpuVa &&
         current->VisibleDestinationPhysical ==
             Description->VisibleDestinationPhysical &&
         current->VisibleDestinationAllocationToken ==
             Description->VisibleDestinationAllocationToken &&
         current->VisibleDestinationBytes ==
             Description->VisibleDestinationBytes;
}

int AdmissionRenderPacketQueue(
    ADMISSION_RENDER_PACKET *Packet, unsigned int Fence,
    unsigned long long ContextToken, unsigned long long PrivateDataToken,
    unsigned int DmaStart, unsigned int DmaEnd) {
  const ADMISSION_RENDER_PACKET_DESCRIPTION *description;

  if (Packet == ADMISSION_RENDER_NULL ||
      Packet->Magic != ADMISSION_RENDER_PACKET_MAGIC ||
      Packet->State != AdmissionRenderPacketPrepared)
    return 0;
  description = &Packet->Description;
  if (Fence != description->Fence ||
      ContextToken != description->ContextToken ||
      PrivateDataToken != description->PrivateDataToken ||
      DmaStart != description->DmaStart ||
      DmaEnd != description->DmaEnd)
    return 0;
  Packet->State = AdmissionRenderPacketQueued;
  return 1;
}

int AdmissionRenderPacketActivate(
    ADMISSION_RENDER_PACKET *Packet, unsigned int Fence) {
  if (Packet == ADMISSION_RENDER_NULL ||
      Packet->Magic != ADMISSION_RENDER_PACKET_MAGIC ||
      Packet->State != AdmissionRenderPacketQueued ||
      Fence != Packet->Description.Fence)
    return 0;
  Packet->State = AdmissionRenderPacketActive;
  return 1;
}

int AdmissionRenderPacketComplete(
    ADMISSION_RENDER_PACKET *Packet, unsigned int Fence) {
  if (Packet == ADMISSION_RENDER_NULL ||
      Packet->Magic != ADMISSION_RENDER_PACKET_MAGIC ||
      Packet->State != AdmissionRenderPacketActive ||
      Fence != Packet->Description.Fence)
    return 0;
  AdmissionRenderPacketClear(Packet);
  return 1;
}

int AdmissionRenderPacketCancelPrepared(
    ADMISSION_RENDER_PACKET *Packet, unsigned long long ContextToken) {
  if (Packet == ADMISSION_RENDER_NULL ||
      Packet->Magic != ADMISSION_RENDER_PACKET_MAGIC ||
      Packet->State != AdmissionRenderPacketPrepared ||
      ContextToken != Packet->Description.ContextToken)
    return 0;
  AdmissionRenderPacketClear(Packet);
  return 1;
}

int AdmissionRenderPacketDiscardQueued(
    ADMISSION_RENDER_PACKET *Packet, unsigned int CutoffFence) {
  if (Packet == ADMISSION_RENDER_NULL ||
      Packet->Magic != ADMISSION_RENDER_PACKET_MAGIC ||
      Packet->State != AdmissionRenderPacketQueued ||
      CutoffFence == 0u ||
      !AdmissionRenderFenceAtOrAfter(
          CutoffFence, Packet->Description.Fence))
    return 0;
  AdmissionRenderPacketClear(Packet);
  return 1;
}

int AdmissionRenderPacketReset(
    ADMISSION_RENDER_PACKET *Packet, unsigned int Fence,
    unsigned int BackendQuiesced) {
  if (Packet == ADMISSION_RENDER_NULL ||
      Packet->Magic != ADMISSION_RENDER_PACKET_MAGIC ||
      (Packet->State != AdmissionRenderPacketQueued &&
       Packet->State != AdmissionRenderPacketActive) ||
      Fence != Packet->Description.Fence ||
      (BackendQuiesced != 0u && BackendQuiesced != 1u) ||
      (Packet->State == AdmissionRenderPacketActive &&
       BackendQuiesced == 0u))
    return 0;
  AdmissionRenderPacketClear(Packet);
  return 1;
}

void AdmissionPrepatchedInitialize(ADMISSION_PREPATCHED_RENDER *State) {
  if (State != ADMISSION_RENDER_NULL)
    AdmissionPrepatchedClear(State);
}

int AdmissionPrepatchedActive(const ADMISSION_PREPATCHED_RENDER *State) {
  return State != ADMISSION_RENDER_NULL && State->Active == 1u;
}

int AdmissionPrepatchedCapture(
    ADMISSION_PREPATCHED_RENDER *State,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *Description) {
  if (State == ADMISSION_RENDER_NULL || Description == ADMISSION_RENDER_NULL ||
      State->Active != 0u || Description->Fence != 0u ||
      Description->ContextToken == 0ULL ||
      Description->AllocationToken == 0ULL ||
      Description->PrivateDataToken == 0ULL ||
      Description->PrivateDataBytes == 0u ||
      Description->PrivateDataStart >= Description->PrivateDataEnd ||
      Description->PrivateDataEnd > Description->PrivateDataBytes ||
      Description->DmaStart >= Description->DmaEnd ||
      Description->DestinationCpuToken == 0ULL ||
      Description->DestinationGpuVa == 0ULL ||
      Description->DestinationPhysical == 0ULL ||
      Description->DestinationBytes == 0u ||
      !AdmissionVisibleDescriptionComplete(Description)) {
    if (State != ADMISSION_RENDER_NULL)
      AdmissionPrepatchedClear(State);
    return 0;
  }
  State->Description = *Description;
  State->Active = 1u;
  return 1;
}

int AdmissionPrepatchedAdopt(
    ADMISSION_PREPATCHED_RENDER *State, unsigned int Fence,
    unsigned long long ContextToken, unsigned long long PrivateDataToken,
    unsigned int DmaStart, unsigned int DmaEnd,
    ADMISSION_RENDER_PACKET_DESCRIPTION *Description) {
  ADMISSION_RENDER_PACKET_DESCRIPTION captured;
  int valid;
  if (Description != ADMISSION_RENDER_NULL) {
    unsigned char *bytes = (unsigned char *)Description;
    unsigned int index;
    for (index = 0u; index < (unsigned int)sizeof(*Description); ++index)
      bytes[index] = 0u;
  }
  if (State == ADMISSION_RENDER_NULL || Description == ADMISSION_RENDER_NULL)
    return 0;
  captured = State->Description;
  valid = State->Active == 1u && Fence != 0u &&
          captured.Fence == 0u && captured.ContextToken == ContextToken &&
          captured.PrivateDataToken == PrivateDataToken &&
          captured.DmaStart == DmaStart && captured.DmaEnd == DmaEnd &&
          AdmissionVisibleDescriptionComplete(&captured);
  AdmissionPrepatchedClear(State);
  if (!valid)
    return 0;
  captured.Fence = Fence;
  *Description = captured;
  return 1;
}

int AdmissionPrepatchedCancel(
    ADMISSION_PREPATCHED_RENDER *State, unsigned long long ContextToken) {
  if (State == ADMISSION_RENDER_NULL || State->Active != 1u ||
      State->Description.ContextToken != ContextToken)
    return 0;
  AdmissionPrepatchedClear(State);
  return 1;
}
