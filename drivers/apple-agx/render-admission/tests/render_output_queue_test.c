#include "render_output_queue.h"

#include <assert.h>

int main(void) {
  ADMISSION_OUTPUT_QUEUE_STATE state;
  AdmissionOutputQueueInitialize(&state);
  assert(AdmissionOutputQueueIsIdle(&state));
  /* A queue without a live driver thread must reject work.  This catches a
     regression back to scheduling long verification on an unrelated worker. */
  assert(!AdmissionOutputQueueSchedule(&state, 1u));
  assert(AdmissionOutputQueueStartThread(&state));
  assert(AdmissionOutputQueueThreadRunning(&state));
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
  /* Stop drains the current generation but rejects any later producer. */
  assert(AdmissionOutputQueueRequestStop(&state));
  assert(!AdmissionOutputQueueSchedule(&state, 3u));
  assert(!AdmissionOutputQueueCanExit(&state));
  assert(AdmissionOutputQueueFinish(&state, 2u));
  assert(AdmissionOutputQueueIsIdle(&state));
  assert(AdmissionOutputQueueCanExit(&state));
  assert(AdmissionOutputQueueMarkExited(&state));
  assert(AdmissionOutputQueueThreadExited(&state));
  assert(!AdmissionOutputQueueThreadRunning(&state));
  assert(!AdmissionOutputQueueSchedule(&state, 3u));
  return 0;
}
