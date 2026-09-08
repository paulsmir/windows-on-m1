#ifndef APPLE_AGX_RENDER_COMPLETED_OUTPUT_H
#define APPLE_AGX_RENDER_COMPLETED_OUTPUT_H

#include "render_allocation.h"
#include "render_backend_image.h"

#define ADMISSION_COMPLETED_OUTPUT_VERSION 1u

typedef enum _ADMISSION_COMPLETED_OUTPUT_PHASE {
  AdmissionCompletedOutputEmpty = 0u,
  AdmissionCompletedOutputCaptured,
  AdmissionCompletedOutputBackendReleased,
  AdmissionCompletedOutputPacketRetired,
  AdmissionCompletedOutputNotified,
  AdmissionCompletedOutputPresenting,
  AdmissionCompletedOutputPublishedPending,
  AdmissionCompletedOutputLatched,
  AdmissionCompletedOutputOwnershipUnknown,
} ADMISSION_COMPLETED_OUTPUT_PHASE;

typedef struct _ADMISSION_COMPLETED_OUTPUT {
  unsigned int Version, Phase, Generation, Fence;
  unsigned int CaptureCount, ReleaseCount, NotifyCount, PresentCount;
  unsigned int AccessAttempted, AccessStatus;
  unsigned int PresentationAttempted, PresentationStatus;
  unsigned long long PresentationSequence;
  ADMISSION_BACKEND_OUTPUT_VIEW View;
  ADMISSION_ALLOCATION_OBJECT *Owner;
} ADMISSION_COMPLETED_OUTPUT;

typedef struct _ADMISSION_DISPLAY_OUTPUT_LEASE {
  unsigned int Version, Active, Generation, Fence;
  ADMISSION_BACKEND_OUTPUT_VIEW View;
  ADMISSION_ALLOCATION_OBJECT *Owner;
} ADMISSION_DISPLAY_OUTPUT_LEASE;

void AdmissionCompletedOutputInitialize(ADMISSION_COMPLETED_OUTPUT *State);
void AdmissionDisplayOutputLeaseInitialize(ADMISSION_DISPLAY_OUTPUT_LEASE *Lease);
int AdmissionCompletedOutputCapture(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Generation,
    unsigned int Fence, const ADMISSION_BACKEND_OUTPUT_VIEW *View,
    ADMISSION_ALLOCATION_OBJECT *Owner);
int AdmissionCompletedOutputMarkReleased(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence);
int AdmissionCompletedOutputMarkPacketRetired(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence);
int AdmissionCompletedOutputMarkNotified(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence);
int AdmissionCompletedOutputBeginPresent(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence);
int AdmissionCompletedOutputMarkPublished(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence,
    unsigned long long Sequence);
int AdmissionCompletedOutputMarkLatched(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence,
    unsigned long long Sequence);
int AdmissionCompletedOutputMarkOwnershipUnknown(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence,
    unsigned long long Sequence, unsigned int Status);
int AdmissionCompletedOutputResolveUnknown(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence,
    unsigned long long Sequence);
int AdmissionCompletedOutputRecordAccess(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence,
    unsigned int Status);
int AdmissionCompletedOutputRecordPresentation(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence,
    unsigned int Status);
int AdmissionCompletedOutputContains(
    const ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence,
    const void *Address, unsigned int Bytes);
int AdmissionCompletedOutputPlatformRangeValid(
    const ADMISSION_BACKEND_OUTPUT_VIEW *View, const void *PoolCpuAddress,
    unsigned long long PoolGpuAddress,
    unsigned long long PoolPhysicalAddress,
    unsigned long long PoolBytes);
int AdmissionCompletedOutputReceiptMatchesView(
    const ADMISSION_BACKEND_OUTPUT_VIEW *View,
    unsigned long long ReceiptAllocationGpuAddress,
    unsigned long long ReceiptAllocationPhysicalAddress,
    unsigned int ReceiptAllocationBytes,
    unsigned int OutputBytesExamined);
int AdmissionCompletedOutputTransferToDisplay(
    ADMISSION_COMPLETED_OUTPUT *State,
    ADMISSION_DISPLAY_OUTPUT_LEASE *Lease, unsigned int Fence);
int AdmissionCompletedOutputAbort(
    ADMISSION_COMPLETED_OUTPUT *State, unsigned int Fence);
int AdmissionDisplayOutputLeaseAllowsRender(
    const ADMISSION_DISPLAY_OUTPUT_LEASE *Lease,
    const ADMISSION_ALLOCATION_OBJECT *Owner);
int AdmissionDisplayOutputLeaseMatches(
    const ADMISSION_DISPLAY_OUTPUT_LEASE *Lease,
    const ADMISSION_ALLOCATION_OBJECT *Owner);
int AdmissionDisplayOutputLeaseRetire(ADMISSION_DISPLAY_OUTPUT_LEASE *Lease);
int AdmissionDisplayOutputLeaseCapture(
    ADMISSION_DISPLAY_OUTPUT_LEASE *Lease, unsigned int Generation,
    unsigned int Fence, const ADMISSION_BACKEND_OUTPUT_VIEW *View,
    ADMISSION_ALLOCATION_OBJECT *Owner);
int AdmissionDisplayOutputLeaseMove(
    ADMISSION_DISPLAY_OUTPUT_LEASE *Destination,
    ADMISSION_DISPLAY_OUTPUT_LEASE *Source);

#endif
