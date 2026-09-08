#include "render_admission.h"

C_ASSERT(sizeof(ADMISSION_UMD_COLOR_FILL_COMMAND) == 48u);
C_ASSERT(sizeof(APPLE_AGX_WIN32_COMMAND_HEADER) == 48u);
C_ASSERT(sizeof(APPLE_AGX_WIN32_ALLOCATION_REFERENCE) == 32u);
C_ASSERT(sizeof(APPLE_AGX_WIN32_CLEAR_PAYLOAD) == 48u);

static ULONGLONG AdmissionUmdCommandHash(const VOID *Data, ULONG Bytes) {
  const UCHAR *data = (const UCHAR *)Data;
  ULONGLONG hash = 14695981039346656037ULL;
  ULONG index;
  for (index = 0u; index < Bytes; ++index) {
    hash ^= data[index];
    hash *= 1099511628211ULL;
  }
  return hash;
}

typedef struct _ADMISSION_WIN32_RENDER_LOOKUP_CONTEXT {
  ADMISSION_RENDER_CONTEXT *RenderContext;
  const DXGKARG_RENDER *Args;
  ADMISSION_CONTEXT *Adapter;
} ADMISSION_WIN32_RENDER_LOOKUP_CONTEXT;

static int AdmissionWin32RenderLookupAllocation(
    void *LookupContext, APPLE_AGX_U32 AllocationIndex,
    ADMISSION_WIN32_ALLOCATION_FACT *Fact) {
  ADMISSION_WIN32_RENDER_LOOKUP_CONTEXT *lookup =
      (ADMISSION_WIN32_RENDER_LOOKUP_CONTEXT *)LookupContext;
  const ADMISSION_OPEN_ALLOCATION *opened;
  if (lookup == NULL || lookup->RenderContext == NULL ||
      lookup->RenderContext->Object.Device == NULL || lookup->Args == NULL ||
      lookup->Args->pAllocationList == NULL ||
      AllocationIndex >= lookup->Args->AllocationListSize || Fact == NULL)
    return 0;
  opened = (const ADMISSION_OPEN_ALLOCATION *)
      lookup->Args->pAllocationList[AllocationIndex]
          .hDeviceSpecificAllocation;
  if (opened == NULL || opened->Magic != ADMISSION_OPEN_ALLOCATION_MAGIC ||
      opened->Device == NULL ||
      &opened->Device->Object != lookup->RenderContext->Object.Device ||
      opened->Allocation == NULL ||
      !AdmissionAllocationDescriptionValid(&opened->Allocation->Description))
    return 0;
  RtlZeroMemory(Fact, sizeof(*Fact));
  Fact->AllocationToken = (ULONGLONG)(ULONG_PTR)opened;
  Fact->Bytes = opened->Allocation->Description.Size;
  Fact->SegmentId = lookup->Args->pAllocationList[AllocationIndex].SegmentId;
  Fact->Writable = opened->ReadOnly ? 0u : 1u;
  Fact->Generation = opened->Win32Generation;
  Fact->ClassId = opened->Win32ClassId;
  Fact->Flags = opened->Win32Flags;
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
  Fact->ActiveForDisplay =
      AdmissionScanoutAllowsRender(lookup->Adapter, opened->Allocation)
          ? 0u
          : 1u;
#else
  Fact->ActiveForDisplay = 0u;
#endif
  return 1;
}

