#include "apple_agx_scheduler.h"

#define APPLE_AGX_NULL ((void *)0)

static APPLE_AGX_BOOL AppleAgxSchedulerFenceAfter(
    APPLE_AGX_U32 Candidate, APPLE_AGX_U32 Reference) {
  APPLE_AGX_U32 distance = Candidate - Reference;
  return distance != 0u && distance < 0x80000000u ? APPLE_AGX_TRUE
                                                   : APPLE_AGX_FALSE;
}

static APPLE_AGX_BOOL AppleAgxSchedulerFenceAtOrAfter(
    APPLE_AGX_U32 Candidate, APPLE_AGX_U32 Reference) {
  return Candidate == Reference ||
                 AppleAgxSchedulerFenceAfter(Candidate, Reference)
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

static APPLE_AGX_U32 AppleAgxSchedulerRetiredFence(
    const APPLE_AGX_SCHEDULER *Scheduler) {
  return Scheduler->LastAbortedFence != 0u &&
                 AppleAgxSchedulerFenceAfter(Scheduler->LastAbortedFence,
                                             Scheduler->CompletedFence)
             ? Scheduler->LastAbortedFence : Scheduler->CompletedFence;
}

void AppleAgxSchedulerInitialize(APPLE_AGX_SCHEDULER *Scheduler) {
  APPLE_AGX_U32 index;
  if (Scheduler == APPLE_AGX_NULL)
    return;
  Scheduler->CompletedFence = 0;
  Scheduler->LastAbortedFence = 0;
  Scheduler->LastSubmittedFence = 0;
  Scheduler->QueuedFence = 0;
  Scheduler->QueueHead = Scheduler->QueueCount = 0u;
  Scheduler->PreemptedFenceCount = 0u;
  for (index = 0u; index < APPLE_AGX_SCHEDULER_QUEUE_CAPACITY; ++index) {
    Scheduler->FenceQueue[index] = 0u;
    Scheduler->RejectedAfter[index] = 0u;
    Scheduler->PreemptedRejectedAfter[index] = 0u;
  }
  Scheduler->ActiveFence = 0;
  Scheduler->ActiveRejectedAfter = 0;
  Scheduler->PendingPreemptionFence = 0;
  Scheduler->PreemptionCutoffFence = 0;
  Scheduler->PreemptionActiveFence = 0;
  Scheduler->PreemptionLastCompletedFence = 0;
  Scheduler->ContextCount = 0;
  Scheduler->PreemptionPending = APPLE_AGX_FALSE;
  Scheduler->Responsive = APPLE_AGX_TRUE;
  Scheduler->PreemptionPhase = AppleAgxPreemptionIdle;
}

void AppleAgxSchedulerContextInitialize(
    APPLE_AGX_SCHEDULER_CONTEXT *Context) {
  if (Context == APPLE_AGX_NULL)
    return;
  Context->NodeOrdinal = 0;
  Context->EngineAffinity = 0;
  Context->Active = APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxSchedulerValidateEngine(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal) {
  if (Scheduler == APPLE_AGX_NULL ||
      NodeOrdinal >= APPLE_AGX_SCHEDULER_NODE_COUNT ||
      EngineOrdinal >= APPLE_AGX_SCHEDULER_ENGINE_COUNT)
    return APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerEngineResponsive(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal))
    return APPLE_AGX_FALSE;
  return Scheduler->Responsive;
}

APPLE_AGX_BOOL AppleAgxSchedulerCreateContext(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_SCHEDULER_CONTEXT *Context,
    APPLE_AGX_U32 NodeOrdinal, APPLE_AGX_U32 EngineAffinity) {
  if (Scheduler == APPLE_AGX_NULL || Context == APPLE_AGX_NULL ||
      Context->Active || NodeOrdinal != 0 ||
      EngineAffinity != APPLE_AGX_SCHEDULER_ENGINE_AFFINITY)
    return APPLE_AGX_FALSE;
  Context->NodeOrdinal = NodeOrdinal;
  Context->EngineAffinity = EngineAffinity;
  Context->Active = APPLE_AGX_TRUE;
  ++Scheduler->ContextCount;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerDestroyContext(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_SCHEDULER_CONTEXT *Context) {
  if (Scheduler == APPLE_AGX_NULL || Context == APPLE_AGX_NULL ||
      !Context->Active || Scheduler->ContextCount == 0)
    return APPLE_AGX_FALSE;
  Context->NodeOrdinal = 0;
  Context->EngineAffinity = 0;
  Context->Active = APPLE_AGX_FALSE;
  --Scheduler->ContextCount;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerCompleteFence(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence) {
  if (!AppleAgxSchedulerCanCompleteFence(Scheduler, NodeOrdinal,
                                         EngineOrdinal, Fence))
    return APPLE_AGX_FALSE;
  Scheduler->CompletedFence = Fence;
  /* A newer successful boundary supersedes the abort for reset purposes.
   * Do not compare an ancient abort across half of the fence-number space. */
  Scheduler->LastAbortedFence = 0u;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerCanCompleteFence(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence) {
  APPLE_AGX_U32 distance;

  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal) ||
      Fence == 0u)
    return APPLE_AGX_FALSE;
  if (Scheduler->PreemptionPhase ==
          AppleAgxPreemptionWaitCurrentBoundary &&
      Fence != Scheduler->PreemptionActiveFence)
    return APPLE_AGX_FALSE;
  distance = Fence - AppleAgxSchedulerRetiredFence(Scheduler);
  return distance != 0u && distance < 0x80000000u ? APPLE_AGX_TRUE
                                                   : APPLE_AGX_FALSE;
}

APPLE_AGX_U32 AppleAgxSchedulerCurrentFence(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal))
    return 0;
  return Scheduler->CompletedFence;
}

