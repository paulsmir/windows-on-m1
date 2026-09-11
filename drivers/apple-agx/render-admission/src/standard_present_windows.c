#include "render_admission.h"

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)

_Use_decl_annotations_ NTSTATUS AdmissionStandardPresentTraceQueryWindows(
    ADMISSION_CONTEXT *Context, ADMISSION_STANDARD_PRESENT_TRACE *Query) {
  ADMISSION_STANDARD_PRESENT_TRACE snapshot;
  ULONG count = 0u;
  if (Context == NULL || Query == NULL || !Context->Started ||
      Query->Magic != ADMISSION_STANDARD_PRESENT_TRACE_MAGIC ||
      Query->Version != ADMISSION_STANDARD_PRESENT_TRACE_VERSION ||
      Query->Bytes != sizeof(*Query) ||
      (Query->Command != AdmissionStandardPresentTraceArm &&
       Query->Command != AdmissionStandardPresentTraceRead) ||
      Context->RenderCorrelation.BootGeneration == 0u)
    return STATUS_INVALID_PARAMETER;
  if (Query->Command == AdmissionStandardPresentTraceArm) {
    InterlockedExchange(&Context->StandardPresentTraceArmed, 0);
    InterlockedExchange(&Context->StandardPresentTraceNext, 0);
    InterlockedExchange(&Context->StandardPresentTraceOverflow, 0);
    AdmissionStandardPresentTraceInitialize(
        &Context->StandardPresentTrace, AdmissionStandardPresentTraceRead,
        APPLE_AGX_VERSION_BUILD,
        Context->RenderCorrelation.BootGeneration);
    KeMemoryBarrier();
    InterlockedExchange(&Context->StandardPresentTraceArmed, 1);
  }
  AdmissionStandardPresentTraceInitialize(
      &snapshot, AdmissionStandardPresentTraceRead,
      APPLE_AGX_VERSION_BUILD, Context->RenderCorrelation.BootGeneration);
  while (count < ADMISSION_STANDARD_PRESENT_TRACE_CAPACITY &&
         InterlockedCompareExchange(
             (volatile LONG *)&Context->StandardPresentTrace.Events[count].Valid,
             0, 0) == 1) {
    snapshot.Events[count] = Context->StandardPresentTrace.Events[count];
    ++count;
  }
  snapshot.EventCount = count;
  snapshot.Overflow = (ULONG)InterlockedCompareExchange(
      &Context->StandardPresentTraceOverflow, 0, 0);
  *Query = snapshot;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ VOID AdmissionStandardPresentTraceRecordWindows(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_STANDARD_PRESENT_EVENT *Event) {
  ADMISSION_STANDARD_PRESENT_EVENT record;
  LONG sequence;
  if (Context == NULL || Event == NULL ||
      InterlockedCompareExchange(
          &Context->StandardPresentTraceArmed, 0, 0) == 0)
    return;
  sequence = InterlockedIncrement(&Context->StandardPresentTraceNext);
  if (sequence <= 0 ||
      (ULONG)sequence > ADMISSION_STANDARD_PRESENT_TRACE_CAPACITY) {
    InterlockedExchange(&Context->StandardPresentTraceOverflow, 1);
    return;
  }
  record = *Event;
  record.Valid = 0u;
  record.Sequence = (ULONG)sequence;
  Context->StandardPresentTrace.Events[sequence - 1] = record;
  KeMemoryBarrier();
  InterlockedExchange(
      (volatile LONG *)&Context->StandardPresentTrace.Events[sequence - 1].Valid,
      1);
}

#endif
