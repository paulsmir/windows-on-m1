#include "render_call_correlation.h"

#include <assert.h>
#include <string.h>

int main(void) {
  ADMISSION_RENDER_CORRELATION_STATE state;
  unsigned long long allocations[2] = {0x11110000ULL, 0x22220000ULL};
  unsigned int first = 0u, second = 0u, third = 0u;
  assert(sizeof(ADMISSION_RENDER_CORRELATION_SLOT) == 176u);
  assert(sizeof(ADMISSION_RENDER_CORRELATION_STATE) == 400u);
  memset(&state, 0, sizeof(state));
  assert(AdmissionRenderCorrelationInitialize(&state, 612u, 9u));
  assert(AdmissionRenderCorrelationBegin(&state, 100u, 0xa000u, 0xc001u,
      48u, &first));
  assert(first == 1u && state.Count == 1u);
  assert(state.Slot[0].ValidMask == ADMISSION_RENDER_CAPTURE_ENTRY);
  assert(AdmissionRenderCorrelationInitialize(&state, 612u, 9u));
  assert(state.Count == 1u); /* re-arm does not erase the first call */
  assert(AdmissionRenderCorrelationValidated(
      &state, first, 0xabcdu, 2u, 0u, 2u, allocations));
  assert(AdmissionRenderCorrelationExit(&state, first, 120u, 0u, 0u,
      168u, 1u, 1u));
  assert(state.Slot[0].ValidMask & ADMISSION_RENDER_CAPTURE_EXIT);
  assert(state.Slot[0].DmaBytesProduced == 168u);
  assert(AdmissionRenderCorrelationPatch(&state, 0xc001u, 1u, 0u, 0u));
  assert(AdmissionRenderCorrelationPatch(&state, 0xc001u, 0u, 0u, 0u));
  assert(AdmissionRenderCorrelationSubmit(&state, 0xc001u, 1u, 256u, 0u, 0u));
  assert(AdmissionRenderCorrelationSubmit(&state, 0xc001u, 0u, 256u, 0u, 0u));
  assert(AdmissionRenderCorrelationWorker(&state, 256u, 1u, 0u));
  assert(AdmissionRenderCorrelationWorker(&state, 256u, 0u, 0u));

  assert(AdmissionRenderCorrelationBegin(&state, 200u, 0xa000u, 0xc002u,
      48u, &second));
  assert(second == 2u && state.Count == 2u);
  assert(state.Slot[1].ValidMask == ADMISSION_RENDER_CAPTURE_ENTRY);
  assert(AdmissionRenderCorrelationExit(&state, second, 201u, 6u,
      0xc000000du, 0u, 0u, 0u)); /* error before command validation */
  assert(!(state.Slot[1].ValidMask & ADMISSION_RENDER_CAPTURE_VALIDATED));
  assert(AdmissionRenderCorrelationSubmit(
      &state, 0xc002u, 1u, 257u, 0xffffffffu, 0x00000103u));
  assert(AdmissionRenderCorrelationSubmit(
      &state, 0xc002u, 0u, 257u, 22u, 0x80000011u));
  assert(state.Slot[1].ValidMask & ADMISSION_RENDER_CAPTURE_SUBMIT_ENTRY);
  assert(state.Slot[1].ValidMask & ADMISSION_RENDER_CAPTURE_SUBMIT_EXIT);
  assert(state.Slot[1].Fence == 257u);
  assert(state.Slot[1].SubmitGuard == 22u);
  assert(state.Slot[1].SubmitStatus == 0x80000011u);
  assert(!AdmissionRenderCorrelationBegin(&state, 300u, 0xa000u, 0xc003u,
      48u, &third));
  assert(state.Overflow == 1u);

  assert(AdmissionRenderCorrelationMarkExport(
      &state, state.CapturedGeneration, 0xc0000001u, 0u));
  assert(state.ExportAttempted == 1u && state.Durable == 0u);
  assert(AdmissionRenderCorrelationMarkExport(
      &state, state.CapturedGeneration, 0u, 1u));
  assert(state.ExportedGeneration == state.CapturedGeneration);
  assert(state.Durable == 1u);

  memset(&state, 0, sizeof(state));
  assert(AdmissionRenderCorrelationInitialize(&state, 612u, 10u));
  assert(AdmissionRenderCorrelationBegin(&state, 400u, 0xa001u, 0xc004u,
      48u, &first));
  assert(state.Slot[0].ValidMask == ADMISSION_RENDER_CAPTURE_ENTRY);
  assert(state.Slot[0].ExitTimestamp == 0ULL); /* ENTRY without EXIT */
  assert(AdmissionRenderCorrelationInitialize(&state, 612u, 10u));
  assert(state.Count == 1u); /* slow consumer cannot erase it */
  assert(AdmissionRenderCorrelationInitialize(&state, 612u, 11u));
  assert(state.Count == 0u); /* a new boot is a distinct record set */
  return 0;
}
