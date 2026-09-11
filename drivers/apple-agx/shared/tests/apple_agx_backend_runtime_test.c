#include "apple_agx_backend_runtime.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
  OP_MAP = 1,
  OP_UNMAP,
  OP_ACQUIRE_PREPARED,
  OP_RELOCATE,
  OP_FLUSH_DEVICE,
  OP_FLUSH_CPU,
  OP_PUBLISH_CONTEXT,
  OP_UNPUBLISH_CONTEXT,
  OP_CREATE_QUEUES,
  OP_DESTROY_QUEUES,
  OP_RUN_3D,
  OP_RUN_TA,
  OP_STOP_QUEUES,
  OP_RESET_QUEUES,
  OP_COMPLETE,
};

typedef struct _FAKE_BACKEND {
  unsigned char Arena[4096];
  unsigned char PrivateData[64];
  unsigned int Operations[128];
  unsigned int OperationCount;
  unsigned int CompletionCount;
  unsigned int CompletionAttempts;
  unsigned int RejectedBridgeRetireAttempts;
  unsigned int WindowsNotificationCount;
  unsigned int WindowsNotifiedFence;
  unsigned int CompletionFence;
  unsigned int CompletionNode;
  unsigned int CompletionEngine;
  APPLE_AGX_BACKEND_COMPLETION_STATUS CompletionStatus;
  APPLE_AGX_SUBMISSION_QUEUE *WindowsQueue;
  unsigned int LastRelocatedFence;
  unsigned int FailStop;
  unsigned int FailRun3d;
} FAKE_BACKEND;

static APPLE_AGX_BOOL Record(FAKE_BACKEND *Fake, unsigned int Operation) {
  assert(Fake->OperationCount < 128u);
  Fake->Operations[Fake->OperationCount++] = Operation;
  return APPLE_AGX_TRUE;
}

static unsigned int CountOperation(const FAKE_BACKEND *Fake,
                                   unsigned int Operation) {
  unsigned int count = 0u;
  unsigned int index;
  for (index = 0u; index < Fake->OperationCount; ++index) {
    if (Fake->Operations[index] == Operation)
      ++count;
  }
  return count;
}

static APPLE_AGX_FW_U64 FwNow(void *Context) {
  (void)Context;
  return 1000ULL;
}

static APPLE_AGX_FW_BOOL FwOk(void *Context, APPLE_AGX_FW_U64 Deadline) {
  (void)Context;
  (void)Deadline;
  return 1u;
}

static APPLE_AGX_FW_BOOL FwEndpoint(void *Context,
                                    APPLE_AGX_FW_U32 Endpoint,
                                    APPLE_AGX_FW_U64 Deadline) {
  (void)Context;
  (void)Endpoint;
  (void)Deadline;
  return 1u;
}

static APPLE_AGX_FW_BOOL FwPublish(void *Context,
                                   APPLE_AGX_FW_U64 Deadline,
                                   APPLE_AGX_FW_U64 *Address) {
  (void)Context;
  (void)Deadline;
  *Address = 0x100000000ULL;
  return 1u;
}

static APPLE_AGX_FW_BOOL FwSend(void *Context, APPLE_AGX_FW_U64 Address,
                                APPLE_AGX_FW_U64 Deadline) {
  (void)Context;
  (void)Address;
  (void)Deadline;
  return 1u;
}

static void FwRecord(void *Context, APPLE_AGX_FIRMWARE_PHASE Phase,
                     APPLE_AGX_FW_U32 CompletedMask,
                     APPLE_AGX_FIRMWARE_RESULT Result) {
  (void)Context;
  (void)Phase;
  (void)CompletedMask;
  (void)Result;
}

static APPLE_AGX_FIRMWARE_IO FirmwareIo(FAKE_BACKEND *Fake) {
  APPLE_AGX_FIRMWARE_IO io;
  memset(&io, 0, sizeof(io));
  io.Context = Fake;
  io.NowMs = FwNow;
  io.PowerOn = FwOk;
  io.CreateFirmwareUat = FwOk;
  io.BootAsc = FwOk;
  io.StartEndpoint = FwEndpoint;
  io.PublishInitdata = FwPublish;
  io.SendInitdata = FwSend;
  io.SendDeviceControlInit = FwOk;
  io.UpdateIdleTimestamp = FwOk;
  io.UnpublishInitdata = FwOk;
  io.StopEndpoint = FwEndpoint;
  io.StopAsc = FwOk;
  io.DestroyFirmwareUat = FwOk;
  io.PowerOff = FwOk;
  io.RecordPhase = FwRecord;
  return io;
}

