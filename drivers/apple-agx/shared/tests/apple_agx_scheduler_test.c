#include "apple_agx_scheduler.h"

#include <assert.h>

static void test_one_engine_topology_rejects_invalid_coordinates(void) {
  APPLE_AGX_SCHEDULER scheduler;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerValidateEngine(&scheduler, 0, 0));
  assert(!AppleAgxSchedulerValidateEngine(&scheduler, 1, 0));
  assert(!AppleAgxSchedulerValidateEngine(&scheduler, 0, 1));
  assert(AppleAgxSchedulerEngineResponsive(&scheduler, 0, 0));
}

static void test_context_lifetime_is_owned_by_one_engine(void) {
  APPLE_AGX_SCHEDULER scheduler;
  APPLE_AGX_SCHEDULER_CONTEXT context;

  AppleAgxSchedulerInitialize(&scheduler);
  AppleAgxSchedulerContextInitialize(&context);
  assert(!AppleAgxSchedulerCreateContext(&scheduler, &context, 1, 1));
  assert(!AppleAgxSchedulerCreateContext(&scheduler, &context, 0, 2));
  assert(AppleAgxSchedulerCreateContext(&scheduler, &context, 0, 1));
  assert(!AppleAgxSchedulerCreateContext(&scheduler, &context, 0, 1));
  assert(scheduler.ContextCount == 1);
  assert(AppleAgxSchedulerDestroyContext(&scheduler, &context));
  assert(!AppleAgxSchedulerDestroyContext(&scheduler, &context));
  assert(scheduler.ContextCount == 0);
}

static void test_completed_fence_is_monotonic_and_queryable(void) {
  APPLE_AGX_SCHEDULER scheduler;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerCurrentFence(&scheduler, 0, 0) == 0);
  assert(!AppleAgxSchedulerCanCompleteFence(&scheduler, 0, 0, 0));
  assert(AppleAgxSchedulerCanCompleteFence(&scheduler, 0, 0, 1));
  assert(!AppleAgxSchedulerCompleteFence(&scheduler, 0, 0, 0));
  assert(AppleAgxSchedulerCompleteFence(&scheduler, 0, 0, 1));
  assert(!AppleAgxSchedulerCanCompleteFence(&scheduler, 0, 0, 1));
  assert(!AppleAgxSchedulerCanCompleteFence(&scheduler, 1, 0, 2));
  assert(AppleAgxSchedulerCanCompleteFence(&scheduler, 0, 0, 3));
  assert(!AppleAgxSchedulerCompleteFence(&scheduler, 0, 0, 1));
  assert(AppleAgxSchedulerCompleteFence(&scheduler, 0, 0, 3));
  assert(!AppleAgxSchedulerCompleteFence(&scheduler, 0, 0, 2));
  assert(AppleAgxSchedulerCurrentFence(&scheduler, 0, 0) == 3);
}

static void test_render_progress_separates_queued_active_and_completed(void) {
  APPLE_AGX_SCHEDULER scheduler;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerLastSubmittedFence(&scheduler, 0u, 0u) == 0u);
  assert(AppleAgxSchedulerQueuedFence(&scheduler, 0u, 0u) == 0u);
  assert(AppleAgxSchedulerActiveFence(&scheduler, 0u, 0u) == 0u);
  assert(!AppleAgxSchedulerHasOutstandingFence(&scheduler, 0u, 0u));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerHasOutstandingFence(&scheduler, 0u, 0u));
  assert(AppleAgxSchedulerLastSubmittedFence(&scheduler, 0u, 0u) == 5u);
  assert(AppleAgxSchedulerQueuedFence(&scheduler, 0u, 0u) == 5u);
  assert(AppleAgxSchedulerCurrentFence(&scheduler, 0u, 0u) == 0u);
  assert(!AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 5u));
  assert(!AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 4u));
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerQueuedFence(&scheduler, 0u, 0u) == 0u);
  assert(AppleAgxSchedulerActiveFence(&scheduler, 0u, 0u) == 5u);
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 6u));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 7u));
  assert(AppleAgxSchedulerQueuedFence(&scheduler, 0u, 0u) == 6u);
  assert(!AppleAgxSchedulerCompleteActiveFence(&scheduler, 0u, 0u, 6u));
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerCurrentFence(&scheduler, 0u, 0u) == 5u);
  assert(AppleAgxSchedulerActiveFence(&scheduler, 0u, 0u) == 0u);
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 6u));
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler, 0u, 0u, 6u));
  assert(AppleAgxSchedulerCurrentFence(&scheduler, 0u, 0u) == 6u);
  assert(AppleAgxSchedulerLastSubmittedFence(&scheduler, 0u, 0u) == 7u);
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 7u));
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler, 0u, 0u, 7u));
  assert(!AppleAgxSchedulerHasOutstandingFence(&scheduler, 0u, 0u));
}