/* Windows may queue new work before its preemption DPC returns. Admission
 * may proceed during delivery, but activation still waits for Commit. */
static APPLE_AGX_BOOL AppleAgxSchedulerAdmissionBlocked(
    const APPLE_AGX_SCHEDULER *Scheduler) {
  return Scheduler != APPLE_AGX_NULL &&
      Scheduler->PreemptionPhase != AppleAgxPreemptionIdle &&
      Scheduler->PreemptionPhase != AppleAgxPreemptionNotificationClaimed
          ? APPLE_AGX_TRUE : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxSchedulerQueueFence(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal) ||
      Fence == 0u || Scheduler->QueueCount >= APPLE_AGX_SCHEDULER_QUEUE_CAPACITY ||
      AppleAgxSchedulerAdmissionBlocked(Scheduler) ||
      !AppleAgxSchedulerFenceAfter(Fence, Scheduler->LastSubmittedFence))
    return APPLE_AGX_FALSE;
  Scheduler->FenceQueue[(Scheduler->QueueHead + Scheduler->QueueCount) %
      APPLE_AGX_SCHEDULER_QUEUE_CAPACITY] = Fence;
  Scheduler->RejectedAfter[(Scheduler->QueueHead + Scheduler->QueueCount) %
      APPLE_AGX_SCHEDULER_QUEUE_CAPACITY] = 0u;
  if (Scheduler->QueueCount == 0u)
    Scheduler->QueuedFence = Fence;
  ++Scheduler->QueueCount;
  Scheduler->LastSubmittedFence = Fence;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerRejectFence(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence) {
  APPLE_AGX_U32 index, predecessor, through = Fence;
  APPLE_AGX_U32 *tail = APPLE_AGX_NULL;
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal) ||
      !Fence)
    return APPLE_AGX_FALSE;
  if (!AppleAgxSchedulerFenceAfter(Fence, Scheduler->CompletedFence))
    return APPLE_AGX_TRUE; /* Duplicate/already covered: never regress. */
  predecessor = AppleAgxSchedulerRetiredFence(Scheduler);
  if (Scheduler->ActiveFence == Fence) return APPLE_AGX_FALSE;
  for (index = 0u; index < Scheduler->QueueCount; ++index)
    if (Scheduler->FenceQueue[(Scheduler->QueueHead + index) %
          APPLE_AGX_SCHEDULER_QUEUE_CAPACITY] == Fence)
      return APPLE_AGX_FALSE; /* Never discard accepted work still owned. */
  /* An old paging resubmission can itself be rejected. Remove only that
   * never-executed entry and carry its rejected successors with it. */
  for (index = 0u; index < Scheduler->PreemptedFenceCount; ++index) {
    if (Scheduler->PreemptedFenceQueue[index] != Fence) continue;
    if (Scheduler->PreemptedRejectedAfter[index])
      through = Scheduler->PreemptedRejectedAfter[index];
    for (; index + 1u < Scheduler->PreemptedFenceCount; ++index) {
      Scheduler->PreemptedFenceQueue[index] = Scheduler->PreemptedFenceQueue[index+1u];
      Scheduler->PreemptedRejectedAfter[index] = Scheduler->PreemptedRejectedAfter[index+1u];
    }
    --Scheduler->PreemptedFenceCount;
    break;
  }