static APPLE_AGX_BOOL Map(void *Context, void **CpuAddress,
                          APPLE_AGX_U64 *GpuAddress,
                          APPLE_AGX_U32 *Bytes) {
  FAKE_BACKEND *fake = (FAKE_BACKEND *)Context;
  *CpuAddress = fake->Arena;
  *GpuAddress = APPLE_AGX_RENDER_TEMPLATE_GPU_BASE;
  *Bytes = sizeof(fake->Arena);
  return Record(fake, OP_MAP);
}

static APPLE_AGX_BOOL Unmap(void *Context, void *CpuAddress,
                            APPLE_AGX_U32 Bytes) {
  FAKE_BACKEND *fake = (FAKE_BACKEND *)Context;
  assert(CpuAddress == fake->Arena && Bytes == sizeof(fake->Arena));
  return Record(fake, OP_UNMAP);
}

static APPLE_AGX_BOOL Resolve(
    void *Context, const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    const unsigned char **Bytes, APPLE_AGX_U32 *ByteCount) {
  FAKE_BACKEND *fake = (FAKE_BACKEND *)Context;
  assert(Submission->PrivateDataStart == 0u);
  assert(Submission->PrivateDataEnd == 24u);
  assert(Submission->DmaSubmissionStart == 32u);
  assert(Submission->DmaSubmissionEnd == 48u);
  *Bytes = fake->PrivateData + 8u;
  *ByteCount = 16u;
  return APPLE_AGX_TRUE;
}

static APPLE_AGX_BOOL FlushDevice(void *Context, const void *Address,
                                  APPLE_AGX_U32 Bytes) {
  FAKE_BACKEND *fake = (FAKE_BACKEND *)Context;
  assert(Address == fake->Arena && Bytes == sizeof(fake->Arena));
  return Record(fake, OP_FLUSH_DEVICE);
}

static APPLE_AGX_BOOL FlushCpu(void *Context, const void *Address,
                               APPLE_AGX_U32 Bytes) {
  FAKE_BACKEND *fake = (FAKE_BACKEND *)Context;
  assert(Address == fake->Arena && Bytes == sizeof(fake->Arena));
  return Record(fake, OP_FLUSH_CPU);
}

static APPLE_AGX_BOOL AcquirePrepared(
    void *Context, void *Arena, APPLE_AGX_U32 ArenaBytes,
    APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots) {
  FAKE_BACKEND *fake = (FAKE_BACKEND *)Context;
  assert(Arena == fake->Arena && ArenaBytes == sizeof(fake->Arena));
  Roots->Ta[0] = 0x1503880000ULL;
  Roots->Ta[1] = 0x1503898000ULL;
  Roots->D3[0] = 0x1503870000ULL;
  Roots->D3[1] = 0x1503890000ULL;
  return Record(fake, OP_ACQUIRE_PREPARED);
}

static APPLE_AGX_BOOL Relocate(
    void *Context, void *Arena, APPLE_AGX_U32 ArenaBytes,
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots,
    const unsigned char *SubmissionBytes,
    APPLE_AGX_U32 SubmissionByteCount,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  FAKE_BACKEND *fake = (FAKE_BACKEND *)Context;
  assert(Arena == fake->Arena && ArenaBytes == sizeof(fake->Arena));
  assert(Roots->Ta[0] == 0x1503880000ULL);
  assert(SubmissionBytes == fake->PrivateData + 8u);
  assert(SubmissionByteCount == 16u);
  assert(Submission->Submission.Fence != 0u);
  fake->LastRelocatedFence = Submission->Submission.Fence;
  Job->TaWorkAddresses[0] = Roots->Ta[0];
  Job->TaWorkAddresses[1] = Roots->Ta[1];
  Job->D3WorkAddresses[0] = Roots->D3[0];
  Job->D3WorkAddresses[1] = Roots->D3[1];
  Job->TaWorkAddressCount = APPLE_AGX_BACKEND_QUEUE_WORK_COUNT;
  Job->D3WorkAddressCount = APPLE_AGX_BACKEND_QUEUE_WORK_COUNT;
  Job->TaEvent = 5u;
  Job->D3Event = 6u;
  Job->TaExpectedStamp = 0x7a000100u;
  Job->D3ExpectedStamp = 0x3d000100u;
  Job->TaExpectedDonePointer = 2u;
  Job->D3ExpectedDonePointer = 2u;
  return Record(fake, OP_RELOCATE);
}

static APPLE_AGX_BOOL SimplePublish(void *Context) {
  return Record((FAKE_BACKEND *)Context, OP_PUBLISH_CONTEXT);
}

