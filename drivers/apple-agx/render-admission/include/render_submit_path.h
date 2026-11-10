#ifndef APPLE_AGX_RENDER_SUBMIT_PATH_H
#define APPLE_AGX_RENDER_SUBMIT_PATH_H
/* Wom1SubmitPath1135 (ADMISSION_SUBMIT_PATH, render_admission.h): the
 * values of its v2 fields; no Windows types, so host replays share them.
 * A G4 submission is admissible to the pending queue when none of these
 * holds; NotAdmissible[reason - 1] counts the first that fails, in code
 * order (AdmissionG4PendingAdmissibleWhy). */
#define ADMISSION_SUBMIT_PATH_VERSION 2u
#define ADMISSION_SUBMIT_ADMISSIBLE 0u
#define ADMISSION_SUBMIT_NOT_IRQL 1u           /* not PASSIVE_LEVEL */
#define ADMISSION_SUBMIT_NOT_QUEUEABLE 2u      /* runtime not ready/submitted */
#define ADMISSION_SUBMIT_NOT_CONTEXT_LIMIT 3u  /* two fences of the context */
#define ADMISSION_SUBMIT_NOT_CAPACITY 4u       /* pending queue full */
#define ADMISSION_SUBMIT_NOT_IDLE 5u           /* nothing running or queued */
#define ADMISSION_SUBMIT_NOT_FAULTED 6u        /* scheduler faulted */
#define ADMISSION_SUBMIT_NOT_REASONS 6u
#endif
