#ifndef APPLE_AGX_BACKEND_RUNTIME_H
#define APPLE_AGX_BACKEND_RUNTIME_H

#include "apple_agx_firmware.h"
#include "apple_agx_render_template.h"
#include "apple_agx_submission.h"

/* Wire-codec aliases keep backend-facing layouts independent of WDK types. */
typedef APPLE_AGX_BOOL APPLE_AGX_BACKEND_BOOL;
typedef APPLE_AGX_U32 APPLE_AGX_BACKEND_U32;
typedef APPLE_AGX_U64 APPLE_AGX_BACKEND_U64;

#define APPLE_AGX_BACKEND_FALSE APPLE_AGX_FALSE
#define APPLE_AGX_BACKEND_TRUE APPLE_AGX_TRUE
#define APPLE_AGX_BACKEND_QUEUE_WORK_COUNT 2u

typedef enum _APPLE_AGX_BACKEND_COMPLETION_STATUS {
  AppleAgxBackendCompletionSuccess = 0,
  AppleAgxBackendCompletionFaulted,
  AppleAgxBackendCompletionTimedOut,
  AppleAgxBackendCompletionReset,
  AppleAgxBackendCompletionCancelled,
  /* G4-r2b: the firmware dropped the job after a Timeout/Fault event and
   * was resumed; the queue stays usable, the job's work was not done. */
  AppleAgxBackendCompletionDropped,
} APPLE_AGX_BACKEND_COMPLETION_STATUS;

typedef enum _APPLE_AGX_BACKEND_RUNTIME_PHASE {
  AppleAgxBackendRuntimeStopped = 0,
  AppleAgxBackendRuntimeStarting,
  AppleAgxBackendRuntimeReady,
  AppleAgxBackendRuntimeSubmitted,
  AppleAgxBackendRuntimeStopping,
  AppleAgxBackendRuntimeFailed,
} APPLE_AGX_BACKEND_RUNTIME_PHASE;

typedef enum _APPLE_AGX_BACKEND_RUNTIME_RESULT {
  AppleAgxBackendRuntimeResultOk = 0,
  AppleAgxBackendRuntimeResultInvalidArgument,
  AppleAgxBackendRuntimeResultInvalidState,
  AppleAgxBackendRuntimeResultBusy,
  AppleAgxBackendRuntimeResultMemoryFailed,
  AppleAgxBackendRuntimeResultImageFailed,
  AppleAgxBackendRuntimeResultFirmwareFailed,
  AppleAgxBackendRuntimeResultContextFailed,
  AppleAgxBackendRuntimeResultQueueFailed,
  AppleAgxBackendRuntimeResultFaulted,
  AppleAgxBackendRuntimeResultCleanupFailed,
} APPLE_AGX_BACKEND_RUNTIME_RESULT;

typedef enum _APPLE_AGX_BACKEND_QUEUE {
  AppleAgxBackendQueueTa = 0,
  AppleAgxBackendQueue3d,
  /* G2: the compute queue of a standalone compute job. */
  AppleAgxBackendQueueCompute,
} APPLE_AGX_BACKEND_QUEUE;

/* G2: what the firmware runs for one Windows fence. A render job is TA then
 * 3D (Run3d stages, RunTa submits); a compute job is one RunCompute work
 * item on the compute queue (Asahi drm_asahi_cmd_compute). */
typedef enum _APPLE_AGX_BACKEND_JOB_KIND {
  AppleAgxBackendJobRender = 0,
  AppleAgxBackendJobCompute = 1,
} APPLE_AGX_BACKEND_JOB_KIND;

typedef enum _APPLE_AGX_BACKEND_OBSERVATION_STATUS {
  AppleAgxBackendObservationComplete = 0,
  AppleAgxBackendObservationFault,
  AppleAgxBackendObservationTimeout,
  AppleAgxBackendObservationReset,
  /* EXP1075: the firmware reported a Timeout/Fault for this job, dropped
   * it and was resumed; the job completes without stamp evidence. */
  AppleAgxBackendObservationRecovered,
} APPLE_AGX_BACKEND_OBSERVATION_STATUS;

/*
 * Immutable bridge from the one Windows-owned FIFO head to the AGX runtime.
 * The private-data pointer remains owned by dxgkrnl until the exact submitted
 * fence is reported.  The backend never owns or duplicates the WDDM queue.
 */
typedef struct _APPLE_AGX_BACKEND_SUBMISSION {
  APPLE_AGX_SUBMISSION Submission;
  APPLE_AGX_U64 ContextIdentity;
  const void *PrivateData;
  APPLE_AGX_U32 PrivateDataBytes;
  APPLE_AGX_U32 PrivateDataStart;
  APPLE_AGX_U32 PrivateDataEnd;
  /* Original DMA interval used to select records from the immutable shadow. */
  APPLE_AGX_U32 DmaSubmissionStart;
  APPLE_AGX_U32 DmaSubmissionEnd;
} APPLE_AGX_BACKEND_SUBMISSION;