static APPLE_AGX_BOOL SimpleUnpublish(void *Context) {
  return Record((FAKE_BACKEND *)Context, OP_UNPUBLISH_CONTEXT);
}

static APPLE_AGX_BOOL CreateQueues(void *Context) {
  return Record((FAKE_BACKEND *)Context, OP_CREATE_QUEUES);
}

static APPLE_AGX_BOOL DestroyQueues(void *Context) {
  return Record((FAKE_BACKEND *)Context, OP_DESTROY_QUEUES);
}

static APPLE_AGX_BOOL Run3d(void *Context,
                            const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
                            APPLE_AGX_U32 Fence) {
  FAKE_BACKEND *fake = (FAKE_BACKEND *)Context;
  assert(Job->D3WorkAddresses[0] == 0x1503870000ULL &&
         Job->D3WorkAddresses[1] == 0x1503890000ULL &&
         Fence == fake->LastRelocatedFence);
  (void)Record(fake, OP_RUN_3D);
  return fake->FailRun3d ? APPLE_AGX_FALSE : APPLE_AGX_TRUE;
}

static APPLE_AGX_BOOL RunTa(void *Context,
                            const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
                            APPLE_AGX_U32 Fence) {
  FAKE_BACKEND *fake = (FAKE_BACKEND *)Context;
  assert(Job->TaWorkAddresses[0] == 0x1503880000ULL &&
         Job->TaWorkAddresses[1] == 0x1503898000ULL &&
         Fence == fake->LastRelocatedFence);
  return Record(fake, OP_RUN_TA);
}

static APPLE_AGX_BOOL StopQueues(void *Context, APPLE_AGX_U32 Fence) {
  FAKE_BACKEND *fake = Context;
  assert(Fence == 0u || Fence == fake->LastRelocatedFence);
  (void)Record(fake, OP_STOP_QUEUES);
  return fake->FailStop ? APPLE_AGX_FALSE : APPLE_AGX_TRUE;
}

static APPLE_AGX_BOOL ResetQueues(void *Context, APPLE_AGX_U32 Fence) {
  assert(Fence != 0u && Fence == ((FAKE_BACKEND *)Context)->LastRelocatedFence);
  return Record((FAKE_BACKEND *)Context, OP_RESET_QUEUES);
}

static APPLE_AGX_BOOL Complete(
    void *Context, APPLE_AGX_U32 SubmissionFence,
    APPLE_AGX_U32 NodeOrdinal, APPLE_AGX_U32 EngineOrdinal,
    APPLE_AGX_BACKEND_COMPLETION_STATUS Status) {
  FAKE_BACKEND *fake = (FAKE_BACKEND *)Context;
  ++fake->CompletionAttempts;
  if (fake->RejectedBridgeRetireAttempts != 0u) {
    --fake->RejectedBridgeRetireAttempts;
    return APPLE_AGX_FALSE;
  }
  if (Status == AppleAgxBackendCompletionSuccess &&
      fake->WindowsNotifiedFence == 0u) {
    fake->WindowsNotifiedFence = SubmissionFence;
    ++fake->WindowsNotificationCount;
    if (fake->WindowsQueue != NULL)
      assert(AppleAgxSubmissionQueueComplete(fake->WindowsQueue,
                                             SubmissionFence));
  } else if (Status == AppleAgxBackendCompletionSuccess) {
    assert(fake->WindowsNotifiedFence == SubmissionFence);
  }
  (void)Record(fake, OP_COMPLETE);
  ++fake->CompletionCount;
  fake->CompletionFence = SubmissionFence;
  fake->CompletionNode = NodeOrdinal;
  fake->CompletionEngine = EngineOrdinal;
  fake->CompletionStatus = Status;
  if (Status == AppleAgxBackendCompletionSuccess)
    fake->WindowsNotifiedFence = 0u;
  return APPLE_AGX_TRUE;
}

static APPLE_AGX_BOOL Retire(void *Context,
                             APPLE_AGX_U32 SubmissionFence,
                             APPLE_AGX_U32 NodeOrdinal,
                             APPLE_AGX_U32 EngineOrdinal) {
  return Complete(Context, SubmissionFence, NodeOrdinal, EngineOrdinal,
                  AppleAgxBackendCompletionCancelled);
}