static void test_render_progress_rejects_stale_and_invalid_engine_fences(void) {
  APPLE_AGX_SCHEDULER scheduler;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(!AppleAgxSchedulerQueueFence(&scheduler, 1u, 0u, 1u));
  assert(!AppleAgxSchedulerQueueFence(&scheduler, 0u, 1u, 1u));
  assert(!AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 0u));
  scheduler.CompletedFence = 0xfffffffeu;
  scheduler.LastSubmittedFence = 0xfffffffeu;
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 0xffffffffu));
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 0xffffffffu));
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler, 0u, 0u,
                                              0xffffffffu));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 1u));
  assert(!AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 0xfffffffeu));
}

static void test_idle_preemption_requires_acknowledgement(void) {
  APPLE_AGX_SCHEDULER scheduler;
  APPLE_AGX_PREEMPTION preemption;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(!AppleAgxSchedulerBeginPreemption(&scheduler, 1, 0, 7,
                                           &preemption));
  assert(AppleAgxSchedulerBeginPreemption(&scheduler, 0, 0, 7,
                                          &preemption));
  assert(preemption.PreemptionFence == 7);
  assert(preemption.LastCompletedFence == 0);
  assert(!AppleAgxSchedulerBeginPreemption(&scheduler, 0, 0, 8,
                                           &preemption));
  assert(!AppleAgxSchedulerAcknowledgePreemption(&scheduler, 8));
  assert(AppleAgxSchedulerAcknowledgePreemption(&scheduler, 7));
  assert(AppleAgxSchedulerBeginPreemption(&scheduler, 0, 0, 8,
                                          &preemption));
}

static void test_failed_preemption_delivery_can_be_retried(void) {
  APPLE_AGX_SCHEDULER scheduler;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerBeginBoundaryPreemption(
      &scheduler, 0u, 0u, 9u, 0u, 0u));
  assert(!AppleAgxSchedulerAbortPreemption(&scheduler, 8));
  assert(AppleAgxSchedulerAbortPreemption(&scheduler, 9));
  assert(AppleAgxSchedulerBeginBoundaryPreemption(
      &scheduler, 0u, 0u, 9u, 0u, 0u));
}

static void test_claimed_preemption_cannot_be_aborted_or_reported_twice(void) {
  APPLE_AGX_SCHEDULER scheduler;
  APPLE_AGX_PREEMPTION preemption;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerBeginPreemption(&scheduler, 0u, 0u, 10u,
                                          &preemption));
  assert(AppleAgxSchedulerPreemptionPhase(&scheduler) ==
         AppleAgxPreemptionNotificationClaimed);
  assert(!AppleAgxSchedulerAbortPreemption(&scheduler, 10u));
  assert(!AppleAgxSchedulerClaimBoundaryPreemption(&scheduler,
                                                   &preemption));
  assert(AppleAgxSchedulerCommitBoundaryPreemption(&scheduler, 10u));
  assert(!AppleAgxSchedulerCommitBoundaryPreemption(&scheduler, 10u));
}

