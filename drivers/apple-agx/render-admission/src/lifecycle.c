#include "render_admission.h"
#include "render_wow64.h"
#include "apple_agx_start_policy.h"
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
#include "apple_agx_gpuva_g3_caps.h"
#include "hv_guest_ipa_pa_abi.h"
#include <intrin.h>
#include "arm64_hypercalls.h"
#if ADMISSION_GPUVA_G1B_PAGE_PROFILE != 16 && \
    ADMISSION_GPUVA_G1B_PAGE_PROFILE != 64
#error G3 VidMm path requires a 16- or 64-KiB local segment profile
#endif
C_ASSERT(sizeof(DXGK_PTE) == 16);
C_ASSERT(ADMISSION_MEMORY_LOCAL_SEGMENT == 2u);
C_ASSERT(DXGK_INVALID_MMU_ID == APPLE_AGX_GPUVA_G3_INVALID_MMU_ID);
#endif
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION) || \
    defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
#include "apple_agx_render_template_vm_slot.h"
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
/* RC27: the raw-RAM broker and a second private pool do not establish
 * VidMm shared-backing ownership/accounting or fallible page-in. Recognize
 * the requested contract, but refuse until it is implemented; see
 * investigation/analysis/RC27-system-segment-contract.md. */
static BOOLEAN AdmissionSystemSegmentRequested(ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  ULONG bytes = 0u;
  BOOLEAN enabled = FALSE;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  if (Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &key)))
    return FALSE;
  RtlInitUnicodeString(&name, L"Wom1SystemSegment");
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG) &&
      *(const ULONG *)value->Data == 1u)
    enabled = TRUE;
  ZwClose(key);
  return enabled;
}

static BOOLEAN AdmissionReportGpuvaArmConsumed(ADMISSION_CONTEXT *context) {
  ULONGLONG payload, result;
  if (context == NULL || context->Win32BootGeneration == 0u) {
    AdmissionRecordGpuvaArmGate(context, 5u, STATUS_INVALID_DEVICE_STATE, 0ULL);
    return FALSE;
  }
  payload = ((ULONGLONG)HV_GPUVA_ARM_CONSUMED_VERSION << 32) |
            context->Win32BootGeneration;
  KeMemoryBarrier();
  result = AdmissionHvcArmConsumed(payload);
  AdmissionRecordGpuvaArmGate(context, 6u,
      result == HV_GUEST_IPA_PA_STATUS_SUCCESS ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
      result);
  return result == HV_GUEST_IPA_PA_STATUS_SUCCESS;
}
#endif
static BOOLEAN AdmissionConsumeGpuvaArm(ADMISSION_CONTEXT *context,
                                        PCWSTR value_name,
                                        BOOLEAN report_consumption) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  ULONG bytes = 0u;
  ULONG phase = 3u;
  NTSTATUS status;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  if (context == NULL || context->PhysicalDeviceObject == NULL)
    return FALSE;
  status = IoOpenDeviceRegistryKey(context->PhysicalDeviceObject,
      PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE | KEY_SET_VALUE, &key);
  if (!NT_SUCCESS(status)) {
    AdmissionRecordGpuvaArmGate(context, 1u, status, 0ULL);
    return FALSE;
  }
  RtlInitUnicodeString(&name, value_name);
  status = ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                           value, sizeof(data.Buffer), &bytes);
  if (!NT_SUCCESS(status) || value->Type != REG_DWORD ||
      value->DataLength != sizeof(ULONG) || *(ULONG *)value->Data != 1u) {
    ZwClose(key);
    AdmissionRecordGpuvaArmGate(context, 2u,
        NT_SUCCESS(status) ? STATUS_INVALID_PARAMETER : status, 0ULL);
    return FALSE;
  }
  status = ZwDeleteValueKey(key, &name);
  if (NT_SUCCESS(status)) {
    phase = 4u;
    status = ZwFlushKey(key);
  }
  ZwClose(key);
  if (!NT_SUCCESS(status))
    AdmissionRecordGpuvaArmGate(context, phase, status, 0ULL);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (NT_SUCCESS(status) && report_consumption &&
      !AdmissionReportGpuvaArmConsumed(context))
    return FALSE;
#else
  UNREFERENCED_PARAMETER(report_consumption);
#endif
  return NT_SUCCESS(status);
}
#endif

/* F1-k production start policy (apple_agx_start_policy.h; design
 * investigation/analysis/COMPLETENESS-F1-standalone-gpu-boot.md 4.C). The
 * decision is the pure AppleAgxStartPolicyEvaluate; this layer reads the
 * device-key values, persists exactly UpdateMask with the arm code's
 * set/delete + ZwFlushKey pattern, records one receipt per event and owns
 * the T-second healthy-start timer. Policy 0 (Wom1StartPolicy absent) reads
 * only Wom1StartPolicy at the start attempt and touches nothing afterwards.
 * F1K-STAMP: nothing is written at shutdown (the registry is closed before
 * the device S5 pass, EXP2405); a policy-1 start attempt derives the clean
 * exit of the previous run from Windows' ShutdownTime instead. */
typedef struct _ADMISSION_START_POLICY_VALUE_NAME {
  ULONG Bit;
  PCWSTR Name;
} ADMISSION_START_POLICY_VALUE_NAME;

static const ADMISSION_START_POLICY_VALUE_NAME AdmissionStartPolicyValues[] = {
    {APPLE_AGX_START_POLICY_VALUE_PENDING, L"Wom1StartPending"},
    {APPLE_AGX_START_POLICY_VALUE_RUNNING, L"Wom1StartRunning"},
    {APPLE_AGX_START_POLICY_VALUE_LATCHED, L"Wom1StartLatched"},
    {APPLE_AGX_START_POLICY_VALUE_LATCH_RESET, L"Wom1StartLatchReset"},
    {APPLE_AGX_START_POLICY_VALUE_UNCLEAN_COUNT, L"Wom1StartUncleanCount"},
    {APPLE_AGX_START_POLICY_VALUE_LATCH_COUNT, L"Wom1StartLatchCount"},
};

/* F1K-STAMP. Windows' last graceful shutdown time: REG_BINARY, 8 bytes,
 * written and flushed by the kernel on every orderly shutdown/restart
 * (ExShutdownSystem phase 0) before the registry closes. Undocumented,
 * verified in ntoskrnl 10.0.26100.9711 only; any other type or size reads
 * as unreadable, i.e. not orderly (today's behaviour). */
#define ADMISSION_SHUTDOWN_TIME_KEY \
  L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Windows"
#define ADMISSION_SHUTDOWN_TIME_VALUE L"ShutdownTime"
/* Device key: REG_QWORD copy of ShutdownTime taken at the last allowed
 * policy-1 start (absent: none, or ShutdownTime was unreadable then). */
#define ADMISSION_START_SHUTDOWN_STAMP L"Wom1StartShutdownStamp"
/* Device key: REG_QWORD AppleAgxStartPolicyStampReceipt of the last
 * policy-1 start attempt. */
#define ADMISSION_START_POLICY_STAMP_RECEIPT L"Wom1StartPolicyStamp"

static KDEFERRED_ROUTINE AdmissionStartPolicyDpc;
static IO_WORKITEM_ROUTINE AdmissionStartPolicyWorker;

/* Present = the query returned data (any failure other than "not found"
 * counts as present and malformed, so markers fail closed). */
static void AdmissionStartPolicyQuery(HANDLE Key, PCWSTR Name, ULONG *Present,
                                      ULONG *DwordValid, ULONG *Value) {
  UNICODE_STRING name;
  ULONG bytes = 0u;
  NTSTATUS status;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  *Present = 0u;
  *DwordValid = 0u;
  *Value = 0u;
  RtlInitUnicodeString(&name, Name);
  status = ZwQueryValueKey(Key, &name, KeyValuePartialInformation, value,
                           sizeof(data.Buffer), &bytes);
  if (status == STATUS_OBJECT_NAME_NOT_FOUND)
    return;
  *Present = 1u;
  if (NT_SUCCESS(status) && value->Type == REG_DWORD &&
      value->DataLength == sizeof(ULONG)) {
    *DwordValid = 1u;
    *Value = *(const ULONG *)value->Data;
  }
}

/* An 8-byte value of exactly Type: STATUS_SUCCESS with *Value, the query
 * status when it failed (a larger value fails with STATUS_BUFFER_OVERFLOW),
 * STATUS_OBJECT_TYPE_MISMATCH for any other type or size. */
static NTSTATUS AdmissionStartPolicyQueryQword(HANDLE Key, PCWSTR Name,
                                               ULONG Type, ULONGLONG *Value) {
  UNICODE_STRING name;
  ULONG bytes = 0u;
  NTSTATUS status;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONGLONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  *Value = 0ULL;
  RtlInitUnicodeString(&name, Name);
  status = ZwQueryValueKey(Key, &name, KeyValuePartialInformation, value,
                           sizeof(data.Buffer), &bytes);
  if (!NT_SUCCESS(status))
    return status;
  if (value->Type != Type || value->DataLength != sizeof(ULONGLONG))
    return STATUS_OBJECT_TYPE_MISMATCH;
  /* Data sits at offset 12: copy instead of an unaligned 8-byte load. */
  RtlCopyMemory(Value, value->Data, sizeof(ULONGLONG));
  return STATUS_SUCCESS;
}

/* PASSIVE_LEVEL (the caller checked). Opens and closes its own handle. */
static NTSTATUS AdmissionStartPolicyShutdownTime(ULONGLONG *Value) {
  UNICODE_STRING path;
  OBJECT_ATTRIBUTES attributes;
  HANDLE key = NULL;
  NTSTATUS status;
  *Value = 0ULL;
  RtlInitUnicodeString(&path, ADMISSION_SHUTDOWN_TIME_KEY);
  InitializeObjectAttributes(&attributes, &path,
                             OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL,
                             NULL);
  status = ZwOpenKey(&key, KEY_QUERY_VALUE, &attributes);
  if (!NT_SUCCESS(status))
    return status;
  status = AdmissionStartPolicyQueryQword(key, ADMISSION_SHUTDOWN_TIME_VALUE,
                                          REG_BINARY, Value);
  ZwClose(key);
  return status;
}

static ULONG AdmissionStartPolicyNextField(
    const APPLE_AGX_START_POLICY_STATE *Next, ULONG Bit) {
  switch (Bit) {
  case APPLE_AGX_START_POLICY_VALUE_PENDING: return Next->Pending;
  case APPLE_AGX_START_POLICY_VALUE_RUNNING: return Next->Running;
  case APPLE_AGX_START_POLICY_VALUE_LATCHED: return Next->Latched;
  case APPLE_AGX_START_POLICY_VALUE_LATCH_RESET: return Next->LatchReset;
  case APPLE_AGX_START_POLICY_VALUE_UNCLEAN_COUNT: return Next->UncleanCount;
  default: return Next->LatchCount;
  }
}