#define REJECT_PREDECESSOR(candidate, marker) do { \
    APPLE_AGX_U32 prior = (candidate); \
    if (prior && AppleAgxSchedulerFenceAfter(Fence, prior) && \
        AppleAgxSchedulerFenceAfter(prior, predecessor)) { \
      predecessor = prior; tail = &(marker); \
    } \
  } while (0)
  REJECT_PREDECESSOR(Scheduler->ActiveFence, Scheduler->ActiveRejectedAfter);
  for (index = 0u; index < Scheduler->QueueCount; ++index) {
    APPLE_AGX_U32 slot = (Scheduler->QueueHead + index) % APPLE_AGX_SCHEDULER_QUEUE_CAPACITY;
    REJECT_PREDECESSOR(Scheduler->FenceQueue[slot], Scheduler->RejectedAfter[slot]);
  }
  for (index = 0u; index < Scheduler->PreemptedFenceCount; ++index)
    REJECT_PREDECESSOR(Scheduler->PreemptedFenceQueue[index],
                       Scheduler->PreemptedRejectedAfter[index]);
#undef REJECT_PREDECESSOR
  if (tail) {
    if (!*tail || AppleAgxSchedulerFenceAfter(through, *tail)) *tail = through;
  } else if (AppleAgxSchedulerFenceAfter(through, Scheduler->CompletedFence)) {
    Scheduler->CompletedFence = through;
  }
  if (AppleAgxSchedulerFenceAfter(through, Scheduler->LastSubmittedFence))
    Scheduler->LastSubmittedFence = through;
  /* A pending preemption must expose the same boundary as QueryCurrentFence.
   * Once claimed, its snapshot belongs to the notification transaction. */
  if (Scheduler->PreemptionPhase == AppleAgxPreemptionReadyToNotify)
    Scheduler->PreemptionLastCompletedFence = Scheduler->CompletedFence;
  return APPLE_AGX_TRUE;
}

/* Only the paging caller may use this path. The old fence must have been
 * accepted into the queue before the scheduler discarded it at preemption. */
