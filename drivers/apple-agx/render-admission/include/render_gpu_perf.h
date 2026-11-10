#ifndef APPLE_AGX_RENDER_GPU_PERF_H
#define APPLE_AGX_RENDER_GPU_PERF_H

/* rc84 receipt (Wom1GpuPerf): the GPU performance state the firmware runs
 * at, sampled from the live HwDataA at every job completion.
 *
 * The firmware's DVFS controller (Asahi initdata.rs hwdata_a: perf_* PID,
 * perf_tgt_utilization 85, base pstate 1, max pstate 6) writes its state
 * back into HwDataA. Offsets are the G13 V13_5 layout (Asahi
 * fw/initdata.rs HwDataA; m1n1 fw/agx/initdata.py AGXHWDataA): actual,
 * target and current state, frequency (IEEE-754 MHz), temperature,
 * average power, the firmware's last update timestamp, current power and
 * utilization (IEEE-754 percent). These are ordinary loads from the
 * KMD's CPU mapping of the HwDataA object: no MMIO.
 *
 * Time attribution: the QPC interval since the previous sample goes to
 * the actual state of this sample. An interval longer than
 * ADMISSION_GPU_PERF_GAP_MS (idle desktop, benchmark loading) is counted
 * as a gap and adds no time, so the histogram is the time share of each
 * state while jobs run back to back. A sample whose actual state is
 * below its target is "limited" (EXP2011's tail: actual 4, target 5). A
 * sample with any state >= ADMISSION_GPU_PERF_PSTATES only goes into the
 * ring. IEEE-754 fields stay as their bit patterns: the KMD has no
 * run-time FP support (EXP1102 _fltused). Plain C types only (KMD header). */

#define ADMISSION_GPU_PERF_VERSION 1u
#define ADMISSION_GPU_PERF_PSTATES 16u
#define ADMISSION_GPU_PERF_RING 32u
#define ADMISSION_GPU_PERF_GAP_MS 50u

#define ADMISSION_GPU_PERF_HWDATAA_PWR_STATUS 0x10u
#define ADMISSION_GPU_PERF_HWDATAA_ACTUAL_PSTATE 0x2cu
#define ADMISSION_GPU_PERF_HWDATAA_TGT_PSTATE 0x30u
#define ADMISSION_GPU_PERF_HWDATAA_CUR_PSTATE 0x38u
#define ADMISSION_GPU_PERF_HWDATAA_FREQ_MHZ 0x58u
#define ADMISSION_GPU_PERF_HWDATAA_TEMP_C 0x1c4u
#define ADMISSION_GPU_PERF_HWDATAA_AVG_POWER_MW 0x1c8u
#define ADMISSION_GPU_PERF_HWDATAA_UPDATE_TS 0x1ccu
#define ADMISSION_GPU_PERF_HWDATAA_CUR_POWER_MW 0x740u
#define ADMISSION_GPU_PERF_HWDATAA_PERF_UTIL 0x7e8u
/* PT1: the pstate controller's set point, u32 percent in both words. */
#define ADMISSION_GPU_PERF_HWDATAA_PERF_TGT 0x788u
#define ADMISSION_GPU_PERF_HWDATAA_PERF_TGT_2 0x7ecu
/* Sample Reserved: perf_tgt low byte | perf_tgt_2 low byte << 8 as read back
 * from the live HwDataA (0 for an object that ends before the second word),
 * and the driver's Wom1PerfTgtUtilization verdict. */
#define ADMISSION_GPU_PERF_TGT_APPLIED 0x00010000u
#define ADMISSION_GPU_PERF_TGT_IGNORED 0x00020000u

