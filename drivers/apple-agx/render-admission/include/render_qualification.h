#ifndef APPLE_AGX_RENDER_QUALIFICATION_H
#define APPLE_AGX_RENDER_QUALIFICATION_H

#define ADMISSION_PRESENT_QUERY_MAGIC 0x51504741u /* AGPQ */
#define ADMISSION_PRESENT_QUERY_VERSION 2u
#define ADMISSION_PRESENT_QUERY_CAPACITY 16u
#define ADMISSION_RETIREMENT_QUERY_MAGIC 0x51524741u /* AGRQ */
#define ADMISSION_RETIREMENT_QUERY_VERSION 1u
#define ADMISSION_STANDARD_PRESENT_TRACE_MAGIC 0x54504741u /* AGPT */
#define ADMISSION_STANDARD_PRESENT_TRACE_VERSION 1u
#define ADMISSION_STANDARD_PRESENT_TRACE_CAPACITY 16u
#define ADMISSION_BLT_PROBE_MAGIC 0x50424741u /* AGBP */
#define ADMISSION_BLT_PROBE_VERSION 1u
#define ADMISSION_BLT_PROBE_CAPACITY 32u
#define ADMISSION_DWM_DDI_PROBE_MAGIC 0x50445741u
#define ADMISSION_DWM_DDI_PROBE_VERSION 1u
#define ADMISSION_DWM_FRAME_ARM_MAGIC 0x41465741u
#define ADMISSION_DWM_FRAME_PROBE_MAGIC 0x50465741u
#define ADMISSION_DWM_FRAME_VERSION 3u
#define ADMISSION_DWM_FRAME_CAPACITY 8u

typedef struct _ADMISSION_DWM_FRAME_ARM {
  unsigned int Magic, Version, Bytes, OsProcessId;
  unsigned long long Allocation, CanonicalGpuVa;
} ADMISSION_DWM_FRAME_ARM;

/* EXP911: first branch7 failure in this armed context, never a global PID guess. */
typedef struct _ADMISSION_DWM_ENVELOPE_RECEIPT {
  unsigned int Stage, Irql, Flags, ContextState;
  unsigned int Predicate, RuntimePredicate;
  unsigned int Fence, PrivateFence, PreemptFence, CancelFence;
  unsigned long long InterruptTime, Device;
  unsigned long long ManagerGeneration, ContextGeneration;
  unsigned long long LeaseManagerId, LeaseManagerGeneration;
  unsigned long long LeaseSceneId, LeaseSceneGeneration;
  unsigned int SceneState, SceneFence;
  unsigned long long SceneGeneration;
} ADMISSION_DWM_ENVELOPE_RECEIPT;

typedef struct _ADMISSION_DWM_FRAME_ENTRY {
  volatile long Sequence, Dropped;
  unsigned int OsProcessId, ReservedProcess;
  unsigned long long GraphProcessId, Context, Allocation, CanonicalGpuVa;
  unsigned int QueryCount, QueryPredicate, QueryStatus, QueryResidentPages;
  unsigned int SubmitCount, SubmitStatus, SubmitBranch, CompleteCount;
  unsigned long long CommandGpuVa, SubmittedFence, CompletedFence;
  unsigned int PresentCount, PresentStatus, VirtualPresentCount, VirtualPresentStatus;
  unsigned long long SourceAllocation, DestinationAllocation;
  unsigned long long SourceGpuVa, DestinationGpuVa, DestinationGuestIpa;
  unsigned long long PresentFence, CopiedBytes;
  unsigned int CopyStatus, Reserved;
  ADMISSION_DWM_ENVELOPE_RECEIPT Envelope;
} ADMISSION_DWM_FRAME_ENTRY;

typedef struct _ADMISSION_DWM_FRAME_TDR {
  unsigned int Captured, PacketState, SchedulerCompletedFence, SchedulerLastSubmittedFence;
  unsigned int SchedulerActiveFence, PagingPending, ResetStatus, PrivateResetStatus;
  unsigned long long PacketContext, PacketFence;
} ADMISSION_DWM_FRAME_TDR;

typedef struct _ADMISSION_DWM_FRAME_PROBE {
  unsigned int Magic, Version, Bytes, CandidateBuild;
  unsigned int BootGeneration, ArmedCount, Incomplete, Dropped;
  volatile long WriteClaim, Sequence;
  ADMISSION_DWM_FRAME_ENTRY Entries[ADMISSION_DWM_FRAME_CAPACITY];
  ADMISSION_DWM_FRAME_TDR Tdr;
} ADMISSION_DWM_FRAME_PROBE;

