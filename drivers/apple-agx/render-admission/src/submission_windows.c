#include "render_admission.h"

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
#define ADMISSION_CORRELATE_SUBMIT_EXIT(Context, Arguments, Guard, Status)    \
  AdmissionRenderCorrelationSubmitWindows(                                   \
      (Context), (Arguments) == NULL ? 0ULL :                                \
          (ULONGLONG)(ULONG_PTR)(Arguments)->hContext,                        \
      FALSE, (Arguments) == NULL ? 0u : (Arguments)->SubmissionFenceId,       \
      (Guard), (Status))
#else
#define ADMISSION_CORRELATE_SUBMIT_EXIT(Context, Arguments, Guard, Status)    \
  ((void)0)
#endif

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
  ULONG packet_guard = AdmissionSubmitPacketGuardAccepted;
#define GDI_SUBMIT_RETURN(guard, value)                                       \
  do {                                                                       \
    NTSTATUS gdiStatus = (value);                                            \
    ADMISSION_CORRELATE_SUBMIT_EXIT(                                          \
        Context, Args, (guard), gdiStatus);                                   \
    AdmissionSubmitRenderGuardWindows(Context, (guard), gdiStatus);          \
    AdmissionGdiReceiptSubmitWindows(Context, Args, gdiStatus);              \
    return gdiStatus;                                                        \
  } while (0)

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  AdmissionRenderCorrelationSubmitWindows(
      Context, Args == NULL ? 0ULL :
                   (ULONGLONG)(ULONG_PTR)Args->hContext,
      TRUE, Args == NULL ? 0u : Args->SubmissionFenceId,
      MAXULONG, STATUS_PENDING);
