#ifndef APPLE_AGX_RENDER_JOB_WINDOW_H
#define APPLE_AGX_RENDER_JOB_WINDOW_H

/* Firmware depth-2 job window (Q20: default on, Wom1JobDepth2 = 0 opts
 * out; still requires effective Wom1JobBuildAhead).
 *
 * Depth 2: job N+1 is built and kicked to the firmware queues while N runs.
 * Windows order is kept by the firmware itself: N+1's first queue leads
 * with a Barrier work item on N's last firmware stamp (Asahi queue/mod.rs
 * explicit vdm/cdm barriers, queue/render.rs fragment barrier on the vertex
 * stamp), so N+1's work starts only after N's work ended, exactly as at
 * depth 1, but with no CPU between them. VidSch hands the KMD at most the
 * running job and one successor (sub(N) ~ cmp(N-2), rc94), so 2 is also
 * the deepest window it can fill.
 *
 * Firmware objects a job owns while it runs decide which pairs may share
 * the firmware (docs/superpowers/plans/2026-10-28-firmware-depth-2-job-
 * window.md, rc95 REPORT):
 *   - objects 13-19 and 28-35 alternate by stamp parity: two jobs fit;
 *   - the compute sidecar (object 39) holds one job's RunCompute, its
 *     32 KiB preemption buffer and its prelude timestamps: a compute job
 *     and a render with a v5 compute prelude both use it (U7);
 *   - the buffer-manager objects 1/20/21/22 and the scene 13 are the live
 *     firmware state of one render's manager binding: a second render needs
 *     the same manager key (U1), a compute job does not use them;
 *   - the notifier threshold (object 12) counts every kicked job (Asahi
 *     raises it at run()), so N's completion event is deferred until N+1
 *     ends; N's completion must be seen from its own stamps (U4);
 *   - one VM slot lease (graph slot 1): one process at a time.
 *
 * Pure decisions and the in-flight window state; the KMD fills the inputs
 * under SchedulerLock. Refusals are stable receipt values (0 = allowed). */

#define ADMISSION_JOB_WINDOW_DEPTH 2u

/* What a job runs on the firmware. */
#define ADMISSION_JOB_WINDOW_KIND_NONE 0u
#define ADMISSION_JOB_WINDOW_KIND_RENDER 1u         /* G4 native TA/3D */
#define ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE 2u /* v5: + compute prelude */
#define ADMISSION_JOB_WINDOW_KIND_COMPUTE 3u        /* G4 compute envelope */
#define ADMISSION_JOB_WINDOW_KIND_PRESENT 4u        /* KMD present compute */
#define ADMISSION_JOB_WINDOW_KIND_OTHER 5u          /* legacy image */

typedef struct _ADMISSION_JOB_WINDOW_JOB {
  unsigned int Fence;
  unsigned int Kind;                /* ADMISSION_JOB_WINDOW_KIND_* */
  unsigned long long Process;       /* G3 process object, 0 = none */
  unsigned long long SceneId;       /* private scene of a render, 0 = none */
  /* OV2: the WDDM context that submitted the job (0 = none) and, for a
   * compute job, its envelope named no render-subqueue wait (VdmBarrier
   * NONE: the D3D12 UMD proved it independent of the render job submitted
   * just before it, AGX_WIN32_DEVICE_FEATURE_INDEPENDENT_SUBQUEUE). */
  unsigned long long Context;
  unsigned int Independent;
  /* OV5 (Wom1Ov5TaWaiver): a render without a compute prelude whose
   * header named no compute-subqueue wait (CdmBarrier NONE: the D3D12 UMD
   * proved its vertex/TA work independent of the compute job submitted just
   * before it, AGX_WIN32_DEVICE_FEATURE_RENDER_TA_INDEPENDENT). */
  unsigned int TaIndependent;
} ADMISSION_JOB_WINDOW_JOB;

/* Firmware-side features the window needs for some pairs (rc95: all 0).
 * rc98 implements SECOND_SIDECAR (the sidecar's job-word set B,
 * apple_agx_render_shared_memory.h), TA_ON_3D and COMPUTE_BARRIER (the
 * D3D-G1b Barrier encoder on another queue's firmware stamp,
 * AppleAgxRenderSharedMemoryBuildLeadingBarrier); they are declared only
 * under Wom1JobDepth2Caps (AdmissionJobWindowCapabilities) until a kicked
 * window validates them on hardware. Q6 declares LIVE_MANAGER only under
 * Wom1Depth2General after enabling exact same-slot lifetime ownership. */
