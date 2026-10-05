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

_Use_decl_annotations_ VOID AdmissionPrepatchAdoptGuardWindows(
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
      AdmissionPrepatchAdoptGuardWord(Guard, (ULONG)Status));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

_Use_decl_annotations_ VOID AdmissionSubmitPacketGuardWindows(
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
      AdmissionSubmitPacketGuardWord(Guard, (ULONG)Status));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

_Use_decl_annotations_ VOID AdmissionBackendSubmitResultWindows(
    ADMISSION_CONTEXT *Context, ULONG Result, ULONG Phase) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  if (Context == NULL || Context->BrokerBase == NULL)
    return;
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(request,
      AdmissionBackendSubmitResultWord(Result, Phase));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

_Use_decl_annotations_ VOID AdmissionTerminalObservationTraceWindows(
    ADMISSION_CONTEXT *Context, ULONG Source, ULONG CompletionStatus,
    ULONG RuntimePhase, ULONG ProviderPhase, ULONG Fence) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  if (Context == NULL || Context->BrokerBase == NULL)
    return;
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(request, AdmissionTerminalObservationWord(
      Source, CompletionStatus, RuntimePhase, ProviderPhase, Fence));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

_Use_decl_annotations_ VOID AdmissionTerminalExitTraceWindows(
    ADMISSION_CONTEXT *Context, ULONG Reason, ULONG ValidMask,
    ULONG RuntimePhase, ULONG ProviderPhase, ULONG Fence) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  if (Context == NULL || Context->BrokerBase == NULL)
    return;
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(request, AdmissionTerminalExitWord(
      Reason, ValidMask, RuntimePhase, ProviderPhase, Fence));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

_Use_decl_annotations_ VOID AdmissionBackendProgressWindows(
    ADMISSION_CONTEXT *Context,
    const APPLE_AGX_G13_QUEUE_PROGRESS *Progress) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  ULONG flags;
  if (Context == NULL || Context->BrokerBase == NULL || Progress == NULL)
    return;
  flags = ((ULONG)Progress->ProviderPhase & 0xfu) |
      (((ULONG)Progress->RuntimePhase & 0xfu) << 4u) |
      ((Progress->TaEventSeen ? 1u : 0u) << 8u) |
      ((Progress->TaComplete ? 1u : 0u) << 9u) |
      ((Progress->D3EventSeen ? 1u : 0u) << 10u) |
      ((Progress->D3Complete ? 1u : 0u) << 11u);
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(request,
      AdmissionBackendProgressWord(flags, Progress->Fence));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

_Use_decl_annotations_ VOID AdmissionBackendChannelProgressWindows(
    ADMISSION_CONTEXT *Context, ULONG TaRead, ULONG D3Read, ULONG Fence) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  if (Context == NULL || Context->BrokerBase == NULL)
    return;
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(request,
      AdmissionBackendChannelProgressWord(TaRead, D3Read, Fence));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

_Use_decl_annotations_ VOID AdmissionProviderPollGuardWindows(
    ADMISSION_CONTEXT *Context, ULONG Guard, ULONG ProviderPhase,
    ULONG RuntimePhase, ULONG Fence) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  if (Context == NULL || Context->BrokerBase == NULL)
    return;
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(request,
      AdmissionProviderPollGuardWord(
          Guard, ProviderPhase, RuntimePhase, Fence));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

_Use_decl_annotations_ VOID AdmissionProviderDrainTraceWindows(
    ADMISSION_CONTEXT *Context, ULONG Guard, ULONG ReadPointer,
    ULONG WritePointer) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  if (Context == NULL || Context->BrokerBase == NULL)
    return;
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(
      request, AdmissionProviderDrainTraceWord(1u, Guard));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
  WRITE_REGISTER_ULONG64(
      request, AdmissionProviderDrainTraceWord(2u, ReadPointer));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
  WRITE_REGISTER_ULONG64(
      request, AdmissionProviderDrainTraceWord(3u, WritePointer));
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
