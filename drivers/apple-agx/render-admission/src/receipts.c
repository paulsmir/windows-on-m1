#include "render_admission.h"

#define ADMISSION_QUERY_RECEIPT_VERSION 1u
#define ADMISSION_DISPLAY_DDI_RECEIPT_VERSION 1u

C_ASSERT(sizeof(ADMISSION_SOURCE_ADDRESS_RECEIPT) == 88);
C_ASSERT(sizeof(ADMISSION_PRESENT_TRANSFER_RECEIPT) == 64);

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

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
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
    (void)ZwFlushKey(key);
    ZwClose(key);
  }
  RtlInitUnicodeString(&servicePath,
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AppleAgxAdmission");
  InitializeObjectAttributes(&attributes, &servicePath,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes))) {
    WriteBinary(key, L"Wom1QueueSubmissionReceipt", Receipt,
                sizeof(*Receipt));
    (void)ZwFlushKey(key);
    ZwClose(key);
  }
}

_Use_decl_annotations_ VOID AdmissionRecordUmdRenderGuard(
    ADMISSION_CONTEXT *Context, ULONG Guard, NTSTATUS Status) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1UmdRenderGuard", Guard);
  WriteDword(key, L"Wom1UmdRenderStatus", (ULONG)Status);
  ZwClose(key);
}

#endif

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
  ZwClose(key);
}

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