static APPLE_AGX_BACKEND_IO BackendIo(FAKE_BACKEND *Fake) {
  APPLE_AGX_BACKEND_IO io;
  memset(&io, 0, sizeof(io));
  io.Context = Fake;
  io.Firmware = FirmwareIo(Fake);
  io.Memory.Map = Map;
  io.Memory.Unmap = Unmap;
  io.Memory.Resolve = Resolve;
  io.Memory.FlushForDevice = FlushDevice;
  io.Memory.FlushForCpu = FlushCpu;
  io.Image.AcquirePrepared = AcquirePrepared;
  io.Image.Relocate = Relocate;
  io.RenderContext.Publish = SimplePublish;
  io.RenderContext.Unpublish = SimpleUnpublish;
  io.Queues.Create = CreateQueues;
  io.Queues.Destroy = DestroyQueues;
  io.Queues.Run3d = Run3d;
  io.Queues.RunTa = RunTa;
  io.Queues.Stop = StopQueues;
  io.Queues.Reset = ResetQueues;
  io.Complete = Complete;
  io.Retire = Retire;
  return io;
}

static APPLE_AGX_BACKEND_SUBMISSION Submission(FAKE_BACKEND *Fake) {
  APPLE_AGX_BACKEND_SUBMISSION submission;
  memset(&submission, 0, sizeof(submission));
  submission.Submission.Kind = AppleAgxSubmissionGdi;
  submission.Submission.Fence = 77u;
  submission.Submission.DmaBytes = 16u;
  submission.ContextIdentity = 3ULL;
  submission.PrivateData = Fake->PrivateData;
  submission.PrivateDataBytes = sizeof(Fake->PrivateData);
  submission.PrivateDataStart = 0u;
  submission.PrivateDataEnd = 24u;
  submission.DmaSubmissionStart = 32u;
  submission.DmaSubmissionEnd = 48u;
  return submission;
}

static void TestJoinedCompletion(void) {
  FAKE_BACKEND fake;
  APPLE_AGX_BACKEND_RUNTIME runtime;
  APPLE_AGX_BACKEND_IO io;
  APPLE_AGX_BACKEND_SUBMISSION submission;
  APPLE_AGX_BACKEND_OBSERVATION observation;
  memset(&fake, 0, sizeof(fake));
  io = BackendIo(&fake);
  submission = Submission(&fake);
  AppleAgxBackendRuntimeInitialize(&runtime, 3ULL);
  assert(AppleAgxBackendRuntimeStart(&runtime, &io) ==
         AppleAgxBackendRuntimeResultOk);
  assert(AppleAgxBackendRuntimeSubmit(&runtime, &submission) ==
         AppleAgxBackendRuntimeResultOk);

  memset(&observation, 0, sizeof(observation));
  observation.Status = AppleAgxBackendObservationComplete;
  observation.Queue = AppleAgxBackendQueue3d;
  observation.Event = 6u;
  observation.Stamp = 0x3d000100u;
  observation.DonePointer = 2u;
  assert(AppleAgxBackendRuntimeObserve(&runtime, &observation) ==
         AppleAgxBackendRuntimeResultOk);
  assert(fake.CompletionCount == 0u);

  observation.Queue = AppleAgxBackendQueueTa;
  observation.Event = 5u;
  observation.Stamp = 0x7a000100u;
  assert(AppleAgxBackendRuntimeObserve(&runtime, &observation) ==
         AppleAgxBackendRuntimeResultOk);
  assert(runtime.Phase == AppleAgxBackendRuntimeReady);
  assert(fake.CompletionCount == 1u && fake.CompletionFence == 77u);
  assert(fake.CompletionStatus == AppleAgxBackendCompletionSuccess);
  assert(fake.Operations[fake.OperationCount - 2u] == OP_FLUSH_CPU);
  assert(fake.Operations[fake.OperationCount - 1u] == OP_COMPLETE);
  assert(AppleAgxBackendRuntimeStop(&runtime) ==
         AppleAgxBackendRuntimeResultOk);
}

static void TestCanonicalEntryBridge(void) {
  FAKE_BACKEND fake;
  APPLE_AGX_SUBMISSION_ENTRY entry;
  APPLE_AGX_BACKEND_SUBMISSION submission;
  memset(&fake, 0, sizeof(fake));
  memset(&entry, 0, sizeof(entry));
  entry.Submission.Kind = AppleAgxSubmissionGdi;
  entry.Submission.Fence = 91u;
  entry.Submission.DmaBytes = 16u;
  entry.PrivateDataToken =
      (APPLE_AGX_U64)(unsigned long long)fake.PrivateData;
  entry.PrivateDataBytes = sizeof(fake.PrivateData);
  entry.PrivateDataStart = 0u;
  entry.PrivateDataEnd = 20u;
  entry.DmaSubmissionStart = 64u;
  entry.DmaSubmissionEnd = 80u;
  assert(AppleAgxBackendSubmissionFromEntry(3ULL, &entry, fake.PrivateData,
                                            &submission));
  assert(submission.Submission.Fence == 91u);
  assert(submission.ContextIdentity == 3ULL);
  assert(submission.PrivateData == fake.PrivateData);
  assert(submission.PrivateDataStart == 0u);
  assert(submission.PrivateDataEnd == 20u);
  assert(submission.DmaSubmissionStart == 64u);
  assert(submission.DmaSubmissionEnd == 80u);
}