APPLE_AGX_BOOL AppleAgxSchedulerQueueResubmittedPagingFence(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence) {
  APPLE_AGX_U32 index, tail, rejected;
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal,
          EngineOrdinal) || Fence == 0u ||
      Scheduler->QueueCount >= APPLE_AGX_SCHEDULER_QUEUE_CAPACITY ||
      AppleAgxSchedulerAdmissionBlocked(Scheduler) ||
      !AppleAgxSchedulerFenceAfter(Fence, AppleAgxSchedulerRetiredFence(Scheduler)) ||
      (Scheduler->ActiveFence != 0u &&
       !AppleAgxSchedulerFenceAfter(Fence, Scheduler->ActiveFence)))
    return APPLE_AGX_FALSE;
  if (Scheduler->QueueCount != 0u) {
    tail = Scheduler->FenceQueue[(Scheduler->QueueHead +
        Scheduler->QueueCount - 1u) % APPLE_AGX_SCHEDULER_QUEUE_CAPACITY];
    if (!AppleAgxSchedulerFenceAfter(Fence, tail)) return APPLE_AGX_FALSE;
  }
  for (index=0u;index<Scheduler->PreemptedFenceCount;++index)
    if (Scheduler->PreemptedFenceQueue[index] == Fence) break;
  if (index == Scheduler->PreemptedFenceCount) return APPLE_AGX_FALSE;
  rejected = Scheduler->PreemptedRejectedAfter[index];
  for (; index + 1u < Scheduler->PreemptedFenceCount; ++index) {
    Scheduler->PreemptedFenceQueue[index] = Scheduler->PreemptedFenceQueue[index+1u];
    Scheduler->PreemptedRejectedAfter[index] = Scheduler->PreemptedRejectedAfter[index+1u];
  }
  --Scheduler->PreemptedFenceCount;
  Scheduler->FenceQueue[(Scheduler->QueueHead + Scheduler->QueueCount) %
      APPLE_AGX_SCHEDULER_QUEUE_CAPACITY] = Fence;
  Scheduler->RejectedAfter[(Scheduler->QueueHead + Scheduler->QueueCount) %
      APPLE_AGX_SCHEDULER_QUEUE_CAPACITY] = rejected;
  if (Scheduler->QueueCount == 0u) Scheduler->QueuedFence = Fence;
  ++Scheduler->QueueCount;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerActivateFence(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal) ||
      Fence == 0u || Scheduler->QueueCount == 0u || Scheduler->QueuedFence != Fence ||
      Scheduler->ActiveFence != 0u ||
      AppleAgxSchedulerDispatchBlocked(Scheduler))
    return APPLE_AGX_FALSE;
  Scheduler->ActiveRejectedAfter = Scheduler->RejectedAfter[Scheduler->QueueHead];
  Scheduler->QueueHead = (Scheduler->QueueHead + 1u) % APPLE_AGX_SCHEDULER_QUEUE_CAPACITY;
  --Scheduler->QueueCount;
  Scheduler->QueuedFence = Scheduler->QueueCount != 0u ?
      Scheduler->FenceQueue[Scheduler->QueueHead] : 0u;
  Scheduler->ActiveFence = Fence;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerCompleteActiveFence(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal) ||
      Fence == 0u || Scheduler->ActiveFence != Fence ||
      !AppleAgxSchedulerCompleteFence(Scheduler, NodeOrdinal, EngineOrdinal,
                                     Fence))
    return APPLE_AGX_FALSE;
  Scheduler->ActiveFence = 0u;
  if (Scheduler->ActiveRejectedAfter)
    Scheduler->CompletedFence = Scheduler->ActiveRejectedAfter;
  Scheduler->ActiveRejectedAfter = 0u;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerAbortActiveFence(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal) ||
      Fence == 0u || Scheduler->ActiveFence != Fence ||
      !AppleAgxSchedulerCanCompleteFence(Scheduler, NodeOrdinal, EngineOrdinal,
                                        Fence))
    return APPLE_AGX_FALSE;
  Scheduler->LastAbortedFence = Fence;
  Scheduler->ActiveFence = 0u;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerFaultActiveFence(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence) {
  if (!AppleAgxSchedulerAbortActiveFence(Scheduler, NodeOrdinal,
                                        EngineOrdinal, Fence))
    return APPLE_AGX_FALSE;
  /* DXGK_PAGE_FAULT_FENCE_INVALID is clear: Windows advances to FaultedFenceId.
   * QueryCurrentFence and pending/new preemptions must agree with that boundary.
   * Keep LastAbortedFence for reset accounting and the error notification. */
  Scheduler->CompletedFence = Scheduler->ActiveRejectedAfter ?
      Scheduler->ActiveRejectedAfter : Fence;
  Scheduler->ActiveRejectedAfter = 0u;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_U32 AppleAgxSchedulerLastSubmittedFence(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal))
    return 0u;
  return Scheduler->LastSubmittedFence;
}

