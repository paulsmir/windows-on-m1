#ifndef APPLE_AGX_SUBMISSION_H
#define APPLE_AGX_SUBMISSION_H

#include "apple_agx_state.h"

#define APPLE_AGX_SUBMISSION_FLAG_PAGING (1u << 0)
#define APPLE_AGX_SUBMISSION_FLAG_PRESENT (1u << 1)
#define APPLE_AGX_SUBMISSION_FLAG_REDIRECTED_PRESENT (1u << 2)
#define APPLE_AGX_SUBMISSION_FLAG_NULL_RENDERING (1u << 3)
#define APPLE_AGX_SUBMISSION_FLAG_FLIP (1u << 4)
#define APPLE_AGX_SUBMISSION_FLAG_FLIP_NO_WAIT (1u << 5)
#define APPLE_AGX_SUBMISSION_FLAG_CONTEXT_SWITCH (1u << 6)
#define APPLE_AGX_SUBMISSION_FLAG_RESUBMISSION (1u << 7)
#define APPLE_AGX_SUBMISSION_FLAG_VM_DATA (1u << 8)
#define APPLE_AGX_SUBMISSION_FLAGS_ALLOWED 0x1ffu

typedef enum _APPLE_AGX_SUBMISSION_KIND {
  AppleAgxSubmissionInvalid = 0,
  AppleAgxSubmissionGdi,
  AppleAgxSubmissionPaging,
  AppleAgxSubmissionContextSwitch,
  AppleAgxSubmissionNull,
} APPLE_AGX_SUBMISSION_KIND;

/*
 * Pointer-free scheduler packet.  Windows-owned pointers are deliberately
 * excluded because DxgkDdiSubmitCommand runs later at DISPATCH_LEVEL.  The
 * packet describes the already patched DMA interval that an AGX backend must
 * atomically accept before the WDDM callback may return STATUS_SUCCESS.
 */
typedef struct _APPLE_AGX_SUBMISSION {
  APPLE_AGX_SUBMISSION_KIND Kind;
  APPLE_AGX_U32 Fence;
  APPLE_AGX_U32 NodeOrdinal;
  APPLE_AGX_U32 EngineOrdinal;
  APPLE_AGX_U32 SegmentId;
  APPLE_AGX_U32 Flags;
  APPLE_AGX_U64 DmaAddress;
  APPLE_AGX_U32 DmaBytes;
  APPLE_AGX_U32 Reserved;
} APPLE_AGX_SUBMISSION;

/*
 * Storage for one scheduler-owned DMA packet.  PrivateDataToken is an opaque
 * KMD token (the Windows implementation stores the nonpaged
 * pDmaBufferPrivateData pointer in it).  The queue itself never dereferences
 * the token, which keeps the ordering contract portable and testable.
 */
typedef struct _APPLE_AGX_SUBMISSION_ENTRY {
  APPLE_AGX_SUBMISSION Submission;
  APPLE_AGX_U64 PrivateDataToken;
  APPLE_AGX_U32 PrivateDataBytes;
  APPLE_AGX_U32 PrivateDataStart;
  APPLE_AGX_U32 PrivateDataEnd;
  /* DMA offsets are a distinct WDDM address space from private-data offsets. */
  APPLE_AGX_U32 DmaSubmissionStart;
  APPLE_AGX_U32 DmaSubmissionEnd;
} APPLE_AGX_SUBMISSION_ENTRY;

typedef struct _APPLE_AGX_SUBMISSION_QUEUE {
  APPLE_AGX_SUBMISSION_ENTRY *Entries;
  APPLE_AGX_U32 Capacity;
  APPLE_AGX_U32 Head;
  APPLE_AGX_U32 Count;
  APPLE_AGX_U32 CompletedFence;
  APPLE_AGX_U32 LastSubmittedFence;
  APPLE_AGX_U32 PublishedFence;
  APPLE_AGX_U32 ClaimedCompletionFence;
  APPLE_AGX_U32 *PagingReplayFences;
  APPLE_AGX_U32 PagingReplayCapacity;
  APPLE_AGX_U32 PagingReplayHead;
  APPLE_AGX_U32 PagingReplayCount;
} APPLE_AGX_SUBMISSION_QUEUE;

typedef enum _APPLE_AGX_COMPLETION_PHASE {
  AppleAgxCompletionIdle = 0,
  AppleAgxCompletionClaimed,
  AppleAgxCompletionSchedulerCommitted,
  AppleAgxCompletionLocalCommitted,
  AppleAgxCompletionBackendRetired,
  AppleAgxCompletionReported,
} APPLE_AGX_COMPLETION_PHASE;

/* Exact, monotonic receipt chain across the irreversible Windows report. */
typedef struct _APPLE_AGX_COMPLETION_TRANSACTION {
  APPLE_AGX_COMPLETION_PHASE Phase;
  APPLE_AGX_U32 Fence;
  APPLE_AGX_U32 NodeOrdinal;
  APPLE_AGX_U32 EngineOrdinal;
} APPLE_AGX_COMPLETION_TRANSACTION;

APPLE_AGX_BOOL AppleAgxSubmissionQueueInitialize(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_SUBMISSION_ENTRY *Entries,
    APPLE_AGX_U32 Capacity);