static void TestResetCompletesExactFenceOnce(void) {
  FAKE_BACKEND fake;
  APPLE_AGX_BACKEND_RUNTIME runtime;
  APPLE_AGX_BACKEND_IO io;
  APPLE_AGX_BACKEND_SUBMISSION submission;
  APPLE_AGX_BACKEND_OBSERVATION observation;
  memset(&fake, 0, sizeof(fake));
  io = BackendIo(&fake);
  submission = Submission(&fake);
  AppleAgxBackendRuntimeInitialize(&runtime, 3ULL);
  assert(AppleAgxBackendRuntimeStart(&runtime, &io) ==
         AppleAgxBackendRuntimeResultOk);
  assert(AppleAgxBackendRuntimeSubmit(&runtime, &submission) ==
         AppleAgxBackendRuntimeResultOk);
  memset(&observation, 0, sizeof(observation));
  observation.Status = AppleAgxBackendObservationReset;
  assert(AppleAgxBackendRuntimeObserve(&runtime, &observation) ==
         AppleAgxBackendRuntimeResultFaulted);
  assert(fake.CompletionAttempts == 0u);
  assert(fake.CompletionCount == 0u);
  assert(runtime.QueuesQuiesced);
  assert(runtime.TerminalPending);
  assert(runtime.TerminalStatus == AppleAgxBackendCompletionReset);
  assert(runtime.PendingSubmission.Submission.Fence == 77u);
  assert(AppleAgxBackendRuntimeObserve(&runtime, &observation) ==
         AppleAgxBackendRuntimeResultFaulted);
  assert(fake.CompletionAttempts == 0u);
  assert(AppleAgxBackendRuntimeStop(&runtime) ==
         AppleAgxBackendRuntimeResultOk);
  assert(fake.CompletionCount == 1u && fake.CompletionFence == 77u);
  assert(fake.CompletionStatus == AppleAgxBackendCompletionCancelled);
  assert(CountOperation(&fake, OP_RESET_QUEUES) == 1u);
  assert(CountOperation(&fake, OP_STOP_QUEUES) == 0u);
}

static void TestRejectedCompletionRetainsExactFenceForRetry(void) {
  FAKE_BACKEND fake;
  APPLE_AGX_BACKEND_RUNTIME runtime;
  APPLE_AGX_BACKEND_IO io;
  APPLE_AGX_BACKEND_SUBMISSION submission;
  APPLE_AGX_BACKEND_OBSERVATION observation;
  APPLE_AGX_SUBMISSION_ENTRY queueStorage[1];
  APPLE_AGX_SUBMISSION_QUEUE queue;

  memset(&fake, 0, sizeof(fake));
  io = BackendIo(&fake);
  submission = Submission(&fake);
  assert(AppleAgxSubmissionQueueInitialize(&queue, queueStorage, 1u));
  assert(AppleAgxSubmissionQueueAccept(
      &queue, &submission.Submission,
      (APPLE_AGX_U64)(unsigned long long)fake.PrivateData,
      sizeof(fake.PrivateData), 0u, 24u, 32u, 48u));
  fake.WindowsQueue = &queue;
  fake.RejectedBridgeRetireAttempts = 1u;
  AppleAgxBackendRuntimeInitialize(&runtime, 3ULL);
  assert(AppleAgxBackendRuntimeStart(&runtime, &io) ==
         AppleAgxBackendRuntimeResultOk);
  assert(AppleAgxBackendRuntimeSubmit(&runtime, &submission) ==
         AppleAgxBackendRuntimeResultOk);

  memset(&observation, 0, sizeof(observation));
  observation.Status = AppleAgxBackendObservationComplete;
  observation.Queue = AppleAgxBackendQueueTa;
  observation.Event = 5u;
  observation.Stamp = 0x7a000100u;
  observation.DonePointer = 2u;
  assert(AppleAgxBackendRuntimeObserve(&runtime, &observation) ==
         AppleAgxBackendRuntimeResultOk);
  observation.Queue = AppleAgxBackendQueue3d;
  observation.Event = 6u;
  observation.Stamp = 0x3d000100u;
  assert(AppleAgxBackendRuntimeObserve(&runtime, &observation) ==
         AppleAgxBackendRuntimeResultBusy);

  assert(runtime.Phase == AppleAgxBackendRuntimeSubmitted);
  assert(runtime.TerminalPending);
  assert(runtime.PendingSubmission.Submission.Fence == 77u);
  assert(fake.CompletionAttempts == 1u);
  assert(fake.CompletionCount == 0u);
  assert(fake.WindowsNotificationCount == 0u);
  assert(fake.WindowsNotifiedFence == 0u);
  assert(queue.Count == 1u && queue.CompletedFence == 0u);
  assert(AppleAgxBackendRuntimeSubmit(&runtime, &submission) ==
         AppleAgxBackendRuntimeResultBusy);

  assert(AppleAgxBackendRuntimeAcknowledgeCompletion(&runtime) ==
         AppleAgxBackendRuntimeResultOk);
  assert(runtime.Phase == AppleAgxBackendRuntimeReady);
  assert(!runtime.TerminalPending);
  assert(runtime.PendingSubmission.Submission.Fence == 0u);
  assert(fake.CompletionAttempts == 2u);
  assert(fake.CompletionCount == 1u);
  assert(fake.CompletionFence == 77u);
  assert(fake.WindowsNotificationCount == 1u);
  assert(fake.WindowsNotifiedFence == 0u);
  assert(queue.Count == 0u && queue.CompletedFence == 77u);
  assert(AppleAgxBackendRuntimeAcknowledgeCompletion(&runtime) ==
         AppleAgxBackendRuntimeResultInvalidState);
  assert(fake.CompletionAttempts == 2u);
}

