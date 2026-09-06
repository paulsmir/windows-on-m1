#include "render_admission.h"

_Use_decl_annotations_ NTSTATUS AdmissionDdiSubmitRender(
    ADMISSION_CONTEXT *Context,
    const DXGKARG_SUBMITCOMMAND *Args) {
  ADMISSION_RENDER_CONTEXT *render_context;
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_DMA_SHADOW_VIEW view;
  ADMISSION_GDI_PREPARED prepared;
  APPLE_AGX_EXP208_GDI_BINDING binding;
  BOOLEAN accepted = FALSE;
  BOOLEAN bound = FALSE;

  if (Context == NULL || Args == NULL || !Context->Started ||
      !AdmissionPlatformRuntimeReady(Context) ||
      Args->Flags.Value != 0u || Args->SubmissionFenceId == 0u ||
      Args->NodeOrdinal != 0u || Args->EngineOrdinal != 0u ||
      Args->hContext == NULL || Args->pDmaBufferPrivateData == NULL ||
      Args->DmaBufferPrivateDataSize != ADMISSION_GDI_DMA_PRIVATE_SIZE ||
      Args->DmaBufferPrivateDataSubmissionStartOffset != 0u ||
      Args->DmaBufferPrivateDataSubmissionEndOffset >
          Args->DmaBufferPrivateDataSize ||
      Args->DmaBufferSubmissionStartOffset >=
          Args->DmaBufferSubmissionEndOffset ||
      Args->DmaBufferSubmissionEndOffset > Args->DmaBufferSize)
    return STATUS_INVALID_PARAMETER;
  render_context = (ADMISSION_RENDER_CONTEXT *)Args->hContext;
  if (render_context->Object.Magic !=
          ADMISSION_OBJECT_CONTEXT_MAGIC ||
      render_context->Object.Device == NULL ||
      render_context->Object.Device->Adapter !=
          &Context->ObjectAdapter ||
      (render_context->Object.Flags & ADMISSION_CONTEXT_GDI) == 0u ||
      !render_context->SchedulerContext.Active ||
      render_context->SchedulerContext.NodeOrdinal != Args->NodeOrdinal ||
      (render_context->SchedulerContext.EngineAffinity &
       (1u << Args->EngineOrdinal)) == 0u ||
      render_context->Object.FenceOutstanding !=
          Args->SubmissionFenceId)
    return STATUS_INVALID_HANDLE;
  if (!AppleAgxDmaShadowOpen(
          &shadow, Args->pDmaBufferPrivateData,
          Args->DmaBufferPrivateDataSize) ||
      !AppleAgxDmaShadowIsSealedForFence(
          shadow.Storage, shadow.BytesUsed,
          Args->SubmissionFenceId) ||
      !AdmissionNonPagingPrivateRangeCovers(
          shadow.BytesUsed,
          Args->DmaBufferPrivateDataSubmissionStartOffset,
          Args->DmaBufferPrivateDataSubmissionEndOffset,
          Args->DmaBufferPrivateDataSize) ||
      !AppleAgxDmaShadowFind(
          shadow.Storage, shadow.BytesUsed,
          Args->DmaBufferSubmissionStartOffset,
          Args->DmaBufferSubmissionEndOffset -
              Args->DmaBufferSubmissionStartOffset,
          &view) ||
      !AdmissionGdiDescribePreparedRecord(
          view.Bytes, view.DmaBytes, view.DmaOffset, &prepared))
    return STATUS_INVALID_USER_BUFFER;

  KeAcquireSpinLockAtDpcLevel(&Context->SchedulerLock);
  if (AdmissionRenderPacketState(&Context->RenderPacket) ==
          AdmissionRenderPacketPrepared &&
      Context->RenderPacket.Description.Fence ==
          Args->SubmissionFenceId &&
      Context->RenderPacket.Description.ContextToken ==
          (ULONGLONG)(ULONG_PTR)render_context &&
      Context->RenderPacket.Description.PrivateDataToken ==
          (ULONGLONG)(ULONG_PTR)Args->pDmaBufferPrivateData &&
      Context->RenderPacket.Description.PrivateDataEnd ==
          shadow.BytesUsed &&
      Context->RenderPacket.Description.DmaStart ==
          Args->DmaBufferSubmissionStartOffset &&
      Context->RenderPacket.Description.DmaEnd ==
          Args->DmaBufferSubmissionEndOffset &&
      AdmissionBackendImageBindSubmission(
          &Context->BackendImage,
          &Context->RenderPacket.Description,
          (PVOID)(ULONG_PTR)Context->RenderPacket.Description
              .DestinationCpuToken,
          view.Bytes, view.DmaBytes, &binding)) {
    bound = TRUE;
    if (AppleAgxSchedulerQueueFence(
          &Context->Scheduler, Args->NodeOrdinal,
          Args->EngineOrdinal, Args->SubmissionFenceId) &&
        AdmissionRenderPacketQueue(
          &Context->RenderPacket, Args->SubmissionFenceId,
          (ULONGLONG)(ULONG_PTR)render_context,
          (ULONGLONG)(ULONG_PTR)Args->pDmaBufferPrivateData,
          Args->DmaBufferSubmissionStartOffset,
          Args->DmaBufferSubmissionEndOffset))
      accepted = TRUE;
  }
  if (bound && !accepted &&
      !AdmissionBackendImageReleaseSubmission(
          &Context->BackendImage, Args->SubmissionFenceId))
    InterlockedExchange(&Context->SchedulerFaulted, 1);
  KeReleaseSpinLockFromDpcLevel(&Context->SchedulerLock);
  if (!accepted)
    return STATUS_DEVICE_BUSY;
  AdmissionDispatchQueuedWork(Context);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiCancelCommand(
    HANDLE Adapter, const DXGKARG_CANCELCOMMAND *Args) {
  ADMISSION_CONTEXT *adapter = (ADMISSION_CONTEXT *)Adapter;
  ADMISSION_RENDER_CONTEXT *context;
  BOOLEAN cancelled = FALSE;
  KIRQL old_irql;

  if (adapter == NULL || Args == NULL || Args->hContext == NULL)
    return STATUS_INVALID_PARAMETER;
  context = (ADMISSION_RENDER_CONTEXT *)Args->hContext;
  if (context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device == NULL ||
      context->Object.Device->Adapter != &adapter->ObjectAdapter)
    return STATUS_INVALID_HANDLE;
  /* CancelCommand concerns a packet not submitted to the hardware queue.
   * BLT preparation holds only buffer-owned snapshots, no runtime resources. */
  if (AdmissionPresentIsBltPrivate(Args->pDmaBufferPrivateData,
                                  Args->DmaBufferPrivateDataSize))
    return STATUS_SUCCESS;
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (AdmissionRenderPacketCancelPrepared(
          &adapter->RenderPacket,
          (ULONGLONG)(ULONG_PTR)context)) {
    context->Object.FenceOutstanding = 0u;
    cancelled = TRUE;
  }
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  return cancelled ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
}