#define ADMISSION_JOB_WINDOW_CAP_SECOND_SIDECAR 0x1u  /* U7: sidecar per slot */
#define ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER 0x2u    /* U1: same-key skip */
#define ADMISSION_JOB_WINDOW_CAP_TA_ON_3D 0x4u        /* TA Barrier, 3D stamp */
#define ADMISSION_JOB_WINDOW_CAP_COMPUTE_BARRIER 0x8u /* compute-queue Barrier */
/* P1 (EXP2131 budget: the R7 -> present -> C0 span is ~1.7 ms per frame):
 * the KMD present compute job joins the window as a compute job on its
 * own (second) sidecar set. Declared only under Wom1Depth2Present = 1 on
 * top of the general depth-two mode; never two presents in one window
 * (one present arena per process, AppleAgxPresentBuild). */
#define ADMISSION_JOB_WINDOW_CAP_PRESENT_PAIRS 0x10u

#define ADMISSION_JOB_WINDOW_CAPS_RC98                                       \
  (ADMISSION_JOB_WINDOW_CAP_SECOND_SIDECAR | ADMISSION_JOB_WINDOW_CAP_TA_ON_3D | \
   ADMISSION_JOB_WINDOW_CAP_COMPUTE_BARRIER)

/* rc98: the capabilities the KMD declares: ADMISSION_JOB_WINDOW_CAPS_RC98
 * when Depth2 (Wom1JobDepth2 with Wom1JobBuildAhead) and CapsSwitch
 * (device value Wom1JobDepth2Caps = 1) are both set, else 0. */
unsigned int AdmissionJobWindowCapabilities(unsigned int Depth2,
                                            unsigned int CapsSwitch);

/* rc98: does a sidecar job of this kind use the second job-word set when
 * SECOND_SIDECAR is declared? Compute jobs and v5 preludes do (each has a
 * job-owned preemption buffer); others never touch the sidecar's words. */
unsigned int AdmissionJobWindowSecondSet(unsigned int Kind,
                                         unsigned int Capabilities);

typedef struct _ADMISSION_JOB_WINDOW_KICK_STATE {
  unsigned int Enabled;         /* Wom1JobDepth2 and Wom1JobBuildAhead */
  unsigned int Stopping, Resetting;
  unsigned int Faulted;         /* SchedulerFaulted != 0 */
  unsigned int PreemptionPhase; /* APPLE_AGX_PREEMPTION_PHASE, 0 = idle */
  unsigned int PagingPending;   /* a CPU (paging/present) packet runs */
  unsigned int CpuHeadFence;    /* fence of the CPU queue head, 0 = none */
  unsigned int QueuedFence;     /* scheduler: next fence to activate */
  unsigned int InFlight;        /* jobs the firmware holds (window Count) */
  unsigned int WindowPoisoned;  /* a job of the window was dropped */
  unsigned int Capabilities;    /* ADMISSION_JOB_WINDOW_CAP_* */
  /* The successor is the Prebound head entry and the backend image holds
   * its binding (rc90 prebind). */
  unsigned int NextPrebound;
  ADMISSION_JOB_WINDOW_JOB Running; /* N: kicked, its image released */
  ADMISSION_JOB_WINDOW_JOB Next;    /* N+1 */
  /* rc102: N+1 would start a new queue lifetime, carry InitBM or rebind
   * the buffer manager (all need idle queues: depth 1). */
  unsigned int NextNeedsIdle;
  /* rc102: the sidecar job-word sets of N and N+1 when both use the
   * sidecar (SetsValid); they must differ. */
  unsigned int SetsValid, RunningSet, NextSet;
  /* rc102 (EXP2047): the Barrier classes the wired window may kick, bit
   * (1 << ADMISSION_JOB_WINDOW_BARRIER_*); 0 = no restriction (the rc95
   * receipt mode). Refusal 64 otherwise. */
  unsigned int KickClasses;
  /* Q6 general mode: caller proves the pending key equals the running live slot. */
  unsigned int General, SameManagerSlot;
  /* Q14: unproven prelude pairs require Wom1Depth2Prelude=1 in general mode. */
  unsigned int Prelude;
} ADMISSION_JOB_WINDOW_KICK_STATE;

