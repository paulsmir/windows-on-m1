#ifndef APPLE_AGX_WORK_QUEUE_H
#define APPLE_AGX_WORK_QUEUE_H

#include "apple_agx_submission.h"

#define APPLE_AGX_WORK_QUEUE_RING_CAPACITY 0x500u
#define APPLE_AGX_RUN_QUEUE_MESSAGE_SIZE 0x30u

typedef enum _APPLE_AGX_WORK_QUEUE_TYPE {
  AppleAgxWorkQueueTa = 0,
  AppleAgxWorkQueue3d = 1,
  AppleAgxWorkQueueCompute = 2,
} APPLE_AGX_WORK_QUEUE_TYPE;

/*
 * A prepared item is an immutable receipt for the memory writes which must be
 * visible to firmware before CPU_WPTR is advanced and the queue is rung.
 */
typedef struct _APPLE_AGX_WORK_QUEUE_ITEM {
  APPLE_AGX_U64 WorkCommandAddress;
  APPLE_AGX_U32 Fence;
  APPLE_AGX_U32 ExpectedStamp;
  APPLE_AGX_U32 RingIndex;
  APPLE_AGX_U32 NextWritePointer;
  APPLE_AGX_U32 QueueType;
  APPLE_AGX_U32 EventNumber;
  APPLE_AGX_U32 NewQueue;
} APPLE_AGX_WORK_QUEUE_ITEM;

typedef struct _APPLE_AGX_WORK_QUEUE {
  APPLE_AGX_U64 CommandQueueAddress;
  APPLE_AGX_U32 Capacity;
  APPLE_AGX_U32 CpuWritePointer;
  APPLE_AGX_U32 GpuDonePointer;
  APPLE_AGX_U32 QueueType;
  APPLE_AGX_U32 EventNumber;
  APPLE_AGX_U32 PendingFence;
  APPLE_AGX_U32 ExpectedStamp;
  APPLE_AGX_BOOL FirstRun;
  APPLE_AGX_BOOL PendingNewQueue;
  APPLE_AGX_BOOL HasPending;
  APPLE_AGX_BOOL Faulted;
} APPLE_AGX_WORK_QUEUE;

APPLE_AGX_BOOL AppleAgxWorkQueueInitialize(
    APPLE_AGX_WORK_QUEUE *Queue, APPLE_AGX_U32 QueueType,
    APPLE_AGX_U32 EventNumber, APPLE_AGX_U64 CommandQueueAddress,
    APPLE_AGX_U32 Capacity);
APPLE_AGX_BOOL AppleAgxWorkQueuePrepare(
    const APPLE_AGX_WORK_QUEUE *Queue,
    const APPLE_AGX_SUBMISSION *Submission,
    APPLE_AGX_U64 WorkCommandAddress, APPLE_AGX_U32 ExpectedStamp,
    APPLE_AGX_WORK_QUEUE_ITEM *Item);
APPLE_AGX_BOOL AppleAgxWorkQueueCommit(
    APPLE_AGX_WORK_QUEUE *Queue, const APPLE_AGX_WORK_QUEUE_ITEM *Item,
    APPLE_AGX_U64 *RingSlots, APPLE_AGX_U32 RingSlotCount);
APPLE_AGX_BOOL AppleAgxWorkQueueCanCommit(
    const APPLE_AGX_WORK_QUEUE *Queue,
    const APPLE_AGX_WORK_QUEUE_ITEM *Item,
    APPLE_AGX_U32 RingSlotCount);
APPLE_AGX_BOOL AppleAgxWorkQueueEncodeRunMessageG13V13_5(
    const APPLE_AGX_WORK_QUEUE *Queue,
    unsigned char Message[APPLE_AGX_RUN_QUEUE_MESSAGE_SIZE]);
APPLE_AGX_BOOL AppleAgxWorkQueueObserveCompletion(
    APPLE_AGX_WORK_QUEUE *Queue, APPLE_AGX_U32 EventNumber,
    APPLE_AGX_U32 ObservedStamp, APPLE_AGX_U32 GpuDonePointer,
    APPLE_AGX_U32 *CompletedFence);
APPLE_AGX_BOOL AppleAgxWorkQueueAbort(
    APPLE_AGX_WORK_QUEUE *Queue, APPLE_AGX_U32 Fence);

#endif /* APPLE_AGX_WORK_QUEUE_H */