static void test_idle_boundary_preemption_blocks_dispatch_and_notifies_once(
    void) {
  APPLE_AGX_SCHEDULER scheduler;
  APPLE_AGX_PREEMPTION preemption;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerBeginBoundaryPreemption(
      &scheduler, 0u, 0u, 41u, 0u, 0u));
  assert(AppleAgxSchedulerPreemptionPhase(&scheduler) ==
         AppleAgxPreemptionReadyToNotify);
  assert(AppleAgxSchedulerDispatchBlocked(&scheduler));
  assert(AppleAgxSchedulerPreemptionCutoffFence(&scheduler) == 0u);
  assert(AppleAgxSchedulerClaimBoundaryPreemption(&scheduler,
                                                  &preemption));
  assert(preemption.PreemptionFence == 41u);
  assert(preemption.LastCompletedFence == 0u);
  assert(!AppleAgxSchedulerClaimBoundaryPreemption(&scheduler,
                                                   &preemption));
  assert(AppleAgxSchedulerCommitBoundaryPreemption(&scheduler, 41u));
  assert(AppleAgxSchedulerPreemptionPhase(&scheduler) ==
         AppleAgxPreemptionIdle);
  assert(!AppleAgxSchedulerDispatchBlocked(&scheduler));
  assert(!AppleAgxSchedulerCommitBoundaryPreemption(&scheduler, 41u));
}

static void test_active_dma_must_reach_its_boundary_before_notification(void) {
  APPLE_AGX_SCHEDULER scheduler;
  APPLE_AGX_PREEMPTION preemption;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 7u));
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 7u));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 9u));
  assert(AppleAgxSchedulerBeginBoundaryPreemption(
      &scheduler, 0u, 0u, 42u, 9u, 7u));
  assert(AppleAgxSchedulerQueuedFence(&scheduler, 0u, 0u) == 0u);
  assert(AppleAgxSchedulerLastSubmittedFence(&scheduler, 0u, 0u) == 9u);
  assert(AppleAgxSchedulerPreemptionPhase(&scheduler) ==
         AppleAgxPreemptionWaitCurrentBoundary);
  assert(!AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 10u));
  assert(!AppleAgxSchedulerClaimBoundaryPreemption(&scheduler,
                                                   &preemption));
  assert(!AppleAgxSchedulerObserveBoundaryCompletion(&scheduler, 0u, 0u,
                                                     6u));
  assert(AppleAgxSchedulerPreemptionPhase(&scheduler) ==
         AppleAgxPreemptionWaitCurrentBoundary);
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler, 0u, 0u, 7u));
  assert(AppleAgxSchedulerObserveBoundaryCompletion(&scheduler, 0u, 0u,
                                                    7u));
  assert(!AppleAgxSchedulerObserveBoundaryCompletion(&scheduler, 0u, 0u,
                                                     7u));
  assert(AppleAgxSchedulerPreemptionPhase(&scheduler) ==
         AppleAgxPreemptionReadyToNotify);
  assert(AppleAgxSchedulerClaimBoundaryPreemption(&scheduler,
                                                  &preemption));
  assert(preemption.PreemptionFence == 42u);
  assert(preemption.LastCompletedFence == 7u);
  assert(AppleAgxSchedulerCommitBoundaryPreemption(&scheduler, 42u));
}

static void test_waiting_preemption_rejects_completion_past_active_boundary(
    void) {
  APPLE_AGX_SCHEDULER scheduler;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 7u));
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 7u));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 9u));
  assert(AppleAgxSchedulerBeginBoundaryPreemption(
      &scheduler, 0u, 0u, 43u, 9u, 7u));
  assert(!AppleAgxSchedulerCanCompleteFence(&scheduler, 0u, 0u, 8u));
  assert(!AppleAgxSchedulerCompleteFence(&scheduler, 0u, 0u, 8u));
  assert(AppleAgxSchedulerCurrentFence(&scheduler, 0u, 0u) == 0u);
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler, 0u, 0u, 7u));
  assert(AppleAgxSchedulerObserveBoundaryCompletion(&scheduler, 0u, 0u,
                                                    7u));
}