/* Markers: REG_DWORD (1, or 2 for a pending start faulted at T) or deleted;
 * counters: REG_DWORD. One flush after
 * all values, before the caller acts on the decision.
 * StampAction (APPLE_AGX_START_POLICY_STAMP_*): the shutdown stamp is set
 * (REG_QWORD Stamp) or deleted FIRST, before any marker, so that a lazy
 * hive flush between the writes cannot leave a new Pending durable next to
 * the previous run's stamp (a crash of this run would then read as
 * orderly at the next start). */
static NTSTATUS AdmissionStartPolicyPersist(
    HANDLE Key, const APPLE_AGX_START_POLICY_RESULT *Result,
    ULONG StampAction, ULONGLONG Stamp) {
  UNICODE_STRING name;
  NTSTATUS status = STATUS_SUCCESS;
  NTSTATUS one;
  ULONG index;
  ULONG data;
  if (Result->UpdateMask == 0u &&
      StampAction == APPLE_AGX_START_POLICY_STAMP_KEEP)
    return STATUS_SUCCESS;
  if (StampAction != APPLE_AGX_START_POLICY_STAMP_KEEP) {
    RtlInitUnicodeString(&name, ADMISSION_START_SHUTDOWN_STAMP);
    if (StampAction == APPLE_AGX_START_POLICY_STAMP_SET) {
      status = ZwSetValueKey(Key, &name, 0, REG_QWORD, &Stamp, sizeof(Stamp));
    } else {
      status = ZwDeleteValueKey(Key, &name);
      if (status == STATUS_OBJECT_NAME_NOT_FOUND)
        status = STATUS_SUCCESS;
    }
  }
  for (index = 0u; index < RTL_NUMBER_OF(AdmissionStartPolicyValues);
       ++index) {
    const ULONG bit = AdmissionStartPolicyValues[index].Bit;
    if ((Result->UpdateMask & bit) == 0u)
      continue;
    RtlInitUnicodeString(&name, AdmissionStartPolicyValues[index].Name);
    data = AdmissionStartPolicyNextField(&Result->Next, bit);
    if ((bit & APPLE_AGX_START_POLICY_VALUE_MARKERS) != 0u && data == 0u) {
      one = ZwDeleteValueKey(Key, &name);
      if (one == STATUS_OBJECT_NAME_NOT_FOUND)
        one = STATUS_SUCCESS;
    } else {
      one = ZwSetValueKey(Key, &name, 0, REG_DWORD, &data, sizeof(data));
    }
    if (NT_SUCCESS(status) && !NT_SUCCESS(one))
      status = one;
  }
  one = ZwFlushKey(Key);
  return NT_SUCCESS(status) ? one : status;
}

static PCWSTR AdmissionStartPolicyReceiptName(ULONG Event) {
  switch (Event) {
  case AppleAgxStartPolicyEventAttempt: return L"Wom1StartPolicyAttempt";
  case AppleAgxStartPolicyEventSucceeded:
  case AppleAgxStartPolicyEventFailed: return L"Wom1StartPolicyReturn";
  case AppleAgxStartPolicyEventCleanStop: return L"Wom1StartPolicyStop";
  default: return L"Wom1StartPolicyTimer";
  }
}

/* Timer, DPC, event and work item exist only once a policy-1 start was
 * allowed; without the work item the start is refused before any write. */
static NTSTATUS AdmissionStartPolicyPrepare(ADMISSION_CONTEXT *Context) {
  ADMISSION_START_POLICY_RUNTIME *runtime = &Context->StartPolicy;
  if (runtime->Initialized == 0u) {
    KeInitializeTimer(&runtime->Timer);
    KeInitializeDpc(&runtime->Dpc, AdmissionStartPolicyDpc, Context);
    KeInitializeEvent(&runtime->Idle, NotificationEvent, TRUE);
    InterlockedExchange(&runtime->TimerState, 0);
    runtime->Initialized = 1u;
  }
  if (runtime->WorkItem == NULL)
    runtime->WorkItem = IoAllocateWorkItem(Context->PhysicalDeviceObject);
  return runtime->WorkItem != NULL ? STATUS_SUCCESS
                                   : STATUS_INSUFFICIENT_RESOURCES;
}

/* One event: read, decide, persist + flush, receipt (+ flush). Returns the
 * registry status; Result->Decision is Refuse whenever an allowed start
 * could not be made durable. */
static NTSTATUS AdmissionStartPolicyEvent(ADMISSION_CONTEXT *Context,
                                          ULONG Event, ULONG Healthy,
                                          APPLE_AGX_START_POLICY_RESULT *Result) {
  HANDLE key = NULL;
  APPLE_AGX_START_POLICY_STATE state;
  ULONG present, dword, value;
  NTSTATUS status;
  /* F1K-STAMP derivation, policy-1 start attempts only. */
  ULONG stamp_derived = 0u;
  NTSTATUS time_status = STATUS_SUCCESS;
  ULONGLONG shutdown_time = 0ULL;
  ULONGLONG stamp = 0ULL;
  ULONG time_readable = 0u;
  ULONG stamp_present = 0u;
  ULONG stamp_action = APPLE_AGX_START_POLICY_STAMP_KEEP;
  ULONG stamp_done = APPLE_AGX_START_POLICY_STAMP_KEEP;
  RtlZeroMemory(&state, sizeof(state));
  RtlZeroMemory(Result, sizeof(*Result));
  if (Event != AppleAgxStartPolicyEventAttempt &&
      Context->StartPolicy.Policy != APPLE_AGX_START_POLICY_CRASH_LATCH)
    return STATUS_SUCCESS;
  status = KeGetCurrentIrql() == PASSIVE_LEVEL &&
                   Context->PhysicalDeviceObject != NULL
               ? IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
                     PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE | KEY_SET_VALUE,
                     &key)
               : STATUS_INVALID_DEVICE_STATE;
  if (!NT_SUCCESS(status)) {
    /* Unreadable at the start attempt: today's arm path, which fails
     * closed on the same key with its own phase-1 receipt. */
    if (Event == AppleAgxStartPolicyEventAttempt) {
      Context->StartPolicy.Policy = APPLE_AGX_START_POLICY_ONE_SHOT_ARM;
      (void)AppleAgxStartPolicyEvaluate(&state, Event, Healthy, Result);
    }
    return status;
  }
  if (Event == AppleAgxStartPolicyEventAttempt) {
    AdmissionStartPolicyQuery(key, L"Wom1StartPolicy", &present, &dword,
                              &value);
    state.Policy = AppleAgxStartPolicyParsePolicy(present, dword, value);
    Context->StartPolicy.Policy = state.Policy;
  } else {
    state.Policy = Context->StartPolicy.Policy;
  }
  if (state.Policy == APPLE_AGX_START_POLICY_CRASH_LATCH) {
    AdmissionStartPolicyQuery(key, L"Wom1StartPending", &present, &dword,
                              &value);
    state.Pending = AppleAgxStartPolicyParsePending(present, dword, value);
    AdmissionStartPolicyQuery(key, L"Wom1StartRunning", &present, &dword,
                              &value);
    state.Running = AppleAgxStartPolicyParseMarker(present, dword, value);
    AdmissionStartPolicyQuery(key, L"Wom1StartLatched", &present, &dword,
                              &value);
    state.Latched = AppleAgxStartPolicyParseMarker(present, dword, value);
    AdmissionStartPolicyQuery(key, L"Wom1StartLatchReset", &present, &dword,
                              &value);
    state.LatchReset = AppleAgxStartPolicyParseExplicit(present, dword, value);
    AdmissionStartPolicyQuery(key, L"Wom1StartUncleanCount", &present, &dword,
                              &value);
    state.UncleanCount = AppleAgxStartPolicyParseCounter(present, dword, value);
    AdmissionStartPolicyQuery(key, L"Wom1StartLatchCount", &present, &dword,
                              &value);
    state.LatchCount = AppleAgxStartPolicyParseCounter(present, dword, value);
    if (Event == AppleAgxStartPolicyEventAttempt) {
      /* OrderlyExit = ShutdownTime readable, stamp present, and different:
       * Windows recorded a graceful shutdown/restart after the last allowed
       * start. Anything unreadable is not orderly (fail closed). */
      stamp_derived = 1u;
      time_status = AdmissionStartPolicyShutdownTime(&shutdown_time);
      time_readable = NT_SUCCESS(time_status) ? 1u : 0u;
      stamp_present =
          NT_SUCCESS(AdmissionStartPolicyQueryQword(
              key, ADMISSION_START_SHUTDOWN_STAMP, REG_QWORD, &stamp))
              ? 1u
              : 0u;
      state.OrderlyExit = AppleAgxStartPolicyOrderlyExit(
          time_readable, shutdown_time, stamp_present, stamp);
    }
  }
  (void)AppleAgxStartPolicyEvaluate(&state, Event, Healthy, Result);
  if (stamp_derived != 0u)
    stamp_action =
        AppleAgxStartPolicyStampAction(Result->Decision, time_readable);
  status = Result->Decision == AppleAgxStartPolicyDecisionAllow
               ? AdmissionStartPolicyPrepare(Context)
               : STATUS_SUCCESS;
  if (NT_SUCCESS(status)) {
    stamp_done = stamp_action;
    status = AdmissionStartPolicyPersist(key, Result, stamp_action,
                                         shutdown_time);
    if (!NT_SUCCESS(status) &&
        Result->Decision == AppleAgxStartPolicyDecisionAllow) {
      /* Best effort: do not leave a marker of a start that is refused. */
      APPLE_AGX_START_POLICY_RESULT undo;
      if (AppleAgxStartPolicyEvaluate(&Result->Next,
                                      AppleAgxStartPolicyEventFailed, 0u,
                                      &undo))
        (void)AdmissionStartPolicyPersist(
            key, &undo, APPLE_AGX_START_POLICY_STAMP_KEEP, 0ULL);
    }
  }
  if (!NT_SUCCESS(status) &&
      Result->Decision == AppleAgxStartPolicyDecisionAllow)
    Result->Decision = AppleAgxStartPolicyDecisionRefuse;
  {
    UNICODE_STRING name;
    ULONG receipt = AppleAgxStartPolicyReceipt(&state, Result);
    ULONG io = (ULONG)status;
    RtlInitUnicodeString(&name, AdmissionStartPolicyReceiptName(Event));
    (void)ZwSetValueKey(key, &name, 0, REG_DWORD, &receipt, sizeof(receipt));
    RtlInitUnicodeString(&name, L"Wom1StartPolicyIoStatus");
    (void)ZwSetValueKey(key, &name, 0, REG_DWORD, &io, sizeof(io));
    if (stamp_derived != 0u) {
      ULONGLONG stamp_receipt = AppleAgxStartPolicyStampReceipt(
          (ULONG)time_status, stamp_present,
          time_readable != 0u && stamp_present != 0u &&
                  shutdown_time == stamp
              ? 1u
              : 0u,
          state.OrderlyExit, stamp_done);
      RtlInitUnicodeString(&name, ADMISSION_START_POLICY_STAMP_RECEIPT);
      (void)ZwSetValueKey(key, &name, 0, REG_QWORD, &stamp_receipt,
                          sizeof(stamp_receipt));
    }
  }
  if (state.Policy != APPLE_AGX_START_POLICY_ONE_SHOT_ARM)
    (void)ZwFlushKey(key);
  if (Event == AppleAgxStartPolicyEventAttempt &&
      Result->Decision == AppleAgxStartPolicyDecisionAllow)
    Context->StartPolicy.PendingOwned = 1u;
  ZwClose(key);
  return status;
}

