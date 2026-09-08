#include "render_output_queue.h"

#define OUTPUT_NULL ((void *)0)

void AdmissionOutputQueueInitialize(ADMISSION_OUTPUT_QUEUE_STATE *State) {
  if (State == OUTPUT_NULL)
    return;
  State->Generation = 0u;
  State->Scheduled = 0u;
  State->Active = 0u;
}

int AdmissionOutputQueueSchedule(
    ADMISSION_OUTPUT_QUEUE_STATE *State, unsigned int Generation) {
  if (State == OUTPUT_NULL || Generation == 0u || State->Scheduled != 0u ||
      State->Active != 0u)
    return 0;
  State->Generation = Generation;
  State->Scheduled = 1u;
  return 1;
}

int AdmissionOutputQueueBegin(
    ADMISSION_OUTPUT_QUEUE_STATE *State, unsigned int Generation) {
  if (State == OUTPUT_NULL || Generation == 0u ||
      State->Generation != Generation || State->Scheduled != 1u ||
      State->Active != 0u)
    return 0;
  State->Active = 1u;
  return 1;
}

int AdmissionOutputQueueFinish(
    ADMISSION_OUTPUT_QUEUE_STATE *State, unsigned int Generation) {
  if (State == OUTPUT_NULL || Generation == 0u ||
      State->Generation != Generation || State->Scheduled != 1u ||
      State->Active != 1u)
    return 0;
  State->Scheduled = 0u;
  State->Active = 0u;
  return 1;
}

int AdmissionOutputQueueIsIdle(const ADMISSION_OUTPUT_QUEUE_STATE *State) {
  return State != OUTPUT_NULL && State->Scheduled == 0u && State->Active == 0u;
}

#undef OUTPUT_NULL
