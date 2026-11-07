#ifndef APPLE_AGX_RENDER_JOB_TIMING_H
#define APPLE_AGX_RENDER_JOB_TIMING_H

/* v2 (rc81): CpuBuild/CpuComplete/Flags per slot, WorkerFlags in the
 * header (was Reserved). v3 (rc83): Sub[] build/completion sub-interval
 * stamps appended to each slot, build flags in Flags. v4 (rc95): five more
 * Sub stamps (the depth-2 receipts below) appended; v3 offsets unchanged. */
#define ADMISSION_JOB_TIMING_VERSION 4u
#define ADMISSION_JOB_TIMING_CAPACITY 64u

typedef enum _ADMISSION_JOB_PHASE {
  AdmissionJobPhaseSubmit,
  AdmissionJobPhaseWorker,
  AdmissionJobPhaseBeginBefore,
  AdmissionJobPhaseBeginAfter,
  AdmissionJobPhaseBackendBefore,
  AdmissionJobPhaseKickTa,
  AdmissionJobPhaseKick3d,
  AdmissionJobPhaseBackendAfter,
  AdmissionJobPhaseFirstProgress,
  AdmissionJobPhaseComplete,
  AdmissionJobPhaseJobEnd,
  AdmissionJobPhaseNotify,
  ADMISSION_JOB_PHASE_COUNT
} ADMISSION_JOB_PHASE;

/* rc83 (EXP2012: BackendBefore -> Kick3d 266 us, Complete -> JobEnd 77 us
 * per render job): QPC stamps inside the build and the completion, each
 * recorded once, 0 = not reached. Build, in order: the backend image
 * staged the job (AdmissionBackendImageStageJob), the template objects
 * were copied to the firmware-shared queue objects, relocations/stats/
 * threshold applied, the buffer-manager state restored (managed G4 only),
 * the v5 compute prelude built, the flushes done (end of the external
 * build; Kick3d follows). Completion: the buffer-manager state saved
 * (AdmissionSaveG4Manager), the arena released (Release*Restore, before
 * Notify). */
typedef enum _ADMISSION_JOB_SUB {
  AdmissionJobSubStaged,
  AdmissionJobSubCopied,
  AdmissionJobSubRelocated,
  AdmissionJobSubManaged,
  AdmissionJobSubPrelude,
  AdmissionJobSubFlushed,
  AdmissionJobSubSaved,
  AdmissionJobSubReleased,
  /* v4 (rc95, Wom1JobDepth2 receipt mode): while this job ran, its
   * successor became Prebound (the earliest a depth-2 kick could happen);
   * the first poll that saw this job's TA done pointer reach its expected
   * value and the TA stamp reach its expected stamp; the same for the
   * compute prelude of a render with one (done pointer, stamp). A stamp
   * that lags its done pointer until the whole job completes is held back
   * by the notifier threshold (U4). */
  AdmissionJobSubSuccessorBound,
  AdmissionJobSubTaDone,
  AdmissionJobSubTaStamp,
  AdmissionJobSubPreludeDone,
  AdmissionJobSubPreludeStamp,
  ADMISSION_JOB_SUB_COUNT
} ADMISSION_JOB_SUB;

typedef struct _ADMISSION_JOB_TIMING_SLOT {
  unsigned int Fence, ProcessId, DmaBytes, DelayCount;
  unsigned long long ContextToken;
  unsigned long long TargetBytes;
  unsigned long long Qpc[ADMISSION_JOB_PHASE_COUNT];
  unsigned long long FirmwareTaStart, FirmwareTaEnd;
  unsigned long long Firmware3dStart, Firmware3dEnd;
  unsigned int FirmwareValid, Status;
  /* v2 (rc81): the worker's processor when it began the build
   * (BackendBefore) and when it observed completion (Complete), as
   * AdmissionJobTimingCpuValue; 0 = not recorded. */
  unsigned int CpuBuild, CpuComplete;
  /* v2: ADMISSION_JOB_FLAG_* noted by BeginJob (v3: and the build). */
  unsigned int Flags;
  /* rc90 (was Reserved, same layout): the build-ahead refusals while this
   * job ran, AdmissionJobAhead*Refusal values: early release in bits 0-7,
   * the last prebind refusal for its successor in bits 8-15, the chain
   * refusal at its completion in bits 16-23; 0 = allowed or not tried.
   * rc95: bits 24-31 the depth-2 kick refusal for its Prebound successor
   * (AdmissionJobWindowKickRefusal; FLAG_DEPTH2_ELIGIBLE when 0). */
  unsigned int AheadRefusals;
  /* v3: ADMISSION_JOB_SUB stamps. */
  unsigned long long Sub[ADMISSION_JOB_SUB_COUNT];
} ADMISSION_JOB_TIMING_SLOT;

