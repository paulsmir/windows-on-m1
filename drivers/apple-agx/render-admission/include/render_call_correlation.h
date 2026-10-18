#ifndef APPLE_AGX_RENDER_CALL_CORRELATION_H
#define APPLE_AGX_RENDER_CALL_CORRELATION_H

#define ADMISSION_RENDER_CORRELATION_VERSION 3u
#define ADMISSION_RENDER_CORRELATION_CAPACITY 2u

enum {
  AdmissionOutputTraceEntry,
  AdmissionOutputTraceVerified,
  AdmissionOutputTracePresentEntry,
  AdmissionOutputTracePresentExit,
  ADMISSION_OUTPUT_TRACE_COUNT
};
typedef struct _ADMISSION_OUTPUT_TRACE {
  unsigned int Valid, Status, Processor, Irql;
  unsigned long long Timestamp;
} ADMISSION_OUTPUT_TRACE;

#define ADMISSION_RENDER_CAPTURE_ENTRY       (1u << 0)
#define ADMISSION_RENDER_CAPTURE_VALIDATED   (1u << 1)
#define ADMISSION_RENDER_CAPTURE_EXIT        (1u << 2)
#define ADMISSION_RENDER_CAPTURE_PATCH_ENTRY (1u << 3)
#define ADMISSION_RENDER_CAPTURE_PATCH_EXIT  (1u << 4)
#define ADMISSION_RENDER_CAPTURE_SUBMIT_ENTRY (1u << 5)
#define ADMISSION_RENDER_CAPTURE_SUBMIT_EXIT  (1u << 6)
#define ADMISSION_RENDER_CAPTURE_WORKER_ENTRY (1u << 7)
#define ADMISSION_RENDER_CAPTURE_WORKER_EXIT  (1u << 8)
#define ADMISSION_RENDER_CAPTURE_NOTIFY       (1u << 9)
#define ADMISSION_RENDER_CAPTURE_DPC          (1u << 10)
#define ADMISSION_RENDER_CAPTURE_QUERY_FENCE  (1u << 11)

typedef struct _ADMISSION_RENDER_CORRELATION_SLOT {
  unsigned int Version, Bytes, CandidateBuild, BootGeneration;
  unsigned int CallSequence, ValidMask, RenderGuard, RenderStatus;
  unsigned int CommandLength, AllocationCount, DestinationIndex;
  unsigned int DestinationSegment, DmaBytesProduced, PatchesProduced;
  unsigned int Prepatched, PatchGuard, PatchStatus, SubmitGuard;
  unsigned int SubmitStatus, Fence, WorkerStatus, Reserved;
  unsigned int SynchronizeStatus, QueueDpcResult;
  unsigned int QueryFenceCount, QueryFenceValue;
  unsigned long long EntryTimestamp, ExitTimestamp;
  unsigned long long NotifyTimestamp, DpcTimestamp;
  unsigned long long AdapterToken, ContextToken, CommandHash;
  unsigned long long AllocationToken[2];
  ADMISSION_OUTPUT_TRACE Output[ADMISSION_OUTPUT_TRACE_COUNT];
} ADMISSION_RENDER_CORRELATION_SLOT;

typedef struct _ADMISSION_RENDER_CORRELATION_STATE {
  unsigned int Version, Bytes, CandidateBuild, BootGeneration;
  unsigned int Count, Overflow, CapturedGeneration, ExportedGeneration;
  unsigned int ExportAttempted, ExportStatus, Durable, Reserved;
  ADMISSION_RENDER_CORRELATION_SLOT Slot[ADMISSION_RENDER_CORRELATION_CAPACITY];
} ADMISSION_RENDER_CORRELATION_STATE;

int AdmissionRenderCorrelationInitialize(
    ADMISSION_RENDER_CORRELATION_STATE *State,
    unsigned int CandidateBuild, unsigned int BootGeneration);
int AdmissionRenderCorrelationBegin(
    ADMISSION_RENDER_CORRELATION_STATE *State,
    unsigned long long Timestamp, unsigned long long AdapterToken,
    unsigned long long ContextToken, unsigned int CommandLength,
    unsigned int *CallSequence);
int AdmissionRenderCorrelationValidated(
    ADMISSION_RENDER_CORRELATION_STATE *State, unsigned int CallSequence,
    unsigned long long CommandHash, unsigned int AllocationCount,
    unsigned int DestinationIndex, unsigned int DestinationSegment,
    const unsigned long long AllocationToken[2]);
int AdmissionRenderCorrelationExit(
    ADMISSION_RENDER_CORRELATION_STATE *State, unsigned int CallSequence,
    unsigned long long Timestamp, unsigned int Guard, unsigned int Status,
    unsigned int DmaBytesProduced, unsigned int PatchesProduced,
    unsigned int Prepatched);
int AdmissionRenderCorrelationPatch(
    ADMISSION_RENDER_CORRELATION_STATE *State,
    unsigned long long ContextToken, unsigned int Entry,
    unsigned int Guard, unsigned int Status);
int AdmissionRenderCorrelationSubmit(
    ADMISSION_RENDER_CORRELATION_STATE *State,
    unsigned long long ContextToken, unsigned int Entry,
    unsigned int Fence, unsigned int Guard, unsigned int Status);
int AdmissionRenderCorrelationWorker(
    ADMISSION_RENDER_CORRELATION_STATE *State, unsigned int Fence,
    unsigned int Entry, unsigned int Status);
int AdmissionRenderCorrelationMarkExport(
    ADMISSION_RENDER_CORRELATION_STATE *State,
    unsigned int CapturedGeneration, unsigned int Status,
    unsigned int Durable);
int AdmissionRenderCorrelationOutput(
    ADMISSION_RENDER_CORRELATION_STATE *State, unsigned int Fence,
    unsigned int Stage, unsigned int Status, unsigned int Processor,
    unsigned int Irql, unsigned long long Timestamp);

#endif