static void TestFailedQuiescePinsAllDmaOwnershipForRetry(void) {
  FAKE_BACKEND fake;
  APPLE_AGX_BACKEND_RUNTIME runtime;
  APPLE_AGX_BACKEND_IO io;
  APPLE_AGX_BACKEND_SUBMISSION submission;
  unsigned int operationCount;

  memset(&fake, 0, sizeof(fake));
  io = BackendIo(&fake);
  submission = Submission(&fake);
  AppleAgxBackendRuntimeInitialize(&runtime, 3ULL);
  assert(AppleAgxBackendRuntimeStart(&runtime, &io) ==
         AppleAgxBackendRuntimeResultOk);
  assert(AppleAgxBackendRuntimeSubmit(&runtime, &submission) ==
         AppleAgxBackendRuntimeResultOk);
  operationCount = fake.OperationCount;
  fake.FailStop = 1u;
  assert(AppleAgxBackendRuntimeStop(&runtime) ==
         AppleAgxBackendRuntimeResultCleanupFailed);
  assert(runtime.Phase == AppleAgxBackendRuntimeFailed);
  assert(runtime.PendingSubmission.Submission.Fence == 77u);
  assert(runtime.ArenaMapped && runtime.ContextPublished &&
         runtime.QueuesCreated);
  assert(fake.CompletionCount == 0u);
  assert(fake.OperationCount == operationCount + 1u);
  assert(fake.Operations[operationCount] == OP_STOP_QUEUES);

  fake.FailStop = 0u;
  assert(AppleAgxBackendRuntimeStop(&runtime) ==
         AppleAgxBackendRuntimeResultOk);
  assert(runtime.Phase == AppleAgxBackendRuntimeStopped);
  assert(fake.CompletionCount == 1u);
  assert(fake.CompletionFence == 77u);
  assert(fake.CompletionStatus == AppleAgxBackendCompletionCancelled);
}

