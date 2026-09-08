#include "render_output_queue.h"

#include <assert.h>

int main(void) {
  ADMISSION_OUTPUT_QUEUE_STATE state;
  AdmissionOutputQueueInitialize(&state);
  assert(AdmissionOutputQueueIsIdle(&state));
  assert(AdmissionOutputQueueSchedule(&state, 1u));
  assert(!AdmissionOutputQueueIsIdle(&state));
  assert(!AdmissionOutputQueueSchedule(&state, 2u));
  assert(AdmissionOutputQueueBegin(&state, 1u));
  assert(!AdmissionOutputQueueIsIdle(&state));
  assert(AdmissionOutputQueueFinish(&state, 1u));
  assert(AdmissionOutputQueueIsIdle(&state));

  assert(AdmissionOutputQueueSchedule(&state, 2u));
  assert(!AdmissionOutputQueueFinish(&state, 1u));
  assert(!AdmissionOutputQueueIsIdle(&state));
  assert(AdmissionOutputQueueBegin(&state, 2u));
  assert(AdmissionOutputQueueFinish(&state, 2u));
  assert(AdmissionOutputQueueIsIdle(&state));
  return 0;
}
