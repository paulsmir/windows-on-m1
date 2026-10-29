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

unsigned int AdmissionJobTimingCpuValue(unsigned int index,
    unsigned int efficiency) {
  if (index > 0xffu || efficiency > 0xffu) return 0u;
  return index | efficiency << 8 | 0x80000000u;
}

int AdmissionJobTimingCpu(ADMISSION_JOB_TIMING_STATE *state,
    unsigned int fence, int complete, unsigned int value) {
  ADMISSION_JOB_TIMING_SLOT *slot =
      (ADMISSION_JOB_TIMING_SLOT *)AdmissionJobTimingFind(state, fence);
  unsigned int *field;
  if (!slot || !value) return 0;
  field = complete ? &slot->CpuComplete : &slot->CpuBuild;
  if (*field) return 0;
  *field = value;
  return 1;
}

int AdmissionJobTimingFlag(ADMISSION_JOB_TIMING_STATE *state,
    unsigned int fence, unsigned int flag) {
  ADMISSION_JOB_TIMING_SLOT *slot =
      (ADMISSION_JOB_TIMING_SLOT *)AdmissionJobTimingFind(state, fence);
  if (!slot || !flag) return 0;
  slot->Flags |= flag;
  return 1;
}

int AdmissionJobTimingAheadRefusal(ADMISSION_JOB_TIMING_STATE *state,
    unsigned int fence, unsigned int shift, unsigned int refusal) {
  ADMISSION_JOB_TIMING_SLOT *slot =
      (ADMISSION_JOB_TIMING_SLOT *)AdmissionJobTimingFind(state, fence);
  if (!slot || shift > 24u) return 0;
  if (refusal > 0xffu) refusal = 0xffu;
  slot->AheadRefusals = (slot->AheadRefusals & ~(0xffu << shift)) |
      (refusal << shift);
  return 1;
}

int AdmissionJobTimingSub(ADMISSION_JOB_TIMING_STATE *state,
    unsigned int fence, ADMISSION_JOB_SUB sub, unsigned long long qpc) {
  ADMISSION_JOB_TIMING_SLOT *slot =
      (ADMISSION_JOB_TIMING_SLOT *)AdmissionJobTimingFind(state, fence);
  if (!slot || (int)sub < 0 || sub >= ADMISSION_JOB_SUB_COUNT || !qpc ||
      slot->Sub[sub]) return 0;
  slot->Sub[sub] = qpc;
  return 1;
}

int AdmissionJobTimingDense(const ADMISSION_JOB_TIMING_STATE *state) {
  unsigned long long oldest = ~0ull, newest = 0ull, span;
  unsigned int i;
  if (!state || !state->QpcFrequency ||
      state->Count < ADMISSION_JOB_TIMING_CAPACITY) return 0;
  for (i = 0; i < ADMISSION_JOB_TIMING_CAPACITY; ++i) {
    unsigned long long submit = state->Slot[i].Qpc[AdmissionJobPhaseSubmit];
    if (!state->Slot[i].Fence || !submit) return 0;
    if (submit < oldest) oldest = submit;
    if (submit > newest) newest = submit;
  }
  span = state->QpcFrequency / 1000ull * ADMISSION_JOB_TIMING_HOT_SPAN_MS;
  return newest - oldest <= span ? 1 : 0;
}