/* Set by BeginJob: BEGIN_JOB always; MAPPING_UNCHANGED when the process
 * graph's MappingGeneration still equals the one the submission was parsed
 * and bound against (no page-table update in between); PRIVATE_SCENE for a
 * render with a private scene, whose access also depends on scene storage
 * and TVB block counts that the generation does not cover. */
#define ADMISSION_JOB_FLAG_MAPPING_UNCHANGED 0x1u
#define ADMISSION_JOB_FLAG_PRIVATE_SCENE 0x2u
#define ADMISSION_JOB_FLAG_BEGIN_JOB 0x4u
/* v3, set by the render build: the job carried the InitBM command, it
 * (re)initialised the queues, it had a v5 compute prelude, it used the
 * second copy of the slotted objects, its build restored saved
 * buffer-manager state. */
#define ADMISSION_JOB_FLAG_INIT_BM 0x8u
#define ADMISSION_JOB_FLAG_INIT_QUEUES 0x10u
#define ADMISSION_JOB_FLAG_PRELUDE 0x20u
#define ADMISSION_JOB_FLAG_SLOT1 0x40u
#define ADMISSION_JOB_FLAG_MANAGER_RESTORED 0x80u
/* rc90 build-ahead (Wom1JobBuildAhead): the job's image was released right
 * after its kick; it was bound while its predecessor ran; the worker that
 * ran its predecessor claimed it at that completion (no dispatch hop). */
#define ADMISSION_JOB_FLAG_AHEAD_RELEASED 0x100u
#define ADMISSION_JOB_FLAG_AHEAD_PREBOUND 0x200u
#define ADMISSION_JOB_FLAG_AHEAD_CHAINED 0x400u
#define ADMISSION_JOB_AHEAD_SHIFT_RELEASE 0u
#define ADMISSION_JOB_AHEAD_SHIFT_PREBIND 8u
#define ADMISSION_JOB_AHEAD_SHIFT_CHAIN 16u
/* rc95 (Wom1JobDepth2 receipt mode): the successor bound while this job ran
 * could have been kicked to the firmware at once (refusal 0). */
#define ADMISSION_JOB_FLAG_DEPTH2_ELIGIBLE 0x800u
#define ADMISSION_JOB_AHEAD_SHIFT_DEPTH2 24u
/* rc98 (Wom1JobDepth2Caps): the job's compute work (envelope compute or v5
 * prelude) was built in the sidecar's job-word set B; its depth-2 decision
 * was taken with the rc98 capabilities declared. */
#define ADMISSION_JOB_FLAG_SIDECAR_SET_B 0x1000u
#define ADMISSION_JOB_FLAG_DEPTH2_CAPS 0x2000u
/* rc102 (Wom1JobDepth2Kick, the wired depth-2 window; layout still v4):
 * the job was kicked to the firmware while its predecessor ran (its
 * BackendAfter is then earlier than the predecessor's Complete), its
 * completion was reported in the same poll batch as its predecessor's
 * (U4: the notifier deferred both ends), and the class of its leading
 * Barrier (ADMISSION_JOB_WINDOW_BARRIER_*) in bits 17-19. */
#define ADMISSION_JOB_FLAG_DEPTH2_KICKED 0x4000u
#define ADMISSION_JOB_FLAG_DEPTH2_BATCHED 0x8000u
#define ADMISSION_JOB_FLAG_DEPTH2_BARRIER_SHIFT 17u
#define ADMISSION_JOB_FLAG_DEPTH2_BARRIER_MASK 0xe0000u

/* q1: opt-in Wom1ManagerReceipt; successful managed builds only. Restored
 * (0x80) is an independent subset of rebind, not a third exclusive class. */
#define ADMISSION_JOB_FLAG_MANAGER_LIVE 0x100000u
#define ADMISSION_JOB_FLAG_MANAGER_REBIND 0x200000u
unsigned int AdmissionJobTimingManagerFlags(unsigned int Enabled,
    unsigned int Managed, unsigned int Live);

