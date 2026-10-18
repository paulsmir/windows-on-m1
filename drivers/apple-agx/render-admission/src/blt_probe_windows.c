#include "render_admission.h"

#if defined(APPLE_AGX_BLT_PROBE_QUALIFICATION)

_Use_decl_annotations_ NTSTATUS AdmissionBltProbeQueryWindows(
    ADMISSION_CONTEXT *Context, ADMISSION_BLT_PROBE *Query) {
  ULONG count = 0u;
  if (Context == NULL || Query == NULL || !Context->Started ||
      Query->Magic != ADMISSION_BLT_PROBE_MAGIC ||
      Query->Version != ADMISSION_BLT_PROBE_VERSION ||
      Query->Bytes != sizeof(*Query))
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(Query, sizeof(*Query));
  Query->Magic = ADMISSION_BLT_PROBE_MAGIC;
  Query->Version = ADMISSION_BLT_PROBE_VERSION;
  Query->Bytes = sizeof(*Query);
  Query->CandidateBuild = APPLE_AGX_VERSION_BUILD;
  Query->BootGeneration = Context->Win32BootGeneration;
  Query->AdapterToken = (unsigned long long)(ULONG_PTR)Context;
  Query->PresentCalls = (ULONG)InterlockedCompareExchange(
      (volatile LONG *)&Context->BltProbe.PresentCalls, 0, 0);
  Query->PresentBltCalls = (ULONG)InterlockedCompareExchange(
      (volatile LONG *)&Context->BltProbe.PresentBltCalls, 0, 0);
  Query->VirtualSubmitCalls = (ULONG)InterlockedCompareExchange(
      (volatile LONG *)&Context->BltProbe.VirtualSubmitCalls, 0, 0);
  Query->PhysicalPresentSubmits = (ULONG)InterlockedCompareExchange(
      (volatile LONG *)&Context->BltProbe.PhysicalPresentSubmits, 0, 0);
  Query->CpuBltExecutions = (ULONG)InterlockedCompareExchange(
      (volatile LONG *)&Context->BltProbe.CpuBltExecutions, 0, 0);
  while (count < ADMISSION_BLT_PROBE_CAPACITY &&
         InterlockedCompareExchange(
             (volatile LONG *)&Context->BltProbe.Events[count].Valid,
             0, 0) == 1) {
    Query->Events[count] = Context->BltProbe.Events[count];
    ++count;
  }
  Query->EventCount = count;
  Query->Overflow = Query->CpuBltExecutions > ADMISSION_BLT_PROBE_CAPACITY;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ VOID AdmissionBltProbeRecordWindows(
    ADMISSION_CONTEXT *Context, const ADMISSION_BLT_EXECUTION *Event) {
  ADMISSION_BLT_EXECUTION record;
  LONG sequence;
  if (Context == NULL || Event == NULL)
    return;
  sequence = InterlockedIncrement(
      (volatile LONG *)&Context->BltProbe.CpuBltExecutions);
  if (sequence <= 0 || (ULONG)sequence > ADMISSION_BLT_PROBE_CAPACITY)
    return;
  record = *Event;
  record.Valid = 0u;
  record.Sequence = (ULONG)sequence;
  Context->BltProbe.Events[sequence - 1] = record;
  KeMemoryBarrier();
  InterlockedExchange(
      (volatile LONG *)&Context->BltProbe.Events[sequence - 1].Valid, 1);
}

#endif
