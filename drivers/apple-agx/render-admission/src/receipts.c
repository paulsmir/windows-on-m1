#include "render_admission.h"

#define ADMISSION_QUERY_RECEIPT_VERSION 1u
#define ADMISSION_DISPLAY_DDI_RECEIPT_VERSION 1u

C_ASSERT(sizeof(ADMISSION_SOURCE_ADDRESS_RECEIPT) == 128);
C_ASSERT(sizeof(ADMISSION_PRESENT_TRANSFER_RECEIPT) == 64);
C_ASSERT(sizeof(ADMISSION_QUEUE_INFO_RECEIPT) == 576);
C_ASSERT(sizeof(ADMISSION_QUEUE_FAULT_SNAPSHOT) == 184);
C_ASSERT(sizeof(ADMISSION_BUFFER_MANAGER_RECEIPT) == 396);
C_ASSERT(sizeof(ADMISSION_TA_PROGRESS_RECEIPT) == 920);
C_ASSERT(sizeof(ADMISSION_TA_RETIRE_RECEIPT) == 2236);
C_ASSERT(sizeof(ADMISSION_TA_TEMPORAL_SAMPLE) == 148);
C_ASSERT(sizeof(ADMISSION_TA_TEMPORAL_RECEIPT) == 312);
C_ASSERT(sizeof(ADMISSION_KTRACE_RECEIPT) == 928);
C_ASSERT(sizeof(ADMISSION_EVENT_DRAIN_RECEIPT) == 96);
C_ASSERT(sizeof(ADMISSION_TERMINAL_RECEIPT) == 368);
C_ASSERT(sizeof(ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT) == 1096);
C_ASSERT(sizeof(ADMISSION_VISIBLE_PATTERN_RECEIPT) == 112);
C_ASSERT(sizeof(ADMISSION_VISIBLE_SCANOUT_RECEIPT) == 216);
C_ASSERT(sizeof(ADMISSION_VISIBLE_AGX_RECEIPT) == 248);
C_ASSERT(sizeof(ADMISSION_PRESENT_OPEN_ENDPOINT) == 64);

typedef struct _ADMISSION_PRESENT_OPEN_FAILURE_RECEIPT {
  ULONG Version, Bytes, Status, Flags, NumSrc, NumDst, PatchListSize;
  ULONG ContextFlags, Pid, Irql;
  ULONGLONG ContextToken, DeviceToken, AllocationListToken, DmaGpuVirtualAddress;
  ADMISSION_PRESENT_OPEN_ENDPOINT Source, Destination;
  ULONG RawCopyStatus, RawBytes;
  ULONG SourceMagicCopyStatus, SourceMagicBytes, SourceMagic;
  ULONG DestinationMagicCopyStatus, DestinationMagicBytes, DestinationMagic;
  UCHAR RawList[96];
} ADMISSION_PRESENT_OPEN_FAILURE_RECEIPT;
C_ASSERT(sizeof(ADMISSION_PRESENT_OPEN_FAILURE_RECEIPT) == 328);
static volatile LONG AdmissionPresentOpenFailureClaimed;

typedef struct _ADMISSION_PRESENT_RECEIPT {
  ULONG Version, Bytes, Branch, Status, Irql, DevicePresent, ArgsPresent, Flags;
  ULONG DmaSize, DmaPrivateSize, PatchListSize, Multipass, Color, SubrectCount;
  ULONG FlipInterval, NumSrc, NumDst, DriverPrivateSize;
  ULONG DmaBufferPresent, DmaPrivatePresent, AllocationInfoPresent;
  ULONG DriverPrivatePresent, SubrectPresent, DmaSegment;
  ULONGLONG DmaPhysical, DmaGpuVirtual;
  RECT SrcRect, DstRect, FirstSubrect;
} ADMISSION_PRESENT_RECEIPT;
C_ASSERT(sizeof(ADMISSION_PRESENT_RECEIPT) == 160);
static volatile LONG AdmissionPresentReceiptClaimed;

typedef struct _ADMISSION_TYPE1_RECEIPT {
  ULONG Version;
  ULONG Bytes;
  ULONG Type;
  ULONG Status;
  ULONG OutputBytes;
  ULONG CapturedBytes;
  DXGK_DRIVERCAPS Caps;
} ADMISSION_TYPE1_RECEIPT;

typedef struct _ADMISSION_DISPLAY_CAPS_RECEIPT {
  ULONG Version;
  ULONG Bytes;
  ULONG Type;
  ULONG Status;
  ULONG OutputBytes;
  ULONG CapturedBytes;
  DXGK_DISPLAY_DRIVERCAPS_EXTENSION Caps;
} ADMISSION_DISPLAY_CAPS_RECEIPT;

typedef struct _ADMISSION_WDDM_DEVICE_CAPS_RECEIPT {
  ULONG Version;
  ULONG Bytes;
  ULONG Type;
  ULONG Status;
  ULONG OutputBytes;
  ULONG CapturedBytes;
  DXGK_WDDMDEVICECAPS Caps;
} ADMISSION_WDDM_DEVICE_CAPS_RECEIPT;

typedef struct _ADMISSION_DISPLAY_DDI_RECEIPT {
  ULONG Version;
  ULONG Bytes;
  ULONG DdiId;
  ULONG Phase;
  ULONG Status;
  ULONG Reserved;
  ULONGLONG Time;
} ADMISSION_DISPLAY_DDI_RECEIPT;

static void WriteDword(HANDLE Key, PCWSTR Name, ULONG Value) {
  UNICODE_STRING name;
  RtlInitUnicodeString(&name, Name);
  (void)ZwSetValueKey(Key, &name, 0, REG_DWORD, &Value, sizeof(Value));
}

static void WriteBinary(HANDLE Key, PCWSTR Name, const VOID *Value,
                        ULONG Bytes) {
  UNICODE_STRING name;
  RtlInitUnicodeString(&name, Name);
  (void)ZwSetValueKey(Key, &name, 0, REG_BINARY, (PVOID)Value, Bytes);
}

static void WriteQword(HANDLE Key, PCWSTR Name, ULONGLONG Value) {
  UNICODE_STRING name;
  RtlInitUnicodeString(&name,Name);
  (void)ZwSetValueKey(Key,&name,0,REG_QWORD,&Value,sizeof(Value));
}

#if ADMISSION_GPUVA_G1B_PAGE_PROFILE != 0
static volatile LONG g1bAllocationInputCount;
static volatile LONG g1bDdiFailureCount;

_Use_decl_annotations_ void AdmissionRecordG1bDdiFailure(
    PDEVICE_OBJECT DeviceObject, ULONG DdiId, NTSTATUS Status) {
  static const PCWSTR names[16] = {
      L"Wom1G1bFailure00", L"Wom1G1bFailure01", L"Wom1G1bFailure02",
      L"Wom1G1bFailure03", L"Wom1G1bFailure04", L"Wom1G1bFailure05",
      L"Wom1G1bFailure06", L"Wom1G1bFailure07", L"Wom1G1bFailure08",
      L"Wom1G1bFailure09", L"Wom1G1bFailure10", L"Wom1G1bFailure11",
      L"Wom1G1bFailure12", L"Wom1G1bFailure13", L"Wom1G1bFailure14",
      L"Wom1G1bFailure15"};
  struct { ULONG Sequence, DdiId, Status, Reserved; } receipt;
  LONG index;
  HANDLE key = NULL;
  if (NT_SUCCESS(Status) || DeviceObject == NULL)
    return;
  index = InterlockedIncrement(&g1bDdiFailureCount) - 1;
  if (index < 0 || index >= 16 ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  receipt.Sequence = (ULONG)index + 1u;
  receipt.DdiId = DdiId;
  receipt.Status = (ULONG)Status;
  receipt.Reserved = 0u;
  WriteBinary(key, names[index], &receipt, sizeof(receipt));
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordG1bAllocationInput(
    PDEVICE_OBJECT DeviceObject, USHORT MinimumPageSize,
    USHORT RecommendedPageSize) {
  static const PCWSTR names[16] = {
      L"Wom1G1bInput00", L"Wom1G1bInput01", L"Wom1G1bInput02",
      L"Wom1G1bInput03", L"Wom1G1bInput04", L"Wom1G1bInput05",
      L"Wom1G1bInput06", L"Wom1G1bInput07", L"Wom1G1bInput08",
      L"Wom1G1bInput09", L"Wom1G1bInput10", L"Wom1G1bInput11",
      L"Wom1G1bInput12", L"Wom1G1bInput13", L"Wom1G1bInput14",
      L"Wom1G1bInput15"};
  LONG index = InterlockedIncrement(&g1bAllocationInputCount) - 1;
  HANDLE key = NULL;
  if (DeviceObject == NULL || index < 0 || index >= 16 ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, names[index],
             ((ULONG)RecommendedPageSize << 16) | MinimumPageSize);
  ZwClose(key);
}
#endif

#if defined(APPLE_AGX_VISIBLE_SCANOUT_QUALIFICATION)
_Use_decl_annotations_ VOID AdmissionRecordVisibleScanout(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_VISIBLE_SCANOUT_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL ||
      Receipt->Version != ADMISSION_VISIBLE_SCANOUT_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1VisibleScanoutReceipt", Receipt, sizeof(*Receipt));
    (void)ZwFlushKey(key);
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1VisibleScanoutReceipt", Receipt, sizeof(*Receipt));
    (void)ZwFlushKey(key);
    ZwClose(key);
  }
}
#endif

#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
_Use_decl_annotations_ VOID AdmissionRecordVisibleAgx(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_VISIBLE_AGX_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL ||
      Receipt->Version != ADMISSION_VISIBLE_AGX_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1VisibleAgxReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
}
#endif

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)