/* Previously unused policy reason; keep all existing wire values and layout. */
#define ADMISSION_JOB_WINDOW_REFUSAL_PRELUDE_UNPROVEN 5u

/* While N runs: may N+1 be kicked now? Refusal values:
 *   1 disabled, 2 stopping/resetting, 3 scheduler faulted, 4 preemption,
 *   5 general-mode prelude pair not explicitly enabled,
 *   50 window not exactly {N}, 51 window poisoned (a dropped job),
 *   52 no Prebound successor, 53 successor is not the scheduler's next
 *   fence, 54 a paging/CPU packet runs or heads the queue,
 *   55 another process (one VM slot), 56 a present or legacy job,
 *   57 both use the compute sidecar (U7), 58 render after render with
 *   another manager binding (legacy SceneId; Q6 live-slot key), 59 missing
 *   live-manager capability (U1), 60 the pair's Barrier is not available (TA on a 3D stamp,
 *   or any compute-queue Barrier). The TA Barrier on a compute stamp is
 *   the hardware-validated D3D-G1b one (EXP1279+). */
unsigned int AdmissionJobWindowKickRefusal(
    const ADMISSION_JOB_WINDOW_KICK_STATE *State);
/* rc102 (EXP2047): the classes the wired window kicks. By default only
 * those whose Barrier waits on N's compute stamp; On3d (device value
 * Wom1JobDepth2KickOn3d = 1) adds those waiting on N's 3D stamp (R->R,
 * R->C), whose end the notifier may defer behind N+1's (U4). */
unsigned int AdmissionJobWindowKickClasses(unsigned int On3d);
unsigned int AdmissionJobWindowGeneralClasses(unsigned int TaOnCompute,
    unsigned int TaOn3d, unsigned int ComputeOnCompute);
/* rc102 refusals of the wired window (after 1-60):
 *   61 N+1 needs idle queues (new queue lifetime, InitBM, manager bind),
 *   62 N and N+1 would share one sidecar job-word set,
 *   (63 the KMD's KickLocked refused),
 *   64 the pair's Barrier class is not in KickClasses (checked after 60;
 *      EXP2047: the classes waiting on N's 3D stamp are opt-in). */

/* Does a job of this kind use the compute sidecar / the buffer manager? */
unsigned int AdmissionJobWindowUsesSidecar(unsigned int Kind);
unsigned int AdmissionJobWindowUsesManager(unsigned int Kind);

/* The Barrier N+1's first queue leads with: on N's last stamp (3D for a
 * render, compute for a compute job), in the queue N+1 starts on (compute
 * for a compute job or a render with a prelude, whose TA already waits for
 * its own prelude; TA otherwise). 0 = no legal Barrier for this pair. */
#define ADMISSION_JOB_WINDOW_BARRIER_NONE 0u
#define ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE 1u
#define ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D 2u
#define ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D 3u
#define ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE 4u
unsigned int AdmissionJobWindowBarrier(unsigned int RunningKind,
                                       unsigned int NextKind);
/* P1: as AdmissionJobWindowBarrier, with a present job taken as a compute
 * job when Capabilities declare ADMISSION_JOB_WINDOW_CAP_PRESENT_PAIRS. */
unsigned int AdmissionJobWindowBarrierEx(unsigned int RunningKind,
                                         unsigned int NextKind,
                                         unsigned int Capabilities);
/* rc102: the queue N+1's leading Barrier is published on (it starts there)
 * and the queue whose firmware stamp it waits on (N's last). */
#define ADMISSION_JOB_WINDOW_QUEUE_TA 0u
#define ADMISSION_JOB_WINDOW_QUEUE_3D 1u
#define ADMISSION_JOB_WINDOW_QUEUE_COMPUTE 2u
unsigned int AdmissionJobWindowLeadQueue(unsigned int Barrier);
unsigned int AdmissionJobWindowWaitQueue(unsigned int Barrier);

/* OV2 (REG_DWORD Wom1Ov2NoHazardLead = 1 on Q2c, default off): the kicked
 * successor N+1 is a compute job its UMD flagged independent of the render
 * N it follows (same WDDM context, consecutive submissions). Its leading
 * Barrier keeps its queue, slot and address but waits on the 3D stamp
 * value already in memory when the kick was decided, so it is satisfied on
 * arrival and the compute overlaps N's TA/3D. Nothing else changes (window
 * depth, refusals, in-order completion). Refusals (stable receipt values):
 *   1 switch off, 2 not a COMPUTE_ON_3D pair, 3 N+1 not a flagged compute
 *   job, 4 N is not a render (with or without prelude), 5 another or no
 *   WDDM context (or process), 6 the 3D stamp was unreadable or zero (the
 *   Barrier encoder refuses a zero wait; KMD). */
