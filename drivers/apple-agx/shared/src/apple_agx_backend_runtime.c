#include "apple_agx_backend_runtime.h"

#define APPLE_AGX_BACKEND_NULL ((void *)0)

static void AppleAgxBackendZero(void *Address, APPLE_AGX_U32 Bytes) {
  unsigned char *destination = (unsigned char *)Address;
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    destination[index] = 0u;
}

static APPLE_AGX_BOOL
AppleAgxBackendIoValid(const APPLE_AGX_BACKEND_IO *Io) {
  return Io != APPLE_AGX_BACKEND_NULL &&
         Io->Memory.Map != APPLE_AGX_BACKEND_NULL &&
         Io->Memory.Unmap != APPLE_AGX_BACKEND_NULL &&
         Io->Memory.Resolve != APPLE_AGX_BACKEND_NULL &&
         Io->Memory.FlushForDevice != APPLE_AGX_BACKEND_NULL &&
         Io->Memory.FlushForCpu != APPLE_AGX_BACKEND_NULL &&
         Io->Image.AcquirePrepared != APPLE_AGX_BACKEND_NULL &&
         Io->Image.Relocate != APPLE_AGX_BACKEND_NULL &&
         Io->RenderContext.Publish != APPLE_AGX_BACKEND_NULL &&
         Io->RenderContext.Unpublish != APPLE_AGX_BACKEND_NULL &&
         Io->Queues.Create != APPLE_AGX_BACKEND_NULL &&
         Io->Queues.Destroy != APPLE_AGX_BACKEND_NULL &&
         Io->Queues.Run3d != APPLE_AGX_BACKEND_NULL &&
         Io->Queues.RunTa != APPLE_AGX_BACKEND_NULL &&
         Io->Queues.Stop != APPLE_AGX_BACKEND_NULL &&
         Io->Queues.Reset != APPLE_AGX_BACKEND_NULL &&
         Io->Complete != APPLE_AGX_BACKEND_NULL &&
         Io->Retire != APPLE_AGX_BACKEND_NULL;
}

static APPLE_AGX_BOOL AppleAgxBackendRootsValid(
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots) {
  return Roots->Ta[0] != 0ULL && Roots->Ta[1] != 0ULL &&
         Roots->D3[0] != 0ULL && Roots->D3[1] != 0ULL;
}

static void AppleAgxBackendClearPending(APPLE_AGX_BACKEND_RUNTIME *Runtime) {
  AppleAgxBackendZero(&Runtime->PendingSubmission,
                      (APPLE_AGX_U32)sizeof(Runtime->PendingSubmission));
  AppleAgxBackendZero(&Runtime->PendingJob,
                      (APPLE_AGX_U32)sizeof(Runtime->PendingJob));
  Runtime->TaComplete = APPLE_AGX_FALSE;
  Runtime->D3Complete = APPLE_AGX_FALSE;
  Runtime->TerminalPending = APPLE_AGX_FALSE;
  Runtime->TerminalCpuFlushed = APPLE_AGX_FALSE;
  Runtime->TerminalStatus = AppleAgxBackendCompletionSuccess;
  Runtime->TerminalNextPhase = AppleAgxBackendRuntimeStopped;
  Runtime->TerminalResult = AppleAgxBackendRuntimeResultInvalidState;
}