/* Header WorkerFlags: the job worker ran on the dedicated thread; that
 * thread is pinned to the processors of efficiency class (bits 8-15),
 * count of them in bits 16-23. */
#define ADMISSION_JOB_WORKER_THREAD 0x1u
#define ADMISSION_JOB_WORKER_PINNED 0x2u

typedef struct _ADMISSION_JOB_TIMING_STATE {
  unsigned int Version, Bytes, Count, WorkerFlags;
  unsigned long long QpcFrequency;
  unsigned long long LastExportQpcTicks;
  ADMISSION_JOB_TIMING_SLOT Slot[ADMISSION_JOB_TIMING_CAPACITY];
} ADMISSION_JOB_TIMING_STATE;

void AdmissionJobTimingInitialize(ADMISSION_JOB_TIMING_STATE *State,
    unsigned long long QpcFrequency);
int AdmissionJobTimingBegin(ADMISSION_JOB_TIMING_STATE *State,
    unsigned int Fence, unsigned int ProcessId,
    unsigned long long ContextToken, unsigned int DmaBytes,
    unsigned long long Qpc);
const ADMISSION_JOB_TIMING_SLOT *AdmissionJobTimingFind(
    const ADMISSION_JOB_TIMING_STATE *State, unsigned int Fence);
int AdmissionJobTimingMark(ADMISSION_JOB_TIMING_STATE *State,
    unsigned int Fence, ADMISSION_JOB_PHASE Phase, unsigned long long Qpc);
int AdmissionJobTimingDelay(ADMISSION_JOB_TIMING_STATE *State,
    unsigned int Fence);
int AdmissionJobTimingTarget(ADMISSION_JOB_TIMING_STATE *State,
    unsigned int Fence, unsigned long long Bytes);
/* Processor index | efficiency class << 8 | 1 << 31; 0 when either does
 * not fit one byte. */
unsigned int AdmissionJobTimingCpuValue(unsigned int Index,
    unsigned int EfficiencyClass);
/* Complete = 0: CpuBuild, else CpuComplete; the first non-zero value
 * wins. */
int AdmissionJobTimingCpu(ADMISSION_JOB_TIMING_STATE *State,
    unsigned int Fence, int Complete, unsigned int Value);
int AdmissionJobTimingFlag(ADMISSION_JOB_TIMING_STATE *State,
    unsigned int Fence, unsigned int Flag);
/* rc90: store Refusal (low 8 bits; 0xff when larger) in AheadRefusals at
 * Shift (ADMISSION_JOB_AHEAD_SHIFT_*), replacing the previous value. */
int AdmissionJobTimingAheadRefusal(ADMISSION_JOB_TIMING_STATE *State,
    unsigned int Fence, unsigned int Shift, unsigned int Refusal);
/* v3: the first stamp of Sub wins. */
int AdmissionJobTimingSub(ADMISSION_JOB_TIMING_STATE *State,
    unsigned int Fence, ADMISSION_JOB_SUB Sub, unsigned long long Qpc);

/* rc83: EXP2010-EXP2012 exported Wom1JobTiming971 after the benchmark had
 * ended, so its 64 slots held DWM's idle-desktop jobs (EXP2012: 54 dwm, 1
 * explorer, 0 GravityMark; the ring spanned 1.75 s). The export also
 * keeps Wom1JobTiming971Hot, written only from a dense ring: every slot
 * used and all 64 submissions within ADMISSION_JOB_TIMING_HOT_SPAN_MS (a
 * GravityMark HD/FHD frame of 9 jobs at 24-40 FPS fills 64 slots in
 * 180-300 ms). Returns 1 for a dense ring. */
#define ADMISSION_JOB_TIMING_HOT_SPAN_MS 500u
int AdmissionJobTimingDense(const ADMISSION_JOB_TIMING_STATE *State);

/* rc81 (EXP2010): one processor core of the topology (a
 * PROCESSOR_RELATIONSHIP GroupMask) and its efficiency class. */
