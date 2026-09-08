#ifndef APPLE_AGX_RENDER_QUALIFICATION_H
#define APPLE_AGX_RENDER_QUALIFICATION_H

#define ADMISSION_PRESENT_QUERY_MAGIC 0x51504741u /* AGPQ */
#define ADMISSION_PRESENT_QUERY_VERSION 2u
#define ADMISSION_PRESENT_QUERY_CAPACITY 2u

typedef enum _ADMISSION_PRESENT_PURPOSE {
  AdmissionPresentPurposeUnknown = 0u,
  AdmissionPresentPurposeRenderFrame = 1u,
  AdmissionPresentPurposeInitialScanout = 2u,
  AdmissionPresentPurposeFallback = 3u,
} ADMISSION_PRESENT_PURPOSE;

typedef enum _ADMISSION_PRESENT_WAIT_RESULT {
  AdmissionPresentWaitCompleted = 0u,
  AdmissionPresentWaitTimedOut = 1u,
  AdmissionPresentWaitQueryFailed = 2u,
  AdmissionPresentWaitInvalidRecord = 3u,
} ADMISSION_PRESENT_WAIT_RESULT;

typedef enum _ADMISSION_PRESENT_PRODUCER_ACTION {
  AdmissionPresentProducerSubmitNextFrame = 1u,
  AdmissionPresentProducerBeginHold = 2u,
  AdmissionPresentProducerCleanup = 3u,
  AdmissionPresentProducerPreserveForRecovery = 4u,
} ADMISSION_PRESENT_PRODUCER_ACTION;

typedef struct _ADMISSION_PRESENT_PRODUCER_STATE {
  unsigned int HoldNoCleanup;
  unsigned int CompletedFrames;
  unsigned int CleanupAllowed;
  unsigned int Terminal;
} ADMISSION_PRESENT_PRODUCER_STATE;

typedef struct _ADMISSION_PRESENT_QUERY {
  unsigned int Magic, Version, CandidateBuild, BootGeneration;
  unsigned int Index, PresentCount, Purpose, DestinationIndex;
  unsigned int Fence, Status, Valid, Captured;
  unsigned int ExpectedColor, PixelsExpected, PixelsVerified, Format;
  unsigned int Width, Height, Pitch, PublishedToQuery;
  unsigned int Exported, Durable, Reserved0, Reserved1;
  unsigned long long AllocationToken, Sequence, ActiveOffset;
  unsigned long long PhysicalAddress, ContentHash;
} ADMISSION_PRESENT_QUERY;

typedef struct _ADMISSION_PRESENT_VERIFICATION {
  unsigned int CandidateBuild, BootGeneration, Index, Purpose;
  unsigned int Fence, DestinationIndex, ExpectedColor, PixelsExpected;
  unsigned int PixelsVerified, Format, Width, Height, Pitch;
  unsigned long long AllocationToken, Sequence, ActiveOffset;
  unsigned long long PhysicalAddress, ContentHash;
} ADMISSION_PRESENT_VERIFICATION;

typedef struct _ADMISSION_PRESENT_EXPECTATION {
  unsigned int CandidateBuild, BootGeneration, Index, DestinationIndex;
  unsigned int ExpectedColor, PixelsExpected, Format, Width, Height, Pitch;
  unsigned int PreviousFence;
  unsigned long long PreviousAllocationToken, PreviousSequence;
  unsigned long long PreviousActiveOffset, PreviousPhysicalAddress;
  unsigned long long ExpectedContentHash, PreviousContentHash;
} ADMISSION_PRESENT_EXPECTATION;

int AdmissionPresentQueryBuild(
    ADMISSION_PRESENT_QUERY *Record,
    const ADMISSION_PRESENT_VERIFICATION *Verified);
int AdmissionPresentQueryAccept(
    const ADMISSION_PRESENT_QUERY *Record,
    const ADMISSION_PRESENT_EXPECTATION *Expected);
ADMISSION_PRESENT_WAIT_RESULT AdmissionPresentWaitClassify(
    int QuerySucceeded, int RecordAccepted, int SawInvalidRecord,
    int DeadlineExpired);
void AdmissionPresentProducerInitialize(
    ADMISSION_PRESENT_PRODUCER_STATE *State, int HoldNoCleanup);
ADMISSION_PRESENT_PRODUCER_ACTION AdmissionPresentProducerAfterWait(
    ADMISSION_PRESENT_PRODUCER_STATE *State,
    ADMISSION_PRESENT_WAIT_RESULT Result);

#endif