static APPLE_AGX_BACKEND_RUNTIME_RESULT AppleAgxBackendStartRollback(
    APPLE_AGX_BACKEND_RUNTIME *Runtime,
    APPLE_AGX_BACKEND_RUNTIME_RESULT Failure) {
  APPLE_AGX_BOOL cleanupFailed = APPLE_AGX_FALSE;

  if (Runtime->QueuesCreated) {
    if (!Runtime->Io.Queues.Destroy(Runtime->Io.Context))
      cleanupFailed = APPLE_AGX_TRUE;
    else {
      Runtime->QueuesCreated = APPLE_AGX_FALSE;
      Runtime->QueuesQuiesced = APPLE_AGX_FALSE;
    }
  }
  if (Runtime->ContextPublished) {
    if (!Runtime->Io.RenderContext.Unpublish(Runtime->Io.Context))
      cleanupFailed = APPLE_AGX_TRUE;
    else
      Runtime->ContextPublished = APPLE_AGX_FALSE;
  }
  if (Runtime->Firmware.CompletedMask != 0u &&
      AppleAgxFirmwareRollback(&Runtime->Firmware, &Runtime->Io.Firmware) !=
          AppleAgxFirmwareResultOk)
    cleanupFailed = APPLE_AGX_TRUE;
  if (Runtime->ArenaMapped) {
    if (!Runtime->Io.Memory.Unmap(Runtime->Io.Context,
                                  Runtime->ArenaCpuAddress,
                                  Runtime->ArenaBytes))
      cleanupFailed = APPLE_AGX_TRUE;
    else
      Runtime->ArenaMapped = APPLE_AGX_FALSE;
  }
  Runtime->Phase = cleanupFailed ? AppleAgxBackendRuntimeFailed
                                 : AppleAgxBackendRuntimeStopped;
  return cleanupFailed ? AppleAgxBackendRuntimeResultCleanupFailed : Failure;
}

void AppleAgxBackendRuntimeInitialize(APPLE_AGX_BACKEND_RUNTIME *Runtime,
                                      APPLE_AGX_U64 ContextIdentity) {
  if (Runtime == APPLE_AGX_BACKEND_NULL)
    return;
  AppleAgxBackendZero(Runtime, (APPLE_AGX_U32)sizeof(*Runtime));
  Runtime->ContextIdentity = ContextIdentity;
  Runtime->Phase = AppleAgxBackendRuntimeStopped;
  AppleAgxFirmwareInitialize(&Runtime->Firmware);
}

