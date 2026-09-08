#ifndef APPLE_AGX_RENDER_OUTPUT_QUEUE_H
#define APPLE_AGX_RENDER_OUTPUT_QUEUE_H

typedef struct _ADMISSION_OUTPUT_QUEUE_STATE {
  unsigned int Generation;
  unsigned int Scheduled;
  unsigned int Active;
} ADMISSION_OUTPUT_QUEUE_STATE;

void AdmissionOutputQueueInitialize(ADMISSION_OUTPUT_QUEUE_STATE *State);
int AdmissionOutputQueueSchedule(
    ADMISSION_OUTPUT_QUEUE_STATE *State, unsigned int Generation);
int AdmissionOutputQueueBegin(
    ADMISSION_OUTPUT_QUEUE_STATE *State, unsigned int Generation);
int AdmissionOutputQueueFinish(
    ADMISSION_OUTPUT_QUEUE_STATE *State, unsigned int Generation);
int AdmissionOutputQueueIsIdle(const ADMISSION_OUTPUT_QUEUE_STATE *State);

#endif