static void test_preemption_snapshot_must_describe_a_reachable_boundary(void) {
  APPLE_AGX_SCHEDULER scheduler;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler, 0u, 0u, 5u));
  assert(!AppleAgxSchedulerBeginBoundaryPreemption(
      &scheduler, 0u, 0u, 44u, 4u, 0u));
  assert(!AppleAgxSchedulerBeginBoundaryPreemption(
      &scheduler, 0u, 0u, 44u, 9u, 5u));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 7u));
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 7u));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 9u));
  assert(!AppleAgxSchedulerBeginBoundaryPreemption(
      &scheduler, 0u, 0u, 44u, 9u, 10u));
  assert(AppleAgxSchedulerBeginBoundaryPreemption(
      &scheduler, 0u, 0u, 44u, 9u, 7u));
}

static void test_idle_preemption_discards_queued_work_before_notification(
    void) {
  APPLE_AGX_SCHEDULER scheduler;
  APPLE_AGX_PREEMPTION preemption;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerBeginBoundaryPreemption(
      &scheduler, 0u, 0u, 45u, 5u, 0u));
  assert(AppleAgxSchedulerQueuedFence(&scheduler, 0u, 0u) == 0u);
  assert(AppleAgxSchedulerCurrentFence(&scheduler, 0u, 0u) == 0u);
  assert(AppleAgxSchedulerClaimBoundaryPreemption(&scheduler,
                                                  &preemption));
  assert(preemption.LastCompletedFence == 0u);
  assert(AppleAgxSchedulerCommitBoundaryPreemption(&scheduler, 45u));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 6u));
}

static void test_reset_reports_last_aborted_fence_and_restores_progress(void) {
  APPLE_AGX_SCHEDULER scheduler;
  APPLE_AGX_U32 last_aborted = 99;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 6u));
  assert(!AppleAgxSchedulerResetEngine(&scheduler, 0, 1, &last_aborted));
  assert(AppleAgxSchedulerResetEngine(&scheduler, 0, 0, &last_aborted));
  assert(last_aborted == 5u);
  assert(AppleAgxSchedulerEngineResponsive(&scheduler, 0, 0));
  assert(AppleAgxSchedulerCurrentFence(&scheduler, 0, 0) == 0u);
  assert(AppleAgxSchedulerLastSubmittedFence(&scheduler, 0, 0) == 6u);
  assert(!AppleAgxSchedulerHasOutstandingFence(&scheduler, 0, 0));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 7u));
}

static void test_reset_without_active_packet_reports_completed_boundary(void) {
  APPLE_AGX_SCHEDULER scheduler;
  APPLE_AGX_U32 last_aborted = 99u;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 3u));
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 3u));
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler, 0u, 0u, 3u));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 4u));
  assert(AppleAgxSchedulerResetEngine(&scheduler, 0u, 0u, &last_aborted));
  assert(last_aborted == 3u);
  assert(AppleAgxSchedulerCurrentFence(&scheduler, 0u, 0u) == 3u);
  assert(!AppleAgxSchedulerHasOutstandingFence(&scheduler, 0u, 0u));
}

static void test_aborted_preemption_cannot_replay_discarded_fence(void) {
  APPLE_AGX_SCHEDULER scheduler;
  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler,0u,0u,10u));
  assert(AppleAgxSchedulerBeginBoundaryPreemption(&scheduler,0u,0u,11u,10u,0u));
  assert(AppleAgxSchedulerAbortPreemption(&scheduler,11u));
  assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,10u));
}

