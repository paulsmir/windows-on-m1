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
      Description->DmaStart >= Description->DmaEnd)
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
         current->DmaEnd == Description->DmaEnd;
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