APPLE_AGX_U32 AppleAgxSchedulerQueuedFence(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal))
    return 0u;
  return Scheduler->QueuedFence;
}

APPLE_AGX_U32 AppleAgxSchedulerActiveFence(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal))
    return 0u;
  return Scheduler->ActiveFence;
}

APPLE_AGX_BOOL AppleAgxSchedulerHasOutstandingFence(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal))
    return APPLE_AGX_FALSE;
  return Scheduler->QueuedFence != 0u || Scheduler->ActiveFence != 0u
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

/* Carry rejection dependencies with accepted paging packets across repeated
 * preemption/reset. Never turn a rejected tail into a resubmittable packet. */
static void AppleAgxSchedulerPreservePreemptedQueue(APPLE_AGX_SCHEDULER *Scheduler) {
  APPLE_AGX_U32 index, kept, after;
  after = Scheduler->QueueCount != 0u
              ? Scheduler->FenceQueue[(Scheduler->QueueHead +
                    Scheduler->QueueCount - 1u) % APPLE_AGX_SCHEDULER_QUEUE_CAPACITY]
              : AppleAgxSchedulerRetiredFence(Scheduler);
  kept = 0u;
  for (index = 0u; index < Scheduler->PreemptedFenceCount &&
       kept < APPLE_AGX_SCHEDULER_QUEUE_CAPACITY - Scheduler->QueueCount; ++index)
    if (AppleAgxSchedulerFenceAfter(Scheduler->PreemptedFenceQueue[index], after)) {
      after = Scheduler->PreemptedFenceQueue[index];
      Scheduler->PreemptedFenceQueue[kept] = after;
      Scheduler->PreemptedRejectedAfter[kept++] = Scheduler->PreemptedRejectedAfter[index];
    }
  for (index = kept; index-- > 0u;) {
    Scheduler->PreemptedFenceQueue[index + Scheduler->QueueCount] =
        Scheduler->PreemptedFenceQueue[index];
    Scheduler->PreemptedRejectedAfter[index + Scheduler->QueueCount] =
        Scheduler->PreemptedRejectedAfter[index];
  }
  for (index = 0u; index < Scheduler->QueueCount; ++index) {
    Scheduler->PreemptedFenceQueue[index] = Scheduler->FenceQueue[
        (Scheduler->QueueHead + index) % APPLE_AGX_SCHEDULER_QUEUE_CAPACITY];
    Scheduler->PreemptedRejectedAfter[index] = Scheduler->RejectedAfter[
        (Scheduler->QueueHead + index) % APPLE_AGX_SCHEDULER_QUEUE_CAPACITY];
  }
  Scheduler->PreemptedFenceCount = Scheduler->QueueCount + kept;
}

