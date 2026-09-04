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

void AppleAgxSchedulerInitialize(APPLE_AGX_SCHEDULER *Scheduler) {
  if (Scheduler == APPLE_AGX_NULL)
    return;
  Scheduler->CompletedFence = 0;
  Scheduler->LastSubmittedFence = 0;
  Scheduler->QueuedFence = 0;
  Scheduler->ActiveFence = 0;
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
  distance = Fence - Scheduler->CompletedFence;
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

APPLE_AGX_BOOL AppleAgxSchedulerQueueFence(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal) ||
      Fence == 0u || Scheduler->QueuedFence != 0u ||
      AppleAgxSchedulerDispatchBlocked(Scheduler) ||
      !AppleAgxSchedulerFenceAfter(Fence, Scheduler->LastSubmittedFence))
    return APPLE_AGX_FALSE;
  Scheduler->QueuedFence = Fence;
  Scheduler->LastSubmittedFence = Fence;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerActivateFence(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal) ||
      Fence == 0u || Scheduler->QueuedFence != Fence ||
      Scheduler->ActiveFence != 0u ||
      AppleAgxSchedulerDispatchBlocked(Scheduler))
    return APPLE_AGX_FALSE;
  Scheduler->QueuedFence = 0u;
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

APPLE_AGX_BOOL AppleAgxSchedulerBeginBoundaryPreemption(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 PreemptionFence,
    APPLE_AGX_U32 CutoffFence, APPLE_AGX_U32 ActiveFence) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal) ||
      PreemptionFence == 0u ||
      Scheduler->PreemptionPhase != AppleAgxPreemptionIdle ||
      !AppleAgxSchedulerFenceAtOrAfter(CutoffFence,
                                      Scheduler->CompletedFence) ||
      (ActiveFence != 0u &&
       (!AppleAgxSchedulerFenceAfter(ActiveFence,
                                    Scheduler->CompletedFence) ||
        !AppleAgxSchedulerFenceAtOrAfter(CutoffFence, ActiveFence))))
    return APPLE_AGX_FALSE;
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
      Scheduler->CompletedFence != CompletedFence)
    return APPLE_AGX_FALSE;
  Scheduler->PreemptionActiveFence = 0u;
  Scheduler->PreemptionLastCompletedFence = CompletedFence;
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
  Scheduler->PreemptionPending = APPLE_AGX_FALSE;
  Scheduler->PreemptionPhase = AppleAgxPreemptionIdle;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSchedulerResetEngine(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 *LastAbortedFence) {
  if (!AppleAgxSchedulerValidateEngine(Scheduler, NodeOrdinal, EngineOrdinal) ||
      LastAbortedFence == APPLE_AGX_NULL)
    return APPLE_AGX_FALSE;
  *LastAbortedFence = Scheduler->CompletedFence;
  Scheduler->PendingPreemptionFence = 0;
  Scheduler->PreemptionCutoffFence = 0;
  Scheduler->PreemptionActiveFence = 0;
  Scheduler->PreemptionLastCompletedFence = 0;
  Scheduler->PreemptionPending = APPLE_AGX_FALSE;
  Scheduler->PreemptionPhase = AppleAgxPreemptionIdle;
  Scheduler->Responsive = APPLE_AGX_TRUE;
  return APPLE_AGX_TRUE;
}
