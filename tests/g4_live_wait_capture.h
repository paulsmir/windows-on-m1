/* Test-only diagnostic sink; the independent receipt test runs the real writer. */
#ifndef G4_LIVE_WAIT_CAPTURE_H
#define G4_LIVE_WAIT_CAPTURE_H
#include "render_live_paging_wait.h"
static unsigned wait_captures;
static ADMISSION_G3_LIVE_WAIT_ENTRY wait_capture;
static void AdmissionRecordG3LiveWait(ADMISSION_CONTEXT *a, const ADMISSION_G3_LIVE_WAIT_ENTRY *e) {
  (void)a; ++wait_captures; wait_capture=*e;
}

#endif
