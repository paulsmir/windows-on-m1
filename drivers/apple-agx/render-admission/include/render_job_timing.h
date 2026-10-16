#ifndef APPLE_AGX_RENDER_JOB_TIMING_H
#define APPLE_AGX_RENDER_JOB_TIMING_H

#define ADMISSION_JOB_TIMING_VERSION 1u
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

typedef struct _ADMISSION_JOB_TIMING_SLOT {
  unsigned int Fence, ProcessId, DmaBytes, DelayCount;
  unsigned long long ContextToken;
  unsigned long long TargetBytes;
  unsigned long long Qpc[ADMISSION_JOB_PHASE_COUNT];
  unsigned long long FirmwareTaStart, FirmwareTaEnd;
  unsigned long long Firmware3dStart, Firmware3dEnd;
  unsigned int FirmwareValid, Status;
} ADMISSION_JOB_TIMING_SLOT;

typedef struct _ADMISSION_JOB_TIMING_STATE {
  unsigned int Version, Bytes, Count, Reserved;
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
/* EXP1073: completion poll schedule of the job worker. EXP1072: 62 of 63
 * jobs completed inside the first 1 ms sleep after the kick, so detection
 * cost one timer tick (BackendAfter->Complete median 1510 us, minimum
 * 1041 us). The worker busy-polls every STEP microseconds for the first
 * WINDOW microseconds after the kick, then sleeps one tick per poll. */
#define ADMISSION_JOB_POLL_SPIN_WINDOW_US 2000u
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
 * 3D work's timestamp_pointers (template objects 28/29); compute jobs that
 * requested them: the sidecar block's requested start/end. Values are
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
/* The newest record of ProcessId's batch CommandVa. */
int AdmissionGpuTimestampFind(const ADMISSION_GPU_TIMESTAMP_RING *Ring,
    unsigned int ProcessId, unsigned long long CommandVa,
    ADMISSION_GPU_TIMESTAMP *Out);

/* The escape the UMD sends after its batch completed (DxgkDdiEscape, in the
 * caller's process). Found 1 with Kind/Start/End in firmware ticks;
 * Frequency is the tick frequency (the system counter). */
#define ADMISSION_GPU_TIMESTAMP_QUERY_MAGIC 0x53545041u /* "APTS" */
#define ADMISSION_GPU_TIMESTAMP_QUERY_VERSION 1u
typedef struct _ADMISSION_GPU_TIMESTAMP_QUERY {
  unsigned int Magic, Version, Bytes, Found;
  unsigned long long CommandVa;
  unsigned int Kind, Reserved;
  unsigned long long Start, End, Frequency, Qpc;
} ADMISSION_GPU_TIMESTAMP_QUERY;

#endif
