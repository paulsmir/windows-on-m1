#include "apple_agx_submission.h"

#include <assert.h>

static APPLE_AGX_SUBMISSION describe(unsigned int fence,
                                      unsigned int flags,
                                      unsigned int start,
                                      unsigned int end) {
  APPLE_AGX_SUBMISSION submission;
  assert(AppleAgxSubmissionDescribe(fence, 0u, 0u, 1u, 0x100000ULL,
                                    0x1000u, start, end, flags,
                                    &submission));
  return submission;
}

static void test_gdi_interval_is_pointer_free_and_bounded(void) {
  APPLE_AGX_SUBMISSION submission = describe(7u, 0u, 0x100u, 0x300u);
  assert(submission.Kind == AppleAgxSubmissionGdi);
  assert(submission.DmaAddress == 0x100100ULL);
  assert(submission.DmaBytes == 0x200u);
  assert(submission.Fence == 7u);
  assert(!AppleAgxSubmissionDescribe(7u, 0u, 0u, 1u, 0x100000ULL,
                                     0x1000u, 0x300u, 0x200u, 0u,
                                     &submission));
  assert(!AppleAgxSubmissionDescribe(7u, 0u, 0u, 1u, ~0ULL - 0x10ULL,
                                     0x1000u, 0x20u, 0x30u, 0u,
                                     &submission));
}

static void test_physical_engine_system_dma_interval_is_supported(void) {
  APPLE_AGX_SUBMISSION submission;

  assert(AppleAgxSubmissionDescribe(8u, 0u, 0u, 0u, 0x200000ULL,
                                    0x1000u, 0x80u, 0x180u, 0u,
                                    &submission));
  assert(submission.Kind == AppleAgxSubmissionGdi);
  assert(submission.SegmentId == 0u);
  assert(submission.DmaAddress == 0x200080ULL);
  assert(submission.DmaBytes == 0x100u);
  assert(!AppleAgxSubmissionDescribe(8u, 0u, 0u, 0u, 0ULL, 0x1000u,
                                     0x80u, 0x180u, 0u, &submission));
}

static void test_zero_length_context_switch_is_the_only_empty_work(void) {
  APPLE_AGX_SUBMISSION submission;
  assert(AppleAgxSubmissionDescribe(
      1u, 0u, 0u, 0u, 0ULL, 0u, 0u, 0u,
      APPLE_AGX_SUBMISSION_FLAG_CONTEXT_SWITCH, &submission));
  assert(submission.Kind == AppleAgxSubmissionContextSwitch);
  assert(!AppleAgxSubmissionDescribe(1u, 0u, 0u, 0u, 0ULL, 0u, 0u, 0u,
                                     0u, &submission));
  assert(!AppleAgxSubmissionDescribe(
      1u, 0u, 0u, 1u, 0x100000ULL, 0x100u, 0u, 1u,
      APPLE_AGX_SUBMISSION_FLAG_CONTEXT_SWITCH, &submission));
}

static void test_flags_select_truthful_submission_kind(void) {
  APPLE_AGX_SUBMISSION submission =
      describe(2u, APPLE_AGX_SUBMISSION_FLAG_PAGING, 0u, 0x40u);
  assert(submission.Kind == AppleAgxSubmissionPaging);
  submission = describe(3u, APPLE_AGX_SUBMISSION_FLAG_NULL_RENDERING,
                        0u, 0x40u);
  assert(submission.Kind == AppleAgxSubmissionNull);
  assert(!AppleAgxSubmissionDescribe(4u, 0u, 0u, 1u, 0x100000ULL,
                                     0x100u, 0u, 0x40u, 1u << 31,
                                     &submission));
}

