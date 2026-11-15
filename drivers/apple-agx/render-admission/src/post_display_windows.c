/* PD-1c (KMD-D2): post-display hand-back at stop
 * (DxgkDdiStopDeviceAndReleasePostDisplayOwnership body): release info from the
 * acquired POST surface and the child table, black fill outside Windows RAM, then
 * the shared stop path (lifecycle.c AdmissionStopDeviceInternal). */
#include "render_admission.h"

/* Clear the returned surface to black before the hand-back, through a
 * write-combined map, only when the range is not Windows RAM (clearing is "if
 * possible" in the release contract; a skipped fill does not fail the release). */
static ULONG AdmissionStopFillBlack(ULONGLONG Pa, ULONGLONG Bytes, ULONG *FillUs) {
  PPHYSICAL_MEMORY_RANGE ranges;
  PHYSICAL_ADDRESS base;
  LARGE_INTEGER frequency, t0, t1;
  PVOID cpu;
  ULONG i;
  *FillUs = 0u;
  if (Pa == 0ULL || Bytes == 0ULL || Pa + Bytes < Pa)
    return ADMISSION_STOP_FILL_NA;
  ranges = MmGetPhysicalMemoryRangesEx2(NULL, 0u);
  if (ranges == NULL)
    return ADMISSION_STOP_FILL_RAM_SKIP; /* unknown: do not write */
  for (i = 0u; ranges[i].BaseAddress.QuadPart != 0 ||
               ranges[i].NumberOfBytes.QuadPart != 0; ++i) {
    ULONGLONG rb = (ULONGLONG)ranges[i].BaseAddress.QuadPart;
    ULONGLONG re = rb + (ULONGLONG)ranges[i].NumberOfBytes.QuadPart;
    if (Pa < re && rb < Pa + Bytes) {
      ExFreePool(ranges);
      return ADMISSION_STOP_FILL_RAM_SKIP;
    }
  }
  ExFreePool(ranges);
  base.QuadPart = (LONGLONG)Pa;
  t0 = KeQueryPerformanceCounter(&frequency);
  cpu = MmMapIoSpaceEx(base, (SIZE_T)Bytes, PAGE_READWRITE | PAGE_WRITECOMBINE);
  if (cpu == NULL)
    return ADMISSION_STOP_FILL_MAP_FAIL;
  RtlZeroMemory(cpu, (SIZE_T)Bytes); /* black in X8R8G8B8 / A8R8G8B8 */
  KeMemoryBarrier();
  MmUnmapIoSpace(cpu, (SIZE_T)Bytes);
  t1 = KeQueryPerformanceCounter(NULL);
  if (frequency.QuadPart > 0)
    *FillUs = (ULONG)((ULONGLONG)(t1.QuadPart - t0.QuadPart) * 1000000ULL /
                      (ULONGLONG)frequency.QuadPart);
  return ADMISSION_STOP_FILL_DONE;
}

/* PD-1c (KMD-D2): Microsoft DxgkDdiStopDeviceAndReleasePostDisplayOwnership:
 * return the display that remains active (any TargetId in maps to the active
 * internal panel: TargetId/AcpiId from the child table, not 0xFFFFFFFF from the
 * acquire), a cleared 32-bit linear surface; NOT_SUPPORTED only when no display
 * is connected. The own-scanout route (no POST surface yet; its _DSD descriptor
 * is a separate Mu step) returns STATUS_UNSUCCESSFUL, so the OS calls StopDevice. */
_Use_decl_annotations_ NTSTATUS AdmissionScanoutReleasePostDisplay(
    ADMISSION_CONTEXT *context, D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    PDXGK_DISPLAY_INFORMATION DisplayInfo) {
  /* The child table reported by AdmissionDdiQueryChildRelations: one internal
   * panel, ChildUid 0, AcpiUid 0, always connected. */
  static const APPLE_AGX_RELEASE_CHILD children[1] = {{0u, 0u, 1u, 1u}};
  APPLE_AGX_RELEASE_INFO acquired, info;
  APPLE_AGX_RELEASE_RESULT result;
  ADMISSION_STOP_EVENT event;
  APPLE_AGX_SCANOUT_U32 mapped = 0u;
  NTSTATUS status;

  if (context == NULL || DisplayInfo == NULL)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(&event, sizeof(event));
  event.Caller = ADMISSION_STOP_CALLER_RELEASE;
  event.TargetIdIn = (ULONG)TargetId;
  event.FillResult = ADMISSION_STOP_FILL_NA;
  RtlZeroMemory(&acquired, sizeof(acquired));
  acquired.Width = context->PostDisplayInformation.Width;
  acquired.Height = context->PostDisplayInformation.Height;
  acquired.Pitch = context->PostDisplayInformation.Pitch;
  acquired.ColorFormat = (APPLE_AGX_SCANOUT_U32)context->PostDisplayInformation.ColorFormat;
  acquired.PhysicalAddress =
      (APPLE_AGX_SCANOUT_U64)context->PostDisplayInformation.PhysicAddress.QuadPart;
  acquired.TargetId = (APPLE_AGX_SCANOUT_U32)context->PostDisplayInformation.TargetId;
  acquired.AcpiId = (APPLE_AGX_SCANOUT_U32)context->PostDisplayInformation.AcpiId;
  event.SurfaceSource = acquired.Width != 0u ? ADMISSION_STOP_SOURCE_POST
                                             : ADMISSION_STOP_SOURCE_NONE;
  result = AppleAgxBuildReleaseInfo(&acquired, children, 1u, (APPLE_AGX_SCANOUT_U32)TargetId,
                                    AdmissionPanelLimits(), &info, &mapped);
  event.MappedTargetId = mapped;
  if (result != AppleAgxReleaseOk) {
    status = result == AppleAgxReleaseNoDisplay ? STATUS_NOT_SUPPORTED
             : result == AppleAgxReleaseNoSurface ? STATUS_UNSUCCESSFUL
                                                  : STATUS_INVALID_PARAMETER;
    event.Status = (ULONG)status;
    AdmissionRecordStopReceipt(context->PhysicalDeviceObject, &event);
    return status;
  }
  event.Info = info;
  /* Black before the broker RELEASE makes the surface visible again. */
  event.FillResult = AdmissionStopFillBlack(
      info.PhysicalAddress, (ULONGLONG)info.Pitch * info.Height, &event.FillUs);
  /* Copy the answer before the stop zeroes PostDisplayInformation. */
  RtlZeroMemory(DisplayInfo, sizeof(*DisplayInfo));
  DisplayInfo->Width = info.Width;
  DisplayInfo->Height = info.Height;
  DisplayInfo->Pitch = info.Pitch;
  DisplayInfo->ColorFormat = (D3DDDIFORMAT)info.ColorFormat;
  DisplayInfo->PhysicAddress.QuadPart = (LONGLONG)info.PhysicalAddress;
  DisplayInfo->TargetId = (D3DDDI_VIDEO_PRESENT_TARGET_ID)info.TargetId;
  DisplayInfo->AcpiId = info.AcpiId;
  return AdmissionStopDeviceInternal(context, &event);
}
