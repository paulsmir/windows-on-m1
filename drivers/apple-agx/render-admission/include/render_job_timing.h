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

#endif
