#include "apple_agx_work_queue.h"

#include <assert.h>
#include <string.h>

static APPLE_AGX_SUBMISSION submission(unsigned int fence) {
  APPLE_AGX_SUBMISSION value;
  assert(AppleAgxSubmissionDescribe(fence, 0u, 0u, 1u, 0x100000ULL,
                                    0x1000u, 0u, 0x40u, 0u, &value));
  return value;
}

static unsigned int get_u32(const unsigned char *value) {
  return (unsigned int)value[0] | ((unsigned int)value[1] << 8) |
         ((unsigned int)value[2] << 16) | ((unsigned int)value[3] << 24);
}

static unsigned long long get_u64(const unsigned char *value) {
  return (unsigned long long)get_u32(value) |
         ((unsigned long long)get_u32(value + 4) << 32);
}

static void test_prepare_commit_and_run_message_match_m1n1(void) {
  APPLE_AGX_WORK_QUEUE queue;
  APPLE_AGX_WORK_QUEUE_ITEM item;
  APPLE_AGX_SUBMISSION work = submission(17u);
  APPLE_AGX_U64 ring[4] = {0};
  unsigned char message[APPLE_AGX_RUN_QUEUE_MESSAGE_SIZE];

  assert(AppleAgxWorkQueueInitialize(&queue, AppleAgxWorkQueue3d, 9u,
                                     0x123456780ULL, 4u));
  assert(AppleAgxWorkQueuePrepare(&queue, &work, 0x200020ULL, 0x3d000100u,
                                  &item));
  assert(item.RingIndex == 0u && item.NextWritePointer == 1u);
  assert(item.NewQueue == 1u);
  assert(AppleAgxWorkQueueCommit(&queue, &item, ring, 4u));
  assert(ring[0] == 0x200020ULL);
  assert(queue.CpuWritePointer == 1u && queue.HasPending);
  assert(AppleAgxWorkQueueEncodeRunMessageG13V13_5(&queue, message));
  assert(get_u32(message + 0x00u) == 1u);
  assert(get_u64(message + 0x04u) == 0x123456780ULL);
  assert(get_u32(message + 0x0cu) == 1u);
  assert(get_u32(message + 0x10u) == 9u);
  assert(get_u32(message + 0x14u) == 1u);
  assert(get_u64(message + 0x18u) == 0ULL);
  assert(get_u64(message + 0x20u) == 0ULL);
  assert(get_u64(message + 0x28u) == 0ULL);
}

static void test_completion_requires_event_stamp_and_done_pointer(void) {
  APPLE_AGX_WORK_QUEUE queue;
  APPLE_AGX_WORK_QUEUE_ITEM item;
  APPLE_AGX_SUBMISSION work = submission(23u);
  APPLE_AGX_U64 ring[4] = {0};
  APPLE_AGX_U32 fence = 0u;

  assert(AppleAgxWorkQueueInitialize(&queue, AppleAgxWorkQueueTa, 3u,
                                     0x400000ULL, 4u));
  assert(AppleAgxWorkQueuePrepare(&queue, &work, 0x500020ULL, 0x7a000100u,
                                  &item));
  assert(AppleAgxWorkQueueCommit(&queue, &item, ring, 4u));
  assert(!AppleAgxWorkQueueObserveCompletion(&queue, 2u, 0x7a000100u,
                                              1u, &fence));
  assert(!AppleAgxWorkQueueObserveCompletion(&queue, 3u, 0x7a000000u,
                                              1u, &fence));
  assert(!AppleAgxWorkQueueObserveCompletion(&queue, 3u, 0x7a000100u,
                                              0u, &fence));
  assert(AppleAgxWorkQueueObserveCompletion(&queue, 3u, 0x7a000100u,
                                             1u, &fence));
  assert(fence == 23u && !queue.HasPending);
}

static void test_ring_full_wrap_and_fault_are_bounded(void) {
  APPLE_AGX_WORK_QUEUE queue;
  APPLE_AGX_WORK_QUEUE_ITEM item;
  APPLE_AGX_SUBMISSION work = submission(1u);
  APPLE_AGX_U64 ring[2] = {0};

  assert(AppleAgxWorkQueueInitialize(&queue, AppleAgxWorkQueue3d, 1u,
                                     0x600000ULL, 2u));
  queue.CpuWritePointer = 1u;
  queue.GpuDonePointer = 0u;
  assert(!AppleAgxWorkQueuePrepare(&queue, &work, 0x700020ULL,
                                   0x3d000100u, &item));
  queue.GpuDonePointer = 1u;
  assert(AppleAgxWorkQueuePrepare(&queue, &work, 0x700020ULL,
                                  0x3d000100u, &item));
  assert(item.RingIndex == 1u && item.NextWritePointer == 0u);
  assert(AppleAgxWorkQueueCommit(&queue, &item, ring, 2u));
  assert(ring[1] == 0x700020ULL);
  assert(AppleAgxWorkQueueAbort(&queue, 1u));
  work = submission(2u);
  assert(!AppleAgxWorkQueuePrepare(&queue, &work, 0x800020ULL,
                                   0x3d000200u, &item));
  assert(queue.Faulted);
}

static void test_second_run_is_not_marked_new_after_completion(void) {
  APPLE_AGX_WORK_QUEUE queue;
  APPLE_AGX_WORK_QUEUE_ITEM item;
  APPLE_AGX_SUBMISSION work = submission(1u);
  APPLE_AGX_U64 ring[4] = {0};
  APPLE_AGX_U32 fence = 0u;

  assert(AppleAgxWorkQueueInitialize(&queue, AppleAgxWorkQueue3d, 1u,
                                     0x600000ULL, 4u));
  assert(AppleAgxWorkQueuePrepare(&queue, &work, 0x700020ULL,
                                  0x3d000100u, &item));
  assert(item.NewQueue == 1u);
  assert(AppleAgxWorkQueueCommit(&queue, &item, ring, 4u));
  assert(AppleAgxWorkQueueObserveCompletion(&queue, 1u, 0x3d000100u,
                                             1u, &fence));
  work = submission(2u);
  assert(AppleAgxWorkQueuePrepare(&queue, &work, 0x800020ULL,
                                  0x3d000200u, &item));
  assert(item.NewQueue == 0u);
}

static void test_rejects_untruthful_or_unaligned_work(void) {
  APPLE_AGX_WORK_QUEUE queue;
  APPLE_AGX_WORK_QUEUE_ITEM item;
  APPLE_AGX_SUBMISSION work = submission(1u);

  assert(!AppleAgxWorkQueueInitialize(&queue, 3u, 0u, 0x1000ULL, 2u));
  assert(AppleAgxWorkQueueInitialize(&queue, AppleAgxWorkQueue3d, 0u,
                                     0x1000ULL, 2u));
  assert(!AppleAgxWorkQueuePrepare(&queue, &work, 0x200001ULL,
                                   0x3d000100u, &item));
  work.Kind = AppleAgxSubmissionPaging;
  assert(!AppleAgxWorkQueuePrepare(&queue, &work, 0x200020ULL,
                                   0x3d000100u, &item));
}

int main(void) {
  test_prepare_commit_and_run_message_match_m1n1();
  test_completion_requires_event_stamp_and_done_pointer();
  test_ring_full_wrap_and_fault_are_bounded();
  test_second_run_is_not_marked_new_after_completion();
  test_rejects_untruthful_or_unaligned_work();
  return 0;
}
