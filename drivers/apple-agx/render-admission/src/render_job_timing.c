#include "render_job_timing.h"

static void timing_zero(void *data, unsigned int bytes) {
  unsigned char *p = (unsigned char *)data;
  while (bytes--) *p++ = 0u;
}

void AdmissionJobTimingInitialize(ADMISSION_JOB_TIMING_STATE *state,
    unsigned long long frequency) {
  if (!state) return;
  timing_zero(state, sizeof(*state));
  state->Version = ADMISSION_JOB_TIMING_VERSION;
  state->Bytes = sizeof(*state);
  state->QpcFrequency = frequency;
}

int AdmissionJobTimingBegin(ADMISSION_JOB_TIMING_STATE *state,
    unsigned int fence, unsigned int pid, unsigned long long context,
    unsigned int bytes, unsigned long long qpc) {
  ADMISSION_JOB_TIMING_SLOT *slot;
  if (!state || !fence || !qpc || !state->QpcFrequency) return 0;
  slot = &state->Slot[state->Count % ADMISSION_JOB_TIMING_CAPACITY];
  timing_zero(slot, sizeof(*slot));
  slot->Fence = fence;
  slot->ProcessId = pid;
  slot->ContextToken = context;
  slot->DmaBytes = bytes;
  slot->Qpc[AdmissionJobPhaseSubmit] = qpc;
  ++state->Count;
  return 1;
}

const ADMISSION_JOB_TIMING_SLOT *AdmissionJobTimingFind(
    const ADMISSION_JOB_TIMING_STATE *state, unsigned int fence) {
  unsigned int i;
  if (!state || !fence) return 0;
  for (i = 0; i < ADMISSION_JOB_TIMING_CAPACITY; ++i)
    if (state->Slot[i].Fence == fence) return &state->Slot[i];
  return 0;
}

int AdmissionJobTimingMark(ADMISSION_JOB_TIMING_STATE *state,
    unsigned int fence, ADMISSION_JOB_PHASE phase, unsigned long long qpc) {
  ADMISSION_JOB_TIMING_SLOT *slot =
      (ADMISSION_JOB_TIMING_SLOT *)AdmissionJobTimingFind(state, fence);
  if (!slot || (int)phase < 0 || phase >= ADMISSION_JOB_PHASE_COUNT || !qpc ||
      slot->Qpc[phase]) return 0;
  slot->Qpc[phase] = qpc;
  return 1;
}

int AdmissionJobTimingDelay(ADMISSION_JOB_TIMING_STATE *state,
    unsigned int fence) {
  ADMISSION_JOB_TIMING_SLOT *slot =
      (ADMISSION_JOB_TIMING_SLOT *)AdmissionJobTimingFind(state, fence);
  if (!slot || slot->DelayCount == 0xffffffffu) return 0;
  ++slot->DelayCount;
  return 1;
}

int AdmissionJobTimingTarget(ADMISSION_JOB_TIMING_STATE *state,
    unsigned int fence, unsigned long long bytes) {
  ADMISSION_JOB_TIMING_SLOT *slot =
      (ADMISSION_JOB_TIMING_SLOT *)AdmissionJobTimingFind(state, fence);
  if (!slot || !bytes || slot->TargetBytes) return 0;
  slot->TargetBytes = bytes;
  return 1;
}

unsigned long long AdmissionJobTimingDeltaUs(
    const ADMISSION_JOB_TIMING_STATE *state,
    const ADMISSION_JOB_TIMING_SLOT *slot,
    ADMISSION_JOB_PHASE first, ADMISSION_JOB_PHASE last) {
  unsigned long long delta;
  if (!state || !slot || (int)first < 0 || (int)last < 0 ||
      first >= ADMISSION_JOB_PHASE_COUNT ||
      last >= ADMISSION_JOB_PHASE_COUNT || !state->QpcFrequency ||
      !slot->Qpc[first] || slot->Qpc[last] < slot->Qpc[first]) return 0;
  delta = slot->Qpc[last] - slot->Qpc[first];
  return delta / state->QpcFrequency * 1000000ULL +
      delta % state->QpcFrequency * 1000000ULL / state->QpcFrequency;
}

unsigned int AdmissionJobPollStallUs(unsigned long long ElapsedUs) {
  return ElapsedUs < ADMISSION_JOB_POLL_SPIN_WINDOW_US ?
      ADMISSION_JOB_POLL_SPIN_STEP_US : 0u;
}

int AdmissionGpuTimestampRecord(ADMISSION_GPU_TIMESTAMP_RING *ring,
    unsigned int fence, unsigned int pid, unsigned long long command,
    unsigned int kind, unsigned long long start, unsigned long long end,
    unsigned long long qpc) {
  ADMISSION_GPU_TIMESTAMP *slot;
  if (!ring || !fence || !command || !end ||
      (kind != ADMISSION_GPU_TIMESTAMP_RENDER &&
       kind != ADMISSION_GPU_TIMESTAMP_COMPUTE)) return 0;
  slot = &ring->Slot[fence % ADMISSION_GPU_TIMESTAMP_CAPACITY];
  slot->Fence = fence;
  slot->ProcessId = pid;
  slot->Kind = kind;
  slot->Reserved = 0u;
  slot->CommandVa = command;
  slot->Start = start;
  slot->End = end;
  slot->Qpc = qpc;
  return 1;
}

int AdmissionGpuTimestampFind(const ADMISSION_GPU_TIMESTAMP_RING *ring,
    unsigned int pid, unsigned long long command,
    ADMISSION_GPU_TIMESTAMP *out) {
  const ADMISSION_GPU_TIMESTAMP *best = 0;
  unsigned int i;
  if (!ring || !out) return 0;
  for (i = 0; i < ADMISSION_GPU_TIMESTAMP_CAPACITY; ++i) {
    const ADMISSION_GPU_TIMESTAMP *slot = &ring->Slot[i];
    if (!slot->Fence || !slot->End ||
        (command && (slot->ProcessId != pid || slot->CommandVa != command)))
      continue;
    /* fences are monotonic; the newest submission of the batch wins */
    if (!best || (int)(slot->Fence - best->Fence) > 0) best = slot;
  }
  if (!best) return 0;
  *out = *best;
  if (!command) {
    /* calibration: only the offset's two values */
    out->ProcessId = 0u; out->Kind = 0u; out->CommandVa = 0u; out->Start = 0u;
  }
  return 1;
}
