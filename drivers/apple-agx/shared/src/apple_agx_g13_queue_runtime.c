#include "apple_agx_g13_queue_runtime.h"

#define APPLE_AGX_G13_QUEUE_RUNTIME_NULL ((void *)0)

static void AppleAgxG13QueueRuntimeZero(void *Buffer,
                                        APPLE_AGX_BACKEND_U32 Bytes) {
  unsigned char *buffer = (unsigned char *)Buffer;
  APPLE_AGX_BACKEND_U32 index;
  for (index = 0u; index < Bytes; ++index)
    buffer[index] = 0u;
}

static void AppleAgxG13QueueRuntimeCopy(unsigned char *Destination,
                                        const unsigned char *Source,
                                        APPLE_AGX_BACKEND_U32 Bytes) {
  APPLE_AGX_BACKEND_U32 index;
  for (index = 0u; index < Bytes; ++index)
    Destination[index] = Source[index];
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueBindingValid(
    const APPLE_AGX_G13_QUEUE_BINDING *Queue,
    APPLE_AGX_BACKEND_U32 ExpectedType) {
  return Queue != APPLE_AGX_G13_QUEUE_RUNTIME_NULL &&
         Queue->QueueType == ExpectedType &&
         Queue->QueueInfoGpuAddress != 0ULL &&
         (Queue->QueueInfoGpuAddress & 7ULL) == 0ULL &&
         Queue->RingCpuAddress != APPLE_AGX_G13_QUEUE_RUNTIME_NULL &&
         Queue->RingCapacity > APPLE_AGX_BACKEND_QUEUE_WORK_COUNT &&
         Queue->RingCapacity <= APPLE_AGX_G13_RING_CAPACITY &&
         Queue->CpuWritePointer != APPLE_AGX_G13_QUEUE_RUNTIME_NULL &&
         Queue->GpuDonePointer != APPLE_AGX_G13_QUEUE_RUNTIME_NULL &&
         Queue->Stamp != APPLE_AGX_G13_QUEUE_RUNTIME_NULL &&
         Queue->EventNumber < APPLE_AGX_G13_EVENT_COUNT;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueIoValid(
    const APPLE_AGX_G13_QUEUE_RUNTIME_IO *Io) {
  return Io != APPLE_AGX_G13_QUEUE_RUNTIME_NULL &&
         Io->FlushForDevice != APPLE_AGX_G13_QUEUE_RUNTIME_NULL &&
         Io->MemoryBarrier != APPLE_AGX_G13_QUEUE_RUNTIME_NULL &&
         Io->PublishU32 != APPLE_AGX_G13_QUEUE_RUNTIME_NULL &&
         Io->ReadU32 != APPLE_AGX_G13_QUEUE_RUNTIME_NULL &&
         Io->SendRunMessage != APPLE_AGX_G13_QUEUE_RUNTIME_NULL &&
         Io->Quiesce != APPLE_AGX_G13_QUEUE_RUNTIME_NULL;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13WorkValid(
    const APPLE_AGX_G13_WORK_PUBLICATION *Work) {
  APPLE_AGX_BACKEND_U32 index;
  APPLE_AGX_BACKEND_BOOL prepared;
  APPLE_AGX_BACKEND_BOOL legacy;
  if (Work == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
      Work->GpuAddressCount == 0u ||
      Work->GpuAddressCount > APPLE_AGX_BACKEND_QUEUE_WORK_COUNT ||
      Work->ExpectedStamp == 0u)
    return APPLE_AGX_BACKEND_FALSE;
  prepared = Work->PreparedRangeCount == Work->GpuAddressCount;
  legacy = Work->PreparedRangeCount == 0u &&
           Work->Source != APPLE_AGX_G13_QUEUE_RUNTIME_NULL &&
           Work->Destination != APPLE_AGX_G13_QUEUE_RUNTIME_NULL &&
           Work->Bytes != 0u;
  if (!prepared && !legacy)
    return APPLE_AGX_BACKEND_FALSE;
  for (index = 0u; index < Work->GpuAddressCount; ++index) {
    if (Work->GpuAddresses[index] == 0ULL ||
        (Work->GpuAddresses[index] & 0x1fULL) != 0ULL ||
        (prepared &&
         (Work->PreparedRanges[index].Address ==
              APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
          Work->PreparedRanges[index].Bytes == 0u)))
      return APPLE_AGX_BACKEND_FALSE;
  }
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13PublishPreparedWork(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    const APPLE_AGX_G13_WORK_PUBLICATION *Work) {
  APPLE_AGX_BACKEND_U32 index;
  if (Work->PreparedRangeCount != 0u) {
    for (index = 0u; index < Work->PreparedRangeCount; ++index) {
      if (!Runtime->Io.FlushForDevice(
              Runtime->Io.Context, Work->PreparedRanges[index].Address,
              Work->PreparedRanges[index].Bytes))
        return APPLE_AGX_BACKEND_FALSE;
    }
    return APPLE_AGX_BACKEND_TRUE;
  }
  AppleAgxG13QueueRuntimeCopy(Work->Destination, Work->Source, Work->Bytes);
  return Runtime->Io.FlushForDevice(Runtime->Io.Context, Work->Destination,
                                    Work->Bytes);
}

static void AppleAgxG13ClearPending(APPLE_AGX_G13_QUEUE_RUNTIME *Runtime) {
  Runtime->PendingFence = 0u;
  Runtime->DeadlineTicks = 0ULL;
  Runtime->CompletionAvailable = APPLE_AGX_BACKEND_FALSE;
  AppleAgxG13QueueRuntimeZero(
      &Runtime->TaPending,
      (APPLE_AGX_BACKEND_U32)sizeof(Runtime->TaPending));
  AppleAgxG13QueueRuntimeZero(
      &Runtime->D3Pending,
      (APPLE_AGX_BACKEND_U32)sizeof(Runtime->D3Pending));
}

APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeInitialize(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    const APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *Config,
    const APPLE_AGX_G13_QUEUE_RUNTIME_IO *Io) {
  APPLE_AGX_BACKEND_U32 ta_write;
  APPLE_AGX_BACKEND_U32 ta_done;
  APPLE_AGX_BACKEND_U32 d3_write;
  APPLE_AGX_BACKEND_U32 d3_done;

  if (Runtime == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
      Config == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
      !AppleAgxG13QueueIoValid(Io) || Config->TimeoutTicks == 0ULL ||
      !AppleAgxG13QueueBindingValid(
          &Config->Ta, (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueTa) ||
      !AppleAgxG13QueueBindingValid(
          &Config->D3, (APPLE_AGX_BACKEND_U32)AppleAgxG13Queue3d) ||
      Config->Ta.EventNumber == Config->D3.EventNumber)
    return AppleAgxG13QueueRuntimeResultInvalidArgument;

  if (!Io->ReadU32(Io->Context, Config->Ta.CpuWritePointer, &ta_write) ||
      !Io->ReadU32(Io->Context, Config->Ta.GpuDonePointer, &ta_done) ||
      !Io->ReadU32(Io->Context, Config->D3.CpuWritePointer, &d3_write) ||
      !Io->ReadU32(Io->Context, Config->D3.GpuDonePointer, &d3_done))
    return AppleAgxG13QueueRuntimeResultTransportFailed;
  if (ta_write >= Config->Ta.RingCapacity ||
      ta_done >= Config->Ta.RingCapacity ||
      d3_write >= Config->D3.RingCapacity ||
      d3_done >= Config->D3.RingCapacity)
    return AppleAgxG13QueueRuntimeResultInvalidArgument;

  AppleAgxG13QueueRuntimeZero(Runtime,
                              (APPLE_AGX_BACKEND_U32)sizeof(*Runtime));
  Runtime->Config = *Config;
  Runtime->Io = *Io;
  Runtime->TaFirstRun = APPLE_AGX_BACKEND_TRUE;
  Runtime->D3FirstRun = APPLE_AGX_BACKEND_TRUE;
  Runtime->Phase = AppleAgxG13QueueRuntimeReady;
  return AppleAgxG13QueueRuntimeResultOk;
}

typedef struct _APPLE_AGX_G13_QUEUE_BATCH_PUBLICATION {
  APPLE_AGX_BACKEND_U32 RingIndices[APPLE_AGX_BACKEND_QUEUE_WORK_COUNT];
  APPLE_AGX_BACKEND_U32 NextWritePointer;
  APPLE_AGX_BACKEND_U32 ExpectedDonePointer;
} APPLE_AGX_G13_QUEUE_BATCH_PUBLICATION;

static APPLE_AGX_BACKEND_BOOL AppleAgxG13PrepareQueue(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    const APPLE_AGX_G13_QUEUE_BINDING *Binding,
    const APPLE_AGX_G13_WORK_PUBLICATION *Work,
    APPLE_AGX_BACKEND_BOOL FirstRun, APPLE_AGX_BACKEND_U64 Timestamp,
    APPLE_AGX_G13_QUEUE_BATCH_PUBLICATION *Publication,
    APPLE_AGX_G13_RUN_COMMAND *Run) {
  APPLE_AGX_BACKEND_U32 current;
  APPLE_AGX_BACKEND_U32 done;
  APPLE_AGX_BACKEND_U32 index;
  APPLE_AGX_BACKEND_U32 next;

  if (!Runtime->Io.ReadU32(Runtime->Io.Context, Binding->CpuWritePointer,
                           &current) ||
      !Runtime->Io.ReadU32(Runtime->Io.Context, Binding->GpuDonePointer,
                           &done))
    return APPLE_AGX_BACKEND_FALSE;
  if (current >= Binding->RingCapacity || done >= Binding->RingCapacity)
    return APPLE_AGX_BACKEND_FALSE;
  next = current;
  for (index = 0u; index < Work->GpuAddressCount; ++index) {
    Publication->RingIndices[index] = next;
    ++next;
    if (next >= Binding->RingCapacity)
      next = 0u;
    if (next == done)
      return APPLE_AGX_BACKEND_FALSE;
  }
  Publication->NextWritePointer = next;
  Publication->ExpectedDonePointer = next;

  Run->QueueType = Binding->QueueType;
  Run->CommandQueueAddress = Binding->QueueInfoGpuAddress;
  Run->Head = Publication->NextWritePointer;
  Run->EventNumber = Binding->EventNumber;
  Run->NewQueue = FirstRun ? 1u : 0u;
  Run->Timestamp = Timestamp;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13SubmitFailure(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    APPLE_AGX_G13_QUEUE_RUNTIME_RESULT Result,
    APPLE_AGX_BACKEND_BOOL VisibleToFirmware) {
  if (VisibleToFirmware) {
    if (!Runtime->Io.Quiesce(Runtime->Io.Context, Runtime->PendingFence)) {
      Runtime->Phase = AppleAgxG13QueueRuntimeFaulted;
      return AppleAgxG13QueueRuntimeResultResetFailed;
    }
    Runtime->Phase = AppleAgxG13QueueRuntimeFaulted;
  } else {
    Runtime->Phase = AppleAgxG13QueueRuntimeReady;
  }
  AppleAgxG13ClearPending(Runtime);
  return Result;
}

APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeSubmit(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    const APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION *Submission) {
  APPLE_AGX_G13_QUEUE_BATCH_PUBLICATION ta_publication;
  APPLE_AGX_G13_QUEUE_BATCH_PUBLICATION d3_publication;
  APPLE_AGX_G13_RUN_COMMAND ta_run;
  APPLE_AGX_G13_RUN_COMMAND d3_run;
  APPLE_AGX_G13_JOINED_RUN joined;
  APPLE_AGX_BACKEND_BOOL visible = APPLE_AGX_BACKEND_FALSE;
  APPLE_AGX_BACKEND_U32 index;

  if (Runtime == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
      Submission == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
      Submission->Fence == 0u || Submission->NowTicks == 0ULL ||
      !AppleAgxG13WorkValid(&Submission->Ta) ||
      !AppleAgxG13WorkValid(&Submission->D3) ||
      Submission->D3.GpuAddressCount != APPLE_AGX_BACKEND_QUEUE_WORK_COUNT ||
      Submission->Ta.GpuAddressCount !=
          (Runtime->BufferManagerInitialized ? 1u
                                             : APPLE_AGX_BACKEND_QUEUE_WORK_COUNT))
    return AppleAgxG13QueueRuntimeResultInvalidArgument;
  if (Runtime->Phase == AppleAgxG13QueueRuntimeSubmitted ||
      Runtime->Phase == AppleAgxG13QueueRuntimeCompletionPending)
    return AppleAgxG13QueueRuntimeResultBusy;
  if (Runtime->Phase != AppleAgxG13QueueRuntimeReady)
    return AppleAgxG13QueueRuntimeResultInvalidState;

  if (!AppleAgxG13PrepareQueue(Runtime, &Runtime->Config.Ta,
                               &Submission->Ta, Runtime->TaFirstRun,
                               Submission->Timestamp, &ta_publication,
                               &ta_run) ||
      !AppleAgxG13PrepareQueue(Runtime, &Runtime->Config.D3,
                               &Submission->D3, Runtime->D3FirstRun,
                               Submission->Timestamp, &d3_publication,
                               &d3_run) ||
      !AppleAgxG13BuildJoinedRun(&d3_run, &ta_run, &joined))
    return AppleAgxG13QueueRuntimeResultBusy;

  Runtime->PendingFence = Submission->Fence;
  Runtime->DeadlineTicks = Submission->NowTicks + Runtime->Config.TimeoutTicks;
  if (!AppleAgxG13PublishPreparedWork(Runtime, &Submission->D3) ||
      !AppleAgxG13PublishPreparedWork(Runtime, &Submission->Ta))
    return AppleAgxG13SubmitFailure(
        Runtime, AppleAgxG13QueueRuntimeResultMemoryFailed, visible);

  for (index = 0u; index < Submission->D3.GpuAddressCount; ++index) {
    Runtime->Config.D3.RingCpuAddress[d3_publication.RingIndices[index]] =
        Submission->D3.GpuAddresses[index];
    if (!Runtime->Io.FlushForDevice(
            Runtime->Io.Context,
            &Runtime->Config.D3.RingCpuAddress[
                d3_publication.RingIndices[index]],
            APPLE_AGX_G13_RING_SLOT_SIZE))
      return AppleAgxG13SubmitFailure(
          Runtime, AppleAgxG13QueueRuntimeResultMemoryFailed, visible);
  }
  for (index = 0u; index < Submission->Ta.GpuAddressCount; ++index) {
    Runtime->Config.Ta.RingCpuAddress[ta_publication.RingIndices[index]] =
        Submission->Ta.GpuAddresses[index];
    if (!Runtime->Io.FlushForDevice(
            Runtime->Io.Context,
            &Runtime->Config.Ta.RingCpuAddress[
                ta_publication.RingIndices[index]],
            APPLE_AGX_G13_RING_SLOT_SIZE))
      return AppleAgxG13SubmitFailure(
          Runtime, AppleAgxG13QueueRuntimeResultMemoryFailed, visible);
  }

  Runtime->Io.MemoryBarrier(Runtime->Io.Context);
  if (!Runtime->Io.PublishU32(Runtime->Io.Context,
                              Runtime->Config.D3.CpuWritePointer,
                              d3_publication.NextWritePointer))
    return AppleAgxG13SubmitFailure(
        Runtime, AppleAgxG13QueueRuntimeResultMemoryFailed, visible);
  visible = APPLE_AGX_BACKEND_TRUE;
  if (!Runtime->Io.PublishU32(Runtime->Io.Context,
                              Runtime->Config.Ta.CpuWritePointer,
                              ta_publication.NextWritePointer))
    return AppleAgxG13SubmitFailure(
        Runtime, AppleAgxG13QueueRuntimeResultMemoryFailed, visible);
  Runtime->Io.MemoryBarrier(Runtime->Io.Context);

  if (!Runtime->Io.SendRunMessage(
          Runtime->Io.Context, (APPLE_AGX_BACKEND_U32)AppleAgxG13Queue3d,
          joined.Messages[0]) ||
      !Runtime->Io.SendRunMessage(
          Runtime->Io.Context, (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueTa,
          joined.Messages[1]))
    return AppleAgxG13SubmitFailure(
        Runtime, AppleAgxG13QueueRuntimeResultTransportFailed, visible);

  Runtime->D3Pending.EventNumber = Runtime->Config.D3.EventNumber;
  Runtime->D3Pending.ExpectedStamp = Submission->D3.ExpectedStamp;
  Runtime->D3Pending.ExpectedDonePointer =
      d3_publication.ExpectedDonePointer;
  Runtime->TaPending.EventNumber = Runtime->Config.Ta.EventNumber;
  Runtime->TaPending.ExpectedStamp = Submission->Ta.ExpectedStamp;
  Runtime->TaPending.ExpectedDonePointer = ta_publication.ExpectedDonePointer;
  Runtime->D3FirstRun = APPLE_AGX_BACKEND_FALSE;
  Runtime->TaFirstRun = APPLE_AGX_BACKEND_FALSE;
  Runtime->BufferManagerInitialized = APPLE_AGX_BACKEND_TRUE;
  Runtime->Phase = AppleAgxG13QueueRuntimeSubmitted;
  return AppleAgxG13QueueRuntimeResultOk;
}

static void AppleAgxG13SetCompletion(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    APPLE_AGX_G13_QUEUE_COMPLETION_STATUS Status) {
  Runtime->Completion.Fence = Runtime->PendingFence;
  Runtime->Completion.Status = Status;
  Runtime->CompletionAvailable = APPLE_AGX_BACKEND_TRUE;
  Runtime->Phase = AppleAgxG13QueueRuntimeCompletionPending;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ObserveQueue(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    const APPLE_AGX_G13_EVENT *Event,
    const APPLE_AGX_G13_QUEUE_BINDING *Binding,
    APPLE_AGX_G13_QUEUE_PENDING *Pending) {
  APPLE_AGX_BACKEND_U32 stamp;
  APPLE_AGX_BACKEND_U32 done;

  if (Pending->Complete)
    return APPLE_AGX_BACKEND_TRUE;
  if (AppleAgxG13EventHasNumber(Event, Pending->EventNumber))
    Pending->EventSeen = APPLE_AGX_BACKEND_TRUE;
  if (!Pending->EventSeen)
    return APPLE_AGX_BACKEND_TRUE;
  if (!Runtime->Io.ReadU32(Runtime->Io.Context, Binding->Stamp, &stamp) ||
      !Runtime->Io.ReadU32(Runtime->Io.Context, Binding->GpuDonePointer,
                           &done))
    return APPLE_AGX_BACKEND_FALSE;
  if (AppleAgxG13CompletionSatisfied(
          Event, Pending->EventNumber, stamp, Pending->ExpectedStamp, done,
          Pending->ExpectedDonePointer))
    Pending->Complete = APPLE_AGX_BACKEND_TRUE;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13TerminalFailure(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    APPLE_AGX_G13_QUEUE_COMPLETION_STATUS Status,
    APPLE_AGX_G13_QUEUE_RUNTIME_RESULT Result) {
  if (!Runtime->Io.Quiesce(Runtime->Io.Context, Runtime->PendingFence)) {
    Runtime->Phase = AppleAgxG13QueueRuntimeFaulted;
    Runtime->CompletionAvailable = APPLE_AGX_BACKEND_FALSE;
    return AppleAgxG13QueueRuntimeResultResetFailed;
  }
  AppleAgxG13SetCompletion(Runtime, Status);
  return Result;
}

APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeHandleEvent(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime, const unsigned char *Message,
    APPLE_AGX_BACKEND_U32 MessageBytes) {
  APPLE_AGX_G13_EVENT event;

  if (Runtime == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
      Message == APPLE_AGX_G13_QUEUE_RUNTIME_NULL)
    return AppleAgxG13QueueRuntimeResultInvalidArgument;
  if (Runtime->Phase != AppleAgxG13QueueRuntimeSubmitted)
    return AppleAgxG13QueueRuntimeResultInvalidState;
  if (!AppleAgxG13DecodeEvent(Message, MessageBytes, &event))
    return AppleAgxG13QueueRuntimeResultInvalidArgument;
  if (event.TerminalFault)
    return AppleAgxG13TerminalFailure(
        Runtime, AppleAgxG13QueueCompletionFaulted,
        AppleAgxG13QueueRuntimeResultFaulted);
  if (event.Kind != (APPLE_AGX_BACKEND_U32)AppleAgxG13EventFlag)
    return AppleAgxG13QueueRuntimeResultOk;

  if (!AppleAgxG13ObserveQueue(Runtime, &event, &Runtime->Config.D3,
                               &Runtime->D3Pending) ||
      !AppleAgxG13ObserveQueue(Runtime, &event, &Runtime->Config.Ta,
                               &Runtime->TaPending))
    return AppleAgxG13TerminalFailure(
        Runtime, AppleAgxG13QueueCompletionFaulted,
        AppleAgxG13QueueRuntimeResultTransportFailed);
  if (Runtime->D3Pending.Complete && Runtime->TaPending.Complete)
    AppleAgxG13SetCompletion(Runtime, AppleAgxG13QueueCompletionSuccess);
  return AppleAgxG13QueueRuntimeResultOk;
}

APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeCheckTimeout(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    APPLE_AGX_BACKEND_U64 NowTicks) {
  if (Runtime == APPLE_AGX_G13_QUEUE_RUNTIME_NULL || NowTicks == 0ULL)
    return AppleAgxG13QueueRuntimeResultInvalidArgument;
  if (Runtime->Phase != AppleAgxG13QueueRuntimeSubmitted)
    return AppleAgxG13QueueRuntimeResultInvalidState;
  if ((APPLE_AGX_BACKEND_U64)(NowTicks - Runtime->DeadlineTicks) >=
      (1ULL << 63))
    return AppleAgxG13QueueRuntimeResultOk;
  return AppleAgxG13TerminalFailure(
      Runtime, AppleAgxG13QueueCompletionTimedOut,
      AppleAgxG13QueueRuntimeResultTimedOut);
}

APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeCancel(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime, APPLE_AGX_BACKEND_U32 Fence) {
  if (Runtime == APPLE_AGX_G13_QUEUE_RUNTIME_NULL || Fence == 0u)
    return AppleAgxG13QueueRuntimeResultInvalidArgument;
  if (Runtime->Phase != AppleAgxG13QueueRuntimeSubmitted ||
      Runtime->PendingFence != Fence)
    return AppleAgxG13QueueRuntimeResultInvalidState;
  return AppleAgxG13TerminalFailure(
      Runtime, AppleAgxG13QueueCompletionCancelled,
      AppleAgxG13QueueRuntimeResultCancelled);
}

APPLE_AGX_G13_QUEUE_RUNTIME_RESULT
AppleAgxG13QueueRuntimeReset(APPLE_AGX_G13_QUEUE_RUNTIME *Runtime) {
  APPLE_AGX_BACKEND_U32 ta_done;
  APPLE_AGX_BACKEND_U32 d3_done;

  if (Runtime == APPLE_AGX_G13_QUEUE_RUNTIME_NULL)
    return AppleAgxG13QueueRuntimeResultInvalidArgument;
  if (Runtime->Phase == AppleAgxG13QueueRuntimeCompletionPending)
    return AppleAgxG13QueueRuntimeResultInvalidState;
  if (Runtime->Phase == AppleAgxG13QueueRuntimeSubmitted &&
      !Runtime->Io.Quiesce(Runtime->Io.Context, Runtime->PendingFence))
    return AppleAgxG13QueueRuntimeResultResetFailed;
  if (!Runtime->Io.ReadU32(Runtime->Io.Context,
                           Runtime->Config.Ta.GpuDonePointer, &ta_done) ||
      !Runtime->Io.ReadU32(Runtime->Io.Context,
                           Runtime->Config.D3.GpuDonePointer, &d3_done) ||
      ta_done >= Runtime->Config.Ta.RingCapacity ||
      d3_done >= Runtime->Config.D3.RingCapacity ||
      !Runtime->Io.PublishU32(Runtime->Io.Context,
                              Runtime->Config.Ta.CpuWritePointer, ta_done) ||
      !Runtime->Io.PublishU32(Runtime->Io.Context,
                              Runtime->Config.D3.CpuWritePointer, d3_done)) {
    Runtime->Phase = AppleAgxG13QueueRuntimeFaulted;
    return AppleAgxG13QueueRuntimeResultResetFailed;
  }
  Runtime->Io.MemoryBarrier(Runtime->Io.Context);
  AppleAgxG13ClearPending(Runtime);
  Runtime->TaFirstRun = APPLE_AGX_BACKEND_TRUE;
  Runtime->D3FirstRun = APPLE_AGX_BACKEND_TRUE;
  Runtime->BufferManagerInitialized = APPLE_AGX_BACKEND_FALSE;
  Runtime->Phase = AppleAgxG13QueueRuntimeReady;
  return AppleAgxG13QueueRuntimeResultOk;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueRuntimeTakeCompletion(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION *Completion) {
  APPLE_AGX_G13_QUEUE_COMPLETION_STATUS status;

  if (Runtime == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
      Completion == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
      Runtime->Phase != AppleAgxG13QueueRuntimeCompletionPending ||
      !Runtime->CompletionAvailable)
    return APPLE_AGX_BACKEND_FALSE;
  *Completion = Runtime->Completion;
  status = Runtime->Completion.Status;
  AppleAgxG13ClearPending(Runtime);
  Runtime->Phase = status == AppleAgxG13QueueCompletionSuccess
                       ? AppleAgxG13QueueRuntimeReady
                       : AppleAgxG13QueueRuntimeFaulted;
  return APPLE_AGX_BACKEND_TRUE;
}
