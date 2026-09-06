#include "render_admission.h"

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)

static VOID AdmissionSubmitTraceWrite(
    _In_ ADMISSION_CONTEXT *Context, _In_ ULONG Field, _In_ ULONG Value) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  if (Context == NULL || Context->BrokerBase == NULL)
    return;
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(request, AdmissionSubmitTraceWord(Field, Value));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

_Use_decl_annotations_ VOID AdmissionSubmitTraceValueWindows(
    ADMISSION_CONTEXT *Context, BOOLEAN Enabled, ULONG Field, ULONG Value) {
  if (Enabled)
    AdmissionSubmitTraceWrite(Context, Field, Value);
}

_Use_decl_annotations_ VOID AdmissionSubmitRenderGuardWindows(
    ADMISSION_CONTEXT *Context, ULONG Guard, NTSTATUS Status) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  if (Context == NULL || Context->BrokerBase == NULL)
    return;
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(request,
      AdmissionSubmitRenderGuardWord(Guard, (ULONG)Status));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

_Use_decl_annotations_ VOID AdmissionSubmitFenceDetailWindows(
    ADMISSION_CONTEXT *Context, ULONG Outstanding, ULONG Submitted) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  if (Context == NULL || Context->BrokerBase == NULL)
    return;
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(request,
      AdmissionSubmitFenceDetailWord(Outstanding, Submitted));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

_Use_decl_annotations_ VOID AdmissionPatchRenderGuardWindows(
    ADMISSION_CONTEXT *Context, ULONG Guard, NTSTATUS Status) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  if (Context == NULL || Context->BrokerBase == NULL)
    return;
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(request,
      AdmissionPatchRenderGuardWord(Guard, (ULONG)Status));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

static VOID AdmissionSubmitTraceU64(
    _In_ ADMISSION_CONTEXT *Context, _In_ ULONG LowField,
    _In_ ULONGLONG Value) {
  AdmissionSubmitTraceWrite(Context, LowField, (ULONG)Value);
  AdmissionSubmitTraceWrite(Context, LowField + 1u, (ULONG)(Value >> 32));
}

_Use_decl_annotations_ BOOLEAN AdmissionSubmitTraceBegin(
    ADMISSION_CONTEXT *Context, const DXGKARG_SUBMITCOMMAND *Args,
    ULONG PrivateStage, const ADMISSION_PRESENT_BLT_COMMAND *Command) {
  if (Context == NULL || Args == NULL || Args->Flags.Paging ||
      Context->BrokerBase == NULL ||
      InterlockedCompareExchange(&Context->SubmitTraceClaimed, 1, 0) != 0)
    return FALSE;
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTraceVersion, 1u);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTraceIrql,
                            (ULONG)KeGetCurrentIrql());
  AdmissionSubmitTraceU64(Context, AdmissionSubmitTraceArgsLow,
                          (ULONGLONG)(ULONG_PTR)Args);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTraceFlags,
                            Args->Flags.Value);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTraceNode,
                            Args->NodeOrdinal);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTraceEngine,
                            Args->EngineOrdinal);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTraceFence,
                            Args->SubmissionFenceId);
  AdmissionSubmitTraceU64(Context, AdmissionSubmitTraceContextLow,
                          (ULONGLONG)(ULONG_PTR)Args->hContext);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTraceDmaSegment,
                            Args->DmaBufferSegmentId);
  AdmissionSubmitTraceU64(Context, AdmissionSubmitTraceDmaPhysicalLow,
                          (ULONGLONG)Args->DmaBufferPhysicalAddress.QuadPart);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTraceDmaSize,
                            Args->DmaBufferSize);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTraceDmaStart,
                            Args->DmaBufferSubmissionStartOffset);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTraceDmaEnd,
                            Args->DmaBufferSubmissionEndOffset);
  AdmissionSubmitTraceU64(Context, AdmissionSubmitTracePrivateLow,
                          (ULONGLONG)(ULONG_PTR)Args->pDmaBufferPrivateData);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTracePrivateSize,
                            Args->DmaBufferPrivateDataSize);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTracePrivateStart,
                            Args->DmaBufferPrivateDataSubmissionStartOffset);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTracePrivateEnd,
                            Args->DmaBufferPrivateDataSubmissionEndOffset);
  AdmissionSubmitTraceU64(Context, AdmissionSubmitTraceDmaVirtualLow,
                          Args->DmaBufferVirtualAddress);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTraceVidPnSource,
                            Args->VidPnSourceId);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTraceFlipInterval,
                            (ULONG)Args->FlipInterval);
  AdmissionSubmitTraceWrite(Context, AdmissionSubmitTracePrivateStage,
                            PrivateStage);
  if (PrivateStage == AdmissionPresentPrivateValid && Command != NULL) {
    AdmissionSubmitTraceU64(Context, AdmissionSubmitTraceCommandContextLow,
                            Command->ContextToken);
    AdmissionSubmitTraceU64(Context, AdmissionSubmitTraceSourceLocationLow,
                            Command->SourceLocation);
    AdmissionSubmitTraceU64(Context,
                            AdmissionSubmitTraceDestinationLocationLow,
                            Command->DestinationLocation);
  }
  return TRUE;
}

#endif /* APPLE_AGX_SUBMIT_QUALIFICATION */