/* Caller-owned bounded storage; Windows must allocate it from nonpaged pool. */
APPLE_AGX_BOOL AppleAgxSubmissionQueueConfigurePagingReplay(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 *ReplayFences,
    APPLE_AGX_U32 ReplayCapacity);
APPLE_AGX_U32 AppleAgxSubmissionQueuePagingReplayDebt(
    const APPLE_AGX_SUBMISSION_QUEUE *Queue);
APPLE_AGX_BOOL AppleAgxSubmissionQueueReplayBlocked(
    const APPLE_AGX_SUBMISSION_QUEUE *Queue);
APPLE_AGX_BOOL AppleAgxSubmissionDescribe(
    APPLE_AGX_U32 Fence, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 SegmentId,
    APPLE_AGX_U64 DmaBufferAddress, APPLE_AGX_U32 DmaBufferBytes,
    APPLE_AGX_U32 SubmissionStart, APPLE_AGX_U32 SubmissionEnd,
    APPLE_AGX_U32 Flags, APPLE_AGX_SUBMISSION *Submission);
APPLE_AGX_BOOL AppleAgxSubmissionMayCompleteInSoftware(
    const APPLE_AGX_SUBMISSION *Submission);
/*
 * A paging packet carrying RESUBMISSION may reuse only the next exact fence
 * in the replay ledger.  Replays are inserted ahead of preserved later work;
 * normal admission and publication remain blocked until the ledger is empty.
 * The normal LastSubmittedFence watermark never moves backward.
 */
APPLE_AGX_BOOL AppleAgxSubmissionQueueAccept(
    APPLE_AGX_SUBMISSION_QUEUE *Queue,
    const APPLE_AGX_SUBMISSION *Submission, APPLE_AGX_U64 PrivateDataToken,
    APPLE_AGX_U32 PrivateDataBytes, APPLE_AGX_U32 PrivateDataStart,
    APPLE_AGX_U32 PrivateDataEnd, APPLE_AGX_U32 DmaSubmissionStart,
    APPLE_AGX_U32 DmaSubmissionEnd);
APPLE_AGX_BOOL AppleAgxSubmissionQueuePeek(
    const APPLE_AGX_SUBMISSION_QUEUE *Queue,
    APPLE_AGX_SUBMISSION_ENTRY *Entry);
APPLE_AGX_BOOL AppleAgxSubmissionQueueMarkPublished(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 Fence);
APPLE_AGX_BOOL AppleAgxSubmissionQueueClaimCompletion(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 Fence);
APPLE_AGX_BOOL AppleAgxSubmissionQueueReleaseCompletion(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 Fence);
APPLE_AGX_BOOL AppleAgxSubmissionQueueCommitCompletion(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 Fence);
/*
 * Remove an exactly claimed FIFO head after a separately proven teardown
 * retirement.  Unlike completion, this must not advance CompletedFence.
 */
APPLE_AGX_BOOL AppleAgxSubmissionQueueCommitRetirement(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 Fence);
/*
 * Drop only software-owned FIFO entries up to the submit snapshot.  The
 * operation is forbidden while the head is published and never advances the
 * completed fence.
 */
APPLE_AGX_BOOL AppleAgxSubmissionQueueDiscardPreemptedThrough(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 CutoffFence,
    APPLE_AGX_U32 *DiscardedCount);
/* Explicit recovery escape hatch; Quiesced must be a proven backend receipt. */
APPLE_AGX_BOOL AppleAgxSubmissionQueueResetPagingReplay(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_BOOL Quiesced);
APPLE_AGX_BOOL AppleAgxSubmissionQueueComplete(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 Fence);
APPLE_AGX_BOOL AppleAgxSubmissionQueueFault(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 Fence);
void AppleAgxCompletionTransactionInitialize(
    APPLE_AGX_COMPLETION_TRANSACTION *Transaction);
APPLE_AGX_BOOL AppleAgxCompletionTransactionBegin(
    APPLE_AGX_COMPLETION_TRANSACTION *Transaction, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 NodeOrdinal, APPLE_AGX_U32 EngineOrdinal);
APPLE_AGX_BOOL AppleAgxCompletionTransactionMatches(
    const APPLE_AGX_COMPLETION_TRANSACTION *Transaction,
    APPLE_AGX_U32 Fence, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal);
APPLE_AGX_BOOL AppleAgxCompletionTransactionAdvance(
    APPLE_AGX_COMPLETION_TRANSACTION *Transaction, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 NodeOrdinal, APPLE_AGX_U32 EngineOrdinal,
    APPLE_AGX_COMPLETION_PHASE Expected,
    APPLE_AGX_COMPLETION_PHASE Next);
APPLE_AGX_BOOL AppleAgxCompletionTransactionCanReport(
    const APPLE_AGX_COMPLETION_TRANSACTION *Transaction,
    APPLE_AGX_U32 Fence, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal);
void AppleAgxCompletionTransactionMarkReported(
    APPLE_AGX_COMPLETION_TRANSACTION *Transaction);
APPLE_AGX_BOOL AppleAgxCompletionTransactionFinish(
    APPLE_AGX_COMPLETION_TRANSACTION *Transaction, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 NodeOrdinal, APPLE_AGX_U32 EngineOrdinal);

#endif /* APPLE_AGX_SUBMISSION_H */
