#ifndef APPLE_AGX_RENDER_VISIBLE_SCANOUT_H
#define APPLE_AGX_RENDER_VISIBLE_SCANOUT_H

#include "apple_agx_scanout.h"

#define ADMISSION_VISIBLE_PATTERN_VERSION 1u
#define ADMISSION_VISIBLE_PATTERN_PREFIX_BYTES 64u

typedef struct _ADMISSION_VISIBLE_PATTERN_RECEIPT {
  unsigned int Version;
  unsigned int Bytes;
  unsigned int Frame;
  unsigned int Width;
  unsigned int Height;
  unsigned int Pitch;
  unsigned int Format;
  unsigned int Reserved;
  unsigned long long SurfaceBytes;
  unsigned long long ContentHash;
  unsigned char Prefix[ADMISSION_VISIBLE_PATTERN_PREFIX_BYTES];
} ADMISSION_VISIBLE_PATTERN_RECEIPT;

#define ADMISSION_VISIBLE_SCANOUT_RECEIPT_VERSION 1u
typedef struct _ADMISSION_VISIBLE_SCANOUT_RECEIPT {
  unsigned int Version;
  unsigned int Bytes;
  unsigned int Stage;
  unsigned int Status;
  ADMISSION_VISIBLE_PATTERN_RECEIPT Pattern;
  unsigned long long CpuAddress;
  unsigned long long GuestIpaAddress;
  unsigned long long HostPhysicalAddress;
  unsigned long long SurfaceOffset;
  unsigned long long RequestedSequence;
  unsigned long long AppliedSequence;
  unsigned long long LatchedSequence;
  unsigned long long ActiveOffset;
  unsigned long long PoolPhysicalAddress;
  unsigned int SwapId;
  unsigned int SourceVisible;
  unsigned int ElapsedMs;
  unsigned int Reserved;
} ADMISSION_VISIBLE_SCANOUT_RECEIPT;

#define ADMISSION_VISIBLE_AGX_RECEIPT_VERSION 1u
typedef enum _ADMISSION_VISIBLE_AGX_GUARD {
  AdmissionVisibleAgxGuardEntry = 1u,
  AdmissionVisibleAgxGuardArguments = 2u,
  AdmissionVisibleAgxGuardPanel = 3u,
  AdmissionVisibleAgxGuardCapturedDestination = 4u,
  AdmissionVisibleAgxGuardScanoutView = 5u,
  AdmissionVisibleAgxGuardDestinationRange = 6u,
  AdmissionVisibleAgxGuardDestinationIdentity = 7u,
  AdmissionVisibleAgxGuardActiveSurface = 8u,
  AdmissionVisibleAgxGuardScaled = 9u,
  AdmissionVisibleAgxGuardQueued = 10u,
  AdmissionVisibleAgxGuardComplete = 11u,
} ADMISSION_VISIBLE_AGX_GUARD;

typedef struct _ADMISSION_VISIBLE_AGX_RECEIPT {
  unsigned int Version, Bytes, Stage, Status, Fence;
  unsigned int SourceWidth, SourceHeight, SourcePitch;
  unsigned int Guard, CapturedValid, CapturedFence, Reserved;
  unsigned long long SourceBytes, SourceGpuAddress, SourcePhysicalAddress;
  unsigned long long SourceHash;
  unsigned long long DestinationCpuAddress, DestinationGuestIpa;
  unsigned long long DestinationPhysicalAddress, DestinationOffset;
  unsigned long long DestinationAllocationToken;
  unsigned long long DestinationBytes, DestinationHash;
  unsigned long long ActiveOffsetBefore, RequestedSequence;
  unsigned long long AppliedSequence, LatchedSequence, ActiveOffsetAfter;
  unsigned int SwapId, ElapsedMs;
  unsigned char SourcePrefix[64];
} ADMISSION_VISIBLE_AGX_RECEIPT;

int AdmissionVisiblePatternFill(
    void *Surface, unsigned long long SurfaceBytes, unsigned int Frame,
    ADMISSION_VISIBLE_PATTERN_RECEIPT *Receipt);

int AdmissionVisibleScanoutReceiptValid(
    const ADMISSION_VISIBLE_SCANOUT_RECEIPT *Receipt);

int AdmissionVisibleAgxScale16x16(
    const void *Source, unsigned long long SourceBytes,
    void *Destination, unsigned long long DestinationBytes,
    ADMISSION_VISIBLE_AGX_RECEIPT *Receipt);

int AdmissionVisibleAgxReceiptValid(
    const ADMISSION_VISIBLE_AGX_RECEIPT *Receipt);


#endif /* APPLE_AGX_RENDER_VISIBLE_SCANOUT_H */