static VOID AdmissionStartPolicyDpc(_In_ PKDPC Dpc,
                                    _In_opt_ PVOID DeferredContext,
                                    _In_opt_ PVOID Argument1,
                                    _In_opt_ PVOID Argument2) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)DeferredContext;
  UNREFERENCED_PARAMETER(Dpc);
  UNREFERENCED_PARAMETER(Argument1);
  UNREFERENCED_PARAMETER(Argument2);
  if (context == NULL)
    return;
  if (InterlockedCompareExchange(&context->StartPolicy.TimerState, 2, 1) == 1)
    IoQueueWorkItem(context->StartPolicy.WorkItem, AdmissionStartPolicyWorker,
                    DelayedWorkQueue, context);
}

/* PASSIVE_LEVEL, T seconds after a successful start: clear the pending
 * marker while the scheduler is not faulted; otherwise keep it pending as
 * value 2 (the next boot latches with reason FaultedPending). */
static VOID AdmissionStartPolicyWorker(_In_ PDEVICE_OBJECT DeviceObject,
                                       _In_opt_ PVOID Parameter) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Parameter;
  APPLE_AGX_START_POLICY_RESULT result;
  UNREFERENCED_PARAMETER(DeviceObject);
  if (context == NULL)
    return;
  (void)AdmissionStartPolicyEvent(
      context, AppleAgxStartPolicyEventTimer,
      InterlockedCompareExchange(&context->SchedulerFaulted, 0, 0) == 0 ? 1u
                                                                        : 0u,
      &result);
  InterlockedExchange(&context->StartPolicy.TimerState, 0);
  KeSetEvent(&context->StartPolicy.Idle, IO_NO_INCREMENT, FALSE);
}

/* Cancel the timer and drain its DPC and work item (StopDevice before the
 * runtime goes away, RemoveDevice before the context is freed). */
static void AdmissionStartPolicyQuiesce(ADMISSION_CONTEXT *Context) {
  if (Context == NULL || Context->StartPolicy.Initialized == 0u)
    return;
  if (InterlockedCompareExchange(&Context->StartPolicy.TimerState, 0, 1) ==
      1) {
    if (!KeCancelTimer(&Context->StartPolicy.Timer))
      KeFlushQueuedDpcs();
    KeSetEvent(&Context->StartPolicy.Idle, IO_NO_INCREMENT, FALSE);
  }
  (void)KeWaitForSingleObject(&Context->StartPolicy.Idle, Executive,
                              KernelMode, FALSE, NULL);
}

/* After StartDevice returned, for a start this policy allowed: an error
 * clears both markers; a success arms the healthy-start timer. */
static void AdmissionStartPolicyReturned(ADMISSION_CONTEXT *Context,
                                         NTSTATUS Status) {
  APPLE_AGX_START_POLICY_RESULT result;
  LARGE_INTEGER due;
  if (Context == NULL || Context->StartPolicy.PendingOwned == 0u)
    return;
  Context->StartPolicy.PendingOwned = 0u;
  (void)AdmissionStartPolicyEvent(
      Context,
      NT_SUCCESS(Status) ? AppleAgxStartPolicyEventSucceeded
                         : AppleAgxStartPolicyEventFailed,
      1u, &result);
  if (result.ArmTimer == 0u || Context->StartPolicy.WorkItem == NULL)
    return;
  KeClearEvent(&Context->StartPolicy.Idle);
  InterlockedExchange(&Context->StartPolicy.TimerState, 1);
  due.QuadPart =
      -(LONGLONG)APPLE_AGX_START_POLICY_TIMER_SECONDS * 10000000LL;
  (void)KeSetTimer(&Context->StartPolicy.Timer, due,
                   &Context->StartPolicy.Dpc);
}

static void AdmissionStartPolicyStopped(ADMISSION_CONTEXT *Context) {
  APPLE_AGX_START_POLICY_RESULT result;
  (void)AdmissionStartPolicyEvent(Context, AppleAgxStartPolicyEventCleanStop,
                                  1u, &result);
}

static void AdmissionStartPolicyRelease(ADMISSION_CONTEXT *Context) {
  AdmissionStartPolicyQuiesce(Context);
  if (Context->StartPolicy.WorkItem != NULL) {
    IoFreeWorkItem(Context->StartPolicy.WorkItem);
    Context->StartPolicy.WorkItem = NULL;
  }
}

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
/* Called where G3Armed is consumed, after the resource-shape check and
 * before any GPU or broker access. Policy 0: AppleAgxStartPolicyDecisionArm,
 * and the caller consumes the one-shot arm exactly as before. Policy 1:
 * Allow only after Wom1StartPending/Wom1StartRunning were flushed; the arm
 * consumption is then reported to m1n1 as the arm path does. */
static ULONG AdmissionStartPolicyAttempt(ADMISSION_CONTEXT *Context) {
  APPLE_AGX_START_POLICY_RESULT result;
  Context->StartPolicy.PendingOwned = 0u;
  (void)AdmissionStartPolicyEvent(Context, AppleAgxStartPolicyEventAttempt,
                                  1u, &result);
  /* A refused report leaves PendingOwned set: the return clears it. */
  if (result.Decision == AppleAgxStartPolicyDecisionAllow &&
      !AdmissionReportGpuvaArmConsumed(Context))
    return AppleAgxStartPolicyDecisionRefuse;
  return result.Decision;
}
#endif

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
/* _CRS publishes the dynamic local range only when Mu read a valid R64
 * receipt. Check that resource shape before consuming the one-shot arm or
 * touching any GPU/broker register. Borrow later verifies the live receipt. */
static BOOLEAN AdmissionG3FirmwareResourcesPresent(
    const ADMISSION_CONTEXT *context) {
  PCM_RESOURCE_LIST resources;
  PCM_FULL_RESOURCE_DESCRIPTOR full;
  ULONG index;
  ULONG seen = 0u;
  ULONG edge_interrupts = 0u;
  ULONG level_interrupts = 0u;
  if (context == NULL)
    return FALSE;
  resources = context->DeviceInformation.TranslatedResourceList;
  if (resources == NULL || resources->Count != 1u)
    return FALSE;
  full = &resources->List[0];
  for (index = 0u; index < full->PartialResourceList.Count; ++index) {
    PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor =
        &full->PartialResourceList.PartialDescriptors[index];
    ULONG bit = 0u;
    if (descriptor->Type == CmResourceTypeMemory) {
      ULONGLONG start = (ULONGLONG)descriptor->u.Memory.Start.QuadPart;
      ULONG length = descriptor->u.Memory.Length;
      if (start == J313_AGX_G2_SGX_MMIO_BASE &&
          length == J313_AGX_G2_SGX_MMIO_SIZE)
        bit = 1u << 0;
      else if (start == J313_AGX_G2_GPU_BASE &&
               length == J313_AGX_G2_GPU_SIZE)
        bit = 1u << 1;
      else if (start == J313_AGX_G2_HANDOFF_BASE &&
               length == J313_AGX_G2_HANDOFF_SIZE)
        bit = 1u << 2;
      else if (start == J313_AGX_G2_POWER_BROKER_BASE &&
               length == J313_AGX_G2_POWER_BROKER_SIZE)
        bit = 1u << 3;
      else if (start != 0ULL &&
               (start & (APPLE_AGX_LOCAL_RESERVE_ALIGNMENT - 1ULL)) == 0ULL &&
               AppleAgxLocalReserveBytesValid(length))
        bit = 1u << 4;
      if (bit == 0u || (seen & bit) != 0u)
        return FALSE;
      seen |= bit;
    } else if (descriptor->Type == CmResourceTypeDevicePrivate) {
      /* PnP owns this reserved payload; it is not an address range. */
      continue;
    } else if (descriptor->Type == CmResourceTypeInterrupt &&
               descriptor->ShareDisposition == CmResourceShareDeviceExclusive &&
               descriptor->Flags == CM_RESOURCE_INTERRUPT_LATCHED &&
               descriptor->u.Interrupt.Vector != 0u) {
      ++edge_interrupts;
    } else if (descriptor->Type == CmResourceTypeInterrupt &&
               descriptor->ShareDisposition == CmResourceShareDeviceExclusive &&
               descriptor->Flags == CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE &&
               descriptor->u.Interrupt.Vector != 0u) {
      /* IRQ-completion step 1: the optional ASC mailbox recv-not-empty line. */
      ++level_interrupts;
    } else {
      return FALSE;
    }
  }
  return seen == 0x1fu && edge_interrupts == 1u && level_interrupts <= 1u;
}

