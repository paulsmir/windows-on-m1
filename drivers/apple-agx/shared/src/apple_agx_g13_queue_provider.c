#include "apple_agx_g13_queue_provider.h"

#define APPLE_AGX_G13_PROVIDER_NULL ((void *)0)
#define APPLE_AGX_G13_TA_WORK_ROOT_INDEX 1u

static void AppleAgxG13ProviderZero(void *Address,
                                    APPLE_AGX_BACKEND_U32 Bytes) {
  unsigned char *bytes = (unsigned char *)Address;
  APPLE_AGX_BACKEND_U32 index;
  for (index = 0u; index < Bytes; ++index)
    bytes[index] = 0u;
}

static void AppleAgxG13ProviderClearStaged(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider) {
  AppleAgxG13ProviderZero(&Provider->Staged3d,
                          (APPLE_AGX_BACKEND_U32)sizeof(Provider->Staged3d));
  Provider->PendingFence = 0u;
  Provider->FailureQuiesced = APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderNextDone(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    const APPLE_AGX_G13_QUEUE_BINDING *Binding,
    APPLE_AGX_BACKEND_U32 AddressCount, APPLE_AGX_BACKEND_U32 *Next) {
  APPLE_AGX_BACKEND_U32 current;
  APPLE_AGX_BACKEND_U32 index;
  if (AddressCount == 0u ||
      AddressCount > APPLE_AGX_BACKEND_QUEUE_WORK_COUNT ||
      Next == APPLE_AGX_G13_PROVIDER_NULL)
    return APPLE_AGX_BACKEND_FALSE;
  if (!Provider->RuntimeIo.ReadU32(Provider->RuntimeIo.Context,
                                   Binding->CpuWritePointer, &current) ||
      current >= Binding->RingCapacity)
    return APPLE_AGX_BACKEND_FALSE;
  *Next = current;
  for (index = 0u; index < AddressCount; ++index) {
    ++*Next;
    if (*Next >= Binding->RingCapacity)
      *Next = 0u;
  }
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderExpectedDone(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    const APPLE_AGX_G13_QUEUE_BINDING *Binding,
    APPLE_AGX_BACKEND_U32 AddressCount, APPLE_AGX_BACKEND_U32 Expected) {
  APPLE_AGX_BACKEND_U32 next;
  return Expected < Binding->RingCapacity &&
                 AppleAgxG13ProviderNextDone(Provider, Binding, AddressCount,
                                             &next) &&
                 next == Expected
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderAddressesValid(
    const APPLE_AGX_BACKEND_U64
        Addresses[APPLE_AGX_BACKEND_QUEUE_WORK_COUNT],
    APPLE_AGX_BACKEND_U32 AddressCount) {
  APPLE_AGX_BACKEND_U32 index;
  if (AddressCount != APPLE_AGX_BACKEND_QUEUE_WORK_COUNT)
    return APPLE_AGX_BACKEND_FALSE;
  for (index = 0u; index < AddressCount; ++index) {
    if (Addresses[index] == 0ULL || (Addresses[index] & 0x1fULL) != 0ULL)
      return APPLE_AGX_BACKEND_FALSE;
  }
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderProgressBindingValid(
    const APPLE_AGX_G13_QUEUE_BINDING *Binding,
    APPLE_AGX_BACKEND_U32 ExpectedQueueType) {
  return Binding != APPLE_AGX_G13_PROVIDER_NULL &&
         Binding->QueueType == ExpectedQueueType &&
         Binding->RingCapacity > APPLE_AGX_BACKEND_QUEUE_WORK_COUNT &&
         Binding->RingCapacity <= APPLE_AGX_G13_RING_CAPACITY &&
         Binding->GpuDonePointer != APPLE_AGX_G13_PROVIDER_NULL &&
         Binding->Stamp != APPLE_AGX_G13_PROVIDER_NULL;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13Provider3dValid(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    const APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  return Job != APPLE_AGX_G13_PROVIDER_NULL &&
         AppleAgxG13ProviderAddressesValid(Job->D3WorkAddresses,
                                           Job->D3WorkAddressCount) &&
         Job->D3Event == Provider->Config.D3.EventNumber &&
         Job->D3ExpectedStamp != 0u &&
         AppleAgxG13ProviderExpectedDone(
             Provider, &Provider->Config.D3, Job->D3WorkAddressCount,
             Job->D3ExpectedDonePointer);
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderTaValid(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    const APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  APPLE_AGX_BACKEND_U32 publicationCount =
      Provider->Runtime.BufferManagerInitialized
          ? 1u
          : APPLE_AGX_BACKEND_QUEUE_WORK_COUNT;
  return Job != APPLE_AGX_G13_PROVIDER_NULL &&
         AppleAgxG13ProviderAddressesValid(Job->TaWorkAddresses,
                                           Job->TaWorkAddressCount) &&
         Job->TaEvent == Provider->Config.Ta.EventNumber &&
         Job->TaExpectedStamp != 0u &&
         AppleAgxG13ProviderExpectedDone(
             Provider, &Provider->Config.Ta, publicationCount,
             Job->TaExpectedDonePointer);
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderPlanJob(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_G13_QUEUE_JOB_PLAN *Plan) {
  APPLE_AGX_BACKEND_U32 ta_count;
  APPLE_AGX_G13_QUEUE_JOB_PLAN candidate;
  if (Provider == APPLE_AGX_G13_PROVIDER_NULL ||
      Plan == APPLE_AGX_G13_PROVIDER_NULL ||
      Provider->Phase != AppleAgxG13QueueProviderCreated ||
      Provider->PendingFence != 0u)
    return APPLE_AGX_BACKEND_FALSE;
  candidate.IncludeInitBm =
      Provider->Runtime.BufferManagerInitialized
          ? APPLE_AGX_BACKEND_FALSE
          : APPLE_AGX_BACKEND_TRUE;
  ta_count = candidate.IncludeInitBm ? APPLE_AGX_BACKEND_QUEUE_WORK_COUNT : 1u;
  if (!AppleAgxG13ProviderNextDone(Provider, &Provider->Config.Ta, ta_count,
                                   &candidate.TaExpectedDonePointer) ||
      !AppleAgxG13ProviderNextDone(
          Provider, &Provider->Config.D3, APPLE_AGX_BACKEND_QUEUE_WORK_COUNT,
          &candidate.D3ExpectedDonePointer))
    return APPLE_AGX_BACKEND_FALSE;
  *Plan = candidate;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderQueryProgress(
    const APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_G13_QUEUE_PROGRESS *Progress) {
  const APPLE_AGX_G13_QUEUE_RUNTIME *runtime;
  APPLE_AGX_G13_QUEUE_PROGRESS candidate;

  if (Provider == APPLE_AGX_G13_PROVIDER_NULL ||
      Progress == APPLE_AGX_G13_PROVIDER_NULL ||
      Provider->Phase != AppleAgxG13QueueProviderSubmitted ||
      Provider->PendingFence == 0u)
    return APPLE_AGX_BACKEND_FALSE;
  runtime = &Provider->Runtime;
  if (runtime->Phase != AppleAgxG13QueueRuntimeSubmitted ||
      runtime->PendingFence != Provider->PendingFence ||
      runtime->Io.ReadU32 == APPLE_AGX_G13_PROVIDER_NULL ||
      !AppleAgxG13ProviderProgressBindingValid(
          &runtime->Config.Ta, (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueTa) ||
      !AppleAgxG13ProviderProgressBindingValid(
          &runtime->Config.D3, (APPLE_AGX_BACKEND_U32)AppleAgxG13Queue3d))
    return APPLE_AGX_BACKEND_FALSE;
  if (!runtime->Io.ReadU32(runtime->Io.Context,
                           runtime->Config.Ta.GpuDonePointer,
                           &candidate.TaDonePointer) ||
      !runtime->Io.ReadU32(runtime->Io.Context, runtime->Config.Ta.Stamp,
                           &candidate.TaStamp) ||
      !runtime->Io.ReadU32(runtime->Io.Context,
                           runtime->Config.D3.GpuDonePointer,
                           &candidate.D3DonePointer) ||
      !runtime->Io.ReadU32(runtime->Io.Context, runtime->Config.D3.Stamp,
                           &candidate.D3Stamp) ||
      candidate.TaDonePointer >= runtime->Config.Ta.RingCapacity ||
      candidate.D3DonePointer >= runtime->Config.D3.RingCapacity)
    return APPLE_AGX_BACKEND_FALSE;
  candidate.Fence = Provider->PendingFence;
  candidate.ProviderPhase = Provider->Phase;
  candidate.RuntimePhase = runtime->Phase;
  candidate.TaEventSeen = runtime->TaPending.EventSeen;
  candidate.TaComplete = runtime->TaPending.Complete;
  candidate.D3EventSeen = runtime->D3Pending.EventSeen;
  candidate.D3Complete = runtime->D3Pending.Complete;
  *Progress = candidate;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProgressHasAdvanced(
    const APPLE_AGX_G13_QUEUE_PROGRESS *Previous,
    const APPLE_AGX_G13_QUEUE_PROGRESS *Current) {
  if (Previous == APPLE_AGX_G13_PROVIDER_NULL ||
      Current == APPLE_AGX_G13_PROVIDER_NULL || Previous->Fence == 0u ||
      Previous->Fence != Current->Fence ||
      Previous->ProviderPhase != AppleAgxG13QueueProviderSubmitted ||
      Current->ProviderPhase != AppleAgxG13QueueProviderSubmitted ||
      Previous->RuntimePhase != AppleAgxG13QueueRuntimeSubmitted ||
      Current->RuntimePhase != AppleAgxG13QueueRuntimeSubmitted)
    return APPLE_AGX_BACKEND_FALSE;
  return Previous->TaDonePointer != Current->TaDonePointer ||
                 Previous->TaStamp != Current->TaStamp ||
                 Previous->TaEventSeen != Current->TaEventSeen ||
                 Previous->TaComplete != Current->TaComplete ||
                 Previous->D3DonePointer != Current->D3DonePointer ||
                 Previous->D3Stamp != Current->D3Stamp ||
                 Previous->D3EventSeen != Current->D3EventSeen ||
                 Previous->D3Complete != Current->D3Complete
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13Provider3dMatches(
    const APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    const APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  APPLE_AGX_BACKEND_U32 index;
  if (Job->D3WorkAddressCount != Provider->Staged3d.WorkAddressCount ||
      Job->D3Event != Provider->Staged3d.Event ||
      Job->D3ExpectedStamp != Provider->Staged3d.ExpectedStamp ||
      Job->D3ExpectedDonePointer !=
          Provider->Staged3d.ExpectedDonePointer)
    return APPLE_AGX_BACKEND_FALSE;
  for (index = 0u; index < Job->D3WorkAddressCount; ++index) {
    if (Job->D3WorkAddresses[index] !=
        Provider->Staged3d.WorkAddresses[index])
      return APPLE_AGX_BACKEND_FALSE;
  }
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderCreate(void *Context) {
  APPLE_AGX_G13_QUEUE_PROVIDER *provider =
      (APPLE_AGX_G13_QUEUE_PROVIDER *)Context;
  if (provider == APPLE_AGX_G13_PROVIDER_NULL ||
      provider->Phase != AppleAgxG13QueueProviderInitialized ||
      provider->RuntimeIo.Quiesce == APPLE_AGX_G13_PROVIDER_NULL ||
      AppleAgxG13QueueRuntimeInitialize(&provider->Runtime, &provider->Config,
                                        &provider->RuntimeIo) !=
          AppleAgxG13QueueRuntimeResultOk)
    return APPLE_AGX_BACKEND_FALSE;
  provider->Phase = AppleAgxG13QueueProviderCreated;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderDestroy(void *Context) {
  APPLE_AGX_G13_QUEUE_PROVIDER *provider =
      (APPLE_AGX_G13_QUEUE_PROVIDER *)Context;
  if (provider == APPLE_AGX_G13_PROVIDER_NULL)
    return APPLE_AGX_BACKEND_FALSE;
  if (provider->Phase == AppleAgxG13QueueProviderInitialized)
    return APPLE_AGX_BACKEND_TRUE;
  if (provider->Phase != AppleAgxG13QueueProviderCreated)
    return APPLE_AGX_BACKEND_FALSE;
  if (AppleAgxG13QueueRuntimeReset(&provider->Runtime) !=
      AppleAgxG13QueueRuntimeResultOk)
    return APPLE_AGX_BACKEND_FALSE;
  AppleAgxG13ProviderClearStaged(provider);
  provider->Phase = AppleAgxG13QueueProviderInitialized;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderRun3d(
    void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
    APPLE_AGX_BACKEND_U32 Fence) {
  APPLE_AGX_G13_QUEUE_PROVIDER *provider =
      (APPLE_AGX_G13_QUEUE_PROVIDER *)Context;
  APPLE_AGX_BACKEND_U32 index;
  if (provider == APPLE_AGX_G13_PROVIDER_NULL || Fence == 0u ||
      provider->Phase != AppleAgxG13QueueProviderCreated ||
      !AppleAgxG13Provider3dValid(provider, Job))
    return APPLE_AGX_BACKEND_FALSE;
  for (index = 0u; index < Job->D3WorkAddressCount; ++index)
    provider->Staged3d.WorkAddresses[index] = Job->D3WorkAddresses[index];
  provider->Staged3d.WorkAddressCount = Job->D3WorkAddressCount;
  provider->Staged3d.Event = Job->D3Event;
  provider->Staged3d.ExpectedStamp = Job->D3ExpectedStamp;
  provider->Staged3d.ExpectedDonePointer = Job->D3ExpectedDonePointer;
  provider->PendingFence = Fence;
  provider->Phase = AppleAgxG13QueueProvider3dStaged;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderRunTa(
    void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
    APPLE_AGX_BACKEND_U32 Fence) {
  APPLE_AGX_G13_QUEUE_PROVIDER *provider =
      (APPLE_AGX_G13_QUEUE_PROVIDER *)Context;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION submission;
  APPLE_AGX_G13_QUEUE_RUNTIME_RESULT result;
  APPLE_AGX_BACKEND_U32 index;
  if (provider == APPLE_AGX_G13_PROVIDER_NULL || Job == APPLE_AGX_G13_PROVIDER_NULL ||
      provider->Phase != AppleAgxG13QueueProvider3dStaged ||
      Fence != provider->PendingFence ||
      !AppleAgxG13Provider3dMatches(provider, Job) ||
      !AppleAgxG13Provider3dValid(provider, Job) ||
      !AppleAgxG13ProviderTaValid(provider, Job))
    return APPLE_AGX_BACKEND_FALSE;
  AppleAgxG13ProviderZero(&submission,
                          (APPLE_AGX_BACKEND_U32)sizeof(submission));
  if (!provider->ProviderIo.BuildSubmission(
          provider->ProviderIo.Context, Job, Fence, &submission))
    return APPLE_AGX_BACKEND_FALSE;
  submission.Fence = Fence;
  for (index = 0u; index < provider->Staged3d.WorkAddressCount; ++index)
    submission.D3.GpuAddresses[index] =
        provider->Staged3d.WorkAddresses[index];
  submission.D3.GpuAddressCount = provider->Staged3d.WorkAddressCount;
  submission.D3.ExpectedStamp = provider->Staged3d.ExpectedStamp;
  if (provider->Runtime.BufferManagerInitialized) {
    /* The persistent InitBM root is queue-lifetime state, not per-job work. */
    submission.Ta.GpuAddresses[0] =
        Job->TaWorkAddresses[APPLE_AGX_G13_TA_WORK_ROOT_INDEX];
    submission.Ta.GpuAddressCount = 1u;
    if (submission.Ta.PreparedRangeCount != 0u) {
      if (submission.Ta.PreparedRangeCount !=
          APPLE_AGX_BACKEND_QUEUE_WORK_COUNT)
        return APPLE_AGX_BACKEND_FALSE;
      submission.Ta.PreparedRanges[0] =
          submission.Ta.PreparedRanges[APPLE_AGX_G13_TA_WORK_ROOT_INDEX];
      AppleAgxG13ProviderZero(
          &submission.Ta.PreparedRanges[1],
          (APPLE_AGX_BACKEND_U32)sizeof(submission.Ta.PreparedRanges[1]));
      submission.Ta.PreparedRangeCount = 1u;
    }
  } else {
    for (index = 0u; index < Job->TaWorkAddressCount; ++index)
      submission.Ta.GpuAddresses[index] = Job->TaWorkAddresses[index];
    submission.Ta.GpuAddressCount = Job->TaWorkAddressCount;
  }
  submission.Ta.ExpectedStamp = Job->TaExpectedStamp;
  result = AppleAgxG13QueueRuntimeSubmit(&provider->Runtime, &submission);
  if (result != AppleAgxG13QueueRuntimeResultOk) {
    if (provider->Runtime.Phase == AppleAgxG13QueueRuntimeFaulted) {
      provider->FailureQuiesced =
          result == AppleAgxG13QueueRuntimeResultResetFailed
              ? APPLE_AGX_BACKEND_FALSE
              : APPLE_AGX_BACKEND_TRUE;
      provider->Phase = AppleAgxG13QueueProviderFaulted;
    }
    return APPLE_AGX_BACKEND_FALSE;
  }
  provider->Phase = AppleAgxG13QueueProviderSubmitted;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderRestoreAfterQuiesce(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider) {
  if (AppleAgxG13QueueRuntimeReset(&Provider->Runtime) !=
      AppleAgxG13QueueRuntimeResultOk)
    return APPLE_AGX_BACKEND_FALSE;
  AppleAgxG13ProviderClearStaged(Provider);
  Provider->Phase = AppleAgxG13QueueProviderCreated;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderEnsureFailureQuiesced(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider) {
  if (Provider->FailureQuiesced)
    return APPLE_AGX_BACKEND_TRUE;
  if (Provider->PendingFence == 0u ||
      Provider->RuntimeIo.Quiesce == APPLE_AGX_G13_PROVIDER_NULL ||
      !Provider->RuntimeIo.Quiesce(Provider->RuntimeIo.Context,
                                   Provider->PendingFence))
    return APPLE_AGX_BACKEND_FALSE;
  Provider->FailureQuiesced = APPLE_AGX_BACKEND_TRUE;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderStop(
    void *Context, APPLE_AGX_BACKEND_U32 Fence) {
  APPLE_AGX_G13_QUEUE_PROVIDER *provider =
      (APPLE_AGX_G13_QUEUE_PROVIDER *)Context;
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION completion;
  APPLE_AGX_G13_QUEUE_RUNTIME_RESULT result;
  if (provider == APPLE_AGX_G13_PROVIDER_NULL)
    return APPLE_AGX_BACKEND_FALSE;
  if (Fence == 0u)
    return provider->Phase == AppleAgxG13QueueProviderCreated &&
                   provider->PendingFence == 0u
               ? APPLE_AGX_BACKEND_TRUE
               : APPLE_AGX_BACKEND_FALSE;
  if (Fence != provider->PendingFence)
    return APPLE_AGX_BACKEND_FALSE;
  if (provider->Phase == AppleAgxG13QueueProvider3dStaged) {
    AppleAgxG13ProviderClearStaged(provider);
    provider->Phase = AppleAgxG13QueueProviderCreated;
    return APPLE_AGX_BACKEND_TRUE;
  }
  if (provider->Phase == AppleAgxG13QueueProviderFaulted) {
    if (!AppleAgxG13ProviderEnsureFailureQuiesced(provider))
      return APPLE_AGX_BACKEND_FALSE;
    return AppleAgxG13ProviderRestoreAfterQuiesce(provider);
  }
  if (provider->Phase != AppleAgxG13QueueProviderSubmitted)
    return APPLE_AGX_BACKEND_FALSE;
  result = AppleAgxG13QueueRuntimeCancel(&provider->Runtime, Fence);
  if (result == AppleAgxG13QueueRuntimeResultResetFailed) {
    provider->FailureQuiesced = APPLE_AGX_BACKEND_FALSE;
    provider->Phase = AppleAgxG13QueueProviderFaulted;
    return APPLE_AGX_BACKEND_FALSE;
  }
  if (result != AppleAgxG13QueueRuntimeResultCancelled ||
      !AppleAgxG13QueueRuntimeTakeCompletion(&provider->Runtime, &completion) ||
      completion.Fence != Fence ||
      completion.Status != AppleAgxG13QueueCompletionCancelled)
    return APPLE_AGX_BACKEND_FALSE;
  provider->FailureQuiesced = APPLE_AGX_BACKEND_TRUE;
  return AppleAgxG13ProviderRestoreAfterQuiesce(provider);
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderReset(
    void *Context, APPLE_AGX_BACKEND_U32 Fence) {
  APPLE_AGX_G13_QUEUE_PROVIDER *provider =
      (APPLE_AGX_G13_QUEUE_PROVIDER *)Context;
  if (provider == APPLE_AGX_G13_PROVIDER_NULL || Fence == 0u ||
      Fence != provider->PendingFence)
    return APPLE_AGX_BACKEND_FALSE;
  if (provider->Phase == AppleAgxG13QueueProvider3dStaged) {
    AppleAgxG13ProviderClearStaged(provider);
    provider->Phase = AppleAgxG13QueueProviderCreated;
    return APPLE_AGX_BACKEND_TRUE;
  }
  if (provider->Phase == AppleAgxG13QueueProviderFaulted &&
      !AppleAgxG13ProviderEnsureFailureQuiesced(provider))
    return APPLE_AGX_BACKEND_FALSE;
  if (provider->Phase != AppleAgxG13QueueProviderSubmitted &&
      provider->Phase != AppleAgxG13QueueProviderFaulted)
    return APPLE_AGX_BACKEND_FALSE;
  return AppleAgxG13ProviderRestoreAfterQuiesce(provider);
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderInitialize(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    const APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *Config,
    const APPLE_AGX_G13_QUEUE_RUNTIME_IO *RuntimeIo,
    const APPLE_AGX_G13_QUEUE_PROVIDER_IO *ProviderIo) {
  if (Provider == APPLE_AGX_G13_PROVIDER_NULL ||
      Config == APPLE_AGX_G13_PROVIDER_NULL ||
      RuntimeIo == APPLE_AGX_G13_PROVIDER_NULL ||
      ProviderIo == APPLE_AGX_G13_PROVIDER_NULL ||
      ProviderIo->BuildSubmission == APPLE_AGX_G13_PROVIDER_NULL)
    return APPLE_AGX_BACKEND_FALSE;
  AppleAgxG13ProviderZero(Provider,
                          (APPLE_AGX_BACKEND_U32)sizeof(*Provider));
  Provider->Config = *Config;
  Provider->RuntimeIo = *RuntimeIo;
  Provider->ProviderIo = *ProviderIo;
  Provider->Phase = AppleAgxG13QueueProviderInitialized;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderInstall(
    APPLE_AGX_BACKEND_IO *Io) {
  if (Io == APPLE_AGX_G13_PROVIDER_NULL ||
      Io->Context == APPLE_AGX_G13_PROVIDER_NULL)
    return APPLE_AGX_BACKEND_FALSE;
  Io->Queues.Create = AppleAgxG13ProviderCreate;
  Io->Queues.Destroy = AppleAgxG13ProviderDestroy;
  Io->Queues.Run3d = AppleAgxG13ProviderRun3d;
  Io->Queues.RunTa = AppleAgxG13ProviderRunTa;
  Io->Queues.Stop = AppleAgxG13ProviderStop;
  Io->Queues.Reset = AppleAgxG13ProviderReset;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderAppendObservation(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch,
    APPLE_AGX_BACKEND_QUEUE Queue,
    const APPLE_AGX_G13_QUEUE_BINDING *Binding,
    const APPLE_AGX_G13_QUEUE_PENDING *Pending) {
  APPLE_AGX_BACKEND_OBSERVATION *observation =
      &Batch->Observations[Batch->ObservationCount++];
  if (!Provider->RuntimeIo.ReadU32(Provider->RuntimeIo.Context,
                                   Binding->Stamp, &observation->Stamp) ||
      !Provider->RuntimeIo.ReadU32(Provider->RuntimeIo.Context,
                                   Binding->GpuDonePointer,
                                   &observation->DonePointer)) {
    --Batch->ObservationCount;
    return APPLE_AGX_BACKEND_FALSE;
  }
  observation->Queue = Queue;
  observation->Event = Pending->EventNumber;
  observation->Status = AppleAgxBackendObservationComplete;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderIngestEvent(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider, const unsigned char *Message,
    APPLE_AGX_BACKEND_U32 MessageBytes,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch) {
  APPLE_AGX_BACKEND_BOOL d3WasComplete;
  APPLE_AGX_BACKEND_BOOL taWasComplete;
  APPLE_AGX_G13_QUEUE_RUNTIME_RESULT result;
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION completion;
  if (Provider == APPLE_AGX_G13_PROVIDER_NULL ||
      Message == APPLE_AGX_G13_PROVIDER_NULL ||
      Batch == APPLE_AGX_G13_PROVIDER_NULL ||
      Provider->Phase != AppleAgxG13QueueProviderSubmitted)
    return APPLE_AGX_BACKEND_FALSE;
  AppleAgxG13ProviderZero(Batch, (APPLE_AGX_BACKEND_U32)sizeof(*Batch));
  d3WasComplete = Provider->Runtime.D3Pending.Complete;
  taWasComplete = Provider->Runtime.TaPending.Complete;
  result = AppleAgxG13QueueRuntimeHandleEvent(&Provider->Runtime, Message,
                                              MessageBytes);
  if (result == AppleAgxG13QueueRuntimeResultInvalidArgument ||
      result == AppleAgxG13QueueRuntimeResultInvalidState ||
      result == AppleAgxG13QueueRuntimeResultResetFailed) {
    if (result == AppleAgxG13QueueRuntimeResultResetFailed)
      Provider->Phase = AppleAgxG13QueueProviderFaulted;
    return APPLE_AGX_BACKEND_FALSE;
  }
  if ((!d3WasComplete && Provider->Runtime.D3Pending.Complete &&
       !AppleAgxG13ProviderAppendObservation(
           Provider, Batch, AppleAgxBackendQueue3d, &Provider->Config.D3,
           &Provider->Runtime.D3Pending)) ||
      (!taWasComplete && Provider->Runtime.TaPending.Complete &&
       !AppleAgxG13ProviderAppendObservation(
           Provider, Batch, AppleAgxBackendQueueTa, &Provider->Config.Ta,
           &Provider->Runtime.TaPending))) {
    Provider->Phase = AppleAgxG13QueueProviderFaulted;
    return APPLE_AGX_BACKEND_FALSE;
  }
  if (!AppleAgxG13QueueRuntimeTakeCompletion(&Provider->Runtime, &completion))
    return APPLE_AGX_BACKEND_TRUE;
  if (completion.Fence != Provider->PendingFence)
    return APPLE_AGX_BACKEND_FALSE;
  Batch->CompletedFence = completion.Fence;
  if (completion.Status == AppleAgxG13QueueCompletionSuccess) {
    AppleAgxG13ProviderClearStaged(Provider);
    Provider->Phase = AppleAgxG13QueueProviderCreated;
    return APPLE_AGX_BACKEND_TRUE;
  }
  Batch->ObservationCount = 1u;
  AppleAgxG13ProviderZero(&Batch->Observations[0],
      (APPLE_AGX_BACKEND_U32)sizeof(Batch->Observations[0]));
  Batch->Observations[0].Status = AppleAgxBackendObservationFault;
  Provider->FailureQuiesced = APPLE_AGX_BACKEND_TRUE;
  Provider->Phase = AppleAgxG13QueueProviderFaulted;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderCheckTimeout(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_BACKEND_U64 NowTicks,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch) {
  APPLE_AGX_G13_QUEUE_RUNTIME_RESULT result;
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION completion;

  if (Provider == APPLE_AGX_G13_PROVIDER_NULL || NowTicks == 0ULL ||
      Batch == APPLE_AGX_G13_PROVIDER_NULL ||
      Provider->Phase != AppleAgxG13QueueProviderSubmitted)
    return APPLE_AGX_BACKEND_FALSE;
  AppleAgxG13ProviderZero(Batch, (APPLE_AGX_BACKEND_U32)sizeof(*Batch));
  result = AppleAgxG13QueueRuntimeCheckTimeout(&Provider->Runtime, NowTicks);
  if (result == AppleAgxG13QueueRuntimeResultOk)
    return APPLE_AGX_BACKEND_TRUE;
  if (result == AppleAgxG13QueueRuntimeResultResetFailed) {
    Provider->FailureQuiesced = APPLE_AGX_BACKEND_FALSE;
    Provider->Phase = AppleAgxG13QueueProviderFaulted;
    return APPLE_AGX_BACKEND_FALSE;
  }
  if (result != AppleAgxG13QueueRuntimeResultTimedOut ||
      !AppleAgxG13QueueRuntimeTakeCompletion(&Provider->Runtime,
                                              &completion) ||
      completion.Fence != Provider->PendingFence ||
      completion.Status != AppleAgxG13QueueCompletionTimedOut)
    return APPLE_AGX_BACKEND_FALSE;
  Batch->ObservationCount = 1u;
  Batch->Observations[0].Status = AppleAgxBackendObservationTimeout;
  Batch->CompletedFence = completion.Fence;
  Provider->FailureQuiesced = APPLE_AGX_BACKEND_TRUE;
  Provider->Phase = AppleAgxG13QueueProviderFaulted;
  return APPLE_AGX_BACKEND_TRUE;
}