typedef struct _APPLE_AGX_BACKEND_JOB_IMAGE {
  APPLE_AGX_U64 TaWorkAddresses[APPLE_AGX_BACKEND_QUEUE_WORK_COUNT];
  APPLE_AGX_U64 D3WorkAddresses[APPLE_AGX_BACKEND_QUEUE_WORK_COUNT];
  APPLE_AGX_U64 ComputeWorkAddresses[APPLE_AGX_BACKEND_QUEUE_WORK_COUNT];
  APPLE_AGX_U32 TaWorkAddressCount;
  APPLE_AGX_U32 D3WorkAddressCount;
  APPLE_AGX_U32 ComputeWorkAddressCount;
  APPLE_AGX_U32 TaEvent;
  APPLE_AGX_U32 D3Event;
  APPLE_AGX_U32 ComputeEvent;
  APPLE_AGX_U32 TaExpectedStamp;
  APPLE_AGX_U32 D3ExpectedStamp;
  APPLE_AGX_U32 ComputeExpectedStamp;
  APPLE_AGX_U32 TaExpectedDonePointer;
  APPLE_AGX_U32 D3ExpectedDonePointer;
  APPLE_AGX_U32 ComputeExpectedDonePointer;
  APPLE_AGX_BACKEND_JOB_KIND Kind;
  /* D3D-G1b: a render with compute work leads its TA queue with this
   * firmware Barrier on the compute stamp; zero for every other job. */
  APPLE_AGX_U64 TaBarrierAddress;
  /* GPU timestamps (T1): the compute job's KMD timestamp block (job start,
   * job end, requested start, requested end; firmware-written), or NULL. */
  const void *ComputeTimestampBlock;
  /* GPU timestamps: the G4 envelope's command (DMA buffer) GPU VA, which
   * the UMD names its batch by; 0 for jobs without a G4 envelope. */
  APPLE_AGX_U64 CommandVa;
} APPLE_AGX_BACKEND_JOB_IMAGE;

typedef struct _APPLE_AGX_BACKEND_OBSERVATION {
  APPLE_AGX_BACKEND_QUEUE Queue;
  APPLE_AGX_U32 Event;
  APPLE_AGX_U32 Stamp;
  APPLE_AGX_U32 DonePointer;
  APPLE_AGX_BACKEND_OBSERVATION_STATUS Status;
} APPLE_AGX_BACKEND_OBSERVATION;

typedef struct _APPLE_AGX_BACKEND_IO {
  void *Context;
  APPLE_AGX_FIRMWARE_IO Firmware;
  struct {
    APPLE_AGX_BOOL (*Map)(void *Context, void **CpuAddress,
                          APPLE_AGX_U64 *GpuAddress,
                          APPLE_AGX_U32 *Bytes);
    APPLE_AGX_BOOL (*Unmap)(void *Context, void *CpuAddress,
                            APPLE_AGX_U32 Bytes);
    APPLE_AGX_BOOL (*Resolve)(
        void *Context, const APPLE_AGX_BACKEND_SUBMISSION *Submission,
        const unsigned char **Bytes, APPLE_AGX_U32 *ByteCount);
    APPLE_AGX_BOOL (*FlushForDevice)(void *Context, const void *Address,
                                     APPLE_AGX_U32 Bytes);
    APPLE_AGX_BOOL (*FlushForCpu)(void *Context, const void *Address,
                                  APPLE_AGX_U32 Bytes);
  } Memory;
  struct {
    /*
     * The platform memory owner materializes and maps the immutable EXP208
     * arena before the backend starts.  Runtime only borrows that mapping and
     * validates/copies its published roots; it must never rewrite or remap it.
     */
    APPLE_AGX_BOOL (*AcquirePrepared)(
        void *Context, void *Arena, APPLE_AGX_U32 ArenaBytes,
        APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots);
    APPLE_AGX_BOOL (*Relocate)(
        void *Context, void *Arena, APPLE_AGX_U32 ArenaBytes,
        const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots,
        const unsigned char *SubmissionBytes,
        APPLE_AGX_U32 SubmissionByteCount,
        const APPLE_AGX_BACKEND_SUBMISSION *Submission,
        APPLE_AGX_BACKEND_JOB_IMAGE *Job);
  } Image;
  struct {
    APPLE_AGX_BOOL (*Publish)(void *Context);
    APPLE_AGX_BOOL (*Unpublish)(void *Context);
  } RenderContext;
  struct {
    APPLE_AGX_BOOL (*Create)(void *Context);
    APPLE_AGX_BOOL (*Destroy)(void *Context);
    APPLE_AGX_BOOL (*Run3d)(void *Context,
                            const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
                            APPLE_AGX_U32 Fence);
    APPLE_AGX_BOOL (*RunTa)(void *Context,
                            const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
                            APPLE_AGX_U32 Fence);
    /* G2: submit a compute job (Kind Compute). FALSE means the fence was
     * never accepted (as Run3d); TRUE hands it to the firmware. */
    APPLE_AGX_BOOL (*RunCompute)(void *Context,
                                 const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
                                 APPLE_AGX_U32 Fence);
    APPLE_AGX_BOOL (*Stop)(void *Context, APPLE_AGX_U32 Fence);
    APPLE_AGX_BOOL (*Reset)(void *Context, APPLE_AGX_U32 Fence);
  } Queues;
  /*
   * TRUE is the ownership receipt: the exact fence has been accepted by the
   * Windows-facing completion path. FALSE is retryable and must not cause the
   * runtime to discard or reorder the pending submission.
   */
  APPLE_AGX_BOOL (*Complete)(
      void *Context, APPLE_AGX_U32 SubmissionFence,
      APPLE_AGX_U32 NodeOrdinal, APPLE_AGX_U32 EngineOrdinal,
      APPLE_AGX_BACKEND_COMPLETION_STATUS Status);
  /*
   * Teardown-only Windows ownership receipt.  It never represents GPU DMA
   * completion and therefore must not emit DXGK_INTERRUPT_DMA_COMPLETED.
   */
  APPLE_AGX_BOOL (*Retire)(void *Context,
                           APPLE_AGX_U32 SubmissionFence,
                           APPLE_AGX_U32 NodeOrdinal,
                           APPLE_AGX_U32 EngineOrdinal);
} APPLE_AGX_BACKEND_IO;