#endif

  if (Context == NULL || Args == NULL)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardArgs,
                      STATUS_INVALID_PARAMETER);
  if (!Context->Started)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardStarted,
                      STATUS_INVALID_PARAMETER);
  if (!AdmissionPlatformRuntimeReady(Context))
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardRuntime,
                      STATUS_INVALID_PARAMETER);
  if (Args->Flags.Value != 0u)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardFlags,
                      STATUS_INVALID_PARAMETER);
  if (Args->SubmissionFenceId == 0u)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardFenceArgument,
                      STATUS_INVALID_PARAMETER);
  if (Args->NodeOrdinal != 0u)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardNodeArgument,
                      STATUS_INVALID_PARAMETER);
  if (Args->EngineOrdinal != 0u)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardEngineArgument,
                      STATUS_INVALID_PARAMETER);
  if (Args->hContext == NULL)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardContextArgument,
                      STATUS_INVALID_PARAMETER);
  if (Args->pDmaBufferPrivateData == NULL)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardPrivateArgument,
                      STATUS_INVALID_PARAMETER);
  if (Args->DmaBufferPrivateDataSize != ADMISSION_GDI_DMA_PRIVATE_SIZE)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardPrivateSize,
                      STATUS_INVALID_PARAMETER);
  if (Args->DmaBufferPrivateDataSubmissionStartOffset != 0u ||
      Args->DmaBufferPrivateDataSubmissionEndOffset >
          Args->DmaBufferPrivateDataSize)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardPrivateRange,
                      STATUS_INVALID_PARAMETER);
  if (Args->DmaBufferSubmissionStartOffset >=
          Args->DmaBufferSubmissionEndOffset ||
      Args->DmaBufferSubmissionEndOffset > Args->DmaBufferSize)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardDmaRange,
                      STATUS_INVALID_PARAMETER);
  render_context = (ADMISSION_RENDER_CONTEXT *)Args->hContext;
  if (render_context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardContextMagic,
                      STATUS_INVALID_HANDLE);
  if (render_context->Object.Device == NULL)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardContextDevice,
                      STATUS_INVALID_HANDLE);
  if (render_context->Object.Device->Adapter != &Context->ObjectAdapter)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardAdapter,
                      STATUS_INVALID_HANDLE);
  if ((render_context->Object.Flags & ADMISSION_CONTEXT_SYSTEM) != 0u)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardSystem,
                      STATUS_INVALID_HANDLE);
  if (!render_context->SchedulerContext.Active)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardSchedulerInactive,
                      STATUS_INVALID_HANDLE);
  if (render_context->SchedulerContext.NodeOrdinal != Args->NodeOrdinal)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardNode,
                      STATUS_INVALID_HANDLE);
  if ((render_context->SchedulerContext.EngineAffinity &
       (1u << Args->EngineOrdinal)) == 0u)
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardEngine,
                      STATUS_INVALID_HANDLE);
  AdmissionSubmitFenceDetailWindows(Context,
        render_context->Object.FenceOutstanding,
        Args->SubmissionFenceId);
  if (render_context->Object.FenceOutstanding == 0u &&
      !NT_SUCCESS(AdmissionGdiAdoptPrepatchedPacket(
          Context, render_context, Args)))
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardFence,
                      STATUS_INVALID_HANDLE);
  if (render_context->Object.FenceOutstanding != Args->SubmissionFenceId) {
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardFence,
                      STATUS_INVALID_HANDLE);
  }
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
    GDI_SUBMIT_RETURN(AdmissionSubmitRenderGuardShadow,
                      STATUS_INVALID_USER_BUFFER);

  KeAcquireSpinLockAtDpcLevel(&Context->SchedulerLock);
  if (AdmissionRenderPacketState(&Context->RenderPacket) !=
      AdmissionRenderPacketPrepared)
    packet_guard = AdmissionSubmitPacketGuardState;
  else if (Context->RenderPacket.Description.Fence != Args->SubmissionFenceId)
    packet_guard = AdmissionSubmitPacketGuardFence;
  else if (Context->RenderPacket.Description.ContextToken !=
           (ULONGLONG)(ULONG_PTR)render_context)
    packet_guard = AdmissionSubmitPacketGuardContext;
  else if (Context->RenderPacket.Description.PrivateDataToken !=
           (ULONGLONG)(ULONG_PTR)Args->pDmaBufferPrivateData)
    packet_guard = AdmissionSubmitPacketGuardPrivate;
  else if (Context->RenderPacket.Description.PrivateDataEnd != shadow.BytesUsed)
    packet_guard = AdmissionSubmitPacketGuardPrivateEnd;
  else if (Context->RenderPacket.Description.DmaStart !=
           Args->DmaBufferSubmissionStartOffset)
    packet_guard = AdmissionSubmitPacketGuardDmaStart;
  else if (Context->RenderPacket.Description.DmaEnd !=
           Args->DmaBufferSubmissionEndOffset)
    packet_guard = AdmissionSubmitPacketGuardDmaEnd;
  else if (!AdmissionBackendImageBindSubmission(
          &Context->BackendImage,
          &Context->RenderPacket.Description,
          (PVOID)(ULONG_PTR)Context->RenderPacket.Description
              .DestinationCpuToken,
          view.Bytes, view.DmaBytes, &binding))
    packet_guard = AdmissionSubmitPacketGuardBind;
  else {
    bound = TRUE;
    if (!AppleAgxSchedulerQueueFence(
            &Context->Scheduler, Args->NodeOrdinal,
            Args->EngineOrdinal, Args->SubmissionFenceId))
      packet_guard = AdmissionSubmitPacketGuardScheduler;
    else if (!AdmissionRenderPacketQueue(
          &Context->RenderPacket, Args->SubmissionFenceId,
          (ULONGLONG)(ULONG_PTR)render_context,
          (ULONGLONG)(ULONG_PTR)Args->pDmaBufferPrivateData,
          Args->DmaBufferSubmissionStartOffset,
          Args->DmaBufferSubmissionEndOffset))
      packet_guard = AdmissionSubmitPacketGuardQueue;
    else
      accepted = TRUE;
  }
  if (bound && !accepted &&
      !AdmissionBackendImageReleaseSubmission(
          &Context->BackendImage, Args->SubmissionFenceId))
    InterlockedExchange(&Context->SchedulerFaulted, 1);
  KeReleaseSpinLockFromDpcLevel(&Context->SchedulerLock);
  if (!accepted) {
    /* Capture the return owner in the adapter/boot record before touching the
     * optional broker transport. */
    ADMISSION_CORRELATE_SUBMIT_EXIT(
        Context, Args, AdmissionSubmitRenderGuardPacket,
        STATUS_DEVICE_BUSY);
    AdmissionSubmitPacketGuardWindows(
        Context, packet_guard, STATUS_DEVICE_BUSY);
    AdmissionSubmitRenderGuardWindows(
        Context, AdmissionSubmitRenderGuardPacket, STATUS_DEVICE_BUSY);
    AdmissionGdiReceiptSubmitWindows(Context, Args, STATUS_DEVICE_BUSY);
    return STATUS_DEVICE_BUSY;
  }
  AdmissionGdiReceiptSubmitWindows(Context, Args, STATUS_SUCCESS);
  AdmissionSubmitRenderGuardWindows(
      Context, AdmissionSubmitRenderGuardAccepted, STATUS_SUCCESS);
  AdmissionRenderCorrelationSubmitWindows(
      Context, (ULONGLONG)(ULONG_PTR)Args->hContext, FALSE,
      Args->SubmissionFenceId, AdmissionSubmitRenderGuardAccepted,
      STATUS_SUCCESS);
  AdmissionDispatchQueuedWork(Context);
  return STATUS_SUCCESS;
#undef GDI_SUBMIT_RETURN
}

#undef ADMISSION_CORRELATE_SUBMIT_EXIT

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
  } else if (AdmissionPrepatchedCancel(
                 &context->PrepatchedRender,
                 (ULONGLONG)(ULONG_PTR)context)) {
    cancelled = TRUE;
  }
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  return cancelled ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
}
