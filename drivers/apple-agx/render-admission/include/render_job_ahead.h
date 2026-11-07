#ifndef APPLE_AGX_RENDER_JOB_AHEAD_H
#define APPLE_AGX_RENDER_JOB_AHEAD_H

/* rc90 build-ahead (device value Wom1JobBuildAhead = 1, default off).
 *
 * The firmware still runs one job at a time (one RenderPacket, one VM-slot
 * lease, one buffer-manager slot, unslotted queue object 12). What moves off
 * the critical path between job N's firmware end and job N+1's kick:
 *   1. release: N's backend image (the template arena) is restored right
 *      after N's kick instead of at N's completion. In the G4 path the
 *      arena is CPU staging only once the build copied N's per-submission
 *      objects into the firmware-shared queue objects (every firmware-read
 *      reference from those objects into arena objects 36..75 is a process
 *      VA or zero, apple_agx_g4_builder.c AppleAgxG4BindNativeObjects; no
 *      arena-resident object references objects 0..35).
 *   2. prebind: while N runs, the next pending G4 submission is bound into
 *      the released image (TA/3D scene, G4 header, command, lease).
 *   3. chain: at N's completion (reported to Windows first), the worker
 *      that ran N claims N+1 itself: no WorkerFinished, DPC dispatch or
 *      job-thread wake between them.
 * These are the pure decisions; the KMD fills the state under
 * SchedulerLock. Each returns 0 when the step is allowed, otherwise the
 * number of the first refusing predicate (receipt value). */

#define ADMISSION_JOB_AHEAD_CHAIN_LIMIT 16u

/* ADMISSION_G4_PENDING_STATE (render_admission.h) values read here. */
#define ADMISSION_JOB_AHEAD_PENDING_QUEUED 2u
#define ADMISSION_JOB_AHEAD_PENDING_DROPPED 4u
#define ADMISSION_JOB_AHEAD_PENDING_PREBOUND 5u

/* ADMISSION_RENDER_PACKET_STATE (render_submission.h) values read here. */
#define ADMISSION_JOB_AHEAD_PACKET_EMPTY 0u
#define ADMISSION_JOB_AHEAD_PACKET_ACTIVE 3u

/* What the bound backend image holds. Only the G4 kinds release early:
 * the legacy GDI/dynamic images keep firmware-read arena objects. */
#define ADMISSION_JOB_AHEAD_IMAGE_NONE 0u
#define ADMISSION_JOB_AHEAD_IMAGE_G4_RENDER 1u
#define ADMISSION_JOB_AHEAD_IMAGE_G4_COMPUTE 2u
#define ADMISSION_JOB_AHEAD_IMAGE_PRESENT 3u
#define ADMISSION_JOB_AHEAD_IMAGE_OTHER 4u

typedef struct _ADMISSION_JOB_AHEAD_STATE {
  unsigned int Enabled;          /* Wom1JobBuildAhead */
  unsigned int Stopping, Resetting;
  unsigned int Faulted;          /* SchedulerFaulted != 0 */
  unsigned int PreemptionPhase;  /* APPLE_AGX_PREEMPTION_PHASE, 0 = idle */
  unsigned int BackendReady;     /* backend runtime Ready, no job submitted */
  unsigned int PagingPending;    /* a CPU (paging/present) packet runs */
  unsigned int CpuHeadFence;     /* fence of the CPU queue head, 0 = none */
  unsigned int QueuedFence;      /* scheduler: next fence to activate */
  unsigned int ActiveFence;      /* scheduler: running fence */
  unsigned int DispatchedFence;
  unsigned int PacketState, PacketFence;
  unsigned int PendingCount, PendingHeadState, PendingHeadFence;
  unsigned int ImageKind, ImageBoundFence;
  unsigned int ImageFastRelease; /* the release cannot fail (no re-Prepare) */
  unsigned int ReleasedFence;    /* job whose image was released after kick */
  unsigned int JobFence;         /* the job this worker invocation ran */
  unsigned int FinishedFence;    /* last completion transaction finished */
  unsigned int FinishedDropped;  /* ... and it was a firmware-dropped job */
  unsigned int Chained;          /* jobs chained in this worker invocation */
} ADMISSION_JOB_AHEAD_STATE;

/* Right after job JobFence was kicked. */
unsigned int AdmissionJobAheadReleaseRefusal(
    const ADMISSION_JOB_AHEAD_STATE *State);
/* While job JobFence runs: bind the pending head into the released image. */
unsigned int AdmissionJobAheadPrebindRefusal(
    const ADMISSION_JOB_AHEAD_STATE *State);
/* After job JobFence's completion was reported: claim the pending head. */
unsigned int AdmissionJobAheadChainRefusal(
    const ADMISSION_JOB_AHEAD_STATE *State);

#endif /* APPLE_AGX_RENDER_JOB_AHEAD_H */