typedef struct _ADMISSION_GPU_PERF_SAMPLE {
  unsigned long long Qpc;
  unsigned long long UpdateTs;
  /* actual | target << 8 | current << 16 (low byte of each word) */
  unsigned int Pstates;
  unsigned int FreqMhzBits;
  unsigned int TempC, AvgPowerMw, CurPowerMw;
  unsigned int UtilBits;
  /* Reserved: the PT1 word described at ADMISSION_GPU_PERF_TGT_APPLIED. */
  unsigned int PwrStatus, Reserved;
} ADMISSION_GPU_PERF_SAMPLE;

typedef struct _ADMISSION_GPU_PERF_STATE {
  unsigned int Version, Bytes, Samples, Invalid;
  unsigned long long QpcFrequency, LastQpc;
  /* Sum of ActualTicks; ticks spent below the target state. */
  unsigned long long SampledTicks, LimitedTicks;
  unsigned long long ActualTicks[ADMISSION_GPU_PERF_PSTATES];
  unsigned int ActualCount[ADMISSION_GPU_PERF_PSTATES];
  unsigned int TargetCount[ADMISSION_GPU_PERF_PSTATES];
  unsigned int CurCount[ADMISSION_GPU_PERF_PSTATES];
  unsigned int LimitedCount, Gaps, TempMin, TempMax;
  unsigned int AvgPowerMax, CurPowerMax, FreqMhzBitsMax;
  /* samples whose update_ts differs from the previous sample's */
  unsigned int UpdateChanges;
  /* the newest samples, at Samples % ADMISSION_GPU_PERF_RING */
  ADMISSION_GPU_PERF_SAMPLE Ring[ADMISSION_GPU_PERF_RING];
} ADMISSION_GPU_PERF_STATE;

static inline void AdmissionGpuPerfInitialize(ADMISSION_GPU_PERF_STATE *State,
                                                unsigned long long QpcFrequency) {
  unsigned char *bytes = (unsigned char *)State;
  unsigned int index;
  if (State == 0) return;
  for (index = 0u; index < sizeof(*State); ++index) bytes[index] = 0u;
  State->Version = ADMISSION_GPU_PERF_VERSION;
  State->Bytes = (unsigned int)sizeof(*State);
  State->QpcFrequency = QpcFrequency;
  State->TempMin = 0xffffffffu;
}

static inline unsigned int AdmissionGpuPerfLoad32(
    const volatile unsigned char *Base, unsigned int Offset) {
  return *(const volatile unsigned int *)(Base + Offset);
}

/* 1 when the object holds every field (all offsets 4-byte aligned). */
static inline int AdmissionGpuPerfRead(const volatile void *HwdataA,
                                         unsigned long long Length,
                                         unsigned long long Qpc,
                                         ADMISSION_GPU_PERF_SAMPLE *Out) {
  const volatile unsigned char *base = (const volatile unsigned char *)HwdataA;
  if (base == 0 || Out == 0 ||
      Length < ADMISSION_GPU_PERF_HWDATAA_PERF_UTIL + 4u)
    return 0;
  Out->Qpc = Qpc;
  Out->UpdateTs =
      (unsigned long long)AdmissionGpuPerfLoad32(base, ADMISSION_GPU_PERF_HWDATAA_UPDATE_TS) |
      ((unsigned long long)AdmissionGpuPerfLoad32(
           base, ADMISSION_GPU_PERF_HWDATAA_UPDATE_TS + 4u) << 32);
  Out->Pstates =
      (AdmissionGpuPerfLoad32(base, ADMISSION_GPU_PERF_HWDATAA_ACTUAL_PSTATE) & 0xffu) |
      ((AdmissionGpuPerfLoad32(base, ADMISSION_GPU_PERF_HWDATAA_TGT_PSTATE) & 0xffu) << 8) |
      ((AdmissionGpuPerfLoad32(base, ADMISSION_GPU_PERF_HWDATAA_CUR_PSTATE) & 0xffu) << 16);
  Out->FreqMhzBits = AdmissionGpuPerfLoad32(base, ADMISSION_GPU_PERF_HWDATAA_FREQ_MHZ);
  Out->TempC = AdmissionGpuPerfLoad32(base, ADMISSION_GPU_PERF_HWDATAA_TEMP_C);
  Out->AvgPowerMw = AdmissionGpuPerfLoad32(base, ADMISSION_GPU_PERF_HWDATAA_AVG_POWER_MW);
  Out->CurPowerMw = AdmissionGpuPerfLoad32(base, ADMISSION_GPU_PERF_HWDATAA_CUR_POWER_MW);
  Out->UtilBits = AdmissionGpuPerfLoad32(base, ADMISSION_GPU_PERF_HWDATAA_PERF_UTIL);
  Out->PwrStatus = AdmissionGpuPerfLoad32(base, ADMISSION_GPU_PERF_HWDATAA_PWR_STATUS);
  Out->Reserved = 0u;
  if (Length >= ADMISSION_GPU_PERF_HWDATAA_PERF_TGT_2 + 4u)
    Out->Reserved =
        (AdmissionGpuPerfLoad32(base, ADMISSION_GPU_PERF_HWDATAA_PERF_TGT) & 0xffu) |
        ((AdmissionGpuPerfLoad32(base, ADMISSION_GPU_PERF_HWDATAA_PERF_TGT_2) & 0xffu) << 8);
  return 1;
}