_Use_decl_annotations_ NTSTATUS AdmissionWin32SnapshotRenderCommand(
    ADMISSION_RENDER_CONTEXT *Context, const DXGKARG_RENDER *Args,
    ADMISSION_WIN32_RENDER_SNAPSHOT *Snapshot) {
  ADMISSION_WIN32_RENDER_LOOKUP_CONTEXT lookup;
  APPLE_AGX_WIN32_ABI_RESULT abiResult;
  ADMISSION_WIN32_TRANSPORT_RESULT referenceResult;
  if (Context == NULL || !Context->Win32Transport ||
      Context->Win32Generation == 0u || Args == NULL ||
      Context->Object.Device == NULL ||
      Context->Object.Device->Adapter == NULL ||
      Args->pCommand == NULL || Args->pAllocationList == NULL ||
      Args->AllocationListSize == 0u || Snapshot == NULL ||
      Args->CommandLength < sizeof(APPLE_AGX_WIN32_COMMAND_HEADER) ||
      Args->CommandLength > APPLE_AGX_WIN32_COMMAND_MAX_BYTES)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(Snapshot, sizeof(*Snapshot));
  __try {
    RtlCopyMemory(Snapshot->Storage, Args->pCommand, Args->CommandLength);
  }
  __except(EXCEPTION_EXECUTE_HANDLER) {
    return STATUS_INVALID_USER_BUFFER;
  }
  Snapshot->Bytes = Args->CommandLength;
  Snapshot->Generation = Context->Win32Generation;
  abiResult = AppleAgxWin32CommandValidate(
      Snapshot->Storage, Snapshot->Bytes, Snapshot->Generation,
      Args->AllocationListSize, &Snapshot->View);
  if (abiResult != AppleAgxWin32AbiSuccess)
    return STATUS_INVALID_USER_BUFFER;
  lookup.RenderContext = Context;
  lookup.Args = Args;
  lookup.Adapter = CONTAINING_RECORD(Context->Object.Device->Adapter,
                                     ADMISSION_CONTEXT, ObjectAdapter);
  referenceResult = AdmissionWin32ValidateReferences(
      &Snapshot->View, Snapshot->Generation,
      AdmissionWin32RenderLookupAllocation, &lookup, Snapshot->Facts,
      ARRAYSIZE(Snapshot->Facts));
  return referenceResult == AdmissionWin32TransportSuccess
             ? STATUS_SUCCESS
             : STATUS_INVALID_USER_BUFFER;
}

static BOOLEAN AdmissionUmdRenderOpenValid(
    const ADMISSION_RENDER_CONTEXT *Context,
    const DXGK_ALLOCATIONLIST *Allocations, UINT AllocationCount,
    UINT Index, BOOLEAN Write) {
  const ADMISSION_OPEN_ALLOCATION *opened;
  if (Context == NULL || Context->Object.Device == NULL ||
      Allocations == NULL || Index >= AllocationCount)
    return FALSE;
  opened = (const ADMISSION_OPEN_ALLOCATION *)
      Allocations[Index].hDeviceSpecificAllocation;
  return opened != NULL &&
         opened->Magic == ADMISSION_OPEN_ALLOCATION_MAGIC &&
         opened->Device != NULL &&
         &opened->Device->Object == Context->Object.Device &&
         opened->Allocation != NULL &&
         AdmissionAllocationDescriptionValid(
             &opened->Allocation->Description) &&
         (!Write || !opened->ReadOnly);
}

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
static PVOID volatile AdmissionUmdRenderTraceAdapter;
static volatile LONG AdmissionUmdRenderTraceCallCount;

_Use_decl_annotations_ VOID AdmissionUmdRenderTraceArm(
    ADMISSION_CONTEXT *Context) {
  InterlockedExchangePointer(&AdmissionUmdRenderTraceAdapter, Context);
}

_Use_decl_annotations_ VOID AdmissionUmdRenderTraceDisarm(
    ADMISSION_CONTEXT *Context) {
  (void)InterlockedCompareExchangePointer(
      &AdmissionUmdRenderTraceAdapter, NULL, Context);
}

static ADMISSION_CONTEXT *AdmissionUmdRenderTraceAdapterGet(VOID) {
  return (ADMISSION_CONTEXT *)InterlockedCompareExchangePointer(
      &AdmissionUmdRenderTraceAdapter, NULL, NULL);
}

static VOID AdmissionUmdRenderTraceWrite(
    ADMISSION_CONTEXT *Context, ULONG Field, ULONG Value) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  if (Context == NULL || Context->BrokerBase == NULL)
    return;
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(request, AdmissionUmdRenderTraceWord(Field, Value));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