_Use_decl_annotations_ BOOLEAN AdmissionGpuvaG3DeclarationReady(
    const ADMISSION_CONTEXT *context) {
  APPLE_AGX_GPUVA_G3_ADMISSION_CONTRACT model =
      AppleAgxGpuvaG3AdmissionContract(1u, ADMISSION_GPUVA_G1B_PAGE_PROFILE);
  APPLE_AGX_GPUVA_G3_CAPS tables =
      AppleAgxGpuvaG3Caps(model.LocalUse64KBPages ||
                          model.SysMem64KBPageSupported);
  if (context == NULL || context->GpuvaG3State == NULL ||
      !AdmissionMemoryReady(&context->Memory) ||
      !AppleAgxGpuvaG3AdmissionContractValid(&model, sizeof(DXGK_PTE)) ||
      !AppleAgxGpuvaG3CapsValid(&tables, sizeof(DXGK_PTE),
                               model.LocalUse64KBPages ||
                               model.SysMem64KBPageSupported, 0u))
    return FALSE;
  /* M2-f2: an optional third segment is the KMD-populated pool. */
  return (context->Memory.Topology.SegmentCount == model.SegmentCount ||
          (context->Memory.Topology.SegmentCount == model.SegmentCount + 1u &&
           context->Memory.Topology.Pool.Id == model.SegmentCount + 1u &&
           context->Memory.Topology.Pool.Aperture == APPLE_AGX_FALSE)) &&
         context->Memory.Topology.Aperture.Id == model.ApertureSegmentId &&
         context->Memory.Topology.Local.Id == model.LocalSegmentId &&
         context->Memory.Topology.PagingBufferSegmentId ==
             model.PagingBufferSegmentId &&
         context->Memory.Topology.PagingBufferSize ==
             model.PagingBufferBytes &&
         context->Memory.Topology.Aperture.Aperture != APPLE_AGX_FALSE &&
         context->Memory.Topology.Local.Aperture == APPLE_AGX_FALSE;
}
#endif

_Use_decl_annotations_ NTSTATUS AdmissionDdiAddDevice(
    PDEVICE_OBJECT PhysicalDeviceObject, PVOID *MiniportDeviceContext) {
  ADMISSION_CONTEXT *context;
  AdmissionRecordDevice(PhysicalDeviceObject, AdmissionReceiptAddEntered,
                        STATUS_PENDING);
  if (PhysicalDeviceObject == NULL || MiniportDeviceContext == NULL)
    return STATUS_INVALID_PARAMETER;
  context = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*context),
                            ADMISSION_POOL_TAG);
  if (context == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(context, sizeof(*context));
  AdmissionObjectsInitializeAdapter(&context->ObjectAdapter);
  context->Win32BootGeneration = (ULONG)KeQueryInterruptTime();
  if (context->Win32BootGeneration == 0u)
    context->Win32BootGeneration = 1u;
  context->FeatureReadyMask =
      APPLE_AGX_WDDM_READY_WDDM3_IDENTITY |
      APPLE_AGX_WDDM_READY_DEVICE_CONTEXT;
  context->PhysicalDeviceObject = PhysicalDeviceObject;
  *MiniportDeviceContext = context;
  AdmissionRecordDevice(PhysicalDeviceObject, AdmissionReceiptAddSucceeded,
                        STATUS_SUCCESS);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiStartDevice(
    PVOID MiniportDeviceContext, PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface, PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  NTSTATUS status;
  APPLE_AGX_POST_DISPLAY_ROUTE post_display_route;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  ULONG start_policy;
#endif

  AdmissionRecordDevice(context == NULL ? NULL : context->PhysicalDeviceObject,
                        AdmissionReceiptStartEntered, STATUS_PENDING);
  if (context != NULL) {
    InterlockedIncrement(&context->MpoGate.StartDeviceCount);
    context->MpoGate.StartEntryQpc = AdmissionMpoGateQpc(context);
  }
  AdmissionRecordStartStage(context, AdmissionStartEntered, STATUS_PENDING);
  if (context == NULL || DxgkStartInfo == NULL || DxgkInterface == NULL ||
      NumberOfVideoPresentSources == NULL || NumberOfChildren == NULL)
    return STATUS_INVALID_PARAMETER;
  *NumberOfVideoPresentSources = 0;
  *NumberOfChildren = 0;
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
  if (!AdmissionConsumeGpuvaArm(context, L"B1Armed", FALSE)) {
    AdmissionRecordB1Qualification(
        context, 0u, STATUS_NOT_SUPPORTED, STATUS_NOT_SUPPORTED,
        0u, 0u, 0ULL,
        0u, 0u, 0u, 0u, 0u);
    AdmissionRecordStartStage(context, AdmissionStartEntered,
                              STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
  }
#endif
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  context->StartInfo = *DxgkStartInfo;
  context->Interface = *DxgkInterface;
  context->InterfaceValid = TRUE;
  RtlZeroMemory(&context->DeviceInformation,
                sizeof(context->DeviceInformation));
  status = context->Interface.DxgkCbGetDeviceInformation(
      context->Interface.DeviceHandle, &context->DeviceInformation);
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartDeviceInfo, status);
  AdmissionRecordStartStage(context, AdmissionStartDeviceInfo, status);
  if (!NT_SUCCESS(status))
    return status;
  AdmissionRecordTranslatedResources(context);
  if (!AdmissionG3FirmwareResourcesPresent(context)) {
    AdmissionRecordStartStage(context, AdmissionStartDeviceInfo,
                              STATUS_DEVICE_NOT_READY);
    return STATUS_DEVICE_NOT_READY;
  }
  /* F1-k: policy 0 (Wom1StartPolicy absent) is the one-shot arm below,
   * unchanged; policy 1 starts without an arm once Wom1StartPending is
   * durable; every other decision fails closed before GPU access. */
  start_policy = AdmissionStartPolicyAttempt(context);
  if (start_policy == AppleAgxStartPolicyDecisionArm &&
      !AdmissionConsumeGpuvaArm(context, L"G3Armed", TRUE)) {
    AdmissionRecordStartStage(context, AdmissionStartEntered,
                              STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
  }
  if (start_policy != AppleAgxStartPolicyDecisionArm &&
      start_policy != AppleAgxStartPolicyDecisionAllow) {
    AdmissionRecordStartStage(context, AdmissionStartEntered,
                              STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
  }
  /* Consume the one-shot arm as usual, then stop before any runtime owner
   * starts. Refusing later in G3 would unnecessarily unwind live scanout. */
  if (AdmissionSystemSegmentRequested(context)) {
    AdmissionRecordStartStage(context, AdmissionStartEntered,
                              STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
  }
#endif
#if !defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  context->StartInfo = *DxgkStartInfo;
  context->Interface = *DxgkInterface;
  context->InterfaceValid = TRUE;
  RtlZeroMemory(&context->DeviceInformation,
                sizeof(context->DeviceInformation));
  status = context->Interface.DxgkCbGetDeviceInformation(
      context->Interface.DeviceHandle, &context->DeviceInformation);
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartDeviceInfo, status);
  AdmissionRecordStartStage(context, AdmissionStartDeviceInfo, status);
  if (!NT_SUCCESS(status))
    return status;
#endif

  status = AdmissionInterruptStart(context);
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartInterrupt, status);
  AdmissionRecordStartStage(context, AdmissionStartInterrupt, status);
  if (!NT_SUCCESS(status))
    return status;

  status = AdmissionMemoryRuntimeStart(context);
  AdmissionRecordStartStage(context, AdmissionStartMemory, status);
  if (!NT_SUCCESS(status)) {
#if defined(APPLE_AGX_RENDER_MEMORY_QUALIFICATION)
    ADMISSION_MEMORY_QUALIFICATION qualification;
    NTSTATUS interruptStatus;
    RtlZeroMemory(&qualification, sizeof(qualification));
    qualification.Version = ADMISSION_MEMORY_QUALIFICATION_VERSION;
    qualification.Size = sizeof(qualification);
    qualification.QualificationStatus = status;
    qualification.CleanupStatus = context->MemoryRuntime == NULL
                                      ? STATUS_SUCCESS
                                      : STATUS_DEVICE_BUSY;
    qualification.StartStage = (ULONG)InterlockedCompareExchange(
        &context->MemoryStartStage, 0, 0);
    interruptStatus = AdmissionInterruptStop(context);
    AdmissionRecordDevice(context->PhysicalDeviceObject,
                          AdmissionReceiptMemoryQualified, status);
    AdmissionRecordMemoryQualification(context->PhysicalDeviceObject,
                                       &qualification);
    if (!NT_SUCCESS(interruptStatus))
      return interruptStatus;
#else
    (void)AdmissionInterruptStop(context);
#endif
    return status;
  }
#if defined(APPLE_AGX_RENDER_MEMORY_QUALIFICATION)
  {
    ADMISSION_MEMORY_QUALIFICATION qualification;
    NTSTATUS cleanupStatus;
    NTSTATUS interruptStatus;

    status = AdmissionMemoryRuntimeQualify(context, &qualification);
    cleanupStatus = AdmissionMemoryRuntimeStop(context);
    qualification.CleanupStatus = cleanupStatus;
    interruptStatus = AdmissionInterruptStop(context);
    AdmissionRecordDevice(context->PhysicalDeviceObject,
                          AdmissionReceiptMemoryQualified, status);
    AdmissionRecordMemoryQualification(context->PhysicalDeviceObject,
                                       &qualification);
    RtlZeroMemory(&context->Interface, sizeof(context->Interface));
    context->InterfaceValid = FALSE;
    if (!NT_SUCCESS(status))
      return status;
    if (!NT_SUCCESS(cleanupStatus))
      return cleanupStatus;
    if (!NT_SUCCESS(interruptStatus))
      return interruptStatus;
    return STATUS_NOT_SUPPORTED;
  }
#endif
  status = AdmissionBackendImageStart(context);
  AdmissionRecordStartStage(context, AdmissionStartBackendImage, status);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION) || \
    defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (!AdmissionBackendImageSelectVmSlot(&context->BackendImage,
                                         ADMISSION_GPUVA_G3_JOB_VM_SLOT)) {
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return STATUS_INVALID_IMAGE_FORMAT;
  }
#endif
  status = AdmissionSchedulerStart(context);
  AdmissionRecordStartStage(context, AdmissionStartScheduler, status);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }
  status = AdmissionPagingStart(context);
  AdmissionRecordStartStage(context, AdmissionStartPaging, status);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }
  status = AdmissionPlatformRuntimeStart(context);
  AdmissionRecordStartStage(context, AdmissionStartPlatform, status);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }

#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
  status = AdmissionGpuvaB1Qualify(context);
  if (context->GpuvaB1State != NULL)
    return STATUS_DEVICE_BUSY; /* Preserve pages with uncertain GPU use. */
  if (!NT_SUCCESS(AdmissionPlatformRuntimeStop(context)))
    return STATUS_DEVICE_BUSY;
  if (!NT_SUCCESS(AdmissionPagingStop(context)) ||
      !NT_SUCCESS(AdmissionSchedulerStop(context)) ||
      !NT_SUCCESS(AdmissionBackendImageStop(context)) ||
      !NT_SUCCESS(AdmissionMemoryRuntimeStop(context)) ||
      !NT_SUCCESS(AdmissionInterruptStop(context)))
    return STATUS_DEVICE_BUSY;
  return NT_SUCCESS(status) ? STATUS_NOT_SUPPORTED : status;