typedef struct _ADMISSION_JOB_WORKER_CORE {
  unsigned long long Mask;
  unsigned short Group;
  unsigned char EfficiencyClass;
  unsigned char Reserved[5];
} ADMISSION_JOB_WORKER_CORE;
/* The job worker's processors (EXP2010: the CPU-bound build, completion
 * and BeginJob took 3.8x longer on an Icestorm, efficiency class 0, than on
 * a Firestorm, class 1, core): every core of the highest efficiency class
 * in the processor group of the first such core; returns 1 with *Group and
 * *Mask. Returns 0 and leaves them unchanged (do not pin) for no cores, one
 * class only, or no processor in the top class. */
int AdmissionJobWorkerAffinity(const ADMISSION_JOB_WORKER_CORE *Cores,
    unsigned int Count, unsigned short *Group, unsigned long long *Mask);

/* EXP1073: completion poll schedule of the job worker. EXP1072: 62 of 63
 * jobs completed inside the first 1 ms sleep after the kick, so detection
 * cost one timer tick (BackendAfter->Complete median 1510 us, minimum
 * 1041 us). The worker busy-polls every STEP microseconds for the first
 * WINDOW microseconds after the kick, then sleeps one tick per poll.
 * rc72 (EXP1989, GravityMark D3D12): with a 2 ms window every job of 2 ms
 * or more was seen ~1.1 ms after its firmware end (6.3/7.2 ms per HD/FHD
 * frame; jobs under 2 ms: 0.03 ms). The device has no firmware completion
 * interrupt (only the synthetic scanout edge), so the window now covers the
 * longest EXP1989 job (45.2 ms). Each step returns to the worker's loop
 * condition (Stopping, Resetting, TDR drill, job phase); a job outliving
 * the window (hung GPU awaiting TDR) busy-waits at most WINDOW, then
 * sleeps ticks. Cost: one vCPU of 8 while a job runs. */
#define ADMISSION_JOB_POLL_SPIN_WINDOW_US 50000u
#define ADMISSION_JOB_POLL_SPIN_STEP_US 20u
/* Microseconds to busy-wait before the next poll, or 0 to sleep one tick. */
unsigned int AdmissionJobPollStallUs(unsigned long long ElapsedUs);
/* rc100: predicted-sleep completion poll. The J313 is fanless: the CPU and
 * the GPU share one ~10 W budget, and the rc72 schedule above keeps a
 * pinned P-core 100 % busy for the first 50 ms of every job (every
 * GravityMark job is shorter), while the firmware lowers the GPU clock
 * (EXP2031c: 49 % of the run at 1278 MHz against 92 % cool). The guest
 * takes no completion interrupt today (rc100 REPORT section 1), so the
 * worker still polls, but sleeps through the predictable part of a job:
 *
 * - a job's key is its context, kind and DMA bytes plus the kind and DMA
 *   bytes of the context's two previous jobs (a frame's position: EXP2034
 *   compute jobs all carry 72 DMA bytes, 0.5-13 ms by position);
 * - its prediction is the SHORTEST of the key's last HISTORY durations: a
 *   job that ends before the worker wakes costs latency, one that ends
 *   after it costs only spin, so the predictor errs early;
 * - the worker sleeps (high-resolution timer) until GUARD before the
 *   prediction, less the timer's learned lateness, in chunks of at most
 *   SLEEP_CHUNK so the loop condition (stop, reset, drill) and the RTKit
 *   drain run at least as often as the old 1 ms tick; then it spins with
 *   the rc72 step; GUARD is a tenth of the prediction within
 *   [GUARD_MIN, GUARD_MAX], so the spin stays about 10 % of the job;
 * - a job outliving prediction + GUARD is polled every OVERRUN_SLEEP
 *   (spun on, as rc72, without a high-resolution timer);
 * - a key without two samples spins as rc72 (the
 *   first frames learn exact durations); from SPIN_WINDOW on, every mode
 *   sleeps ticks as before (hung GPU awaiting TDR).
 *
 * Device value Wom1JobPollSpinUs (REG_DWORD, read at start): absent =
 * predictive; any value = the rc72 schedule with that spin window
 * (50000 = the pre-rc100 behaviour, 0 = tick sleeps only). */