_Use_decl_annotations_ VOID AdmissionRecordTerminalReceipt(
    ADMISSION_CONTEXT *Context, const ADMISSION_TERMINAL_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL ||
      Receipt->Version != ADMISSION_TERMINAL_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) ||
      !(Receipt->ValidMask & ADMISSION_TERMINAL_VALID_EXIT) ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1TerminalReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1TerminalReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordOutputTerminalSnapshot(
    ADMISSION_CONTEXT *Context, const ADMISSION_TERMINAL_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL ||
      Receipt->Version != ADMISSION_TERMINAL_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) ||
      (Receipt->ValidMask & (ADMISSION_TERMINAL_VALID_TERMINAL |
                             ADMISSION_TERMINAL_VALID_OUTPUT)) !=
          (ADMISSION_TERMINAL_VALID_TERMINAL |
           ADMISSION_TERMINAL_VALID_OUTPUT) ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1OutputTerminalSnapshot", Receipt,
                sizeof(*Receipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(
      &servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1OutputTerminalSnapshot", Receipt,
                sizeof(*Receipt));
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordDynamicOutputSnapshot(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT *Snapshot) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Snapshot == NULL ||
      Snapshot->Version != ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT_VERSION ||
      Snapshot->Bytes != sizeof(*Snapshot) || Snapshot->Valid != 1u ||
      Snapshot->Fence == 0u || Snapshot->Generation == 0u ||
      Snapshot->DataBytes != ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT_CAPACITY ||
      Snapshot->Status != 0u || Snapshot->Reserved != 0u ||
      Snapshot->ExpectedLayout != AdmissionDynamicOutputLayoutAgxTiled64 ||
      Snapshot->ExpectedForegroundColor == 0u ||
      Snapshot->VerificationValid == 0u ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1DynamicOutputSnapshot", Snapshot,
                sizeof(*Snapshot));
    ZwClose(key);
  }
  RtlInitUnicodeString(
      &servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1DynamicOutputSnapshot", Snapshot,
                sizeof(*Snapshot));
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordDynamicGraph(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_DYNAMIC_GRAPH_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL || Receipt->Version != 1u ||
      Receipt->Bytes != sizeof(*Receipt) || Receipt->Valid != 1u ||
      Receipt->Fence == 0u || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1DynamicGraphReceipt", Receipt,
                sizeof(*Receipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(
      &servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1DynamicGraphReceipt", Receipt,
                sizeof(*Receipt));
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordNativeGraph(
    ADMISSION_CONTEXT *Context, const ADMISSION_NATIVE_GRAPH_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL || Receipt->Version != 1u ||
      Receipt->Bytes != sizeof(*Receipt) || Receipt->Valid != 1u ||
      !Receipt->Fence || !Receipt->Generation || !Receipt->CommandHash ||
      Receipt->CandidateBuild != APPLE_AGX_VERSION_BUILD ||
      Receipt->BootGeneration != Context->Win32BootGeneration ||
      Receipt->ReadbackAvailable > 1u ||
      (Receipt->ReadbackAvailable && (!Receipt->SnapshotGeneration ||
          !Receipt->ReadbackFnv1a || Receipt->ReadbackBytes != Receipt->RenderTargetBytes ||
          Receipt->ReadbackBytes > sizeof(Receipt->ReadbackData))) ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL && NT_SUCCESS(IoOpenDeviceRegistryKey(
      Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1NativeGraphReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes,&servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,NULL,NULL);
  if (NT_SUCCESS(ZwOpenKey(&key,KEY_SET_VALUE,&attributes))) {
    WriteBinary(key,L"Wom1NativeGraphReceipt",Receipt,sizeof(*Receipt));
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordDynamicStore(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_DYNAMIC_STORE_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL ||
      Receipt->Version != ADMISSION_DYNAMIC_STORE_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) || Receipt->Valid != 1u ||
      Receipt->Fence == 0u || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1DynamicStoreReceipt", Receipt,
                sizeof(*Receipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(
      &servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1DynamicStoreReceipt", Receipt,
                sizeof(*Receipt));
    ZwClose(key);
  }
}

static VOID AdmissionWritePreSubmitHeartbeat(
    HANDLE Key, APPLE_AGX_RTKIT_SESSION_RESULT Result,
    const APPLE_AGX_RTKIT_SESSION *Session) {
  WriteDword(Key, L"Wom1PreSubmitHeartbeatResult", (ULONG)Result);
  WriteDword(Key, L"Wom1PreSubmitHeartbeatRxCount", Session->ReceivedCount);
  WriteDword(Key, L"Wom1PreSubmitHeartbeatRxEndpoint", Session->LastRxEndpoint);
  WriteQword(Key, L"Wom1PreSubmitHeartbeatRxPayload", Session->LastRxPayload);
}

_Use_decl_annotations_ VOID AdmissionRecordPreSubmitHeartbeat(
    ADMISSION_CONTEXT *Context, APPLE_AGX_RTKIT_SESSION_RESULT Result,
    const APPLE_AGX_RTKIT_SESSION *Session) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Session == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    AdmissionWritePreSubmitHeartbeat(key, Result, Session);
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    AdmissionWritePreSubmitHeartbeat(key, Result, Session);
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordQueueSubmission(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_QUEUE_SUBMISSION_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL ||
      Receipt->Version != ADMISSION_QUEUE_SUBMISSION_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1QueueSubmissionReceipt", Receipt,
                sizeof(*Receipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1QueueSubmissionReceipt", Receipt,
                sizeof(*Receipt));
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordQueueInfo(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_QUEUE_INFO_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL ||
      Receipt->Version != ADMISSION_QUEUE_INFO_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1QueueInfoReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1QueueInfoReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordBufferManager(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_BUFFER_MANAGER_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL ||
      Receipt->Version != ADMISSION_BUFFER_MANAGER_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1BufferManagerReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1BufferManagerReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordTaProgress(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_TA_PROGRESS_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL ||
      Receipt->Version != ADMISSION_TA_PROGRESS_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1TaProgressReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1TaProgressReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordTaRetire(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_TA_RETIRE_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL ||
      Receipt->Version != ADMISSION_TA_RETIRE_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1TaRetireReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1TaRetireReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordTaTemporal(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_TA_TEMPORAL_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL ||
      Receipt->Version != ADMISSION_TA_TEMPORAL_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) || Receipt->SampleCount == 0u ||
      Receipt->SampleCount > ADMISSION_TA_TEMPORAL_SAMPLE_COUNT ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1TaTemporalReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1TaTemporalReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordKTrace(
    ADMISSION_CONTEXT *Context, const ADMISSION_KTRACE_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL ||
      Receipt->Version != ADMISSION_KTRACE_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1KTraceReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1KTraceReceipt", Receipt, sizeof(*Receipt));
    ZwClose(key);
  }
}

static VOID AdmissionWriteEventDrain(
    HANDLE Key, const ADMISSION_EVENT_DRAIN_RECEIPT *Receipt) {
  WriteBinary(Key, L"Wom1EventDrainReceipt", Receipt, sizeof(*Receipt));
}

_Use_decl_annotations_ VOID AdmissionRecordEventDrain(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_EVENT_DRAIN_RECEIPT *Receipt) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Receipt == NULL ||
      Receipt->Version != ADMISSION_EVENT_DRAIN_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key))) {
    AdmissionWriteEventDrain(key, Receipt);
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    AdmissionWriteEventDrain(key, Receipt);
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordQueueFaultSnapshot(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_QUEUE_FAULT_SNAPSHOT *Snapshot) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Snapshot == NULL ||
      Snapshot->Version != ADMISSION_QUEUE_FAULT_SNAPSHOT_VERSION ||
      Snapshot->Bytes != sizeof(*Snapshot) ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1QueueFaultSnapshot", Snapshot,
                sizeof(*Snapshot));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1QueueFaultSnapshot", Snapshot,
                sizeof(*Snapshot));
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordComputeIdentityDiagnostic(
    ADMISSION_CONTEXT *Context,
    const APPLE_AGX_G13_COMPUTE_IDENTITY_DIAGNOSTIC *Diagnostic) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Context == NULL || Diagnostic == NULL ||
      (!Diagnostic->Build.Valid && !Diagnostic->RunTa.Valid) ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1ComputeIdentityDiagnostic", Diagnostic,
                sizeof(*Diagnostic));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1ComputeIdentityDiagnostic", Diagnostic,
                sizeof(*Diagnostic));
    ZwClose(key);
  }
}


#endif


/* At most 32 process-owned values per driver lifetime. A process never evicts
 * another process: DWM cannot overwrite the one-shot client's receipt. Multiple
 * calls in one process retain only its last observation, not a complete trace. */
_Use_decl_annotations_ VOID AdmissionRecordUmdRenderGuard(
    ADMISSION_CONTEXT *Context, const ULONG *Snapshot,
    ULONG Guard, NTSTATUS Status) {
  static volatile LONG processIds[32];
  HANDLE key = NULL;
  ULONG receipt[16], slot;
  WCHAR valueName[40];
  UNICODE_STRING servicePath;
  OBJECT_ATTRIBUTES attributes;
  if (Snapshot == NULL || Snapshot[3] == 0u ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  for (slot = 0u; slot < ARRAYSIZE(processIds); ++slot) {
    LONG owner = InterlockedCompareExchange(&processIds[slot],
        (LONG)Snapshot[3], 0);
    if (owner == 0 || (ULONG)owner == Snapshot[3]) break;
  }
  if (slot == ARRAYSIZE(processIds)) return;
  RtlCopyMemory(receipt, Snapshot, sizeof(receipt));
  receipt[5] = Guard;
  receipt[6] = (ULONG)Status;
  if (!NT_SUCCESS(RtlStringCchPrintfW(valueName, ARRAYSIZE(valueName),
                                     L"Wom1UmdRenderSlot%02u", slot)))
    return;
  if (Context != NULL && Context->PhysicalDeviceObject != NULL &&
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key)))
    key = NULL;
  if (key != NULL) {
    WriteBinary(key, valueName, receipt, sizeof(receipt));
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
    WriteDword(key, L"Wom1UmdRenderGuard", Guard);
    WriteDword(key, L"Wom1UmdRenderStatus", (ULONG)Status);
#endif
    ZwClose(key);
  }
  key = NULL;
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (!NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) return;
  WriteBinary(key, valueName, receipt, sizeof(receipt));
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  WriteDword(key, L"Wom1UmdRenderGuard", Guard);
  WriteDword(key, L"Wom1UmdRenderStatus", (ULONG)Status);