static void TestRejectedStopCompletionPinsExactFenceForRetry(void) {
  FAKE_BACKEND fake;
  APPLE_AGX_BACKEND_RUNTIME runtime;
  APPLE_AGX_BACKEND_IO io;
  APPLE_AGX_BACKEND_SUBMISSION submission;

  memset(&fake, 0, sizeof(fake));
  io = BackendIo(&fake);
  submission = Submission(&fake);
  AppleAgxBackendRuntimeInitialize(&runtime, 3ULL);
  assert(AppleAgxBackendRuntimeStart(&runtime, &io) ==
         AppleAgxBackendRuntimeResultOk);
  assert(AppleAgxBackendRuntimeSubmit(&runtime, &submission) ==
         AppleAgxBackendRuntimeResultOk);
  fake.RejectedBridgeRetireAttempts = 1u;

  assert(AppleAgxBackendRuntimeStop(&runtime) ==
         AppleAgxBackendRuntimeResultCleanupFailed);
  assert(runtime.Phase == AppleAgxBackendRuntimeFailed);
  assert(runtime.TerminalPending);
  assert(runtime.TerminalStatus == AppleAgxBackendCompletionCancelled);
  assert(runtime.PendingSubmission.Submission.Fence == 77u);
  assert(runtime.ArenaMapped && runtime.ContextPublished &&
         runtime.QueuesCreated);
  assert(fake.CompletionAttempts == 1u);
  assert(fake.CompletionCount == 0u);

  assert(AppleAgxBackendRuntimeStop(&runtime) ==
         AppleAgxBackendRuntimeResultOk);
  assert(runtime.Phase == AppleAgxBackendRuntimeStopped);
  assert(!runtime.TerminalPending);
  assert(runtime.PendingSubmission.Submission.Fence == 0u);
  assert(!runtime.QueuesCreated && !runtime.QueuesQuiesced);
  assert(fake.CompletionAttempts == 2u);
  assert(fake.CompletionCount == 1u);
  assert(fake.CompletionStatus == AppleAgxBackendCompletionCancelled);
  assert(CountOperation(&fake, OP_STOP_QUEUES) == 1u);
}

static void TestRejectedRun3dRetainsNoQueueOwnership(void) {
  FAKE_BACKEND fake;
  APPLE_AGX_BACKEND_RUNTIME runtime;
  APPLE_AGX_BACKEND_IO io;
  APPLE_AGX_BACKEND_SUBMISSION submission;

  memset(&fake, 0, sizeof(fake));
  io = BackendIo(&fake);
  submission = Submission(&fake);
  AppleAgxBackendRuntimeInitialize(&runtime, 3ULL);
  assert(AppleAgxBackendRuntimeStart(&runtime, &io) ==
         AppleAgxBackendRuntimeResultOk);
  fake.FailRun3d = 1u;
  assert(AppleAgxBackendRuntimeSubmit(&runtime, &submission) ==
         AppleAgxBackendRuntimeResultQueueFailed);
  assert(runtime.Phase == AppleAgxBackendRuntimeReady);
  assert(runtime.PendingSubmission.Submission.Fence == 0u);
  assert(!runtime.QueuesQuiesced);
  assert(CountOperation(&fake, OP_RUN_3D) == 1u);
  assert(CountOperation(&fake, OP_STOP_QUEUES) == 0u);
  assert(AppleAgxBackendRuntimeStop(&runtime) ==
         AppleAgxBackendRuntimeResultOk);
}

static void TestFaultWithoutQuiesceDoesNotCompleteOrDropFence(void) {
  FAKE_BACKEND fake;
  APPLE_AGX_BACKEND_RUNTIME runtime;
  APPLE_AGX_BACKEND_IO io;
  APPLE_AGX_BACKEND_SUBMISSION submission;
  APPLE_AGX_BACKEND_OBSERVATION observation;

  memset(&fake, 0, sizeof(fake));
  io = BackendIo(&fake);
  submission = Submission(&fake);
  AppleAgxBackendRuntimeInitialize(&runtime, 3ULL);
  assert(AppleAgxBackendRuntimeStart(&runtime, &io) ==
         AppleAgxBackendRuntimeResultOk);
  assert(AppleAgxBackendRuntimeSubmit(&runtime, &submission) ==
         AppleAgxBackendRuntimeResultOk);
  memset(&observation, 0, sizeof(observation));
  observation.Status = AppleAgxBackendObservationFault;
  fake.FailStop = 1u;
  assert(AppleAgxBackendRuntimeObserve(&runtime, &observation) ==
         AppleAgxBackendRuntimeResultCleanupFailed);
  assert(runtime.Phase == AppleAgxBackendRuntimeFailed);
  assert(runtime.PendingSubmission.Submission.Fence == 77u);
  assert(fake.CompletionCount == 0u);

  fake.FailStop = 0u;
  assert(AppleAgxBackendRuntimeStop(&runtime) ==
         AppleAgxBackendRuntimeResultOk);
}