static void test_preempted_paging_fence_replays_once_without_reordering(void) {
  APPLE_AGX_SCHEDULER scheduler;
  APPLE_AGX_PREEMPTION preemption = {0};
  APPLE_AGX_U32 aborted = 0u;
  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler,0u,0u,0x1d3eu));
  assert(AppleAgxSchedulerActivateFence(&scheduler,0u,0u,0x1d3eu));
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler,0u,0u,0x1d3eu));
  assert(AppleAgxSchedulerQueueFence(&scheduler,0u,0u,0x1d3fu));
  assert(AppleAgxSchedulerQueueFence(&scheduler,0u,0u,0x1d40u));
  assert(AppleAgxSchedulerBeginBoundaryPreemption(&scheduler,0u,0u,0x1d50u,0x1d40u,0u));
  assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,0x1d3fu));
  assert(AppleAgxSchedulerClaimBoundaryPreemption(&scheduler,&preemption));
  assert(preemption.LastCompletedFence==0x1d3eu);
  assert(AppleAgxSchedulerCommitBoundaryPreemption(&scheduler,0x1d50u));
  assert(!AppleAgxSchedulerQueueFence(&scheduler,0u,0u,0x1d3fu));
  assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,0x1d3eu));
  assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,0x1d3du));
  assert(AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,0x1d3fu));
  assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,0x1d3fu));
  assert(AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,0x1d40u));
  assert(scheduler.PreemptedFenceCount==0u);
  assert(AppleAgxSchedulerActivateFence(&scheduler,0u,0u,0x1d3fu));
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler,0u,0u,0x1d3fu));
  assert(AppleAgxSchedulerActivateFence(&scheduler,0u,0u,0x1d40u));
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler,0u,0u,0x1d40u));
  assert(AppleAgxSchedulerQueueFence(&scheduler,0u,0u,0x1d41u));
  assert(AppleAgxSchedulerResetEngine(&scheduler,0u,0u,&aborted));
  /* 0x1d41 never ran: an engine reset leaves it resubmittable (once). */
  assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,0x1d40u));
  assert(AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,0x1d41u));
  assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,0x1d41u));
}

/* Microsoft, TDR in Windows 8 and later (packets unaffected by engine reset):
 * VidSch resubmits the paging packets that never ran with their original
 * fence IDs, in order, before new packets; render packets get new IDs. */
static void test_engine_reset_keeps_unexecuted_fences_resubmittable(void) {
  APPLE_AGX_SCHEDULER scheduler;
  APPLE_AGX_PREEMPTION preemption = {0};
  APPLE_AGX_U32 aborted = 0u;

  /* Queued behind the active packet. */
  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler,0u,0u,10u));
  assert(AppleAgxSchedulerActivateFence(&scheduler,0u,0u,10u));
  assert(AppleAgxSchedulerQueueFence(&scheduler,0u,0u,11u));
  assert(AppleAgxSchedulerQueueFence(&scheduler,0u,0u,12u));
  assert(AppleAgxSchedulerResetEngine(&scheduler,0u,0u,&aborted));
  assert(aborted==10u && !AppleAgxSchedulerHasOutstandingFence(&scheduler,0u,0u));
  assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,10u));
  assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,13u));
  assert(AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,11u));
  assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,11u));
  assert(AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,12u));
  assert(AppleAgxSchedulerQueueFence(&scheduler,0u,0u,13u));
  assert(AppleAgxSchedulerActivateFence(&scheduler,0u,0u,11u));

  /* EXP1296 order: the preemption waits on the active boundary, the firmware
   * ends the packet, then the reset. */
  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler,0u,0u,20u));
  assert(AppleAgxSchedulerActivateFence(&scheduler,0u,0u,20u));
  assert(AppleAgxSchedulerQueueFence(&scheduler,0u,0u,21u));
  assert(AppleAgxSchedulerBeginBoundaryPreemption(&scheduler,0u,0u,30u,21u,20u));
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler,0u,0u,20u));
  assert(AppleAgxSchedulerResetEngine(&scheduler,0u,0u,&aborted));
  assert(aborted==20u && AppleAgxSchedulerPreemptionPhase(&scheduler)==AppleAgxPreemptionIdle);
  assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,20u));
  assert(AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,21u));

  /* After a preemption VidSch gave render 40 a new ID and resubmitted paging
   * 41; the reset keeps 41 resubmittable and does not revive 40. */
  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler,0u,0u,39u));
  assert(AppleAgxSchedulerActivateFence(&scheduler,0u,0u,39u));
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler,0u,0u,39u));
  assert(AppleAgxSchedulerQueueFence(&scheduler,0u,0u,40u));
  assert(AppleAgxSchedulerQueueFence(&scheduler,0u,0u,41u));
  assert(AppleAgxSchedulerBeginBoundaryPreemption(&scheduler,0u,0u,50u,41u,0u));
  assert(AppleAgxSchedulerClaimBoundaryPreemption(&scheduler,&preemption));
  assert(AppleAgxSchedulerCommitBoundaryPreemption(&scheduler,50u));
  assert(AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,41u));
  assert(AppleAgxSchedulerResetEngine(&scheduler,0u,0u,&aborted));
  assert(aborted==39u && scheduler.PreemptedFenceCount==1u);
  assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,40u));
  assert(AppleAgxSchedulerQueueResubmittedPagingFence(&scheduler,0u,0u,41u));
}