#define ADMISSION_JOB_WINDOW_OV2_WAIVE 0u
#define ADMISSION_JOB_WINDOW_OV2_OFF 1u
#define ADMISSION_JOB_WINDOW_OV2_CLASS 2u
#define ADMISSION_JOB_WINDOW_OV2_NOT_FLAGGED 3u
#define ADMISSION_JOB_WINDOW_OV2_RUNNING_KIND 4u
#define ADMISSION_JOB_WINDOW_OV2_CONTEXT 5u
#define ADMISSION_JOB_WINDOW_OV2_STAMP 6u
#define ADMISSION_JOB_WINDOW_OV2_REASONS 7u
unsigned int AdmissionJobWindowOv2Refusal(unsigned int Enabled,
    unsigned int Barrier, const ADMISSION_JOB_WINDOW_JOB *Running,
    const ADMISSION_JOB_WINDOW_JOB *Next);
/* OV2: the lead Barrier's WaitValue: WaiveStamp (nonzero only for a waived
 * pair) for COMPUTE_ON_3D, else Expected (N's 3D/compute end). */
unsigned int AdmissionJobWindowOv2WaitValue(unsigned int Barrier,
    unsigned int Expected, unsigned int WaiveStamp);
/* OV2: cumulative receipt (device value Wom1Ov2Receipt, written by the
 * worker with the rc102 export): kicked pairs considered, waived, the
 * refusal per reason, and the last waived pair. */
#define ADMISSION_JOB_WINDOW_OV2_RECEIPT_VERSION 1u
typedef struct _ADMISSION_JOB_WINDOW_OV2_RECEIPT {
  unsigned int Version, Bytes;
  unsigned int Considered, Waived;
  unsigned int Refused[ADMISSION_JOB_WINDOW_OV2_REASONS];
  unsigned int LastFence, LastExpected, LastWaitValue;
} ADMISSION_JOB_WINDOW_OV2_RECEIPT;
void AdmissionJobWindowOv2ReceiptInitialize(
    ADMISSION_JOB_WINDOW_OV2_RECEIPT *Receipt);
/* Counts one kicked pair (Refusal as above; Fence N+1, Expected N's 3D
 * stamp, WaitValue the Barrier's). Returns 1 when an export is due: the
 * first 16 considered pairs, then every 256th. */
unsigned int AdmissionJobWindowOv2Note(ADMISSION_JOB_WINDOW_OV2_RECEIPT *Receipt,
    unsigned int Refusal, unsigned int Fence, unsigned int Expected,
    unsigned int WaitValue);

/* OV5 drill (REG_DWORD Wom1Ov5FragWaitDrill = 1, default off): a kicked
 * successor render without compute work (no CDM prelude) behind a running
 * compute job (TA_ON_COMPUTE) keeps its TA lead Barrier and its 3D queue
 * also leads with a Barrier on the same compute stamp. The TA already waits
 * for that stamp, so the 3D Barrier is satisfied on arrival: no behaviour
 * change, only the firmware's acceptance of a compute-stamp Barrier on the
 * 3D queue is exercised (investigation/analysis/OV1-render-compute-
 * overlap.md, OV5 KMD drill). Refusals (stable receipt values; the build
 * then follows today's path):
 *   1 switch off, 2 not a successor build, 3 not TA_ON_COMPUTE, 4 the render
 *   has compute work of its own, 5 the 3D publication is at the ring-entry
 *   limit, 6 the plan's 3D done pointer is out of the ring, 7 the Barrier
 *   could not be built (KMD: encoder, sidecar or running job refused). */