APPLE_AGX_BOOL AppleAgxSchedulerBeginBoundaryPreemption(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 PreemptionFence,
    APPLE_AGX_U32 CutoffFence, APPLE_AGX_U32 ActiveFence) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal) ||
      PreemptionFence == 0u ||
      Scheduler->PreemptionPhase != AppleAgxPreemptionIdle ||
      CutoffFence != Scheduler->LastSubmittedFence ||
      ActiveFence != Scheduler->ActiveFence ||
      !AppleAgxSchedulerFenceAtOrAfter(CutoffFence,
                                      AppleAgxSchedulerRetiredFence(Scheduler)) ||
      (ActiveFence != 0u &&
       (!AppleAgxSchedulerFenceAfter(ActiveFence,
                                    AppleAgxSchedulerRetiredFence(Scheduler)) ||
        !AppleAgxSchedulerFenceAtOrAfter(CutoffFence, ActiveFence))))
    return APPLE_AGX_FALSE;
  AppleAgxSchedulerPreservePreemptedQueue(Scheduler);
  Scheduler->QueuedFence = 0u;
  Scheduler->QueueHead = Scheduler->QueueCount = 0u;
  Scheduler->PendingPreemptionFence = PreemptionFence;
  Scheduler->PreemptionCutoffFence = CutoffFence;
  Scheduler->PreemptionActiveFence = ActiveFence;
  Scheduler->PreemptionLastCompletedFence = Scheduler->CompletedFence;
  Scheduler->PreemptionPending = APPLE_AGX_TRUE;
  Scheduler->PreemptionPhase = ActiveFence == 0u
                                   ? AppleAgxPreemptionReadyToNotify
                                   : AppleAgxPreemptionWaitCurrentBoundary;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_PREEMPTION_PHASE AppleAgxSchedulerPreemptionPhase(
    const APPLE_AGX_SCHEDULER *Scheduler) {
  return Scheduler == APPLE_AGX_NULL ? AppleAgxPreemptionIdle
                                     : Scheduler->PreemptionPhase;
}

