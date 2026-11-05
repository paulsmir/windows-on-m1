#ifndef APPLE_AGX_RENDER_OUTPUT_QUEUE_H
#define APPLE_AGX_RENDER_OUTPUT_QUEUE_H

typedef struct _ADMISSION_OUTPUT_QUEUE_STATE {
  unsigned int Generation;
  unsigned int Scheduled;
  unsigned int Active;
  unsigned int ThreadPhase;
} ADMISSION_OUTPUT_QUEUE_STATE;

typedef enum _ADMISSION_OUTPUT_THREAD_PHASE {
  AdmissionOutputThreadStopped = 0u,
  AdmissionOutputThreadRunning,
  AdmissionOutputThreadStopRequested,
  AdmissionOutputThreadExited,
} ADMISSION_OUTPUT_THREAD_PHASE;

void AdmissionOutputQueueInitialize(ADMISSION_OUTPUT_QUEUE_STATE *State);
int AdmissionOutputQueueSchedule(
    ADMISSION_OUTPUT_QUEUE_STATE *State, unsigned int Generation);
int AdmissionOutputQueueBegin(
    ADMISSION_OUTPUT_QUEUE_STATE *State, unsigned int Generation);
int AdmissionOutputQueueFinish(
    ADMISSION_OUTPUT_QUEUE_STATE *State, unsigned int Generation);
int AdmissionOutputQueueIsIdle(const ADMISSION_OUTPUT_QUEUE_STATE *State);
int AdmissionOutputQueueStartThread(ADMISSION_OUTPUT_QUEUE_STATE *State);
int AdmissionOutputQueueRequestStop(ADMISSION_OUTPUT_QUEUE_STATE *State);
int AdmissionOutputQueueCanExit(const ADMISSION_OUTPUT_QUEUE_STATE *State);
int AdmissionOutputQueueMarkExited(ADMISSION_OUTPUT_QUEUE_STATE *State);
int AdmissionOutputQueueThreadRunning(
    const ADMISSION_OUTPUT_QUEUE_STATE *State);
int AdmissionOutputQueueThreadExited(
    const ADMISSION_OUTPUT_QUEUE_STATE *State);

#endif