#endif

  if (context->Interface.DxgkCbAcquirePostDisplayOwnership == NULL) {
    AdmissionRecordPostDisplay(context, STATUS_NOT_SUPPORTED,
                               AppleAgxPostDisplayAcquireFailed,
                               STATUS_NOT_SUPPORTED);
    AdmissionRecordStartStage(
        context, AdmissionStartPostDisplay, STATUS_NOT_SUPPORTED);
    (void)AdmissionPlatformRuntimeStop(context);
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return STATUS_NOT_SUPPORTED;
  }
  RtlZeroMemory(&context->PostDisplayInformation,
                sizeof(context->PostDisplayInformation));
  status = context->Interface.DxgkCbAcquirePostDisplayOwnership(
      context->Interface.DeviceHandle, &context->PostDisplayInformation);
  post_display_route = AppleAgxPostDisplayRoute(
      NT_SUCCESS(status) ? APPLE_AGX_SCANOUT_TRUE : APPLE_AGX_SCANOUT_FALSE,
      context->PostDisplayInformation.Width,
      context->PostDisplayInformation.Height,
      context->PostDisplayInformation.Pitch,
      (APPLE_AGX_SCANOUT_U32)context->PostDisplayInformation.ColorFormat,
      (ULONGLONG)context->PostDisplayInformation.PhysicAddress.QuadPart,
      AdmissionPanelLimits());
  AdmissionRecordPostDisplay(
      context, status, post_display_route,
      post_display_route == AppleAgxPostDisplayGeometryRejected
          ? STATUS_GRAPHICS_INVALID_DISPLAY_ADAPTER : status);
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartPostDisplay, status);
  AdmissionRecordStartStage(context, AdmissionStartPostDisplay, status);
  if (post_display_route == AppleAgxPostDisplayAcquireFailed) {
    (void)AdmissionPlatformRuntimeStop(context);
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }
  if (post_display_route == AppleAgxPostDisplayGeometryRejected) {
    AdmissionRecordStartStage(
        context, AdmissionStartPostDisplay,
        STATUS_GRAPHICS_INVALID_DISPLAY_ADAPTER);
    (void)AdmissionPlatformRuntimeStop(context);
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return STATUS_GRAPHICS_INVALID_DISPLAY_ADAPTER;
  }
  if (post_display_route == AppleAgxPostDisplayOwnScanout)
    RtlZeroMemory(&context->PostDisplayInformation,
                  sizeof(context->PostDisplayInformation));
  status = AdmissionScanoutStart(context);
  AdmissionRecordStartStage(context, AdmissionStartScanout, status);
  if (!NT_SUCCESS(status)) {
    /* PD-1c: failed-start teardown; the scanout stop below (or PnP's retry)
     * releases to the guest surface (PD-1 m1n1), leaving GOP mode usable. */
    {
      ADMISSION_STOP_EVENT stopEvent;
      RtlZeroMemory(&stopEvent, sizeof(stopEvent));
      stopEvent.Caller = ADMISSION_STOP_CALLER_START_FAILED;
      stopEvent.Status = (ULONG)status;
      stopEvent.FillResult = ADMISSION_STOP_FILL_NA;
      stopEvent.SurfaceSource = ADMISSION_STOP_SOURCE_NONE;
      stopEvent.OwnershipBefore = context->PostDisplayInformation.Width != 0u;
      AdmissionRecordStopReceipt(context->PhysicalDeviceObject, &stopEvent);
    }
    /* An uncertain REGISTER/RELEASE result retains every lower memory owner.
     * PnP teardown may retry AdmissionScanoutStop, but must not unmap a pool
     * that m1n1/DCP could still own. */
    if (context->ScanoutRuntime != NULL)
      return status;
    (void)AdmissionPlatformRuntimeStop(context);
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }

  context->Started = TRUE;
  if (!AdmissionObjectsStartAdapter(&context->ObjectAdapter)) {
    AdmissionRecordStartStage(
        context, AdmissionStartObjects, STATUS_INVALID_DEVICE_STATE);
    context->Started = FALSE;
    status = AdmissionScanoutStop(context);
    if (!NT_SUCCESS(status))
      return status;
    (void)AdmissionPlatformRuntimeStop(context);
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return STATUS_INVALID_DEVICE_STATE;
  }
  AdmissionRecordStartStage(context, AdmissionStartObjects, STATUS_SUCCESS);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  status = AdmissionGpuvaG3Start(context);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionObjectsStopAdapter(&context->ObjectAdapter);
    context->Started = FALSE;
    (void)AdmissionScanoutStop(context);
    (void)AdmissionPlatformRuntimeStop(context);
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }
  /* M2-f2: after every fallible start step; without the block the adapter
   * keeps two segments. */
  AdmissionMemoryPoolStart(context);
#endif
  context->DisplayActive = TRUE;
  context->SourceVisible = TRUE;
  context->CommittedWidth = APPLE_AGX_SCANOUT_J313_WIDTH;
  context->CommittedHeight = APPLE_AGX_SCANOUT_J313_HEIGHT;
  context->CommittedStride = APPLE_AGX_SCANOUT_J313_STRIDE;
  context->CommittedFormat = D3DDDIFMT_A8R8G8B8;
  context->CommittedScaling = (ULONG)D3DKMDT_VPPS_IDENTITY;
  /* QueryAdapterInfo cannot run until StartDevice returns. Publish the complete
   * immutable implementation vector only after every runtime owner above has
   * started and the adapter object is live. */
  InterlockedExchange(&context->FeatureReadyMask,
                      APPLE_AGX_WDDM_REQUIRED_READY_MASK);
  AdmissionRecordStartStage(context, AdmissionStartComplete, STATUS_SUCCESS);
  context->MpoGate.StartExitQpc = AdmissionMpoGateQpc(context);
  AdmissionRecordMpoGate(context);
  *NumberOfVideoPresentSources = 1;
  *NumberOfChildren = 1;
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartSucceeded, STATUS_SUCCESS);
  return STATUS_SUCCESS;
}

static NTSTATUS AdmissionStopDeviceBody(PVOID MiniportDeviceContext) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  if (context != NULL) {
    InterlockedIncrement(&context->MpoGate.StopDeviceCount);
    AdmissionRecordMpoGate(context);
  }
  NTSTATUS status;
  if (context == NULL)
    return STATUS_INVALID_PARAMETER;
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
  if (context->GpuvaB1State != NULL)
    return STATUS_DEVICE_BUSY;
#endif
  /* F1-k: no healthy-start timer may outlive the runtime it judges. */
  AdmissionStartPolicyQuiesce(context);
  AdmissionFlushSourceAddressReceipt(context);
  AdmissionFlushPresentTransfer(context);
  AdmissionFlushGdiReceipt(context);
  AdmissionRecordDevice(context->PhysicalDeviceObject, AdmissionReceiptStop,
                        STATUS_SUCCESS);
  if (context->ObjectAdapter.DeviceCount != 0u)
    return STATUS_DEVICE_BUSY;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  status = AdmissionGpuvaG3Stop(context);
  if (!NT_SUCCESS(status)) return status;
#endif
  status = AdmissionScanoutStop(context);
  if (!NT_SUCCESS(status))
    return status;
  status = AdmissionPagingStop(context);
  if (!NT_SUCCESS(status))
    return status;
  status = AdmissionPlatformRuntimeStop(context);
  if (!NT_SUCCESS(status))
    return status;
  status = AdmissionSchedulerStop(context);
  if (!NT_SUCCESS(status))
    return status;
  status = AdmissionBackendImageStop(context);
  if (!NT_SUCCESS(status))
    return status;
  status = AdmissionMemoryRuntimeStop(context);
  if (!NT_SUCCESS(status))
    return status;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  /* VidMm no longer uses segment 3 once the memory runtime stopped. */
  AdmissionMemoryPoolStop(context);
#endif
  status = AdmissionInterruptStop(context);
  if (!NT_SUCCESS(status))
    return status;
  if (!AdmissionObjectsStopAdapter(&context->ObjectAdapter))
    return STATUS_DEVICE_BUSY;
  context->Started = FALSE;
  context->DisplayActive = FALSE;
  context->SourceVisible = FALSE;
  InterlockedExchange(
      &context->FeatureReadyMask,
      APPLE_AGX_WDDM_READY_WDDM3_IDENTITY |
          APPLE_AGX_WDDM_READY_DEVICE_CONTEXT);
  RtlZeroMemory(&context->StartInfo, sizeof(context->StartInfo));
  RtlZeroMemory(&context->DeviceInformation,
                sizeof(context->DeviceInformation));
  RtlZeroMemory(&context->PostDisplayInformation,
                sizeof(context->PostDisplayInformation));
  RtlZeroMemory(&context->Interface, sizeof(context->Interface));
  context->InterfaceValid = FALSE;
  /* F1-k: the runtime stopped cleanly: clear pending/running markers. */
  AdmissionStartPolicyStopped(context);
  return STATUS_SUCCESS;
}

/* PD-1c (KMD-D2): the one stop path. StopDevice (Caller 2) and the post-display
 * release (Caller 26, after its black fill) run the same teardown, whose scanout
 * stop sends the broker RELEASE (PD-1 m1n1 presents the guest surface and
 * removes slot 1). Every call leaves one Wom1StopReceipt ring entry. */
