#include "apple_agx_work_queue.h"

#define APPLE_AGX_WORK_QUEUE_NULL ((void *)0)

static void AppleAgxPutU32(unsigned char *Destination, APPLE_AGX_U32 Value) {
  Destination[0] = (unsigned char)(Value & 0xffu);
  Destination[1] = (unsigned char)((Value >> 8) & 0xffu);
  Destination[2] = (unsigned char)((Value >> 16) & 0xffu);
  Destination[3] = (unsigned char)((Value >> 24) & 0xffu);
}

static void AppleAgxPutU64(unsigned char *Destination, APPLE_AGX_U64 Value) {
  AppleAgxPutU32(Destination, (APPLE_AGX_U32)Value);
  AppleAgxPutU32(Destination + 4, (APPLE_AGX_U32)(Value >> 32));
}

static APPLE_AGX_BOOL AppleAgxStampReached(APPLE_AGX_U32 Observed,
                                           APPLE_AGX_U32 Expected) {
  return (APPLE_AGX_U32)(Observed - Expected) < 0x80000000u
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

static void AppleAgxWorkQueueClearPending(APPLE_AGX_WORK_QUEUE *Queue) {
  Queue->PendingFence = 0u;
  Queue->ExpectedStamp = 0u;
  Queue->PendingNewQueue = APPLE_AGX_FALSE;
  Queue->HasPending = APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxWorkQueueInitialize(
    APPLE_AGX_WORK_QUEUE *Queue, APPLE_AGX_U32 QueueType,
    APPLE_AGX_U32 EventNumber, APPLE_AGX_U64 CommandQueueAddress,
    APPLE_AGX_U32 Capacity) {
  if (Queue == APPLE_AGX_WORK_QUEUE_NULL ||
      QueueType > (APPLE_AGX_U32)AppleAgxWorkQueueCompute ||
      EventNumber >= 128u || CommandQueueAddress == 0ULL ||
      (CommandQueueAddress & 7ULL) != 0ULL || Capacity < 2u ||
      Capacity > APPLE_AGX_WORK_QUEUE_RING_CAPACITY)
    return APPLE_AGX_FALSE;

  Queue->CommandQueueAddress = CommandQueueAddress;
  Queue->Capacity = Capacity;
  Queue->CpuWritePointer = 0u;
  Queue->GpuDonePointer = 0u;
  Queue->QueueType = QueueType;
  Queue->EventNumber = EventNumber;
  Queue->FirstRun = APPLE_AGX_TRUE;
  Queue->Faulted = APPLE_AGX_FALSE;
  AppleAgxWorkQueueClearPending(Queue);
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxWorkQueuePrepare(
    const APPLE_AGX_WORK_QUEUE *Queue,
    const APPLE_AGX_SUBMISSION *Submission,
    APPLE_AGX_U64 WorkCommandAddress, APPLE_AGX_U32 ExpectedStamp,
    APPLE_AGX_WORK_QUEUE_ITEM *Item) {
  APPLE_AGX_U32 next;

  if (Queue == APPLE_AGX_WORK_QUEUE_NULL ||
      Submission == APPLE_AGX_WORK_QUEUE_NULL ||
      Item == APPLE_AGX_WORK_QUEUE_NULL || Queue->HasPending ||
      Queue->Faulted ||
      Queue->Capacity < 2u || Queue->CpuWritePointer >= Queue->Capacity ||
      Queue->GpuDonePointer >= Queue->Capacity ||
      Submission->Kind != AppleAgxSubmissionGdi ||
      Submission->Fence == 0u || WorkCommandAddress == 0ULL ||
      (WorkCommandAddress & 0x1fULL) != 0ULL || ExpectedStamp == 0u)
    return APPLE_AGX_FALSE;

  next = Queue->CpuWritePointer + 1u;
  if (next == Queue->Capacity)
    next = 0u;
  if (next == Queue->GpuDonePointer)
    return APPLE_AGX_FALSE;

  Item->WorkCommandAddress = WorkCommandAddress;
  Item->Fence = Submission->Fence;
  Item->ExpectedStamp = ExpectedStamp;
  Item->RingIndex = Queue->CpuWritePointer;
  Item->NextWritePointer = next;
  Item->QueueType = Queue->QueueType;
  Item->EventNumber = Queue->EventNumber;
  Item->NewQueue = Queue->FirstRun ? 1u : 0u;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxWorkQueueCanCommit(
    const APPLE_AGX_WORK_QUEUE *Queue,
    const APPLE_AGX_WORK_QUEUE_ITEM *Item,
    APPLE_AGX_U32 RingSlotCount) {
  APPLE_AGX_U32 expectedNext;

  if (Queue == APPLE_AGX_WORK_QUEUE_NULL || Queue->Capacity < 2u ||
      Queue->CpuWritePointer >= Queue->Capacity)
    return APPLE_AGX_FALSE;
  expectedNext = Queue->CpuWritePointer + 1u;
  if (expectedNext == Queue->Capacity)
    expectedNext = 0u;
  if (Item == APPLE_AGX_WORK_QUEUE_NULL || Queue->HasPending ||
      Queue->Faulted ||
      RingSlotCount != Queue->Capacity ||
      Item->RingIndex != Queue->CpuWritePointer ||
      Item->RingIndex >= RingSlotCount || Item->QueueType != Queue->QueueType ||
      Item->EventNumber != Queue->EventNumber ||
      Item->NextWritePointer != expectedNext ||
      expectedNext == Queue->GpuDonePointer ||
      Item->NewQueue != (Queue->FirstRun ? 1u : 0u) ||
      Item->Fence == 0u || Item->ExpectedStamp == 0u ||
      Item->WorkCommandAddress == 0ULL ||
      (Item->WorkCommandAddress & 0x1fULL) != 0ULL)
    return APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxWorkQueueCommit(
    APPLE_AGX_WORK_QUEUE *Queue, const APPLE_AGX_WORK_QUEUE_ITEM *Item,
    APPLE_AGX_U64 *RingSlots, APPLE_AGX_U32 RingSlotCount) {
  if (RingSlots == APPLE_AGX_WORK_QUEUE_NULL ||
      !AppleAgxWorkQueueCanCommit(Queue, Item, RingSlotCount))
    return APPLE_AGX_FALSE;

  RingSlots[Item->RingIndex] = Item->WorkCommandAddress;
  Queue->CpuWritePointer = Item->NextWritePointer;
  Queue->PendingFence = Item->Fence;
  Queue->ExpectedStamp = Item->ExpectedStamp;
  Queue->PendingNewQueue = Item->NewQueue ? APPLE_AGX_TRUE : APPLE_AGX_FALSE;
  Queue->FirstRun = APPLE_AGX_FALSE;
  Queue->HasPending = APPLE_AGX_TRUE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxWorkQueueEncodeRunMessageG13V13_5(
    const APPLE_AGX_WORK_QUEUE *Queue,
    unsigned char Message[APPLE_AGX_RUN_QUEUE_MESSAGE_SIZE]) {
  APPLE_AGX_U32 index;

  if (Queue == APPLE_AGX_WORK_QUEUE_NULL ||
      Message == APPLE_AGX_WORK_QUEUE_NULL || !Queue->HasPending ||
      Queue->Faulted ||
      Queue->CommandQueueAddress == 0ULL)
    return APPLE_AGX_FALSE;

  for (index = 0u; index < APPLE_AGX_RUN_QUEUE_MESSAGE_SIZE; ++index)
    Message[index] = 0u;
  AppleAgxPutU32(Message + 0x00u, Queue->QueueType);
  AppleAgxPutU64(Message + 0x04u, Queue->CommandQueueAddress);
  AppleAgxPutU32(Message + 0x0cu, Queue->CpuWritePointer);
  AppleAgxPutU32(Message + 0x10u, Queue->EventNumber);
  AppleAgxPutU32(Message + 0x14u, Queue->PendingNewQueue ? 1u : 0u);
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxWorkQueueObserveCompletion(
    APPLE_AGX_WORK_QUEUE *Queue, APPLE_AGX_U32 EventNumber,
    APPLE_AGX_U32 ObservedStamp, APPLE_AGX_U32 GpuDonePointer,
    APPLE_AGX_U32 *CompletedFence) {
  if (Queue == APPLE_AGX_WORK_QUEUE_NULL ||
      CompletedFence == APPLE_AGX_WORK_QUEUE_NULL || !Queue->HasPending ||
      Queue->Faulted ||
      EventNumber != Queue->EventNumber ||
      GpuDonePointer != Queue->CpuWritePointer ||
      !AppleAgxStampReached(ObservedStamp, Queue->ExpectedStamp))
    return APPLE_AGX_FALSE;

  Queue->GpuDonePointer = GpuDonePointer;
  *CompletedFence = Queue->PendingFence;
  AppleAgxWorkQueueClearPending(Queue);
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxWorkQueueAbort(
    APPLE_AGX_WORK_QUEUE *Queue, APPLE_AGX_U32 Fence) {
  if (Queue == APPLE_AGX_WORK_QUEUE_NULL || !Queue->HasPending ||
      Fence != Queue->PendingFence)
    return APPLE_AGX_FALSE;
  AppleAgxWorkQueueClearPending(Queue);
  Queue->Faulted = APPLE_AGX_TRUE;
  return APPLE_AGX_TRUE;
}