typedef enum _ADMISSION_DWM_DDI_KIND {
  AdmissionDwmDdiPresentBlt = 0,
  AdmissionDwmDdiPresentFlip = 1,
  AdmissionDwmDdiPresentOther = 2,
  AdmissionDwmDdiSourceAddress = 3,
  AdmissionDwmDdiCommitVidPn = 4,
  AdmissionDwmDdiSubmitPresent = 5,
  AdmissionDwmDdiSubmitOther = 6,
  AdmissionDwmDdiMpo = 7,
  AdmissionDwmDdiDisplayOnly = 8,
  AdmissionDwmDdiCount = 9
} ADMISSION_DWM_DDI_KIND;

typedef struct _ADMISSION_DWM_DDI_EVENT {
  unsigned int Kind, Status, Flags, SourceId;
  unsigned int Segment, SourceCount, DestinationCount, Reserved;
  unsigned long long Allocation, Address, Context, Fence;
} ADMISSION_DWM_DDI_EVENT;

typedef struct _ADMISSION_DWM_DDI_ENTRY {
  volatile long Sequence, Count, Dropped, Reserved;
  ADMISSION_DWM_DDI_EVENT Last;
} ADMISSION_DWM_DDI_ENTRY;

typedef struct _ADMISSION_DWM_DDI_PROBE {
  unsigned int Magic, Version, Bytes, CandidateBuild;
  unsigned int BootGeneration, Incomplete, Reserved0, Reserved1;
  ADMISSION_DWM_DDI_ENTRY Entries[AdmissionDwmDdiCount];
} ADMISSION_DWM_DDI_PROBE;

typedef struct _ADMISSION_BLT_EXECUTION {
  unsigned int Valid, Sequence, Status, SourceSegment;
  unsigned int DestinationSegment, CacheCleanPerformed, Reserved0, Reserved1;
  unsigned long long SourceAddress, DestinationAddress, BytesCopied;
  unsigned long long SourceCpuAddress, DestinationCpuAddress;
  unsigned long long SourceHostPa, DestinationHostPa;
  unsigned long long SourceGuestIpa, DestinationGuestIpa;
  unsigned long long SourceLocalGpuVa, DestinationLocalGpuVa;
} ADMISSION_BLT_EXECUTION;

typedef struct _ADMISSION_BLT_PROBE {
  unsigned int Magic, Version, Bytes, CandidateBuild;
  unsigned int BootGeneration, PresentCalls, PresentBltCalls, VirtualSubmitCalls;
  unsigned int PhysicalPresentSubmits, CpuBltExecutions, EventCount, Overflow;
  unsigned long long AdapterToken;
  ADMISSION_BLT_EXECUTION Events[ADMISSION_BLT_PROBE_CAPACITY];
} ADMISSION_BLT_PROBE;

typedef enum _ADMISSION_STANDARD_PRESENT_TRACE_COMMAND {
  AdmissionStandardPresentTraceArm = 1u,
  AdmissionStandardPresentTraceRead = 2u,
} ADMISSION_STANDARD_PRESENT_TRACE_COMMAND;

typedef enum _ADMISSION_STANDARD_PRESENT_EVENT_KIND {
  AdmissionStandardPresentEventPresent = 1u,
  AdmissionStandardPresentEventSourceAddress = 2u,
} ADMISSION_STANDARD_PRESENT_EVENT_KIND;

typedef enum _ADMISSION_STANDARD_PRESENT_EVENT_PHASE {
  AdmissionStandardPresentPhaseEntry = 1u,
  AdmissionStandardPresentPhaseExit = 2u,
} ADMISSION_STANDARD_PRESENT_EVENT_PHASE;

typedef struct _ADMISSION_STANDARD_PRESENT_EVENT {
  unsigned int Valid, Kind, Phase, Sequence;
  unsigned int Status, Irql, Flags, SourceId;
  unsigned int Segment, NumSrc, NumDst, Reserved;
  unsigned long long ContextToken, AllocationToken, PrimaryAddress;
} ADMISSION_STANDARD_PRESENT_EVENT;

typedef struct _ADMISSION_STANDARD_PRESENT_TRACE {
  unsigned int Magic, Version, Bytes, Command;
  unsigned int CandidateBuild, BootGeneration, EventCount, Overflow;
  ADMISSION_STANDARD_PRESENT_EVENT
      Events[ADMISSION_STANDARD_PRESENT_TRACE_CAPACITY];
} ADMISSION_STANDARD_PRESENT_TRACE;

typedef struct _ADMISSION_STANDARD_PRESENT_EXPECTATION {
  unsigned int CandidateBuild, BootGeneration, Flags, SourceId, Segment;
  unsigned int NumSrc, NumDst, Reserved;
  unsigned long long ContextToken, AllocationToken;
} ADMISSION_STANDARD_PRESENT_EXPECTATION;

