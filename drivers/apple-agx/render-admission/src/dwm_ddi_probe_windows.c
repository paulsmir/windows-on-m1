#include "render_admission.h"

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION) || defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)

_Use_decl_annotations_ VOID AdmissionDwmDdiProbeRecordWindows(
    ADMISSION_CONTEXT *Context, const ADMISSION_DWM_DDI_EVENT *Event) {
  ADMISSION_DWM_DDI_ENTRY *entry;
  LONG sequence;
  if (Context == NULL || Event == NULL ||
      Event->Kind >= AdmissionDwmDdiMpo)
    return;
  entry = &Context->DwmDdiProbe.Entries[Event->Kind];
  InterlockedIncrement(&entry->Count);
  sequence = InterlockedCompareExchange(&entry->Sequence, 0, 0);
  if ((sequence & 1) != 0 ||
      InterlockedCompareExchange(&entry->Sequence, sequence + 1, sequence) !=
          sequence) {
    InterlockedIncrement(&entry->Dropped);
    return;
  }
  entry->Last = *Event;
  KeMemoryBarrier();
  InterlockedExchange(&entry->Sequence, sequence + 2);
}

_Use_decl_annotations_ NTSTATUS AdmissionDwmDdiProbeQueryWindows(
    ADMISSION_CONTEXT *Context, ADMISSION_DWM_DDI_PROBE *Query) {
  ADMISSION_DWM_DDI_PROBE snapshot;
  unsigned int index;
  if (Context == NULL || Query == NULL || !Context->Started ||
      Query->Magic != ADMISSION_DWM_DDI_PROBE_MAGIC ||
      Query->Version != ADMISSION_DWM_DDI_PROBE_VERSION ||
      Query->Bytes != sizeof(*Query))
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(&snapshot, sizeof(snapshot));
  snapshot.Magic = ADMISSION_DWM_DDI_PROBE_MAGIC;
  snapshot.Version = ADMISSION_DWM_DDI_PROBE_VERSION;
  snapshot.Bytes = sizeof(snapshot);
  snapshot.CandidateBuild = APPLE_AGX_VERSION_BUILD;
  snapshot.BootGeneration = Context->Win32BootGeneration;
  for (index = 0; index < AdmissionDwmDdiMpo; ++index) {
    ADMISSION_DWM_DDI_ENTRY *entry = &Context->DwmDdiProbe.Entries[index];
    LONG before = InterlockedCompareExchange(&entry->Sequence, 0, 0);
    snapshot.Entries[index].Count =
        InterlockedCompareExchange(&entry->Count, 0, 0);
    snapshot.Entries[index].Dropped =
        InterlockedCompareExchange(&entry->Dropped, 0, 0);
    if ((before & 1) == 0) {
      snapshot.Entries[index].Last = entry->Last;
      KeMemoryBarrier();
      if (before == InterlockedCompareExchange(&entry->Sequence, 0, 0)) {
        snapshot.Entries[index].Sequence = before;
        continue;
      }
    }
    snapshot.Incomplete = 1u;
  }
  *Query = snapshot;
  return STATUS_SUCCESS;
}

#endif
