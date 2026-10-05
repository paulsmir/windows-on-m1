#include "render_admission.h"

_Use_decl_annotations_ NTSTATUS AdmissionBackendImageStart(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_BACKEND_MEMORY_VIEW memory_view;
  ADMISSION_LOCAL_MEMORY_VIEW image_view;
  NTSTATUS status;

  if (Context == NULL ||
      Context->BackendImage.Ready == APPLE_AGX_TRUE)
    return STATUS_INVALID_DEVICE_STATE;
  RtlZeroMemory(&memory_view, sizeof(memory_view));
  status = AdmissionMemoryRuntimeBackendView(Context, &memory_view);
  if (!NT_SUCCESS(status))
    return status;
  image_view.CpuAddress = memory_view.CpuAddress;
  image_view.HostPhysicalAddress = memory_view.HostPhysicalAddress;
  image_view.GpuVirtualAddress = memory_view.GpuVirtualAddress;
  image_view.Bytes = memory_view.Bytes;
  if (!AdmissionBackendImagePrepare(
          &Context->BackendImage, &image_view))
    return STATUS_INVALID_IMAGE_FORMAT;
  KeMemoryBarrier();
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionBackendImageStop(
    ADMISSION_CONTEXT *Context) {
  if (Context == NULL)
    return STATUS_INVALID_PARAMETER;
  if (AdmissionRenderPacketState(&Context->RenderPacket) !=
      AdmissionRenderPacketEmpty)
    return STATUS_DEVICE_BUSY;
  AdmissionBackendImageReset(&Context->BackendImage);
  return STATUS_SUCCESS;
}
