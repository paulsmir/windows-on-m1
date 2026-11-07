#include "render_job_ahead.h"

/* rc90: see render_job_ahead.h. Refusal numbers are stable receipt values. */

static unsigned int ahead_common(const ADMISSION_JOB_AHEAD_STATE *s) {
  if (!s) return 99u;
  if (!s->Enabled) return 1u;
  if (s->Stopping || s->Resetting) return 2u;
  /* rc63: a faulted scheduler starts nothing until the TDR engine reset. */
  if (s->Faulted) return 3u;
  return 0u;
}

static int ahead_running(const ADMISSION_JOB_AHEAD_STATE *s) {
  return s->JobFence != 0u && s->PacketState == ADMISSION_JOB_AHEAD_PACKET_ACTIVE &&
      s->PacketFence == s->JobFence && s->ActiveFence == s->JobFence;
}

unsigned int AdmissionJobAheadReleaseRefusal(
    const ADMISSION_JOB_AHEAD_STATE *s) {
  unsigned int common = ahead_common(s);
  if (common) return common;
  if (!ahead_running(s)) return 10u;
  /* Legacy images keep firmware-read arena objects; only the G4 kinds
   * stage through the arena (render) or do not use it (compute, present). */
  if (s->ImageKind != ADMISSION_JOB_AHEAD_IMAGE_G4_RENDER &&
      s->ImageKind != ADMISSION_JOB_AHEAD_IMAGE_G4_COMPUTE &&
      s->ImageKind != ADMISSION_JOB_AHEAD_IMAGE_PRESENT) return 11u;
  if (s->ImageBoundFence != s->JobFence) return 12u;
  /* A release that may re-Prepare the arena could fail after the kick; the
   * completion path would then find no image to release. */
  if (!s->ImageFastRelease) return 13u;
  if (s->ReleasedFence != 0u) return 14u;
  return 0u;
}

unsigned int AdmissionJobAheadPrebindRefusal(
    const ADMISSION_JOB_AHEAD_STATE *s) {
  unsigned int common = ahead_common(s);
  if (common) return common;
  /* PreemptCommand drops pending entries; nothing new binds meanwhile. */
  if (s->PreemptionPhase != 0u) return 4u;
  if (!ahead_running(s)) return 20u;
  if (s->ReleasedFence != s->JobFence) return 21u;
  if (s->ImageBoundFence != 0u) return 22u;
  if (s->PendingCount == 0u) return 23u;
  if (s->PendingHeadState != ADMISSION_JOB_AHEAD_PENDING_QUEUED) return 24u;
  /* Only the immediate successor in fence order: a paging packet between
   * them may change the mappings the bind captures. */
  if (s->QueuedFence == 0u || s->PendingHeadFence != s->QueuedFence) return 25u;
  if (s->CpuHeadFence != 0u && s->CpuHeadFence == s->QueuedFence) return 26u;
  return 0u;
}

unsigned int AdmissionJobAheadChainRefusal(
    const ADMISSION_JOB_AHEAD_STATE *s) {
  unsigned int common = ahead_common(s);
  if (common) return common;
  /* A boundary preemption waits for this completion; the DPC notifies it. */
  if (s->PreemptionPhase != 0u) return 4u;
  if (!s->BackendReady) return 30u;
  if (s->ActiveFence != 0u) return 31u;
  if (s->PacketState != ADMISSION_JOB_AHEAD_PACKET_EMPTY) return 32u;
  /* N's completion transaction finished: DMA_COMPLETED was reported. */
  if (s->JobFence == 0u || s->FinishedFence != s->JobFence) return 33u;
  /* After a firmware-dropped job Windows gets DMA_PAGE_FAULTED; the next
   * job takes the ordinary dispatch path. */
  if (s->FinishedDropped) return 34u;
  if (s->ReleasedFence != 0u) return 35u;
  /* The DPC for N may not have run yet (DispatchedFence still N); any
   * other reservation belongs to a concurrent dispatch. */
  if (s->DispatchedFence != 0u && s->DispatchedFence != s->JobFence) return 36u;
  if (s->PagingPending) return 37u;
  if (s->QueuedFence == 0u ||
      (s->CpuHeadFence != 0u && s->CpuHeadFence == s->QueuedFence)) return 38u;
  if (s->PendingCount == 0u || s->PendingHeadFence != s->QueuedFence) return 39u;
  if (s->PendingHeadState == ADMISSION_JOB_AHEAD_PENDING_PREBOUND
          ? s->ImageBoundFence != s->PendingHeadFence
          : s->PendingHeadState != ADMISSION_JOB_AHEAD_PENDING_QUEUED ||
                s->ImageBoundFence != 0u) return 40u;
  /* Bounded: the worker returns through WorkerFinished regularly. */
  if (s->Chained >= ADMISSION_JOB_AHEAD_CHAIN_LIMIT) return 41u;
  return 0u;
}