#define ADMISSION_JOB_WINDOW_OV5_PLACE 0u
#define ADMISSION_JOB_WINDOW_OV5_OFF 1u
#define ADMISSION_JOB_WINDOW_OV5_NOT_SUCCESSOR 2u
#define ADMISSION_JOB_WINDOW_OV5_CLASS 3u
#define ADMISSION_JOB_WINDOW_OV5_COMPUTE_WORK 4u
#define ADMISSION_JOB_WINDOW_OV5_RING_LIMIT 5u
#define ADMISSION_JOB_WINDOW_OV5_DONE_POINTER 6u
#define ADMISSION_JOB_WINDOW_OV5_BUILD 7u
#define ADMISSION_JOB_WINDOW_OV5_REASONS 8u
/* D3Entries: the render's 3D work entries; PublishMax: the most entries one
 * publication may carry; D3BarrierDone/RingCapacity: the plan's 3D done
 * pointer with the Barrier and the 3D ring size. */
unsigned int AdmissionJobWindowOv5DrillRefusal(unsigned int Enabled,
    unsigned int Successor, unsigned int Barrier,
    unsigned int ComputeWorkCount, unsigned int D3Entries,
    unsigned int PublishMax, unsigned int D3BarrierDone,
    unsigned int RingCapacity);
/* OV5 drill: cumulative receipt (device value Wom1Ov5DrillReceipt, written
 * by the worker like Wom1Ov2Receipt): successor render builds considered,
 * 3D Barriers built (in the sidecar), placed (accepted first in the 3D
 * publication; the provider's count, copied at export), refusals per
 * reason, and the last built Barrier. */
#define ADMISSION_JOB_WINDOW_OV5_RECEIPT_VERSION 1u
typedef struct _ADMISSION_JOB_WINDOW_OV5_RECEIPT {
  unsigned int Version, Bytes;
  unsigned int Considered, Built, Placed;
  unsigned int Refused[ADMISSION_JOB_WINDOW_OV5_REASONS];
  unsigned int LastFence, LastWaitValue, LastStampSelf, LastOffset;
} ADMISSION_JOB_WINDOW_OV5_RECEIPT;
void AdmissionJobWindowOv5ReceiptInitialize(
    ADMISSION_JOB_WINDOW_OV5_RECEIPT *Receipt);
/* Counts one successor render build (Refusal as above; Fence N+1, the
 * Barrier's WaitValue and StampSelf, its sidecar offset). Returns 1 when an
 * export is due: the first 16 considered builds, then every 256th. */
unsigned int AdmissionJobWindowOv5Note(ADMISSION_JOB_WINDOW_OV5_RECEIPT *Receipt,
    unsigned int Refusal, unsigned int Fence, unsigned int WaitValue,
    unsigned int StampSelf, unsigned int Offset);

/* OV5 step 2, the TA waiver (REG_DWORD Wom1Ov5TaWaiver = 1, default off;
 * investigation/analysis/OV1-render-compute-overlap.md, "OV5 step 2"): a
 * kicked successor render without compute work that its UMD flagged TA
 * independent of the compute job N it follows (same WDDM context and
 * process, TA_ON_COMPUTE) gets no TA lead Barrier; its 3D queue leads with
 * the drill's Barrier on N's compute stamp instead. The TA overlaps N, the
 * 3D still waits for N. Window depth, refusals and in-order completion are
 * unchanged. Refusals (stable receipt values; a refused render is built
 * exactly as without the switch):
 *   1 switch off, 2 not a TA_ON_COMPUTE pair, 3 N+1 not a flagged plain
 *   render, 4 N is not a compute job, 5 another or no WDDM context (or
 *   process), 6 the render has compute work of its own (KMD build), 7 the
 *   3D publication is at the ring-entry limit or a planned done pointer is
 *   out of the ring, 8 the 3D Barrier could not be built (not a successor
 *   build, encoder, sidecar or running job refused). */
#define ADMISSION_JOB_WINDOW_OV5W_WAIVE 0u
#define ADMISSION_JOB_WINDOW_OV5W_OFF 1u
#define ADMISSION_JOB_WINDOW_OV5W_CLASS 2u
#define ADMISSION_JOB_WINDOW_OV5W_NOT_FLAGGED 3u
#define ADMISSION_JOB_WINDOW_OV5W_RUNNING_KIND 4u
#define ADMISSION_JOB_WINDOW_OV5W_CONTEXT 5u
#define ADMISSION_JOB_WINDOW_OV5W_COMPUTE_WORK 6u
#define ADMISSION_JOB_WINDOW_OV5W_RING 7u
#define ADMISSION_JOB_WINDOW_OV5W_BUILD 8u
#define ADMISSION_JOB_WINDOW_OV5W_REASONS 9u
/* The kick-time decision (refusals 1-5). */
unsigned int AdmissionJobWindowOv5WaiverRefusal(unsigned int Enabled,
    unsigned int Barrier, const ADMISSION_JOB_WINDOW_JOB *Running,
    const ADMISSION_JOB_WINDOW_JOB *Next);