#endif
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordContext0Inventory(
    ADMISSION_CONTEXT *Context, ULONG Stage, ULONG Result,
    const APPLE_AGX_CONTEXT0_BROKER *Journal) {
  HANDLE key=NULL; LARGE_INTEGER time;
  if(!Context || !Journal || !Context->PhysicalDeviceObject ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE,KEY_SET_VALUE,&key))) return;
  KeQuerySystemTimePrecise(&time);
  WriteDword(key,L"Wom1Context0Stage",Stage);
  WriteDword(key,L"Wom1Context0Result",Result);
  WriteDword(key,L"Wom1Context0Mapped",Journal->Mapped);
  WriteDword(key,L"Wom1Context0Verified",Journal->Verified);
  WriteDword(key,L"Wom1Context0Retired",Journal->Retired);
  WriteDword(key,L"Wom1Context0Absent",Journal->Absent);
  WriteDword(key,L"Wom1Context0LiveCount",Journal->Count);
  WriteDword(key,L"Wom1Context0Uncertain",Journal->Uncertain);
  WriteDword(key,L"Wom1Context0FailedRange",Journal->FailedRange);
  WriteDword(key,L"Wom1Context0LastOperation",Journal->LastOperation);
  WriteQword(key,L"Wom1Context0Root",Journal->Root);
  WriteQword(key,L"Wom1Context0Epoch",Journal->Epoch);
  if(Stage==1) {
    WriteQword(key,L"Wom1Context0MapTime",time.QuadPart);
    WriteDword(key,L"Wom1Context0MapResult",Result);
    WriteDword(key,L"Wom1Context0LeafRecordBytes",sizeof(Journal->Leaves[0]));
    if(Journal->Count<=APPLE_AGX_CONTEXT0_MAX_LEAVES)
      WriteBinary(key,L"Wom1Context0MapInventory",Journal->Leaves,
          Journal->Count*sizeof(Journal->Leaves[0]));
    WriteBinary(key,L"Wom1Context0MapReply",&Journal->LastResponse,sizeof(Journal->LastResponse));
  } else if(Stage==2) {
    WriteQword(key,L"Wom1Context0ManagementTime",time.QuadPart);
    WriteDword(key,L"Wom1Context0ManagementVerifyResult",Result);
    WriteBinary(key,L"Wom1Context0ManagementReply",&Journal->LastResponse,sizeof(Journal->LastResponse));
  } else if(Stage==3) {
    WriteQword(key,L"Wom1Context0RetireTime",time.QuadPart);
    WriteDword(key,L"Wom1Context0RetireResult",Result);
  }
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordFirmwareIo(
    ADMISSION_CONTEXT *Context,ULONG Result,const AGX_FW_IO_MANIFEST *Manifest) {
  HANDLE key=NULL; LARGE_INTEGER time;
  if(!Context || !Manifest || !Context->PhysicalDeviceObject ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE,KEY_SET_VALUE,&key))) return;
  KeQuerySystemTimePrecise(&time);
  WriteDword(key,L"Wom1FirmwareIoResult",Result);
  WriteQword(key,L"Wom1FirmwareIoTime",time.QuadPart);
  WriteBinary(key,L"Wom1FirmwareIoManifest",Manifest,sizeof(*Manifest));
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordHwdataProfile(
    ADMISSION_CONTEXT *Context,ULONG Result,const AGX_HWDATA_RECEIPT *Receipt) {
  HANDLE key=NULL; LARGE_INTEGER time;
  if(!Context || !Receipt || !Context->PhysicalDeviceObject ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE,KEY_SET_VALUE,&key))) return;
  KeQuerySystemTimePrecise(&time);
  WriteDword(key,L"Wom1HwdataProfileResult",Result);
  WriteQword(key,L"Wom1HwdataProfileTime",time.QuadPart);
  WriteBinary(key,L"Wom1HwdataProfileReceipt",Receipt,sizeof(*Receipt));
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordFirmwareQualification(
    ADMISSION_CONTEXT *Context,ULONG StartResult,ULONG StartReturn,ULONG CompletedMask,ULONG CleanupResult) {
  HANDLE key=NULL;
  if(!Context || !Context->PhysicalDeviceObject ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE,KEY_SET_VALUE,&key))) return;
  WriteDword(key,L"Wom1FirmwareQualificationStartResult",StartResult);
  WriteDword(key,L"Wom1FirmwareQualificationStartReturn",StartReturn);
  WriteDword(key,L"Wom1FirmwareQualificationCompletedMask",CompletedMask);
  WriteDword(key,L"Wom1FirmwareQualificationCleanupResult",CleanupResult);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordBackendQualification(
    ADMISSION_CONTEXT *Context,ULONG Stage,ULONG Result,ULONG Phase,ULONG Flags,
    ULONGLONG ArenaGpu,ULONG ArenaBytes) {
  HANDLE key=NULL;LARGE_INTEGER time;
  if(!Context || !Context->PhysicalDeviceObject ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE,KEY_SET_VALUE,&key))) return;
  KeQuerySystemTimePrecise(&time);
  if(Stage==1) {
    WriteDword(key,L"Wom1BackendQualificationResult",Result);
    WriteDword(key,L"Wom1BackendQualificationPhase",Phase);
    WriteDword(key,L"Wom1BackendQualificationFlags",Flags);
    WriteQword(key,L"Wom1BackendQualificationArenaGpu",ArenaGpu);
    WriteDword(key,L"Wom1BackendQualificationArenaBytes",ArenaBytes);
    WriteQword(key,L"Wom1BackendQualificationTime",time.QuadPart);
  } else if(Stage==2) {
    WriteDword(key,L"Wom1BackendQualificationCleanupStatus",Result);
    WriteDword(key,L"Wom1BackendQualificationFinalPhase",Phase);
    WriteDword(key,L"Wom1BackendQualificationFinalFlags",Flags);
    WriteQword(key,L"Wom1BackendQualificationCleanupTime",time.QuadPart);
  }
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordDeviceControl(
    ADMISSION_CONTEXT *Context,ULONG Idle,ULONG Result,ULONG ReadPointer,
    ULONG WritePointer,ULONG Expected) {
  HANDLE key=NULL; LARGE_INTEGER time;
  if(!Context || !Context->PhysicalDeviceObject ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE,KEY_SET_VALUE,&key))) return;
  KeQuerySystemTimePrecise(&time);
  WriteDword(key,Idle?L"Wom1DcIdleResult":L"Wom1DcInitResult",Result);
  WriteDword(key,Idle?L"Wom1DcIdleRead":L"Wom1DcInitRead",ReadPointer);
  WriteDword(key,Idle?L"Wom1DcIdleWrite":L"Wom1DcInitWrite",WritePointer);
  WriteDword(key,Idle?L"Wom1DcIdleExpected":L"Wom1DcInitExpected",Expected);
  WriteQword(key,Idle?L"Wom1DcIdleTime":L"Wom1DcInitTime",time.QuadPart);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordEndpoint(
    ADMISSION_CONTEXT *Context, ULONG Endpoint, ULONG Success) {
  HANDLE key=NULL; LARGE_INTEGER time;
  if(!Context || !Context->PhysicalDeviceObject ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE,KEY_SET_VALUE,&key))) return;
  KeQuerySystemTimePrecise(&time);
  if(Endpoint==0x20) {
    WriteDword(key,L"Wom1Endpoint20Started",Success);
    WriteQword(key,L"Wom1Endpoint20Time",time.QuadPart);
  } else if(Endpoint==0x21) {
    WriteDword(key,L"Wom1Endpoint21Started",Success);
    WriteQword(key,L"Wom1Endpoint21Time",time.QuadPart);
  } else if(!Endpoint) {
    WriteDword(key,L"Wom1EndpointQualificationStop",Success);
    WriteQword(key,L"Wom1EndpointStopTime",time.QuadPart);
  }
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordService(
    PUNICODE_STRING RegistryPath, PCWSTR Name, ULONG Value) {
  OBJECT_ATTRIBUTES attributes;
  HANDLE key = NULL;
  InitializeObjectAttributes(&attributes, RegistryPath,
                             OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL,
                             NULL);
  if (!NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes)))
    return;
  WriteDword(key, Name, Value);
  (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordDevice(
    PDEVICE_OBJECT DeviceObject, ADMISSION_RECEIPT Receipt, NTSTATUS Status) {
  HANDLE key = NULL;
  if (DeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1CleanReceipt", (ULONG)Receipt);
  WriteDword(key, L"Wom1CleanStatus", (ULONG)Status);
  if (Receipt == AdmissionReceiptAddEntered ||
      Receipt == AdmissionReceiptStartEntered)
    (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordStartStage(
    ADMISSION_CONTEXT *Context, ADMISSION_START_STAGE Stage,
    NTSTATUS Status) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1StartStage", (ULONG)Stage);
  WriteDword(key, L"Wom1StartStatus", (ULONG)Status);
  if (Stage == AdmissionStartEntered)
    (void)ZwFlushKey(key);
  ZwClose(key);
}

typedef struct _ADMISSION_RESOURCE_ENTRY {
  ULONG Type, ShareDisposition, Flags, Length;
  ULONGLONG Address;
  ULONG Vector, Reserved;
} ADMISSION_RESOURCE_ENTRY;

typedef struct _ADMISSION_RESOURCE_LIST_RECEIPT {
  ULONG Version, Bytes, FullCount, PartialCount, CapturedCount, Truncated;
  ADMISSION_RESOURCE_ENTRY Entries[32];
} ADMISSION_RESOURCE_LIST_RECEIPT;

static void AdmissionFillTranslatedResources(
    PCM_RESOURCE_LIST Resources, ADMISSION_RESOURCE_LIST_RECEIPT *Receipt) {
  ULONG full_index;
  RtlZeroMemory(Receipt, sizeof(*Receipt));
  Receipt->Version = 1u;
  Receipt->Bytes = sizeof(*Receipt);
  if (Resources == NULL)
    return;
  Receipt->FullCount = Resources->Count;
  for (full_index = 0u; full_index < Resources->Count; ++full_index) {
    PCM_FULL_RESOURCE_DESCRIPTOR full = &Resources->List[full_index];
    ULONG partial_index;
    for (partial_index = 0u;
         partial_index < full->PartialResourceList.Count; ++partial_index) {
      PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor =
          &full->PartialResourceList.PartialDescriptors[partial_index];
      ADMISSION_RESOURCE_ENTRY *entry;
      ++Receipt->PartialCount;
      if (Receipt->CapturedCount == RTL_NUMBER_OF(Receipt->Entries)) {
        Receipt->Truncated = 1u;
        continue;
      }
      entry = &Receipt->Entries[Receipt->CapturedCount++];
      entry->Type = descriptor->Type;
      entry->ShareDisposition = descriptor->ShareDisposition;
      entry->Flags = descriptor->Flags;
      if (descriptor->Type == CmResourceTypeMemory) {
        entry->Address = (ULONGLONG)descriptor->u.Memory.Start.QuadPart;
        entry->Length = descriptor->u.Memory.Length;
      } else if (descriptor->Type == CmResourceTypeInterrupt) {
        entry->Vector = descriptor->u.Interrupt.Vector;
      }
    }
  }
}

_Use_decl_annotations_ void AdmissionRecordTranslatedResources(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_RESOURCE_LIST_RECEIPT receipt;
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL)
    return;
  AdmissionFillTranslatedResources(
      Context->DeviceInformation.TranslatedResourceList, &receipt);
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1TranslatedResources", &receipt, sizeof(receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

typedef struct _ADMISSION_POST_DISPLAY_RECEIPT {
  ULONG Version, Bytes, AcquireStatus, Route, DecisionStatus;
  ULONG Width, Height, Pitch, ColorFormat, TargetId, AcpiId;
  ULONGLONG PhysicalAddress;
} ADMISSION_POST_DISPLAY_RECEIPT;

_Use_decl_annotations_ void AdmissionRecordPostDisplay(
    ADMISSION_CONTEXT *Context, NTSTATUS AcquireStatus,
    APPLE_AGX_POST_DISPLAY_ROUTE Route, NTSTATUS DecisionStatus) {
  ADMISSION_POST_DISPLAY_RECEIPT receipt;
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL)
    return;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = 1u;
  receipt.Bytes = sizeof(receipt);
  receipt.AcquireStatus = (ULONG)AcquireStatus;
  receipt.Route = (ULONG)Route;
  receipt.DecisionStatus = (ULONG)DecisionStatus;
  receipt.Width = Context->PostDisplayInformation.Width;
  receipt.Height = Context->PostDisplayInformation.Height;
  receipt.Pitch = Context->PostDisplayInformation.Pitch;
  receipt.ColorFormat = (ULONG)Context->PostDisplayInformation.ColorFormat;
  receipt.TargetId = Context->PostDisplayInformation.TargetId;
  receipt.AcpiId = Context->PostDisplayInformation.AcpiId;
  receipt.PhysicalAddress =
      (ULONGLONG)Context->PostDisplayInformation.PhysicAddress.QuadPart;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1PostDisplayRoute", &receipt, sizeof(receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordMemoryStartFailure(
    ADMISSION_CONTEXT *Context, ULONG Substage, NTSTATUS Status,
    ULONGLONG RequestedBytes, ULONG OperationResult,
    LONG OutstandingAllocations, ULONG PhysicalStep,
    NTSTATUS PhysicalStatus, ULONGLONG PhysicalBytes) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1MemoryStartStage", Substage);
  WriteDword(key, L"Wom1MemoryStartStatus", (ULONG)Status);
  WriteQword(key, L"Wom1MemoryRequestedBytes", RequestedBytes);
  WriteDword(key, L"Wom1MemoryOperationResult", OperationResult);
  WriteDword(key, L"Wom1MemoryOutstandingAllocations",
             (ULONG)OutstandingAllocations);
  WriteDword(key, L"Wom1PhysicalAllocateStep", PhysicalStep);
  WriteDword(key, L"Wom1PhysicalAllocateStatus", (ULONG)PhysicalStatus);
  WriteQword(key, L"Wom1PhysicalAllocateBytes", PhysicalBytes);
  ZwClose(key);
}

_Use_decl_annotations_ NTSTATUS AdmissionRecordLocalReserve(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_PHYSICAL_ALLOCATION *Allocation) {
  struct {
    ULONG Version;
    ULONG Bytes;
    ULONG Owner;
    ULONG Status;
    ULONGLONG GuestIpa;
    ULONGLONG HostPa;
    ULONGLONG LocalBytes;
  } receipt;
  UNICODE_STRING name;
  HANDLE key = NULL;
  NTSTATUS status;

  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      Allocation == NULL || !Allocation->BorrowedFirmwareReserve ||
      Allocation->CpuBase == NULL)
    return STATUS_INVALID_PARAMETER;
  receipt.Version = 1u;
  receipt.Bytes = sizeof(receipt);
  receipt.Owner = 1u; /* Firmware-reserved pages, KMD-borrowed CPU view. */
  receipt.Status = STATUS_SUCCESS;
  receipt.GuestIpa = Allocation->GuestIpaBase;
  receipt.HostPa = Allocation->HostPhysicalBase;
  receipt.LocalBytes = Allocation->Size;
  status = IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
                                   PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE,
                                   &key);
  if (!NT_SUCCESS(status))
    return status;
  RtlInitUnicodeString(&name, L"Wom1LocalReserve");
  status = ZwSetValueKey(key, &name, 0, REG_BINARY, &receipt,
                         sizeof(receipt));
  if (NT_SUCCESS(status))
    status = ZwFlushKey(key);
  ZwClose(key);
  return status;
}