static void TestCanonicalFifoCompletesTwoSubmissionsInOrder(void) {
  FAKE_BACKEND fake;
  APPLE_AGX_BACKEND_RUNTIME runtime;
  APPLE_AGX_BACKEND_IO io;
  APPLE_AGX_SUBMISSION_ENTRY storage[2];
  APPLE_AGX_SUBMISSION_QUEUE queue;
  APPLE_AGX_SUBMISSION first;
  APPLE_AGX_SUBMISSION second;
  APPLE_AGX_SUBMISSION_ENTRY head;
  APPLE_AGX_BACKEND_SUBMISSION backendSubmission;
  APPLE_AGX_BACKEND_OBSERVATION observation;

  memset(&fake, 0, sizeof(fake));
  memset(&first, 0, sizeof(first));
  memset(&second, 0, sizeof(second));
  assert(AppleAgxSubmissionQueueInitialize(&queue, storage, 2u));
  fake.WindowsQueue = &queue;
  first.Kind = AppleAgxSubmissionGdi;
  first.Fence = 77u;
  first.DmaBytes = 16u;
  second = first;
  second.Fence = 78u;
  assert(AppleAgxSubmissionQueueAccept(
      &queue, &first, (APPLE_AGX_U64)(unsigned long long)fake.PrivateData,
      sizeof(fake.PrivateData), 0u, 24u, 32u, 48u));
  assert(AppleAgxSubmissionQueueAccept(
      &queue, &second, (APPLE_AGX_U64)(unsigned long long)fake.PrivateData,
      sizeof(fake.PrivateData), 0u, 24u, 32u, 48u));

  io = BackendIo(&fake);
  AppleAgxBackendRuntimeInitialize(&runtime, 3ULL);
  assert(AppleAgxBackendRuntimeStart(&runtime, &io) ==
         AppleAgxBackendRuntimeResultOk);

  assert(AppleAgxSubmissionQueuePeek(&queue, &head));
  assert(head.Submission.Fence == 77u);
  assert(AppleAgxBackendSubmissionFromEntry(3ULL, &head, fake.PrivateData,
                                            &backendSubmission));
  assert(AppleAgxBackendRuntimeSubmit(&runtime, &backendSubmission) ==
         AppleAgxBackendRuntimeResultOk);
  memset(&observation, 0, sizeof(observation));
  observation.Status = AppleAgxBackendObservationComplete;
  observation.Queue = AppleAgxBackendQueueTa;
  observation.Event = 5u;
  observation.Stamp = 0x7a000100u;
  observation.DonePointer = 2u;
  assert(AppleAgxBackendRuntimeObserve(&runtime, &observation) ==
         AppleAgxBackendRuntimeResultOk);
  observation.Queue = AppleAgxBackendQueue3d;
  observation.Event = 6u;
  observation.Stamp = 0x3d000100u;
  assert(AppleAgxBackendRuntimeObserve(&runtime, &observation) ==
         AppleAgxBackendRuntimeResultOk);
  assert(queue.Count == 1u && queue.CompletedFence == 77u);
  assert(AppleAgxSubmissionQueuePeek(&queue, &head));
  assert(head.Submission.Fence == 78u);

  assert(AppleAgxBackendSubmissionFromEntry(3ULL, &head, fake.PrivateData,
                                            &backendSubmission));
  assert(AppleAgxBackendRuntimeSubmit(&runtime, &backendSubmission) ==
         AppleAgxBackendRuntimeResultOk);
  assert(queue.Count == 1u && queue.CompletedFence == 77u);
  assert(fake.CompletionCount == 1u && fake.CompletionFence == 77u);
  observation.Queue = AppleAgxBackendQueue3d;
  observation.Event = 6u;
  observation.Stamp = 0x3d000100u;
  observation.DonePointer = 2u;
  assert(AppleAgxBackendRuntimeObserve(&runtime, &observation) ==
         AppleAgxBackendRuntimeResultOk);
  observation.Queue = AppleAgxBackendQueueTa;
  observation.Event = 5u;
  observation.Stamp = 0x7a000100u;
  assert(AppleAgxBackendRuntimeObserve(&runtime, &observation) ==
         AppleAgxBackendRuntimeResultOk);
  assert(queue.Count == 0u && queue.CompletedFence == 78u);
  assert(fake.CompletionCount == 2u && fake.CompletionFence == 78u);
  assert(AppleAgxBackendRuntimeStop(&runtime) ==
         AppleAgxBackendRuntimeResultOk);
}

int main(void) {
  TestCanonicalEntryBridge();
  TestJoinedCompletion();
  TestResetCompletesExactFenceOnce();
  TestRejectedCompletionRetainsExactFenceForRetry();
  TestFailedQuiescePinsAllDmaOwnershipForRetry();
  TestRejectedStopCompletionPinsExactFenceForRetry();
  TestRejectedRun3dRetainsNoQueueOwnership();
  TestFaultWithoutQuiesceDoesNotCompleteOrDropFence();
  TestCanonicalFifoCompletesTwoSubmissionsInOrder();
  puts("apple_agx_backend_runtime_test: ok");
  return 0;
}
