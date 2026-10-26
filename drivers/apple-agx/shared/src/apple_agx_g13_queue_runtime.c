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
      Work->GpuAddressCount > APPLE_AGX_G13_QUEUE_PUBLISH_MAX ||
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
  Runtime->DeadlinePassed = APPLE_AGX_BACKEND_FALSE;
  Runtime->CompletionAvailable = APPLE_AGX_BACKEND_FALSE;
  Runtime->RecoveryPending = APPLE_AGX_BACKEND_FALSE;
  Runtime->ChannelRecoveryMask = 0u;
  Runtime->ChannelRecoveryFailed = APPLE_AGX_BACKEND_FALSE;
  AppleAgxG13QueueRuntimeZero(
      &Runtime->TaPending,
      (APPLE_AGX_BACKEND_U32)sizeof(Runtime->TaPending));
  AppleAgxG13QueueRuntimeZero(
      &Runtime->D3Pending,
      (APPLE_AGX_BACKEND_U32)sizeof(Runtime->D3Pending));
  AppleAgxG13QueueRuntimeZero(
      &Runtime->ComputePending,
      (APPLE_AGX_BACKEND_U32)sizeof(Runtime->ComputePending));
  Runtime->ComputeTerminal = APPLE_AGX_BACKEND_FALSE;
  Runtime->ComputeInRender = APPLE_AGX_BACKEND_FALSE;
}

APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeInitialize(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    const APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *Config,
    const APPLE_AGX_G13_QUEUE_RUNTIME_IO *Io) {
  APPLE_AGX_BACKEND_U32 ta_write;
  APPLE_AGX_BACKEND_U32 ta_done;
  APPLE_AGX_BACKEND_U32 d3_write;
  APPLE_AGX_BACKEND_U32 d3_done;
  APPLE_AGX_BACKEND_U32 compute_write=0u;
  APPLE_AGX_BACKEND_U32 compute_done=0u;

  if (Runtime == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
      Config == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
      !AppleAgxG13QueueIoValid(Io) || Config->TimeoutTicks == 0ULL ||
      !AppleAgxG13QueueBindingValid(
          &Config->Ta, (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueTa) ||
      !AppleAgxG13QueueBindingValid(
          &Config->D3, (APPLE_AGX_BACKEND_U32)AppleAgxG13Queue3d) ||
      (Config->HasCompute && !AppleAgxG13QueueBindingValid(
          &Config->Compute,(APPLE_AGX_BACKEND_U32)AppleAgxG13QueueCompute)) ||
      Config->Ta.EventNumber == Config->D3.EventNumber ||
      (Config->HasCompute &&
       (Config->Compute.EventNumber==Config->Ta.EventNumber ||
        Config->Compute.EventNumber==Config->D3.EventNumber)))
    return AppleAgxG13QueueRuntimeResultInvalidArgument;

  if (!Io->ReadU32(Io->Context, Config->Ta.CpuWritePointer, &ta_write) ||
      !Io->ReadU32(Io->Context, Config->Ta.GpuDonePointer, &ta_done) ||
      !Io->ReadU32(Io->Context, Config->D3.CpuWritePointer, &d3_write) ||
      !Io->ReadU32(Io->Context, Config->D3.GpuDonePointer, &d3_done) ||
      (Config->HasCompute &&
       (!Io->ReadU32(Io->Context,Config->Compute.CpuWritePointer,&compute_write) ||
        !Io->ReadU32(Io->Context,Config->Compute.GpuDonePointer,&compute_done))))
    return AppleAgxG13QueueRuntimeResultTransportFailed;
  if (ta_write >= Config->Ta.RingCapacity ||
      ta_done >= Config->Ta.RingCapacity ||
      d3_write >= Config->D3.RingCapacity ||
      d3_done >= Config->D3.RingCapacity ||
      (Config->HasCompute &&
       (compute_write>=Config->Compute.RingCapacity ||
        compute_done>=Config->Compute.RingCapacity)))
    return AppleAgxG13QueueRuntimeResultInvalidArgument;

  AppleAgxG13QueueRuntimeZero(Runtime,
                              (APPLE_AGX_BACKEND_U32)sizeof(*Runtime));
  Runtime->Config = *Config;
  Runtime->Io = *Io;
  Runtime->TaFirstRun = APPLE_AGX_BACKEND_TRUE;
  Runtime->D3FirstRun = APPLE_AGX_BACKEND_TRUE;
  Runtime->ComputeFirstRun = APPLE_AGX_BACKEND_TRUE;
  Runtime->Phase = AppleAgxG13QueueRuntimeReady;
  return AppleAgxG13QueueRuntimeResultOk;
}

typedef struct _APPLE_AGX_G13_QUEUE_BATCH_PUBLICATION {
  APPLE_AGX_BACKEND_U32 RingIndices[APPLE_AGX_G13_QUEUE_PUBLISH_MAX];
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

APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeSubmitCompute(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    const APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION *Submission) {
  APPLE_AGX_G13_QUEUE_BATCH_PUBLICATION publication;
  APPLE_AGX_G13_RUN_COMMAND run;
  unsigned char message[APPLE_AGX_G13_RUN_MESSAGE_SIZE];
  APPLE_AGX_BACKEND_BOOL visible=APPLE_AGX_BACKEND_FALSE;
  APPLE_AGX_BACKEND_U32 index;
  if(Runtime==APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
     Submission==APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
     !Runtime->Config.HasCompute || Runtime->Phase!=AppleAgxG13QueueRuntimeReady ||
     !Submission->Fence || !Submission->NowTicks ||
     !AppleAgxG13WorkValid(&Submission->Compute) ||
     Submission->Compute.GpuAddressCount!=1u || !Submission->ComputeOnly ||
     Submission->TaLeadingBarrier)
    return AppleAgxG13QueueRuntimeResultInvalidArgument;
  if(!AppleAgxG13PrepareQueue(Runtime,&Runtime->Config.Compute,
       &Submission->Compute,Runtime->ComputeFirstRun,Submission->Timestamp,
       &publication,&run) || !AppleAgxG13EncodeRunCommand(&run,message))
    return AppleAgxG13QueueRuntimeResultBusy;
  Runtime->PendingFence=Submission->Fence;
  Runtime->DeadlineTicks=Submission->NowTicks+Runtime->Config.TimeoutTicks;
  Runtime->DeadlinePassed=APPLE_AGX_BACKEND_FALSE;
  if(!AppleAgxG13PublishPreparedWork(Runtime,&Submission->Compute))
    return AppleAgxG13SubmitFailure(Runtime,
        AppleAgxG13QueueRuntimeResultMemoryFailed,visible);
  for(index=0u;index<Submission->Compute.GpuAddressCount;++index) {
    Runtime->Config.Compute.RingCpuAddress[publication.RingIndices[index]]=
        Submission->Compute.GpuAddresses[index];
    if(!Runtime->Io.FlushForDevice(Runtime->Io.Context,
        &Runtime->Config.Compute.RingCpuAddress[publication.RingIndices[index]],
        APPLE_AGX_G13_RING_SLOT_SIZE))
      return AppleAgxG13SubmitFailure(Runtime,
          AppleAgxG13QueueRuntimeResultMemoryFailed,visible);
  }
  Runtime->Io.MemoryBarrier(Runtime->Io.Context);
  if(!Runtime->Io.PublishU32(Runtime->Io.Context,
      Runtime->Config.Compute.CpuWritePointer,publication.NextWritePointer))
    return AppleAgxG13SubmitFailure(Runtime,
        AppleAgxG13QueueRuntimeResultMemoryFailed,visible);
  visible=APPLE_AGX_BACKEND_TRUE;
  Runtime->Io.MemoryBarrier(Runtime->Io.Context);
  if(!Runtime->Io.SendRunMessage(Runtime->Io.Context,
      (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueCompute,message))
    return AppleAgxG13SubmitFailure(Runtime,
        AppleAgxG13QueueRuntimeResultTransportFailed,visible);
  Runtime->ComputePending.EventNumber=Runtime->Config.Compute.EventNumber;
  Runtime->ComputePending.ExpectedStamp=Submission->Compute.ExpectedStamp;
  Runtime->ComputePending.ExpectedDonePointer=publication.ExpectedDonePointer;
  Runtime->ComputeTerminal=APPLE_AGX_BACKEND_TRUE;
  Runtime->ComputeFirstRun=APPLE_AGX_BACKEND_FALSE;
  Runtime->Phase=AppleAgxG13QueueRuntimeComputeSubmitted;
  return AppleAgxG13QueueRuntimeResultOk;
}

/* Writes one queue's prepared ring entries (after its work is published). */
static APPLE_AGX_BACKEND_BOOL AppleAgxG13WriteRing(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime, APPLE_AGX_G13_QUEUE_BINDING *Binding,
    const APPLE_AGX_G13_WORK_PUBLICATION *Work,
    const APPLE_AGX_G13_QUEUE_BATCH_PUBLICATION *Publication) {
  APPLE_AGX_BACKEND_U32 index;
  for (index = 0u; index < Work->GpuAddressCount; ++index) {
    Binding->RingCpuAddress[Publication->RingIndices[index]] =
        Work->GpuAddresses[index];
    if (!Runtime->Io.FlushForDevice(
            Runtime->Io.Context,
            &Binding->RingCpuAddress[Publication->RingIndices[index]],
            APPLE_AGX_G13_RING_SLOT_SIZE))
      return APPLE_AGX_BACKEND_FALSE;
  }
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeSubmitRenderWithCompute(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    const APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION *Submission) {
  APPLE_AGX_G13_QUEUE_BATCH_PUBLICATION ta_publication, d3_publication,
      compute_publication;
  APPLE_AGX_G13_RUN_COMMAND ta_run, d3_run, compute_run;
  APPLE_AGX_G13_JOINED_RUN joined;
  unsigned char compute_message[APPLE_AGX_G13_RUN_MESSAGE_SIZE];
  APPLE_AGX_BACKEND_BOOL visible = APPLE_AGX_BACKEND_FALSE;
  const APPLE_AGX_BACKEND_U32 ta_entries =
      (Runtime != APPLE_AGX_G13_QUEUE_RUNTIME_NULL &&
       Runtime->BufferManagerInitialized ? 1u
                                         : APPLE_AGX_BACKEND_QUEUE_WORK_COUNT) + 1u;

  if (Runtime == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
      Submission == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
      !Runtime->Config.HasCompute || Submission->Fence == 0u ||
      Submission->NowTicks == 0ULL || Submission->ComputeOnly ||
      !Submission->TaLeadingBarrier ||
      !AppleAgxG13WorkValid(&Submission->Ta) ||
      !AppleAgxG13WorkValid(&Submission->D3) ||
      !AppleAgxG13WorkValid(&Submission->Compute) ||
      Submission->Compute.GpuAddressCount != 1u ||
      Submission->D3.GpuAddressCount != APPLE_AGX_BACKEND_QUEUE_WORK_COUNT ||
      Submission->Ta.GpuAddressCount != ta_entries)
    return AppleAgxG13QueueRuntimeResultInvalidArgument;
  if (Runtime->Phase == AppleAgxG13QueueRuntimeSubmitted ||
      Runtime->Phase == AppleAgxG13QueueRuntimeComputeSubmitted ||
      Runtime->Phase == AppleAgxG13QueueRuntimeCompletionPending)
    return AppleAgxG13QueueRuntimeResultBusy;
  if (Runtime->Phase != AppleAgxG13QueueRuntimeReady)
    return AppleAgxG13QueueRuntimeResultInvalidState;
  if (!AppleAgxG13PrepareQueue(Runtime, &Runtime->Config.Compute,
                               &Submission->Compute, Runtime->ComputeFirstRun,
                               Submission->Timestamp, &compute_publication,
                               &compute_run) ||
      !AppleAgxG13EncodeRunCommand(&compute_run, compute_message) ||
      !AppleAgxG13PrepareQueue(Runtime, &Runtime->Config.Ta,
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
  Runtime->DeadlinePassed = APPLE_AGX_BACKEND_FALSE;
  /* Every work item (the compute's sidecar holds the barrier too) is in
   * device memory before any ring entry names it. */
  if (!AppleAgxG13PublishPreparedWork(Runtime, &Submission->Compute) ||
      !AppleAgxG13PublishPreparedWork(Runtime, &Submission->D3) ||
      !AppleAgxG13PublishPreparedWork(Runtime, &Submission->Ta) ||
      !AppleAgxG13WriteRing(Runtime, &Runtime->Config.Compute,
                            &Submission->Compute, &compute_publication) ||
      !AppleAgxG13WriteRing(Runtime, &Runtime->Config.D3, &Submission->D3,
                            &d3_publication) ||
      !AppleAgxG13WriteRing(Runtime, &Runtime->Config.Ta, &Submission->Ta,
                            &ta_publication))
    return AppleAgxG13SubmitFailure(
        Runtime, AppleAgxG13QueueRuntimeResultMemoryFailed, visible);

  Runtime->Io.MemoryBarrier(Runtime->Io.Context);
  if (!Runtime->Io.PublishU32(Runtime->Io.Context,
                              Runtime->Config.Compute.CpuWritePointer,
                              compute_publication.NextWritePointer))
    return AppleAgxG13SubmitFailure(
        Runtime, AppleAgxG13QueueRuntimeResultMemoryFailed, visible);
  visible = APPLE_AGX_BACKEND_TRUE;
  if (!Runtime->Io.PublishU32(Runtime->Io.Context,
                              Runtime->Config.D3.CpuWritePointer,
                              d3_publication.NextWritePointer) ||
      !Runtime->Io.PublishU32(Runtime->Io.Context,
                              Runtime->Config.Ta.CpuWritePointer,
                              ta_publication.NextWritePointer))
    return AppleAgxG13SubmitFailure(
        Runtime, AppleAgxG13QueueRuntimeResultMemoryFailed, visible);
  Runtime->Io.MemoryBarrier(Runtime->Io.Context);
  /* Asahi kicks every queue of the submission; the TA barrier and the 3D
   * barrier (on TA) order the work in firmware. */
  if (!Runtime->Io.SendRunMessage(
          Runtime->Io.Context, (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueCompute,
          compute_message) ||
      !Runtime->Io.SendRunMessage(
          Runtime->Io.Context, (APPLE_AGX_BACKEND_U32)AppleAgxG13Queue3d,
          joined.Messages[0]) ||
      !Runtime->Io.SendRunMessage(
          Runtime->Io.Context, (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueTa,
          joined.Messages[1]))
    return AppleAgxG13SubmitFailure(
        Runtime, AppleAgxG13QueueRuntimeResultTransportFailed, visible);

  Runtime->ComputePending.EventNumber = Runtime->Config.Compute.EventNumber;
  Runtime->ComputePending.ExpectedStamp = Submission->Compute.ExpectedStamp;
  Runtime->ComputePending.ExpectedDonePointer =
      compute_publication.ExpectedDonePointer;
  Runtime->D3Pending.EventNumber = Runtime->Config.D3.EventNumber;
  Runtime->D3Pending.ExpectedStamp = Submission->D3.ExpectedStamp;
  Runtime->D3Pending.ExpectedDonePointer = d3_publication.ExpectedDonePointer;
  Runtime->TaPending.EventNumber = Runtime->Config.Ta.EventNumber;
  Runtime->TaPending.ExpectedStamp = Submission->Ta.ExpectedStamp;
  Runtime->TaPending.ExpectedDonePointer = ta_publication.ExpectedDonePointer;
  Runtime->ComputeTerminal = APPLE_AGX_BACKEND_FALSE;
  Runtime->ComputeInRender = APPLE_AGX_BACKEND_TRUE;
  Runtime->ComputeFirstRun = APPLE_AGX_BACKEND_FALSE;
  Runtime->D3FirstRun = APPLE_AGX_BACKEND_FALSE;
  Runtime->TaFirstRun = APPLE_AGX_BACKEND_FALSE;
  Runtime->BufferManagerInitialized = APPLE_AGX_BACKEND_TRUE;
  Runtime->Phase = AppleAgxG13QueueRuntimeSubmitted;
  return AppleAgxG13QueueRuntimeResultOk;
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
      Submission->TaLeadingBarrier ||
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
  Runtime->DeadlinePassed = APPLE_AGX_BACKEND_FALSE;
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
  APPLE_AGX_BACKEND_U32 write;

  if (Pending->Complete)
    return APPLE_AGX_BACKEND_TRUE;
  if (AppleAgxG13EventHasNumber(Event, Pending->EventNumber))
    Pending->EventSeen = APPLE_AGX_BACKEND_TRUE;
  if (!Pending->EventSeen)
    return APPLE_AGX_BACKEND_TRUE;
  if (!Runtime->Io.ReadU32(Runtime->Io.Context, Binding->Stamp, &stamp) ||
      !Runtime->Io.ReadU32(Runtime->Io.Context, Binding->GpuDonePointer,
                           &done) ||
      !Runtime->Io.ReadU32(Runtime->Io.Context, Binding->CpuWritePointer,
                           &write))
    return APPLE_AGX_BACKEND_FALSE;
  if (AppleAgxG13CompletionSatisfiedWindow(
          Event, Pending->EventNumber, stamp, Pending->ExpectedStamp, done,
          Pending->ExpectedDonePointer, write, Binding->RingCapacity))
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

/* A faulted command need not execute its success-stamp write. Asahi
 * workqueue.rs doneptr() counts work items completed by the GPU (not merely
 * fetched, which is gpu_rptr). After halt/mark-killed/resume, require every
 * published entry of our single outstanding submission to reach that done
 * boundary. A resume request alone is NOT ownership transfer. Never write
 * stamps or GPU pointers, and never turn this path into a success completion. */
static APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13PollRecovery(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime) {
  const APPLE_AGX_G13_QUEUE_BINDING *bindings[3];
  const APPLE_AGX_G13_QUEUE_PENDING *pending[3];
  APPLE_AGX_BACKEND_U32 count = 0u, index;
  APPLE_AGX_BACKEND_BOOL retired = APPLE_AGX_BACKEND_TRUE;
  if (!Runtime->ComputeTerminal) {
    bindings[count] = &Runtime->Config.Ta;
    pending[count++] = &Runtime->TaPending;
    bindings[count] = &Runtime->Config.D3;
    pending[count++] = &Runtime->D3Pending;
  }
  if (Runtime->ComputeTerminal || Runtime->ComputeInRender) {
    bindings[count] = &Runtime->Config.Compute;
    pending[count++] = &Runtime->ComputePending;
  }
  for (index = 0u; index < count; ++index) {
    APPLE_AGX_BACKEND_U32 done, write;
    if (!Runtime->Io.ReadU32(Runtime->Io.Context, bindings[index]->GpuDonePointer,
                             &done) ||
        !Runtime->Io.ReadU32(Runtime->Io.Context, bindings[index]->CpuWritePointer,
                             &write))
      return AppleAgxG13QueueRuntimeResultMemoryFailed;
    if (done >= bindings[index]->RingCapacity ||
        write >= bindings[index]->RingCapacity ||
        write != pending[index]->ExpectedDonePointer)
      return AppleAgxG13QueueRuntimeResultInvalidState;
    if (done != write)
      retired = APPLE_AGX_BACKEND_FALSE;
  }
  if (retired)
    AppleAgxG13SetCompletion(Runtime, AppleAgxG13QueueCompletionRecovered);
  return AppleAgxG13QueueRuntimeResultOk;
}

/* The resume callback starts recovery. Polling, including polling without a
 * Flag event, retains the submission until firmware retirement is observed. */
static APPLE_AGX_BACKEND_BOOL AppleAgxG13RecoverFirmware(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime, const APPLE_AGX_G13_EVENT *Event) {
  if ((Event->Kind != (APPLE_AGX_BACKEND_U32)AppleAgxG13EventTimeout &&
       Event->Kind != (APPLE_AGX_BACKEND_U32)AppleAgxG13EventFault) ||
      Runtime->Io.Recover == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
      !Runtime->Io.Recover(Runtime->Io.Context, Runtime->PendingFence,
                           Event->Kind))
    return APPLE_AGX_BACKEND_FALSE;
  ++Runtime->RecoveredFaults;
  Runtime->LastRecoveredEventKind = Event->Kind;
  if (Event->Kind == (APPLE_AGX_BACKEND_U32)AppleAgxG13EventTimeout) {
    /* Asahi fw/channels.rs: the first word is the u32 timeout counter;
     * the second word in the codec's u64 is an unknown field. */
    Runtime->LastRecoveredTimeoutCounter = (APPLE_AGX_BACKEND_U32)Event->TimeoutCounter;
    Runtime->RecoveredTimeoutValid = APPLE_AGX_BACKEND_TRUE;
  }
  Runtime->RecoveryPending = APPLE_AGX_BACKEND_TRUE;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13StaleTimeout(
    const APPLE_AGX_G13_QUEUE_RUNTIME *Runtime, const APPLE_AGX_G13_EVENT *Event) {
  APPLE_AGX_BACKEND_U32 delta;
  if (Event->Kind != (APPLE_AGX_BACKEND_U32)AppleAgxG13EventTimeout ||
      !Runtime->RecoveredTimeoutValid)
    return APPLE_AGX_BACKEND_FALSE;
  delta = (APPLE_AGX_BACKEND_U32)Event->TimeoutCounter -
          Runtime->LastRecoveredTimeoutCounter;
  return delta == 0u || delta >= 0x80000000u;
}

static APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13RecoverChannel(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime, const APPLE_AGX_G13_EVENT *Event) {
  const APPLE_AGX_G13_QUEUE_BINDING *binding;
  const APPLE_AGX_G13_QUEUE_PENDING *pending;
  APPLE_AGX_BACKEND_BOOL active;
  APPLE_AGX_BACKEND_U32 pipe = Event->ChannelPipeType, bit;
  if (pipe > (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueCompute)
    return AppleAgxG13QueueRuntimeResultResetFailed;
  bit = 1u << pipe;
  if (pipe == (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueCompute) {
    if (!Runtime->Config.HasCompute)
      return AppleAgxG13QueueRuntimeResultResetFailed;
    binding = &Runtime->Config.Compute;
    pending = &Runtime->ComputePending;
    active = Runtime->ComputeTerminal || Runtime->ComputeInRender ||
             Runtime->Phase == AppleAgxG13QueueRuntimeComputeSubmitted;
  } else {
    binding = pipe == (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueTa
                  ? &Runtime->Config.Ta : &Runtime->Config.D3;
    pending = pipe == (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueTa
                  ? &Runtime->TaPending : &Runtime->D3Pending;
    active = !Runtime->ComputeTerminal &&
             Runtime->Phase == AppleAgxG13QueueRuntimeSubmitted;
  }
  /* Resolve the firmware event's slot to its owner, as Asahi does. Our
   * serialized queues have one job value each; never invent a queue or value
   * for an unknown slot, a future value, or a bare Timeout event. */
  if (Event->ChannelEventSlot != binding->EventNumber)
    return AppleAgxG13QueueRuntimeResultResetFailed;
  if (!active || Event->ChannelEventValue != pending->ExpectedStamp) {
    /* Only an already acknowledged tuple proves a delayed duplicate. */
    if ((Runtime->RecoveredChannelMask & bit) != 0u &&
        Runtime->LastRecoveredChannelValue[pipe] == Event->ChannelEventValue)
      return AppleAgxG13QueueRuntimeResultOk;
    return AppleAgxG13QueueRuntimeResultResetFailed;
  }
  if ((Runtime->ChannelRecoveryMask & bit) == 0u) {
    /* Freeze this submission's success path before any externally visible
     * recovery. A failed command/receipt must never fall back to RTKit stop
     * or allow a subsequent Flag to report successful execution. */
    Runtime->RecoveryPending = APPLE_AGX_BACKEND_TRUE;
    if (Runtime->Io.RecoverChannel == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
        !Runtime->Io.RecoverChannel(Runtime->Io.Context, Runtime->PendingFence,
            pipe, binding->QueueInfoGpuAddress, Event->ChannelEventValue)) {
      /* Publication may already be visible. Never publish a second command
       * after an ambiguous transport failure; the provider also fails closed. */
      Runtime->ChannelRecoveryFailed = APPLE_AGX_BACKEND_TRUE;
      return AppleAgxG13QueueRuntimeResultResetFailed;
    }
    Runtime->ChannelRecoveryMask |= bit;
    Runtime->RecoveredChannelMask |= bit;
    Runtime->LastRecoveredChannelValue[pipe] = Event->ChannelEventValue;
    ++Runtime->RecoveredFaults;
    Runtime->LastRecoveredEventKind = Event->Kind;
  }
  return AppleAgxG13PollRecovery(Runtime);
}

APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeHandleEvent(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime, const unsigned char *Message,
    APPLE_AGX_BACKEND_U32 MessageBytes) {
  APPLE_AGX_G13_EVENT event;

  if (Runtime == APPLE_AGX_G13_QUEUE_RUNTIME_NULL ||
      Message == APPLE_AGX_G13_QUEUE_RUNTIME_NULL)
    return AppleAgxG13QueueRuntimeResultInvalidArgument;
  if (Runtime->Phase != AppleAgxG13QueueRuntimeSubmitted &&
      Runtime->Phase != AppleAgxG13QueueRuntimeComputeSubmitted)
    return AppleAgxG13QueueRuntimeResultInvalidState;
  if (Runtime->ChannelRecoveryFailed)
    return AppleAgxG13QueueRuntimeResultResetFailed;
  if (!AppleAgxG13DecodeEvent(Message, MessageBytes, &event))
    return AppleAgxG13QueueRuntimeResultInvalidArgument;
  if (event.Kind == (APPLE_AGX_BACKEND_U32)AppleAgxG13EventChannelError)
    return AppleAgxG13RecoverChannel(Runtime, &event);
  if (Runtime->RecoveryPending) {
    if (!AppleAgxG13DecodeEvent(Message, MessageBytes, &event))
      return AppleAgxG13QueueRuntimeResultInvalidArgument;
    /* Only Timeout has a counter that proves a duplicate. A Fault may
     * be a new halt of a dependent queue before this submission retires;
     * let the platform establish halted and resume it, as Asahi does. */
    if (event.TerminalFault && !AppleAgxG13StaleTimeout(Runtime, &event) &&
        !AppleAgxG13RecoverFirmware(Runtime, &event))
      return AppleAgxG13QueueRuntimeResultResetFailed;
    return AppleAgxG13PollRecovery(Runtime);
  }
  if (Runtime->Phase == AppleAgxG13QueueRuntimeComputeSubmitted) {
    APPLE_AGX_G13_EVENT computeEvent;
    if (!AppleAgxG13DecodeEvent(Message,MessageBytes,&computeEvent))
      return AppleAgxG13QueueRuntimeResultInvalidArgument;
    if (AppleAgxG13StaleTimeout(Runtime, &computeEvent))
      return AppleAgxG13QueueRuntimeResultOk;
    if (computeEvent.TerminalFault && AppleAgxG13RecoverFirmware(Runtime,&computeEvent))
      return AppleAgxG13QueueRuntimeResultOk;
    if (computeEvent.TerminalFault)
      return AppleAgxG13TerminalFailure(
          Runtime,AppleAgxG13QueueCompletionFaulted,
          AppleAgxG13QueueRuntimeResultFaulted);
    if (computeEvent.Kind != (APPLE_AGX_BACKEND_U32)AppleAgxG13EventFlag)
      return AppleAgxG13QueueRuntimeResultOk;
    if (!AppleAgxG13ObserveQueue(Runtime,&computeEvent,
            &Runtime->Config.Compute,&Runtime->ComputePending))
      return AppleAgxG13TerminalFailure(
          Runtime,AppleAgxG13QueueCompletionFaulted,
          AppleAgxG13QueueRuntimeResultTransportFailed);
    /* G2: a standalone compute job completes here. */
    if (Runtime->ComputePending.Complete && Runtime->ComputeTerminal)
      AppleAgxG13SetCompletion(Runtime, AppleAgxG13QueueCompletionSuccess);
    return AppleAgxG13QueueRuntimeResultOk;
  }
  if (Runtime->Phase != AppleAgxG13QueueRuntimeSubmitted &&
      Runtime->Phase != AppleAgxG13QueueRuntimeComputeSubmitted)
    return AppleAgxG13QueueRuntimeResultInvalidState;
  if (!AppleAgxG13DecodeEvent(Message, MessageBytes, &event))
    return AppleAgxG13QueueRuntimeResultInvalidArgument;
  if (AppleAgxG13StaleTimeout(Runtime, &event))
    return AppleAgxG13QueueRuntimeResultOk;
  if (event.TerminalFault && AppleAgxG13RecoverFirmware(Runtime, &event))
    return AppleAgxG13QueueRuntimeResultOk;
  if (event.TerminalFault)
    return AppleAgxG13TerminalFailure(
        Runtime, AppleAgxG13QueueCompletionFaulted,
        AppleAgxG13QueueRuntimeResultFaulted);
  if (event.Kind != (APPLE_AGX_BACKEND_U32)AppleAgxG13EventFlag)
    return AppleAgxG13QueueRuntimeResultOk;

  if (!AppleAgxG13ObserveQueue(Runtime, &event, &Runtime->Config.D3,
                               &Runtime->D3Pending) ||
      !AppleAgxG13ObserveQueue(Runtime, &event, &Runtime->Config.Ta,
                               &Runtime->TaPending) ||
      (Runtime->ComputeInRender &&
       !AppleAgxG13ObserveQueue(Runtime, &event, &Runtime->Config.Compute,
                                &Runtime->ComputePending)))
    return AppleAgxG13TerminalFailure(
        Runtime, AppleAgxG13QueueCompletionFaulted,
        AppleAgxG13QueueRuntimeResultTransportFailed);
  if (Runtime->D3Pending.Complete && Runtime->TaPending.Complete &&
      (!Runtime->ComputeInRender || Runtime->ComputePending.Complete))
    AppleAgxG13SetCompletion(Runtime, AppleAgxG13QueueCompletionSuccess);
  return AppleAgxG13QueueRuntimeResultOk;
}

APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeCheckTimeout(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    APPLE_AGX_BACKEND_U64 NowTicks) {
  if (Runtime == APPLE_AGX_G13_QUEUE_RUNTIME_NULL || NowTicks == 0ULL)
    return AppleAgxG13QueueRuntimeResultInvalidArgument;
  /* EXP1213: SubmitCompute arms the same deadline as Submit. */
  if (Runtime->Phase != AppleAgxG13QueueRuntimeSubmitted &&
      Runtime->Phase != AppleAgxG13QueueRuntimeComputeSubmitted)
    return AppleAgxG13QueueRuntimeResultInvalidState;
  if (Runtime->ChannelRecoveryFailed)
    return AppleAgxG13QueueRuntimeResultResetFailed;
  if (Runtime->RecoveryPending)
    return AppleAgxG13PollRecovery(Runtime);
  if ((APPLE_AGX_BACKEND_U64)(NowTicks - Runtime->DeadlineTicks) >=
      (1ULL << 63))
    return AppleAgxG13QueueRuntimeResultOk;
  /* G4-recovery (EXP1241/EXP1242): an expired deadline is not a hang verdict.
   * The firmware reports faulting work itself (Timeout/Fault events, Asahi
   * handle_timeout/recover) and the Windows TDR owns hang detection
   * (TdrDelay, DxgkDdiResetEngine). The terminal Quiesce that ran here stopped
   * a retained firmware that cannot be restarted, so every later TDR was a
   * 0x116. The expiry is counted once and the job stays pending. */
  if (!Runtime->DeadlinePassed) {
    Runtime->DeadlinePassed = APPLE_AGX_BACKEND_TRUE;
    ++Runtime->DeadlineExpiries;
  }
  return AppleAgxG13QueueRuntimeResultOk;
}

APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeCancel(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime, APPLE_AGX_BACKEND_U32 Fence) {
  if (Runtime == APPLE_AGX_G13_QUEUE_RUNTIME_NULL || Fence == 0u)
    return AppleAgxG13QueueRuntimeResultInvalidArgument;
  if ((Runtime->Phase != AppleAgxG13QueueRuntimeSubmitted &&
       Runtime->Phase != AppleAgxG13QueueRuntimeComputeSubmitted) ||
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
  APPLE_AGX_BACKEND_U32 compute_done=0u;

  if (Runtime == APPLE_AGX_G13_QUEUE_RUNTIME_NULL)
    return AppleAgxG13QueueRuntimeResultInvalidArgument;
  if (Runtime->Phase == AppleAgxG13QueueRuntimeCompletionPending)
    return AppleAgxG13QueueRuntimeResultInvalidState;
  if ((Runtime->Phase == AppleAgxG13QueueRuntimeSubmitted ||
       Runtime->Phase == AppleAgxG13QueueRuntimeComputeSubmitted) &&
      !Runtime->Io.Quiesce(Runtime->Io.Context, Runtime->PendingFence))
    return AppleAgxG13QueueRuntimeResultResetFailed;
  if (!Runtime->Io.ReadU32(Runtime->Io.Context,
                           Runtime->Config.Ta.GpuDonePointer, &ta_done) ||
      !Runtime->Io.ReadU32(Runtime->Io.Context,
                           Runtime->Config.D3.GpuDonePointer, &d3_done) ||
      (Runtime->Config.HasCompute && !Runtime->Io.ReadU32(
          Runtime->Io.Context,Runtime->Config.Compute.GpuDonePointer,
          &compute_done)) ||
      ta_done >= Runtime->Config.Ta.RingCapacity ||
      d3_done >= Runtime->Config.D3.RingCapacity ||
      (Runtime->Config.HasCompute &&
       compute_done>=Runtime->Config.Compute.RingCapacity) ||
      !Runtime->Io.PublishU32(Runtime->Io.Context,
                              Runtime->Config.Ta.CpuWritePointer, ta_done) ||
      !Runtime->Io.PublishU32(Runtime->Io.Context,
                              Runtime->Config.D3.CpuWritePointer, d3_done) ||
      (Runtime->Config.HasCompute && !Runtime->Io.PublishU32(
          Runtime->Io.Context,Runtime->Config.Compute.CpuWritePointer,
          compute_done))) {
    Runtime->Phase = AppleAgxG13QueueRuntimeFaulted;
    return AppleAgxG13QueueRuntimeResultResetFailed;
  }
  Runtime->Io.MemoryBarrier(Runtime->Io.Context);
  AppleAgxG13ClearPending(Runtime);
  Runtime->TaFirstRun = APPLE_AGX_BACKEND_TRUE;
  Runtime->D3FirstRun = APPLE_AGX_BACKEND_TRUE;
  Runtime->ComputeFirstRun = APPLE_AGX_BACKEND_TRUE;
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
  Runtime->Phase = status == AppleAgxG13QueueCompletionSuccess ||
                           status == AppleAgxG13QueueCompletionRecovered
                       ? AppleAgxG13QueueRuntimeReady
                       : AppleAgxG13QueueRuntimeFaulted;
  return APPLE_AGX_BACKEND_TRUE;
}