/* The build-time check of a waived successor (refusals 6-8 or 0): its
 * compute work, its 3D entries against PublishMax, the plan's 3D done
 * pointer with the Barrier and TA done pointer without the lead against
 * the ring sizes. */
unsigned int AdmissionJobWindowOv5WaiverBuildRefusal(unsigned int Successor,
    unsigned int ComputeWorkCount, unsigned int D3Entries,
    unsigned int PublishMax, unsigned int D3BarrierDone,
    unsigned int D3RingCapacity, unsigned int TaWaivedDone,
    unsigned int TaRingCapacity);
/* May the KMD publish AGX_WIN32_DEVICE_FEATURE_RENDER_TA_INDEPENDENT? Only
 * with the switch, the depth-two kick on (the 3D-queue Barrier is a
 * successor-only build), TA_ON_COMPUTE among its classes (KickClasses 0 =
 * every class) and Q2c (ComputeOwnQueue: compute signals its own Queue's
 * notifier, so a TA that now ends before the compute job cannot count
 * toward that job's notifier threshold). */
unsigned int AdmissionJobWindowOv5WaiverAvailable(unsigned int Switch,
    unsigned int Kick, unsigned int KickClasses, unsigned int ComputeOwnQueue,
    unsigned int HasCompute);
/* OV5 waiver: cumulative receipt (device value Wom1Ov5WaiverReceipt,
 * written by the worker like Wom1Ov5DrillReceipt): successor kicks
 * considered (switch on), waived (built without a TA lead), placed (the
 * provider's count, copied at export), refusals per reason, the last
 * waived render's fence, its 3D Barrier's WaitValue, StampSelf and sidecar
 * offset. */
#define ADMISSION_JOB_WINDOW_OV5W_RECEIPT_VERSION 1u
typedef struct _ADMISSION_JOB_WINDOW_OV5W_RECEIPT {
  unsigned int Version, Bytes;
  unsigned int Considered, Waived, Placed;
  unsigned int Refused[ADMISSION_JOB_WINDOW_OV5W_REASONS];
  unsigned int LastFence, LastWaitValue, LastStampSelf, LastOffset;
} ADMISSION_JOB_WINDOW_OV5W_RECEIPT;
void AdmissionJobWindowOv5WaiverReceiptInitialize(
    ADMISSION_JOB_WINDOW_OV5W_RECEIPT *Receipt);
/* Counts one decision (Refusal as above). Returns 1 when an export is due:
 * the first 16 considered, then every 256th. */
unsigned int AdmissionJobWindowOv5WaiverNote(
    ADMISSION_JOB_WINDOW_OV5W_RECEIPT *Receipt, unsigned int Refusal,
    unsigned int Fence, unsigned int WaitValue, unsigned int StampSelf,
    unsigned int Offset);

/* In-flight window: the jobs the firmware holds, oldest first. */
#define ADMISSION_JOB_WINDOW_ENTRY_FREE 0u
#define ADMISSION_JOB_WINDOW_ENTRY_KICKED 1u
#define ADMISSION_JOB_WINDOW_ENTRY_ENDED 2u   /* own stamps/done reached */
#define ADMISSION_JOB_WINDOW_ENTRY_DROPPED 3u /* firmware recovery dropped it */

typedef struct _ADMISSION_JOB_WINDOW_ENTRY {
  unsigned int Fence;
  unsigned int State;
} ADMISSION_JOB_WINDOW_ENTRY;

typedef struct _ADMISSION_JOB_WINDOW {
  ADMISSION_JOB_WINDOW_ENTRY Entry[ADMISSION_JOB_WINDOW_DEPTH];
  unsigned int Count;
  unsigned int Poisoned;
} ADMISSION_JOB_WINDOW;

void AdmissionJobWindowInitialize(ADMISSION_JOB_WINDOW *Window);
/* A job was kicked: appended as newest. 0 when full or out of order. */
int AdmissionJobWindowKick(ADMISSION_JOB_WINDOW *Window, unsigned int Fence);
/* Firmware evidence for one entry (any order): its own end, or its drop by
 * a recovered Timeout/Fault. A drop poisons the window: nothing more is
 * kicked until it drained. 0 for an unknown fence or a second verdict. */