/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * A firmware-killed job is aborted, not successfully completed. Reset and
 * preemption must retain that distinction, including fence wrap and retry. */
static void test_killed_fence_is_aborted_without_success(void) {
  APPLE_AGX_SCHEDULER s;
  APPLE_AGX_PREEMPTION p;
  APPLE_AGX_U32 aborted;
  unsigned wrap;
  for (wrap = 0; wrap < 2; ++wrap) {
    APPLE_AGX_U32 before = wrap ? 0xfffffffeu : 17u;
    APPLE_AGX_U32 killed = before + 1u;
    APPLE_AGX_U32 next = wrap ? 1u : 19u;
    AppleAgxSchedulerInitialize(&s);
    s.CompletedFence = s.LastSubmittedFence = before;
    assert(AppleAgxSchedulerQueueFence(&s,0,0,killed));
    assert(AppleAgxSchedulerActivateFence(&s,0,0,killed));
    assert(AppleAgxSchedulerQueueFence(&s,0,0,next));
    assert(AppleAgxSchedulerBeginBoundaryPreemption(&s,0,0,42,next,killed));
    assert(!AppleAgxSchedulerAbortActiveFence(&s,1,0,killed));
    assert(!AppleAgxSchedulerAbortActiveFence(&s,0,0,next));
    assert(s.ActiveFence == killed && s.CompletedFence == before);
    assert(AppleAgxSchedulerAbortActiveFence(&s,0,0,killed));
    assert(!AppleAgxSchedulerAbortActiveFence(&s,0,0,killed));
    assert(!AppleAgxSchedulerCompleteFence(&s,0,0,killed));
    assert(s.CompletedFence == before && s.LastAbortedFence == killed);
    assert(AppleAgxSchedulerObserveBoundaryCompletion(&s,0,0,killed));
    assert(AppleAgxSchedulerClaimBoundaryPreemption(&s,&p));
    assert(p.LastCompletedFence == before);
    assert(AppleAgxSchedulerResetEngine(&s,0,0,&aborted));
    assert(aborted == killed && s.CompletedFence == before);
    assert(AppleAgxSchedulerResetEngine(&s,0,0,&aborted));
    assert(aborted == killed);
    assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&s,0,0,killed));
    assert(AppleAgxSchedulerQueueResubmittedPagingFence(&s,0,0,next));
    assert(AppleAgxSchedulerActivateFence(&s,0,0,next));
    assert(AppleAgxSchedulerCompleteActiveFence(&s,0,0,next));
    assert(s.CompletedFence == next && s.LastAbortedFence == 0u);
    assert(AppleAgxSchedulerResetEngine(&s,0,0,&aborted));
    assert(aborted == next); /* completed boundary when nothing was aborted */
  }
}

/* EXP2215 Wom1PreemptRefusal: PreemptCommand 1104 arrived while boundary
 * 1103 was NotificationClaimed (same millisecond). The claimer had already
 * handed DMA_PREEMPTED to dxgkrnl (scheduler_windows.c records the fence in
 * DeliveredPreemptionFence before DxgkCbNotifyInterrupt) but had not yet
 * re-taken the lock to commit. */
