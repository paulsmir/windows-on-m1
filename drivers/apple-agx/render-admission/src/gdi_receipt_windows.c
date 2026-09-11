#include "render_admission.h"

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)

C_ASSERT(sizeof(ADMISSION_GDI_HW_RECEIPT) == 160u);

static VOID AdmissionGdiTraceWrite(
    _In_ ADMISSION_CONTEXT *Context, ULONG Field, ULONG Value) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  if (Context == NULL || Context->BrokerBase == NULL)
    return;
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(request, AdmissionGdiSubmitTraceWord(Field, Value));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

static VOID AdmissionGdiTraceSubmit(
    _In_ ADMISSION_CONTEXT *Context,
    _In_opt_ const DXGKARG_SUBMITCOMMAND *Args, NTSTATUS Status) {
  if (Context == NULL || Args == NULL ||
      InterlockedCompareExchange(&Context->GdiSubmitTraceClaimed, 1, 0) != 0)
    return;
  AdmissionGdiTraceWrite(Context, 1u, 1u);
  AdmissionGdiTraceWrite(Context, 2u, Args->Flags.Value);
  AdmissionGdiTraceWrite(Context, 3u, Args->SubmissionFenceId);
  AdmissionGdiTraceWrite(Context, 4u, Args->DmaBufferSubmissionStartOffset);
  AdmissionGdiTraceWrite(Context, 5u, Args->DmaBufferSubmissionEndOffset);
  AdmissionGdiTraceWrite(Context, 6u,
      Args->DmaBufferPrivateDataSubmissionStartOffset);
  AdmissionGdiTraceWrite(Context, 7u,
      Args->DmaBufferPrivateDataSubmissionEndOffset);
  AdmissionGdiTraceWrite(Context, 8u, Args->DmaBufferPrivateDataSize);
  AdmissionGdiTraceWrite(Context, 9u, (ULONG)Status);
}

static VOID AdmissionGdiWriteBinary(
    HANDLE Key, PCWSTR Name, const VOID *Data, ULONG Bytes) {
  UNICODE_STRING name;
  RtlInitUnicodeString(&name, Name);
  (void)ZwSetValueKey(Key, &name, 0, REG_BINARY, (PVOID)Data, Bytes);
}

_Use_decl_annotations_ VOID AdmissionGdiReceiptBeginWindows(
    ADMISSION_CONTEXT *Context, ULONGLONG ContextToken, ULONG Opcode,
    ULONG Color, ULONG RectCount, ULONG DmaBytes) {
  KIRQL oldIrql;
  if (Context == NULL ||
      InterlockedCompareExchange(&Context->GdiReceiptClaimed, 1, 0) != 0)
    return;
  KeAcquireSpinLock(&Context->GdiReceiptLock, &oldIrql);
  AdmissionGdiReceiptInitialize(&Context->GdiReceipt);
  if (!AdmissionGdiReceiptBegin(&Context->GdiReceipt, ContextToken, Opcode,
                                Color, RectCount, DmaBytes))
    InterlockedExchange(&Context->GdiReceiptClaimed, 0);
  KeReleaseSpinLock(&Context->GdiReceiptLock, oldIrql);
}

_Use_decl_annotations_ VOID AdmissionGdiReceiptPatchWindows(
    ADMISSION_CONTEXT *Context, ULONGLONG ContextToken, ULONG Fence,
    ULONGLONG DestinationGpuVa, ULONGLONG DestinationPhysical,
    ULONG DestinationBytes) {
  KIRQL oldIrql;
  if (Context == NULL ||
      InterlockedCompareExchange(&Context->GdiReceiptClaimed, 0, 0) == 0)
    return;
  KeAcquireSpinLock(&Context->GdiReceiptLock, &oldIrql);
  (void)AdmissionGdiReceiptPatch(&Context->GdiReceipt, ContextToken, Fence,
      DestinationGpuVa, DestinationPhysical, DestinationBytes);
  KeReleaseSpinLock(&Context->GdiReceiptLock, oldIrql);
}

_Use_decl_annotations_ VOID AdmissionGdiReceiptSubmitWindows(
    ADMISSION_CONTEXT *Context, const DXGKARG_SUBMITCOMMAND *Args,
    NTSTATUS Status) {
  KIRQL oldIrql;
  AdmissionGdiTraceSubmit(Context, Args, Status);
  if (Context == NULL || Args == NULL ||
      InterlockedCompareExchange(&Context->GdiReceiptClaimed, 0, 0) == 0)
    return;
  KeAcquireSpinLock(&Context->GdiReceiptLock, &oldIrql);
  (void)AdmissionGdiReceiptSubmit(&Context->GdiReceipt,
      (ULONGLONG)(ULONG_PTR)Args->hContext, Args->SubmissionFenceId,
      (ULONG)Status);
  KeReleaseSpinLock(&Context->GdiReceiptLock, oldIrql);
}