int AdmissionJobWindowEnded(ADMISSION_JOB_WINDOW *Window, unsigned int Fence,
                            int Dropped);
/* The fence Windows may be told about now: only the oldest entry, once it
 * ended or was dropped (DMA_COMPLETED order); 0 otherwise. */
unsigned int AdmissionJobWindowReportable(const ADMISSION_JOB_WINDOW *Window,
                                          int *Dropped);
/* The oldest entry was reported to Windows: it leaves the window. */
int AdmissionJobWindowReported(ADMISSION_JOB_WINDOW *Window,
                               unsigned int Fence);
/* Reset (TDR): the quiescence wait must prove the end of this fence, the
 * newest kicked one (its done pointers cover every older entry's ring
 * entries); 0 = the firmware holds nothing. */
unsigned int AdmissionJobWindowQuiesceFence(const ADMISSION_JOB_WINDOW *Window);
/* After that proof: every unreported entry is aborted (it may have run, so
 * VidSch must not resubmit it) and the window is empty. Returns the
 * LastAbortedFenceId contribution (the newest unreported fence, 0 = none)
 * and the number of aborted entries. */
unsigned int AdmissionJobWindowResetAbort(ADMISSION_JOB_WINDOW *Window,
                                          unsigned int *Aborted);

/* rc102: cumulative receipt of the wired window (device value
 * Wom1JobWindow, exported with the job timing; decoded by
 * tools/agx_gpu_frame_breakdown.py --kmd-window). */
#define ADMISSION_JOB_WINDOW_RECEIPT_VERSION 3u /* v2: KickFallbacks, v3: KickFallbacksPreempt */
typedef struct _ADMISSION_JOB_WINDOW_RECEIPT {
  unsigned int Version, Bytes;
  unsigned int Kicks;          /* successors kicked while a job ran */
  unsigned int MaxInFlight;    /* jobs the firmware held at once */
  unsigned int Reported[3];    /* reported batches by size: [1], [2] */
  unsigned int Barriers[5];    /* leading Barriers by ADMISSION_JOB_WINDOW_BARRIER_* */
  unsigned int Adopted;        /* kicked successors that became the running job */
  unsigned int KickFailures;   /* a decided kick that failed (scheduler faulted) */
  /* the step that failed: KMD line in bits 0-15, the BeginSuccessorJob
   * refusal (AdmissionGpuvaG3BeginJobAs) in bits 16-23 */
  unsigned int LastKickFailure;
  unsigned int ResetAborts;    /* kicked fences a reset aborted */
  unsigned int LastKickedFence;
  unsigned int LastKickStatus; /* NTSTATUS of the failed step (was Reserved) */
  unsigned int KickFallbacks;  /* v2: failed kicks undone to depth 1 (no fault) */
  unsigned int KickFallbacksPreempt; /* v3: of those, undone in a boundary preemption */
} ADMISSION_JOB_WINDOW_RECEIPT;

/* rc102 (EXP2045): the state at the last failed kick (device value
 * Wom1JobWindowKickFail, written and flushed by the worker at the failure,
 * before the TDR it leads to; decoded by agx_gpu_frame_breakdown.py
 * --kmd-kick-fail). Backend/provider/runtime fields are the shared
 * enumerations' numeric values. */
#define ADMISSION_JOB_WINDOW_KICK_FAILURE_VERSION 2u /* v2: BuildRefusal */
/* Flags */
#define ADMISSION_KICK_FAIL_TERMINAL_PENDING   0x0001u /* backend */
#define ADMISSION_KICK_FAIL_SUCCESSOR_PENDING  0x0002u /* backend */
#define ADMISSION_KICK_FAIL_COMPLETION_READY   0x0004u /* queue runtime */
#define ADMISSION_KICK_FAIL_RECOVERY_PENDING   0x0008u
#define ADMISSION_KICK_FAIL_CHANNEL_FAILED     0x0010u
#define ADMISSION_KICK_FAIL_BM_INITIALIZED     0x0020u
#define ADMISSION_KICK_FAIL_TA_FIRST_RUN       0x0040u
#define ADMISSION_KICK_FAIL_D3_FIRST_RUN       0x0080u
#define ADMISSION_KICK_FAIL_G3_ABORTED         0x0100u /* nested lease undone */
#define ADMISSION_KICK_FAIL_UNSTAGED           0x0200u /* render stamp sequence given back */
#define ADMISSION_KICK_FAIL_FELL_BACK          0x0400u /* undone; runs at depth 1, no reset */
#define ADMISSION_KICK_FAIL_FELL_BACK_PREEMPT  0x0800u /* undone in a boundary preemption: handed back to VidSch */
#define ADMISSION_KICK_FAIL_BUILD_UNDONE       0x1000u /* S4a: the refused successor's build given back */
/* BuildStep: how far the KMD build of the successor got (0: not entered,
 * the provider plan or the platform refused first). */
