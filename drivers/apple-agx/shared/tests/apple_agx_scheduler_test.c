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

int main(void) {
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
  return 0;
}