static BOOLEAN AdmissionUmdRenderTraceBegin(
    ADMISSION_CONTEXT *Context, const ADMISSION_RENDER_CONTEXT *RenderContext,
    const DXGKARG_RENDER *Args) {
  ULONG contextFlags = ~0u;
  ULONG callSequence;
  if (Context == NULL)
    return FALSE;
  if (RenderContext != NULL &&
      RenderContext->Object.Magic == ADMISSION_OBJECT_CONTEXT_MAGIC)
    contextFlags = RenderContext->Object.Flags;
  callSequence = (ULONG)InterlockedIncrement(
      &AdmissionUmdRenderTraceCallCount);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceVersion,
      (2u << 16) | (callSequence & 0xffffu));
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceIrql, (ULONG)KeGetCurrentIrql());
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceContextFlags,
      contextFlags);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceCommandLength,
      Args == NULL ? 0u : Args->CommandLength);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceDmaSize,
      Args == NULL ? 0u : Args->DmaSize);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTracePrivateSize,
      Args == NULL ? 0u : Args->DmaBufferPrivateDataSize);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceAllocationCount,
      Args == NULL ? 0u : Args->AllocationListSize);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTracePatchInCount,
      Args == NULL ? 0u : Args->PatchLocationListInSize);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTracePatchOutCount,
      Args == NULL ? 0u : Args->PatchLocationListOutSize);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceMultipass,
      Args == NULL ? 0u : Args->MultipassOffset);
  return TRUE;
}

static VOID AdmissionUmdRenderTraceCommand(
    ADMISSION_CONTEXT *Context, BOOLEAN Enabled,
    const ADMISSION_RENDER_CONTEXT *RenderContext,
    const ADMISSION_UMD_COLOR_FILL_COMMAND *Command) {
  ULONGLONG contextToken;
  ULONGLONG commandHash;
  if (!Enabled || Context == NULL || Command == NULL)
    return;
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceCommandMagic, Command->Magic);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceCommandVersion, Command->Version);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceCommandBytes, Command->Bytes);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceCommandOpcode, Command->Opcode);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceDestinationIndex,
      Command->DestinationAllocationIndex);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceColor, Command->Color);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceRop, Command->Rop);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceRop3, Command->Rop3);
  contextToken = (ULONGLONG)(ULONG_PTR)RenderContext;
  commandHash = AdmissionUmdCommandHash(Command, sizeof(*Command));
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceContextLow, (ULONG)contextToken);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceContextHigh,
      (ULONG)(contextToken >> 32));
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceCommandHashLow, (ULONG)commandHash);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceCommandHashHigh,
      (ULONG)(commandHash >> 32));
}

static VOID AdmissionUmdRenderTraceResult(
    ADMISSION_CONTEXT *Context, BOOLEAN Enabled, ULONG Guard,
    NTSTATUS Status, ULONG DmaBytesProduced, ULONG PatchesProduced,
    BOOLEAN Prepatched) {
  if (!Enabled || Context == NULL)
    return;
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceGuard, Guard);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceStatus, (ULONG)Status);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTraceDmaBytesProduced, DmaBytesProduced);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTracePatchesProduced, PatchesProduced);
  AdmissionUmdRenderTraceWrite(
      Context, AdmissionUmdRenderTracePrepatched, Prepatched ? 1u : 0u);
}
#else
#define AdmissionRecordUmdRenderGuard(Context, Guard, Status)                 \
  ((void)(Context), (void)(Guard), (void)(Status))
#define AdmissionUmdRenderTraceAdapterGet() ((ADMISSION_CONTEXT *)NULL)
#define AdmissionUmdRenderTraceBegin(Context, RenderContext, Args)            \
  ((void)(Context), (void)(RenderContext), (void)(Args), FALSE)
#define AdmissionUmdRenderTraceCommand(Context, Enabled, RenderContext,       \
                                       Command)                               \
  do {                                                                        \
    (void)(Context);                                                          \
    (void)(Enabled);                                                          \
    (void)(RenderContext);                                                    \
    (void)(Command);                                                          \
  } while (0)
#define AdmissionUmdRenderTraceResult(Context, Enabled, Guard, Status,        \
                                      DmaBytes, Patches, Prepatched)          \
  do {                                                                        \
    (void)(Context);                                                          \
    (void)(Enabled);                                                          \
    (void)(Guard);                                                            \
    (void)(Status);                                                           \
    (void)(DmaBytes);                                                         \
    (void)(Patches);                                                          \
    (void)(Prepatched);                                                       \
  } while (0)