_Use_decl_annotations_ NTSTATUS AdmissionStopDeviceInternal(
    ADMISSION_CONTEXT *Context, ADMISSION_STOP_EVENT *Event) {
  NTSTATUS status;
  PDEVICE_OBJECT pdo = Context->PhysicalDeviceObject;
  Event->OwnershipBefore = Context->PostDisplayInformation.Width != 0u;
  status = AdmissionStopDeviceBody(Context);
  Event->Status = (ULONG)status;
  Event->BrokerReleaseResult = (ULONG)status;
  AdmissionRecordStopReceipt(pdo, Event);
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiStopDevice(PVOID MiniportDeviceContext) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  ADMISSION_STOP_EVENT event;
  if (context == NULL)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(&event, sizeof(event));
  event.Caller = ADMISSION_STOP_CALLER_STOP;
  event.FillResult = ADMISSION_STOP_FILL_NA;
  event.SurfaceSource = context->PostDisplayInformation.Width != 0u
                            ? ADMISSION_STOP_SOURCE_POST : ADMISSION_STOP_SOURCE_NONE;
  return AdmissionStopDeviceInternal(context, &event);
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiRemoveDevice(PVOID MiniportDeviceContext) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  if (context == NULL)
    return STATUS_INVALID_PARAMETER;
  {
    ADMISSION_STOP_EVENT removeEvent;
    RtlZeroMemory(&removeEvent, sizeof(removeEvent));
    removeEvent.Caller = ADMISSION_STOP_CALLER_REMOVE;
    removeEvent.FillResult = ADMISSION_STOP_FILL_NA;
    removeEvent.SurfaceSource = ADMISSION_STOP_SOURCE_NONE;
    AdmissionRecordStopReceipt(context->PhysicalDeviceObject, &removeEvent);
  }
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (!NT_SUCCESS(AdmissionGpuvaG3Stop(context))) return STATUS_DEVICE_BUSY;
#endif
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
  if (context->GpuvaB1State != NULL)
    return STATUS_DEVICE_BUSY;
#endif
  AdmissionFlushSourceAddressReceipt(context);
  AdmissionFlushPresentTransfer(context);
  AdmissionFlushGdiReceipt(context);
  if (context->ScanoutRuntime != NULL &&
      !NT_SUCCESS(AdmissionScanoutStop(context)))
    return STATUS_DEVICE_BUSY;
  if (context->PagingWorkItem != NULL &&
      !NT_SUCCESS(AdmissionPagingStop(context)))
    return STATUS_DEVICE_BUSY;
  if (context->PlatformRuntime != NULL &&
      !NT_SUCCESS(AdmissionPlatformRuntimeStop(context)))
    return STATUS_DEVICE_BUSY;
  if (InterlockedCompareExchange(&context->SchedulerInitialized, 0, 0) != 0 &&
      !NT_SUCCESS(AdmissionSchedulerStop(context)))
    return STATUS_DEVICE_BUSY;
  if (context->BackendImage.Ready == APPLE_AGX_TRUE &&
      !NT_SUCCESS(AdmissionBackendImageStop(context)))
    return STATUS_DEVICE_BUSY;
  if (context->MemoryRuntime != NULL &&
      !NT_SUCCESS(AdmissionMemoryRuntimeStop(context)))
    return STATUS_DEVICE_BUSY;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  AdmissionMemoryPoolStop(context);
#endif
  AdmissionStartPolicyRelease(context);
  AdmissionRecordDevice(context->PhysicalDeviceObject, AdmissionReceiptRemove,
                        STATUS_SUCCESS);
  ExFreePoolWithTag(context, ADMISSION_POOL_TAG);
  return STATUS_SUCCESS;
}

/* EXP1225: whether the installed package registered a WOW64 user-mode
 * driver (the INF's HKR UserModeDriverNameWow, in the driver key). Runs at
 * PASSIVE_LEVEL from DxgkDdiQueryAdapterInfo; any failure reads as "not
 * registered", which keeps the adapter 64-bit-only. */