typedef enum _ADMISSION_STANDARD_PRESENT_PRODUCER_STAGE {
  AdmissionStandardPresentOwnerAcquired = 1u,
  AdmissionStandardPresentModeSet = 2u,
  AdmissionStandardPresentFrameConfirmed = 3u,
  AdmissionStandardPresentFlipBackConfirmed = 4u,
  AdmissionStandardPresentOwnerReleased = 5u,
} ADMISSION_STANDARD_PRESENT_PRODUCER_STAGE;

typedef struct _ADMISSION_STANDARD_PRESENT_PRODUCER_STATE {
  unsigned int OwnerAcquired, ModeSet, FrameConfirmed;
  unsigned int FlipBackConfirmed, OwnerReleased, Invalid;
} ADMISSION_STANDARD_PRESENT_PRODUCER_STATE;

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
  unsigned int TargetFrames;
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
  unsigned long long ExpectedAllocationToken, ExpectedActiveOffset;
  unsigned long long ExpectedPhysicalAddress;
} ADMISSION_PRESENT_EXPECTATION;

typedef enum _ADMISSION_RETIREMENT_COMMAND {
  AdmissionRetirementCommandExecute = 1u,
} ADMISSION_RETIREMENT_COMMAND;

typedef struct _ADMISSION_RETIREMENT_QUERY {
  unsigned int Magic, Version, Command, Status;
  unsigned int CandidateBuild, BootGeneration, Purpose, Valid;
  unsigned long long Sequence, AllocationToken, ActiveOffset;
  unsigned long long PhysicalAddress;
} ADMISSION_RETIREMENT_QUERY;

typedef struct _ADMISSION_RETIREMENT_EXPECTATION {
  unsigned int CandidateBuild, BootGeneration;
  unsigned long long PreviousSequence, ExpectedPoolPhysical;
  unsigned long long RenderAllocation0, RenderAllocation1;
} ADMISSION_RETIREMENT_EXPECTATION;

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
    ADMISSION_PRESENT_PRODUCER_STATE *State, int HoldNoCleanup,
    unsigned int TargetFrames);
ADMISSION_PRESENT_PRODUCER_ACTION AdmissionPresentProducerAfterWait(
    ADMISSION_PRESENT_PRODUCER_STATE *State,
    ADMISSION_PRESENT_WAIT_RESULT Result);
int AdmissionPresentProducerRetirementComplete(
    ADMISSION_PRESENT_PRODUCER_STATE *State);
int AdmissionPresentProducerCanCleanup(
    const ADMISSION_PRESENT_PRODUCER_STATE *State, int HasAllocations);
int AdmissionRetirementQueryBuild(
    ADMISSION_RETIREMENT_QUERY *Record, unsigned int CandidateBuild,
    unsigned int BootGeneration, unsigned long long Sequence,
    unsigned long long AllocationToken, unsigned long long ActiveOffset,
    unsigned long long PhysicalAddress);
int AdmissionRetirementQueryAccept(
    const ADMISSION_RETIREMENT_QUERY *Record,
    const ADMISSION_RETIREMENT_EXPECTATION *Expected);
void AdmissionStandardPresentTraceInitialize(
    ADMISSION_STANDARD_PRESENT_TRACE *Trace,
    ADMISSION_STANDARD_PRESENT_TRACE_COMMAND Command,
    unsigned int CandidateBuild, unsigned int BootGeneration);
int AdmissionStandardPresentTraceAppend(
    ADMISSION_STANDARD_PRESENT_TRACE *Trace,
    const ADMISSION_STANDARD_PRESENT_EVENT *Event);
int AdmissionStandardPresentTraceAccept(
    const ADMISSION_STANDARD_PRESENT_TRACE *Trace,
    const ADMISSION_STANDARD_PRESENT_EXPECTATION *Expected,
    unsigned int *PresentSequence,
    unsigned int *SourceAddressSequence);
int AdmissionStandardPresentTraceAcceptPresent(
    const ADMISSION_STANDARD_PRESENT_TRACE *Trace,
    const ADMISSION_STANDARD_PRESENT_EXPECTATION *Expected,
    unsigned int *PresentSequence,
    unsigned long long *ContextToken,
    unsigned long long *AllocationToken);
void AdmissionStandardPresentProducerInitialize(
    ADMISSION_STANDARD_PRESENT_PRODUCER_STATE *State);
int AdmissionStandardPresentProducerAdvance(
    ADMISSION_STANDARD_PRESENT_PRODUCER_STATE *State,
    ADMISSION_STANDARD_PRESENT_PRODUCER_STAGE Stage);
int AdmissionStandardPresentProducerCanCleanup(
    const ADMISSION_STANDARD_PRESENT_PRODUCER_STATE *State);

#endif