#endif

_Use_decl_annotations_ NTSTATUS AdmissionDdiRender(
    HANDLE Context, DXGKARG_RENDER *Args) {
  ADMISSION_RENDER_CONTEXT *context = (ADMISSION_RENDER_CONTEXT *)Context;
  ADMISSION_CONTEXT *adapter;
  ADMISSION_UMD_COLOR_FILL_COMMAND command;
  ADMISSION_WIN32_RENDER_SNAPSHOT win32Snapshot = {0};
  const ADMISSION_OPEN_ALLOCATION *opened;
  ADMISSION_GDI_COLOR_FILL_INPUT input;
  ADMISSION_GDI_PREPARED prepared;
  APPLE_AGX_DMA_SHADOW shadow;
  D3DDDI_PATCHLOCATIONLIST *location;
  DXGK_ALLOCATIONLIST *allocation;
  ADMISSION_LOCAL_MEMORY_VIEW destination = {0};
  ADMISSION_RENDER_PACKET_DESCRIPTION prepatchedDescription = {0};
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
  ADMISSION_LOCAL_MEMORY_VIEW visibleDestination = {0};
  ULONGLONG visibleAllocationToken = 0ULL;
#endif
  ULONGLONG alignedSize;
  ULONGLONG allocationOffset = 0ULL;
  ULONGLONG allocationBytes = 0ULL;
  ULONGLONG commandHash = 0ULL;
  KIRQL oldIrql;
  BOOLEAN prepatched = FALSE;
  BOOLEAN win32Command = FALSE;
  BOOLEAN trace;
  ULONG traceDmaBytes = 0u;
  ULONG tracePatches = 0u;
  ULONG correlationSequence = 0u;
  ULONGLONG allocationTokens[2] = {0ULL, 0ULL};

#define UMD_RENDER_RETURN(guard, value)                                      \
  do {                                                                       \
    NTSTATUS renderStatus = (value);                                         \
    AdmissionRecordUmdRenderGuard(adapter, (guard), renderStatus);           \
    AdmissionUmdRenderTraceResult(adapter, trace, (guard), renderStatus,     \
                                  traceDmaBytes, tracePatches, prepatched);   \
    AdmissionRenderCorrelationExitWindows(                                  \
        adapter, correlationSequence, (guard), renderStatus,                 \
        traceDmaBytes, tracePatches, prepatched);                            \
    return renderStatus;                                                     \
  } while (0)

  adapter = AdmissionUmdRenderTraceAdapterGet();
  trace = FALSE;
  if (context == NULL ||
      context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC)
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardContext,
                      STATUS_INVALID_PARAMETER);
  if (context->Object.Device == NULL ||
      context->Object.Device->Magic != ADMISSION_OBJECT_DEVICE_MAGIC ||
      context->Object.Device->Adapter == NULL)
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardDevice,
                      STATUS_INVALID_PARAMETER);
  adapter = CONTAINING_RECORD(context->Object.Device->Adapter,
                              ADMISSION_CONTEXT, ObjectAdapter);
  AdmissionRecordUmdRenderGuard(adapter, MAXULONG, STATUS_PENDING);
  correlationSequence = AdmissionRenderCorrelationBeginWindows(
      adapter, context, Args);
  trace = AdmissionUmdRenderTraceBegin(adapter, context, Args);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  if (trace && AdmissionUmdRenderTraceAdapterGet() != NULL &&
      adapter != AdmissionUmdRenderTraceAdapterGet())
    trace = FALSE;
