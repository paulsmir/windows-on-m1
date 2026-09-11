#include "apple_agx_submission.h"

#define APPLE_AGX_SUBMISSION_NULL ((void *)0)

static void AppleAgxSubmissionZero(APPLE_AGX_SUBMISSION *Submission) {
  if (Submission == APPLE_AGX_SUBMISSION_NULL)
    return;
  Submission->Kind = AppleAgxSubmissionInvalid;
  Submission->Fence = 0u;
  Submission->NodeOrdinal = 0u;
  Submission->EngineOrdinal = 0u;
  Submission->SegmentId = 0u;
  Submission->Flags = 0u;
  Submission->DmaAddress = 0ULL;
  Submission->DmaBytes = 0u;
  Submission->Reserved = 0u;
}

static void AppleAgxSubmissionEntryZero(APPLE_AGX_SUBMISSION_ENTRY *Entry) {
  if (Entry == APPLE_AGX_SUBMISSION_NULL)
    return;
  AppleAgxSubmissionZero(&Entry->Submission);
  Entry->PrivateDataToken = 0ULL;
  Entry->PrivateDataBytes = 0u;
  Entry->PrivateDataStart = 0u;
  Entry->PrivateDataEnd = 0u;
  Entry->DmaSubmissionStart = 0u;
  Entry->DmaSubmissionEnd = 0u;
}

static APPLE_AGX_BOOL AppleAgxFenceAfter(APPLE_AGX_U32 Candidate,
                                         APPLE_AGX_U32 Reference) {
  APPLE_AGX_U32 distance = Candidate - Reference;
  return distance != 0u && distance < 0x80000000u ? APPLE_AGX_TRUE
                                                   : APPLE_AGX_FALSE;
}