#define ADMISSION_JOB_POLL_VERSION 1u
#define ADMISSION_JOB_POLL_PREDICTIVE 0xffffffffu
#define ADMISSION_JOB_POLL_KEYS 64u
#define ADMISSION_JOB_POLL_HISTORY 4u
#define ADMISSION_JOB_POLL_CONTEXTS 8u
#define ADMISSION_JOB_POLL_LATENESS 8u
#define ADMISSION_JOB_POLL_GUARD_MIN_US 50u
#define ADMISSION_JOB_POLL_GUARD_MAX_US 300u
#define ADMISSION_JOB_POLL_SLEEP_CHUNK_US 1000u
#define ADMISSION_JOB_POLL_MIN_SLEEP_US 100u
#define ADMISSION_JOB_POLL_OVERRUN_SLEEP_US 100u
/* Initial timer lateness: a high-resolution EX_TIMER, or a 1 ms tick
 * (KeDelayExecutionThread fallback). */
#define ADMISSION_JOB_POLL_HIGHRES_SLACK_US 100u
#define ADMISSION_JOB_POLL_TICK_SLACK_US 1000u
#define ADMISSION_JOB_POLL_LATENESS_MAX_US 2000u

/* AdmissionJobPollPlan actions. */
#define ADMISSION_JOB_POLL_TICK 0u   /* sleep one timer tick (old path) */
#define ADMISSION_JOB_POLL_SPIN 1u   /* KeStallExecutionProcessor(*Us) */
#define ADMISSION_JOB_POLL_SLEEP 2u  /* high-resolution sleep of *Us */

/* Header Flags. */
#define ADMISSION_JOB_POLL_FLAG_HIGHRES 0x1u  /* EX_TIMER allocated */

typedef struct _ADMISSION_JOB_POLL_ENTRY {
  unsigned long long Key;
  unsigned int Us[ADMISSION_JOB_POLL_HISTORY];
  unsigned int Count, Next, Used, Reserved;
} ADMISSION_JOB_POLL_ENTRY;

typedef struct _ADMISSION_JOB_POLL_CONTEXT {
  unsigned long long Context;
  unsigned int Previous[2];
  unsigned int Used, Reserved;
} ADMISSION_JOB_POLL_CONTEXT;

/* Cumulative since start (read before and after a run, subtract). JobUs:
 * kick -> completion seen; SleptUs: measured inside sleeps (the rest of
 * JobUs is the worker's busy time); Misses: completions seen by the first
 * poll after a sleep (latency up to that sleep, duration learned as a lower
 * bound; includes the overrun polls); MissWindowUs: their
 * last-running-poll-to-seen spans. Spins/Ticks: plans returned. */
typedef struct _ADMISSION_JOB_POLL_STATS {
  unsigned long long Jobs, PredictedJobs, Sleeps, Spins, Ticks, Misses;
  unsigned long long JobUs, SleptUs, SleepRequestedUs, LateUs, LateMaxUs;
  unsigned long long MissWindowUs;
} ADMISSION_JOB_POLL_STATS;

typedef struct _ADMISSION_JOB_POLL_STATE {
  unsigned int Version, Bytes, SpinWindowUs, Flags;
  unsigned int Clock, LatenessNext;
  unsigned int Lateness[ADMISSION_JOB_POLL_LATENESS];
  ADMISSION_JOB_POLL_STATS Stats;
  ADMISSION_JOB_POLL_CONTEXT Context[ADMISSION_JOB_POLL_CONTEXTS];
  ADMISSION_JOB_POLL_ENTRY Entry[ADMISSION_JOB_POLL_KEYS];
} ADMISSION_JOB_POLL_STATE;

/* SpinWindowUs: ADMISSION_JOB_POLL_PREDICTIVE or the rc72 window. */
void AdmissionJobPollInitialize(ADMISSION_JOB_POLL_STATE *State,
    unsigned int SpinWindowUs, unsigned int Flags);
/* A job starts: its key (from the context's two previous jobs), then the
 * job becomes the context's newest. 0 when State is NULL. */
unsigned long long AdmissionJobPollBegin(ADMISSION_JOB_POLL_STATE *State,
    unsigned long long ContextToken, unsigned int Kind, unsigned int DmaBytes);
/* Predicted duration of Key in microseconds; 0 = no prediction. */
unsigned int AdmissionJobPollPredict(const ADMISSION_JOB_POLL_STATE *State,
    unsigned long long Key);
/* The job of Key (predicted PredictedUs) was seen complete DetectUs after
 * its kick by a poll that followed a SLEEP (AfterSleep) or a spin step;
 * LastPollUs: the previous poll, which saw it running. Learns DetectUs
 * after a spin step (exact within one step), LastPollUs after a sleep (a
 * lower bound: the predictor errs early), and counts. */