static int AdmissionReadWow64Umd(PDEVICE_OBJECT PhysicalDeviceObject) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  KEY_VALUE_PARTIAL_INFORMATION probe;
  PKEY_VALUE_PARTIAL_INFORMATION value = NULL;
  ULONG bytes = 0;
  NTSTATUS status;
  int named = 0;
  PAGED_CODE();
  if (PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DRIVER, KEY_READ, &key)))
    return 0;
  RtlInitUnicodeString(&name, L"UserModeDriverNameWow");
  status = ZwQueryValueKey(key, &name, KeyValuePartialInformation, &probe,
                           sizeof(probe), &bytes);
  if ((status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL) &&
      bytes > sizeof(probe) && bytes <= 0x4000u) {
    value = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePool2(
        POOL_FLAG_PAGED, bytes, ADMISSION_POOL_TAG);
    if (value != NULL &&
        NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                   value, bytes, &bytes)) &&
        FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + value->DataLength <= bytes)
      named = AdmissionWow64UmdNamed(value->Type, value->Data, value->DataLength);
    if (value != NULL)
      ExFreePoolWithTag(value, ADMISSION_POOL_TAG);
  }
  ZwClose(key);
  return named;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQueryAdapterInfo(
    HANDLE Adapter, const DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  NTSTATUS status = STATUS_NOT_SUPPORTED;
  LONG drvCapsZeroed = 0;
  if (context == NULL || QueryAdapterInfo == NULL)
    return STATUS_INVALID_PARAMETER;

  switch (QueryAdapterInfo->Type) {
  case DXGKQAITYPE_UMDRIVERPRIVATE: {
    AGX_WIN32_DEVICE_INFO *info;
    if (QueryAdapterInfo->pInputData != NULL ||
        QueryAdapterInfo->InputDataSize != 0u ||
        QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize != sizeof(*info)) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    info = (AGX_WIN32_DEVICE_INFO *)QueryAdapterInfo->pOutputData;
    RtlZeroMemory(info, sizeof(*info));
    info->Magic = AGX_WIN32_DEVICE_INFO_MAGIC;
    info->Version = AGX_WIN32_DEVICE_INFO_VERSION;
    info->Bytes = sizeof(*info);
    info->BootGeneration = context->Win32BootGeneration;
    info->GpuGeneration = 13u;
    info->GpuVariant = AgxWin32GpuG13G;
    info->PageBytes = 0x4000u;
    info->ClassCount = AGX_WIN32_BUFFER_CLASS_COUNT;
    info->Classes[0].ClassId = AgxWin32BufferClassGeneral;
    info->Classes[0].MinimumAlignment = 0x4000u;
    /* M2-f7: the segment General BOs live in, not the legacy arena. */
    info->Classes[0].MaximumBytes = AdmissionGeneralMaximumBytes(context);
    info->Classes[0].Flags = AppleAgxWin32BufferCpuRead |
        AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead |
        AppleAgxWin32BufferGpuWrite | AppleAgxWin32BufferGpuOnly;
    info->Classes[1].ClassId = AgxWin32BufferClassShader;
    info->Classes[1].MinimumAlignment = 0x4000u;
    info->Classes[1].MaximumBytes = AGX_RR_SHARED_ARENA_BYTES;
    info->Classes[1].Flags = AppleAgxWin32BufferCpuWrite |
        AppleAgxWin32BufferGpuRead;
    info->Classes[2].ClassId = AgxWin32BufferClassEncoder;
    info->Classes[2].MinimumAlignment = 0x4000u;
    info->Classes[2].MaximumBytes = AGX_RR_COMMAND_ARENA_BYTES;
    info->Classes[2].Flags = AppleAgxWin32BufferCpuWrite |
        AppleAgxWin32BufferGpuRead;
    /* EXP1317: 0/0 until the first power-on read a valid topology. */
    info->CoresPerCluster = context->GpuCoreMask ? context->GpuCoresPerCluster : 0u;
    info->CoreMask = context->GpuCoreMask;
    /* rc82: Wom1SoftFaults not 0 and RegionC fault_control 0xb. */
    info->Features = InterlockedCompareExchange(&context->GpuSoftFaults, 0, 0) != 0
        ? AGX_WIN32_DEVICE_FEATURE_SOFT_FAULTS : 0u;
    /* OV2: Wom1Ov2NoHazardLead = 1 with Q2c (backend start). */
    if (InterlockedCompareExchange(&context->GpuIndependentSubqueue, 0, 0) != 0)
      info->Features |= AGX_WIN32_DEVICE_FEATURE_INDEPENDENT_SUBQUEUE;
    /* N4c-v2: Wom1Ov2CrossLead = 1 with Q2c (backend start). */
    if (InterlockedCompareExchange(&context->GpuOv2CrossLead, 0, 0) != 0)
      info->Features |= AGX_WIN32_DEVICE_FEATURE_OV2_CROSS_LEAD;
    /* C2a: Wom1CrossProcessPrivateLead = 1 with C1 and Q2c (backend start). */
    if (InterlockedCompareExchange(&context->GpuPrivateOnlyCompute, 0, 0) != 0)
      info->Features |= AGX_WIN32_DEVICE_FEATURE_PRIVATE_ONLY_COMPUTE;
    /* OV5 step 2: Wom1Ov5TaWaiver = 1 with the 3D-queue Barrier path. */
    if (InterlockedCompareExchange(&context->GpuRenderTaIndependent, 0, 0) != 0)
      info->Features |= AGX_WIN32_DEVICE_FEATURE_RENDER_TA_INDEPENDENT;
    /* S2: Wom1MpoTwoPlanes effective (broker bit 9 + V3 read back at start). */
    if (AdmissionScanoutMpoEffective(context))
      info->Features |= AGX_WIN32_DEVICE_FEATURE_MPO_TWO_PLANES;
    /* STRETCH-1: and broker bit 8 (scaled layer records). */
    if (AdmissionScanoutMpoScaledSource(context))
      info->Features |= AGX_WIN32_DEVICE_FEATURE_MPO_STRETCH;
    status = STATUS_SUCCESS;
    break;
  }

  case DXGKQAITYPE_DRIVERCAPS: {
    DXGK_DRIVERCAPS *caps;
    APPLE_AGX_WDDM_FEATURE_INPUT featureInput;
    APPLE_AGX_WDDM_FEATURE_OUTPUT featureOutput;
    APPLE_AGX_WDDM_FEATURE_CONTRACT_RESULT featureResult;
    if (QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*caps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      caps = (DXGK_DRIVERCAPS *)QueryAdapterInfo->pOutputData;
      RtlZeroMemory(caps, sizeof(*caps));
      caps->HighestAcceptableAddress.QuadPart = -1;
      /* WDK 26100: must match DXGK_WDDMDEVICECAPS.WDDMVersion. */
      caps->WDDMVersion = ADMISSION_G1B_WDDM_VERSION;
      RtlZeroMemory(&featureInput, sizeof(featureInput));
      featureInput.Version = APPLE_AGX_WDDM_FEATURE_CONTRACT_VERSION;
      featureInput.Size = sizeof(featureInput);
      featureInput.WddmMajor = 3u;
      featureInput.WddmMinor = 0u;
      featureInput.NodeCount = context->LiveAdditivePaging ? 2u : 1u;
      featureInput.ReadyMask = (ULONG)InterlockedCompareExchange(
          &context->FeatureReadyMask, 0, 0);
      featureResult = AppleAgxWddmFeatureContractEvaluate(
          &featureInput, &featureOutput);
      if (featureResult == AppleAgxWddmFeatureContractIncomplete &&
          featureOutput.PublishCapsMask == 0u) {
        drvCapsZeroed = 1;
        status = STATUS_SUCCESS;
      } else if (featureResult == AppleAgxWddmFeatureContractReady &&
                 featureOutput.PublishCapsMask ==
                     APPLE_AGX_WDDM_MANDATORY_CAPS_MASK) {
        caps->MaxAllocationListSlotId =
            ADMISSION_GDI_ALLOCATION_LIST_SIZE - 1u;
        caps->MaxQueuedFlipOnVSync = 1u;
        caps->GpuEngineTopology.NbAsymetricProcessingNodes = 1u;
        caps->SchedulingCaps.MultiEngineAware = 1u;
        caps->SchedulingCaps.PreemptionAware = 1u;
        /* Q8: a kicked successor still owns a pending entry and the
         * context's second fence. Bound VidSch's entire node to the two
         * packets we can retain, including active/kicked work. This is a
         * DMA packet limit, not MaxQueuedFlipOnVSync or a HWQueue count.
         * General absent preserves the zeroed legacy caps byte for byte. */
        if (AdmissionPlatformRuntimeGeneralDepth2(context))
          caps->SchedulingCaps.HwQueuePacketCap = 2u;
        caps->PreemptionCaps.GraphicsPreemptionGranularity =
            D3DKMDT_GRAPHICS_PREEMPTION_DMA_BUFFER_BOUNDARY;
        caps->PreemptionCaps.ComputePreemptionGranularity =
            D3DKMDT_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY;
        caps->FlipCaps.FlipOnVSyncMmIo = 1u;
        caps->FlipCaps.FlipIndependent = 1u;
        caps->SupportNonVGA = TRUE;
        /* WDDM 1.2+ full graphics requires this even when the only exposed
         * path transform is the already-supported identity/Offset0 pair. */
        caps->SupportSmoothRotation = TRUE;
        caps->SupportPerEngineTDR = TRUE;
        caps->SupportDirectFlip = TRUE;
        /* S2 ATOMIC CONTRACT (specification v1): only while the
         * Wom1MpoTwoPlanes switch is effective (switch, broker bit 9 and
         * the V3 negotiation read back).  Field names and members: WDK
         * 26100 d3dkmddi.h DXGK_DRIVERCAPS (SupportMultiPlaneOverlay,
         * MaxOverlayPlanes, SupportMultiPlaneOverlayImmediateFlip,
         * CursorScaledWithMultiPlaneOverlayPlane0,
         * MaxQueuedMultiPlaneOverlayFlipVSync).  SupportDirectFlip stays
         * TRUE (dxgkrnl refuses MPO without it, R22). */
        if (AdmissionScanoutMpoEffective(context)) {
          caps->SupportMultiPlaneOverlay = TRUE;
          caps->MaxOverlayPlanes = 2u;
          caps->SupportMultiPlaneOverlayImmediateFlip = FALSE;
          caps->CursorScaledWithMultiPlaneOverlayPlane0 = FALSE;
          caps->MaxQueuedMultiPlaneOverlayFlipVSync = 1u;
        }
        /* The current RenderKm path implements ColorFill only and the GPU
         * aperture is not declared cache-coherent.  Keep CDD GDI command
         * buffers disabled until both requirements are met. */
        caps->PresentationCaps.SupportKernelModeCommandBuffer = 0u;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
        if (context->GpuvaG3State != NULL) {
          APPLE_AGX_GPUVA_G3_ADMISSION_CONTRACT model =
              AppleAgxGpuvaG3AdmissionContract(
                  1u, ADMISSION_GPUVA_G1B_PAGE_PROFILE);
          if (!AdmissionGpuvaG3DeclarationReady(context)) {
            status = STATUS_INVALID_DEVICE_STATE;
            break;
          }
          caps->MemoryManagementCaps.VirtualAddressingSupported =
              model.VirtualAddressingSupported;
          caps->MemoryManagementCaps.GpuMmuSupported =
              model.AdapterGpuMmuSupported;
          caps->MemoryManagementCaps.PagingNode = context->LiveAdditivePaging ? 1u : model.PagingNode;
          caps->GpuEngineTopology.NbAsymetricProcessingNodes = context->LiveAdditivePaging ? 2u : model.NodeCount;
        }
#endif
        status = STATUS_SUCCESS;
      } else {
        status = STATUS_INVALID_DEVICE_STATE;
      }
    }
    break;
  }

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  case DXGKQAITYPE_GPUMMUCAPS: {
    const DXGK_QUERYGPUMMUCAPSIN *input;
    DXGK_GPUMMUCAPS *caps;
    APPLE_AGX_GPUVA_G3_ADMISSION_CONTRACT declaration =
        AppleAgxGpuvaG3AdmissionContract(1u, ADMISSION_GPUVA_G1B_PAGE_PROFILE);
    unsigned int table64 = declaration.LocalUse64KBPages ||
                           declaration.SysMem64KBPageSupported;
    APPLE_AGX_GPUVA_G3_CAPS model = AppleAgxGpuvaG3Caps(table64);
    if (!AdmissionGpuvaG3DeclarationReady(context)) {
      status = STATUS_INVALID_DEVICE_STATE;
    } else if (QueryAdapterInfo->pInputData == NULL ||
        QueryAdapterInfo->InputDataSize < sizeof(*input) ||
        QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*caps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      input = (const DXGK_QUERYGPUMMUCAPSIN *)QueryAdapterInfo->pInputData;
      caps = (DXGK_GPUMMUCAPS *)QueryAdapterInfo->pOutputData;
      if (input->PhysicalAdapterIndex != 0u ||
          !AppleAgxGpuvaG3CapsValid(&model, sizeof(DXGK_PTE),
                                   table64, 0u)) {
        status = STATUS_INVALID_PARAMETER;
      } else {
        RtlZeroMemory(caps, sizeof(*caps));
        caps->ReadOnlyMemorySupported = 1u;
        caps->SysMem64KBPageSupported = declaration.SysMem64KBPageSupported;
        caps->ExplicitPageTableInvalidation = 1u;
        /* EXP986: VidMm must not idle the process for table updates. The
         * KMD serialises UpdatePageTable/FlushTlb with an in-flight job
         * itself (R155/R165); requiring an idle address space deferred PTEs
         * past the MakeResident fence (EXP985) and stalled paging 10 s. */
        caps->PageTableUpdateRequireAddressSpaceIdle = 0u;
        caps->PageTableUpdateMode = DXGK_PAGETABLEUPDATE_GPU_PHYSICAL;
        caps->VirtualAddressBitCount = model.VirtualAddressBits;
        caps->LeafPageTableSizeFor64KPagesInBytes = model.Leaf64KBytes;
        caps->PageTableLevelCount = model.LevelCount;
        status = STATUS_SUCCESS;
      }
    }
    break;
  }

  case DXGKQAITYPE_PAGETABLELEVELDESC: {
    const DXGK_QUERYPAGETABLELEVELDESCIN *input;
    DXGK_PAGE_TABLE_LEVEL_DESC *level;
    APPLE_AGX_GPUVA_G3_ADMISSION_CONTRACT declaration =
        AppleAgxGpuvaG3AdmissionContract(1u, ADMISSION_GPUVA_G1B_PAGE_PROFILE);
    unsigned int table64 = declaration.LocalUse64KBPages ||
                           declaration.SysMem64KBPageSupported;
    APPLE_AGX_GPUVA_G3_CAPS model = AppleAgxGpuvaG3Caps(table64);
    if (!AdmissionGpuvaG3DeclarationReady(context)) {
      status = STATUS_INVALID_DEVICE_STATE;
    } else if (QueryAdapterInfo->pInputData == NULL ||
        QueryAdapterInfo->InputDataSize < sizeof(*input) ||
        QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*level)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      input = (const DXGK_QUERYPAGETABLELEVELDESCIN *)
          QueryAdapterInfo->pInputData;
      level = (DXGK_PAGE_TABLE_LEVEL_DESC *)QueryAdapterInfo->pOutputData;
      if (input->PhysicalAdapterIndex != 0u ||
          input->LevelIndex >= model.LevelCount ||
          !AppleAgxGpuvaG3CapsValid(&model, sizeof(DXGK_PTE),
                                   table64, 0u)) {
        status = STATUS_INVALID_PARAMETER;
      } else {
        RtlZeroMemory(level, sizeof(*level));
        level->PageTableIndexBitCount =
            model.Level[input->LevelIndex].IndexBits;
        level->PageTableSegmentId = model.Level[input->LevelIndex].SegmentId;
        level->PagingProcessPageTableSegmentId =
            model.Level[input->LevelIndex].SegmentId;
        level->PageTableSizeInBytes = model.Level[input->LevelIndex].SizeBytes;
        level->PageTableAlignmentInBytes =
            model.Level[input->LevelIndex].AlignmentBytes;
        status = STATUS_SUCCESS;
      }
    }
    break;
  }
#endif

  case DXGKQAITYPE_WDDMDEVICECAPS: {
    DXGK_WDDMDEVICECAPS *caps;
    if (QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*caps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      caps = (DXGK_WDDMDEVICECAPS *)QueryAdapterInfo->pOutputData;
      RtlZeroMemory(caps, sizeof(*caps));
      caps->WDDMVersion = ADMISSION_G1B_WDDM_VERSION;
      status = STATUS_SUCCESS;
    }
    break;
  }

  case DXGKQAITYPE_QUERYSEGMENT4:
    status = AdmissionDdiQuerySegment4(context, QueryAdapterInfo);
    AdmissionRecordQuerySegment4(context->PhysicalDeviceObject,
                                 context->Win32BootGeneration,
                                 QueryAdapterInfo, status);
    break;

#if ADMISSION_GPUVA_G1B_PAGE_PROFILE != 0
  case DXGKQAITYPE_QUERYPAGINGBUFFERINFO: {
    const DXGK_QUERYPAGINGBUFFERINFOIN *input;
    DXGK_QUERYPAGINGBUFFERINFOOUT *output;
    if (QueryAdapterInfo->pInputData == NULL ||
        QueryAdapterInfo->InputDataSize < sizeof(*input) ||
        QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*output)) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    input = (const DXGK_QUERYPAGINGBUFFERINFOIN *)QueryAdapterInfo->pInputData;
    output = (DXGK_QUERYPAGINGBUFFERINFOOUT *)QueryAdapterInfo->pOutputData;
    if (input->PhysicalAdapterIndex != 0u ||
        !AdmissionMemoryReady(&context->Memory)) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    output->PagingBufferSize =
        (UINT32)context->Memory.Topology.PagingBufferSize;
    output->PagingBufferPrivateDataSize = (UINT)ADMISSION_PAGING_PRIVATE_BYTES;
    status = STATUS_SUCCESS;
    break;
  }
  case DXGKQAITYPE_QUERYSEGMENTCOUNT: {
    const DXGK_QUERYSEGMENTCOUNTIN *input;
    DXGK_QUERYSEGMENTCOUNTOUT *output;
    if (QueryAdapterInfo->pInputData == NULL ||
        QueryAdapterInfo->InputDataSize < sizeof(*input) ||
        QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*output)) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    input = (const DXGK_QUERYSEGMENTCOUNTIN *)QueryAdapterInfo->pInputData;
    output = (DXGK_QUERYSEGMENTCOUNTOUT *)QueryAdapterInfo->pOutputData;
    if (input->PhysicalAdapterIndex != 0u || input->Reserved != 0u ||
        !AdmissionMemoryReady(&context->Memory)) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    RtlZeroMemory(output, sizeof(*output));
    output->SegmentCount = (UINT16)context->Memory.Topology.SegmentCount;
    status = STATUS_SUCCESS;
    break;
  }
  case DXGKQAITYPE_QUERYSEGMENT5:
    status = AdmissionDdiQuerySegment5(context, QueryAdapterInfo);
    break;
  case DXGKQAITYPE_QUERYMMUCOUNT: {
    const DXGK_QUERYMMUCOUNTIN *input;
    DXGK_QUERYMMUCOUNTOUT *output;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    APPLE_AGX_GPUVA_G3_ADMISSION_CONTRACT model =
        AppleAgxGpuvaG3AdmissionContract(context->GpuvaG3State != NULL,
                                         ADMISSION_GPUVA_G1B_PAGE_PROFILE);
#endif
    if (QueryAdapterInfo->pInputData == NULL ||
        QueryAdapterInfo->InputDataSize < sizeof(*input) ||
        QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*output)) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    input = (const DXGK_QUERYMMUCOUNTIN *)QueryAdapterInfo->pInputData;
    output = (DXGK_QUERYMMUCOUNTOUT *)QueryAdapterInfo->pOutputData;
    if (input->PhysicalAdapterIndex != 0u || input->Reserved != 0u) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    if (!AppleAgxGpuvaG3AdmissionContractValid(&model, sizeof(DXGK_PTE)) ||
        (model.Enabled && !AdmissionGpuvaG3DeclarationReady(context))) {
      status = STATUS_INVALID_DEVICE_STATE;
      break;
    }
