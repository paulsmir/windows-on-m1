#ifndef APPLE_AGX_RENDER_SUBMISSION_H
#define APPLE_AGX_RENDER_SUBMISSION_H

#define ADMISSION_RENDER_PACKET_MAGIC 0x4b505252u /* "RRPK" */

typedef enum _ADMISSION_RENDER_PACKET_STATE {
  AdmissionRenderPacketEmpty = 0,
  AdmissionRenderPacketPrepared,
  AdmissionRenderPacketQueued,
  AdmissionRenderPacketActive,
} ADMISSION_RENDER_PACKET_STATE;

typedef struct _ADMISSION_RENDER_PACKET_DESCRIPTION {
  unsigned int Fence;
  unsigned long long ContextToken;
  unsigned long long AllocationToken;
  unsigned long long PrivateDataToken;
  unsigned int PrivateDataBytes;
  unsigned int PrivateDataStart;
  unsigned int PrivateDataEnd;
  unsigned int DmaStart;
  unsigned int DmaEnd;
  unsigned long long DestinationCpuToken;
  unsigned long long DestinationGpuVa;
  unsigned long long DestinationPhysical;
  unsigned int DestinationBytes;
} ADMISSION_RENDER_PACKET_DESCRIPTION;

typedef struct _ADMISSION_RENDER_PACKET {
  unsigned int Magic;
  ADMISSION_RENDER_PACKET_STATE State;
  ADMISSION_RENDER_PACKET_DESCRIPTION Description;
} ADMISSION_RENDER_PACKET;

int AdmissionNonPagingPrivateRangeCovers(
    unsigned int PrivateBytesUsed, unsigned int SubmissionStart,
    unsigned int SubmissionEnd, unsigned int PrivateBufferBytes);

void AdmissionRenderPacketInitialize(ADMISSION_RENDER_PACKET *Packet);
ADMISSION_RENDER_PACKET_STATE AdmissionRenderPacketState(
    const ADMISSION_RENDER_PACKET *Packet);
int AdmissionRenderPacketPrepare(
    ADMISSION_RENDER_PACKET *Packet,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *Description);
int AdmissionRenderPacketMatches(
    const ADMISSION_RENDER_PACKET *Packet,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *Description,
    ADMISSION_RENDER_PACKET_STATE ExpectedState);
int AdmissionRenderPacketQueue(
    ADMISSION_RENDER_PACKET *Packet, unsigned int Fence,
    unsigned long long ContextToken, unsigned long long PrivateDataToken,
    unsigned int DmaStart, unsigned int DmaEnd);
int AdmissionRenderPacketActivate(
    ADMISSION_RENDER_PACKET *Packet, unsigned int Fence);
int AdmissionRenderPacketComplete(
    ADMISSION_RENDER_PACKET *Packet, unsigned int Fence);
int AdmissionRenderPacketCancelPrepared(
    ADMISSION_RENDER_PACKET *Packet, unsigned long long ContextToken);
int AdmissionRenderPacketDiscardQueued(
    ADMISSION_RENDER_PACKET *Packet, unsigned int CutoffFence);
int AdmissionRenderPacketReset(
    ADMISSION_RENDER_PACKET *Packet, unsigned int Fence,
    unsigned int BackendQuiesced);

#endif /* APPLE_AGX_RENDER_SUBMISSION_H */