void AdmissionJobPollFinish(ADMISSION_JOB_POLL_STATE *State,
    unsigned long long Key, unsigned int PredictedUs,
    unsigned long long DetectUs, unsigned long long LastPollUs,
    int AfterSleep);
/* The timer's lateness the planner subtracts: the largest of the last
 * LATENESS sleeps' (actual - requested). */
unsigned int AdmissionJobPollSlackUs(const ADMISSION_JOB_POLL_STATE *State);
void AdmissionJobPollNoteSleep(ADMISSION_JOB_POLL_STATE *State,
    unsigned long long RequestedUs, unsigned long long ActualUs);
/* Next step for a job ElapsedUs after its kick, predicted PredictedUs (0 =
 * none); a SLEEP never exceeds SleepCapUs (0 = SLEEP_CHUNK). */
unsigned int AdmissionJobPollPlan(ADMISSION_JOB_POLL_STATE *State,
    unsigned long long ElapsedUs, unsigned int PredictedUs,
    unsigned int SleepCapUs, unsigned int *Us);
unsigned long long AdmissionJobTimingDeltaUs(
    const ADMISSION_JOB_TIMING_STATE *State,
    const ADMISSION_JOB_TIMING_SLOT *Slot,
    ADMISSION_JOB_PHASE First, ADMISSION_JOB_PHASE Last);

/* GPU timestamps (docs/superpowers/plans/2026-10-16-gpu-timestamps.md):
 * the firmware-written start/end of completed jobs until the UMD fetches
 * them (escape). The UMD names its batch by the G4 envelope's command GPU
 * VA (unique while the batch is in flight: its command BO returns to the
 * pool only after the UMD retired it); the record also holds the owning
 * process and the submission fence (newest record wins). Render jobs: the
 * 3D work's timestamp_pointers (template objects 28/29); envelope compute
 * jobs: the sidecar block's job start/end (EXP1342). Values are
 * firmware ticks; Qpc is the KMD's performance counter when it took them
 * at completion (EXP1112: the firmware timebase runs at the counter's
 * frequency, offset from the guest's QPC by the hypervisor's stolen time),
 * so End - Qpc estimates that offset. */
#define ADMISSION_GPU_TIMESTAMP_CAPACITY 64u
#define ADMISSION_GPU_TIMESTAMP_RENDER 1u
#define ADMISSION_GPU_TIMESTAMP_COMPUTE 2u
typedef struct _ADMISSION_GPU_TIMESTAMP {
  unsigned int Fence, ProcessId, Kind, Reserved;
  unsigned long long CommandVa, Start, End, Qpc;
} ADMISSION_GPU_TIMESTAMP;
typedef struct _ADMISSION_GPU_TIMESTAMP_RING {
  ADMISSION_GPU_TIMESTAMP Slot[ADMISSION_GPU_TIMESTAMP_CAPACITY];
} ADMISSION_GPU_TIMESTAMP_RING;
int AdmissionGpuTimestampRecord(ADMISSION_GPU_TIMESTAMP_RING *Ring,
    unsigned int Fence, unsigned int ProcessId, unsigned long long CommandVa,
    unsigned int Kind, unsigned long long Start, unsigned long long End,
    unsigned long long Qpc);
/* The newest record of ProcessId's batch CommandVa; CommandVa 0: the
 * newest record of any job (calibration: its End and Qpc give the
 * firmware-minus-counter offset; the UMD gets no other field of it). */
int AdmissionGpuTimestampFind(const ADMISSION_GPU_TIMESTAMP_RING *Ring,
    unsigned int ProcessId, unsigned long long CommandVa,
    ADMISSION_GPU_TIMESTAMP *Out);

/* C11d (D3D12-C11-queries.md): DxgkDdiCalibrateGpuClock values, the D3D12
 * runtime's GetTimestampFrequency/GetClockCalibration source
 * (D3DKMTQueryClockCalibration). The UMD's Gallium timestamps are
 * nanoseconds of the firmware timebase (agx_get_gpu_timestamp: counter time
 * plus the newest job's firmware-minus-counter offset), so the GPU clock is
 * reported in nanoseconds: *GpuFrequency = 1e9, *GpuClockNs = (Qpc + newest
 * End - Qpc offset, 0 before any job, clamped at 0) at Frequency. Returns 0
 * for a zero frequency. */