#endif
    RtlZeroMemory(output, sizeof(*output));
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    output->MmuCount = (UINT16)model.MmuCount;
#else
    output->MmuCount = 0u;
#endif
    status = STATUS_SUCCESS;
    break;
  }
  case DXGKQAITYPE_QUERYMMUS: {
    const DXGK_QUERYMMUSIN *input;
    DXGK_QUERYMMUSOUT *output;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    APPLE_AGX_GPUVA_G3_ADMISSION_CONTRACT model =
        AppleAgxGpuvaG3AdmissionContract(context->GpuvaG3State != NULL,
                                         ADMISSION_GPUVA_G1B_PAGE_PROFILE);
#endif
    if (QueryAdapterInfo->pInputData == NULL ||
        QueryAdapterInfo->InputDataSize < sizeof(*input) ||
        QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*output)) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    input = (const DXGK_QUERYMMUSIN *)QueryAdapterInfo->pInputData;
    output = (DXGK_QUERYMMUSOUT *)QueryAdapterInfo->pOutputData;
    if (input->PhysicalAdapterIndex != 0u) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    if (!AppleAgxGpuvaG3AdmissionContractValid(&model, sizeof(DXGK_PTE)) ||
        (model.Enabled && !AdmissionGpuvaG3DeclarationReady(context))) {
      status = STATUS_INVALID_DEVICE_STATE;
      break;
    }
    if (model.MmuCount != 0u && output->MmuDescriptors == NULL) {
      status = STATUS_BUFFER_TOO_SMALL;
      break;
    }
#endif
    /* The caller owns MmuDescriptors; clearing the output would lose it. */
    output->Reserved0 = 0u;
    RtlZeroMemory(output->Reserved, sizeof(output->Reserved));
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    if (model.MmuCount != 0u) {
      RtlZeroMemory(&output->MmuDescriptors[0], sizeof(DXGK_MMUDESCRIPTOR));
      output->MmuDescriptors[0].Size = model.MmuSizeBytes;
    }
    output->DisplayMmuId = (UINT16)model.DisplayMmuId;
#else
    output->DisplayMmuId = DXGK_INVALID_MMU_ID;
#endif
    status = STATUS_SUCCESS;
    break;
  }
#endif

  case DXGKQAITYPE_PHYSICAL_MEMORY_CAPS: {
    DXGK_PHYSICAL_MEMORY_CAPS *physicalMemoryCaps;
    if (QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*physicalMemoryCaps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      physicalMemoryCaps =
          (DXGK_PHYSICAL_MEMORY_CAPS *)QueryAdapterInfo->pOutputData;
      RtlZeroMemory(physicalMemoryCaps, sizeof(*physicalMemoryCaps));
      physicalMemoryCaps->HighestVisibleAddress.QuadPart = 0xFFFFFFFFFFLL;
      status = STATUS_SUCCESS;
    }
    break;
  }

  case DXGKQAITYPE_IOMMU_CAPS: {
    DXGK_IOMMU_CAPS *iommuCaps;
    if (QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*iommuCaps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      iommuCaps = (DXGK_IOMMU_CAPS *)QueryAdapterInfo->pOutputData;
      RtlZeroMemory(iommuCaps, sizeof(*iommuCaps));
      iommuCaps->Value = 0;
      status = STATUS_SUCCESS;
    }
    break;
  }

  case DXGKQAITYPE_64BITONLYCAPS: {
    DXGK_64_BIT_ONLY_CAPS *only64Caps;
    if (QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*only64Caps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      only64Caps = (DXGK_64_BIT_ONLY_CAPS *)QueryAdapterInfo->pOutputData;
      RtlZeroMemory(only64Caps, sizeof(*only64Caps));
      /* dxgkrnl hides a 64-bit-only adapter from WOW64 enumeration. */
      only64Caps->SupportsOnly64Bit = AdmissionWow64Only64Bit(
          AdmissionReadWow64Umd(context->PhysicalDeviceObject));
      status = STATUS_SUCCESS;
    }
    break;
  }

  case DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION: {
    DXGK_DISPLAY_DRIVERCAPS_EXTENSION *displayCaps;
    if (QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*displayCaps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      displayCaps = (DXGK_DISPLAY_DRIVERCAPS_EXTENSION *)
          QueryAdapterInfo->pOutputData;
      RtlZeroMemory(displayCaps, sizeof(*displayCaps));
      status = STATUS_SUCCESS;
    }
    break;
  }

  default:
    status = STATUS_NOT_SUPPORTED;
    break;
  }
  AdmissionRecordQuery(context->PhysicalDeviceObject, QueryAdapterInfo->Type,
                       QueryAdapterInfo->OutputDataSize, status,
                       QueryAdapterInfo->pOutputData);
  /* S2-1b: per-type counts, refusals, and every DRIVERCAPS answer. */
  {
    const ULONG type = (ULONG)QueryAdapterInfo->Type;
    const ULONG slot = type < ADMISSION_MPO_GATE_QUERY_TYPES - 1u
                           ? type : ADMISSION_MPO_GATE_QUERY_TYPES - 1u;
    InterlockedIncrement(&context->MpoGate.QueryTypeCounts[slot]);
    if (!NT_SUCCESS(status))
      InterlockedOr(&context->MpoGate.QueryRefusalBitmap[slot / 32u],
                    (LONG)(1u << (slot % 32u)));
    if (QueryAdapterInfo->Type == DXGKQAITYPE_DRIVERCAPS) {
      ADMISSION_MPO_GATE_CAPS answer;
      const DXGK_DRIVERCAPS *caps =
          QueryAdapterInfo->pOutputData != NULL &&
                  QueryAdapterInfo->OutputDataSize >= sizeof(DXGK_DRIVERCAPS)
              ? (const DXGK_DRIVERCAPS *)QueryAdapterInfo->pOutputData
              : NULL;
      RtlZeroMemory(&answer, sizeof(answer));
      answer.Valid = 1;
      answer.Status = (LONG)status;
      answer.Zeroed = drvCapsZeroed;
      answer.Qpc = AdmissionMpoGateQpc(context);
      if (caps != NULL) {
        answer.Mpo = caps->SupportMultiPlaneOverlay ? 1 : 0;
        answer.DirectFlip = caps->SupportDirectFlip ? 1 : 0;
        answer.MaxOverlayPlanes = (LONG)caps->MaxOverlayPlanes;
        answer.WddmVersion = (LONG)caps->WDDMVersion;
      }
      if (InterlockedIncrement(&context->MpoGate.DrvCapsQueryCount) == 1)
        context->MpoGate.FirstDrvCaps = answer;
      context->MpoGate.LastDrvCaps = answer;
      AdmissionRecordMpoGate(context);
    }
  }
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  AdmissionRecordGpuvaG3Query(context->PhysicalDeviceObject,
                              QueryAdapterInfo, status,
                              context->GpuvaG3State == NULL ? 0u : 1u);
#endif
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiDispatchIoRequest(
    PVOID MiniportDeviceContext, ULONG VidPnSourceId,
    PVIDEO_REQUEST_PACKET VideoRequestPacket) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(VidPnSourceId);
  UNREFERENCED_PARAMETER(VideoRequestPacket);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiSetPowerState(
    PVOID MiniportDeviceContext, ULONG DeviceUid,
    DEVICE_POWER_STATE DevicePowerState, POWER_ACTION ActionType) {
  /* F1K-STAMP: no start-policy write here. dxgkrnl calls this for the S5
   * pass after CmShutdownSystem closed the registry: every registry call
   * returns STATUS_TOO_LATE and nothing persists (EXP2405). The next start
   * derives the clean exit from Windows' ShutdownTime instead. */
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(DeviceUid);
  UNREFERENCED_PARAMETER(DevicePowerState);
  UNREFERENCED_PARAMETER(ActionType);
  return STATUS_SUCCESS;
}

VOID AdmissionDdiResetDevice(PVOID MiniportDeviceContext) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
}

VOID AdmissionDdiUnload(VOID) {
#ifdef APPLE_AGX_NATIVE_STAGING
  AdmissionNativeStagingUninitialize();
#endif
}

/* F1-k: the registered DxgkDdiStartDevice. AdmissionDdiStartDevice is
 * unchanged apart from its policy gate; this adds the return handling of a
 * start the policy allowed (error: clear the markers; success: arm the
 * healthy-start timer). Policy 0 starts never set PendingOwned. */
_Use_decl_annotations_ NTSTATUS AdmissionDdiStartDeviceGated(
    PVOID MiniportDeviceContext, PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface, PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren) {
  NTSTATUS status = AdmissionDdiStartDevice(MiniportDeviceContext,
      DxgkStartInfo, DxgkInterface, NumberOfVideoPresentSources,
      NumberOfChildren);
  AdmissionStartPolicyReturned((ADMISSION_CONTEXT *)MiniportDeviceContext,
                               status);
  return status;
}