#define ADMISSION_KICK_BUILD_RENDER_ENTERED    1u
#define ADMISSION_KICK_BUILD_RENDER_OVERLAY    2u
#define ADMISSION_KICK_BUILD_RENDER_STAGED     3u
#define ADMISSION_KICK_BUILD_RENDER_BUILT      4u
#define ADMISSION_KICK_BUILD_RENDER_COMPUTE    5u
#define ADMISSION_KICK_BUILD_RENDER_LEAD       6u
#define ADMISSION_KICK_BUILD_RENDER_FLUSHED    7u
#define ADMISSION_KICK_BUILD_RENDER_BINDINGS   8u  /* the render bindings were read */
#define ADMISSION_KICK_BUILD_COMPUTE_ENTERED   21u
#define ADMISSION_KICK_BUILD_COMPUTE_STREAM    22u
#define ADMISSION_KICK_BUILD_COMPUTE_BUILT     23u
#define ADMISSION_KICK_BUILD_COMPUTE_LEAD      24u
#define ADMISSION_KICK_BUILD_RELOCATED         30u /* the platform relocate returned */
typedef struct _ADMISSION_JOB_WINDOW_KICK_FAILURE {
  unsigned int Version, Bytes;
  unsigned int Failures;        /* failed kicks so far */
  unsigned int Fence, RunningFence;
  unsigned int Line;            /* KMD line of the failed step */
  unsigned int BeginRefusal;    /* AdmissionGpuvaG3BeginJobAs refusal */
  unsigned int Status;          /* NTSTATUS of BeginSuccessorJob */
  unsigned int BackendResult;   /* APPLE_AGX_BACKEND_RUNTIME_RESULT */
  unsigned int BackendPhase, BackendPendingFence;
  unsigned int ProviderPhase, ProviderPendingFence, ProviderSuccessorFence;
  unsigned int SubmitGuard, SubmitRuntimeResult, SuccessorGuard;
  unsigned int RuntimePhase, RuntimePendingFence, RuntimeSuccessorFence;
  unsigned int Flags;           /* ADMISSION_KICK_FAIL_* */
  unsigned int BuildStep;       /* ADMISSION_KICK_BUILD_* */
  unsigned int RunningKind, NextKind, Barrier; /* ADMISSION_JOB_WINDOW_* */
  unsigned int JobWordStamp, JobWordSet;       /* sidecar owner at failure */
  /* v2 (EXP2047): APPLE_AGX_RENDER_BUILD_REFUSAL_* of the shared render
   * builder (0: it did not refuse, or was not reached). */
  unsigned int BuildRefusal;
} ADMISSION_JOB_WINDOW_KICK_FAILURE;

void AdmissionJobWindowReceiptInitialize(ADMISSION_JOB_WINDOW_RECEIPT *Receipt);
/* A successor was kicked with this Barrier while InFlight jobs (now) ran. */
void AdmissionJobWindowReceiptKick(ADMISSION_JOB_WINDOW_RECEIPT *Receipt,
                                   unsigned int Fence, unsigned int Barrier,
                                   unsigned int InFlight);
/* Completions reported to Windows by one poll batch (1 or 2). */
void AdmissionJobWindowReceiptReported(ADMISSION_JOB_WINDOW_RECEIPT *Receipt,
                                       unsigned int Count);

/* rc95 receipt helper: a firmware stamp has reached the expected value
 * (stamps step by 0x100 and wrap; 0 expected is never reached). */
unsigned int AdmissionJobWindowStampReached(unsigned int Observed,
                                            unsigned int Expected);

#endif /* APPLE_AGX_RENDER_JOB_WINDOW_H */