static inline void AdmissionGpuPerfRecord(ADMISSION_GPU_PERF_STATE *State,
                                            const ADMISSION_GPU_PERF_SAMPLE *Sample) {
  unsigned int actual, target, cur;
  unsigned long long previous, limit;
  if (State == 0 || Sample == 0) return;
  previous = State->LastQpc;
  if (State->Samples != 0u &&
      State->Ring[(State->Samples - 1u) % ADMISSION_GPU_PERF_RING].UpdateTs !=
          Sample->UpdateTs)
    ++State->UpdateChanges;
  State->Ring[State->Samples % ADMISSION_GPU_PERF_RING] = *Sample;
  ++State->Samples;
  State->LastQpc = Sample->Qpc;
  actual = Sample->Pstates & 0xffu;
  target = (Sample->Pstates >> 8) & 0xffu;
  cur = (Sample->Pstates >> 16) & 0xffu;
  if (actual >= ADMISSION_GPU_PERF_PSTATES || target >= ADMISSION_GPU_PERF_PSTATES ||
      cur >= ADMISSION_GPU_PERF_PSTATES) {
    ++State->Invalid;
    return;
  }
  ++State->ActualCount[actual];
  ++State->TargetCount[target];
  ++State->CurCount[cur];
  if (actual < target) ++State->LimitedCount;
  if (Sample->TempC < State->TempMin) State->TempMin = Sample->TempC;
  if (Sample->TempC > State->TempMax) State->TempMax = Sample->TempC;
  if (Sample->AvgPowerMw > State->AvgPowerMax) State->AvgPowerMax = Sample->AvgPowerMw;
  if (Sample->CurPowerMw > State->CurPowerMax) State->CurPowerMax = Sample->CurPowerMw;
  /* non-negative IEEE-754 values order like their bit patterns */
  if ((Sample->FreqMhzBits & 0x80000000u) == 0u &&
      Sample->FreqMhzBits > State->FreqMhzBitsMax)
    State->FreqMhzBitsMax = Sample->FreqMhzBits;
  if (previous == 0ull || Sample->Qpc <= previous) return;
  limit = State->QpcFrequency * ADMISSION_GPU_PERF_GAP_MS / 1000u;
  if (Sample->Qpc - previous > limit) {
    ++State->Gaps;
    return;
  }
  State->ActualTicks[actual] += Sample->Qpc - previous;
  State->SampledTicks += Sample->Qpc - previous;
  if (actual < target) State->LimitedTicks += Sample->Qpc - previous;
}

#endif /* APPLE_AGX_RENDER_GPU_PERF_H */
