#include "render_job_timing.h"

#include <assert.h>
#include <string.h>

int main(void) {
  ADMISSION_JOB_TIMING_STATE state;
  const ADMISSION_JOB_TIMING_SLOT *slot;
  unsigned int fence;
  memset(&state, 0, sizeof(state));
  AdmissionJobTimingInitialize(&state, 24000000ULL);
  for (fence = 1; fence <= 65; ++fence) {
    assert(AdmissionJobTimingBegin(&state, fence, 0x1000u + fence,
        0x2000ULL + fence, 4096u, 1000ULL + fence * 100ULL));
    assert(AdmissionJobTimingMark(&state, fence, AdmissionJobPhaseWorker,
        1010ULL + fence * 100ULL));
  }
  assert(state.Count == 65u);
  assert(AdmissionJobTimingFind(&state, 1u) == 0);
  slot = AdmissionJobTimingFind(&state, 2u);
  assert(slot && slot->ProcessId == 0x1002u && slot->DmaBytes == 4096u);
  assert(slot->Qpc[AdmissionJobPhaseSubmit] == 1200u);
  assert(slot->Qpc[AdmissionJobPhaseWorker] == 1210u);
  assert(slot->Qpc[AdmissionJobPhaseNotify] == 0u);
  assert(!AdmissionJobTimingMark(&state, 1u, AdmissionJobPhaseNotify, 9999u));
  assert(AdmissionJobTimingDelay(&state, 65u));
  assert(AdmissionJobTimingTarget(&state, 65u, 16384000ULL));
  assert(AdmissionJobTimingDelay(&state, 65u));
  assert(AdmissionJobTimingMark(&state, 65u, AdmissionJobPhaseKickTa, 7700u));
  assert(AdmissionJobTimingMark(&state, 65u, AdmissionJobPhaseNotify, 7800u));
  slot = AdmissionJobTimingFind(&state, 65u);
  assert(slot && slot->DelayCount == 2u);
  assert(slot->TargetBytes == 16384000ULL);
  assert(AdmissionJobTimingDeltaUs(&state, slot, AdmissionJobPhaseKickTa,
      AdmissionJobPhaseNotify) == 4u);
  assert(AdmissionJobTimingDeltaUs(&state, slot, AdmissionJobPhaseNotify,
      AdmissionJobPhaseFirstProgress) == 0u);
  assert(!AdmissionJobTimingMark(&state, 65u, AdmissionJobPhaseWorker, 9999u));
  assert(!AdmissionJobTimingMark(&state, 65u, (ADMISSION_JOB_PHASE)-1, 9999u));
  assert(AdmissionJobTimingDeltaUs(&state, slot,
      (ADMISSION_JOB_PHASE)-1, AdmissionJobPhaseNotify) == 0u);
  return 0;
}
