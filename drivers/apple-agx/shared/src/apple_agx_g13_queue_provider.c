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
  AppleAgxG13ProviderZero(&Provider->StagedSubmission,
      (APPLE_AGX_BACKEND_U32)sizeof(Provider->StagedSubmission));
  Provider->PendingFence = 0u;
  Provider->FailureQuiesced = APPLE_AGX_BACKEND_FALSE;
  Provider->SuccessorFence = 0u;
  Provider->SuccessorComputeOnly = APPLE_AGX_BACKEND_FALSE;
  Provider->ObservedMask = 0u;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderNextDone(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    const APPLE_AGX_G13_QUEUE_BINDING *Binding,
    APPLE_AGX_BACKEND_U32 AddressCount, APPLE_AGX_BACKEND_U32 *Next) {
  APPLE_AGX_BACKEND_U32 current;
  APPLE_AGX_BACKEND_U32 index;
  /* D3D-G1b: a TA publication may lead with a Barrier (one more entry). */
  if (AddressCount == 0u ||
      AddressCount > APPLE_AGX_G13_QUEUE_PUBLISH_MAX ||
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

static void AppleAgxG13ProviderCaptureComputeIdentity(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider, void *Context,
    const APPLE_AGX_BACKEND_JOB_IMAGE *Job, APPLE_AGX_BACKEND_U32 Fence,
    APPLE_AGX_G13_COMPUTE_IDENTITY_SNAPSHOT *Snapshot) {
  APPLE_AGX_BACKEND_U32 current = 0u;
  APPLE_AGX_BACKEND_BOOL current_valid = APPLE_AGX_BACKEND_FALSE;
  AppleAgxG13ProviderZero(Snapshot,
                          (APPLE_AGX_BACKEND_U32)sizeof(*Snapshot));
  Snapshot->ContextIdentity = (APPLE_AGX_BACKEND_U64)(unsigned long long)Context;
  Snapshot->ProviderIdentity = (APPLE_AGX_BACKEND_U64)(unsigned long long)Provider;
  Snapshot->JobIdentity = (APPLE_AGX_BACKEND_U64)(unsigned long long)Job;
  Snapshot->Fence = Fence;
  Snapshot->PendingFence = Provider->PendingFence;
  Snapshot->ProviderPhase = (APPLE_AGX_BACKEND_U32)Provider->Phase;
  Snapshot->HasCompute = Provider->Config.HasCompute;
  if (Job != APPLE_AGX_G13_PROVIDER_NULL) {
    Snapshot->WorkAddressCount = Job->ComputeWorkAddressCount;
    Snapshot->JobEvent = Job->ComputeEvent;
    Snapshot->JobExpectedStamp = Job->ComputeExpectedStamp;
    Snapshot->JobExpectedDonePointer = Job->ComputeExpectedDonePointer;
    Snapshot->WorkAddress = Job->ComputeWorkAddresses[0];
  }
  Snapshot->ConfigEvent = Provider->Config.Compute.EventNumber;
  Snapshot->QueueInfoGpuAddress = Provider->Config.Compute.QueueInfoGpuAddress;
  if (Provider->Config.Compute.CpuWritePointer != APPLE_AGX_G13_PROVIDER_NULL &&
      Provider->RuntimeIo.ReadU32 != APPLE_AGX_G13_PROVIDER_NULL &&
      Provider->RuntimeIo.ReadU32(Provider->RuntimeIo.Context,
                                  Provider->Config.Compute.CpuWritePointer,
                                  &current)) {
    Snapshot->CpuWritePointer = current;
    current_valid = current < Provider->Config.Compute.RingCapacity
                        ? APPLE_AGX_BACKEND_TRUE
                        : APPLE_AGX_BACKEND_FALSE;
  }
  if (current_valid) {
    Snapshot->ExpectedNextDone = current + 1u;
    if (Snapshot->ExpectedNextDone >= Provider->Config.Compute.RingCapacity)
      Snapshot->ExpectedNextDone = 0u;
  }
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
  /* D3D-G1b: the leading Barrier is one more TA ring entry; rc102: so is
   * a successor render's depth-2 Barrier (a render with compute work
   * leads its compute queue instead). */
  if (Job != APPLE_AGX_G13_PROVIDER_NULL &&
      (Job->TaBarrierAddress != 0ULL || Job->LeadBarrierAddress != 0ULL))
    ++publicationCount;
  return Job != APPLE_AGX_G13_PROVIDER_NULL &&
         AppleAgxG13ProviderAddressesValid(Job->TaWorkAddresses,
                                           Job->TaWorkAddressCount) &&
         Job->TaEvent == Provider->Config.Ta.EventNumber &&
         Job->TaExpectedStamp != 0u &&
         AppleAgxG13ProviderExpectedDone(
             Provider, &Provider->Config.Ta, publicationCount,
             Job->TaExpectedDonePointer);
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderRequireInitBm(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider) {
  if (!Provider || Provider->Phase!=AppleAgxG13QueueProviderCreated ||
      Provider->PendingFence || Provider->Runtime.PendingFence ||
      Provider->Runtime.Phase!=AppleAgxG13QueueRuntimeReady ||
      Provider->Runtime.CompletionAvailable)
    return APPLE_AGX_BACKEND_FALSE;
  Provider->Runtime.BufferManagerInitialized=APPLE_AGX_BACKEND_FALSE;
  return APPLE_AGX_BACKEND_TRUE;
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
  /* rc102 (EXP2039): every field, Successor included, is defined; a stack
   * candidate copied out field-incomplete made a depth-1 plan a successor
   * plan and failed the first submit of the boot. */
  AppleAgxG13ProviderZero(&candidate, (APPLE_AGX_BACKEND_U32)sizeof(candidate));
  candidate.InitializeQueues = Provider->Runtime.TaFirstRun;
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
  candidate.ComputeExpectedDonePointer=0u;
  candidate.TaBarrierExpectedDonePointer=0u;
  if(Provider->Config.HasCompute &&
     (!AppleAgxG13ProviderNextDone(Provider,&Provider->Config.Compute,1u,
                                   &candidate.ComputeExpectedDonePointer) ||
      !AppleAgxG13ProviderNextDone(Provider,&Provider->Config.Ta,ta_count+1u,
                                   &candidate.TaBarrierExpectedDonePointer)))
    return APPLE_AGX_BACKEND_FALSE;
  *Plan = candidate;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderPlanSuccessor(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_G13_QUEUE_JOB_PLAN *Plan) {
  APPLE_AGX_G13_QUEUE_JOB_PLAN candidate;
  if (Provider == APPLE_AGX_G13_PROVIDER_NULL ||
      Plan == APPLE_AGX_G13_PROVIDER_NULL ||
      (Provider->Phase != AppleAgxG13QueueProviderSubmitted &&
       Provider->Phase != AppleAgxG13QueueProviderComputeOnlySubmitted) ||
      Provider->PendingFence == 0u || Provider->SuccessorFence != 0u ||
      Provider->Runtime.PendingFence != Provider->PendingFence ||
      Provider->Runtime.Successor.Fence != 0u ||
      !Provider->Runtime.BufferManagerInitialized ||
      Provider->Runtime.TaFirstRun || Provider->Runtime.D3FirstRun)
    return APPLE_AGX_BACKEND_FALSE;
  AppleAgxG13ProviderZero(&candidate, (APPLE_AGX_BACKEND_U32)sizeof(candidate));
  candidate.Successor = APPLE_AGX_BACKEND_TRUE;
  /* After the running job's entries: TA = its root + the Barrier (the
   * depth-2 one of a render without compute, or the D3D-G1b one). */
  if (!AppleAgxG13ProviderNextDone(Provider, &Provider->Config.Ta, 2u,
                                   &candidate.TaExpectedDonePointer) ||
      !AppleAgxG13ProviderNextDone(
          Provider, &Provider->Config.D3, APPLE_AGX_BACKEND_QUEUE_WORK_COUNT,
          &candidate.D3ExpectedDonePointer))
    return APPLE_AGX_BACKEND_FALSE;
  candidate.TaBarrierExpectedDonePointer = candidate.TaExpectedDonePointer;
  if (Provider->Config.HasCompute &&
      !AppleAgxG13ProviderNextDone(Provider, &Provider->Config.Compute, 2u,
                                   &candidate.ComputeExpectedDonePointer))
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

/* RunTa's publication of a staged render, and rc102's successor render
 * (Successor: no staged 3D, the lead Barrier first). */
static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderSubmitRender(
    APPLE_AGX_G13_QUEUE_PROVIDER *provider, void *Context,
    const APPLE_AGX_BACKEND_JOB_IMAGE *Job, APPLE_AGX_BACKEND_U32 Fence,
    APPLE_AGX_BACKEND_BOOL Successor) {
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION submission;
  APPLE_AGX_G13_QUEUE_RUNTIME_RESULT result;
  APPLE_AGX_BACKEND_U32 index;
#define SUBMIT_REJECT(g) do { provider->LastSubmitGuard=(g); return APPLE_AGX_BACKEND_FALSE; } while(0)
  if (!AppleAgxG13Provider3dValid(provider, Job)) SUBMIT_REJECT(AppleAgxG13SubmitGuardD3Valid);
  if (!AppleAgxG13ProviderTaValid(provider, Job)) SUBMIT_REJECT(AppleAgxG13SubmitGuardTaValid);
  if (Job->ComputeWorkAddressCount != 0u) {
    APPLE_AGX_G13_COMPUTE_IDENTITY_SNAPSHOT compute;
    APPLE_AGX_BACKEND_U32 subguard = AppleAgxG13ComputeGuardNone;
    AppleAgxG13ProviderCaptureComputeIdentity(provider, Context, Job, Fence,
                                               &compute);
    if (!compute.HasCompute)
      subguard = AppleAgxG13ComputeGuardUnavailable;
    else if (compute.WorkAddressCount != 1u)
      subguard = AppleAgxG13ComputeGuardCount;
    else if (compute.JobEvent != compute.ConfigEvent)
      subguard = AppleAgxG13ComputeGuardEvent;
    else if (!compute.JobExpectedStamp)
      subguard = AppleAgxG13ComputeGuardStamp;
    else if (!AppleAgxG13ProviderExpectedDone(
                 provider, &provider->Config.Compute,
                 Job->LeadBarrierAddress != 0ULL ? 2u : 1u,
                 Job->ComputeExpectedDonePointer))
      subguard = AppleAgxG13ComputeGuardDonePointer;
    else if (Job->TaBarrierAddress == 0ULL || (Job->TaBarrierAddress & 0x1fULL))
      subguard = AppleAgxG13ComputeGuardBarrier;
    if (subguard != AppleAgxG13ComputeGuardNone) {
      compute.Guard = AppleAgxG13SubmitGuardCompute;
      compute.Subguard = subguard;
      compute.Valid = APPLE_AGX_BACKEND_TRUE;
      provider->ComputeIdentityDiagnostic.RunTa = compute;
      provider->LastSubmitRuntimeResult = subguard;
      SUBMIT_REJECT(AppleAgxG13SubmitGuardCompute);
    }
  }
  AppleAgxG13ProviderZero(&submission,
                          (APPLE_AGX_BACKEND_U32)sizeof(submission));
  if (!provider->ProviderIo.BuildSubmission(
          provider->ProviderIo.Context, Job, Fence, &submission))
    SUBMIT_REJECT(AppleAgxG13SubmitGuardBuild);
  submission.Fence = Fence;
  for (index = 0u; index < Job->D3WorkAddressCount; ++index)
    submission.D3.GpuAddresses[index] = Job->D3WorkAddresses[index];
  submission.D3.GpuAddressCount = Job->D3WorkAddressCount;
  submission.D3.ExpectedStamp = Job->D3ExpectedStamp;
  if (provider->Runtime.BufferManagerInitialized) {
    /* Omit InitBM only while the selected manager binding remains valid. */
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
  if (Successor) {
    /* rc102: the lead Barrier goes first in the queue the job starts on:
     * compute for a render with compute work, TA otherwise. */
    APPLE_AGX_G13_WORK_PUBLICATION *lead =
        Job->ComputeWorkAddressCount != 0u ? &submission.Compute
                                           : &submission.Ta;
    const APPLE_AGX_BACKEND_BOOL prepared =
        lead->PreparedRangeCount != 0u ? APPLE_AGX_BACKEND_TRUE
                                       : APPLE_AGX_BACKEND_FALSE;
    if (Job->LeadBarrierAddress == 0ULL || (Job->LeadBarrierAddress & 0x1fULL) ||
        lead->GpuAddressCount == 0u ||
        lead->GpuAddressCount >= APPLE_AGX_G13_QUEUE_PUBLISH_MAX ||
        (prepared && (submission.LeadBarrierRange.Address ==
                          APPLE_AGX_G13_PROVIDER_NULL ||
                      submission.LeadBarrierRange.Bytes == 0u ||
                      lead->PreparedRangeCount != lead->GpuAddressCount)))
      SUBMIT_REJECT(AppleAgxG13SubmitGuardBuild);
    for (index = lead->GpuAddressCount; index > 0u; --index) {
      lead->GpuAddresses[index] = lead->GpuAddresses[index - 1u];
      if (prepared)
        lead->PreparedRanges[index] = lead->PreparedRanges[index - 1u];
    }
    lead->GpuAddresses[0] = Job->LeadBarrierAddress;
    ++lead->GpuAddressCount;
    if (prepared) {
      lead->PreparedRanges[0].Address = submission.LeadBarrierRange.Address;
      lead->PreparedRanges[0].Bytes = submission.LeadBarrierRange.Bytes;
      ++lead->PreparedRangeCount;
    }
    submission.Successor = APPLE_AGX_BACKEND_TRUE;
  }
  if (Job->TaBarrierAddress != 0ULL) {
    /* D3D-G1b: the TA queue leads with the firmware Barrier on the compute
     * stamp. It lives in the compute sidecar, whose prepared range the
     * compute publication flushes; the same range covers its ring entry. */
    const APPLE_AGX_BACKEND_BOOL prepared =
        submission.Ta.PreparedRangeCount != 0u ? APPLE_AGX_BACKEND_TRUE
                                               : APPLE_AGX_BACKEND_FALSE;
    const APPLE_AGX_BACKEND_U32 work = Successor ? 1u : 0u;
    if (Job->ComputeWorkAddressCount != 1u ||
        submission.Compute.GpuAddressCount != 1u + work ||
        (prepared && (submission.Compute.PreparedRangeCount != 1u + work ||
                      submission.Ta.PreparedRangeCount !=
                          submission.Ta.GpuAddressCount)) ||
        submission.Ta.GpuAddressCount >= APPLE_AGX_G13_QUEUE_PUBLISH_MAX)
      SUBMIT_REJECT(AppleAgxG13SubmitGuardBuild);
    for (index = submission.Ta.GpuAddressCount; index > 0u; --index) {
      submission.Ta.GpuAddresses[index] = submission.Ta.GpuAddresses[index - 1u];
      if (prepared)
        submission.Ta.PreparedRanges[index] =
            submission.Ta.PreparedRanges[index - 1u];
    }
    submission.Ta.GpuAddresses[0] = Job->TaBarrierAddress;
    ++submission.Ta.GpuAddressCount;
    if (prepared) {
      submission.Ta.PreparedRanges[0].Address =
          submission.Compute.PreparedRanges[work].Address;
      submission.Ta.PreparedRanges[0].Bytes =
          submission.Compute.PreparedRanges[work].Bytes;
      ++submission.Ta.PreparedRangeCount;
    }
    submission.TaLeadingBarrier = APPLE_AGX_BACKEND_TRUE;
    result = AppleAgxG13QueueRuntimeSubmitRenderWithCompute(&provider->Runtime,
                                                           &submission);
  } else if (submission.Compute.GpuAddressCount != 0u) {
    SUBMIT_REJECT(AppleAgxG13SubmitGuardBuild);
  } else {
    result = AppleAgxG13QueueRuntimeSubmit(&provider->Runtime, &submission);
  }
  if (result != AppleAgxG13QueueRuntimeResultOk) {
    provider->LastSubmitGuard=AppleAgxG13SubmitGuardRuntime;
    provider->LastSubmitRuntimeResult=(APPLE_AGX_BACKEND_U32)result;
    if (provider->Runtime.Phase == AppleAgxG13QueueRuntimeFaulted) {
      provider->FailureQuiesced =
          result == AppleAgxG13QueueRuntimeResultResetFailed
              ? APPLE_AGX_BACKEND_FALSE
              : APPLE_AGX_BACKEND_TRUE;
      provider->Phase = AppleAgxG13QueueProviderFaulted;
    }
    return APPLE_AGX_BACKEND_FALSE;
  }
  provider->LastSubmitGuard=AppleAgxG13SubmitGuardNone;
  if (Successor) {
    provider->SuccessorFence = Fence;
    provider->SuccessorComputeOnly = APPLE_AGX_BACKEND_FALSE;
    return APPLE_AGX_BACKEND_TRUE;
  }
  provider->ObservedMask = 0u;
  provider->Phase = AppleAgxG13QueueProviderSubmitted;
  return APPLE_AGX_BACKEND_TRUE;
#undef SUBMIT_REJECT
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderRunTa(
    void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
    APPLE_AGX_BACKEND_U32 Fence) {
  APPLE_AGX_G13_QUEUE_PROVIDER *provider =
      (APPLE_AGX_G13_QUEUE_PROVIDER *)Context;
  if (provider == APPLE_AGX_G13_PROVIDER_NULL) return APPLE_AGX_BACKEND_FALSE;
  provider->LastSubmitGuard=AppleAgxG13SubmitGuardNone;
  provider->LastSubmitRuntimeResult=0u;
  AppleAgxG13ProviderZero(
      &provider->ComputeIdentityDiagnostic.RunTa,
      (APPLE_AGX_BACKEND_U32)sizeof(provider->ComputeIdentityDiagnostic.RunTa));
#define SUBMIT_REJECT(g) do { provider->LastSubmitGuard=(g); return APPLE_AGX_BACKEND_FALSE; } while(0)
  if (Job == APPLE_AGX_G13_PROVIDER_NULL || Job->LeadBarrierAddress != 0ULL)
    SUBMIT_REJECT(AppleAgxG13SubmitGuardArguments);
  if (provider->Phase != AppleAgxG13QueueProvider3dStaged) {
    provider->LastSubmitRuntimeResult = AppleAgxG13PhaseFenceGuardPhase;
    SUBMIT_REJECT(AppleAgxG13SubmitGuardPhaseFence);
  }
  if (Fence != provider->PendingFence) {
    provider->LastSubmitRuntimeResult = AppleAgxG13PhaseFenceGuardFence;
    SUBMIT_REJECT(AppleAgxG13SubmitGuardPhaseFence);
  }
  if (!AppleAgxG13Provider3dMatches(provider, Job)) SUBMIT_REJECT(AppleAgxG13SubmitGuardD3Match);
#undef SUBMIT_REJECT
  return AppleAgxG13ProviderSubmitRender(provider, Context, Job, Fence,
                                         APPLE_AGX_BACKEND_FALSE);
}

/* G2: submit a standalone compute job (Kind Compute): one RunCompute work
 * item on the compute queue whose completion is the fence's completion.
 * Accepted only while the queues are idle (no staged or running render). */
/* G2 (and rc102's compute successor): one RunCompute work item on the
 * compute queue whose completion is the fence's completion; a successor's
 * leading Barrier is the entry before it. */
static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderSubmitCompute(
    APPLE_AGX_G13_QUEUE_PROVIDER *provider,
    const APPLE_AGX_BACKEND_JOB_IMAGE *Job, APPLE_AGX_BACKEND_U32 Fence,
    APPLE_AGX_BACKEND_BOOL Successor) {
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION submission;
  APPLE_AGX_G13_QUEUE_RUNTIME_RESULT result;
#define SUBMIT_REJECT(g) do { provider->LastSubmitGuard=(g); return APPLE_AGX_BACKEND_FALSE; } while(0)
  if (!provider->Config.HasCompute || Job->ComputeWorkAddressCount != 1u ||
      Job->ComputeWorkAddresses[0] == 0ULL ||
      Job->ComputeEvent != provider->Config.Compute.EventNumber ||
      Job->ComputeExpectedStamp == 0u ||
      (Successor ? Job->LeadBarrierAddress == 0ULL ||
                       (Job->LeadBarrierAddress & 0x1fULL) != 0ULL
                 : Job->LeadBarrierAddress != 0ULL) ||
      !AppleAgxG13ProviderExpectedDone(provider, &provider->Config.Compute,
                                       Successor ? 2u : 1u,
                                       Job->ComputeExpectedDonePointer))
    SUBMIT_REJECT(AppleAgxG13SubmitGuardCompute);
  AppleAgxG13ProviderZero(&submission,
                          (APPLE_AGX_BACKEND_U32)sizeof(submission));
  if (!provider->ProviderIo.BuildSubmission(
          provider->ProviderIo.Context, Job, Fence, &submission) ||
      submission.Compute.GpuAddressCount != 1u ||
      submission.Compute.GpuAddresses[0] != Job->ComputeWorkAddresses[0] ||
      submission.Ta.GpuAddressCount != 0u || submission.D3.GpuAddressCount != 0u)
    SUBMIT_REJECT(AppleAgxG13SubmitGuardBuild);
  if (Successor) {
    const APPLE_AGX_BACKEND_BOOL prepared =
        submission.Compute.PreparedRangeCount != 0u ? APPLE_AGX_BACKEND_TRUE
                                                    : APPLE_AGX_BACKEND_FALSE;
    if (prepared && (submission.Compute.PreparedRangeCount != 1u ||
                     submission.LeadBarrierRange.Address ==
                         APPLE_AGX_G13_PROVIDER_NULL ||
                     submission.LeadBarrierRange.Bytes == 0u))
      SUBMIT_REJECT(AppleAgxG13SubmitGuardBuild);
    submission.Compute.GpuAddresses[1] = submission.Compute.GpuAddresses[0];
    submission.Compute.GpuAddresses[0] = Job->LeadBarrierAddress;
    submission.Compute.GpuAddressCount = 2u;
    if (prepared) {
      submission.Compute.PreparedRanges[1] = submission.Compute.PreparedRanges[0];
      submission.Compute.PreparedRanges[0].Address =
          submission.LeadBarrierRange.Address;
      submission.Compute.PreparedRanges[0].Bytes =
          submission.LeadBarrierRange.Bytes;
      submission.Compute.PreparedRangeCount = 2u;
    }
    submission.Successor = APPLE_AGX_BACKEND_TRUE;
  }
  submission.Fence = Fence;
  submission.ComputeOnly = APPLE_AGX_BACKEND_TRUE;
  result = AppleAgxG13QueueRuntimeSubmitCompute(&provider->Runtime, &submission);
  if (result != AppleAgxG13QueueRuntimeResultOk) {
    provider->LastSubmitGuard=AppleAgxG13SubmitGuardRuntime;
    provider->LastSubmitRuntimeResult=(APPLE_AGX_BACKEND_U32)result;
    if (provider->Runtime.Phase == AppleAgxG13QueueRuntimeFaulted) {
      /* The work became visible: the fence is owned by the queue now. */
      if (!Successor) provider->PendingFence = Fence;
      provider->FailureQuiesced =
          result == AppleAgxG13QueueRuntimeResultResetFailed
              ? APPLE_AGX_BACKEND_FALSE : APPLE_AGX_BACKEND_TRUE;
      provider->Phase = AppleAgxG13QueueProviderFaulted;
    }
    return APPLE_AGX_BACKEND_FALSE;
  }
  if (Successor) {
    provider->SuccessorFence = Fence;
    provider->SuccessorComputeOnly = APPLE_AGX_BACKEND_TRUE;
    return APPLE_AGX_BACKEND_TRUE;
  }
  provider->PendingFence = Fence;
  provider->ObservedMask = 0u;
  provider->Phase = AppleAgxG13QueueProviderComputeOnlySubmitted;
  return APPLE_AGX_BACKEND_TRUE;
#undef SUBMIT_REJECT
}

/* G2: submit a standalone compute job (Kind Compute). Accepted only while
 * the queues are idle (no staged or running render). */
static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderRunCompute(
    void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
    APPLE_AGX_BACKEND_U32 Fence) {
  APPLE_AGX_G13_QUEUE_PROVIDER *provider =
      (APPLE_AGX_G13_QUEUE_PROVIDER *)Context;
  if (provider == APPLE_AGX_G13_PROVIDER_NULL) return APPLE_AGX_BACKEND_FALSE;
  provider->LastSubmitGuard=AppleAgxG13SubmitGuardNone;
  provider->LastSubmitRuntimeResult=0u;
#define SUBMIT_REJECT(g) do { provider->LastSubmitGuard=(g); return APPLE_AGX_BACKEND_FALSE; } while(0)
  if (Job == APPLE_AGX_G13_PROVIDER_NULL || Fence == 0u ||
      Job->Kind != AppleAgxBackendJobCompute)
    SUBMIT_REJECT(AppleAgxG13SubmitGuardArguments);
  if (provider->Phase != AppleAgxG13QueueProviderCreated ||
      provider->PendingFence != 0u) {
    provider->LastSubmitRuntimeResult = AppleAgxG13PhaseFenceGuardPhase;
    SUBMIT_REJECT(AppleAgxG13SubmitGuardPhaseFence);
  }
#undef SUBMIT_REJECT
  return AppleAgxG13ProviderSubmitCompute(provider, Job, Fence,
                                          APPLE_AGX_BACKEND_FALSE);
}

/* rc102 (depth 2): kick Job behind the running job, in one admission. The
 * running job keeps PendingFence and the provider phase; the successor
 * runs once its leading Barrier sees the running job's last stamp. */
static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderRunSuccessor(
    void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
    APPLE_AGX_BACKEND_U32 Fence) {
  APPLE_AGX_G13_QUEUE_PROVIDER *provider =
      (APPLE_AGX_G13_QUEUE_PROVIDER *)Context;
  APPLE_AGX_BACKEND_BOOL submitted;
  if (provider == APPLE_AGX_G13_PROVIDER_NULL) return APPLE_AGX_BACKEND_FALSE;
  provider->LastSubmitGuard = AppleAgxG13SubmitGuardNone;
  provider->LastSubmitRuntimeResult = 0u;
  if (Job == APPLE_AGX_G13_PROVIDER_NULL || Fence == 0u ||
      Fence == provider->PendingFence || provider->PendingFence == 0u ||
      provider->SuccessorFence != 0u ||
      (provider->Phase != AppleAgxG13QueueProviderSubmitted &&
       provider->Phase != AppleAgxG13QueueProviderComputeOnlySubmitted) ||
      provider->Runtime.PendingFence != provider->PendingFence) {
    provider->LastSubmitGuard = AppleAgxG13SubmitGuardPhaseFence;
    provider->LastSubmitRuntimeResult = AppleAgxG13PhaseFenceGuardPhase;
    provider->LastSuccessorGuard = AppleAgxG13SubmitGuardPhaseFence;
    return APPLE_AGX_BACKEND_FALSE;
  }
  submitted = Job->Kind == AppleAgxBackendJobCompute
      ? AppleAgxG13ProviderSubmitCompute(provider, Job, Fence,
                                         APPLE_AGX_BACKEND_TRUE)
      : Job->Kind == AppleAgxBackendJobRender &&
            AppleAgxG13ProviderSubmitRender(provider, Context, Job, Fence,
                                            APPLE_AGX_BACKEND_TRUE)
            ? APPLE_AGX_BACKEND_TRUE : APPLE_AGX_BACKEND_FALSE;
  provider->LastSuccessorGuard = provider->LastSubmitGuard;
  return submitted;
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
  if (provider->Phase != AppleAgxG13QueueProviderSubmitted &&
      provider->Phase != AppleAgxG13QueueProviderComputeSubmitted &&
      provider->Phase != AppleAgxG13QueueProviderComputeOnlySubmitted)
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
      provider->Phase != AppleAgxG13QueueProviderComputeSubmitted &&
      provider->Phase != AppleAgxG13QueueProviderComputeOnlySubmitted &&
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
  Io->Queues.RunCompute = AppleAgxG13ProviderRunCompute;
  Io->Queues.Stop = AppleAgxG13ProviderStop;
  Io->Queues.Reset = AppleAgxG13ProviderReset;
  Io->Queues.RunSuccessor = AppleAgxG13ProviderRunSuccessor;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderAppendObservation(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch,
    APPLE_AGX_BACKEND_QUEUE Queue,
    const APPLE_AGX_G13_QUEUE_BINDING *Binding,
    const APPLE_AGX_G13_QUEUE_PENDING *Pending) {
  APPLE_AGX_BACKEND_OBSERVATION *observation;
  APPLE_AGX_BACKEND_U32 done;
  if (Batch->ObservationCount >= APPLE_AGX_G13_QUEUE_PROVIDER_MAX_OBSERVATIONS)
    return APPLE_AGX_BACKEND_FALSE;
  observation = &Batch->Observations[Batch->ObservationCount++];
  if (!Provider->RuntimeIo.ReadU32(Provider->RuntimeIo.Context,
                                   Binding->Stamp, &observation->Stamp) ||
      !Provider->RuntimeIo.ReadU32(Provider->RuntimeIo.Context,
                                   Binding->GpuDonePointer, &done)) {
    --Batch->ObservationCount;
    return APPLE_AGX_BACKEND_FALSE;
  }
  observation->Queue = Queue;
  observation->Event = Pending->EventNumber;
  /* rc102: the job's own retirement point. The runtime proved the done
   * pointer at or past it and not past what was published; with one job
   * in flight both are the same value, behind a successor the live
   * pointer may already be the successor's. */
  observation->DonePointer = Pending->ExpectedDonePointer;
  observation->Status = AppleAgxBackendObservationComplete;
  return APPLE_AGX_BACKEND_TRUE;
}

/* The running job's completed queues not reported yet (3D, TA, then a
 * standalone compute job's queue). All = every queue of the job whatever
 * its record says (a successor a recovery found finished). */
static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderAppendRunning(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch, APPLE_AGX_BACKEND_BOOL All) {
  APPLE_AGX_G13_QUEUE_RUNTIME *runtime = &Provider->Runtime;
  const APPLE_AGX_BACKEND_U32 d3 = 1u << AppleAgxBackendQueue3d;
  const APPLE_AGX_BACKEND_U32 ta = 1u << AppleAgxBackendQueueTa;
  const APPLE_AGX_BACKEND_U32 compute = 1u << AppleAgxBackendQueueCompute;
  if (Provider->Phase == AppleAgxG13QueueProviderComputeOnlySubmitted) {
    if ((Provider->ObservedMask & compute) == 0u &&
        (All || runtime->ComputePending.Complete)) {
      if (!AppleAgxG13ProviderAppendObservation(
              Provider, Batch, AppleAgxBackendQueueCompute,
              &Provider->Config.Compute, &runtime->ComputePending)) {
        Provider->LastIngestGuard =
            AppleAgxG13QueueProviderIngestGuardComputeObservation;
        Provider->Phase = AppleAgxG13QueueProviderFaulted;
        return APPLE_AGX_BACKEND_FALSE;
      }
      Provider->ObservedMask |= compute;
    }
    return APPLE_AGX_BACKEND_TRUE;
  }
  if ((Provider->ObservedMask & d3) == 0u &&
      (All || runtime->D3Pending.Complete)) {
    if (!AppleAgxG13ProviderAppendObservation(
            Provider, Batch, AppleAgxBackendQueue3d, &Provider->Config.D3,
            &runtime->D3Pending)) {
      Provider->LastIngestGuard =
          AppleAgxG13QueueProviderIngestGuardD3Observation;
      Provider->Phase = AppleAgxG13QueueProviderFaulted;
      return APPLE_AGX_BACKEND_FALSE;
    }
    Provider->ObservedMask |= d3;
  }
  if ((Provider->ObservedMask & ta) == 0u &&
      (All || runtime->TaPending.Complete)) {
    if (!AppleAgxG13ProviderAppendObservation(
            Provider, Batch, AppleAgxBackendQueueTa, &Provider->Config.Ta,
            &runtime->TaPending)) {
      Provider->LastIngestGuard =
          AppleAgxG13QueueProviderIngestGuardTaObservation;
      Provider->Phase = AppleAgxG13QueueProviderFaulted;
      return APPLE_AGX_BACKEND_FALSE;
    }
    Provider->ObservedMask |= ta;
  }
  return APPLE_AGX_BACKEND_TRUE;
}

static void AppleAgxG13ProviderAppendStatus(
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch,
    APPLE_AGX_BACKEND_OBSERVATION_STATUS Status, APPLE_AGX_BACKEND_BOOL Reset) {
  if (Reset) Batch->ObservationCount = 0u;
  AppleAgxG13ProviderZero(&Batch->Observations[Batch->ObservationCount],
      (APPLE_AGX_BACKEND_U32)sizeof(Batch->Observations[0]));
  Batch->Observations[Batch->ObservationCount++].Status = Status;
}

/* Takes the running job's completion when one is available. A successful
 * one first reports every queue of the job not reported yet: those a
 * recovery found finished, a batched successor's (rc102). FALSE: none
 * available, or (provider Faulted) an observation could not be read. */
static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderTake(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch,
    APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION *Completion) {
  if (!Provider->Runtime.CompletionAvailable)
    return APPLE_AGX_BACKEND_FALSE;
  if (Provider->Runtime.Completion.Status == AppleAgxG13QueueCompletionSuccess &&
      !AppleAgxG13ProviderAppendRunning(Provider, Batch, APPLE_AGX_BACKEND_TRUE))
    return APPLE_AGX_BACKEND_FALSE;
  return AppleAgxG13QueueRuntimeTakeCompletion(&Provider->Runtime, Completion);
}

/* A completion of the running job was taken. Success and Recovered end it
 * (the provider then accepts the next job, or rc102: its kicked successor
 * becomes the running job and, when it already ended, is reported in the
 * same batch right behind it, oldest first). */
static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderEndRunning(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch,
    const APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION *Completion,
    APPLE_AGX_BACKEND_BOOL FirstOfBatch) {
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION next;
  if (Completion->Fence != Provider->PendingFence) {
    Provider->LastIngestGuard =
        AppleAgxG13QueueProviderIngestGuardCompletionFence;
    return APPLE_AGX_BACKEND_FALSE;
  }
  Batch->CompletedFence = Completion->Fence;
  ++Batch->CompletedCount;
  if (Completion->Status == AppleAgxG13QueueCompletionRecovered) {
    /* Firmware retirement after a resumed Timeout/Fault is proven; the
     * job ends as dropped and the provider accepts the next submission. */
    AppleAgxG13ProviderAppendStatus(Batch, AppleAgxBackendObservationRecovered,
                                    FirstOfBatch);
  } else if (Completion->Status != AppleAgxG13QueueCompletionSuccess) {
    AppleAgxG13ProviderAppendStatus(Batch, AppleAgxBackendObservationFault,
                                    FirstOfBatch);
    Provider->SuccessorFence = 0u;
    Provider->FailureQuiesced = APPLE_AGX_BACKEND_TRUE;
    Provider->Phase = AppleAgxG13QueueProviderFaulted;
    Provider->LastIngestGuard = AppleAgxG13QueueProviderIngestGuardOk;
    return APPLE_AGX_BACKEND_TRUE;
  }
  if (Provider->SuccessorFence == 0u ||
      Provider->Runtime.PendingFence != Provider->SuccessorFence) {
    AppleAgxG13ProviderClearStaged(Provider);
    Provider->Phase = AppleAgxG13QueueProviderCreated;
    Provider->LastIngestGuard = AppleAgxG13QueueProviderIngestGuardOk;
    return APPLE_AGX_BACKEND_TRUE;
  }
  /* rc102: promoted. */
  AppleAgxG13ProviderZero(&Provider->Staged3d,
                          (APPLE_AGX_BACKEND_U32)sizeof(Provider->Staged3d));
  Provider->PendingFence = Provider->SuccessorFence;
  Provider->Phase = Provider->SuccessorComputeOnly
                        ? AppleAgxG13QueueProviderComputeOnlySubmitted
                        : AppleAgxG13QueueProviderSubmitted;
  Provider->SuccessorFence = 0u;
  Provider->SuccessorComputeOnly = APPLE_AGX_BACKEND_FALSE;
  Provider->ObservedMask = 0u;
  Provider->LastIngestGuard = AppleAgxG13QueueProviderIngestGuardOk;
  if (!AppleAgxG13ProviderTake(Provider, Batch, &next))
    return Provider->Phase != AppleAgxG13QueueProviderFaulted
               ? APPLE_AGX_BACKEND_TRUE : APPLE_AGX_BACKEND_FALSE;
  return AppleAgxG13ProviderEndRunning(Provider, Batch, &next,
                                       APPLE_AGX_BACKEND_FALSE);
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderIngestEvent(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider, const unsigned char *Message,
    APPLE_AGX_BACKEND_U32 MessageBytes,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch) {
  APPLE_AGX_G13_QUEUE_RUNTIME_RESULT result;
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION completion;
  if (Provider != APPLE_AGX_G13_PROVIDER_NULL) {
    Provider->LastIngestGuard =
        AppleAgxG13QueueProviderIngestGuardInvalid;
    Provider->LastIngestRuntimeResult =
        (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueRuntimeResultInvalidArgument;
  }
  if (Provider == APPLE_AGX_G13_PROVIDER_NULL ||
      Message == APPLE_AGX_G13_PROVIDER_NULL ||
      Batch == APPLE_AGX_G13_PROVIDER_NULL)
    return APPLE_AGX_BACKEND_FALSE;
  AppleAgxG13ProviderZero(Batch, (APPLE_AGX_BACKEND_U32)sizeof(*Batch));
  if (Provider->Phase == AppleAgxG13QueueProviderCreated) {
    APPLE_AGX_G13_EVENT event;
    APPLE_AGX_BACKEND_U32 number;
    /* Flags signal queue progress, not a particular job. Shared stamps may
     * retire both renders on the first of two already queued notifications.
     * Asahi workqueue::signal likewise permits no pending commands. Drain
     * the remaining owned Flag without manufacturing another completion;
     * faults, handler messages and foreign slots still require diagnosis. */
    if (Provider->PendingFence != 0u || Provider->SuccessorFence != 0u ||
        Provider->Runtime.Phase != AppleAgxG13QueueRuntimeReady ||
        Provider->Runtime.PendingFence != 0u ||
        Provider->Runtime.Successor.Fence != 0u ||
        Provider->Runtime.CompletionAvailable ||
        !AppleAgxG13DecodeEvent(Message, MessageBytes, &event) ||
        event.Kind != (APPLE_AGX_BACKEND_U32)AppleAgxG13EventFlag)
      return APPLE_AGX_BACKEND_FALSE;
    for (number = 0u; number < APPLE_AGX_G13_EVENT_COUNT; ++number) {
      if (AppleAgxG13EventHasNumber(&event, number) &&
          number != Provider->Config.Ta.EventNumber &&
          number != Provider->Config.D3.EventNumber &&
          (!Provider->Config.HasCompute ||
           number != Provider->Config.Compute.EventNumber))
        return APPLE_AGX_BACKEND_FALSE;
    }
    Provider->LastIngestGuard = AppleAgxG13QueueProviderIngestGuardOk;
    Provider->LastIngestRuntimeResult =
        (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueRuntimeResultOk;
    return APPLE_AGX_BACKEND_TRUE;
  }
  if (Provider->Phase != AppleAgxG13QueueProviderSubmitted &&
      Provider->Phase != AppleAgxG13QueueProviderComputeSubmitted &&
      Provider->Phase != AppleAgxG13QueueProviderComputeOnlySubmitted)
    return APPLE_AGX_BACKEND_FALSE;
  result = AppleAgxG13QueueRuntimeHandleEvent(&Provider->Runtime, Message,
                                              MessageBytes);
  Provider->LastIngestGuard = AppleAgxG13QueueProviderIngestGuardRuntime;
  Provider->LastIngestRuntimeResult = (APPLE_AGX_BACKEND_U32)result;
  if (result == AppleAgxG13QueueRuntimeResultInvalidArgument ||
      result == AppleAgxG13QueueRuntimeResultMemoryFailed ||
      result == AppleAgxG13QueueRuntimeResultInvalidState ||
      result == AppleAgxG13QueueRuntimeResultResetFailed) {
    if (result == AppleAgxG13QueueRuntimeResultResetFailed)
      Provider->Phase = AppleAgxG13QueueProviderFaulted;
    return APPLE_AGX_BACKEND_FALSE;
  }
  /* The running job's queues that completed with this event; a standalone
   * compute job (G2) reports its compute queue. */
  if (!AppleAgxG13ProviderAppendRunning(Provider, Batch, APPLE_AGX_BACKEND_FALSE))
    return APPLE_AGX_BACKEND_FALSE;
  if (!AppleAgxG13ProviderTake(Provider, Batch, &completion)) {
    if (Provider->Phase == AppleAgxG13QueueProviderFaulted)
      return APPLE_AGX_BACKEND_FALSE;
    Provider->LastIngestGuard = AppleAgxG13QueueProviderIngestGuardOk;
    return APPLE_AGX_BACKEND_TRUE;
  }
  return AppleAgxG13ProviderEndRunning(Provider, Batch, &completion,
                                       APPLE_AGX_BACKEND_TRUE);
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderCheckTimeout(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_BACKEND_U64 NowTicks,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch) {
  APPLE_AGX_G13_QUEUE_RUNTIME_RESULT result;
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION completion;

  if (Provider == APPLE_AGX_G13_PROVIDER_NULL || NowTicks == 0ULL ||
      Batch == APPLE_AGX_G13_PROVIDER_NULL ||
      (Provider->Phase != AppleAgxG13QueueProviderSubmitted &&
       Provider->Phase != AppleAgxG13QueueProviderComputeSubmitted &&
       Provider->Phase != AppleAgxG13QueueProviderComputeOnlySubmitted))
    return APPLE_AGX_BACKEND_FALSE;
  AppleAgxG13ProviderZero(Batch, (APPLE_AGX_BACKEND_U32)sizeof(*Batch));
  result = AppleAgxG13QueueRuntimeCheckTimeout(&Provider->Runtime, NowTicks);
  if (result == AppleAgxG13QueueRuntimeResultOk) {
    /* A recovery retired the job (rc102: behind a successor it may have
     * ended with all its stamps: Success). */
    if (!AppleAgxG13ProviderTake(Provider, Batch, &completion))
      return Provider->Phase != AppleAgxG13QueueProviderFaulted
                 ? APPLE_AGX_BACKEND_TRUE : APPLE_AGX_BACKEND_FALSE;
    if (completion.Fence != Provider->PendingFence ||
        (completion.Status != AppleAgxG13QueueCompletionRecovered &&
         completion.Status != AppleAgxG13QueueCompletionSuccess))
      return APPLE_AGX_BACKEND_FALSE;
    return AppleAgxG13ProviderEndRunning(Provider, Batch, &completion,
                                         APPLE_AGX_BACKEND_TRUE);
  }
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
  Batch->CompletedCount = 1u;
  Provider->FailureQuiesced = APPLE_AGX_BACKEND_TRUE;
  Provider->Phase = AppleAgxG13QueueProviderFaulted;
  return APPLE_AGX_BACKEND_TRUE;
}