APPLE_AGX_BOOL AppleAgxBackendSubmissionFromEntry(
    APPLE_AGX_U64 ContextIdentity,
    const APPLE_AGX_SUBMISSION_ENTRY *Entry,
    const void *PrivateData,
    APPLE_AGX_BACKEND_SUBMISSION *Submission) {
  if (Submission == APPLE_AGX_BACKEND_NULL)
    return APPLE_AGX_FALSE;
  AppleAgxBackendZero(Submission, (APPLE_AGX_U32)sizeof(*Submission));
  if (Entry == APPLE_AGX_BACKEND_NULL || PrivateData == APPLE_AGX_BACKEND_NULL ||
      ContextIdentity == 0ULL ||
      Entry->Submission.Kind != AppleAgxSubmissionGdi ||
      Entry->Submission.Fence == 0u || Entry->PrivateDataToken == 0ULL ||
      Entry->PrivateDataBytes == 0u ||
      Entry->PrivateDataStart != 0u ||
      Entry->PrivateDataStart >= Entry->PrivateDataEnd ||
      Entry->PrivateDataEnd > Entry->PrivateDataBytes ||
      Entry->DmaSubmissionStart >= Entry->DmaSubmissionEnd ||
      Entry->DmaSubmissionEnd - Entry->DmaSubmissionStart !=
          Entry->Submission.DmaBytes)
    return APPLE_AGX_FALSE;
  Submission->Submission = Entry->Submission;
  Submission->ContextIdentity = ContextIdentity;
  Submission->PrivateData = PrivateData;
  Submission->PrivateDataBytes = Entry->PrivateDataBytes;
  Submission->PrivateDataStart = Entry->PrivateDataStart;
  Submission->PrivateDataEnd = Entry->PrivateDataEnd;
  Submission->DmaSubmissionStart = Entry->DmaSubmissionStart;
  Submission->DmaSubmissionEnd = Entry->DmaSubmissionEnd;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BACKEND_RUNTIME_RESULT AppleAgxBackendRuntimeStart(
    APPLE_AGX_BACKEND_RUNTIME *Runtime, const APPLE_AGX_BACKEND_IO *Io) {
  if (Runtime == APPLE_AGX_BACKEND_NULL || !AppleAgxBackendIoValid(Io) ||
      Runtime->ContextIdentity == 0ULL)
    return AppleAgxBackendRuntimeResultInvalidArgument;
  if (Runtime->Phase != AppleAgxBackendRuntimeStopped)
    return AppleAgxBackendRuntimeResultInvalidState;

  Runtime->Io = *Io;
  Runtime->Phase = AppleAgxBackendRuntimeStarting;
  if (!Runtime->Io.Memory.Map(Runtime->Io.Context, &Runtime->ArenaCpuAddress,
                              &Runtime->ArenaGpuAddress,
                              &Runtime->ArenaBytes) ||
      Runtime->ArenaCpuAddress == APPLE_AGX_BACKEND_NULL ||
      Runtime->ArenaGpuAddress == 0ULL || Runtime->ArenaBytes == 0u)
    return AppleAgxBackendStartRollback(
        Runtime, AppleAgxBackendRuntimeResultMemoryFailed);
  Runtime->ArenaMapped = APPLE_AGX_TRUE;
  if (!Runtime->Io.Image.AcquirePrepared(Runtime->Io.Context,
                                         Runtime->ArenaCpuAddress,
                                         Runtime->ArenaBytes,
                                         &Runtime->Roots) ||
      !AppleAgxBackendRootsValid(&Runtime->Roots))
    return AppleAgxBackendStartRollback(
        Runtime, AppleAgxBackendRuntimeResultImageFailed);
  if (!Runtime->Io.Memory.FlushForDevice(Runtime->Io.Context,
                                         Runtime->ArenaCpuAddress,
                                         Runtime->ArenaBytes))
    return AppleAgxBackendStartRollback(
        Runtime, AppleAgxBackendRuntimeResultMemoryFailed);

  AppleAgxFirmwareInitialize(&Runtime->Firmware);
  if (AppleAgxFirmwareStart(&Runtime->Firmware, &Runtime->Io.Firmware) !=
      AppleAgxFirmwareResultOk)
    return AppleAgxBackendStartRollback(
        Runtime, AppleAgxBackendRuntimeResultFirmwareFailed);
  if (!Runtime->Io.RenderContext.Publish(Runtime->Io.Context))
    return AppleAgxBackendStartRollback(
        Runtime, AppleAgxBackendRuntimeResultContextFailed);
  Runtime->ContextPublished = APPLE_AGX_TRUE;
  if (!Runtime->Io.Queues.Create(Runtime->Io.Context))
    return AppleAgxBackendStartRollback(
        Runtime, AppleAgxBackendRuntimeResultQueueFailed);
  Runtime->QueuesCreated = APPLE_AGX_TRUE;
  Runtime->QueuesQuiesced = APPLE_AGX_FALSE;
  Runtime->Phase = AppleAgxBackendRuntimeReady;
  return AppleAgxBackendRuntimeResultOk;
}

static APPLE_AGX_BOOL AppleAgxBackendSubmissionValid(
    const APPLE_AGX_BACKEND_RUNTIME *Runtime,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission) {
  return Submission != APPLE_AGX_BACKEND_NULL &&
         Submission->ContextIdentity == Runtime->ContextIdentity &&
         Submission->Submission.Kind == AppleAgxSubmissionGdi &&
         Submission->Submission.Fence != 0u &&
         Submission->Submission.NodeOrdinal == 0u &&
         Submission->Submission.EngineOrdinal == 0u &&
         Submission->PrivateData != APPLE_AGX_BACKEND_NULL &&
         Submission->PrivateDataBytes != 0u &&
         Submission->PrivateDataStart == 0u &&
         Submission->PrivateDataStart < Submission->PrivateDataEnd &&
         Submission->PrivateDataEnd <= Submission->PrivateDataBytes &&
         Submission->DmaSubmissionStart < Submission->DmaSubmissionEnd &&
         Submission->DmaSubmissionEnd - Submission->DmaSubmissionStart ==
             Submission->Submission.DmaBytes;
}

static APPLE_AGX_BOOL
AppleAgxBackendJobValid(const APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  APPLE_AGX_U32 index;
  if (Job->TaWorkAddressCount != APPLE_AGX_BACKEND_QUEUE_WORK_COUNT ||
      Job->D3WorkAddressCount != APPLE_AGX_BACKEND_QUEUE_WORK_COUNT ||
      Job->TaExpectedStamp == 0u || Job->D3ExpectedStamp == 0u)
    return APPLE_AGX_FALSE;
  for (index = 0u; index < APPLE_AGX_BACKEND_QUEUE_WORK_COUNT; ++index) {
    if (Job->TaWorkAddresses[index] == 0ULL ||
        Job->D3WorkAddresses[index] == 0ULL)
      return APPLE_AGX_FALSE;
  }
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BACKEND_RUNTIME_RESULT AppleAgxBackendRuntimeSubmit(
    APPLE_AGX_BACKEND_RUNTIME *Runtime,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission) {
  const unsigned char *submissionBytes = APPLE_AGX_BACKEND_NULL;
  APPLE_AGX_U32 submissionByteCount = 0u;

  if (Runtime == APPLE_AGX_BACKEND_NULL)
    return AppleAgxBackendRuntimeResultInvalidArgument;
  if (Runtime->Phase == AppleAgxBackendRuntimeSubmitted)
    return AppleAgxBackendRuntimeResultBusy;
  if (Runtime->Phase != AppleAgxBackendRuntimeReady)
    return AppleAgxBackendRuntimeResultInvalidState;
  if (!AppleAgxBackendSubmissionValid(Runtime, Submission))
    return AppleAgxBackendRuntimeResultInvalidArgument;
  if (!Runtime->Io.Memory.Resolve(Runtime->Io.Context, Submission,
                                  &submissionBytes,
                                  &submissionByteCount) ||
      submissionBytes == APPLE_AGX_BACKEND_NULL ||
      submissionByteCount !=
          Submission->DmaSubmissionEnd - Submission->DmaSubmissionStart)
    return AppleAgxBackendRuntimeResultMemoryFailed;
  if (!Runtime->Io.Image.Relocate(
          Runtime->Io.Context, Runtime->ArenaCpuAddress, Runtime->ArenaBytes,
          &Runtime->Roots, submissionBytes, submissionByteCount, Submission,
          &Runtime->PendingJob) ||
      !AppleAgxBackendJobValid(&Runtime->PendingJob))
    return AppleAgxBackendRuntimeResultImageFailed;
  if (!Runtime->Io.Memory.FlushForDevice(Runtime->Io.Context,
                                         Runtime->ArenaCpuAddress,
                                         Runtime->ArenaBytes))
    return AppleAgxBackendRuntimeResultMemoryFailed;

  Runtime->PendingSubmission = *Submission;
  Runtime->TaComplete = APPLE_AGX_FALSE;
  Runtime->D3Complete = APPLE_AGX_FALSE;
  if (!Runtime->Io.Queues.Run3d(Runtime->Io.Context, &Runtime->PendingJob,
                                Submission->Submission.Fence)) {
    /*
     * Run3d is the provider's atomic staging admission.  FALSE means the
     * exact fence was never accepted by the queue owner and is therefore not
     * stoppable.  Drop only the unaccepted runtime shadow and remain ready.
     */
    AppleAgxBackendClearPending(Runtime);
    Runtime->Phase = AppleAgxBackendRuntimeReady;
    return AppleAgxBackendRuntimeResultQueueFailed;
  }
  if (!Runtime->Io.Queues.RunTa(Runtime->Io.Context, &Runtime->PendingJob,
                                Submission->Submission.Fence)) {
    if (Runtime->Io.Queues.Stop(Runtime->Io.Context,
                                Submission->Submission.Fence))
      Runtime->QueuesQuiesced = APPLE_AGX_TRUE;
    Runtime->Phase = AppleAgxBackendRuntimeFailed;
    return AppleAgxBackendRuntimeResultQueueFailed;
  }
  Runtime->Phase = AppleAgxBackendRuntimeSubmitted;
  return AppleAgxBackendRuntimeResultOk;
}

static APPLE_AGX_BOOL AppleAgxBackendStampReached(APPLE_AGX_U32 Actual,
                                                   APPLE_AGX_U32 Expected) {
  return (APPLE_AGX_U32)(Actual - Expected) < 0x80000000u;
}

APPLE_AGX_BACKEND_RUNTIME_RESULT AppleAgxBackendRuntimeAcknowledgeCompletion(
    APPLE_AGX_BACKEND_RUNTIME *Runtime) {
  APPLE_AGX_BACKEND_RUNTIME_RESULT result;
  APPLE_AGX_BACKEND_RUNTIME_PHASE nextPhase;
  APPLE_AGX_U32 fence;
  APPLE_AGX_U32 node;
  APPLE_AGX_U32 engine;

  if (Runtime == APPLE_AGX_BACKEND_NULL)
    return AppleAgxBackendRuntimeResultInvalidArgument;
  if (!Runtime->TerminalPending ||
      Runtime->PendingSubmission.Submission.Fence == 0u)
    return AppleAgxBackendRuntimeResultInvalidState;
  fence = Runtime->PendingSubmission.Submission.Fence;
  node = Runtime->PendingSubmission.Submission.NodeOrdinal;
  engine = Runtime->PendingSubmission.Submission.EngineOrdinal;
  if (Runtime->TerminalStatus == AppleAgxBackendCompletionSuccess &&
      !Runtime->TerminalCpuFlushed) {
    if (!Runtime->Io.Memory.FlushForCpu(Runtime->Io.Context,
                                        Runtime->ArenaCpuAddress,
                                        Runtime->ArenaBytes))
      return AppleAgxBackendRuntimeResultMemoryFailed;
    Runtime->TerminalCpuFlushed = APPLE_AGX_TRUE;
  }
  if (Runtime->TerminalStatus == AppleAgxBackendCompletionCancelled) {
    if (!Runtime->Io.Retire(Runtime->Io.Context, fence, node, engine))
      return AppleAgxBackendRuntimeResultBusy;
  } else if (!Runtime->Io.Complete(Runtime->Io.Context, fence, node, engine,
                                   Runtime->TerminalStatus)) {
    return AppleAgxBackendRuntimeResultBusy;
  }
  result = Runtime->TerminalResult;
  nextPhase = Runtime->TerminalNextPhase;
  AppleAgxBackendClearPending(Runtime);
  Runtime->Phase = nextPhase;
  return result;
}

static APPLE_AGX_BACKEND_RUNTIME_RESULT AppleAgxBackendTerminal(
    APPLE_AGX_BACKEND_RUNTIME *Runtime,
    APPLE_AGX_BACKEND_COMPLETION_STATUS Status,
    APPLE_AGX_BACKEND_RUNTIME_PHASE NextPhase,
    APPLE_AGX_BACKEND_RUNTIME_RESULT Result) {
  if (Runtime->TerminalPending ||
      Runtime->PendingSubmission.Submission.Fence == 0u)
    return AppleAgxBackendRuntimeResultInvalidState;
  Runtime->TerminalPending = APPLE_AGX_TRUE;
  Runtime->TerminalCpuFlushed = APPLE_AGX_FALSE;
  Runtime->TerminalStatus = Status;
  Runtime->TerminalNextPhase = NextPhase;
  Runtime->TerminalResult = Result;
  if (Status != AppleAgxBackendCompletionSuccess)
    return Result;
  return AppleAgxBackendRuntimeAcknowledgeCompletion(Runtime);
}

APPLE_AGX_BACKEND_RUNTIME_RESULT AppleAgxBackendRuntimeObserve(
    APPLE_AGX_BACKEND_RUNTIME *Runtime,
    const APPLE_AGX_BACKEND_OBSERVATION *Observation) {
  APPLE_AGX_BOOL *complete;
  APPLE_AGX_U32 expectedEvent;
  APPLE_AGX_U32 expectedStamp;
  APPLE_AGX_U32 expectedDone;

  if (Runtime == APPLE_AGX_BACKEND_NULL ||
      Observation == APPLE_AGX_BACKEND_NULL)
    return AppleAgxBackendRuntimeResultInvalidArgument;
  if (Runtime->TerminalPending &&
      Runtime->TerminalStatus != AppleAgxBackendCompletionSuccess)
    return Runtime->TerminalResult;
  if (Runtime->TerminalPending)
    return AppleAgxBackendRuntimeAcknowledgeCompletion(Runtime);
  if (Runtime->Phase != AppleAgxBackendRuntimeSubmitted)
    return AppleAgxBackendRuntimeResultInvalidState;
  if (Observation->Status != AppleAgxBackendObservationComplete) {
    APPLE_AGX_BACKEND_COMPLETION_STATUS completionStatus;
    APPLE_AGX_BOOL stopped;
    if (Observation->Status == AppleAgxBackendObservationFault)
      completionStatus = AppleAgxBackendCompletionFaulted;
    else if (Observation->Status == AppleAgxBackendObservationTimeout)
      completionStatus = AppleAgxBackendCompletionTimedOut;
    else if (Observation->Status == AppleAgxBackendObservationReset)
      completionStatus = AppleAgxBackendCompletionReset;
    else
      return AppleAgxBackendRuntimeResultInvalidArgument;
    stopped = Observation->Status == AppleAgxBackendObservationReset
                  ? Runtime->Io.Queues.Reset(
                        Runtime->Io.Context,
                        Runtime->PendingSubmission.Submission.Fence)
                  : Runtime->Io.Queues.Stop(
                        Runtime->Io.Context,
                        Runtime->PendingSubmission.Submission.Fence);
    if (!stopped) {
      Runtime->Phase = AppleAgxBackendRuntimeFailed;
      return AppleAgxBackendRuntimeResultCleanupFailed;
    }
    Runtime->QueuesQuiesced = APPLE_AGX_TRUE;
    return AppleAgxBackendTerminal(Runtime, completionStatus,
                                   AppleAgxBackendRuntimeFailed,
                                   AppleAgxBackendRuntimeResultFaulted);
  }

  if (Observation->Queue == AppleAgxBackendQueueTa) {
    complete = &Runtime->TaComplete;
    expectedEvent = Runtime->PendingJob.TaEvent;
    expectedStamp = Runtime->PendingJob.TaExpectedStamp;
    expectedDone = Runtime->PendingJob.TaExpectedDonePointer;
  } else if (Observation->Queue == AppleAgxBackendQueue3d) {
    complete = &Runtime->D3Complete;
    expectedEvent = Runtime->PendingJob.D3Event;
    expectedStamp = Runtime->PendingJob.D3ExpectedStamp;
    expectedDone = Runtime->PendingJob.D3ExpectedDonePointer;
  } else {
    return AppleAgxBackendRuntimeResultInvalidArgument;
  }
  if (*complete || Observation->Event != expectedEvent ||
      Observation->DonePointer != expectedDone ||
      !AppleAgxBackendStampReached(Observation->Stamp, expectedStamp))
    return AppleAgxBackendRuntimeResultInvalidArgument;
  *complete = APPLE_AGX_TRUE;
  if (Runtime->TaComplete && Runtime->D3Complete)
    return AppleAgxBackendTerminal(Runtime, AppleAgxBackendCompletionSuccess,
                                   AppleAgxBackendRuntimeReady,
                                   AppleAgxBackendRuntimeResultOk);
  return AppleAgxBackendRuntimeResultOk;
}

APPLE_AGX_BACKEND_RUNTIME_RESULT AppleAgxBackendRuntimeStop(
    APPLE_AGX_BACKEND_RUNTIME *Runtime) {
  APPLE_AGX_BACKEND_RUNTIME_RESULT retirementResult;
  APPLE_AGX_U32 pendingFence;

  if (Runtime == APPLE_AGX_BACKEND_NULL)
    return AppleAgxBackendRuntimeResultInvalidArgument;
  if (Runtime->Phase == AppleAgxBackendRuntimeStopped)
    return AppleAgxBackendRuntimeResultOk;
  if (Runtime->Phase != AppleAgxBackendRuntimeReady &&
      Runtime->Phase != AppleAgxBackendRuntimeSubmitted &&
      Runtime->Phase != AppleAgxBackendRuntimeFailed)
    return AppleAgxBackendRuntimeResultInvalidState;

  /*
   * A natural dual-queue completion is the only path that may advance the
   * Windows DMA fence.  If that receipt is pending, retry it before teardown;
   * every other terminal state is converted to a distinct teardown-only
   * retirement after hardware quiesce is proven.
   */
  if (Runtime->TerminalPending &&
      Runtime->TerminalStatus == AppleAgxBackendCompletionSuccess) {
    retirementResult = AppleAgxBackendRuntimeAcknowledgeCompletion(Runtime);
    if (retirementResult != AppleAgxBackendRuntimeResultOk) {
      Runtime->Phase = AppleAgxBackendRuntimeFailed;
      return AppleAgxBackendRuntimeResultCleanupFailed;
    }
  }

  pendingFence = Runtime->PendingSubmission.Submission.Fence;
  Runtime->Phase = AppleAgxBackendRuntimeStopping;
  if (Runtime->QueuesCreated && !Runtime->QueuesQuiesced) {
    if (!Runtime->Io.Queues.Stop(Runtime->Io.Context, pendingFence)) {
      /*
       * A failed stop is not a teardown receipt.  Keep every DMA-visible
       * owner and the exact pending fence intact so a later proven stop or
       * whole-device recovery can retry without use-after-free.
       */
      Runtime->Phase = AppleAgxBackendRuntimeFailed;
      return AppleAgxBackendRuntimeResultCleanupFailed;
    }
    Runtime->QueuesQuiesced = APPLE_AGX_TRUE;
  }

  if (pendingFence != 0u) {
    Runtime->TerminalPending = APPLE_AGX_TRUE;
    Runtime->TerminalCpuFlushed = APPLE_AGX_FALSE;
    Runtime->TerminalStatus = AppleAgxBackendCompletionCancelled;
    Runtime->TerminalNextPhase = AppleAgxBackendRuntimeStopping;
    Runtime->TerminalResult = AppleAgxBackendRuntimeResultOk;
    retirementResult = AppleAgxBackendRuntimeAcknowledgeCompletion(Runtime);
    if (retirementResult != AppleAgxBackendRuntimeResultOk) {
      Runtime->Phase = AppleAgxBackendRuntimeFailed;
      return AppleAgxBackendRuntimeResultCleanupFailed;
    }
  }

  if (Runtime->QueuesCreated) {
    if (!Runtime->Io.Queues.Destroy(Runtime->Io.Context)) {
      Runtime->Phase = AppleAgxBackendRuntimeFailed;
      return AppleAgxBackendRuntimeResultCleanupFailed;
    }
    Runtime->QueuesCreated = APPLE_AGX_FALSE;
    Runtime->QueuesQuiesced = APPLE_AGX_FALSE;
  }
  if (Runtime->ContextPublished) {
    if (!Runtime->Io.RenderContext.Unpublish(Runtime->Io.Context)) {
      Runtime->Phase = AppleAgxBackendRuntimeFailed;
      return AppleAgxBackendRuntimeResultCleanupFailed;
    }
    Runtime->ContextPublished = APPLE_AGX_FALSE;
  }
  if (AppleAgxFirmwareRollback(&Runtime->Firmware, &Runtime->Io.Firmware) !=
      AppleAgxFirmwareResultOk) {
    Runtime->Phase = AppleAgxBackendRuntimeFailed;
    return AppleAgxBackendRuntimeResultCleanupFailed;
  }
  if (Runtime->ArenaMapped) {
    if (!Runtime->Io.Memory.Unmap(Runtime->Io.Context,
                                  Runtime->ArenaCpuAddress,
                                  Runtime->ArenaBytes)) {
      Runtime->Phase = AppleAgxBackendRuntimeFailed;
      return AppleAgxBackendRuntimeResultCleanupFailed;
    }
    Runtime->ArenaMapped = APPLE_AGX_FALSE;
  }
  Runtime->Phase = AppleAgxBackendRuntimeStopped;
  return AppleAgxBackendRuntimeResultOk;
}
