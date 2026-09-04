#ifndef APPLE_AGX_SCHEDULER_H
#define APPLE_AGX_SCHEDULER_H

#include "apple_agx_state.h"

#define APPLE_AGX_SCHEDULER_NODE_COUNT 1u
#define APPLE_AGX_SCHEDULER_ENGINE_COUNT 1u
#define APPLE_AGX_SCHEDULER_ENGINE_AFFINITY 1u

typedef struct _APPLE_AGX_SCHEDULER_CONTEXT {
  APPLE_AGX_U32 NodeOrdinal;
  APPLE_AGX_U32 EngineAffinity;
  APPLE_AGX_BOOL Active;
} APPLE_AGX_SCHEDULER_CONTEXT;

typedef struct _APPLE_AGX_PREEMPTION {
  APPLE_AGX_U32 PreemptionFence;
  APPLE_AGX_U32 LastCompletedFence;
} APPLE_AGX_PREEMPTION;

typedef enum _APPLE_AGX_PREEMPTION_PHASE {
  AppleAgxPreemptionIdle = 0,
  AppleAgxPreemptionWaitCurrentBoundary,
  AppleAgxPreemptionReadyToNotify,
  AppleAgxPreemptionNotificationClaimed,
} APPLE_AGX_PREEMPTION_PHASE;

typedef struct _APPLE_AGX_SCHEDULER {
  APPLE_AGX_U32 CompletedFence;
  APPLE_AGX_U32 LastSubmittedFence;
  APPLE_AGX_U32 QueuedFence;
  APPLE_AGX_U32 ActiveFence;
  APPLE_AGX_U32 PendingPreemptionFence;
  APPLE_AGX_U32 PreemptionCutoffFence;
  APPLE_AGX_U32 PreemptionActiveFence;
  APPLE_AGX_U32 PreemptionLastCompletedFence;
  APPLE_AGX_COUNT ContextCount;
  APPLE_AGX_BOOL PreemptionPending;
  APPLE_AGX_BOOL Responsive;
  APPLE_AGX_PREEMPTION_PHASE PreemptionPhase;
} APPLE_AGX_SCHEDULER;

void AppleAgxSchedulerInitialize(APPLE_AGX_SCHEDULER *Scheduler);
void AppleAgxSchedulerContextInitialize(
    APPLE_AGX_SCHEDULER_CONTEXT *Context);
APPLE_AGX_BOOL AppleAgxSchedulerValidateEngine(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal);
APPLE_AGX_BOOL AppleAgxSchedulerEngineResponsive(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal);
APPLE_AGX_BOOL AppleAgxSchedulerCreateContext(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_SCHEDULER_CONTEXT *Context,
    APPLE_AGX_U32 NodeOrdinal, APPLE_AGX_U32 EngineAffinity);
APPLE_AGX_BOOL AppleAgxSchedulerDestroyContext(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_SCHEDULER_CONTEXT *Context);
APPLE_AGX_BOOL AppleAgxSchedulerCanCompleteFence(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence);
APPLE_AGX_BOOL AppleAgxSchedulerCompleteFence(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence);
APPLE_AGX_U32 AppleAgxSchedulerCurrentFence(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal);
APPLE_AGX_BOOL AppleAgxSchedulerQueueFence(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence);
APPLE_AGX_BOOL AppleAgxSchedulerActivateFence(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence);
APPLE_AGX_BOOL AppleAgxSchedulerCompleteActiveFence(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 Fence);
APPLE_AGX_U32 AppleAgxSchedulerLastSubmittedFence(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal);
APPLE_AGX_U32 AppleAgxSchedulerQueuedFence(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal);
APPLE_AGX_U32 AppleAgxSchedulerActiveFence(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal);
APPLE_AGX_BOOL AppleAgxSchedulerHasOutstandingFence(
    const APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal);
/*
 * Freeze dispatch at a DMA-buffer boundary.  CutoffFence is the submit
 * snapshot; ActiveFence is zero when no packet reached hardware.  A nonzero
 * active fence must finish through the ordinary completion path before the
 * preemption notification may be claimed.
 */
APPLE_AGX_BOOL AppleAgxSchedulerBeginBoundaryPreemption(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 PreemptionFence,
    APPLE_AGX_U32 CutoffFence, APPLE_AGX_U32 ActiveFence);
APPLE_AGX_PREEMPTION_PHASE AppleAgxSchedulerPreemptionPhase(
    const APPLE_AGX_SCHEDULER *Scheduler);
APPLE_AGX_BOOL AppleAgxSchedulerDispatchBlocked(
    const APPLE_AGX_SCHEDULER *Scheduler);
APPLE_AGX_U32 AppleAgxSchedulerPreemptionCutoffFence(
    const APPLE_AGX_SCHEDULER *Scheduler);
APPLE_AGX_BOOL AppleAgxSchedulerObserveBoundaryCompletion(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 CompletedFence);
APPLE_AGX_BOOL AppleAgxSchedulerClaimBoundaryPreemption(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_PREEMPTION *Preemption);
/* A claimed notification is irreversible: it can only be committed. */
APPLE_AGX_BOOL AppleAgxSchedulerCommitBoundaryPreemption(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 PreemptionFence);
APPLE_AGX_BOOL AppleAgxSchedulerBeginPreemption(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 PreemptionFence,
    APPLE_AGX_PREEMPTION *Preemption);
APPLE_AGX_BOOL AppleAgxSchedulerAcknowledgePreemption(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 PreemptionFence);
APPLE_AGX_BOOL AppleAgxSchedulerAbortPreemption(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 PreemptionFence);
APPLE_AGX_BOOL AppleAgxSchedulerResetEngine(
    APPLE_AGX_SCHEDULER *Scheduler, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 *LastAbortedFence);

#endif /* APPLE_AGX_SCHEDULER_H */