APPLE_AGX_BOOL AppleAgxSchedulerDispatchBlocked(
    const APPLE_AGX_SCHEDULER *Scheduler) {
  return Scheduler != APPLE_AGX_NULL &&
                 Scheduler->PreemptionPhase != AppleAgxPreemptionIdle
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_U32 AppleAgxSchedulerPreemptionCutoffFence(
    const APPLE_AGX_SCHEDULER *Scheduler) {
  return Scheduler == APPLE_AGX_NULL ? 0u
                                     : Scheduler->PreemptionCutoffFence;
}

APPLE_AGX_BOOL AppleAgxSchedulerObserveBoundaryCompletion(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 CompletedFence) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal) ||
      Scheduler->PreemptionPhase !=
          AppleAgxPreemptionWaitCurrentBoundary ||
      CompletedFence == 0u ||
      Scheduler->PreemptionActiveFence != CompletedFence ||
      (!AppleAgxSchedulerFenceAtOrAfter(Scheduler->CompletedFence, CompletedFence) &&
       Scheduler->LastAbortedFence != CompletedFence))
    return APPLE_AGX_FALSE;
  Scheduler->PreemptionActiveFence = 0u;
  Scheduler->PreemptionLastCompletedFence = Scheduler->CompletedFence;
  Scheduler->PreemptionPhase = AppleAgxPreemptionReadyToNotify;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerClaimBoundaryPreemption(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_PREEMPTION *Preemption) {
  if (Scheduler == APPLE_AGX_NULL || Preemption == APPLE_AGX_NULL ||
      Scheduler->PreemptionPhase != AppleAgxPreemptionReadyToNotify)
    return APPLE_AGX_FALSE;
  Preemption->PreemptionFence = Scheduler->PendingPreemptionFence;
  Preemption->LastCompletedFence =
      Scheduler->PreemptionLastCompletedFence;
  Scheduler->PreemptionPhase = AppleAgxPreemptionNotificationClaimed;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerCommitBoundaryPreemption(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 PreemptionFence) {
  if (Scheduler == APPLE_AGX_NULL ||
      Scheduler->PreemptionPhase != AppleAgxPreemptionNotificationClaimed ||
      Scheduler->PendingPreemptionFence != PreemptionFence)
    return APPLE_AGX_FALSE;
  Scheduler->PendingPreemptionFence = 0u;
  Scheduler->PreemptionCutoffFence = 0u;
  Scheduler->PreemptionActiveFence = 0u;
  Scheduler->PreemptionLastCompletedFence = 0u;
  Scheduler->PreemptionPending = APPLE_AGX_FALSE;
  Scheduler->PreemptionPhase = AppleAgxPreemptionIdle;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerBeginPreemption(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 PreemptionFence,
    APPLE_AGX_PREEMPTION *Preemption) {
  if (Preemption == APPLE_AGX_NULL ||
      !AppleAgxSchedulerBeginBoundaryPreemption(
          Scheduler, NodeOrdinal, EngineOrdinal, PreemptionFence,
          Scheduler == APPLE_AGX_NULL ? 0u : Scheduler->CompletedFence, 0u) ||
      !AppleAgxSchedulerClaimBoundaryPreemption(Scheduler, Preemption))
    return APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerAcknowledgePreemption(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 PreemptionFence) {
  return AppleAgxSchedulerCommitBoundaryPreemption(Scheduler,
                                                    PreemptionFence);
}

APPLE_AGX_BOOL AppleAgxSchedulerAbortPreemption(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 PreemptionFence) {
  if (Scheduler == APPLE_AGX_NULL || !Scheduler->PreemptionPending ||
      Scheduler->PendingPreemptionFence != PreemptionFence ||
      Scheduler->PreemptionPhase == AppleAgxPreemptionNotificationClaimed)
    return APPLE_AGX_FALSE;
  Scheduler->PendingPreemptionFence = 0u;
  Scheduler->PreemptionCutoffFence = 0u;
  Scheduler->PreemptionActiveFence = 0u;
  Scheduler->PreemptionLastCompletedFence = 0u;
  Scheduler->PreemptedFenceCount = 0u;
  Scheduler->PreemptionPending = APPLE_AGX_FALSE;
  Scheduler->PreemptionPhase = AppleAgxPreemptionIdle;
  return APPLE_AGX_TRUE;
}

/* Microsoft (TDR in Windows 8 and later, packets unaffected by engine reset):
 * VidSch resubmits the packets after LastAbortedFence, paging packets with
 * their original fence IDs in their original order before new packets, render
 * packets with new IDs. As at a boundary preemption, every fence that never
 * ran stays resubmittable once: the discarded queue, then the preempted fences
 * still waiting behind it. A preempted fence at or before the queue tail was
 * skipped by VidSch (a render given a new ID) and is not revived. */
APPLE_AGX_BOOL AppleAgxSchedulerResetEngine(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 *LastAbortedFence) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal) ||
      LastAbortedFence == APPLE_AGX_NULL)
    return APPLE_AGX_FALSE;
  *LastAbortedFence = Scheduler->ActiveFence != 0u
                          ? Scheduler->ActiveFence
                          : AppleAgxSchedulerRetiredFence(Scheduler);
  if (Scheduler->ActiveFence != 0u)
    Scheduler->LastAbortedFence = Scheduler->ActiveFence;
  AppleAgxSchedulerPreservePreemptedQueue(Scheduler);
  /* The reset caller has proved physical quiescence. Only the software
   * rejected tail advances here; the aborted packet is still not a successful
   * completion, and LastAbortedFence above still identifies that packet. */
  if (Scheduler->ActiveRejectedAfter)
    Scheduler->CompletedFence = Scheduler->ActiveRejectedAfter;
  Scheduler->ActiveRejectedAfter = 0u;
  Scheduler->QueuedFence = 0u;
  Scheduler->QueueHead = Scheduler->QueueCount = 0u;
  Scheduler->ActiveFence = 0u;
  Scheduler->PendingPreemptionFence = 0;
  Scheduler->PreemptionCutoffFence = 0;
  Scheduler->PreemptionActiveFence = 0;
  Scheduler->PreemptionLastCompletedFence = 0;
  Scheduler->PreemptionPending = APPLE_AGX_FALSE;
  Scheduler->PreemptionPhase = AppleAgxPreemptionIdle;
  Scheduler->Responsive = APPLE_AGX_TRUE;
  return APPLE_AGX_TRUE;
}
