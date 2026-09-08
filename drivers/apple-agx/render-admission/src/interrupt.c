#include "render_admission.h"
#include "j313_agx_abi_admission.generated.h"

#define ADMISSION_INTERRUPT_MESSAGE_NUMBER 0u

static volatile ULONG *AdmissionBrokerRegister(ADMISSION_CONTEXT *Context,
                                                ULONG Offset) {
  if (Context == NULL || Context->BrokerBase == NULL ||
      Offset > J313_AGX_ABI_ADMISSION_POWER_BROKER_SIZE - sizeof(ULONG))
    return NULL;
  return (volatile ULONG *)(Context->BrokerBase + Offset);
}

static ULONG AdmissionReadIrqStatus(ADMISSION_CONTEXT *Context) {
  volatile ULONG *status = AdmissionBrokerRegister(
      Context, J313_AGX_ABI_ADMISSION_SCANOUT_IRQ_STATUS_OFFSET);
  return status == NULL ? 0u : READ_REGISTER_ULONG(status);
}

static VOID AdmissionMaskInterrupt(ADMISSION_CONTEXT *Context) {
  volatile ULONG *enable = AdmissionBrokerRegister(
      Context, J313_AGX_ABI_ADMISSION_SCANOUT_IRQ_ENABLE_OFFSET);
  if (enable != NULL)
    WRITE_REGISTER_ULONG(enable, 0u);
}

static ULONG AdmissionAcknowledgeInterrupt(ADMISSION_CONTEXT *Context) {
  volatile ULONG *status_register = AdmissionBrokerRegister(
      Context, J313_AGX_ABI_ADMISSION_SCANOUT_IRQ_STATUS_OFFSET);
  ULONG status = AdmissionReadIrqStatus(Context) &
                 J313_AGX_ABI_ADMISSION_SCANOUT_IRQ_MASK;
  if (status_register != NULL && status != 0u)
    WRITE_REGISTER_ULONG(status_register, status);
  return status;
}

static NTSTATUS AdmissionFindBrokerResource(
    PCM_RESOURCE_LIST Resources, PPHYSICAL_ADDRESS BrokerAddress) {
  ULONG interrupt_count = 0u;
  ULONG broker_count = 0u;
  ULONG full_index;

  if (Resources == NULL || BrokerAddress == NULL || Resources->Count != 1u)
    return STATUS_DEVICE_CONFIGURATION_ERROR;
  BrokerAddress->QuadPart = 0;
  for (full_index = 0u; full_index < Resources->Count; ++full_index) {
    PCM_FULL_RESOURCE_DESCRIPTOR full = &Resources->List[full_index];
    ULONG partial_index;
    for (partial_index = 0u;
         partial_index < full->PartialResourceList.Count; ++partial_index) {
      PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor =
          &full->PartialResourceList.PartialDescriptors[partial_index];
      if (descriptor->Type == CmResourceTypeMemory &&
          (ULONGLONG)descriptor->u.Memory.Start.QuadPart ==
              J313_AGX_ABI_ADMISSION_POWER_BROKER_BASE &&
          descriptor->u.Memory.Length ==
              J313_AGX_ABI_ADMISSION_POWER_BROKER_SIZE) {
        ++broker_count;
        *BrokerAddress = descriptor->u.Memory.Start;
      } else if (descriptor->Type == CmResourceTypeInterrupt) {
        ++interrupt_count;
      }
    }
  }
  return broker_count == 1u && interrupt_count == 1u
             ? STATUS_SUCCESS
             : STATUS_DEVICE_CONFIGURATION_ERROR;
}

static BOOLEAN AdmissionInterruptIngressClosed(PVOID MiniportDeviceContext) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  return context != NULL &&
                 InterlockedCompareExchange(
                     &context->InterruptIngressEnabled, 0, 0) == 0
             ? TRUE
             : FALSE;
}

