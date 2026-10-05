#include <assert.h>
#include <string.h>

int main(void) {
  ADMISSION_CONTEXT context;
  ADMISSION_STANDARD_PRESENT_TRACE query;
  ADMISSION_STANDARD_PRESENT_EVENT event;
  unsigned int index;

  memset(&context, 0, sizeof(context));
  context.Started = 1;
  context.RenderCorrelation.BootGeneration = 0x12345678u;
  AdmissionStandardPresentTraceInitialize(
      &query, AdmissionStandardPresentTraceArm, 0u, 0u);
  assert(AdmissionStandardPresentTraceQueryWindows(&context, &query) ==
         STATUS_SUCCESS);
  assert(query.CandidateBuild == 641u);
  assert(query.BootGeneration == 0x12345678u);
  assert(query.EventCount == 0u && query.Overflow == 0u);

  memset(&event, 0, sizeof(event));
  event.Kind = AdmissionStandardPresentEventPresent;
  event.Phase = AdmissionStandardPresentPhaseEntry;
  event.ContextToken = 0x1111ULL;
  event.Flags = 0x4u;
  AdmissionStandardPresentTraceRecordWindows(&context, &event);
  event.Phase = AdmissionStandardPresentPhaseExit;
  event.AllocationToken = 0x2222ULL;
  AdmissionStandardPresentTraceRecordWindows(&context, &event);

  AdmissionStandardPresentTraceInitialize(
      &query, AdmissionStandardPresentTraceRead, 0u, 0u);
  assert(AdmissionStandardPresentTraceQueryWindows(&context, &query) ==
         STATUS_SUCCESS);
  assert(query.EventCount == 2u && query.Overflow == 0u);
  assert(query.Events[0].Valid == 1u && query.Events[0].Sequence == 1u);
  assert(query.Events[1].Valid == 1u && query.Events[1].Sequence == 2u);
  assert(query.Events[1].AllocationToken == 0x2222ULL);

  AdmissionStandardPresentTraceInitialize(
      &query, AdmissionStandardPresentTraceArm, 0u, 0u);
  assert(AdmissionStandardPresentTraceQueryWindows(&context, &query) ==
         STATUS_SUCCESS);
  for (index = 0u; index <= ADMISSION_STANDARD_PRESENT_TRACE_CAPACITY;
       ++index)
    AdmissionStandardPresentTraceRecordWindows(&context, &event);
  AdmissionStandardPresentTraceInitialize(
      &query, AdmissionStandardPresentTraceRead, 0u, 0u);
  assert(AdmissionStandardPresentTraceQueryWindows(&context, &query) ==
         STATUS_SUCCESS);
  assert(query.EventCount == ADMISSION_STANDARD_PRESENT_TRACE_CAPACITY);
  assert(query.Overflow == 1u);

  query.Version++;
  assert(AdmissionStandardPresentTraceQueryWindows(&context, &query) ==
         STATUS_INVALID_PARAMETER);
  return 0;
}