_Use_decl_annotations_ void AdmissionRecordMemoryStop(
    ADMISSION_CONTEXT *Context, NTSTATUS Status,
    LONG OutstandingBefore, LONG OutstandingAfter) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1MemoryStopStatus", (ULONG)Status);
  WriteDword(key, L"Wom1MemoryStopOutstandingBefore",
             (ULONG)OutstandingBefore);
  WriteDword(key, L"Wom1MemoryStopOutstandingAfter",
             (ULONG)OutstandingAfter);
  ZwClose(key);
}

#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
_Use_decl_annotations_ void AdmissionRecordB1Retirement(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_B1_RETIREMENT_RECEIPT *Receipt) {
  static const PCWSTR names[2u * AdmissionB1RetireStepCount] = {
      L"Wom1B1Retire00", L"Wom1B1Retire01", L"Wom1B1Retire02",
      L"Wom1B1Retire03", L"Wom1B1Retire04", L"Wom1B1Retire05",
      L"Wom1B1Retire06", L"Wom1B1Retire07", L"Wom1B1Retire08",
      L"Wom1B1Retire09", L"Wom1B1Retire10", L"Wom1B1Retire11",
      L"Wom1B1Retire12", L"Wom1B1Retire13", L"Wom1B1Retire14",
      L"Wom1B1Retire15", L"Wom1B1Retire16", L"Wom1B1Retire17"};
  HANDLE key = NULL;
  ULONG index;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      Receipt == NULL || Receipt->Version != 1u ||
      Receipt->Bytes != sizeof(*Receipt) || Receipt->Owner >= 2u ||
      Receipt->Step >= AdmissionB1RetireStepCount ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  index = Receipt->Owner * AdmissionB1RetireStepCount + Receipt->Step;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, names[index], Receipt, sizeof(*Receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordB1Cleanup(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_B1_CLEANUP_RECEIPT *Receipt) {
  HANDLE key = NULL;
  WCHAR name[32];
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      Receipt == NULL || Receipt->Version != 1u ||
      Receipt->Bytes != sizeof(*Receipt) || Receipt->Step > 15u ||
      KeGetCurrentIrql() != PASSIVE_LEVEL ||
      !NT_SUCCESS(RtlStringCchPrintfW(name, RTL_NUMBER_OF(name),
                                      L"Wom1B1Cleanup%02u", Receipt->Step)) ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, name, Receipt, sizeof(*Receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordB1Context0Hash(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_B1_CONTEXT0_HASH_RECEIPT *Receipt) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      Receipt == NULL || Receipt->Version != 1u ||
      Receipt->Bytes != sizeof(*Receipt) || Receipt->Phase > 1u ||
      KeGetCurrentIrql() != PASSIVE_LEVEL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, Receipt->Phase == 0u ? L"Wom1B1Context0HashBefore" :
      L"Wom1B1Context0HashAfter", Receipt, sizeof(*Receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordB1Qualification(
    ADMISSION_CONTEXT *Context, ULONG Stage, NTSTATUS Status,
    NTSTATUS FirstFailure, ULONG Precheck, ULONG ProbeStatus,
    ULONGLONG ProbeEpoch,
    ULONG CompletedJobs, ULONG BrokerStatus, ULONG CleanupStatus,
    ULONG OutputPixelA, ULONG OutputPixelB) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1B1Stage", Stage);
  WriteDword(key, L"Wom1B1Status", (ULONG)Status);
  WriteDword(key, L"Wom1B1FirstFailureStatus", (ULONG)FirstFailure);
  WriteDword(key, L"Wom1B1Precheck", Precheck);
  WriteDword(key, L"Wom1B1ProbeStatus", ProbeStatus);
  WriteQword(key, L"Wom1B1ProbeEpoch", ProbeEpoch);
  WriteDword(key, L"Wom1B1CompletedJobs", CompletedJobs);
  WriteDword(key, L"Wom1B1BrokerStatus", BrokerStatus);
  WriteDword(key, L"Wom1B1CleanupStatus", CleanupStatus);
  WriteDword(key, L"Wom1B1OutputPixelA", OutputPixelA);
  WriteDword(key, L"Wom1B1OutputPixelB", OutputPixelB);
  (void)ZwFlushKey(key);
  ZwClose(key);
}
#endif

_Use_decl_annotations_ void AdmissionRecordPlatformStage(
    ADMISSION_CONTEXT *Context, ADMISSION_PLATFORM_STAGE Stage,
    NTSTATUS Status) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1PlatformStage", (ULONG)Stage);
  WriteDword(key, L"Wom1PlatformStatus", (ULONG)Status);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordBackendStartResult(
    ADMISSION_CONTEXT *Context, APPLE_AGX_BACKEND_RUNTIME_RESULT Result) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1BackendStartResult", (ULONG)Result);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordProviderBootstrap(
    ADMISSION_CONTEXT *Context, ULONG Phase, UCHAR Success, ULONG State) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
                                          PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1ProviderBootPhase", Phase);
  WriteDword(key, L"Wom1ProviderBootSucceeded", Success);
  WriteDword(key, L"Wom1ProviderBootOwnedState", State);
  if (Phase == APPLE_AGX_PROVIDER_BOOT_CLEANUP)
    WriteDword(key, L"Wom1ProviderBootCleanupSucceeded", Success);
  else if (!Success)
    WriteDword(key, L"Wom1ProviderBootFailurePhase", Phase);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordFirmwarePhase(
    ADMISSION_CONTEXT *Context, APPLE_AGX_FIRMWARE_PHASE Phase,
    APPLE_AGX_FIRMWARE_RESULT Result, ULONG CompletedMask) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1FirmwarePhase", (ULONG)Phase);
  WriteDword(key, L"Wom1FirmwareResult", (ULONG)Result);
  WriteDword(key, L"Wom1FirmwareCompletedMask", CompletedMask);
  if (Phase == AppleAgxFirmwareFailed) {
    WriteDword(key, L"Wom1FirmwareFailureResult", (ULONG)Result);
    WriteDword(key, L"Wom1FirmwareFailureMask", CompletedMask);
  }
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordFirmwarePowerOn(
    ADMISSION_CONTEXT *Context, BOOLEAN Acquired, ULONG State, ULONG Result,
    ULONGLONG ReceiptSequence) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1FirmwarePowerAcquire", Acquired ? 1u : 0u);
  WriteDword(key, L"Wom1FirmwarePowerState", State);
  WriteDword(key, L"Wom1FirmwarePowerResult", Result);
  {
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"Wom1FirmwarePowerReceiptSequence");
    (void)ZwSetValueKey(key, &name, 0, REG_QWORD, &ReceiptSequence,
                        sizeof(ReceiptSequence));
  }
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordRetainedTrace(
    ADMISSION_CONTEXT *Context, const VOID *Data, ULONG Bytes) {
  HANDLE key = NULL;
  if (!Context || !Data || Bytes > 1024 || !Context->PhysicalDeviceObject ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE,KEY_SET_VALUE,&key))) return;
  WriteBinary(key,L"Wom1RetainedManagementTrace",Data,Bytes);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordRetainedRoot(
    ADMISSION_CONTEXT *Context, ULONG Operation, const AGX_RR_RESPONSE *Response) {
  static const PCWSTR names[] = {L"Wom1RetainedInvalid", L"Wom1RetainedPrepare",
      L"Wom1RetainedActivate",L"Wom1RetainedMap",L"Wom1RetainedUnmap",
      L"Wom1RetainedQuery",L"Wom1RetainedClose"};
  HANDLE key = NULL;
  if (!Context || !Response || Operation >= RTL_NUMBER_OF(names) ||
      !Context->PhysicalDeviceObject ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE,KEY_SET_VALUE,&key))) return;
  WriteDword(key,L"Wom1RetainedLastOperation",Operation);
  WriteDword(key,L"Wom1RetainedLastStatus",Response->Status);
  WriteBinary(key,names[Operation],Response,sizeof(*Response));
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordFirmwarePrefix(
    ADMISSION_CONTEXT *Context, ULONG Stage, const AGX_FW_PREFIX *Prefix,
    const ULONGLONG *Imported) {
  HANDLE key = NULL;
  if (!Context || !Context->PhysicalDeviceObject ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1FirmwarePrefixStage", Stage);
  if (Prefix)
    WriteBinary(key, L"Wom1FirmwarePrefixLive", Prefix, sizeof(*Prefix));
  if (Imported)
    WriteBinary(key, L"Wom1FirmwarePrefixImported", Imported, 16);
  if (Stage == 4)
    WriteDword(key, L"Wom1ManagementQualificationStop", 1);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordRtkitBoot(
    ADMISSION_CONTEXT *Context, APPLE_AGX_RTKIT_SESSION_RESULT Result,
    const APPLE_AGX_RTKIT_SESSION *Session) {
  HANDLE key = NULL;
  if (Context == NULL || Session == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
                                          PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1RtkitBootResult", (ULONG)Result);
  WriteDword(key, L"Wom1RtkitBootPhase", (ULONG)Session->Boot.Phase);
  WriteDword(key, L"Wom1RtkitCpuReady", (ULONG)Session->CpuReady);
  WriteDword(key, L"Wom1RtkitBootBegun", (ULONG)Session->Boot.Begun);
  WriteDword(key, L"Wom1RtkitHelloSeen", (ULONG)Session->Boot.HelloSeen);
  WriteDword(key, L"Wom1RtkitEndpointMap", (ULONG)Session->Boot.EndpointMapComplete);
  WriteDword(key, L"Wom1RtkitIopPower", (ULONG)Session->Boot.IopPowerReady);
  WriteDword(key, L"Wom1RtkitApPower", (ULONG)Session->Boot.ApPowerReady);
  WriteDword(key, L"Wom1RtkitInboxControl", Session->InboxControlAtFailure);
  WriteDword(key, L"Wom1RtkitOutboxControl", Session->OutboxControlAtFailure);
  WriteDword(key, L"Wom1RtkitReceivedCount", Session->ReceivedCount);
  WriteDword(key, L"Wom1RtkitLastRxEndpoint", Session->LastRxEndpoint);
  WriteDword(key, L"Wom1RtkitLastRxPayloadLow", (ULONG)Session->LastRxPayload);
  WriteDword(key, L"Wom1RtkitLastRxPayloadHigh",
              (ULONG)(Session->LastRxPayload >> 32));
  WriteDword(key, L"Wom1RtkitEndpointMap0", Session->Boot.EndpointMap[0]);
  WriteDword(key, L"Wom1RtkitEndpointMap1", Session->Boot.EndpointMap[1]);
  WriteDword(key, L"Wom1RtkitCrashlogRequestedBytes", Session->CrashlogRequestedBytes);
  WriteDword(key, L"Wom1RtkitCrashlogCapacityBytes", Session->CrashlogCapacityBytes);
  WriteDword(key, L"Wom1RtkitCrashlogReplySent", Session->CrashlogReplySent);
  WriteDword(key, L"Wom1RtkitCrashlogCrashed", Session->CrashlogCrashed);
  WriteDword(key, L"Wom1RtkitCrashlogGpuVaLow", (ULONG)Session->CrashlogGpuAddress);
  WriteDword(key, L"Wom1RtkitCrashlogGpuVaHigh", (ULONG)(Session->CrashlogGpuAddress >> 32));
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordRtkitCrashlog(
    ADMISSION_CONTEXT *Context, const VOID *Data, ULONG Bytes) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL || Data == NULL ||
      Bytes != APPLE_AGX_RTKIT_CRASHLOG_BYTES ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
                                          PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1RtkitCrashlogImage", Data, Bytes);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordPreManagementUat(
    ADMISSION_CONTEXT *Context, BOOLEAN Published,
    const APPLE_AGX_UAT_PUBLICATION_STATE *State) {
  HANDLE key = NULL;
  if (Context == NULL || State == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
                                          PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1PreManagementUatPublished", Published ? 1u : 0u);
  WriteDword(key, L"Wom1PreManagementUatActive", State->Active);
  WriteDword(key, L"Wom1PreManagementUatTtbr0Low", (ULONG)State->PublishedTtbr0);
  WriteDword(key, L"Wom1PreManagementUatTtbr0High", (ULONG)(State->PublishedTtbr0 >> 32));
  WriteDword(key, L"Wom1PreManagementUatTtbr1Low", (ULONG)State->PublishedTtbr1);
  WriteDword(key, L"Wom1PreManagementUatTtbr1High", (ULONG)(State->PublishedTtbr1 >> 32));
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordQuery(
    PDEVICE_OBJECT DeviceObject, DXGK_QUERYADAPTERINFOTYPE Type,
    ULONG OutputDataSize, NTSTATUS Status, const VOID *OutputData) {
  HANDLE key = NULL;
  if (DeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1CleanReceipt", (ULONG)AdmissionReceiptQueryAdapterInfo);
  WriteDword(key, L"Wom1CleanQueryType", (ULONG)Type);
  WriteDword(key, L"Wom1CleanQuerySize", OutputDataSize);
  WriteDword(key, L"Wom1CleanStatus", (ULONG)Status);
#if ADMISSION_GPUVA_G1B_PAGE_PROFILE != 0
  if ((ULONG)Type >= 39u && (ULONG)Type <= 46u) {
    static const PCWSTR statusNames[8] = {
        L"Wom1G1bQ39", L"Wom1G1bQ40", L"Wom1G1bQ41",
        L"Wom1G1bQ42", L"Wom1G1bQ43", L"Wom1G1bQ44",
        L"Wom1G1bQ45", L"Wom1G1bQ46"};
    WriteDword(key, statusNames[(ULONG)Type - 39u], (ULONG)Status);
    if (Type == DXGKQAITYPE_QUERYSEGMENT5 && NT_SUCCESS(Status) &&
        OutputData != NULL && OutputDataSize >= sizeof(DXGK_QUERYSEGMENTOUT5)) {
      const DXGK_QUERYSEGMENTOUT5 *segments =
          (const DXGK_QUERYSEGMENTOUT5 *)OutputData;
      if (segments->SegmentDescriptors != NULL)
        WriteDword(key, L"Wom1G1bLocalSlab",
                   (ULONG)segments->SegmentDescriptors[1].SlabSize);
    }
  }
#endif
  if (Type == DXGKQAITYPE_DRIVERCAPS) {
    ADMISSION_TYPE1_RECEIPT receipt;
    RtlZeroMemory(&receipt, sizeof(receipt));
    receipt.Version = ADMISSION_QUERY_RECEIPT_VERSION;
    receipt.Bytes = sizeof(receipt);
    receipt.Type = (ULONG)Type;
    receipt.Status = (ULONG)Status;
    receipt.OutputBytes = OutputDataSize;
    if (NT_SUCCESS(Status) && OutputData != NULL &&
        OutputDataSize >= sizeof(receipt.Caps)) {
      RtlCopyMemory(&receipt.Caps, OutputData, sizeof(receipt.Caps));
      receipt.CapturedBytes = sizeof(receipt.Caps);
    }
    WriteBinary(key, L"Wom1Type1Receipt", &receipt, sizeof(receipt));
  } else if (Type == DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION) {
    ADMISSION_DISPLAY_CAPS_RECEIPT receipt;
    RtlZeroMemory(&receipt, sizeof(receipt));
    receipt.Version = ADMISSION_QUERY_RECEIPT_VERSION;
    receipt.Bytes = sizeof(receipt);
    receipt.Type = (ULONG)Type;
    receipt.Status = (ULONG)Status;
    receipt.OutputBytes = OutputDataSize;
    if (NT_SUCCESS(Status) && OutputData != NULL &&
        OutputDataSize >= sizeof(receipt.Caps)) {
      RtlCopyMemory(&receipt.Caps, OutputData, sizeof(receipt.Caps));
      receipt.CapturedBytes = sizeof(receipt.Caps);
    }
    WriteBinary(key, L"Wom1DisplayCapsReceipt", &receipt, sizeof(receipt));
  } else if (Type == DXGKQAITYPE_WDDMDEVICECAPS) {
    ADMISSION_WDDM_DEVICE_CAPS_RECEIPT receipt;
    RtlZeroMemory(&receipt, sizeof(receipt));
    receipt.Version = ADMISSION_QUERY_RECEIPT_VERSION;
    receipt.Bytes = sizeof(receipt);
    receipt.Type = (ULONG)Type;
    receipt.Status = (ULONG)Status;
    receipt.OutputBytes = OutputDataSize;
    if (NT_SUCCESS(Status) && OutputData != NULL &&
        OutputDataSize >= sizeof(receipt.Caps)) {
      RtlCopyMemory(&receipt.Caps, OutputData, sizeof(receipt.Caps));
      receipt.CapturedBytes = sizeof(receipt.Caps);
    }
    WriteBinary(key, L"Wom1WddmDeviceCapsReceipt", &receipt,
                sizeof(receipt));
  }
  ZwClose(key);
}

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
typedef struct _ADMISSION_G3_DMA_CONTEXT_RECEIPT {
  ULONG Version, Bytes, Flags, NodeOrdinal, EngineAffinity;
  ULONG DmaBufferSize, DmaBufferSegmentSet, DmaPrivateBytes;
  ULONG AllocationListSize, PatchListSize;
  ULONGLONG SystemTime;
} ADMISSION_G3_DMA_CONTEXT_RECEIPT;

typedef struct _ADMISSION_G3_DMA_ALLOCATION_RECEIPT {
  ULONG Version, Bytes, Sequence, Kind, Status, Count;
  ULONG Flags, PrivateBytes, FirstPrivateBytes;
  ULONG InputPresent, OutputPresent, Reserved;
  ULONGLONG FirstSize, SystemTime;
} ADMISSION_G3_DMA_ALLOCATION_RECEIPT;

static volatile LONG g3DmaPoolArmed;
static volatile LONG g3DmaPoolSequence;

_Use_decl_annotations_ void AdmissionRecordGpuvaG3DmaContext(
    PDEVICE_OBJECT DeviceObject, const DXGKARG_CREATECONTEXT *Args) {
  ADMISSION_G3_DMA_CONTEXT_RECEIPT receipt;
  LARGE_INTEGER now;
  HANDLE key = NULL;
  if (DeviceObject == NULL || Args == NULL || !Args->Flags.GdiContext ||
      Args->hContext == NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = 1u;
  receipt.Bytes = sizeof(receipt);
  receipt.Flags = Args->Flags.Value;
  receipt.NodeOrdinal = Args->NodeOrdinal;
  receipt.EngineAffinity = Args->EngineAffinity;
  receipt.DmaBufferSize = Args->ContextInfo.DmaBufferSize;
  receipt.DmaBufferSegmentSet = Args->ContextInfo.DmaBufferSegmentSet;
  receipt.DmaPrivateBytes = Args->ContextInfo.DmaBufferPrivateDataSize;
  receipt.AllocationListSize = Args->ContextInfo.AllocationListSize;
  receipt.PatchListSize = Args->ContextInfo.PatchLocationListSize;
  KeQuerySystemTime(&now);
  receipt.SystemTime = (ULONGLONG)now.QuadPart;
  InterlockedExchange(&g3DmaPoolSequence, 0);
  InterlockedExchange(&g3DmaPoolArmed, 1);
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1G3DmaContext", &receipt, sizeof(receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

static void AdmissionRecordGpuvaG3DmaAllocation(
    PDEVICE_OBJECT DeviceObject, ULONG Kind, ULONG Count, ULONG Flags,
    ULONG PrivateBytes, ULONG FirstPrivateBytes, ULONG InputPresent,
    ULONG OutputPresent, ULONGLONG FirstSize, NTSTATUS Status) {
  static const PCWSTR names[16] = {
      L"Wom1G3DmaOp00", L"Wom1G3DmaOp01", L"Wom1G3DmaOp02",
      L"Wom1G3DmaOp03", L"Wom1G3DmaOp04", L"Wom1G3DmaOp05",
      L"Wom1G3DmaOp06", L"Wom1G3DmaOp07", L"Wom1G3DmaOp08",
      L"Wom1G3DmaOp09", L"Wom1G3DmaOp10", L"Wom1G3DmaOp11",
      L"Wom1G3DmaOp12", L"Wom1G3DmaOp13", L"Wom1G3DmaOp14",
      L"Wom1G3DmaOp15"};
  ADMISSION_G3_DMA_ALLOCATION_RECEIPT receipt;
  LARGE_INTEGER now;
  LONG sequence;
  HANDLE key = NULL;
  if (DeviceObject == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL ||
      InterlockedCompareExchange(&g3DmaPoolArmed, 0, 0) == 0)
    return;
  sequence = InterlockedIncrement(&g3DmaPoolSequence);
  if (sequence <= 0 || sequence > 512)
    return;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = 1u;
  receipt.Bytes = sizeof(receipt);
  receipt.Sequence = (ULONG)sequence;
  receipt.Kind = Kind;
  receipt.Status = (ULONG)Status;
  receipt.Count = Count;
  receipt.Flags = Flags;
  receipt.PrivateBytes = PrivateBytes;
  receipt.FirstPrivateBytes = FirstPrivateBytes;
  receipt.InputPresent = InputPresent;
  receipt.OutputPresent = OutputPresent;
  receipt.FirstSize = FirstSize;
  KeQuerySystemTime(&now);
  receipt.SystemTime = (ULONGLONG)now.QuadPart;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, names[(sequence - 1) & 15], &receipt, sizeof(receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordGpuvaG3DmaCreate(
    PDEVICE_OBJECT DeviceObject, const DXGKARG_CREATEALLOCATION *Args,
    NTSTATUS Status) {
  const DXGK_ALLOCATIONINFO *first =
      Args != NULL && Args->NumAllocations != 0u &&
      Args->pAllocationInfo != NULL ? Args->pAllocationInfo : NULL;
  AdmissionRecordGpuvaG3DmaAllocation(DeviceObject, 2u,
      Args == NULL ? 0u : Args->NumAllocations, 0u,
      Args == NULL ? 0u : Args->PrivateDriverDataSize,
      first == NULL ? 0u : first->PrivateDriverDataSize,
      first != NULL && first->pPrivateDriverData != NULL,
      first != NULL && first->hAllocation != NULL,
      first == NULL ? 0ULL : (ULONGLONG)first->Size, Status);
}

_Use_decl_annotations_ void AdmissionRecordGpuvaG3DmaOpen(
    PDEVICE_OBJECT DeviceObject, const DXGKARG_OPENALLOCATION *Args,
    NTSTATUS Status) {
  const DXGK_OPENALLOCATIONINFO *first =
      Args != NULL && Args->NumAllocations != 0u &&
      Args->pOpenAllocation != NULL ? Args->pOpenAllocation : NULL;
  AdmissionRecordGpuvaG3DmaAllocation(DeviceObject, 3u,
      Args == NULL ? 0u : Args->NumAllocations,
      Args == NULL ? 0u : Args->Flags.Value,
      Args == NULL ? 0u : Args->PrivateDriverSize,
      first == NULL ? 0u : first->PrivateDriverDataSize,
      first != NULL && first->hAllocation != 0u,
      first != NULL && first->hDeviceSpecificAllocation != NULL,
      0ULL, Status);
}

typedef struct _ADMISSION_G3_QUERY_RECEIPT {
  ULONG Version, Bytes, Type, Status;
  ULONG InputBytes, OutputBytes, PhysicalAdapterIndex, LevelIndex;
  ULONG Values[8];
} ADMISSION_G3_QUERY_RECEIPT;

_Use_decl_annotations_ void AdmissionRecordGpuvaG3Query(
    PDEVICE_OBJECT DeviceObject, const DXGKARG_QUERYADAPTERINFO *Query,
    NTSTATUS Status, UINT MmuCount) {
  ADMISSION_G3_QUERY_RECEIPT receipt;
  PCWSTR name;
  HANDLE key = NULL;
  if (DeviceObject == NULL || Query == NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL) return;
  if (Query->Type == DXGKQAITYPE_GPUMMUCAPS) {
    const DXGK_QUERYGPUMMUCAPSIN *input;
    const DXGK_GPUMMUCAPS *caps;
    name = L"Wom1G3Q13";
    RtlZeroMemory(&receipt, sizeof(receipt));
    if (Query->pInputData != NULL &&
        Query->InputDataSize >= sizeof(*input)) {
      input = (const DXGK_QUERYGPUMMUCAPSIN *)Query->pInputData;
      receipt.PhysicalAdapterIndex = input->PhysicalAdapterIndex;
    }
    if (NT_SUCCESS(Status) && Query->pOutputData != NULL &&
        Query->OutputDataSize >= sizeof(*caps)) {
      caps = (const DXGK_GPUMMUCAPS *)Query->pOutputData;
      receipt.Values[0] = caps->ReadOnlyMemorySupported;
      receipt.Values[1] = caps->ExplicitPageTableInvalidation;
      receipt.Values[2] = caps->PageTableUpdateRequireAddressSpaceIdle;
      receipt.Values[3] = (ULONG)caps->PageTableUpdateMode;
      receipt.Values[4] = caps->VirtualAddressBitCount;
      receipt.Values[5] = caps->PageTableLevelCount;
      receipt.Values[6] = caps->LeafPageTableSizeFor64KPagesInBytes;
      receipt.Values[7] = caps->DualPteSupported;
    }
  } else if (Query->Type == DXGKQAITYPE_PAGETABLELEVELDESC) {
    const DXGK_QUERYPAGETABLELEVELDESCIN *input;
    const DXGK_PAGE_TABLE_LEVEL_DESC *level;
    RtlZeroMemory(&receipt, sizeof(receipt));
    receipt.LevelIndex = MAXULONG;
    if (Query->pInputData != NULL &&
        Query->InputDataSize >= sizeof(*input)) {
      input = (const DXGK_QUERYPAGETABLELEVELDESCIN *)Query->pInputData;
      receipt.PhysicalAdapterIndex = input->PhysicalAdapterIndex;
      receipt.LevelIndex = input->LevelIndex;
    }
    name = receipt.LevelIndex == 0u ? L"Wom1G3Q14L0" :
           receipt.LevelIndex == 1u ? L"Wom1G3Q14L1" :
           receipt.LevelIndex == 2u ? L"Wom1G3Q14L2" : L"Wom1G3Q14Other";
    if (NT_SUCCESS(Status) && Query->pOutputData != NULL &&
        Query->OutputDataSize >= sizeof(*level)) {
      level = (const DXGK_PAGE_TABLE_LEVEL_DESC *)Query->pOutputData;
      receipt.Values[0] = level->PageTableIndexBitCount;
      receipt.Values[1] = level->PageTableSegmentId;
      receipt.Values[2] = level->PagingProcessPageTableSegmentId;
      receipt.Values[3] = level->PageTableSizeInBytes;
      receipt.Values[4] = level->PageTableAlignmentInBytes;
    }
  } else if (Query->Type == DXGKQAITYPE_QUERYMMUCOUNT) {
    const DXGK_QUERYMMUCOUNTIN *input;
    const DXGK_QUERYMMUCOUNTOUT *output;
    name = L"Wom1G3Q45";
    RtlZeroMemory(&receipt, sizeof(receipt));
    if (Query->pInputData != NULL &&
        Query->InputDataSize >= sizeof(*input)) {
      input = (const DXGK_QUERYMMUCOUNTIN *)Query->pInputData;
      receipt.PhysicalAdapterIndex = input->PhysicalAdapterIndex;
    }
    if (NT_SUCCESS(Status) && Query->pOutputData != NULL &&
        Query->OutputDataSize >= sizeof(*output)) {
      output = (const DXGK_QUERYMMUCOUNTOUT *)Query->pOutputData;
      receipt.Values[0] = output->MmuCount;
    }
  } else if (Query->Type == DXGKQAITYPE_QUERYMMUS) {
    const DXGK_QUERYMMUSIN *input;
    const DXGK_QUERYMMUSOUT *output;
    name = L"Wom1G3Q46";
    RtlZeroMemory(&receipt, sizeof(receipt));
    if (Query->pInputData != NULL &&
        Query->InputDataSize >= sizeof(*input)) {
      input = (const DXGK_QUERYMMUSIN *)Query->pInputData;
      receipt.PhysicalAdapterIndex = input->PhysicalAdapterIndex;
    }
    if (NT_SUCCESS(Status) && Query->pOutputData != NULL &&
        Query->OutputDataSize >= sizeof(*output)) {
      ULONGLONG size;
      output = (const DXGK_QUERYMMUSOUT *)Query->pOutputData;
      receipt.Values[0] = output->DisplayMmuId;
      receipt.Values[4] = output->MmuDescriptors != NULL;
      if (MmuCount != 0u && output->MmuDescriptors != NULL) {
        size = output->MmuDescriptors[0].Size;
        receipt.Values[1] = (ULONG)size;
        receipt.Values[2] = (ULONG)(size >> 32);
        receipt.Values[3] = output->MmuDescriptors[0].Flags.Value;
      }
    }
  } else return;
  receipt.Version = 1u;
  receipt.Bytes = sizeof(receipt);
  receipt.Type = (ULONG)Query->Type;
  receipt.Status = (ULONG)Status;
  receipt.InputBytes = Query->InputDataSize;
  receipt.OutputBytes = Query->OutputDataSize;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject,
      PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) return;
  WriteBinary(key, name, &receipt, sizeof(receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

typedef struct _ADMISSION_G3_NODE_RECEIPT {
  ULONG Version, Bytes, NodeOrdinal, Status;
  ULONG EngineType, GpuMmuSupported, IoMmuSupported, Flags;
} ADMISSION_G3_NODE_RECEIPT;

_Use_decl_annotations_ void AdmissionRecordGpuvaG3Node(
    PDEVICE_OBJECT DeviceObject, UINT NodeOrdinal,
    const DXGKARG_GETNODEMETADATA *Metadata, NTSTATUS Status) {
  ADMISSION_G3_NODE_RECEIPT receipt;
  HANDLE key = NULL;
  if (DeviceObject == NULL || Metadata == NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL) return;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = 1u;
  receipt.Bytes = sizeof(receipt);
  receipt.NodeOrdinal = NodeOrdinal;
  receipt.Status = (ULONG)Status;
  if (NT_SUCCESS(Status)) {
    receipt.EngineType = Metadata->EngineType;
    receipt.GpuMmuSupported = Metadata->GpuMmuSupported;
    receipt.IoMmuSupported = Metadata->IoMmuSupported;
    receipt.Flags = Metadata->Flags.Value;
  }
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject,
      PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) return;
  WriteBinary(key, L"Wom1G3Node0", &receipt, sizeof(receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

typedef struct _ADMISSION_G3_CREATE_INPUT_RECEIPT {
  ULONG Version, Bytes, Flags, NumPasid;
  ULONG PasidArrayPresent, DxgkProcessPresent, AdapterStarted, Irql;
} ADMISSION_G3_CREATE_INPUT_RECEIPT;

_Use_decl_annotations_ void AdmissionRecordGpuvaG3CreateInput(
    PDEVICE_OBJECT DeviceObject, const DXGKARG_CREATEPROCESS *Args,
    BOOLEAN AdapterStarted, KIRQL CurrentIrql) {
  ADMISSION_G3_CREATE_INPUT_RECEIPT receipt;
  HANDLE key = NULL;
  if (DeviceObject == NULL || CurrentIrql != PASSIVE_LEVEL)
    return;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = 1u;
  receipt.Bytes = sizeof(receipt);
  receipt.AdapterStarted = AdapterStarted ? 1u : 0u;
  receipt.Irql = (ULONG)CurrentIrql;
  if (Args != NULL) {
    receipt.Flags = Args->Flags.Value;
    receipt.NumPasid = Args->NumPasid;
    receipt.PasidArrayPresent = Args->pPasid != NULL;
    receipt.DxgkProcessPresent = Args->hDxgkProcess != NULL;
  }
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1G3CreateInput", &receipt, sizeof(receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

typedef struct _ADMISSION_G3_CONTEXT_INPUT_RECEIPT {
  ULONG Version, Bytes, Flags, NodeOrdinal;
  ULONG EngineAffinity, PrivateDriverDataSize, RuntimeHandlePresent, Irql;
} ADMISSION_G3_CONTEXT_INPUT_RECEIPT;

typedef struct _ADMISSION_G3_DEVICE_INPUT_RECEIPT {
  ULONG Version, Bytes, Flags, Pasid;
  ULONG ProcessPresent, RuntimeHandlePresent, Irql;
} ADMISSION_G3_DEVICE_INPUT_RECEIPT;

_Use_decl_annotations_ void AdmissionRecordGpuvaG3DeviceInput(
    PDEVICE_OBJECT DeviceObject, const DXGKARG_CREATEDEVICE *Args,
    KIRQL CurrentIrql) {
  ADMISSION_G3_DEVICE_INPUT_RECEIPT receipt;
  HANDLE key = NULL;
  if (DeviceObject == NULL || Args == NULL || CurrentIrql != PASSIVE_LEVEL)
    return;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = 1u;
  receipt.Bytes = sizeof(receipt);
  receipt.Flags = Args->Flags.Value;
  receipt.Pasid = Args->Pasid;
  receipt.ProcessPresent = Args->hKmdProcess != NULL;
  receipt.RuntimeHandlePresent = Args->hDevice != NULL;
  receipt.Irql = (ULONG)CurrentIrql;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key))) return;
  WriteBinary(key, L"Wom1G3DeviceInput", &receipt, sizeof(receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordGpuvaG3ContextInput(
    PDEVICE_OBJECT DeviceObject, const DXGKARG_CREATECONTEXT *Args,
    KIRQL CurrentIrql) {
  ADMISSION_G3_CONTEXT_INPUT_RECEIPT receipt;
  HANDLE key = NULL;
  if (DeviceObject == NULL || Args == NULL || CurrentIrql != PASSIVE_LEVEL)
    return;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = 1u;
  receipt.Bytes = sizeof(receipt);
  receipt.Flags = Args->Flags.Value;
  receipt.NodeOrdinal = Args->NodeOrdinal;
  receipt.EngineAffinity = Args->EngineAffinity;
  receipt.PrivateDriverDataSize = Args->PrivateDriverDataSize;
  receipt.RuntimeHandlePresent = Args->hContext != NULL;
  receipt.Irql = (ULONG)CurrentIrql;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1G3ContextInput", &receipt, sizeof(receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

typedef struct _ADMISSION_G3_PAGING_INPUT_RECEIPT {
  ULONG Version, Bytes, Operation, Irql;
  ULONG DmaSize, PrivateSize, DmaPresent, PrivatePresent;
  ULONG UpdateMode, Level, StartIndex, Count;
  ULONG Flags, Reserved0, PtePresent, Pte64Present;
  ULONG ProcessPresent, AllocationPresent;
  ULONGLONG PageTableAddress, FirstPteVirtualAddress;
  ULONGLONG DriverProtection;
} ADMISSION_G3_PAGING_INPUT_RECEIPT;

_Use_decl_annotations_ void AdmissionRecordGpuvaG3PagingInput(
    ADMISSION_CONTEXT *Context, const DXGKARG_BUILDPAGINGBUFFER *Args,
    ULONG CurrentIrql) {
  ADMISSION_G3_PAGING_INPUT_RECEIPT receipt;
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      Args == NULL || CurrentIrql != PASSIVE_LEVEL ||
      Args->Operation != DXGK_OPERATION_UPDATE_PAGE_TABLE)
    return;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = 2u;
  receipt.Bytes = sizeof(receipt);
  receipt.Operation = (ULONG)Args->Operation;
  receipt.Irql = CurrentIrql;
  receipt.DmaSize = Args->DmaSize;
  receipt.PrivateSize = Args->DmaBufferPrivateDataSize;
  receipt.DmaPresent = Args->pDmaBuffer != NULL;
  receipt.PrivatePresent = Args->pDmaBufferPrivateData != NULL;
  receipt.UpdateMode = (ULONG)Args->UpdatePageTable.UpdateMode;
  receipt.Level = Args->UpdatePageTable.PageTableLevel;
  receipt.StartIndex = Args->UpdatePageTable.StartIndex;
  receipt.Count = Args->UpdatePageTable.NumPageTableEntries;
  C_ASSERT(sizeof(Args->UpdatePageTable.Flags) == sizeof(receipt.Flags));
  RtlCopyMemory(&receipt.Flags, &Args->UpdatePageTable.Flags,
                sizeof(receipt.Flags));
  receipt.Reserved0 = Args->UpdatePageTable.Reserved0;
  receipt.PtePresent = Args->UpdatePageTable.pPageTableEntries != NULL;
  receipt.Pte64Present = Args->UpdatePageTable.pPageTableEntries64KB != NULL;
  receipt.ProcessPresent = Args->UpdatePageTable.hProcess != NULL;
  receipt.AllocationPresent = Args->UpdatePageTable.hAllocation != NULL;
  receipt.PageTableAddress =
      (ULONGLONG)(ULONG_PTR)Args->UpdatePageTable.PageTableAddress.CpuVirtual;
  receipt.FirstPteVirtualAddress =
      Args->UpdatePageTable.FirstPteVirtualAddress;
  receipt.DriverProtection = Args->UpdatePageTable.DriverProtection;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1G3PagingInput", &receipt, sizeof(receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

typedef struct _ADMISSION_G3_WORK_INPUT_RECEIPT {
  ULONG Version, Bytes, Operation, Irql;
  ULONG DmaSize, PrivateSize, MultipassOffset, DmaBufferWriteOffset;
  ULONG Direction, Flags, Pattern, Reserved;
  ULONGLONG SystemContext, Allocation, AllocationOffset;
  ULONGLONG SourceVa, DestinationVa, WorkBytes;
  ULONGLONG SourcePageTable, DestinationPageTable;
} ADMISSION_G3_WORK_INPUT_RECEIPT;

_Use_decl_annotations_ void AdmissionRecordGpuvaG3WorkInput(
    ADMISSION_CONTEXT *Context, const DXGKARG_BUILDPAGINGBUFFER *Args,
    ULONG CurrentIrql) {
  ADMISSION_G3_WORK_INPUT_RECEIPT receipt;
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      Args == NULL || CurrentIrql != PASSIVE_LEVEL ||
      (Args->Operation != DXGK_OPERATION_VIRTUAL_FILL &&
       Args->Operation != DXGK_OPERATION_VIRTUAL_TRANSFER &&
       Args->Operation != DXGK_OPERATION_SIGNAL_MONITORED_FENCE))
    return;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = 1u;
  receipt.Bytes = sizeof(receipt);
  receipt.Operation = (ULONG)Args->Operation;
  receipt.Irql = CurrentIrql;
  receipt.DmaSize = Args->DmaSize;
  receipt.PrivateSize = Args->DmaBufferPrivateDataSize;
  receipt.MultipassOffset = Args->MultipassOffset;
  receipt.DmaBufferWriteOffset = Args->DmaBufferWriteOffset;
  receipt.SystemContext = (ULONGLONG)(ULONG_PTR)Args->hSystemContext;
  if (Args->Operation == DXGK_OPERATION_VIRTUAL_FILL) {
    receipt.Allocation = (ULONGLONG)(ULONG_PTR)Args->FillVirtual.hAllocation;
    receipt.AllocationOffset = Args->FillVirtual.AllocationOffsetInBytes;
    receipt.DestinationVa = Args->FillVirtual.DestinationVirtualAddress;
    receipt.WorkBytes = Args->FillVirtual.FillSizeInBytes;
    receipt.Pattern = Args->FillVirtual.FillPattern;
  } else if (Args->Operation == DXGK_OPERATION_VIRTUAL_TRANSFER) {
    receipt.Allocation =
        (ULONGLONG)(ULONG_PTR)Args->TransferVirtual.hAllocation;
    receipt.AllocationOffset =
        Args->TransferVirtual.AllocationOffsetInBytes;
    receipt.SourceVa = Args->TransferVirtual.SourceVirtualAddress;
    receipt.DestinationVa =
        Args->TransferVirtual.DestinationVirtualAddress;
    receipt.WorkBytes = Args->TransferVirtual.TransferSizeInBytes;
    receipt.SourcePageTable = Args->TransferVirtual.SourcePageTable;
    receipt.DestinationPageTable =
        Args->TransferVirtual.DestinationPageTable;
    receipt.Direction = (ULONG)Args->TransferVirtual.TransferDirection;
    receipt.Flags = Args->TransferVirtual.Flags.Flags;
  } else {
    receipt.DestinationVa =
        Args->SignalMonitoredFence.MonitoredFenceGpuVa;
    receipt.WorkBytes = sizeof(Args->SignalMonitoredFence.MonitoredFenceValue);
  }
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1G3WorkInput", &receipt, sizeof(receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordGpuvaG3PagingResult(
    ADMISSION_CONTEXT *Context, NTSTATUS Status) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1G3PagingStatus", (ULONG)Status);
  (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordGpuvaG3Flush(
    ADMISSION_CONTEXT *Context, const ADMISSION_G3_FLUSH_RECEIPT *Receipt) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      Receipt == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1G3FlushInput", Receipt, sizeof(*Receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordGpuvaG3PagingFailure(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_G3_PAGING_FAILURE *Failure) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      Failure == NULL || Failure->Branch == 0u ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1G3PagingFailure", Failure, sizeof(*Failure));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

/* One 16-byte receipt per adapter instance; never overwrite the first cause. */
_Use_decl_annotations_ void AdmissionRecordG3CopyQueryFailure(
    ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  /* Only the successful first-claim owner calls this, after unlocking and
   * releasing the allocation reference. The completed snapshot is immutable. */
  if (Context == NULL || Context->G3CopyQueryFailureClaim != 2 ||
      Context->PhysicalDeviceObject == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) return;
  WriteBinary(key, L"Wom1G3CopyQueryFailure", &Context->G3CopyQueryFailure,
      sizeof(Context->G3CopyQueryFailure));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordG3PrivateFailure(
    ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL ||
      InterlockedCompareExchange(&Context->G3PrivateFailureClaim,0,0) != 2)
    return;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE,KEY_SET_VALUE,&key))) return;
  WriteBinary(key,L"Wom1G3PrivateAcquireFailure",&Context->G3PrivateFailure,
      sizeof(Context->G3PrivateFailure));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordG4SubmitFailure(
    ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  struct _ADMISSION_G4_SUBMIT_FAILURE snapshot;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL ||
      InterlockedCompareExchange(&Context->G4SubmitFailureClaim, 0, 0) != 2)
    return;
  snapshot = Context->G4SubmitFailure;
  if (snapshot.Version != 2u || snapshot.Bytes != sizeof(snapshot)) return;
  snapshot.TotalFailures = (ULONG)InterlockedCompareExchange(
      &Context->G4SubmitFailureCount, 0, 0);
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1G4SubmitFailure", &snapshot, sizeof(snapshot));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordGpuvaG3UnpublishedGroups(
    ADMISSION_CONTEXT *Context, const ULONGLONG *Counts) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      Counts == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1G3UnpublishedGroups", Counts,
              32u * sizeof(*Counts));
  (void)ZwFlushKey(key);
  ZwClose(key);
}
#endif

_Use_decl_annotations_ void AdmissionRecordPresentTransfer(
    ADMISSION_CONTEXT *Context, UINT Fence, const VOID *Command, UINT Bytes,
    ULONGLONG BytesCopied, NTSTATUS Status) {
  ADMISSION_PRESENT_BLT_COMMAND command;
  ADMISSION_PRESENT_TRANSFER_RECEIPT *receipt;
  if (!AdmissionPresentBltValidate(Command, Bytes, 1, &command) ||
      InterlockedCompareExchange(&Context->PresentTransferState, 1, 0) != 0)
    return;
  receipt = &Context->PresentTransferReceipt;
  RtlZeroMemory(receipt, sizeof(*receipt));
  receipt->Version = 1u;
  receipt->Bytes = sizeof(*receipt);
  receipt->Fence = Fence;
  receipt->Status = (ULONG)Status;
  receipt->BytesCopied = BytesCopied;
  receipt->SourceLocation = command.SourceLocation;
  receipt->DestinationLocation = command.DestinationLocation;
  receipt->ContextToken = command.ContextToken;
  receipt->Width = command.DestinationRect.Right - command.DestinationRect.Left;
  receipt->Height = command.DestinationRect.Bottom - command.DestinationRect.Top;
  InterlockedExchange(&Context->PresentTransferState, 2);
}

_Use_decl_annotations_ void AdmissionFlushPresentTransfer(ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
      InterlockedCompareExchange(&Context->PresentTransferState, 5, 4) != 4)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1PresentTransferReceipt", &Context->PresentTransferReceipt,
                sizeof(Context->PresentTransferReceipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1PresentTransferReceipt", &Context->PresentTransferReceipt,
                sizeof(Context->PresentTransferReceipt));
    ZwClose(key);
  }
}

_Use_decl_annotations_ void AdmissionRecordPresent(
    ADMISSION_DEVICE *Device, const DXGKARG_PRESENT *Present,
    ULONG Branch, NTSTATUS Status) {
  ADMISSION_PRESENT_RECEIPT receipt;
  ADMISSION_CONTEXT *context = NULL;
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (KeGetCurrentIrql() != PASSIVE_LEVEL || NT_SUCCESS(Status) ||
      InterlockedCompareExchange(&AdmissionPresentReceiptClaimed, 1, 0) != 0)
    return;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = 1u;
  receipt.Bytes = sizeof(receipt);
  receipt.Branch = Branch;
  receipt.Status = (ULONG)Status;
  receipt.Irql = KeGetCurrentIrql();
  receipt.DevicePresent = Device != NULL;
  receipt.ArgsPresent = Present != NULL;
  if (Present != NULL) {
    receipt.Flags = Present->Flags.Value;
    receipt.DmaSize = Present->DmaSize;
    receipt.DmaPrivateSize = Present->DmaBufferPrivateDataSize;
    receipt.PatchListSize = Present->PatchLocationListOutSize;
    receipt.Multipass = Present->MultipassOffset;
    receipt.Color = Present->Color;
    receipt.SubrectCount = Present->SubRectCnt;
    receipt.FlipInterval = Present->FlipInterval;
    receipt.NumSrc = Present->NumSrcAllocations;
    receipt.NumDst = Present->NumDstAllocations;
    receipt.DriverPrivateSize = Present->PrivateDriverDataSize;
    receipt.DmaBufferPresent = Present->pDmaBuffer != NULL;
    receipt.DmaPrivatePresent = Present->pDmaBufferPrivateData != NULL;
    receipt.AllocationInfoPresent = Present->pAllocationInfo != NULL;
    receipt.DriverPrivatePresent = Present->pPrivateDriverData != NULL;
    receipt.SubrectPresent = Present->pDstSubRects != NULL;
    receipt.DmaSegment = Present->DmaBufferSegmentId;
    receipt.DmaPhysical = Present->DmaBufferPhysicalAddress.QuadPart;
    receipt.DmaGpuVirtual = Present->DmaBufferGpuVirtualAddress;
    receipt.SrcRect = Present->SrcRect;
    receipt.DstRect = Present->DstRect;
    if (Present->pDstSubRects != NULL && Present->SubRectCnt != 0u)
      receipt.FirstSubrect = Present->pDstSubRects[0];
  }
  /* The receipt never follows the reserved allocation-info union or alters
   * command/output buffers. Device has already been resolved by the DDI. */
  if (Device != NULL && Device->Object.Adapter != NULL &&
      Device->Object.Adapter->Magic == ADMISSION_OBJECT_ADAPTER_MAGIC)
    context = CONTAINING_RECORD(Device->Object.Adapter,
                                ADMISSION_CONTEXT, ObjectAdapter);
  if (context != NULL && context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1PresentReceipt", &receipt, sizeof(receipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1PresentReceipt", &receipt, sizeof(receipt));
    ZwClose(key);
  }
}

_Use_decl_annotations_ void AdmissionRecordPresentOpenFailure(
    ADMISSION_DEVICE *Device, HANDLE Context, const DXGKARG_PRESENT *Present,
    const ADMISSION_PRESENT_OPEN_ENDPOINT *Source,
    const ADMISSION_PRESENT_OPEN_ENDPOINT *Destination) {
  ADMISSION_PRESENT_OPEN_FAILURE_RECEIPT receipt;
  ADMISSION_CONTEXT *adapter;
  MM_COPY_ADDRESS copySource;
  SIZE_T copied;
  ULONGLONG candidate;
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (Device == NULL || Present == NULL || Source == NULL ||
      Destination == NULL || Device->Object.Adapter == NULL ||
      Device->Object.Adapter->Magic != ADMISSION_OBJECT_ADAPTER_MAGIC ||
      KeGetCurrentIrql() != PASSIVE_LEVEL ||
      InterlockedCompareExchange(&AdmissionPresentOpenFailureClaimed, 1, 0) != 0)
    return;
  adapter = CONTAINING_RECORD(Device->Object.Adapter,
                             ADMISSION_CONTEXT, ObjectAdapter);
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = 2u;
  receipt.Bytes = sizeof(receipt);
  receipt.Status = (ULONG)STATUS_INVALID_HANDLE;
  receipt.Flags = Present->Flags.Value;
  receipt.NumSrc = Present->NumSrcAllocations;
  receipt.NumDst = Present->NumDstAllocations;
  receipt.PatchListSize = Present->PatchLocationListOutSize;
  receipt.ContextToken = (ULONGLONG)(ULONG_PTR)Context;
  receipt.DeviceToken = (ULONGLONG)(ULONG_PTR)Device;
  receipt.AllocationListToken =
      (ULONGLONG)(ULONG_PTR)Present->pAllocationList;
  receipt.DmaGpuVirtualAddress = Present->DmaBufferGpuVirtualAddress;
  receipt.Pid = HandleToULong(PsGetCurrentProcessId());
  receipt.Irql = KeGetCurrentIrql();
  if (Context != NULL &&
      ((ADMISSION_RENDER_CONTEXT *)Context)->Object.Magic ==
          ADMISSION_OBJECT_CONTEXT_MAGIC)
    receipt.ContextFlags =
        ((ADMISSION_RENDER_CONTEXT *)Context)->Object.Flags;
  receipt.Source = *Source;
  receipt.Destination = *Destination;
  if (Present->pAllocationList != NULL) {
    RtlZeroMemory(&copySource, sizeof(copySource));
    copySource.VirtualAddress = Present->pAllocationList;
    copied = 0u;
    receipt.RawCopyStatus = (ULONG)MmCopyMemory(receipt.RawList, copySource,
        sizeof(receipt.RawList), MM_COPY_MEMORY_VIRTUAL, &copied);
    receipt.RawBytes = (ULONG)copied;
    if (copied >= 72u) {
      RtlCopyMemory(&candidate, receipt.RawList + 32u, sizeof(candidate));
      if (candidate != 0ULL) {
        copySource.VirtualAddress = (PVOID)(ULONG_PTR)candidate;
        copied = 0u;
        receipt.SourceMagicCopyStatus = (ULONG)MmCopyMemory(
            &receipt.SourceMagic, copySource, sizeof(receipt.SourceMagic),
            MM_COPY_MEMORY_VIRTUAL, &copied);
        receipt.SourceMagicBytes = (ULONG)copied;
      }
      RtlCopyMemory(&candidate, receipt.RawList + 64u, sizeof(candidate));
      if (candidate != 0ULL) {
        copySource.VirtualAddress = (PVOID)(ULONG_PTR)candidate;
        copied = 0u;
        receipt.DestinationMagicCopyStatus = (ULONG)MmCopyMemory(
            &receipt.DestinationMagic, copySource,
            sizeof(receipt.DestinationMagic), MM_COPY_MEMORY_VIRTUAL, &copied);
        receipt.DestinationMagicBytes = (ULONG)copied;
      }
    }
  }
  if (adapter->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(adapter->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1PresentOpenFailure", &receipt, sizeof(receipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1PresentOpenFailure", &receipt, sizeof(receipt));
    ZwClose(key);
  }
}

_Use_decl_annotations_ void AdmissionFlushSourceAddressReceipt(
    ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING servicePath;
  if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
      InterlockedCompareExchange(&Context->SourceAddressReceiptState, 3, 2) != 2)
    return;
  if (Context->PhysicalDeviceObject != NULL &&
      NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
    WriteBinary(key, L"Wom1SourceAddressReceipt", &Context->SourceAddressReceipt,
                sizeof(Context->SourceAddressReceipt));
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1SourceAddressReceipt", &Context->SourceAddressReceipt,
                sizeof(Context->SourceAddressReceipt));
    ZwClose(key);
  }
}

_Use_decl_annotations_ void AdmissionRecordDisplayDdi(
    PDEVICE_OBJECT DeviceObject, ULONG DdiId, ULONG Phase, NTSTATUS Status) {
  HANDLE key = NULL;
  LARGE_INTEGER time;
  if (DeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  KeQuerySystemTimePrecise(&time);
  {
    ADMISSION_DISPLAY_DDI_RECEIPT receipt;
    RtlZeroMemory(&receipt, sizeof(receipt));
    receipt.Version = ADMISSION_DISPLAY_DDI_RECEIPT_VERSION;
    receipt.Bytes = sizeof(receipt);
    receipt.DdiId = DdiId;
    receipt.Phase = Phase;
    receipt.Status = (ULONG)Status;
    receipt.Time = (ULONGLONG)time.QuadPart;
    WriteBinary(key, L"Wom1DisplayDdiReceipt", &receipt, sizeof(receipt));

    /* PnP may delete the device-key values while unwinding a failed adapter
     * start.  Keep the same last observation in the owning service key for
     * post-failure diagnostics; this mirror never feeds a driver decision. */
    {
      OBJECT_ATTRIBUTES attributes;
      UNICODE_STRING servicePath;
      HANDLE serviceKey = NULL;
      RtlInitUnicodeString(
          &servicePath,
          L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
      InitializeObjectAttributes(&attributes, &servicePath,
                                 OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                 NULL, NULL);
      if (NT_SUCCESS(ZwOpenKey(&serviceKey, KEY_SET_VALUE, &attributes))) {
        WriteBinary(serviceKey, L"Wom1DisplayDdiReceipt", &receipt,
                    sizeof(receipt));
        ZwClose(serviceKey);
      }
    }
  }
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordMemoryQualification(
    PDEVICE_OBJECT DeviceObject,
    const ADMISSION_MEMORY_QUALIFICATION *Qualification) {
  HANDLE key = NULL;
  if (DeviceObject == NULL || Qualification == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1MemoryQualification", Qualification,
              sizeof(*Qualification));
  ZwClose(key);
}