static void test_next_preemption_inside_claim_commit_window(void) {
  APPLE_AGX_SCHEDULER scheduler;
  APPLE_AGX_PREEMPTION preemption;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 18530u));
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 18530u));
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler, 0u, 0u, 18530u));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 18532u));
  assert(AppleAgxSchedulerBeginBoundaryPreemption(
      &scheduler, 0u, 0u, 1103u, 18532u, 0u));
  assert(AppleAgxSchedulerClaimBoundaryPreemption(&scheduler, &preemption));
  assert(preemption.PreemptionFence == 1103u);
  /* Claimed but not delivered: a new boundary is still refused. */
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 18533u));
  assert(!AppleAgxSchedulerBeginBoundaryPreemption(
      &scheduler, 0u, 0u, 1104u, 18533u, 0u));
  assert(AppleAgxSchedulerPreemptionPhase(&scheduler) ==
         AppleAgxPreemptionNotificationClaimed);
  /* Delivered: VidSch processed 1103 and sends 1104 before the commit. */
  scheduler.DeliveredPreemptionFence = 1103u;
  assert(!AppleAgxSchedulerBeginBoundaryPreemption(
      &scheduler, 0u, 0u, 1103u, 18533u, 0u)); /* not a new preemption */
  assert(AppleAgxSchedulerBeginBoundaryPreemption(
      &scheduler, 0u, 0u, 1104u, 18533u, 0u));
  assert(scheduler.PendingPreemptionFence == 1104u &&
         scheduler.PreemptionCutoffFence == 18533u);
  /* The claimer's late commit of 1103 succeeds once, leaves 1104 alone. */
  assert(AppleAgxSchedulerCommitBoundaryPreemption(&scheduler, 1103u));
  assert(!AppleAgxSchedulerCommitBoundaryPreemption(&scheduler, 1103u));
  assert(scheduler.PendingPreemptionFence == 1104u &&
         AppleAgxSchedulerPreemptionPhase(&scheduler) ==
             AppleAgxPreemptionReadyToNotify);
  assert(AppleAgxSchedulerClaimBoundaryPreemption(&scheduler, &preemption));
  assert(preemption.PreemptionFence == 1104u);
  assert(AppleAgxSchedulerCommitBoundaryPreemption(&scheduler, 1104u));
  assert(AppleAgxSchedulerPreemptionPhase(&scheduler) == AppleAgxPreemptionIdle);
  assert(!scheduler.DeliveredPreemptionFence &&
         !scheduler.EarlyCommittedPreemptionFence);
}

/* Depth-2 kick fallback: a kick refused before the firmware saw it is
 * undone; the fence stays queued and runs at depth 1 after the active one. */
static void test_unkicked_fence_runs_at_depth_one_after_the_active_fence(void) {
  APPLE_AGX_SCHEDULER scheduler;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 6u));
  assert(!AppleAgxSchedulerUnkickFence(&scheduler, 0u, 0u, 5u)); /* none active */
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 5u));
  assert(!AppleAgxSchedulerUnkickFence(&scheduler, 0u, 0u, 6u)); /* not kicked */
  assert(AppleAgxSchedulerKickQueuedFence(&scheduler, 0u, 0u, 6u));
  assert(!AppleAgxSchedulerUnkickFence(&scheduler, 0u, 0u, 7u)); /* wrong fence */
  assert(!AppleAgxSchedulerUnkickFence(&scheduler, 1u, 0u, 6u)); /* wrong engine */
  assert(AppleAgxSchedulerUnkickFence(&scheduler, 0u, 0u, 6u));
  assert(AppleAgxSchedulerKickedFence(&scheduler) == 0u);
  assert(AppleAgxSchedulerQueuedFence(&scheduler, 0u, 0u) == 6u);
  assert(!AppleAgxSchedulerUnkickFence(&scheduler, 0u, 0u, 6u)); /* only once */
  assert(!AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 6u)); /* 5 still runs */
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 6u));
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler, 0u, 0u, 6u));
  assert(AppleAgxSchedulerCurrentFence(&scheduler, 0u, 0u) == 6u);
}

/* EXP2221: the kick is refused after a boundary preemption began with it
 * kicked (the preemption made it the boundary). The never-started fence is
 * handed back first, ahead of the queued tail, and the active fence ends the
 * preemption. */