static void test_only_no_effect_work_may_complete_in_software(void) {
  APPLE_AGX_SUBMISSION submission;

  submission = describe(10u, APPLE_AGX_SUBMISSION_FLAG_NULL_RENDERING,
                        0u, 0x40u);
  assert(AppleAgxSubmissionMayCompleteInSoftware(&submission));

  assert(AppleAgxSubmissionDescribe(
      11u, 0u, 0u, 0u, 0ULL, 0u, 0u, 0u,
      APPLE_AGX_SUBMISSION_FLAG_CONTEXT_SWITCH, &submission));
  assert(AppleAgxSubmissionMayCompleteInSoftware(&submission));

  submission = describe(12u, 0u, 0u, 0x40u);
  assert(!AppleAgxSubmissionMayCompleteInSoftware(&submission));
  submission = describe(13u, APPLE_AGX_SUBMISSION_FLAG_PAGING,
                        0u, 0x40u);
  assert(!AppleAgxSubmissionMayCompleteInSoftware(&submission));
  assert(!AppleAgxSubmissionMayCompleteInSoftware(0));
}

static void test_queue_owns_required_dma_depth_in_fifo_order(void) {
  APPLE_AGX_SUBMISSION_QUEUE queue;
  APPLE_AGX_SUBMISSION_ENTRY entries[3];
  APPLE_AGX_SUBMISSION_ENTRY current;
  APPLE_AGX_SUBMISSION first = describe(7u, 0u, 0u, 0x40u);
  APPLE_AGX_SUBMISSION second = describe(9u, 0u, 0u, 0x40u);
  APPLE_AGX_SUBMISSION third = describe(10u, 0u, 0u, 0x40u);

  assert(AppleAgxSubmissionQueueInitialize(&queue, entries, 3u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &first, 0x1000u, 8192u,
                                       0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &second, 0x2000u, 8192u,
                                       0u, 256u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &third, 0x3000u, 8192u,
                                       0u, 512u, 0u, 0x40u));
  assert(!AppleAgxSubmissionQueueAccept(&queue, &third, 0x4000u, 8192u,
                                        0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueuePeek(&queue, &current));
  assert(current.Submission.Fence == 7u);
  assert(current.PrivateDataToken == 0x1000u);
  assert(current.PrivateDataEnd == 128u);
  assert(current.DmaSubmissionStart == 0u);
  assert(current.DmaSubmissionEnd == 0x40u);
  assert(!AppleAgxSubmissionQueueComplete(&queue, 6u));
  assert(AppleAgxSubmissionQueueComplete(&queue, 7u));
  assert(queue.CompletedFence == 7u);
  assert(AppleAgxSubmissionQueuePeek(&queue, &current));
  assert(current.Submission.Fence == 9u);
  assert(AppleAgxSubmissionQueueComplete(&queue, 9u));
  assert(AppleAgxSubmissionQueueComplete(&queue, 10u));
  assert(!AppleAgxSubmissionQueuePeek(&queue, &current));
  assert(queue.Count == 0u);
}

static void test_fence_order_is_wrap_safe(void) {
  APPLE_AGX_SUBMISSION_QUEUE queue;
  APPLE_AGX_SUBMISSION_ENTRY entries[2];
  APPLE_AGX_SUBMISSION nearWrap = describe(0xffffffffu, 0u, 0u, 0x40u);
  APPLE_AGX_SUBMISSION afterWrap = describe(1u, 0u, 0u, 0x40u);
  APPLE_AGX_SUBMISSION stale = describe(0xfffffff0u, 0u, 0u, 0x40u);

  assert(AppleAgxSubmissionQueueInitialize(&queue, entries, 2u));
  queue.CompletedFence = 0xfffffffeu;
  queue.LastSubmittedFence = queue.CompletedFence;
  assert(AppleAgxSubmissionQueueAccept(&queue, &nearWrap, 0x1000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueComplete(&queue, 0xffffffffu));
  assert(!AppleAgxSubmissionQueueAccept(&queue, &stale, 0x1000u, 128u,
                                        0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &afterWrap, 0x1000u, 128u,
                                       0u, 128u, 0u, 0x40u));
}

static void test_queue_rejects_invalid_private_range_without_ownership(void) {
  APPLE_AGX_SUBMISSION_QUEUE queue;
  APPLE_AGX_SUBMISSION_ENTRY entry;
  APPLE_AGX_SUBMISSION submission = describe(1u, 0u, 0u, 0x40u);

  assert(AppleAgxSubmissionQueueInitialize(&queue, &entry, 1u));
  assert(!AppleAgxSubmissionQueueAccept(&queue, &submission, 0x1000u,
                                        32u, 24u, 40u, 0u, 0x40u));
  assert(!AppleAgxSubmissionQueueAccept(&queue, &submission, 0u, 32u,
                                        0u, 16u, 0u, 0x40u));
  assert(!AppleAgxSubmissionQueueAccept(&queue, &submission, 0x1000u,
                                        32u, 0u, 16u, 4u, 0x40u));
  assert(queue.Count == 0u);
  assert(!AppleAgxSubmissionQueueAccept(&queue, &submission, 0u, 0u, 0u,
                                        0u, 0u, 0x40u));
}

static void test_completion_claim_preserves_fence_until_notification(void) {
  APPLE_AGX_SUBMISSION_QUEUE queue;
  APPLE_AGX_SUBMISSION_ENTRY entries[2];
  APPLE_AGX_SUBMISSION_ENTRY current;
  APPLE_AGX_SUBMISSION first = describe(21u, 0u, 0u, 0x40u);
  APPLE_AGX_SUBMISSION second = describe(22u, 0u, 0u, 0x40u);

  assert(AppleAgxSubmissionQueueInitialize(&queue, entries, 2u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &first, 0x1000u, 128u, 0u,
                                       128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &second, 0x1000u, 128u, 0u,
                                       128u, 0u, 0x40u));
  assert(!AppleAgxSubmissionQueueClaimCompletion(&queue, 22u));
  assert(AppleAgxSubmissionQueueClaimCompletion(&queue, 21u));
  assert(!AppleAgxSubmissionQueueClaimCompletion(&queue, 21u));
  assert(!AppleAgxSubmissionQueueComplete(&queue, 21u));
  assert(!AppleAgxSubmissionQueueFault(&queue, 21u));
  assert(AppleAgxSubmissionQueuePeek(&queue, &current));
  assert(current.Submission.Fence == 21u);
  assert(queue.CompletedFence == 0u);
  assert(AppleAgxSubmissionQueueReleaseCompletion(&queue, 21u));
  assert(AppleAgxSubmissionQueuePeek(&queue, &current));
  assert(queue.Count == 2u);
  assert(AppleAgxSubmissionQueueClaimCompletion(&queue, 21u));
  assert(AppleAgxSubmissionQueueCommitCompletion(&queue, 21u));
  assert(queue.CompletedFence == 21u);
  assert(queue.Count == 1u);
  assert(AppleAgxSubmissionQueuePeek(&queue, &current));
  assert(current.Submission.Fence == 22u);
}

static void test_claimed_retirement_removes_exact_head_without_completion(void) {
  APPLE_AGX_SUBMISSION_QUEUE queue;
  APPLE_AGX_SUBMISSION_ENTRY entries[1];
  APPLE_AGX_SUBMISSION submission = describe(23u, 0u, 0u, 0x40u);

  assert(AppleAgxSubmissionQueueInitialize(&queue, entries, 1u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &submission, 0x1000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueClaimCompletion(&queue, 23u));
  assert(AppleAgxSubmissionQueueCommitRetirement(&queue, 23u));
  assert(queue.Count == 0u);
  assert(queue.CompletedFence == 0u);
  assert(queue.ClaimedCompletionFence == 0u);
  assert(!AppleAgxSubmissionQueueCommitRetirement(&queue, 23u));
}

static void test_preemption_discards_only_never_published_entries_through_cutoff(
    void) {
  APPLE_AGX_SUBMISSION_QUEUE queue;
  APPLE_AGX_SUBMISSION_ENTRY entries[3];
  APPLE_AGX_SUBMISSION_ENTRY current;
  APPLE_AGX_U32 discarded = 99u;
  APPLE_AGX_SUBMISSION first = describe(10u, 0u, 0u, 0x40u);
  APPLE_AGX_SUBMISSION cutoff = describe(11u, 0u, 0u, 0x40u);
  APPLE_AGX_SUBMISSION later = describe(15u, 0u, 0u, 0x40u);

  assert(AppleAgxSubmissionQueueInitialize(&queue, entries, 3u));
  queue.CompletedFence = 5u;
  queue.LastSubmittedFence = 5u;
  assert(AppleAgxSubmissionQueueAccept(&queue, &first, 0x1000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &cutoff, 0x2000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &later, 0x3000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueDiscardPreemptedThrough(
      &queue, 11u, &discarded));
  assert(discarded == 2u);
  assert(queue.CompletedFence == 5u);
  assert(queue.Count == 1u);
  assert(AppleAgxSubmissionQueuePagingReplayDebt(&queue) == 0u);
  assert(!AppleAgxSubmissionQueueReplayBlocked(&queue));
  assert(AppleAgxSubmissionQueuePeek(&queue, &current));
  assert(current.Submission.Fence == 15u);
  assert(current.PrivateDataToken == 0x3000u);
  assert(entries[0].Submission.Fence == 0u);
  assert(entries[0].PrivateDataToken == 0u);
  assert(entries[1].Submission.Fence == 0u);
  assert(entries[1].PrivateDataToken == 0u);
}

static void test_published_dma_must_complete_before_preemption_discard(void) {
  APPLE_AGX_SUBMISSION_QUEUE queue;
  APPLE_AGX_SUBMISSION_ENTRY entries[3];
  APPLE_AGX_SUBMISSION_ENTRY current;
  APPLE_AGX_U32 discarded = 99u;
  APPLE_AGX_SUBMISSION active = describe(20u, 0u, 0u, 0x40u);
  APPLE_AGX_SUBMISSION pending = describe(21u, 0u, 0u, 0x40u);
  APPLE_AGX_SUBMISSION later = describe(25u, 0u, 0u, 0x40u);

  assert(AppleAgxSubmissionQueueInitialize(&queue, entries, 3u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &active, 0x1000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &pending, 0x2000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &later, 0x3000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueMarkPublished(&queue, 20u));
  assert(!AppleAgxSubmissionQueueDiscardPreemptedThrough(
      &queue, 21u, &discarded));
  assert(queue.Count == 3u);
  assert(AppleAgxSubmissionQueueClaimCompletion(&queue, 20u));
  assert(AppleAgxSubmissionQueueCommitCompletion(&queue, 20u));
  assert(AppleAgxSubmissionQueueDiscardPreemptedThrough(
      &queue, 21u, &discarded));
  assert(discarded == 1u);
  assert(queue.CompletedFence == 20u);
  assert(queue.Count == 1u);
  assert(AppleAgxSubmissionQueuePeek(&queue, &current));
  assert(current.Submission.Fence == 25u);
}

static void test_preemption_discard_rejects_stale_cutoff(void) {
  APPLE_AGX_SUBMISSION_QUEUE queue;
  APPLE_AGX_SUBMISSION_ENTRY entry;
  APPLE_AGX_U32 discarded = 99u;

  assert(AppleAgxSubmissionQueueInitialize(&queue, &entry, 1u));
  queue.CompletedFence = 10u;
  queue.LastSubmittedFence = 10u;
  assert(!AppleAgxSubmissionQueueDiscardPreemptedThrough(
      &queue, 9u, &discarded));
  assert(discarded == 99u);
  assert(queue.CompletedFence == 10u);
}

static void test_preempted_paging_fences_replay_before_preserved_work(void) {
  APPLE_AGX_SUBMISSION_QUEUE queue;
  APPLE_AGX_SUBMISSION_ENTRY entries[4];
  APPLE_AGX_U32 replayFences[4];
  APPLE_AGX_SUBMISSION_ENTRY current;
  APPLE_AGX_U32 discarded = 0u;
  APPLE_AGX_SUBMISSION paging10 =
      describe(10u, APPLE_AGX_SUBMISSION_FLAG_PAGING, 0u, 0x40u);
  APPLE_AGX_SUBMISSION paging11 =
      describe(11u, APPLE_AGX_SUBMISSION_FLAG_PAGING, 0u, 0x40u);
  APPLE_AGX_SUBMISSION later = describe(15u, 0u, 0u, 0x40u);
  APPLE_AGX_SUBMISSION newWork = describe(16u, 0u, 0u, 0x40u);
  APPLE_AGX_SUBMISSION replay11 = describe(
      11u, APPLE_AGX_SUBMISSION_FLAG_PAGING |
               APPLE_AGX_SUBMISSION_FLAG_RESUBMISSION,
      0u, 0x40u);
  APPLE_AGX_SUBMISSION replay10 = describe(
      10u, APPLE_AGX_SUBMISSION_FLAG_PAGING |
               APPLE_AGX_SUBMISSION_FLAG_RESUBMISSION,
      0u, 0x40u);
  APPLE_AGX_SUBMISSION staleReplay = describe(
      5u, APPLE_AGX_SUBMISSION_FLAG_PAGING |
              APPLE_AGX_SUBMISSION_FLAG_RESUBMISSION,
      0u, 0x40u);

  assert(AppleAgxSubmissionQueueInitialize(&queue, entries, 4u));
  assert(AppleAgxSubmissionQueueConfigurePagingReplay(
      &queue, replayFences, 4u));
  queue.CompletedFence = 5u;
  queue.LastSubmittedFence = 5u;
  assert(AppleAgxSubmissionQueueAccept(&queue, &paging10, 0x1000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &paging11, 0x2000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &later, 0x3000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueDiscardPreemptedThrough(
      &queue, 11u, &discarded));
  assert(discarded == 2u);
  assert(queue.LastSubmittedFence == 15u);
  assert(AppleAgxSubmissionQueuePagingReplayDebt(&queue) == 2u);
  assert(AppleAgxSubmissionQueueReplayBlocked(&queue));
  assert(!AppleAgxSubmissionQueueMarkPublished(&queue, 15u));
  assert(!AppleAgxSubmissionQueueAccept(&queue, &newWork, 0x7000u, 128u,
                                        0u, 128u, 0u, 0x40u));

  /* Only the exact original order can release preserved later work. */
  assert(!AppleAgxSubmissionQueueAccept(&queue, &replay11, 0x4000u, 128u,
                                        0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &replay10, 0x5000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(!AppleAgxSubmissionQueueAccept(&queue, &replay10, 0x6000u, 128u,
                                        0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueuePagingReplayDebt(&queue) == 1u);
  assert(AppleAgxSubmissionQueueReplayBlocked(&queue));
  assert(AppleAgxSubmissionQueueAccept(&queue, &replay11, 0x4000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(!AppleAgxSubmissionQueueAccept(&queue, &staleReplay, 0x6000u,
                                        128u, 0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueuePagingReplayDebt(&queue) == 0u);
  assert(!AppleAgxSubmissionQueueReplayBlocked(&queue));
  assert(queue.LastSubmittedFence == 15u);
  assert(AppleAgxSubmissionQueuePeek(&queue, &current));
  assert(current.Submission.Fence == 10u);
  assert(AppleAgxSubmissionQueueComplete(&queue, 10u));
  assert(AppleAgxSubmissionQueuePeek(&queue, &current));
  assert(current.Submission.Fence == 11u);
  assert(AppleAgxSubmissionQueueComplete(&queue, 11u));
  assert(AppleAgxSubmissionQueuePeek(&queue, &current));
  assert(current.Submission.Fence == 15u);
}

static void test_only_quiesced_reset_clears_paging_replay_debt(void) {
  APPLE_AGX_SUBMISSION_QUEUE queue;
  APPLE_AGX_SUBMISSION_ENTRY entries[2];
  APPLE_AGX_U32 replayFences[2] = {99u, 99u};
  APPLE_AGX_U32 discarded = 0u;
  APPLE_AGX_SUBMISSION paging =
      describe(20u, APPLE_AGX_SUBMISSION_FLAG_PAGING, 0u, 0x40u);
  APPLE_AGX_SUBMISSION later = describe(21u, 0u, 0u, 0x40u);

  assert(AppleAgxSubmissionQueueInitialize(&queue, entries, 2u));
  assert(AppleAgxSubmissionQueueConfigurePagingReplay(
      &queue, replayFences, 2u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &paging, 0x1000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueAccept(&queue, &later, 0x2000u, 128u,
                                       0u, 128u, 0u, 0x40u));
  assert(AppleAgxSubmissionQueueDiscardPreemptedThrough(
      &queue, 20u, &discarded));
  assert(discarded == 1u);
  assert(AppleAgxSubmissionQueueReplayBlocked(&queue));
  assert(!AppleAgxSubmissionQueueResetPagingReplay(
      &queue, APPLE_AGX_FALSE));
  assert(AppleAgxSubmissionQueueReplayBlocked(&queue));
  assert(AppleAgxSubmissionQueueResetPagingReplay(
      &queue, APPLE_AGX_TRUE));
  assert(!AppleAgxSubmissionQueueReplayBlocked(&queue));
  assert(replayFences[0] == 0u);
  assert(replayFences[1] == 0u);
  assert(AppleAgxSubmissionQueueMarkPublished(&queue, 21u));
}

static void test_completion_transaction_retains_every_irreversible_receipt(void) {
  APPLE_AGX_COMPLETION_TRANSACTION transaction;

  AppleAgxCompletionTransactionInitialize(&transaction);
  assert(transaction.Phase == AppleAgxCompletionIdle);
  assert(AppleAgxCompletionTransactionBegin(&transaction, 31u, 0u, 0u));
  assert(transaction.Phase == AppleAgxCompletionClaimed);
  assert(AppleAgxCompletionTransactionMatches(&transaction, 31u, 0u, 0u));
  assert(!AppleAgxCompletionTransactionMatches(&transaction, 32u, 0u, 0u));

  /* Every fallible local step precedes the irreversible Windows report. */
  assert(AppleAgxCompletionTransactionAdvance(
      &transaction, 31u, 0u, 0u, AppleAgxCompletionClaimed,
      AppleAgxCompletionSchedulerCommitted));
  assert(AppleAgxCompletionTransactionAdvance(
      &transaction, 31u, 0u, 0u, AppleAgxCompletionSchedulerCommitted,
      AppleAgxCompletionLocalCommitted));
  assert(transaction.Phase == AppleAgxCompletionLocalCommitted);
  assert(!AppleAgxCompletionTransactionBegin(&transaction, 32u, 0u, 0u));
  assert(AppleAgxCompletionTransactionAdvance(
      &transaction, 31u, 0u, 0u, AppleAgxCompletionLocalCommitted,
      AppleAgxCompletionBackendRetired));
  assert(transaction.Phase == AppleAgxCompletionBackendRetired);
  assert(AppleAgxCompletionTransactionCanReport(
      &transaction, 31u, 0u, 0u));
  assert(!AppleAgxCompletionTransactionCanReport(
      &transaction, 32u, 0u, 0u));
  AppleAgxCompletionTransactionMarkReported(&transaction);
  assert(transaction.Phase == AppleAgxCompletionReported);
  assert(AppleAgxCompletionTransactionFinish(&transaction, 31u, 0u, 0u));
  assert(transaction.Phase == AppleAgxCompletionIdle);
  assert(!AppleAgxCompletionTransactionFinish(&transaction, 31u, 0u, 0u));
}

int main(void) {
  test_gdi_interval_is_pointer_free_and_bounded();
  test_physical_engine_system_dma_interval_is_supported();
  test_zero_length_context_switch_is_the_only_empty_work();
  test_flags_select_truthful_submission_kind();
  test_only_no_effect_work_may_complete_in_software();
  test_queue_owns_required_dma_depth_in_fifo_order();
  test_fence_order_is_wrap_safe();
  test_queue_rejects_invalid_private_range_without_ownership();
  test_completion_claim_preserves_fence_until_notification();
  test_claimed_retirement_removes_exact_head_without_completion();
  test_preemption_discards_only_never_published_entries_through_cutoff();
  test_published_dma_must_complete_before_preemption_discard();
  test_preemption_discard_rejects_stale_cutoff();
  test_preempted_paging_fences_replay_before_preserved_work();
  test_only_quiesced_reset_clears_paging_replay_debt();
  test_completion_transaction_retains_every_irreversible_receipt();
  return 0;
}