#endif
  if ((context->Object.Flags & ADMISSION_CONTEXT_SYSTEM) != 0u)
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardSystem,
                      STATUS_INVALID_PARAMETER);
  if (!context->SchedulerContext.Active)
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardInactive,
                      STATUS_INVALID_PARAMETER);
  if (Args == NULL)
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardArgs,
                      STATUS_INVALID_PARAMETER);
  if (Args->pCommand == NULL)
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardCommandPointer,
                      STATUS_INVALID_PARAMETER);
  if ((!context->Win32Transport && Args->CommandLength != sizeof(command)) ||
      (context->Win32Transport &&
       (Args->CommandLength < sizeof(APPLE_AGX_WIN32_COMMAND_HEADER) ||
        Args->CommandLength > APPLE_AGX_WIN32_COMMAND_MAX_BYTES)))
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardCommandLength,
                      STATUS_INVALID_PARAMETER);
  if (Args->pDmaBuffer == NULL || Args->DmaSize == 0u)
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardDma,
                      STATUS_INVALID_PARAMETER);
  if (Args->pDmaBufferPrivateData == NULL ||
      Args->DmaBufferPrivateDataSize != ADMISSION_GDI_DMA_PRIVATE_SIZE)
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardPrivate,
                      STATUS_INVALID_PARAMETER);
  if (Args->pAllocationList == NULL || Args->AllocationListSize == 0u)
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardAllocations,
                      STATUS_INVALID_PARAMETER);
  if (Args->PatchLocationListInSize != 0u)
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardPatchIn,
                      STATUS_INVALID_PARAMETER);
  if (Args->pPatchLocationListOut == NULL ||
      Args->PatchLocationListOutSize <
          ADMISSION_GDI_COLOR_FILL_PATCH_COUNT)
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardPatchOut,
                      STATUS_INVALID_PARAMETER);
  if (Args->MultipassOffset != 0u)
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardMultipass,
                      STATUS_INVALID_PARAMETER);

  if (context->Win32Transport) {
    if (!NT_SUCCESS(AdmissionWin32SnapshotRenderCommand(
            context, Args, &win32Snapshot)))
      UMD_RENDER_RETURN(AdmissionUmdRenderGuardUserCopy,
                        STATUS_INVALID_USER_BUFFER);
    RtlZeroMemory(&command, sizeof(command));
    command.Magic = ADMISSION_UMD_COMMAND_MAGIC;
    command.Version = ADMISSION_UMD_COMMAND_VERSION;
    command.Bytes = sizeof(command);
    command.Opcode = AdmissionUmdOpcodeColorFill;
    command.Destination.Left = win32Snapshot.View.Clear->Left;
    command.Destination.Top = win32Snapshot.View.Clear->Top;
    command.Destination.Right = win32Snapshot.View.Clear->Right;
    command.Destination.Bottom = win32Snapshot.View.Clear->Bottom;
    command.DestinationAllocationIndex =
        win32Snapshot.View.References[
            win32Snapshot.View.Clear->DestinationReference]
            .AllocationIndex;
    command.Color = win32Snapshot.View.Clear->Color;
    command.Rop = AdmissionUmdRopPatCopy;
    command.Rop3 = 0u;
    allocationOffset =
        win32Snapshot.View.References[
            win32Snapshot.View.Clear->DestinationReference]
            .Offset;
    allocationBytes =
        win32Snapshot.View.References[
            win32Snapshot.View.Clear->DestinationReference]
            .Bytes;
    commandHash = win32Snapshot.View.Header->ContentHash;
    win32Command = TRUE;
  } else {
    __try {
      RtlCopyMemory(&command, Args->pCommand, sizeof(command));
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
      UMD_RENDER_RETURN(AdmissionUmdRenderGuardUserCopy,
                        STATUS_INVALID_USER_BUFFER);
    }
  }
  AdmissionUmdRenderTraceCommand(adapter, trace, context, &command);

  if (!AdmissionUmdColorFillCommandValid(
          &command, Args->AllocationListSize))
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardCommand,
                      STATUS_INVALID_USER_BUFFER);
  if (!AdmissionUmdRenderOpenValid(
          context, Args->pAllocationList, Args->AllocationListSize,
          command.DestinationAllocationIndex, TRUE))
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardOpenedAllocation,
                      STATUS_INVALID_USER_BUFFER);
  opened = (const ADMISSION_OPEN_ALLOCATION *)
      Args->pAllocationList[command.DestinationAllocationIndex]
          .hDeviceSpecificAllocation;
  if (!win32Command)
    allocationBytes = opened->Allocation->Description.Size;
  if (win32Command &&
      (win32Snapshot.View.Clear->Format !=
           (ULONG)AppleAgxWin32FormatBgra8Unorm ||
       win32Snapshot.View.Clear->SurfaceWidth !=
           opened->Allocation->Description.Width ||
       win32Snapshot.View.Clear->SurfaceHeight !=
           opened->Allocation->Description.Height ||
       win32Snapshot.View.Clear->SurfacePitch !=
           opened->Allocation->Description.Pitch ||
       allocationOffset > MAXULONG || allocationBytes > MAXULONG))
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardBounds,
                      STATUS_INVALID_USER_BUFFER);
  if (command.Destination.Right >
          opened->Allocation->Description.Width ||
      command.Destination.Bottom >
          opened->Allocation->Description.Height)
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardBounds,
                      STATUS_INVALID_USER_BUFFER);

  RtlZeroMemory(&input, sizeof(input));
  input.Destination.Left = command.Destination.Left;
  input.Destination.Top = command.Destination.Top;
  input.Destination.Right = command.Destination.Right;
  input.Destination.Bottom = command.Destination.Bottom;
  input.DestinationAllocationIndex =
      command.DestinationAllocationIndex;
  input.AllocationCount = Args->AllocationListSize;
  input.DestinationWritable = 1u;
  input.Color = command.Color;
  input.DestinationPitch = opened->Allocation->Description.Pitch;
  input.Rop = (UINT)AppleAgxGdiColorFillPatCopy;
  input.Rop3 = 0u;
  if (!AdmissionGdiPrepareColorFill(
          &input, 0u, (unsigned char *)Args->pDmaBuffer,
          Args->DmaSize, &prepared))
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardPrepare,
                      STATUS_INVALID_USER_BUFFER);

  allocation = &Args->pAllocationList[
      command.DestinationAllocationIndex];
  if (allocation->SegmentId != 0u) {
    if (allocation->SegmentId != ADMISSION_MEMORY_LOCAL_SEGMENT ||
        allocation->PhysicalAddress.QuadPart <= 0 ||
        !AdmissionAllocationAlign64K(
            opened->Allocation->Description.Size, &alignedSize) ||
        !NT_SUCCESS(AdmissionMemoryRuntimeResolveLocal(
            adapter, (ULONGLONG)allocation->PhysicalAddress.QuadPart,
            alignedSize, allocationOffset, &destination)) ||
        allocationBytes > destination.Bytes ||
        prepared.Patches[0].PatchOffset >
            prepared.DmaBytes - sizeof(destination.GpuVirtualAddress))
      UMD_RENDER_RETURN(AdmissionUmdRenderGuardPrepare,
                        STATUS_INVALID_ADDRESS);
    RtlCopyMemory(
        (PUCHAR)Args->pDmaBuffer + prepared.Patches[0].PatchOffset,
        &destination.GpuVirtualAddress,
        sizeof(destination.GpuVirtualAddress));
    destination.Bytes = allocationBytes;
    prepatched = TRUE;
  }