int AdmissionJobWorkerAffinity(const ADMISSION_JOB_WORKER_CORE *cores,
    unsigned int count, unsigned short *group, unsigned long long *mask) {
  unsigned int i, first = count;
  unsigned int lowest = 0x100u, highest = 0u;
  unsigned long long selected = 0ull;
  if (!cores || !count || !group || !mask) return 0;
  for (i = 0; i < count; ++i) {
    unsigned int efficiency = cores[i].EfficiencyClass;
    if (!cores[i].Mask) continue;
    if (efficiency < lowest) lowest = efficiency;
    if (efficiency > highest) highest = efficiency;
  }
  if (lowest == 0x100u || lowest == highest) return 0;
  for (i = 0; i < count; ++i)
    if (cores[i].Mask && cores[i].EfficiencyClass == highest) {
      if (first == count) first = i;
      if (cores[i].Group == cores[first].Group) selected |= cores[i].Mask;
    }
  if (first == count || !selected) return 0;
  *group = cores[first].Group;
  *mask = selected;
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

int AdmissionGpuClockCalibrate(const ADMISSION_GPU_TIMESTAMP_RING *ring,
    unsigned long long frequency, unsigned long long qpc,
    unsigned long long *gpu_frequency, unsigned long long *gpu_clock_ns) {
  ADMISSION_GPU_TIMESTAMP newest;
  unsigned long long ticks = qpc;
  if (!ring || !frequency || !gpu_frequency || !gpu_clock_ns) return 0;
  if (AdmissionGpuTimestampFind(ring, 0u, 0u, &newest)) {
    /* firmware minus counter at the newest completion; either sign */
    if (newest.End >= newest.Qpc)
      ticks = qpc + (newest.End - newest.Qpc);
    else
      ticks = qpc > newest.Qpc - newest.End ? qpc - (newest.Qpc - newest.End) : 0u;
  }
  *gpu_frequency = 1000000000ull;
  *gpu_clock_ns = (ticks / frequency) * 1000000000ull +
                  ((ticks % frequency) * 1000000000ull) / frequency;
  return 1;
}

int AdmissionGpuCompletionRecord(ADMISSION_GPU_COMPLETION_RING *ring,
    const ADMISSION_GPU_COMPLETION *record) {
  ADMISSION_GPU_COMPLETION *out;
  if (!ring || !record || !record->Fence || !record->CommandVa) return 0;
  /* A notification can be retried while this same pending job is retained. */
  if (ring->Sequence) {
    const ADMISSION_GPU_COMPLETION *last =
        &ring->Record[(ring->Sequence - 1u) % ADMISSION_GPU_COMPLETION_CAPACITY];
    if (last->Fence == record->Fence && last->ProcessId == record->ProcessId &&
        last->CommandVa == record->CommandVa && last->SubmitQpc == record->SubmitQpc)
      return 0;
  }
  out = &ring->Record[ring->Sequence % ADMISSION_GPU_COMPLETION_CAPACITY];
  *out = *record;
  out->Sequence = ++ring->Sequence;
  return 1;
}

void AdmissionGpuCompletionRead(const ADMISSION_GPU_COMPLETION_RING *ring,
    unsigned int pid, ADMISSION_GPU_COMPLETION_QUERY *query) {
  unsigned long long cursor, oldest;
  if (!ring || !query) return;
  cursor = query->Cursor;
  timing_zero(query->Record, sizeof(query->Record));
  query->Count = query->Reset = 0u;
  query->Reserved = 0u;
  query->Lost = 0u;
  query->Newest = ring->Sequence;
  query->MissingTiming = ring->MissingTiming;
  if (cursor > ring->Sequence) { cursor = 0u; query->Reset = 1u; }
  oldest = ring->Sequence > ADMISSION_GPU_COMPLETION_CAPACITY ?
      ring->Sequence - ADMISSION_GPU_COMPLETION_CAPACITY : 0u;
  if (cursor < oldest) { query->Lost = oldest - cursor; cursor = oldest; }
  while (cursor < ring->Sequence && query->Count < ADMISSION_GPU_COMPLETION_PAGE) {
    const ADMISSION_GPU_COMPLETION *record =
        &ring->Record[cursor % ADMISSION_GPU_COMPLETION_CAPACITY];
    ++cursor;
    if (record->ProcessId == pid) query->Record[query->Count++] = *record;
  }
  query->Cursor = cursor;
}

/* rc100: predicted-sleep completion poll (render_job_timing.h). */
void AdmissionJobPollInitialize(ADMISSION_JOB_POLL_STATE *state,
    unsigned int spinWindowUs, unsigned int flags) {
  unsigned int i, slack;
  if (!state) return;
  timing_zero(state, sizeof(*state));
  state->Version = ADMISSION_JOB_POLL_VERSION;
  state->Bytes = sizeof(*state);
  state->SpinWindowUs = spinWindowUs;
  state->Flags = flags;
  slack = flags & ADMISSION_JOB_POLL_FLAG_HIGHRES ?
      ADMISSION_JOB_POLL_HIGHRES_SLACK_US : ADMISSION_JOB_POLL_TICK_SLACK_US;
  for (i = 0; i < ADMISSION_JOB_POLL_LATENESS; ++i) state->Lateness[i] = slack;
}

static unsigned long long poll_mix(unsigned long long h, unsigned long long v) {
  h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  return h;
}

unsigned long long AdmissionJobPollBegin(ADMISSION_JOB_POLL_STATE *state,
    unsigned long long context, unsigned int kind, unsigned int dmaBytes) {
  ADMISSION_JOB_POLL_CONTEXT *slot = 0;
  unsigned int i, signature, oldest = 0;
  unsigned long long key;
  if (!state) return 0ull;
  ++state->Clock;
  signature = (kind & 0xffu) << 24 | (dmaBytes & 0xffffffu);
  for (i = 0; i < ADMISSION_JOB_POLL_CONTEXTS; ++i) {
    if (state->Context[i].Used && state->Context[i].Context == context) {
      slot = &state->Context[i];
      break;
    }
    if (state->Context[i].Used < state->Context[oldest].Used) oldest = i;
  }
  if (!slot) {
    slot = &state->Context[oldest];
    slot->Context = context;
    slot->Previous[0] = slot->Previous[1] = 0xffffffffu;
  }
  slot->Used = state->Clock;
  key = poll_mix(poll_mix(poll_mix(poll_mix(0x5250u, context), signature),
                          slot->Previous[0]), slot->Previous[1]);
  if (!key) key = 1ull;
  slot->Previous[1] = slot->Previous[0];
  slot->Previous[0] = signature;
  return key;
}

static const ADMISSION_JOB_POLL_ENTRY *poll_find(
    const ADMISSION_JOB_POLL_STATE *state, unsigned long long key) {
  unsigned int i;
  for (i = 0; i < ADMISSION_JOB_POLL_KEYS; ++i)
    if (state->Entry[i].Used && state->Entry[i].Key == key)
      return &state->Entry[i];
  return 0;
}

unsigned int AdmissionJobPollPredict(const ADMISSION_JOB_POLL_STATE *state,
    unsigned long long key) {
  const ADMISSION_JOB_POLL_ENTRY *entry;
  unsigned int i, n, shortest = 0xffffffffu;
  if (!state || !key || !(entry = poll_find(state, key)) || entry->Count < 2u)
    return 0u;
  n = entry->Count < ADMISSION_JOB_POLL_HISTORY ? entry->Count :
      ADMISSION_JOB_POLL_HISTORY;
  for (i = 0; i < n; ++i)
    if (entry->Us[i] < shortest) shortest = entry->Us[i];
  return shortest;
}

static void poll_learn(ADMISSION_JOB_POLL_STATE *state,
    unsigned long long key, unsigned long long durationUs) {
  ADMISSION_JOB_POLL_ENTRY *entry;
  unsigned int i, oldest = 0;
  entry = (ADMISSION_JOB_POLL_ENTRY *)poll_find(state, key);
  if (!entry) {
    for (i = 1; i < ADMISSION_JOB_POLL_KEYS; ++i)
      if (state->Entry[i].Used < state->Entry[oldest].Used) oldest = i;
    entry = &state->Entry[oldest];
    timing_zero(entry, sizeof(*entry));
    entry->Key = key;
  }
  entry->Used = state->Clock ? state->Clock : 1u;
  if (durationUs > 0xffffffffull) durationUs = 0xffffffffull;
  entry->Us[entry->Next] = (unsigned int)durationUs;
  entry->Next = (entry->Next + 1u) % ADMISSION_JOB_POLL_HISTORY;
  if (entry->Count != 0xffffffffu) ++entry->Count;
}

void AdmissionJobPollFinish(ADMISSION_JOB_POLL_STATE *state,
    unsigned long long key, unsigned int predictedUs,
    unsigned long long detectUs, unsigned long long lastPollUs,
    int afterSleep) {
  if (!state) return;
  ++state->Stats.Jobs;
  state->Stats.JobUs += detectUs;
  if (predictedUs) ++state->Stats.PredictedJobs;
  if (lastPollUs > detectUs) lastPollUs = detectUs;
  if (afterSleep) {
    ++state->Stats.Misses;
    state->Stats.MissWindowUs += detectUs - lastPollUs;
  }
  if (key) poll_learn(state, key, afterSleep ? lastPollUs : detectUs);
}

unsigned int AdmissionJobPollSlackUs(const ADMISSION_JOB_POLL_STATE *state) {
  unsigned int i, slack = 0u;
  if (!state) return ADMISSION_JOB_POLL_TICK_SLACK_US;
  for (i = 0; i < ADMISSION_JOB_POLL_LATENESS; ++i)
    if (state->Lateness[i] > slack) slack = state->Lateness[i];
  return slack;
}

void AdmissionJobPollNoteSleep(ADMISSION_JOB_POLL_STATE *state,
    unsigned long long requestedUs, unsigned long long actualUs) {
  unsigned long long late;
  if (!state) return;
  late = actualUs > requestedUs ? actualUs - requestedUs : 0ull;
  ++state->Stats.Sleeps;
  state->Stats.SleepRequestedUs += requestedUs;
  state->Stats.SleptUs += actualUs;
  state->Stats.LateUs += late;
  if (late > state->Stats.LateMaxUs) state->Stats.LateMaxUs = late;
  if (late > ADMISSION_JOB_POLL_LATENESS_MAX_US)
    late = ADMISSION_JOB_POLL_LATENESS_MAX_US;
  state->Lateness[state->LatenessNext] = (unsigned int)late;
  state->LatenessNext = (state->LatenessNext + 1u) % ADMISSION_JOB_POLL_LATENESS;
}

static unsigned int poll_count(ADMISSION_JOB_POLL_STATE *state,
    unsigned int action) {
  if (state && action == ADMISSION_JOB_POLL_SPIN) ++state->Stats.Spins;
  if (state && action == ADMISSION_JOB_POLL_TICK) ++state->Stats.Ticks;
  return action;
}

unsigned int AdmissionJobPollPlan(ADMISSION_JOB_POLL_STATE *state,
    unsigned long long elapsedUs, unsigned int predictedUs,
    unsigned int sleepCapUs, unsigned int *us) {
  unsigned long long guard, wake, slack;
  if (!us) return ADMISSION_JOB_POLL_TICK;
  *us = 0u;
  if (!state || state->SpinWindowUs != ADMISSION_JOB_POLL_PREDICTIVE) {
    unsigned int window = state ? state->SpinWindowUs :
        ADMISSION_JOB_POLL_SPIN_WINDOW_US;
    if (elapsedUs >= window) return poll_count(state, ADMISSION_JOB_POLL_TICK);
    *us = ADMISSION_JOB_POLL_SPIN_STEP_US;
    return poll_count(state, ADMISSION_JOB_POLL_SPIN);
  }
  if (elapsedUs >= ADMISSION_JOB_POLL_SPIN_WINDOW_US)
    return poll_count(state, ADMISSION_JOB_POLL_TICK);
  if (!predictedUs) {
    *us = ADMISSION_JOB_POLL_SPIN_STEP_US;
    return poll_count(state, ADMISSION_JOB_POLL_SPIN);
  }
  if (!sleepCapUs || sleepCapUs > ADMISSION_JOB_POLL_SLEEP_CHUNK_US)
    sleepCapUs = ADMISSION_JOB_POLL_SLEEP_CHUNK_US;
  guard = predictedUs / 10u;
  if (guard < ADMISSION_JOB_POLL_GUARD_MIN_US)
    guard = ADMISSION_JOB_POLL_GUARD_MIN_US;
  if (guard > ADMISSION_JOB_POLL_GUARD_MAX_US)
    guard = ADMISSION_JOB_POLL_GUARD_MAX_US;
  slack = AdmissionJobPollSlackUs(state);
  wake = predictedUs > guard ? predictedUs - guard : 0ull;
  if (elapsedUs + slack + ADMISSION_JOB_POLL_MIN_SLEEP_US <= wake) {
    unsigned long long sleep = wake - elapsedUs - slack;
    *us = sleep < sleepCapUs ? (unsigned int)sleep : sleepCapUs;
    return ADMISSION_JOB_POLL_SLEEP;
  }
  /* A tick-rounded sleep (no high-resolution timer) costs up to a tick of
   * latency: an overrunning job is spun instead. */
  /* rc116 (EXP2061a, D3D11 HD 10k: 297k jobs of 389 us average; 62k
   * overrun sleeps of 100 us each woke 428 us late, a 538 us average miss,
   * 33 s of a 180 s run; the rc72 spin gave 2462 against 2018): an overrun
   * sleeps only while the timer's learned lateness is within the high-
   * resolution slack, else it is spun. */
  if (elapsedUs < (unsigned long long)predictedUs + guard ||
      !(state->Flags & ADMISSION_JOB_POLL_FLAG_HIGHRES) ||
      slack > ADMISSION_JOB_POLL_HIGHRES_SLACK_US) {
    *us = ADMISSION_JOB_POLL_SPIN_STEP_US;
    return poll_count(state, ADMISSION_JOB_POLL_SPIN);
  }
  *us = ADMISSION_JOB_POLL_OVERRUN_SLEEP_US < sleepCapUs ?
      ADMISSION_JOB_POLL_OVERRUN_SLEEP_US : sleepCapUs;
  return ADMISSION_JOB_POLL_SLEEP;
}