typedef struct _APPLE_AGX_BACKEND_RUNTIME {
  APPLE_AGX_BACKEND_RUNTIME_PHASE Phase;
  APPLE_AGX_U64 ContextIdentity;
  APPLE_AGX_BACKEND_IO Io;
  APPLE_AGX_FIRMWARE Firmware;
  void *ArenaCpuAddress;
  APPLE_AGX_U64 ArenaGpuAddress;
  APPLE_AGX_U32 ArenaBytes;
  APPLE_AGX_RENDER_TEMPLATE_ROOTS Roots;
  APPLE_AGX_BACKEND_SUBMISSION PendingSubmission;
  APPLE_AGX_BACKEND_JOB_IMAGE PendingJob;
  APPLE_AGX_BOOL ArenaMapped;
  APPLE_AGX_BOOL ContextPublished;
  APPLE_AGX_BOOL QueuesCreated;
  /* Successful Stop/Reset receipt for the current queue instance. */
  APPLE_AGX_BOOL QueuesQuiesced;
  APPLE_AGX_BOOL TaComplete;
  APPLE_AGX_BOOL D3Complete;
  APPLE_AGX_BOOL ComputeComplete;
  APPLE_AGX_BOOL TerminalPending;
  APPLE_AGX_BOOL TerminalCpuFlushed;
  APPLE_AGX_BACKEND_COMPLETION_STATUS TerminalStatus;
  APPLE_AGX_BACKEND_RUNTIME_PHASE TerminalNextPhase;
  APPLE_AGX_BACKEND_RUNTIME_RESULT TerminalResult;
} APPLE_AGX_BACKEND_RUNTIME;

void AppleAgxBackendRuntimeInitialize(APPLE_AGX_BACKEND_RUNTIME *Runtime,
                                      APPLE_AGX_U64 ContextIdentity);
APPLE_AGX_BOOL AppleAgxBackendSubmissionFromEntry(
    APPLE_AGX_U64 ContextIdentity,
    const APPLE_AGX_SUBMISSION_ENTRY *Entry,
    const void *PrivateData,
    APPLE_AGX_BACKEND_SUBMISSION *Submission);
APPLE_AGX_BACKEND_RUNTIME_RESULT AppleAgxBackendRuntimeStart(
    APPLE_AGX_BACKEND_RUNTIME *Runtime, const APPLE_AGX_BACKEND_IO *Io);
APPLE_AGX_BACKEND_RUNTIME_RESULT AppleAgxBackendRuntimeSubmit(
    APPLE_AGX_BACKEND_RUNTIME *Runtime,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission);
APPLE_AGX_BACKEND_RUNTIME_RESULT AppleAgxBackendRuntimeObserve(
    APPLE_AGX_BACKEND_RUNTIME *Runtime,
    const APPLE_AGX_BACKEND_OBSERVATION *Observation);
APPLE_AGX_BACKEND_RUNTIME_RESULT AppleAgxBackendRuntimeAcknowledgeCompletion(
    APPLE_AGX_BACKEND_RUNTIME *Runtime);
APPLE_AGX_BACKEND_RUNTIME_RESULT AppleAgxBackendRuntimeStop(
    APPLE_AGX_BACKEND_RUNTIME *Runtime);

#endif /* APPLE_AGX_BACKEND_RUNTIME_H */