_Use_decl_annotations_ NTSTATUS
AdmissionInterruptStart(ADMISSION_CONTEXT *Context) {
  PHYSICAL_ADDRESS broker_address;
  PVOID broker_base = NULL;
  NTSTATUS status;

  if (Context == NULL || !Context->InterfaceValid ||
      Context->Interface.DxgkCbMapMemory == NULL ||
      Context->Interface.DxgkCbUnmapMemory == NULL ||
      Context->BrokerBase != NULL)
    return STATUS_INVALID_PARAMETER;
  status = AdmissionFindBrokerResource(
      Context->DeviceInformation.TranslatedResourceList, &broker_address);
  if (!NT_SUCCESS(status))
    return status;
  status = Context->Interface.DxgkCbMapMemory(
      Context->Interface.DeviceHandle, broker_address,
      J313_AGX_ABI_ADMISSION_POWER_BROKER_SIZE, FALSE, FALSE, MmNonCached,
      &broker_base);
  if (!NT_SUCCESS(status) || broker_base == NULL)
    return NT_SUCCESS(status) ? STATUS_NONE_MAPPED : status;

  Context->BrokerBase = (volatile UCHAR *)broker_base;
  AdmissionMaskInterrupt(Context);
  (void)AdmissionAcknowledgeInterrupt(Context);
  InterlockedExchange(&Context->InterruptCount, 0);
  InterlockedExchange(&Context->InterruptAckCount, 0);
  InterlockedExchange(&Context->LastInterruptStatus, 0);
  InterlockedExchange(&Context->DpcCount, 0);
  InterlockedExchange(&Context->InterruptIngressEnabled, 1);
  InterlockedExchange(&Context->InterruptReady, 1);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS
AdmissionInterruptStop(ADMISSION_CONTEXT *Context) {
  BOOLEAN closed = FALSE;
  NTSTATUS status;
  PVOID broker_base;

  if (Context == NULL)
    return STATUS_INVALID_PARAMETER;
  if (Context->BrokerBase == NULL)
    return STATUS_SUCCESS;
  InterlockedExchange(&Context->InterruptIngressEnabled, 0);
  AdmissionMaskInterrupt(Context);
  (void)AdmissionAcknowledgeInterrupt(Context);
  if (Context->Started && Context->InterfaceValid &&
      Context->Interface.DxgkCbSynchronizeExecution != NULL) {
    status = Context->Interface.DxgkCbSynchronizeExecution(
        Context->Interface.DeviceHandle, AdmissionInterruptIngressClosed,
        Context, ADMISSION_INTERRUPT_MESSAGE_NUMBER, &closed);
    if (!NT_SUCCESS(status) || !closed)
      return NT_SUCCESS(status) ? STATUS_DEVICE_BUSY : status;
  }
  InterlockedExchange(&Context->InterruptReady, 0);
  broker_base = (PVOID)Context->BrokerBase;
  status = Context->Interface.DxgkCbUnmapMemory(
      Context->Interface.DeviceHandle, broker_base);
  if (!NT_SUCCESS(status))
    return status;
  Context->BrokerBase = NULL;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ BOOLEAN AdmissionDdiInterruptRoutine(
    PVOID MiniportDeviceContext, ULONG MessageNumber) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  ULONG status;

  if (context == NULL || MessageNumber != 0 ||
      InterlockedCompareExchange(&context->InterruptReady, 0, 0) == 0 ||
      InterlockedCompareExchange(&context->InterruptIngressEnabled, 0, 0) == 0)
    return FALSE;
  if (context->ScanoutRuntime != NULL)
    return AdmissionScanoutInterrupt(context);
  status = AdmissionAcknowledgeInterrupt(context);
  if (status == 0u)
    return FALSE;
  InterlockedExchange(&context->LastInterruptStatus, (LONG)status);
  InterlockedIncrement(&context->InterruptCount);
  InterlockedIncrement(&context->InterruptAckCount);
  return TRUE;
}

VOID AdmissionDdiDpcRoutine(
    PVOID MiniportDeviceContext) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  if (context != NULL) {
    InterlockedIncrement(&context->DpcCount);
    AdmissionPagingDpc(context);
    if (context->InterfaceValid &&
        context->Interface.DxgkCbNotifyDpc != NULL)
      context->Interface.DxgkCbNotifyDpc(
          context->Interface.DeviceHandle);
    AdmissionSchedulerDpc(context);
  }
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiControlInterrupt(
    HANDLE Adapter, DXGK_INTERRUPT_TYPE InterruptType,
    BOOLEAN EnableInterrupt) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;

  if (context == NULL)
    return STATUS_INVALID_PARAMETER;
  if (InterruptType != DXGK_INTERRUPT_CRTC_VSYNC)
    return STATUS_NOT_IMPLEMENTED;
  if (context->ScanoutRuntime != NULL)
    return AdmissionScanoutControlInterrupt(context, EnableInterrupt);
  if (EnableInterrupt)
    return STATUS_NOT_SUPPORTED;
  AdmissionMaskInterrupt(context);
  (void)AdmissionAcknowledgeInterrupt(context);
  return STATUS_SUCCESS;
}

#undef ADMISSION_INTERRUPT_MESSAGE_NUMBER