_Use_decl_annotations_ VOID AdmissionGdiReceiptBackendWindows(
    ADMISSION_CONTEXT *Context, ULONG Fence, ULONG Result,
    const APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  KIRQL oldIrql;
  APPLE_AGX_BACKEND_JOB_IMAGE empty;
  if (Context == NULL ||
      InterlockedCompareExchange(&Context->GdiReceiptClaimed, 0, 0) == 0)
    return;
  RtlZeroMemory(&empty, sizeof(empty));
  if (Job == NULL)
    Job = &empty;
  KeAcquireSpinLock(&Context->GdiReceiptLock, &oldIrql);
  (void)AdmissionGdiReceiptBackend(&Context->GdiReceipt, Fence, Result,
      Job->TaEvent, Job->D3Event, Job->TaExpectedStamp,
      Job->D3ExpectedStamp, Job->TaExpectedDonePointer,
      Job->D3ExpectedDonePointer);
  KeReleaseSpinLock(&Context->GdiReceiptLock, oldIrql);
}

_Use_decl_annotations_ VOID AdmissionGdiReceiptCompleteWindows(
    ADMISSION_CONTEXT *Context, ULONG Fence, ULONG Status,
    BOOLEAN NotifyInterrupt) {
  KIRQL oldIrql;
  if (Context == NULL ||
      InterlockedCompareExchange(&Context->GdiReceiptClaimed, 0, 0) == 0)
    return;
  KeAcquireSpinLock(&Context->GdiReceiptLock, &oldIrql);
  (void)AdmissionGdiReceiptComplete(&Context->GdiReceipt, Fence, Status,
                                    NotifyInterrupt ? 1u : 0u);
  KeReleaseSpinLock(&Context->GdiReceiptLock, oldIrql);
}

_Use_decl_annotations_ VOID AdmissionGdiReceiptProgressWindows(
    ADMISSION_CONTEXT *Context, ULONG Fence,
    const APPLE_AGX_G13_QUEUE_PROGRESS *Progress, ULONG WorkerFinalPhase) {
  KIRQL oldIrql;
  if (Context == NULL || Progress == NULL ||
      InterlockedCompareExchange(&Context->GdiReceiptClaimed, 0, 0) == 0)
    return;
  KeAcquireSpinLock(&Context->GdiReceiptLock, &oldIrql);
  (void)AdmissionGdiReceiptProgress(&Context->GdiReceipt, Fence,
      Progress->TaDonePointer, Progress->TaStamp,
      Progress->TaEventSeen, Progress->TaComplete,
      Progress->D3DonePointer, Progress->D3Stamp,
      Progress->D3EventSeen, Progress->D3Complete, WorkerFinalPhase);
  KeReleaseSpinLock(&Context->GdiReceiptLock, oldIrql);
}

_Use_decl_annotations_ VOID AdmissionGdiReceiptDpcWindows(
    ADMISSION_CONTEXT *Context, ULONG Fence) {
  KIRQL oldIrql;
  if (Context == NULL || Fence == 0u ||
      InterlockedCompareExchange(&Context->GdiReceiptClaimed, 0, 0) == 0)
    return;
  KeAcquireSpinLock(&Context->GdiReceiptLock, &oldIrql);
  (void)AdmissionGdiReceiptDpc(&Context->GdiReceipt, Fence);
  KeReleaseSpinLock(&Context->GdiReceiptLock, oldIrql);
}

_Use_decl_annotations_ VOID AdmissionFlushGdiReceipt(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_GDI_HW_RECEIPT snapshot;
  KIRQL oldIrql;
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL ||
      InterlockedCompareExchange(&Context->GdiReceiptClaimed, 0, 0) == 0)
    return;
  KeAcquireSpinLock(&Context->GdiReceiptLock, &oldIrql);
  snapshot = Context->GdiReceipt;
  KeReleaseSpinLock(&Context->GdiReceiptLock, oldIrql);
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    AdmissionGdiWriteBinary(key, L"Wom1GdiHardwareReceipt", &snapshot,
                            sizeof(snapshot));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    AdmissionGdiWriteBinary(key, L"Wom1GdiHardwareReceipt", &snapshot,
                            sizeof(snapshot));
    ZwClose(key);
  }
}

#endif /* APPLE_AGX_SUBMIT_QUALIFICATION */