static void test_unkick_during_preemption_hands_the_fence_back(void) {
  APPLE_AGX_SCHEDULER scheduler;
  APPLE_AGX_PREEMPTION preemption;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 6u));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 7u));
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerKickQueuedFence(&scheduler, 0u, 0u, 6u));
  assert(AppleAgxSchedulerBeginBoundaryPreemption(&scheduler, 0u, 0u, 900u, 7u, 5u));
  assert(!AppleAgxSchedulerCanCompleteFence(&scheduler, 0u, 0u, 7u));
  assert(AppleAgxSchedulerUnkickFence(&scheduler, 0u, 0u, 6u));
  assert(AppleAgxSchedulerKickedFence(&scheduler) == 0u);
  assert(scheduler.PreemptedFenceCount == 2u &&
         scheduler.PreemptedFenceQueue[0] == 6u &&
         scheduler.PreemptedFenceQueue[1] == 7u);
  assert(AppleAgxSchedulerQueuedFence(&scheduler, 0u, 0u) == 0u);
  assert(!AppleAgxSchedulerUnkickFence(&scheduler, 0u, 0u, 6u));
  assert(AppleAgxSchedulerCompleteActiveFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerObserveBoundaryCompletion(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerPreemptionPhase(&scheduler) ==
         AppleAgxPreemptionReadyToNotify);
  assert(AppleAgxSchedulerClaimBoundaryPreemption(&scheduler, &preemption));
  assert(preemption.PreemptionFence == 900u);
  assert(AppleAgxSchedulerCommitBoundaryPreemption(&scheduler, 900u));
  assert(AppleAgxSchedulerPreemptionPhase(&scheduler) == AppleAgxPreemptionIdle);
}

/* Without the boundary the preemption waits for, the undo is refused. */
static void test_unkick_is_refused_in_a_foreign_preemption_phase(void) {
  APPLE_AGX_SCHEDULER scheduler;

  AppleAgxSchedulerInitialize(&scheduler);
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerQueueFence(&scheduler, 0u, 0u, 6u));
  assert(AppleAgxSchedulerActivateFence(&scheduler, 0u, 0u, 5u));
  assert(AppleAgxSchedulerKickQueuedFence(&scheduler, 0u, 0u, 6u));
  scheduler.PreemptionPhase = AppleAgxPreemptionReadyToNotify;
  assert(!AppleAgxSchedulerUnkickFence(&scheduler, 0u, 0u, 6u));
  assert(AppleAgxSchedulerKickedFence(&scheduler) == 6u);
}

int main(void) {
  test_killed_fence_is_aborted_without_success();
  test_one_engine_topology_rejects_invalid_coordinates();
  test_context_lifetime_is_owned_by_one_engine();
  test_completed_fence_is_monotonic_and_queryable();
  test_render_progress_separates_queued_active_and_completed();
  test_render_progress_rejects_stale_and_invalid_engine_fences();
  test_idle_preemption_requires_acknowledgement();
  test_failed_preemption_delivery_can_be_retried();
  test_claimed_preemption_cannot_be_aborted_or_reported_twice();
  test_idle_boundary_preemption_blocks_dispatch_and_notifies_once();
  test_active_dma_must_reach_its_boundary_before_notification();
  test_waiting_preemption_rejects_completion_past_active_boundary();
  test_preemption_snapshot_must_describe_a_reachable_boundary();
  test_idle_preemption_discards_queued_work_before_notification();
  test_reset_reports_last_aborted_fence_and_restores_progress();
  test_reset_without_active_packet_reports_completed_boundary();
  test_preempted_paging_fence_replays_once_without_reordering();
  test_engine_reset_keeps_unexecuted_fences_resubmittable();
  test_aborted_preemption_cannot_replay_discarded_fence();
  test_next_preemption_inside_claim_commit_window();
  test_unkicked_fence_runs_at_depth_one_after_the_active_fence();
  test_unkick_during_preemption_hands_the_fence_back();
  test_unkick_is_refused_in_a_foreign_preemption_phase();
  return 0;
}