static APPLE_AGX_BOOL AppleAgxFenceAtOrAfter(APPLE_AGX_U32 Candidate,
                                             APPLE_AGX_U32 Reference) {
  return Candidate == Reference || AppleAgxFenceAfter(Candidate, Reference)
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxSubmissionQueueInitialize(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_SUBMISSION_ENTRY *Entries,
    APPLE_AGX_U32 Capacity) {
  APPLE_AGX_U32 index;

  if (Queue == APPLE_AGX_SUBMISSION_NULL)
    return APPLE_AGX_FALSE;
  Queue->Entries = APPLE_AGX_SUBMISSION_NULL;
  Queue->Capacity = 0u;
  Queue->Head = 0u;
  Queue->Count = 0u;
  Queue->CompletedFence = 0u;
  Queue->LastSubmittedFence = 0u;
  Queue->PublishedFence = 0u;
  Queue->ClaimedCompletionFence = 0u;
  Queue->PagingReplayFences = APPLE_AGX_SUBMISSION_NULL;
  Queue->PagingReplayCapacity = 0u;
  Queue->PagingReplayHead = 0u;
  Queue->PagingReplayCount = 0u;
  if (Entries == APPLE_AGX_SUBMISSION_NULL || Capacity == 0u)
    return APPLE_AGX_FALSE;
  for (index = 0u; index < Capacity; ++index)
    AppleAgxSubmissionEntryZero(&Entries[index]);
  Queue->Entries = Entries;
  Queue->Capacity = Capacity;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionQueueConfigurePagingReplay(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 *ReplayFences,
    APPLE_AGX_U32 ReplayCapacity) {
  APPLE_AGX_U32 index;

  if (Queue == APPLE_AGX_SUBMISSION_NULL ||
      Queue->Entries == APPLE_AGX_SUBMISSION_NULL || Queue->Capacity == 0u ||
      Queue->Count != 0u || Queue->PublishedFence != 0u ||
      Queue->ClaimedCompletionFence != 0u ||
      Queue->PagingReplayFences != APPLE_AGX_SUBMISSION_NULL ||
      ReplayFences == APPLE_AGX_SUBMISSION_NULL ||
      ReplayCapacity < Queue->Capacity)
    return APPLE_AGX_FALSE;
  for (index = 0u; index < ReplayCapacity; ++index)
    ReplayFences[index] = 0u;
  Queue->PagingReplayFences = ReplayFences;
  Queue->PagingReplayCapacity = ReplayCapacity;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_U32 AppleAgxSubmissionQueuePagingReplayDebt(
    const APPLE_AGX_SUBMISSION_QUEUE *Queue) {
  return Queue == APPLE_AGX_SUBMISSION_NULL ? 0u
                                            : Queue->PagingReplayCount;
}

APPLE_AGX_BOOL AppleAgxSubmissionQueueReplayBlocked(
    const APPLE_AGX_SUBMISSION_QUEUE *Queue) {
  return Queue != APPLE_AGX_SUBMISSION_NULL &&
                 Queue->PagingReplayCount != 0u
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxSubmissionDescribe(
    APPLE_AGX_U32 Fence, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal, APPLE_AGX_U32 SegmentId,
    APPLE_AGX_U64 DmaBufferAddress, APPLE_AGX_U32 DmaBufferBytes,
    APPLE_AGX_U32 SubmissionStart, APPLE_AGX_U32 SubmissionEnd,
    APPLE_AGX_U32 Flags, APPLE_AGX_SUBMISSION *Submission) {
  APPLE_AGX_SUBMISSION_KIND kind;
  APPLE_AGX_U32 bytes;

  if (Submission == APPLE_AGX_SUBMISSION_NULL)
    return APPLE_AGX_FALSE;
  AppleAgxSubmissionZero(Submission);
  if (Fence == 0u || NodeOrdinal != 0u || EngineOrdinal != 0u ||
      (Flags & ~APPLE_AGX_SUBMISSION_FLAGS_ALLOWED) != 0u ||
      SubmissionStart > SubmissionEnd || SubmissionEnd > DmaBufferBytes)
    return APPLE_AGX_FALSE;

  bytes = SubmissionEnd - SubmissionStart;
  if ((Flags & APPLE_AGX_SUBMISSION_FLAG_CONTEXT_SWITCH) != 0u) {
    if (bytes != 0u || (Flags & ~(APPLE_AGX_SUBMISSION_FLAG_CONTEXT_SWITCH |
                                  APPLE_AGX_SUBMISSION_FLAG_PAGING)) != 0u)
      return APPLE_AGX_FALSE;
    kind = AppleAgxSubmissionContextSwitch;
  } else if ((Flags & APPLE_AGX_SUBMISSION_FLAG_NULL_RENDERING) != 0u) {
    kind = AppleAgxSubmissionNull;
  } else if ((Flags & APPLE_AGX_SUBMISSION_FLAG_PAGING) != 0u) {
    kind = AppleAgxSubmissionPaging;
  } else {
    kind = AppleAgxSubmissionGdi;
  }

  if (bytes != 0u) {
    /*
     * SegmentId zero is the documented physical-engine system-memory DMA
     * buffer.  In that case DmaBufferAddress is its system physical base;
     * nonzero IDs describe a segment-relative GPU address.  Both forms are
     * pointer-free and use the same overflow-safe submitted interval.
     */
    if (DmaBufferAddress == 0ULL ||
        DmaBufferAddress > ~0ULL - SubmissionStart)
      return APPLE_AGX_FALSE;
    DmaBufferAddress += SubmissionStart;
  } else if (kind != AppleAgxSubmissionContextSwitch &&
             kind != AppleAgxSubmissionNull) {
    return APPLE_AGX_FALSE;
  }

  Submission->Kind = kind;
  Submission->Fence = Fence;
  Submission->NodeOrdinal = NodeOrdinal;
  Submission->EngineOrdinal = EngineOrdinal;
  Submission->SegmentId = SegmentId;
  Submission->Flags = Flags;
  Submission->DmaAddress = DmaBufferAddress;
  Submission->DmaBytes = bytes;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionMayCompleteInSoftware(
    const APPLE_AGX_SUBMISSION *Submission) {
  if (Submission == APPLE_AGX_SUBMISSION_NULL)
    return APPLE_AGX_FALSE;
  return Submission->Kind == AppleAgxSubmissionNull ||
                 (Submission->Kind == AppleAgxSubmissionContextSwitch &&
                  Submission->DmaBytes == 0u)
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxSubmissionQueueAccept(
    APPLE_AGX_SUBMISSION_QUEUE *Queue,
    const APPLE_AGX_SUBMISSION *Submission, APPLE_AGX_U64 PrivateDataToken,
    APPLE_AGX_U32 PrivateDataBytes, APPLE_AGX_U32 PrivateDataStart,
    APPLE_AGX_U32 PrivateDataEnd, APPLE_AGX_U32 DmaSubmissionStart,
    APPLE_AGX_U32 DmaSubmissionEnd) {
  APPLE_AGX_BOOL pagingReplay;
  APPLE_AGX_U32 index;
  APPLE_AGX_U32 insertOffset;
  APPLE_AGX_U32 source;
  APPLE_AGX_U32 destination;
  APPLE_AGX_SUBMISSION_ENTRY *entry;

  pagingReplay =
      Submission != APPLE_AGX_SUBMISSION_NULL &&
              Submission->Kind == AppleAgxSubmissionPaging &&
              (Submission->Flags & APPLE_AGX_SUBMISSION_FLAG_RESUBMISSION) !=
                  0u
          ? APPLE_AGX_TRUE
          : APPLE_AGX_FALSE;
  if (Queue == APPLE_AGX_SUBMISSION_NULL ||
      Submission == APPLE_AGX_SUBMISSION_NULL ||
      Queue->Entries == APPLE_AGX_SUBMISSION_NULL || Queue->Capacity == 0u ||
      Queue->Count >= Queue->Capacity ||
      Submission->Kind == AppleAgxSubmissionInvalid ||
      Submission->Reserved != 0u ||
      (Queue->PagingReplayCount != 0u && !pagingReplay) ||
      ((!pagingReplay &&
        !AppleAgxFenceAfter(Submission->Fence,
                            Queue->LastSubmittedFence)) ||
       (pagingReplay &&
        (!AppleAgxFenceAfter(Submission->Fence, Queue->CompletedFence) ||
         AppleAgxFenceAfter(Submission->Fence,
                            Queue->LastSubmittedFence)))) ||
      PrivateDataStart > PrivateDataEnd ||
      PrivateDataEnd > PrivateDataBytes ||
      DmaSubmissionStart > DmaSubmissionEnd ||
      DmaSubmissionEnd - DmaSubmissionStart != Submission->DmaBytes ||
      (Submission->Kind == AppleAgxSubmissionGdi &&
       (PrivateDataToken == 0ULL || PrivateDataBytes == 0u ||
        PrivateDataStart != 0u || PrivateDataEnd == 0u)) ||
      ((PrivateDataToken == 0ULL) != (PrivateDataBytes == 0u)))
    return APPLE_AGX_FALSE;

  if (pagingReplay &&
      (Queue->PagingReplayFences == APPLE_AGX_SUBMISSION_NULL ||
       Queue->PagingReplayCount == 0u ||
       Queue->PagingReplayFences[Queue->PagingReplayHead] !=
           Submission->Fence))
    return APPLE_AGX_FALSE;

  insertOffset = Queue->Count;
  if (pagingReplay) {
    for (index = 0u; index < Queue->Count; ++index) {
      entry = &Queue->Entries[(Queue->Head + index) % Queue->Capacity];
      if (entry->Submission.Fence == Submission->Fence)
        return APPLE_AGX_FALSE;
      if (insertOffset == Queue->Count &&
          AppleAgxFenceAfter(entry->Submission.Fence, Submission->Fence))
        insertOffset = index;
    }
    for (index = Queue->Count; index > insertOffset; --index) {
      destination = (Queue->Head + index) % Queue->Capacity;
      source = (Queue->Head + index - 1u) % Queue->Capacity;
      Queue->Entries[destination] = Queue->Entries[source];
    }
  }
  entry = &Queue->Entries[(Queue->Head + insertOffset) % Queue->Capacity];
  AppleAgxSubmissionEntryZero(entry);
  entry->Submission = *Submission;
  entry->PrivateDataToken = PrivateDataToken;
  entry->PrivateDataBytes = PrivateDataBytes;
  entry->PrivateDataStart = PrivateDataStart;
  entry->PrivateDataEnd = PrivateDataEnd;
  entry->DmaSubmissionStart = DmaSubmissionStart;
  entry->DmaSubmissionEnd = DmaSubmissionEnd;
  ++Queue->Count;
  if (pagingReplay) {
    Queue->PagingReplayFences[Queue->PagingReplayHead] = 0u;
    Queue->PagingReplayHead =
        (Queue->PagingReplayHead + 1u) % Queue->PagingReplayCapacity;
    --Queue->PagingReplayCount;
    if (Queue->PagingReplayCount == 0u)
      Queue->PagingReplayHead = 0u;
  } else {
    Queue->LastSubmittedFence = Submission->Fence;
  }
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionQueuePeek(
    const APPLE_AGX_SUBMISSION_QUEUE *Queue,
    APPLE_AGX_SUBMISSION_ENTRY *Entry) {
  if (Queue == APPLE_AGX_SUBMISSION_NULL ||
      Entry == APPLE_AGX_SUBMISSION_NULL ||
      Queue->Entries == APPLE_AGX_SUBMISSION_NULL || Queue->Capacity == 0u ||
      Queue->Count == 0u)
    return APPLE_AGX_FALSE;
  *Entry = Queue->Entries[Queue->Head];
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionQueueMarkPublished(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 Fence) {
  if (Queue == APPLE_AGX_SUBMISSION_NULL || Fence == 0u ||
      Queue->Entries == APPLE_AGX_SUBMISSION_NULL || Queue->Capacity == 0u ||
      Queue->Count == 0u || Queue->PublishedFence != 0u ||
      Queue->PagingReplayCount != 0u ||
      Queue->Entries[Queue->Head].Submission.Fence != Fence)
    return APPLE_AGX_FALSE;
  Queue->PublishedFence = Fence;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionQueueClaimCompletion(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 Fence) {
  if (Queue == APPLE_AGX_SUBMISSION_NULL || Fence == 0u ||
      Queue->Entries == APPLE_AGX_SUBMISSION_NULL || Queue->Capacity == 0u ||
      Queue->Count == 0u || Queue->ClaimedCompletionFence != 0u ||
      Queue->Entries[Queue->Head].Submission.Fence != Fence)
    return APPLE_AGX_FALSE;
  Queue->ClaimedCompletionFence = Fence;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionQueueReleaseCompletion(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 Fence) {
  if (Queue == APPLE_AGX_SUBMISSION_NULL || Fence == 0u ||
      Queue->ClaimedCompletionFence != Fence)
    return APPLE_AGX_FALSE;
  Queue->ClaimedCompletionFence = 0u;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionQueueCommitCompletion(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 Fence) {
  APPLE_AGX_SUBMISSION_ENTRY *entry;

  if (Queue == APPLE_AGX_SUBMISSION_NULL || Fence == 0u ||
      Queue->Entries == APPLE_AGX_SUBMISSION_NULL || Queue->Capacity == 0u ||
      Queue->Count == 0u || Queue->ClaimedCompletionFence != Fence)
    return APPLE_AGX_FALSE;
  entry = &Queue->Entries[Queue->Head];
  if (entry->Submission.Fence != Fence)
    return APPLE_AGX_FALSE;
  Queue->CompletedFence = Fence;
  if (Queue->PublishedFence == Fence)
    Queue->PublishedFence = 0u;
  Queue->ClaimedCompletionFence = 0u;
  AppleAgxSubmissionEntryZero(entry);
  Queue->Head = (Queue->Head + 1u) % Queue->Capacity;
  --Queue->Count;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionQueueCommitRetirement(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 Fence) {
  APPLE_AGX_SUBMISSION_ENTRY *entry;

  if (Queue == APPLE_AGX_SUBMISSION_NULL || Fence == 0u ||
      Queue->Entries == APPLE_AGX_SUBMISSION_NULL || Queue->Capacity == 0u ||
      Queue->Count == 0u || Queue->ClaimedCompletionFence != Fence)
    return APPLE_AGX_FALSE;
  entry = &Queue->Entries[Queue->Head];
  if (entry->Submission.Fence != Fence)
    return APPLE_AGX_FALSE;
  if (Queue->PublishedFence == Fence)
    Queue->PublishedFence = 0u;
  Queue->ClaimedCompletionFence = 0u;
  AppleAgxSubmissionEntryZero(entry);
  Queue->Head = (Queue->Head + 1u) % Queue->Capacity;
  --Queue->Count;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionQueueDiscardPreemptedThrough(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 CutoffFence,
    APPLE_AGX_U32 *DiscardedCount) {
  APPLE_AGX_SUBMISSION_ENTRY *entry;
  APPLE_AGX_U32 discarded = 0u;
  APPLE_AGX_U32 index;
  APPLE_AGX_U32 paging = 0u;

  if (Queue == APPLE_AGX_SUBMISSION_NULL ||
      DiscardedCount == APPLE_AGX_SUBMISSION_NULL ||
      Queue->Entries == APPLE_AGX_SUBMISSION_NULL || Queue->Capacity == 0u ||
      Queue->ClaimedCompletionFence != 0u || Queue->PublishedFence != 0u ||
      Queue->PagingReplayCount != 0u ||
      !AppleAgxFenceAtOrAfter(CutoffFence, Queue->CompletedFence))
    return APPLE_AGX_FALSE;
  for (index = 0u; index < Queue->Count; ++index) {
    entry = &Queue->Entries[(Queue->Head + index) % Queue->Capacity];
    if (AppleAgxFenceAfter(entry->Submission.Fence, CutoffFence))
      break;
    if (entry->Submission.Kind == AppleAgxSubmissionPaging)
      ++paging;
  }
  if (paging != 0u &&
      (Queue->PagingReplayFences == APPLE_AGX_SUBMISSION_NULL ||
       Queue->PagingReplayCapacity < paging))
    return APPLE_AGX_FALSE;
  while (Queue->Count != 0u) {
    entry = &Queue->Entries[Queue->Head];
    if (AppleAgxFenceAfter(entry->Submission.Fence, CutoffFence))
      break;
    if (entry->Submission.Kind == AppleAgxSubmissionPaging) {
      Queue->PagingReplayFences[Queue->PagingReplayCount] =
          entry->Submission.Fence;
      ++Queue->PagingReplayCount;
    }
    AppleAgxSubmissionEntryZero(entry);
    Queue->Head = (Queue->Head + 1u) % Queue->Capacity;
    --Queue->Count;
    ++discarded;
  }
  *DiscardedCount = discarded;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionQueueResetPagingReplay(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_BOOL Quiesced) {
  APPLE_AGX_U32 index;

  if (Queue == APPLE_AGX_SUBMISSION_NULL ||
      !Quiesced ||
      Queue->PagingReplayFences == APPLE_AGX_SUBMISSION_NULL ||
      Queue->PagingReplayCapacity == 0u || Queue->PublishedFence != 0u ||
      Queue->ClaimedCompletionFence != 0u)
    return APPLE_AGX_FALSE;
  for (index = 0u; index < Queue->PagingReplayCapacity; ++index)
    Queue->PagingReplayFences[index] = 0u;
  Queue->PagingReplayHead = 0u;
  Queue->PagingReplayCount = 0u;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionQueueComplete(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 Fence) {
  APPLE_AGX_SUBMISSION_ENTRY *entry;

  if (Queue == APPLE_AGX_SUBMISSION_NULL ||
      Queue->Entries == APPLE_AGX_SUBMISSION_NULL || Queue->Capacity == 0u ||
      Queue->Count == 0u || Queue->ClaimedCompletionFence != 0u)
    return APPLE_AGX_FALSE;
  entry = &Queue->Entries[Queue->Head];
  if (Fence != entry->Submission.Fence)
    return APPLE_AGX_FALSE;
  Queue->CompletedFence = Fence;
  if (Queue->PublishedFence == Fence)
    Queue->PublishedFence = 0u;
  AppleAgxSubmissionEntryZero(entry);
  Queue->Head = (Queue->Head + 1u) % Queue->Capacity;
  --Queue->Count;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionQueueFault(
    APPLE_AGX_SUBMISSION_QUEUE *Queue, APPLE_AGX_U32 Fence) {
  APPLE_AGX_SUBMISSION_ENTRY *entry;

  if (Queue == APPLE_AGX_SUBMISSION_NULL ||
      Queue->Entries == APPLE_AGX_SUBMISSION_NULL || Queue->Capacity == 0u ||
      Queue->Count == 0u || Queue->ClaimedCompletionFence != 0u)
    return APPLE_AGX_FALSE;
  entry = &Queue->Entries[Queue->Head];
  if (Fence != entry->Submission.Fence)
    return APPLE_AGX_FALSE;
  if (Queue->PublishedFence == Fence)
    Queue->PublishedFence = 0u;
  AppleAgxSubmissionEntryZero(entry);
  Queue->Head = (Queue->Head + 1u) % Queue->Capacity;
  --Queue->Count;
  return APPLE_AGX_TRUE;
}

void AppleAgxCompletionTransactionInitialize(
    APPLE_AGX_COMPLETION_TRANSACTION *Transaction) {
  if (Transaction == APPLE_AGX_SUBMISSION_NULL)
    return;
  Transaction->Phase = AppleAgxCompletionIdle;
  Transaction->Fence = 0u;
  Transaction->NodeOrdinal = 0u;
  Transaction->EngineOrdinal = 0u;
}

APPLE_AGX_BOOL AppleAgxCompletionTransactionBegin(
    APPLE_AGX_COMPLETION_TRANSACTION *Transaction, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 NodeOrdinal, APPLE_AGX_U32 EngineOrdinal) {
  if (Transaction == APPLE_AGX_SUBMISSION_NULL || Fence == 0u ||
      Transaction->Phase != AppleAgxCompletionIdle)
    return APPLE_AGX_FALSE;
  Transaction->Fence = Fence;
  Transaction->NodeOrdinal = NodeOrdinal;
  Transaction->EngineOrdinal = EngineOrdinal;
  Transaction->Phase = AppleAgxCompletionClaimed;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxCompletionTransactionMatches(
    const APPLE_AGX_COMPLETION_TRANSACTION *Transaction,
    APPLE_AGX_U32 Fence, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal) {
  return Transaction != APPLE_AGX_SUBMISSION_NULL && Fence != 0u &&
                 Transaction->Phase != AppleAgxCompletionIdle &&
                 Transaction->Fence == Fence &&
                 Transaction->NodeOrdinal == NodeOrdinal &&
                 Transaction->EngineOrdinal == EngineOrdinal
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

static APPLE_AGX_BOOL AppleAgxCompletionTransitionValid(
    APPLE_AGX_COMPLETION_PHASE Expected,
    APPLE_AGX_COMPLETION_PHASE Next) {
  return (Expected == AppleAgxCompletionClaimed &&
          Next == AppleAgxCompletionSchedulerCommitted) ||
                 (Expected == AppleAgxCompletionSchedulerCommitted &&
                  Next == AppleAgxCompletionLocalCommitted) ||
                 (Expected == AppleAgxCompletionLocalCommitted &&
                  Next == AppleAgxCompletionBackendRetired)
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxCompletionTransactionAdvance(
    APPLE_AGX_COMPLETION_TRANSACTION *Transaction, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 NodeOrdinal, APPLE_AGX_U32 EngineOrdinal,
    APPLE_AGX_COMPLETION_PHASE Expected,
    APPLE_AGX_COMPLETION_PHASE Next) {
  if (!AppleAgxCompletionTransitionValid(Expected, Next) ||
      !AppleAgxCompletionTransactionMatches(Transaction, Fence, NodeOrdinal,
                                            EngineOrdinal) ||
      Transaction->Phase != Expected)
    return APPLE_AGX_FALSE;
  Transaction->Phase = Next;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxCompletionTransactionCanReport(
    const APPLE_AGX_COMPLETION_TRANSACTION *Transaction,
    APPLE_AGX_U32 Fence, APPLE_AGX_U32 NodeOrdinal,
    APPLE_AGX_U32 EngineOrdinal) {
  return AppleAgxCompletionTransactionMatches(
             Transaction, Fence, NodeOrdinal, EngineOrdinal) &&
                 Transaction->Phase == AppleAgxCompletionBackendRetired
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

void AppleAgxCompletionTransactionMarkReported(
    APPLE_AGX_COMPLETION_TRANSACTION *Transaction) {
  /*
   * The caller proves CanReport before the irreversible Windows callback.
   * This store deliberately has no fallible result after NotifyInterrupt.
   */
  Transaction->Phase = AppleAgxCompletionReported;
}

APPLE_AGX_BOOL AppleAgxCompletionTransactionFinish(
    APPLE_AGX_COMPLETION_TRANSACTION *Transaction, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 NodeOrdinal, APPLE_AGX_U32 EngineOrdinal) {
  if (!AppleAgxCompletionTransactionMatches(Transaction, Fence, NodeOrdinal,
                                            EngineOrdinal) ||
      Transaction->Phase != AppleAgxCompletionReported)
    return APPLE_AGX_FALSE;
  AppleAgxCompletionTransactionInitialize(Transaction);
  return APPLE_AGX_TRUE;
}