#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
  if (prepatched && !NT_SUCCESS(AdmissionVisibleAgxResolveDestination(
          adapter, context, Args->pAllocationList,
          Args->AllocationListSize, command.DestinationAllocationIndex,
          &destination, &visibleDestination,
          &visibleAllocationToken)))
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardPrepare,
                      STATUS_INVALID_ADDRESS);
#endif

  allocationTokens[0] = (ULONGLONG)(ULONG_PTR)
      Args->pAllocationList[0].hDeviceSpecificAllocation;
  allocationTokens[1] = Args->AllocationListSize > 1u ?
      (ULONGLONG)(ULONG_PTR)
          Args->pAllocationList[1].hDeviceSpecificAllocation : 0ULL;
  AdmissionRenderCorrelationValidatedWindows(
      adapter, correlationSequence,
      commandHash != 0ULL ? commandHash :
          AdmissionUmdCommandHash(&command, sizeof(command)),
      Args->AllocationListSize, command.DestinationAllocationIndex,
      allocation->SegmentId, allocationTokens);

  /* VidMm zeroes private data only when the DMA buffer is created.
     This new Render request owns the supplied range; reconstruct its shadow
     even when the DMA buffer retains private bytes from an earlier request. */
  AppleAgxDmaShadowInitialize(
      &shadow, Args->pDmaBufferPrivateData,
      Args->DmaBufferPrivateDataSize);
  if (!AppleAgxDmaShadowAppend(
          &shadow, prepared.DmaOffset, Args->pDmaBuffer,
          prepared.DmaBytes)) {
    RtlZeroMemory(Args->pDmaBufferPrivateData,
                  Args->DmaBufferPrivateDataSize);
    UMD_RENDER_RETURN(AdmissionUmdRenderGuardShadow,
                      STATUS_INVALID_USER_BUFFER);
  }

  if (prepatched) {
    prepatchedDescription.ContextToken = (ULONGLONG)(ULONG_PTR)context;
    prepatchedDescription.AllocationToken =
        (ULONGLONG)(ULONG_PTR)opened;
    prepatchedDescription.PrivateDataToken =
        (ULONGLONG)(ULONG_PTR)Args->pDmaBufferPrivateData;
    prepatchedDescription.PrivateDataBytes = Args->DmaBufferPrivateDataSize;
    prepatchedDescription.PrivateDataStart = 0u;
    prepatchedDescription.PrivateDataEnd = shadow.BytesUsed;
    prepatchedDescription.DmaStart = prepared.DmaOffset;
    prepatchedDescription.DmaEnd = prepared.DmaOffset + prepared.DmaBytes;
    prepatchedDescription.PatchOffset = prepared.Patches[0].PatchOffset;
    prepatchedDescription.DestinationCpuToken =
        (ULONGLONG)(ULONG_PTR)destination.CpuAddress;
    prepatchedDescription.DestinationGpuVa = destination.GpuVirtualAddress;
    prepatchedDescription.DestinationPhysical = destination.HostPhysicalAddress;
    prepatchedDescription.DestinationBytes = (UINT)destination.Bytes;
    prepatchedDescription.DestinationIndex =
        command.DestinationAllocationIndex;
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
    prepatchedDescription.VisibleDestinationCpuToken =
        (ULONGLONG)(ULONG_PTR)visibleDestination.CpuAddress;
    prepatchedDescription.VisibleDestinationGpuVa =
        visibleDestination.GpuVirtualAddress;
    prepatchedDescription.VisibleDestinationPhysical =
        visibleDestination.HostPhysicalAddress;
    prepatchedDescription.VisibleDestinationAllocationToken =
        visibleAllocationToken;
    prepatchedDescription.VisibleDestinationBytes =
        (UINT)visibleDestination.Bytes;
#endif
    KeAcquireSpinLock(&adapter->SchedulerLock, &oldIrql);
    if (AdmissionPrepatchedActive(&context->PrepatchedRender) ||
        context->Object.FenceOutstanding != 0u ||
        !AdmissionPrepatchedCapture(
            &context->PrepatchedRender, &prepatchedDescription)) {
      KeReleaseSpinLock(&adapter->SchedulerLock, oldIrql);
      UMD_RENDER_RETURN(AdmissionUmdRenderGuardShadow,
                        STATUS_DEVICE_BUSY);
    }
    KeReleaseSpinLock(&adapter->SchedulerLock, oldIrql);
  }

  location = Args->pPatchLocationListOut;
  RtlZeroMemory(location, sizeof(*location));
  location->AllocationIndex = prepared.Patches[0].AllocationIndex;
  location->SlotId = prepared.Patches[0].SlotId;
  location->AllocationOffset = (UINT)allocationOffset;
  location->PatchOffset = prepared.Patches[0].PatchOffset;
  location->SplitOffset = prepared.Patches[0].SplitOffset;
  Args->pDmaBuffer = (PUCHAR)Args->pDmaBuffer + prepared.DmaBytes;
  Args->pPatchLocationListOut = location + 1;
  Args->MultipassOffset = win32Command ? Args->CommandLength :
                                         sizeof(command);
  traceDmaBytes = prepared.DmaBytes;
  tracePatches = 1u;

  AdmissionGdiReceiptBeginWindows(
      adapter, (ULONGLONG)(ULONG_PTR)context, command.Opcode,
      command.Color, 0u, prepared.DmaBytes);
  UMD_RENDER_RETURN(AdmissionUmdRenderGuardAccepted, STATUS_SUCCESS);
#undef UMD_RENDER_RETURN
}
