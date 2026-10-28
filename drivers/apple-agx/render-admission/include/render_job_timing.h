#ifndef APPLE_AGX_RENDER_JOB_TIMING_H
#define APPLE_AGX_RENDER_JOB_TIMING_H

/* v2 (rc81): CpuBuild/CpuComplete/Flags per slot, WorkerFlags in the
 * header (was Reserved). v3 (rc83): Sub[] build/completion sub-interval
 * stamps appended to each slot, build flags in Flags. */
#define ADMISSION_JOB_TIMING_VERSION 3u
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
   * refusal at its completion in bits 16-23; 0 = allowed or not tried. */
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