int AdmissionGpuClockCalibrate(const ADMISSION_GPU_TIMESTAMP_RING *Ring,
    unsigned long long Frequency, unsigned long long Qpc,
    unsigned long long *GpuFrequency, unsigned long long *GpuClockNs);

/* The escape the UMD sends after its batch completed (DxgkDdiEscape, in the
 * caller's process). Found 1 with Kind/Start/End in firmware ticks;
 * Frequency is the tick frequency (the system counter). CommandVa 0
 * calibrates: End and Qpc of the newest job, Kind and Start 0. */
#define ADMISSION_GPU_TIMESTAMP_QUERY_MAGIC 0x53545041u /* "APTS" */
#define ADMISSION_GPU_TIMESTAMP_QUERY_VERSION 1u
typedef struct _ADMISSION_GPU_TIMESTAMP_QUERY {
  unsigned int Magic, Version, Bytes, Found;
  unsigned long long CommandVa;
  unsigned int Kind, Reserved;
  unsigned long long Start, End, Frequency, Qpc;
  /* EXP1341 receipt-only: the newest compute job's capture -- its fence,
   * command VA, whether it carried a timestamp block, and the block's job
   * start/end and requested start/end as the firmware left them. */
  unsigned int ComputeFence, ComputeRequested;
  unsigned long long ComputeCommandVa, ComputeWords[4];
} ADMISSION_GPU_TIMESTAMP_QUERY;

/* RC35: bounded asynchronous telemetry, independent of application queries.
 * QPC endpoints are submit and first KMD completion observation. Firmware
 * values are raw stage ticks, not calibrated QPC. Status 0 means success.
 * Loss counts adapter-wide overwritten records (not guessed per-process loss).
 * Version 2 shares the APTS magic but never changes the version-1 query ABI.
 * Version 3 (rc79): PreludeStart/PreludeEnd are the firmware start/end of a
 * v5 render's compute prelude (the CDM work the TA waits for behind its
 * Barrier), raw ticks of the same timebase as TaEnd; 0 for a render without
 * compute work, for compute jobs (Start/End already are theirs), when the
 * device value Wom1PreludeTimestampsOff = 1, or on failure.
 * TvbOverflows/TvbMaxPages: a successful render's firmware buffer-manager
 * Stats overflow_count and max_pages (Asahi fw/buffer.rs Stats 0x8/0x0) as
 * the completion flushed them: cumulative per process manager, so the
 * difference between two of the process's renders is the later render's
 * TVB overflows, i.e. partial renders; 0 for compute jobs. */
#define ADMISSION_GPU_COMPLETION_VERSION 3u
#define ADMISSION_GPU_COMPLETION_CAPACITY 256u
#define ADMISSION_GPU_COMPLETION_PAGE 32u
typedef struct _ADMISSION_GPU_COMPLETION {
  unsigned long long Sequence, CommandVa, SubmitQpc, CompleteQpc;
  unsigned long long Start, End, TaStart, TaEnd;
  unsigned long long PreludeStart, PreludeEnd;
  unsigned int Fence, ProcessId, Kind, Status;
  unsigned int TvbOverflows, TvbMaxPages;
} ADMISSION_GPU_COMPLETION;
typedef struct _ADMISSION_GPU_COMPLETION_RING {
  unsigned long long Sequence, MissingTiming;
  ADMISSION_GPU_COMPLETION Record[ADMISSION_GPU_COMPLETION_CAPACITY];
} ADMISSION_GPU_COMPLETION_RING;
typedef struct _ADMISSION_GPU_COMPLETION_QUERY {
  unsigned int Magic, Version, Bytes, Count;
  unsigned long long Cursor, Newest, Lost, Frequency, MissingTiming;
  unsigned int Reset, Reserved;
  ADMISSION_GPU_COMPLETION Record[ADMISSION_GPU_COMPLETION_PAGE];
} ADMISSION_GPU_COMPLETION_QUERY;
int AdmissionGpuCompletionRecord(ADMISSION_GPU_COMPLETION_RING *Ring,
    const ADMISSION_GPU_COMPLETION *Record);
void AdmissionGpuCompletionRead(const ADMISSION_GPU_COMPLETION_RING *Ring,
    unsigned int ProcessId, ADMISSION_GPU_COMPLETION_QUERY *Query);

#endif
