#include "render_admission.h"
#include "render_job_timing.h"

/* Fault diagnostics: first nonzero transition stores file tag 4 and
 * source line. Consumers retain zero/nonzero semantics; reset clears it. */
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
#include "gpuva_g3_private.h"
#endif
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
#include "apple_agx_gpuva_b1_submission.h"
#include "apple_agx_gpuva_b1_completion.h"
#endif
#include "apple_agx_hwdata_profile.h"
#include "apple_agx_firmware_start_receipt.h"

#define ADMISSION_PLATFORM_TAG 'pRGA'
#define ADMISSION_PLATFORM_QUEUE_TIMEOUT_MS 500ULL
#define ADMISSION_OUTPUT_CAPTURE_CHUNK_BYTES 0x40000u
#define ADMISSION_PLATFORM_DEVICE_CONTROL_STALL_US 50u
#define ADMISSION_QUEUE_FAULT_SNAPSHOT_DELAY_MS 50ULL
#define ADMISSION_TA_TEMPORAL_SECOND_DELAY_MS 100ULL
#define ADMISSION_REGIONC_FAULT_INFO_OFFSET 0x11a2cu
#define ADMISSION_PLATFORM_INITDATA_ADDRESS_MASK ((1ULL << 44u) - 1ULL)
#define ADMISSION_QUEUE_OBJECT_D3_INFO 3u
#define ADMISSION_QUEUE_OBJECT_TA_INFO 6u
#define ADMISSION_QUEUE_OBJECT_D3_POINTERS 24u
#define ADMISSION_QUEUE_OBJECT_TA_POINTERS 25u
#define ADMISSION_QUEUE_OBJECT_BUFFER_MANAGER_INFO 1u
#define ADMISSION_QUEUE_OBJECT_BUFFER_MANAGER_CONTROL 20u
#define ADMISSION_QUEUE_OBJECT_BUFFER_MANAGER_COUNTER 21u
#define ADMISSION_QUEUE_OBJECT_BUFFER_MANAGER_MISC 22u
#define ADMISSION_QUEUE_OBJECT_EVENT_CONTROL 11u
#define ADMISSION_QUEUE_OBJECT_EVENT_COUNT 12u
#define ADMISSION_QUEUE_OBJECT_INITBM 16u
#define ADMISSION_QUEUE_OBJECT_TA_MICROSEQUENCE 17u
#define ADMISSION_QUEUE_OBJECT_TA_WORK 19u
#define ADMISSION_QUEUE_OBJECT_TA_STAMP2 9u
#define ADMISSION_QUEUE_OBJECT_TA_STAMP1 26u
#define ADMISSION_QUEUE_OBJECT_TA_TIMESTAMP_START 30u
#define ADMISSION_QUEUE_OBJECT_TA_TIMESTAMP_END 31u
#define ADMISSION_QUEUE_OBJECT_TA_USER_TIMESTAMP_START 34u
#define ADMISSION_QUEUE_OBJECT_TA_USER_TIMESTAMP_END 35u
#define ADMISSION_QUEUE_OBJECT_JOB_LIST 23u
#define ADMISSION_TA_WORK_TIMESTAMP_TAIL_OFFSET 0x5b4u
#define ADMISSION_TA_STATS_TIMESTAMPS_OFFSET 0x5d0u
#define ADMISSION_REGIONC_PENDING_STAMPS_OFFSET 0x111a8u
#define ADMISSION_TA_MICROSEQUENCE_START_OFFSET 0x0u
#define ADMISSION_TA_MICROSEQUENCE_TIMESTAMP_START_OFFSET 0x18cu
#define ADMISSION_TA_MICROSEQUENCE_WAIT_OFFSET 0x1c8u
#define ADMISSION_TA_MICROSEQUENCE_TIMESTAMP_END_OFFSET 0x1ccu
#define ADMISSION_TA_MICROSEQUENCE_FINALIZE_OFFSET 0x208u
#define ADMISSION_TA_MICROSEQUENCE_RETIRE_OFFSET 0x28cu
#define ADMISSION_CHANNEL_OBJECT_KTRACE_STATE 31u
#define ADMISSION_CHANNEL_OBJECT_KTRACE_RING 32u
#define ADMISSION_KTRACE_RING_ENTRIES 0x200u
#define ADMISSION_PLATFORM_SGX_PRE_ASC_OFFSET 0xd14000u
#define ADMISSION_PLATFORM_SGX_PRE_ASC_VALUE 0x00070001u
#define ADMISSION_PLATFORM_SGX_FAULT_INFO_OFFSET 0x17030u
#define ADMISSION_PLATFORM_CONFIG_WINDOW_BYTES                              \
  (APPLE_AGX_CONFIG_MMIO_OFFSET + APPLE_AGX_CONFIG_WIRE_SIZE)

C_ASSERT(J313_AGX_ABI_ADMISSION_SYNTHETIC_SCANOUT_GUEST_INTID == 889u);
C_ASSERT((ADMISSION_PLATFORM_CONFIG_WINDOW_BYTES % sizeof(ULONG)) == 0u);
C_ASSERT(sizeof(ADMISSION_DYNAMIC_STORE_RECEIPT) ==
         ADMISSION_DYNAMIC_STORE_RECEIPT_BYTES);

typedef struct _ADMISSION_ASC_TRANSPORT {
  volatile UCHAR *Base;
  ULONG Length;
  struct { ULONG Offset, Write; ULONGLONG Value; } Trace[64];
  ULONG TraceCount;
} ADMISSION_ASC_TRANSPORT;

/* One platform worker owns this receipt. Odd Sequence means the existing
 * heartbeat is in progress; an even Sequence publishes its exact result.
 * Diagnostic memory only: no additional mailbox operation or retry. */
typedef struct _ADMISSION_HEARTBEAT_RECEIPT {
  volatile LONG Sequence;
  ULONG Calls, Fence, Result;
  ULONGLONG StartMs, EndMs, DeadlineMs;
  ULONG RxBefore, RxAfter;
  ULONGLONG LastRxPayload;
  ULONG LastRxEndpoint, Reserved;
} ADMISSION_HEARTBEAT_RECEIPT;

C_ASSERT(sizeof(ADMISSION_HEARTBEAT_RECEIPT) == 64u);

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
/* EXP1112 receipt-only: what the firmware wrote per job, to split the
 * ~330 us from the TA kick to the TA end timestamp (EXP1100-EXP1111) into
 * firmware dispatch and GPU work: the TA work command's timestamp tail, the
 * TA stats timestamps, and the eight timestamp objects 28..35. */
#define ADMISSION_FW_TIMING_CAPACITY 16u
#define ADMISSION_FW_TIMING_VERSION 2u
#define ADMISSION_TA_WORK_TS1_OFFSET 0x5e4u
typedef struct _ADMISSION_FW_TIMING_ENTRY {
  ULONG Fence, Ts1Polls;
  ULONGLONG KickQpc, CompleteQpc;
  /* EXP1113: first changed TA ts1 word seen while polling, and its QPC. */
  ULONGLONG Ts1Kick, Ts1First, Ts1FirstQpc;
  ULONGLONG Timestamps[8];
  UCHAR TaTail[0x68];
  UCHAR TaStats[0x80];
} ADMISSION_FW_TIMING_ENTRY;
typedef struct _ADMISSION_FW_TIMING_RING {
  /* Reserved (multi-job phase 1, receipt-only): builds whose firmware stamp
   * words did not hold the expected previous values, low 16 bits, and the
   * mismatching objects of the last one (TA2, 3D2, TA1, 3D1) << 16. */
  ULONG Version, Bytes, Count, Reserved;
  ADMISSION_FW_TIMING_ENTRY Entries[ADMISSION_FW_TIMING_CAPACITY];
} ADMISSION_FW_TIMING_RING;
#endif

typedef struct _ADMISSION_PLATFORM_RUNTIME {
  ADMISSION_CONTEXT *Adapter;
  APPLE_AGX_MEMORY_IO MemoryIo;
  APPLE_AGX_CONFIG_SNAPSHOT Snapshot;
  volatile UCHAR *SgxBase;
  volatile UCHAR *HandoffBase;
  ADMISSION_ASC_TRANSPORT AscTransport;
  APPLE_AGX_ASC_IO AscIo;
  APPLE_AGX_RTKIT_SESSION Rtkit;
  ADMISSION_HEARTBEAT_RECEIPT HeartbeatReceipt;
  APPLE_AGX_GFX_HANDOFF_STATE Handoff;
  APPLE_AGX_GFX_HANDOFF_IO HandoffIo;
  APPLE_AGX_INITDATA_MEMORY_GRAPH Initdata;
  ULONGLONG RetainedEpoch, RetainedRoot, RetainedRoot0;
  APPLE_AGX_CONTEXT0_BROKER Context0Lease;
  AGX_FW_IO_MANIFEST FirmwareIoManifest;
  AGX_HWDATA_RECEIPT HwdataProfileReceipt;
  APPLE_AGX_FIRMWARE_START_RECEIPT FirmwareStartFailure;
  BOOLEAN CaptureFirmwareStart;
  BOOLEAN RetainedPrepared;
  APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES FirmwarePrimitives;
  APPLE_AGX_FIRMWARE_PROVIDER FirmwareProvider;
  APPLE_AGX_FIRMWARE_IO FirmwareIo;
  APPLE_AGX_PLATFORM_TRANSPORT_IO TransportIo;
  APPLE_AGX_G13_QUEUE_RUNTIME_IO QueueIo;
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      QueueObjects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_PLATFORM_PROVIDER Provider;
  APPLE_AGX_PLATFORM_PROVIDER_CONFIG ProviderConfig;
  APPLE_AGX_BACKEND_RUNTIME Backend;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  KSPIN_LOCK JobTimingLock;
  ADMISSION_JOB_TIMING_STATE JobTiming;
  ADMISSION_JOB_TIMING_STATE JobTimingSnapshot;
  ULONG JobTimingWorkers;
  /* QPC of the last receipt export (at most one per two seconds). */
  ULONGLONG JobTimingExportQpc;
  ADMISSION_FW_TIMING_RING FwTiming;
  ADMISSION_FW_TIMING_RING FwTimingSnapshot;
  ULONG FwTs1Fence, FwTs1Polls;
  ULONGLONG FwTs1Kick, FwTs1First, FwTs1FirstQpc;
#endif
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
  volatile LONG B1Active;
  volatile LONG B1Completed;
  ULONG B1Fence;
#endif
  APPLE_AGX_BACKEND_IO RenderIo;
  APPLE_AGX_BACKEND_IO PlatformIo;
  APPLE_AGX_BACKEND_IO RuntimeIo;
  ADMISSION_DYNAMIC_OVERLAY_PLAN DynamicOverlayPlan;
  ADMISSION_DYNAMIC_OVERLAY_STATE DynamicOverlayState;
  const APPLE_AGX_DYNAMIC_JOB *DynamicJob;
  const void *DynamicStorage;
  ULONG DynamicStorageBytes;
  ULONG DynamicBackgroundColor;
  ULONG DynamicExpectedForegroundColor;
  PIO_WORKITEM WorkItem;
  KEVENT WorkIdle;
  /* Set when the render slot frees or the worker finishes; submitters
   * waiting for the slot clear it before testing (no lost wakeup). */
  KEVENT SlotEvent;
  volatile LONG WorkScheduled;
  volatile LONG WorkersActive;
  volatile LONG Stopping;
  volatile LONG Resetting;
  volatile LONG64 LastProgressMs;
  APPLE_AGX_G13_QUEUE_PROGRESS Progress;
  BOOLEAN ProgressValid;
  APPLE_AGX_COMPLETION_TRANSACTION Completion;
  ADMISSION_RENDER_CONTEXT *CompletionContext;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  ADMISSION_TERMINAL_RECEIPT TerminalReceipt;
  ADMISSION_DYNAMIC_GRAPH_RECEIPT DynamicGraphReceipt;
  ADMISSION_DYNAMIC_STORE_RECEIPT DynamicStoreReceipt;
  ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT DynamicOutputSnapshot;
  ADMISSION_NATIVE_GRAPH_RECEIPT NativeGraphReceipt;
  APPLE_AGX_U64 NativeCommandHash;
  ADMISSION_DYNAMIC_OVERLAY_BINDINGS NativeBindings;
  volatile LONG TerminalSequence;
  volatile LONG CompletedOutputGeneration;
  ADMISSION_COMPLETED_OUTPUT CompletedOutput;
  ADMISSION_RENDER_PACKET_DESCRIPTION CompletedPacket;
  HANDLE OutputThread;
  KEVENT OutputWake;
  KEVENT OutputExited;
  KEVENT OutputIdle;
  KSPIN_LOCK OutputLock;
  ADMISSION_OUTPUT_QUEUE_STATE OutputQueue;
#endif
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
  UCHAR VisibleAgxSource[1024];
  PVOID VisibleAgxSourceAddress;
  ULONG VisibleAgxSourceBytes;
  ULONGLONG VisibleAgxGpuAddress;
  ULONGLONG VisibleAgxPhysicalAddress;
  ULONG VisibleAgxFence;
  BOOLEAN VisibleAgxValid;
#endif
  BOOLEAN Powered;
  BOOLEAN RenderBorrowed;
  BOOLEAN QueueImageReady;
  BOOLEAN ProviderReady;
  BOOLEAN BackendStarted;
  BOOLEAN TimerResolutionRaised;
  /* EXP1075: firmware Timeout/Fault events resumed (Asahi recover()). */
  volatile LONG FirmwareRecoveries;
  ULONG LastFirmwareRecoveryKind, LastFirmwareRecoveryFence;
  BOOLEAN FirmwareRecoveryUnpublished;
} ADMISSION_PLATFORM_RUNTIME;

/* 1 ms in 100-ns units (ExSetTimerResolution). */
#define ADMISSION_PLATFORM_TIMER_RESOLUTION 10000u

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
static ULONGLONG AdmissionJobQpc(VOID) {
  return (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
}

VOID AdmissionJobTimingStartWindows(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context, ULONG pid, ULONG fence,
    ULONG dmaBytes) {
  ADMISSION_PLATFORM_RUNTIME *runtime;
  KIRQL oldIrql;
  ULONGLONG qpc = AdmissionJobQpc();
  if (adapter == NULL || context == NULL || fence == 0u)
    return;
  runtime = (ADMISSION_PLATFORM_RUNTIME *)adapter->PlatformRuntime;
  if (runtime == NULL) return;
  KeAcquireSpinLock(&runtime->JobTimingLock, &oldIrql);
  (void)AdmissionJobTimingBegin(&runtime->JobTiming, fence, pid,
      (ULONGLONG)(ULONG_PTR)context, dmaBytes, qpc);
  KeReleaseSpinLock(&runtime->JobTimingLock, oldIrql);
}

static VOID AdmissionJobTimingMarkWindows(
    ADMISSION_PLATFORM_RUNTIME *runtime, ULONG fence,
    ADMISSION_JOB_PHASE phase) {
  KIRQL oldIrql;
  ULONGLONG qpc;
  if (runtime == NULL || fence == 0u) return;
  qpc = AdmissionJobQpc();
  KeAcquireSpinLock(&runtime->JobTimingLock, &oldIrql);
  (void)AdmissionJobTimingMark(&runtime->JobTiming, fence, phase, qpc);
  KeReleaseSpinLock(&runtime->JobTimingLock, oldIrql);
}

static VOID AdmissionJobTimingDelayWindows(
    ADMISSION_PLATFORM_RUNTIME *runtime, ULONG fence) {
  KIRQL oldIrql;
  KeAcquireSpinLock(&runtime->JobTimingLock, &oldIrql);
  (void)AdmissionJobTimingDelay(&runtime->JobTiming, fence);
  KeReleaseSpinLock(&runtime->JobTimingLock, oldIrql);
}

static VOID AdmissionJobTimingTargetWindows(
    ADMISSION_PLATFORM_RUNTIME *runtime, ULONG fence,
    ULONGLONG bytes) {
  KIRQL oldIrql;
  KeAcquireSpinLock(&runtime->JobTimingLock, &oldIrql);
  (void)AdmissionJobTimingTarget(&runtime->JobTiming, fence, bytes);
  KeReleaseSpinLock(&runtime->JobTimingLock, oldIrql);
}

/* EXP1101 receipt-only: the firmware's GPU performance state from HwDataA
 * (m1n1 initdata.py AGXHWDataA, G13 V13_5: actual_pstate 0x2c, tgt_pstate
 * 0x30, cur_pstate 0x38), packed actual | tgt << 8 | cur << 16 | valid << 31.
 * Stored in the slot's otherwise unused Firmware3dStart (at the TA kick) and
 * Firmware3dEnd (at completion). */
#define ADMISSION_HWDATAA_ACTUAL_PSTATE 0x2cu
#define ADMISSION_HWDATAA_TGT_PSTATE 0x30u
#define ADMISSION_HWDATAA_CUR_PSTATE 0x38u
static ULONGLONG AdmissionJobTimingPstateWord(
    ADMISSION_PLATFORM_RUNTIME *runtime) {
  const APPLE_AGX_MEMORY_OBJECT *hwdata =
      &runtime->Initdata.RegionBMemory.Objects[AppleAgxRegionBMemoryHwdataA];
  const volatile UCHAR *base = (const volatile UCHAR *)hwdata->CpuAddress;
  if (base == NULL || hwdata->Length < ADMISSION_HWDATAA_CUR_PSTATE + 4u)
    return 0ULL;
  return (ULONGLONG)(*(const volatile ULONG *)(base +
                         ADMISSION_HWDATAA_ACTUAL_PSTATE) & 0xffu) |
         ((ULONGLONG)(*(const volatile ULONG *)(base +
                          ADMISSION_HWDATAA_TGT_PSTATE) & 0xffu) << 8) |
         ((ULONGLONG)(*(const volatile ULONG *)(base +
                          ADMISSION_HWDATAA_CUR_PSTATE) & 0xffu) << 16) |
         (1ULL << 31);
}

static VOID AdmissionJobTimingPstateWindows(
    ADMISSION_PLATFORM_RUNTIME *runtime, ULONG fence, BOOLEAN completion) {
  ADMISSION_JOB_TIMING_SLOT *slot;
  KIRQL oldIrql;
  ULONGLONG word = AdmissionJobTimingPstateWord(runtime);
  KeAcquireSpinLock(&runtime->JobTimingLock, &oldIrql);
  slot = (ADMISSION_JOB_TIMING_SLOT *)AdmissionJobTimingFind(
      &runtime->JobTiming, fence);
  if (slot != NULL) {
    if (completion) slot->Firmware3dEnd = word;
    else slot->Firmware3dStart = word;
  }
  KeReleaseSpinLock(&runtime->JobTimingLock, oldIrql);
}

/* Multi-job phase 1 receipt: the firmware alone writes the TA/3D stamps after
 * queue initialisation, so before a build the previous job's values must be
 * there (Asahi event.rs). Counted, never repaired. */
static VOID AdmissionStampCheckWindows(ADMISSION_PLATFORM_RUNTIME *runtime,
    BOOLEAN initializeQueues) {
  static const ULONG objects[4] = {9u, 10u, 26u, 27u};
  const APPLE_AGX_EXP208_DYNAMIC_RESULT *dynamic =
      &runtime->Adapter->BackendImage.Dynamic;
  ULONG index, mask = 0u, value;
  KIRQL oldIrql;
  if (initializeQueues) return;
  for (index = 0u; index < 4u; ++index) {
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *object =
        &runtime->QueueObjects[objects[index]];
    if (object->Data == NULL || object->Size < sizeof(ULONG)) return;
    RtlCopyMemory(&value, object->Data, sizeof(value));
    if (value != ((index & 1u) ? dynamic->D3PreviousStamp :
                                 dynamic->TaPreviousStamp))
      mask |= 1u << index;
  }
  if (!mask) return;
  KeAcquireSpinLock(&runtime->JobTimingLock, &oldIrql);
  runtime->FwTiming.Reserved = ((runtime->FwTiming.Reserved + 1u) & 0xffffu) |
      (mask << 16);
  KeReleaseSpinLock(&runtime->JobTimingLock, oldIrql);
}

static ULONGLONG AdmissionFwTs1Read(ADMISSION_PLATFORM_RUNTIME *runtime) {
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *w =
      &runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_TA_WORK];
  const volatile ULONG *word;
  if (w->Data == NULL || w->Size < ADMISSION_TA_WORK_TS1_OFFSET + 8u) return 0ULL;
  word = (const volatile ULONG *)(w->Data + ADMISSION_TA_WORK_TS1_OFFSET);
  return (ULONGLONG)word[0] | ((ULONGLONG)word[1] << 32);
}

static VOID AdmissionFwTimingCaptureWindows(
    ADMISSION_PLATFORM_RUNTIME *runtime, ULONG fence) {
  ADMISSION_FW_TIMING_ENTRY entry;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *taWork =
      &runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_TA_WORK];
  const APPLE_AGX_MEMORY_OBJECT *stats =
      &runtime->Initdata.RegionBMemory.Objects[AppleAgxRegionBMemoryStatsTa];
  const ADMISSION_JOB_TIMING_SLOT *slot;
  KIRQL oldIrql;
  ULONG index;
  RtlZeroMemory(&entry, sizeof(entry));
  entry.Fence = fence;
  for (index = 0u; index < 8u; ++index) {
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *object =
        &runtime->QueueObjects[28u + index];
    if (object->Data != NULL && object->Size == sizeof(ULONGLONG))
      RtlCopyMemory(&entry.Timestamps[index], object->Data, sizeof(ULONGLONG));
  }
  if (taWork->Data != NULL && taWork->Size >= ADMISSION_TA_WORK_TIMESTAMP_TAIL_OFFSET +
          sizeof(entry.TaTail))
    RtlCopyMemory(entry.TaTail, (const UCHAR *)taWork->Data +
        ADMISSION_TA_WORK_TIMESTAMP_TAIL_OFFSET, sizeof(entry.TaTail));
  if (stats->CpuAddress != NULL && stats->Length >=
          ADMISSION_TA_STATS_TIMESTAMPS_OFFSET + sizeof(entry.TaStats))
    RtlCopyMemory(entry.TaStats, (const UCHAR *)stats->CpuAddress +
        ADMISSION_TA_STATS_TIMESTAMPS_OFFSET, sizeof(entry.TaStats));
  if (runtime->FwTs1Fence == fence) {
    entry.Ts1Polls = runtime->FwTs1Polls;
    entry.Ts1Kick = runtime->FwTs1Kick;
    entry.Ts1First = runtime->FwTs1First;
    entry.Ts1FirstQpc = runtime->FwTs1FirstQpc;
  }
  KeAcquireSpinLock(&runtime->JobTimingLock, &oldIrql);
  slot = AdmissionJobTimingFind(&runtime->JobTiming, fence);
  if (slot != NULL) {
    entry.KickQpc = slot->Qpc[AdmissionJobPhaseKickTa];
    entry.CompleteQpc = slot->Qpc[AdmissionJobPhaseComplete];
  }
  runtime->FwTiming.Version = ADMISSION_FW_TIMING_VERSION;
  runtime->FwTiming.Bytes = sizeof(runtime->FwTiming);
  runtime->FwTiming.Entries[runtime->FwTiming.Count %
      ADMISSION_FW_TIMING_CAPACITY] = entry;
  ++runtime->FwTiming.Count;
  KeReleaseSpinLock(&runtime->JobTimingLock, oldIrql);
}

static VOID AdmissionJobTimingFirmwareWindows(
    ADMISSION_PLATFORM_RUNTIME *runtime, ULONG fence) {
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *start, *end;
  ADMISSION_JOB_TIMING_SLOT *slot;
  KIRQL oldIrql;
  ULONGLONG taStart, taEnd;
  start = &runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_TA_TIMESTAMP_START];
  end = &runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_TA_TIMESTAMP_END];
  if (start->Data == NULL || end->Data == NULL ||
      start->Size != sizeof(taStart) || end->Size != sizeof(taEnd)) return;
  RtlCopyMemory(&taStart, start->Data, sizeof(taStart));
  RtlCopyMemory(&taEnd, end->Data, sizeof(taEnd));
  KeAcquireSpinLock(&runtime->JobTimingLock, &oldIrql);
  slot = (ADMISSION_JOB_TIMING_SLOT *)AdmissionJobTimingFind(
      &runtime->JobTiming, fence);
  if (slot != NULL) {
    slot->FirmwareTaStart = taStart;
    slot->FirmwareTaEnd = taEnd;
    slot->FirmwareValid = (taStart != 0u ? 1u : 0u) |
        (taEnd != 0u ? 2u : 0u);
  }
  KeReleaseSpinLock(&runtime->JobTimingLock, oldIrql);
  AdmissionJobTimingPstateWindows(runtime, fence, TRUE);
  AdmissionFwTimingCaptureWindows(runtime, fence);
}

static VOID AdmissionJobTimingExportWindows(
    ADMISSION_PLATFORM_RUNTIME *runtime) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  NTSTATUS status;
  KIRQL oldIrql;
  ULONGLONG before;
  if (runtime == NULL || runtime->Adapter == NULL ||
      runtime->Adapter->PhysicalDeviceObject == NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL) return;
  /* EXP1117-EXP1119: every 16th job this export (registry set and flush,
   * 0.76-1.13 ms) ran before the worker released the render slot, ~37 times
   * a second. Export at most every two seconds and never flush: the probe
   * reads the live values. */
  before = AdmissionJobQpc();
  KeAcquireSpinLock(&runtime->JobTimingLock, &oldIrql);
  ++runtime->JobTimingWorkers;
  if (runtime->JobTimingExportQpc != 0ULL &&
      before - runtime->JobTimingExportQpc <
          2ULL * runtime->JobTiming.QpcFrequency) {
    KeReleaseSpinLock(&runtime->JobTimingLock, oldIrql);
    return;
  }
  runtime->JobTimingExportQpc = before;
  RtlCopyMemory(&runtime->JobTimingSnapshot, &runtime->JobTiming,
      sizeof(runtime->JobTimingSnapshot));
  RtlCopyMemory(&runtime->FwTimingSnapshot, &runtime->FwTiming,
      sizeof(runtime->FwTimingSnapshot));
  KeReleaseSpinLock(&runtime->JobTimingLock, oldIrql);
  status = IoOpenDeviceRegistryKey(runtime->Adapter->PhysicalDeviceObject,
      PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key);
  if (NT_SUCCESS(status)) {
    RtlInitUnicodeString(&name, L"Wom1JobTiming971");
    status = ZwSetValueKey(key, &name, 0u, REG_BINARY,
        &runtime->JobTimingSnapshot, sizeof(runtime->JobTimingSnapshot));
    if (NT_SUCCESS(status) && runtime->FwTimingSnapshot.Count != 0u) {
      RtlInitUnicodeString(&name, L"Wom1FwTiming1112");
      status = ZwSetValueKey(key, &name, 0u, REG_BINARY,
          &runtime->FwTimingSnapshot, sizeof(runtime->FwTimingSnapshot));
    }
    ZwClose(key);
  }
  KeAcquireSpinLock(&runtime->JobTimingLock, &oldIrql);
  runtime->JobTiming.LastExportQpcTicks = AdmissionJobQpc() - before;
  KeReleaseSpinLock(&runtime->JobTimingLock, oldIrql);
}
#endif

typedef struct _ADMISSION_COMPLETION_NOTIFICATION {
  ADMISSION_PLATFORM_RUNTIME *Runtime;
  APPLE_AGX_U32 Fence;
  APPLE_AGX_U32 Node;
  APPLE_AGX_U32 Engine;
} ADMISSION_COMPLETION_NOTIFICATION;

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
static ULONGLONG AdmissionTerminalReadU64(const UCHAR *Address) {
  ULONGLONG value = 0ULL;
  ULONG index;
  for (index = 0u; index < sizeof(value); ++index)
    value |= (ULONGLONG)Address[index] << (index * 8u);
  return value;
}

static VOID AdmissionTerminalBegin(
    ADMISSION_PLATFORM_RUNTIME *Runtime,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *Description) {
  const APPLE_AGX_BACKEND_JOB_IMAGE *job;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *ta;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *d3;
  ULONG sequence;
  if (Runtime == NULL || Description == NULL)
    return;
  job = &Runtime->Backend.PendingJob;
  ta = &Runtime->QueueObjects[17u];
  d3 = &Runtime->QueueObjects[15u];
  AdmissionTerminalReceiptInitialize(&Runtime->TerminalReceipt);
  RtlZeroMemory(&Runtime->DynamicGraphReceipt,
                sizeof(Runtime->DynamicGraphReceipt));
  RtlZeroMemory(&Runtime->NativeGraphReceipt,sizeof(Runtime->NativeGraphReceipt));
  AdmissionDynamicOutputSnapshotInitialize(&Runtime->DynamicOutputSnapshot);
  if (ta->Data == NULL || ta->Size < 548u ||
      d3->Data == NULL || d3->Size < 612u)
    return;
  sequence = (ULONG)InterlockedIncrement(&Runtime->TerminalSequence);
  (void)AdmissionTerminalReceiptBegin(
      &Runtime->TerminalReceipt, sequence, Runtime->RetainedEpoch,
      Runtime->RetainedRoot, Description->Fence, Description->ContextToken,
      Description->AllocationToken, Description->DestinationGpuVa,
      Description->DestinationPhysical, Description->DestinationBytes,
      job->TaEvent, job->D3Event, job->TaExpectedStamp,
      job->D3ExpectedStamp, job->TaExpectedDonePointer,
      job->D3ExpectedDonePointer,
      AdmissionTerminalReadU64(ta->Data + 36u),
      AdmissionTerminalReadU64(ta->Data + 540u),
      AdmissionTerminalReadU64(d3->Data + 28u),
      AdmissionTerminalReadU64(d3->Data + 604u));
}

static int AdmissionOutputCaptureProgress(void *Context) {
  ADMISSION_PLATFORM_RUNTIME *runtime =
      (ADMISSION_PLATFORM_RUNTIME *)Context;
  LARGE_INTEGER interval;
  if (runtime == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL ||
      InterlockedCompareExchange(&runtime->Stopping, 0, 0) != 0 ||
      InterlockedCompareExchange(&runtime->Resetting, 0, 0) != 0)
    return 0;
  interval.QuadPart = -10000LL; /* one millisecond, relative */
  return NT_SUCCESS(KeDelayExecutionThread(
      KernelMode, FALSE, &interval)) ? 1 : 0;
}

static VOID AdmissionTerminalObserve(
    ADMISSION_PLATFORM_RUNTIME *Runtime, ULONG Fence,
    APPLE_AGX_BACKEND_COMPLETION_STATUS Status,
    ADMISSION_COMPLETED_OUTPUT *Completed) {
  const ADMISSION_BACKEND_OUTPUT_VIEW *Output =
      Completed != NULL ? &Completed->View : NULL;
  const APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *config;
  APPLE_AGX_BACKEND_U32 taStamp = 0u, taDone = 0u;
  APPLE_AGX_BACKEND_U32 d3Stamp = 0u, d3Done = 0u;
  ULONG source;
  BOOLEAN actualValid;
  const UCHAR *rawEvent = NULL;
  ULONG rawEventBytes = 0u;
  BOOLEAN accessRecorded = FALSE;
  if (Runtime == NULL || Fence == 0u)
    return;
  config = &Runtime->Provider.QueueProvider.Runtime.Config;
  actualValid = Runtime->TransportIo.ReadU32(
                    Runtime, config->Ta.Stamp, &taStamp) &&
                Runtime->TransportIo.ReadU32(
                    Runtime, config->Ta.GpuDonePointer, &taDone) &&
                Runtime->TransportIo.ReadU32(
                    Runtime, config->D3.Stamp, &d3Stamp) &&
                Runtime->TransportIo.ReadU32(
                    Runtime, config->D3.GpuDonePointer, &d3Done);
  if (Runtime->Provider.LastEventMessageValid) {
    rawEvent = Runtime->Provider.LastEventMessage;
    rawEventBytes = sizeof(Runtime->Provider.LastEventMessage);
  }
  if (Status == AppleAgxBackendCompletionSuccess)
    source = AdmissionTerminalSourcePollingEvent;
  else if (Status == AppleAgxBackendCompletionTimedOut)
    source = AdmissionTerminalSourceTimeout;
  else if (Status == AppleAgxBackendCompletionCancelled)
    source = AdmissionTerminalSourceCancellation;
  else
    source = AdmissionTerminalSourceFault;
  if (AdmissionTerminalReceiptObserve(
          &Runtime->TerminalReceipt, Fence,
          Status == AppleAgxBackendCompletionSuccess
              ? (ULONG)AppleAgxBackendRuntimeResultOk
              : (ULONG)AppleAgxBackendRuntimeResultFaulted,
          (ULONG)Status, source, rawEvent, rawEventBytes,
          actualValid ? 1u : 0u, taStamp, taDone, d3Stamp, d3Done)) {
    if (Output != NULL && Output->RenderedCpuAddress != NULL &&
        Output->RenderedBytes != 0u &&
        Runtime->TransportIo.FlushForCpu(
            Runtime, Output->RenderedCpuAddress, Output->RenderedBytes)) {
      BOOLEAN captured;
      unsigned int foreground = 0u;
      ADMISSION_DYNAMIC_OUTPUT_RESULT result;
      const UCHAR *verificationBytes =
          (const UCHAR *)Output->RenderedCpuAddress;
      Runtime->TransportIo.MemoryBarrier(Runtime);
      if (Output->VerificationKind == AdmissionBackendOutputVerificationNativeCapture) {
        /* CPU access is protected by the captured allocation lease. The
         * physical-completion fence and cache synchronization precede copying;
         * no legacy color/72-pixel oracle applies to this native allocation. */
        captured = Status == AppleAgxBackendCompletionSuccess && Completed != NULL &&
            Completed->Phase == AdmissionCompletedOutputNotified && !Completed->AccessAttempted &&
            AdmissionDynamicOverlayCaptureNativeOutput(&Runtime->NativeGraphReceipt,Fence,
                Completed->Generation,Output->RenderedGpuAddress,Output->RenderedPhysicalAddress,
                Output->RenderedCpuAddress,Output->RenderedBytes) == AdmissionDynamicOverlaySuccess;
        accessRecorded = AdmissionCompletedOutputRecordAccess(Completed,Fence,
            (ULONG)(captured ? STATUS_SUCCESS : STATUS_INVALID_ADDRESS)) ? TRUE : FALSE;
        if (!accessRecorded) {
          Runtime->NativeGraphReceipt.ReadbackAvailable=0;
          Runtime->NativeGraphReceipt.ReadbackBytes=0;
          Runtime->NativeGraphReceipt.ReadbackFnv1a=0;
          Runtime->NativeGraphReceipt.SnapshotGeneration=0;
        }
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
        Runtime->VisibleAgxValid=FALSE;
#endif
      } else if (Output->VerificationKind ==
          AdmissionBackendOutputVerificationTriangle) {
        ADMISSION_DYNAMIC_OUTPUT_EXPECTATION expectation = {0};
        captured = Output->RenderedBytes ==
                           ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT_CAPACITY &&
            Completed != NULL &&
            AdmissionDynamicOutputSnapshotCapture(
                &Runtime->DynamicOutputSnapshot, Fence,
                Completed->Generation, Output->RenderedGpuAddress,
                Output->RenderedPhysicalAddress, verificationBytes,
                Output->RenderedBytes,
                AdmissionDynamicOutputLayoutAgxTiled64,
                Output->ExpectedColor) &&
            AdmissionDynamicOutputDescribeExpectation(
                Output->RenderWidth, Output->RenderHeight,
                Output->RenderPitch, Output->BackgroundColor,
                Output->ExpectedColor,
                AdmissionDynamicOutputLayoutAgxTiled64,
                &expectation) &&
            AdmissionTerminalReceiptCaptureTriangleOutputProgress(
                &Runtime->TerminalReceipt, Fence,
                Runtime->DynamicOutputSnapshot.Data,
                Runtime->DynamicOutputSnapshot.DataBytes, &expectation,
                ADMISSION_OUTPUT_CAPTURE_CHUNK_BYTES,
                AdmissionOutputCaptureProgress, Runtime, &foreground,
                &result)
                ? TRUE
                : FALSE;
        if (captured &&
            !AdmissionDynamicOutputSnapshotRecordVerification(
                &Runtime->DynamicOutputSnapshot, &result))
          captured = FALSE;
        if (captured && Completed != NULL)
          Completed->View.ExpectedColor = expectation.ExpectedForegroundColor;
      } else {
        captured = AdmissionTerminalReceiptCaptureOutputProgress(
            &Runtime->TerminalReceipt, Fence,
            (const UCHAR *)Output->RenderedCpuAddress,
            Output->RenderedBytes, Output->RenderedBytes,
            Output->ExpectedColor, 0xa5u,
            ADMISSION_OUTPUT_CAPTURE_CHUNK_BYTES,
            AdmissionOutputCaptureProgress, Runtime)
                       ? TRUE
                       : FALSE;
      }
      if (captured && Output->VerificationKind != AdmissionBackendOutputVerificationNativeCapture) {
        BOOLEAN outputValid =
            Runtime->TerminalReceipt.OutputPixelsExpected ==
                Output->RenderedBytes / 4u &&
            Runtime->TerminalReceipt.OutputFirstMismatchIndex == 0xffffffffu &&
            Runtime->TerminalReceipt.OutputPixelsPoison == 0u &&
            Runtime->TerminalReceipt.OutputGuardCorrupt == 0u &&
            Runtime->TerminalReceipt.OutputBytesExamined ==
                Output->RenderedBytes;
        accessRecorded = AdmissionCompletedOutputRecordAccess(
            Completed, Fence,
            (ULONG)(outputValid ? STATUS_SUCCESS : STATUS_DATA_ERROR))
            ? TRUE : FALSE;
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
        if (outputValid) {
          if (Output->Framebuffer == APPLE_AGX_TRUE) {
            Runtime->VisibleAgxSourceAddress =
                Output->AllocationCpuAddress;
            Runtime->VisibleAgxSourceBytes =
                Output->AllocationBytes;
            Runtime->VisibleAgxGpuAddress = Output->AllocationGpuAddress;
            Runtime->VisibleAgxPhysicalAddress =
                Output->AllocationPhysicalAddress;
          } else {
            RtlCopyMemory(Runtime->VisibleAgxSource,
                          Runtime->DynamicOutputSnapshot.Data,
                          sizeof(Runtime->VisibleAgxSource));
            Runtime->VisibleAgxSourceAddress = Runtime->VisibleAgxSource;
            Runtime->VisibleAgxSourceBytes =
                sizeof(Runtime->VisibleAgxSource);
          }
          if (Output->Framebuffer != APPLE_AGX_TRUE) {
            Runtime->VisibleAgxGpuAddress =
                Runtime->TerminalReceipt.DestinationGpuVa;
            Runtime->VisibleAgxPhysicalAddress =
                Runtime->TerminalReceipt.DestinationPhysical;
          }
          Runtime->VisibleAgxFence = Fence;
          Runtime->VisibleAgxValid = TRUE;
        }
#endif
      }
    }
    if (!accessRecorded && Completed != NULL)
      (void)AdmissionCompletedOutputRecordAccess(
          Completed, Fence, (ULONG)STATUS_INVALID_ADDRESS);
    Runtime->TerminalReceipt.EventReadPointer =
        Runtime->Provider.LastEventReadPointer;
    Runtime->TerminalReceipt.EventWritePointer =
        Runtime->Provider.LastEventWritePointer;
    AdmissionTerminalObservationTraceWindows(
        Runtime->Adapter, source, (ULONG)Status,
        (ULONG)Runtime->Backend.Phase,
        (ULONG)Runtime->Provider.QueueProvider.Phase, Fence);
  }
}

static VOID AdmissionTerminalExit(ADMISSION_PLATFORM_RUNTIME *Runtime) {
  ULONG reason;
  ULONG fence;
  if (Runtime == NULL ||
      !(Runtime->TerminalReceipt.ValidMask & ADMISSION_TERMINAL_VALID_BEGIN))
    return;
  fence = Runtime->TerminalReceipt.Fence;
  if (Runtime->TerminalReceipt.ValidMask & ADMISSION_TERMINAL_VALID_TERMINAL)
    reason = Runtime->TerminalReceipt.CompletionStatus ==
                     (ULONG)AppleAgxBackendCompletionSuccess
                 ? AdmissionTerminalExitCompleted
                 : AdmissionTerminalExitBackendFailure;
  else if (Runtime->Provider.LastPollGuard != AppleAgxPlatformPollGuardOk)
    reason = AdmissionTerminalExitPollFailure;
  else if (InterlockedCompareExchange(&Runtime->Resetting, 0, 0) != 0)
    reason = AdmissionTerminalExitReset;
  else if (InterlockedCompareExchange(&Runtime->Stopping, 0, 0) != 0)
    reason = AdmissionTerminalExitStopped;
  else
    reason = AdmissionTerminalExitNonterminal;
  Runtime->TerminalReceipt.WorkerExitReason = reason;
  Runtime->TerminalReceipt.ProviderPhase =
      (ULONG)Runtime->Provider.QueueProvider.Phase;
  Runtime->TerminalReceipt.RuntimePhase = (ULONG)Runtime->Backend.Phase;
  Runtime->TerminalReceipt.Stopping =
      InterlockedCompareExchange(&Runtime->Stopping, 0, 0) != 0 ? 1u : 0u;
  Runtime->TerminalReceipt.Resetting =
      InterlockedCompareExchange(&Runtime->Resetting, 0, 0) != 0 ? 1u : 0u;
  Runtime->TerminalReceipt.SchedulerFaulted = InterlockedCompareExchange(
      &Runtime->Adapter->SchedulerFaulted, 0, 0) != 0 ? 1u : 0u;
  KeMemoryBarrier();
  (void)InterlockedOr(
      (volatile LONG *)&Runtime->TerminalReceipt.ValidMask,
      ADMISSION_TERMINAL_VALID_EXIT);
  AdmissionTerminalExitTraceWindows(
      Runtime->Adapter, reason, Runtime->TerminalReceipt.ValidMask,
      (ULONG)Runtime->Backend.Phase,
      (ULONG)Runtime->Provider.QueueProvider.Phase, fence);
  AdmissionRecordTerminalReceipt(Runtime->Adapter,
                                 &Runtime->TerminalReceipt);
}

_Use_decl_annotations_ VOID AdmissionTerminalReceiptDpcWindows(
    ADMISSION_CONTEXT *Context, ULONG Fence) {
  ADMISSION_PLATFORM_RUNTIME *runtime =
      Context != NULL ? Context->PlatformRuntime : NULL;
  if (runtime != NULL && runtime->TerminalReceipt.Fence == Fence &&
      (InterlockedCompareExchange(
          (volatile LONG *)&runtime->TerminalReceipt.ValidMask, 0, 0) &
       ADMISSION_TERMINAL_VALID_INTERRUPT)) {
    InterlockedExchange(
        (volatile LONG *)&runtime->TerminalReceipt.NotifyDpc, 1);
    (void)InterlockedOr(
        (volatile LONG *)&runtime->TerminalReceipt.ValidMask,
        ADMISSION_TERMINAL_VALID_DPC);
  }
}
#endif

static VOID AdmissionPlatformWorker(
    _In_ PDEVICE_OBJECT DeviceObject, _In_opt_ PVOID Context);

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
static BOOLEAN AdmissionCaptureQueueSubmission(
    ADMISSION_PLATFORM_RUNTIME *Runtime,
    const APPLE_AGX_G13_QUEUE_PROGRESS *Progress,
    BOOLEAN ProgressValid,
    ADMISSION_QUEUE_SUBMISSION_RECEIPT *Receipt) {
  const APPLE_AGX_G13_QUEUE_RUNTIME *queue;
  const APPLE_AGX_PLATFORM_CHANNEL_BINDINGS *channels;
  ULONG taMessageIndex;
  ULONG d3MessageIndex;
  if (Runtime == NULL || Receipt == NULL ||
      Runtime->Provider.QueueProvider.Phase !=
          AppleAgxG13QueueProviderSubmitted)
    return FALSE;
  queue = &Runtime->Provider.QueueProvider.Runtime;
  channels = &Runtime->Provider.Channels;
  if (queue->Phase != AppleAgxG13QueueRuntimeSubmitted ||
      queue->Config.Ta.RingCpuAddress == NULL ||
      queue->Config.D3.RingCpuAddress == NULL ||
      queue->Config.Ta.CpuWritePointer == NULL ||
      queue->Config.D3.CpuWritePointer == NULL ||
      queue->Config.Ta.GpuDonePointer == NULL ||
      queue->Config.D3.GpuDonePointer == NULL ||
      queue->Config.Ta.Stamp == NULL || queue->Config.D3.Stamp == NULL ||
      channels->Ta.StateCpuAddress == NULL ||
      channels->Ta.RingCpuAddress == NULL ||
      channels->D3.StateCpuAddress == NULL ||
      channels->D3.RingCpuAddress == NULL)
    return FALSE;
  RtlZeroMemory(Receipt, sizeof(*Receipt));
  Receipt->Version = ADMISSION_QUEUE_SUBMISSION_RECEIPT_VERSION;
  Receipt->Bytes = sizeof(*Receipt);
  Receipt->Fence = queue->PendingFence;
  Receipt->BackendPhase = Runtime->Backend.Phase;
  Receipt->ProviderPhase = Runtime->Provider.QueueProvider.Phase;
  Receipt->RuntimePhase = queue->Phase;
  Receipt->InitialProgressValid = ProgressValid ? 1u : 0u;
  Receipt->TaEventNumber = queue->Config.Ta.EventNumber;
  Receipt->D3EventNumber = queue->Config.D3.EventNumber;
  Receipt->TaRingCapacity = queue->Config.Ta.RingCapacity;
  Receipt->D3RingCapacity = queue->Config.D3.RingCapacity;
  Receipt->TaCpuWritePointer = *queue->Config.Ta.CpuWritePointer;
  Receipt->TaGpuDonePointer = *queue->Config.Ta.GpuDonePointer;
  Receipt->TaStamp = *queue->Config.Ta.Stamp;
  Receipt->TaExpectedStamp = queue->TaPending.ExpectedStamp;
  Receipt->TaExpectedDonePointer = queue->TaPending.ExpectedDonePointer;
  Receipt->D3CpuWritePointer = *queue->Config.D3.CpuWritePointer;
  Receipt->D3GpuDonePointer = *queue->Config.D3.GpuDonePointer;
  Receipt->D3Stamp = *queue->Config.D3.Stamp;
  Receipt->D3ExpectedStamp = queue->D3Pending.ExpectedStamp;
  Receipt->D3ExpectedDonePointer = queue->D3Pending.ExpectedDonePointer;
  if (ProgressValid && Progress != NULL) {
    Receipt->TaGpuDonePointer = Progress->TaDonePointer;
    Receipt->TaStamp = Progress->TaStamp;
    Receipt->D3GpuDonePointer = Progress->D3DonePointer;
    Receipt->D3Stamp = Progress->D3Stamp;
  }
  Receipt->TaChannelReadPointer = *(volatile ULONG *)(
      channels->Ta.StateCpuAddress +
      APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET);
  Receipt->TaChannelWritePointer = *(volatile ULONG *)(
      channels->Ta.StateCpuAddress +
      APPLE_AGX_PLATFORM_CHANNEL_WRITE_POINTER_OFFSET);
  Receipt->D3ChannelReadPointer = *(volatile ULONG *)(
      channels->D3.StateCpuAddress +
      APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET);
  Receipt->D3ChannelWritePointer = *(volatile ULONG *)(
      channels->D3.StateCpuAddress +
      APPLE_AGX_PLATFORM_CHANNEL_WRITE_POINTER_OFFSET);
  if (Receipt->TaChannelWritePointer >=
          APPLE_AGX_PLATFORM_COMMAND_RING_ENTRY_COUNT ||
      Receipt->D3ChannelWritePointer >=
          APPLE_AGX_PLATFORM_COMMAND_RING_ENTRY_COUNT)
    return FALSE;
  Receipt->TaDoorbell = channels->Ta.Doorbell;
  Receipt->D3Doorbell = channels->D3.Doorbell;
  Receipt->TaQueueInfoGpuAddress = queue->Config.Ta.QueueInfoGpuAddress;
  Receipt->D3QueueInfoGpuAddress = queue->Config.D3.QueueInfoGpuAddress;
  Receipt->TaChannelStateGpuAddress = channels->Ta.StateGpuAddress;
  Receipt->TaChannelRingGpuAddress = channels->Ta.RingGpuAddress;
  Receipt->D3ChannelStateGpuAddress = channels->D3.StateGpuAddress;
  Receipt->D3ChannelRingGpuAddress = channels->D3.RingGpuAddress;
  RtlCopyMemory(Receipt->TaWorkAddresses, queue->Config.Ta.RingCpuAddress,
                sizeof(Receipt->TaWorkAddresses));
  RtlCopyMemory(Receipt->D3WorkAddresses, queue->Config.D3.RingCpuAddress,
                sizeof(Receipt->D3WorkAddresses));
  taMessageIndex = (Receipt->TaChannelWritePointer +
      APPLE_AGX_PLATFORM_COMMAND_RING_ENTRY_COUNT - 1u) %
      APPLE_AGX_PLATFORM_COMMAND_RING_ENTRY_COUNT;
  d3MessageIndex = (Receipt->D3ChannelWritePointer +
      APPLE_AGX_PLATFORM_COMMAND_RING_ENTRY_COUNT - 1u) %
      APPLE_AGX_PLATFORM_COMMAND_RING_ENTRY_COUNT;
  RtlCopyMemory(Receipt->TaRunMessage,
      channels->Ta.RingCpuAddress +
          taMessageIndex * APPLE_AGX_G13_RUN_MESSAGE_SIZE,
      sizeof(Receipt->TaRunMessage));
  RtlCopyMemory(Receipt->D3RunMessage,
      channels->D3.RingCpuAddress +
          d3MessageIndex * APPLE_AGX_G13_RUN_MESSAGE_SIZE,
      sizeof(Receipt->D3RunMessage));
  return TRUE;
}

static BOOLEAN AdmissionCaptureQueueInfo(
    ADMISSION_PLATFORM_RUNTIME *Runtime, ULONG Fence,
    ADMISSION_QUEUE_INFO_RECEIPT *Receipt) {
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *d3Info;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *taInfo;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *d3Pointers;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *taPointers;
  if (Runtime == NULL || Fence == 0u || Receipt == NULL)
    return FALSE;
  d3Info = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_D3_INFO];
  taInfo = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_TA_INFO];
  d3Pointers = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_D3_POINTERS];
  taPointers = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_TA_POINTERS];
  if (d3Info->Data == NULL || d3Info->Size != ADMISSION_QUEUE_INFO_BYTES ||
      taInfo->Data == NULL || taInfo->Size != ADMISSION_QUEUE_INFO_BYTES ||
      d3Pointers->Data == NULL ||
      d3Pointers->Size != ADMISSION_QUEUE_POINTERS_BYTES ||
      taPointers->Data == NULL ||
      taPointers->Size != ADMISSION_QUEUE_POINTERS_BYTES)
    return FALSE;
  RtlZeroMemory(Receipt, sizeof(*Receipt));
  Receipt->Version = ADMISSION_QUEUE_INFO_RECEIPT_VERSION;
  Receipt->Bytes = sizeof(*Receipt);
  Receipt->Fence = Fence;
  RtlCopyMemory(Receipt->D3Info, d3Info->Data, sizeof(Receipt->D3Info));
  RtlCopyMemory(Receipt->TaInfo, taInfo->Data, sizeof(Receipt->TaInfo));
  RtlCopyMemory(Receipt->D3Pointers, d3Pointers->Data,
                sizeof(Receipt->D3Pointers));
  RtlCopyMemory(Receipt->TaPointers, taPointers->Data,
                sizeof(Receipt->TaPointers));
  return TRUE;
}

static BOOLEAN AdmissionCaptureBufferManager(
    ADMISSION_PLATFORM_RUNTIME *Runtime, ULONG Fence,
    ADMISSION_BUFFER_MANAGER_RECEIPT *Receipt) {
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *info;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *control;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *counter;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *misc;
  if (Runtime == NULL || Fence == 0u || Receipt == NULL)
    return FALSE;
  info = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_BUFFER_MANAGER_INFO];
  control = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_BUFFER_MANAGER_CONTROL];
  counter = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_BUFFER_MANAGER_COUNTER];
  misc = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_BUFFER_MANAGER_MISC];
  if (info->Data == NULL || info->Size != ADMISSION_BUFFER_MANAGER_INFO_BYTES ||
      control->Data == NULL ||
      control->Size != ADMISSION_BUFFER_MANAGER_STATE_BYTES ||
      counter->Data == NULL ||
      counter->Size != ADMISSION_BUFFER_MANAGER_STATE_BYTES ||
      misc->Data == NULL || misc->Size != ADMISSION_BUFFER_MANAGER_STATE_BYTES)
    return FALSE;
  RtlZeroMemory(Receipt, sizeof(*Receipt));
  Receipt->Version = ADMISSION_BUFFER_MANAGER_RECEIPT_VERSION;
  Receipt->Bytes = sizeof(*Receipt);
  Receipt->Fence = Fence;
  RtlCopyMemory(Receipt->Info, info->Data, sizeof(Receipt->Info));
  RtlCopyMemory(Receipt->BlockControl, control->Data,
                sizeof(Receipt->BlockControl));
  RtlCopyMemory(Receipt->Counter, counter->Data, sizeof(Receipt->Counter));
  RtlCopyMemory(Receipt->Misc, misc->Data, sizeof(Receipt->Misc));
  return TRUE;
}

static BOOLEAN AdmissionCaptureTaProgress(
    ADMISSION_PLATFORM_RUNTIME *Runtime, ULONG Fence, ULONGLONG ElapsedMs,
    ADMISSION_TA_PROGRESS_RECEIPT *Receipt) {
  static const ULONG opcodeOffsets[ADMISSION_TA_MICROSEQUENCE_OPCODE_COUNT] = {
      ADMISSION_TA_MICROSEQUENCE_START_OFFSET,
      ADMISSION_TA_MICROSEQUENCE_TIMESTAMP_START_OFFSET,
      ADMISSION_TA_MICROSEQUENCE_WAIT_OFFSET,
      ADMISSION_TA_MICROSEQUENCE_TIMESTAMP_END_OFFSET,
      ADMISSION_TA_MICROSEQUENCE_FINALIZE_OFFSET,
      ADMISSION_TA_MICROSEQUENCE_RETIRE_OFFSET};
  static const ULONG stampObjects[2] = {
      ADMISSION_QUEUE_OBJECT_TA_STAMP1,
      ADMISSION_QUEUE_OBJECT_TA_STAMP2};
  static const ULONG timestampObjects[4] = {
      ADMISSION_QUEUE_OBJECT_TA_TIMESTAMP_START,
      ADMISSION_QUEUE_OBJECT_TA_TIMESTAMP_END,
      ADMISSION_QUEUE_OBJECT_TA_USER_TIMESTAMP_START,
      ADMISSION_QUEUE_OBJECT_TA_USER_TIMESTAMP_END};
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *initBm;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *microsequence;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *taWork;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *eventControl;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *taInfo;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *taPointers;
  const APPLE_AGX_MEMORY_OBJECT *stats;
  ULONG index;
  if (Runtime == NULL || Fence == 0u || Receipt == NULL)
    return FALSE;
  initBm = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_INITBM];
  microsequence =
      &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_TA_MICROSEQUENCE];
  taWork = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_TA_WORK];
  eventControl =
      &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_EVENT_CONTROL];
  taInfo = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_TA_INFO];
  taPointers = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_TA_POINTERS];
  stats = &Runtime->Initdata.RegionBMemory.Objects[
      AppleAgxRegionBMemoryStatsTa];
  if (initBm->Data == NULL || initBm->Size != ADMISSION_TA_INITBM_BYTES ||
      microsequence->Data == NULL ||
      ADMISSION_TA_MICROSEQUENCE_RETIRE_OFFSET > microsequence->Size ||
      sizeof(ULONG) >
          microsequence->Size - ADMISSION_TA_MICROSEQUENCE_RETIRE_OFFSET ||
      taWork->Data == NULL ||
      ADMISSION_TA_WORK_TIMESTAMP_TAIL_OFFSET > taWork->Size ||
      ADMISSION_TA_WORK_TIMESTAMP_TAIL_BYTES >
          taWork->Size - ADMISSION_TA_WORK_TIMESTAMP_TAIL_OFFSET ||
      eventControl->Data == NULL || eventControl->Size != 176u ||
      taInfo->Data == NULL || taInfo->Size != ADMISSION_QUEUE_INFO_BYTES ||
      taPointers->Data == NULL ||
      taPointers->Size != ADMISSION_QUEUE_POINTERS_BYTES ||
      stats->CpuAddress == NULL ||
      ADMISSION_TA_STATS_HEAD_BYTES > stats->Length ||
      ADMISSION_TA_STATS_TIMESTAMPS_OFFSET > stats->Length ||
      ADMISSION_TA_STATS_TIMESTAMPS_BYTES >
          stats->Length - ADMISSION_TA_STATS_TIMESTAMPS_OFFSET)
    return FALSE;
  for (index = 0u; index < RTL_NUMBER_OF(stampObjects); ++index) {
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *object =
        &Runtime->QueueObjects[stampObjects[index]];
    if (object->Data == NULL || object->Size != sizeof(ULONG))
      return FALSE;
  }
  for (index = 0u; index < RTL_NUMBER_OF(timestampObjects); ++index) {
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *object =
        &Runtime->QueueObjects[timestampObjects[index]];
    if (object->Data == NULL || object->Size != sizeof(ULONGLONG))
      return FALSE;
  }
  RtlZeroMemory(Receipt, sizeof(*Receipt));
  Receipt->Version = ADMISSION_TA_PROGRESS_RECEIPT_VERSION;
  Receipt->Bytes = sizeof(*Receipt);
  Receipt->Fence = Fence;
  Receipt->ElapsedMs = ElapsedMs > MAXULONG ? MAXULONG : (ULONG)ElapsedMs;
  RtlCopyMemory(Receipt->InitBm, initBm->Data, sizeof(Receipt->InitBm));
  for (index = 0u; index < ADMISSION_TA_MICROSEQUENCE_OPCODE_COUNT;
       ++index) {
    RtlCopyMemory(&Receipt->MicrosequenceOpcodes[index],
        (const UCHAR *)microsequence->Data + opcodeOffsets[index],
        sizeof(Receipt->MicrosequenceOpcodes[index]));
  }
  RtlCopyMemory(Receipt->TaInfo, taInfo->Data, sizeof(Receipt->TaInfo));
  RtlCopyMemory(Receipt->TaPointers, taPointers->Data,
                sizeof(Receipt->TaPointers));
  RtlCopyMemory(Receipt->EventControl, eventControl->Data,
                sizeof(Receipt->EventControl));
  RtlCopyMemory(Receipt->WorkTimestampTail,
      (const UCHAR *)taWork->Data + ADMISSION_TA_WORK_TIMESTAMP_TAIL_OFFSET,
      sizeof(Receipt->WorkTimestampTail));
  RtlCopyMemory(Receipt->StatsHead, stats->CpuAddress,
                sizeof(Receipt->StatsHead));
  RtlCopyMemory(Receipt->StatsTimestamps,
      (const UCHAR *)stats->CpuAddress + ADMISSION_TA_STATS_TIMESTAMPS_OFFSET,
      sizeof(Receipt->StatsTimestamps));
  for (index = 0u; index < RTL_NUMBER_OF(stampObjects); ++index) {
    RtlCopyMemory(&Receipt->TaStamps[index * sizeof(ULONG)],
        Runtime->QueueObjects[stampObjects[index]].Data, sizeof(ULONG));
  }
  for (index = 0u; index < RTL_NUMBER_OF(timestampObjects); ++index) {
    RtlCopyMemory(&Receipt->TimestampTargets[index * sizeof(ULONGLONG)],
        Runtime->QueueObjects[timestampObjects[index]].Data,
        sizeof(ULONGLONG));
  }
  return TRUE;
}

static BOOLEAN AdmissionCaptureTaRetire(
    ADMISSION_PLATFORM_RUNTIME *Runtime, ULONG Fence, ULONGLONG ElapsedMs,
    ADMISSION_TA_RETIRE_RECEIPT *Receipt) {
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *microsequence;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *eventCount;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *jobList;
  const APPLE_AGX_MEMORY_OBJECT *regionC;
  const APPLE_AGX_PLATFORM_RX_CHANNEL_BINDING *event;
  volatile APPLE_AGX_BACKEND_U32 *readPointer;
  volatile APPLE_AGX_BACKEND_U32 *writePointer;
  APPLE_AGX_BACKEND_U32 read;
  APPLE_AGX_BACKEND_U32 write;
  if (Runtime == NULL || Fence == 0u || Receipt == NULL)
    return FALSE;
  microsequence =
      &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_TA_MICROSEQUENCE];
  eventCount = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_EVENT_COUNT];
  jobList = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_JOB_LIST];
  regionC = &Runtime->Initdata.DataObjects[AppleAgxInitdataMemoryRegionC];
  event = &Runtime->Provider.Channels.Event;
  if (microsequence->Data == NULL ||
      ADMISSION_TA_MICROSEQUENCE_FINALIZE_OFFSET > microsequence->Size ||
      ADMISSION_TA_FINALIZE_RETIRE_BYTES >
          microsequence->Size - ADMISSION_TA_MICROSEQUENCE_FINALIZE_OFFSET ||
      eventCount->Data == NULL || eventCount->Size != 4u ||
      jobList->Data == NULL || jobList->Size != 24u ||
      regionC->CpuAddress == NULL ||
      ADMISSION_REGIONC_PENDING_STAMPS_OFFSET > regionC->Length ||
      ADMISSION_REGIONC_PENDING_STAMPS_BYTES >
          regionC->Length - ADMISSION_REGIONC_PENDING_STAMPS_OFFSET ||
      event->StateCpuAddress == NULL)
    return FALSE;
  readPointer = (volatile APPLE_AGX_BACKEND_U32 *)(
      event->StateCpuAddress + APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET);
  writePointer = (volatile APPLE_AGX_BACKEND_U32 *)(
      event->StateCpuAddress + APPLE_AGX_PLATFORM_CHANNEL_WRITE_POINTER_OFFSET);
  if (!Runtime->TransportIo.ReadU32(Runtime, readPointer, &read) ||
      !Runtime->TransportIo.ReadU32(Runtime, writePointer, &write) ||
      read >= APPLE_AGX_PLATFORM_EVENT_RING_ENTRY_COUNT ||
      write >= APPLE_AGX_PLATFORM_EVENT_RING_ENTRY_COUNT)
    return FALSE;
  RtlZeroMemory(Receipt, sizeof(*Receipt));
  Receipt->Version = ADMISSION_TA_RETIRE_RECEIPT_VERSION;
  Receipt->Bytes = sizeof(*Receipt);
  Receipt->Fence = Fence;
  Receipt->ElapsedMs = ElapsedMs > MAXULONG ? MAXULONG : (ULONG)ElapsedMs;
  Receipt->EventReadPointer = read;
  Receipt->EventWritePointer = write;
  RtlCopyMemory(Receipt->FinalizeAndRetire,
      (const UCHAR *)microsequence->Data +
          ADMISSION_TA_MICROSEQUENCE_FINALIZE_OFFSET,
      sizeof(Receipt->FinalizeAndRetire));
  RtlCopyMemory(Receipt->EventCount, eventCount->Data,
                sizeof(Receipt->EventCount));
  RtlCopyMemory(Receipt->JobList, jobList->Data,
                sizeof(Receipt->JobList));
  RtlCopyMemory(Receipt->PendingStamps,
      (const UCHAR *)regionC->CpuAddress +
          ADMISSION_REGIONC_PENDING_STAMPS_OFFSET,
      sizeof(Receipt->PendingStamps));
  return TRUE;
}

static BOOLEAN AdmissionCaptureTaTemporalSample(
    ADMISSION_PLATFORM_RUNTIME *Runtime, ULONGLONG ElapsedMs,
    ADMISSION_TA_TEMPORAL_SAMPLE *Sample) {
  static const ULONG stampObjects[2] = {
      ADMISSION_QUEUE_OBJECT_TA_STAMP1,
      ADMISSION_QUEUE_OBJECT_TA_STAMP2};
  static const ULONG timestampObjects[4] = {
      ADMISSION_QUEUE_OBJECT_TA_TIMESTAMP_START,
      ADMISSION_QUEUE_OBJECT_TA_TIMESTAMP_END,
      ADMISSION_QUEUE_OBJECT_TA_USER_TIMESTAMP_START,
      ADMISSION_QUEUE_OBJECT_TA_USER_TIMESTAMP_END};
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *taWork;
  ULONG index;
  if (Runtime == NULL || Sample == NULL)
    return FALSE;
  taWork = &Runtime->QueueObjects[ADMISSION_QUEUE_OBJECT_TA_WORK];
  if (taWork->Data == NULL ||
      ADMISSION_TA_WORK_TIMESTAMP_TAIL_OFFSET > taWork->Size ||
      ADMISSION_TA_WORK_TIMESTAMP_TAIL_BYTES >
          taWork->Size - ADMISSION_TA_WORK_TIMESTAMP_TAIL_OFFSET)
    return FALSE;
  for (index = 0u; index < RTL_NUMBER_OF(stampObjects); ++index) {
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *object =
        &Runtime->QueueObjects[stampObjects[index]];
    if (object->Data == NULL || object->Size != sizeof(ULONG))
      return FALSE;
  }
  for (index = 0u; index < RTL_NUMBER_OF(timestampObjects); ++index) {
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *object =
        &Runtime->QueueObjects[timestampObjects[index]];
    if (object->Data == NULL || object->Size != sizeof(ULONGLONG))
      return FALSE;
  }
  RtlZeroMemory(Sample, sizeof(*Sample));
  Sample->ElapsedMs = ElapsedMs > MAXULONG ? MAXULONG : (ULONG)ElapsedMs;
  for (index = 0u; index < RTL_NUMBER_OF(stampObjects); ++index) {
    RtlCopyMemory(&Sample->TaStamps[index * sizeof(ULONG)],
        Runtime->QueueObjects[stampObjects[index]].Data, sizeof(ULONG));
  }
  for (index = 0u; index < RTL_NUMBER_OF(timestampObjects); ++index) {
    RtlCopyMemory(&Sample->TimestampTargets[index * sizeof(ULONGLONG)],
        Runtime->QueueObjects[timestampObjects[index]].Data,
        sizeof(ULONGLONG));
  }
  RtlCopyMemory(Sample->WorkTimestampTail,
      (const UCHAR *)taWork->Data + ADMISSION_TA_WORK_TIMESTAMP_TAIL_OFFSET,
      sizeof(Sample->WorkTimestampTail));
  return TRUE;
}

static BOOLEAN AdmissionCaptureKTrace(
    ADMISSION_PLATFORM_RUNTIME *Runtime, ULONG Fence,
    APPLE_AGX_BACKEND_U32 InitialWrite,
    ADMISSION_KTRACE_RECEIPT *Receipt) {
  const APPLE_AGX_MEMORY_OBJECT *state;
  const APPLE_AGX_MEMORY_OBJECT *ring;
  volatile APPLE_AGX_BACKEND_U32 *writePointer;
  APPLE_AGX_BACKEND_U32 finalWrite;
  APPLE_AGX_BACKEND_U32 entry;
  if (Runtime == NULL || Fence == 0u || Receipt == NULL ||
      InitialWrite >= ADMISSION_KTRACE_RING_ENTRIES)
    return FALSE;
  state = &Runtime->Initdata.ChannelMemory.Objects[
      ADMISSION_CHANNEL_OBJECT_KTRACE_STATE];
  ring = &Runtime->Initdata.ChannelMemory.Objects[
      ADMISSION_CHANNEL_OBJECT_KTRACE_RING];
  if (state->CpuAddress == NULL || state->Length < 0x24u ||
      ring->CpuAddress == NULL ||
      ring->Length < ADMISSION_KTRACE_RING_ENTRIES *
                         ADMISSION_KTRACE_ENTRY_BYTES)
    return FALSE;
  writePointer = (volatile APPLE_AGX_BACKEND_U32 *)(
      (unsigned char *)state->CpuAddress +
      APPLE_AGX_PLATFORM_CHANNEL_WRITE_POINTER_OFFSET);
  if (!Runtime->TransportIo.ReadU32(
          Runtime, writePointer, &finalWrite) ||
      finalWrite >= ADMISSION_KTRACE_RING_ENTRIES)
    return FALSE;
  RtlZeroMemory(Receipt, sizeof(*Receipt));
  Receipt->Version = ADMISSION_KTRACE_RECEIPT_VERSION;
  Receipt->Bytes = sizeof(*Receipt);
  Receipt->Fence = Fence;
  Receipt->InitialWritePointer = InitialWrite;
  Receipt->FinalWritePointer = finalWrite;
  for (entry = 0u; entry < ADMISSION_KTRACE_ENTRY_COUNT; ++entry) {
    APPLE_AGX_BACKEND_U32 slot =
        (finalWrite + ADMISSION_KTRACE_RING_ENTRIES -
         ADMISSION_KTRACE_ENTRY_COUNT + entry) %
        ADMISSION_KTRACE_RING_ENTRIES;
    RtlCopyMemory(Receipt->Entries[entry],
        (const unsigned char *)ring->CpuAddress +
            slot * ADMISSION_KTRACE_ENTRY_BYTES,
        ADMISSION_KTRACE_ENTRY_BYTES);
  }
  return TRUE;
}

static BOOLEAN AdmissionCaptureQueueFaultSnapshot(
    ADMISSION_PLATFORM_RUNTIME *Runtime, ULONG Fence, ULONGLONG ElapsedMs,
    ULONG TaRead, ULONG D3Read, BOOLEAN AllowEarly, BOOLEAN ReadSgx,
    ADMISSION_QUEUE_FAULT_SNAPSHOT *Snapshot) {
  const APPLE_AGX_MEMORY_OBJECT *regionB;
  const APPLE_AGX_MEMORY_OBJECT *regionC;
  if (Runtime == NULL || Runtime->SgxBase == NULL || Snapshot == NULL ||
      Fence == 0u ||
      (!AllowEarly &&
       ElapsedMs < ADMISSION_QUEUE_FAULT_SNAPSHOT_DELAY_MS))
    return FALSE;
  regionB = &Runtime->Initdata.RegionBMemory.Objects[
      AppleAgxRegionBMemoryFaultInfo];
  regionC = &Runtime->Initdata.DataObjects[AppleAgxInitdataMemoryRegionC];
  if (regionB->CpuAddress == NULL ||
      regionB->Length < sizeof(Snapshot->RegionBFault) ||
      regionC->CpuAddress == NULL ||
      ADMISSION_REGIONC_FAULT_INFO_OFFSET > regionC->Length ||
      sizeof(Snapshot->RegionCFault) >
          regionC->Length - ADMISSION_REGIONC_FAULT_INFO_OFFSET)
    return FALSE;
  RtlZeroMemory(Snapshot, sizeof(*Snapshot));
  Snapshot->Version = ADMISSION_QUEUE_FAULT_SNAPSHOT_VERSION;
  Snapshot->Bytes = sizeof(*Snapshot);
  Snapshot->Fence = Fence;
  Snapshot->ElapsedMs = ElapsedMs > MAXULONG ? MAXULONG : (ULONG)ElapsedMs;
  Snapshot->TaChannelReadPointer = TaRead;
  Snapshot->D3ChannelReadPointer = D3Read;
  Snapshot->SgxFaultInfo = ReadSgx
      ? READ_REGISTER_ULONG64(
            (volatile ULONG64 *)(Runtime->SgxBase +
                                 ADMISSION_PLATFORM_SGX_FAULT_INFO_OFFSET))
      : 0xacce5515abad1deaULL;
  RtlCopyMemory(Snapshot->RegionBFault, regionB->CpuAddress,
                sizeof(Snapshot->RegionBFault));
  RtlCopyMemory(Snapshot->RegionCFault,
      (const UCHAR *)regionC->CpuAddress + ADMISSION_REGIONC_FAULT_INFO_OFFSET,
      sizeof(Snapshot->RegionCFault));
  return TRUE;
}
#endif

static ULONGLONG AdmissionPlatformNowMs(void) {
  return (ULONGLONG)(KeQueryInterruptTime() / 10000ULL);
}

static BOOLEAN AdmissionPlatformRangeContains(
    const APPLE_AGX_MEMORY_OBJECT *Object, const void *Address,
    APPLE_AGX_U32 Bytes) {
  ULONG_PTR base;
  ULONG_PTR value;
  ULONGLONG offset;
  if (Object == NULL || Address == NULL || Bytes == 0u ||
      Object->CpuAddress == NULL || Object->Length == 0ULL)
    return FALSE;
  base = (ULONG_PTR)Object->CpuAddress;
  value = (ULONG_PTR)Address;
  if (value < base)
    return FALSE;
  offset = (ULONGLONG)(value - base);
  return offset <= Object->Length &&
                 Bytes <= Object->Length - offset
             ? TRUE
             : FALSE;
}

static BOOLEAN AdmissionPlatformContains(
    ADMISSION_PLATFORM_RUNTIME *Runtime, const void *Address,
    APPLE_AGX_U32 Bytes) {
  APPLE_AGX_U32 index;
  if (Runtime == NULL || Address == NULL || Bytes == 0u)
    return FALSE;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  if (AdmissionCompletedOutputContains(
          &Runtime->CompletedOutput, Runtime->CompletedOutput.Fence,
          Address, Bytes))
    return TRUE;
#endif
  for (index = 0u; index < Runtime->Initdata.ChannelMemory.ObjectCount;
       ++index) {
    if (AdmissionPlatformRangeContains(
            &Runtime->Initdata.ChannelMemory.Objects[index], Address,
            Bytes))
      return TRUE;
  }
  for (index = 0u;
       index < Runtime->Initdata.RenderSharedMemory.ObjectCount; ++index) {
    if (AdmissionPlatformRangeContains(
            &Runtime->Initdata.RenderSharedMemory.Objects[index], Address,
            Bytes))
      return TRUE;
  }
  for (index = 0u;
       index < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT; ++index) {
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *object =
        &Runtime->Adapter->BackendImage.Objects[index];
    ULONG_PTR base;
    ULONG_PTR value;
    ULONGLONG offset;
    if (object->Data == NULL || object->Size == 0u)
      continue;
    base = (ULONG_PTR)object->Data;
    value = (ULONG_PTR)Address;
    if (value < base)
      continue;
    offset = (ULONGLONG)(value - base);
    if (offset <= object->Size && Bytes <= object->Size - offset)
      return TRUE;
  }
  return FALSE;
}

static NTSTATUS AdmissionPlatformValidateResources(
    ADMISSION_CONTEXT *Context) {
  PCM_RESOURCE_LIST resources;
  ULONG memory_count = 0u;
  ULONG interrupt_count = 0u;
  ULONG seen = 0u;
  ULONG full_index;
  if (Context == NULL)
    return STATUS_INVALID_PARAMETER;
  resources = Context->DeviceInformation.TranslatedResourceList;
  if (resources == NULL || resources->Count != 1u)
    return STATUS_DEVICE_CONFIGURATION_ERROR;
  for (full_index = 0u; full_index < resources->Count; ++full_index) {
    PCM_FULL_RESOURCE_DESCRIPTOR full = &resources->List[full_index];
    ULONG partial_index;
    for (partial_index = 0u;
         partial_index < full->PartialResourceList.Count; ++partial_index) {
      PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor =
          &full->PartialResourceList.PartialDescriptors[partial_index];
      if (descriptor->Type == CmResourceTypeMemory) {
        ULONGLONG start =
            (ULONGLONG)descriptor->u.Memory.Start.QuadPart;
        ULONG length = descriptor->u.Memory.Length;
        ULONG bit = 0u;
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
        else if (AppleAgxLocalReserveMatchesResource(
                     &Context->LocalReserveReceipt, start, length))
          bit = 1u << 4;
        else
          return STATUS_DEVICE_CONFIGURATION_ERROR;
        if ((seen & bit) != 0u)
          return STATUS_DEVICE_CONFIGURATION_ERROR;
        seen |= bit;
        ++memory_count;
      } else if (descriptor->Type == CmResourceTypeDevicePrivate) {
        /* PnP owns the reserved payload; only memory and IRQ are ours. */
        continue;
      } else if (descriptor->Type == CmResourceTypeInterrupt) {
        if (descriptor->ShareDisposition != CmResourceShareDeviceExclusive ||
            descriptor->Flags != CM_RESOURCE_INTERRUPT_LATCHED ||
            descriptor->u.Interrupt.Vector == 0u)
          return STATUS_DEVICE_CONFIGURATION_ERROR;
        ++interrupt_count;
      } else {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
      }
    }
  }
  return memory_count == 5u && seen == 0x1fu && interrupt_count == 1u
             ? STATUS_SUCCESS
             : STATUS_DEVICE_CONFIGURATION_ERROR;
}

static NTSTATUS AdmissionPlatformReadSnapshot(
    ADMISSION_CONTEXT *Context, APPLE_AGX_CONFIG_SNAPSHOT *Snapshot) {
  ULONG wire[ADMISSION_PLATFORM_CONFIG_WINDOW_BYTES / sizeof(ULONG)];
  ULONG index;
  if (Context == NULL || Snapshot == NULL || Context->BrokerBase == NULL ||
      sizeof(wire) > J313_AGX_G2_POWER_BROKER_SIZE)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(wire, sizeof(wire));
  for (index = APPLE_AGX_CONFIG_MMIO_OFFSET / sizeof(ULONG);
       index < RTL_NUMBER_OF(wire); ++index)
    wire[index] = READ_REGISTER_ULONG(
        (volatile ULONG *)(Context->BrokerBase + index * sizeof(ULONG)));
  return AppleAgxConfigSnapshotDecodeJ313(
             (const unsigned char *)wire,
             (APPLE_AGX_U32)sizeof(wire), Snapshot) ==
                 AppleAgxConfigResultOk
             ? STATUS_SUCCESS
             : STATUS_DEVICE_CONFIGURATION_ERROR;
}

static BOOLEAN AdmissionAscRange(
    ADMISSION_ASC_TRANSPORT *Transport, APPLE_AGX_U32 Offset,
    APPLE_AGX_U32 Width) {
  return Transport != NULL && Transport->Base != NULL && Width != 0u &&
                 Offset <= Transport->Length &&
                 Width <= Transport->Length - Offset
             ? TRUE
             : FALSE;
}

static APPLE_AGX_ASC_U64 AdmissionAscNow(void *Context) {
  UNREFERENCED_PARAMETER(Context);
  return AdmissionPlatformNowMs();
}

static APPLE_AGX_ASC_BOOL AdmissionAscRead32(
    void *Context, APPLE_AGX_ASC_U32 Offset, APPLE_AGX_ASC_U32 *Value) {
  ADMISSION_ASC_TRANSPORT *transport = Context;
  if (Value == NULL || (Offset & 3u) != 0u ||
      !AdmissionAscRange(transport, Offset, sizeof(ULONG)))
    return APPLE_AGX_ASC_FALSE;
  *Value = READ_REGISTER_ULONG(
      (volatile ULONG *)(transport->Base + Offset));
  return APPLE_AGX_ASC_TRUE;
}

static APPLE_AGX_ASC_BOOL AdmissionAscRead64(
    void *Context, APPLE_AGX_ASC_U32 Offset, APPLE_AGX_ASC_U64 *Value) {
  ADMISSION_ASC_TRANSPORT *transport = Context;
  if (Value == NULL || (Offset & 7u) != 0u ||
      !AdmissionAscRange(transport, Offset, sizeof(ULONG64)))
    return APPLE_AGX_ASC_FALSE;
  *Value = READ_REGISTER_ULONG64(
      (volatile ULONG64 *)(transport->Base + Offset));
  if (transport->TraceCount < RTL_NUMBER_OF(transport->Trace)) {
    transport->Trace[transport->TraceCount].Offset = Offset;
    transport->Trace[transport->TraceCount].Write = 0;
    transport->Trace[transport->TraceCount++].Value = *Value;
  }
  return APPLE_AGX_ASC_TRUE;
}

static APPLE_AGX_ASC_BOOL AdmissionAscWrite32(
    void *Context, APPLE_AGX_ASC_U32 Offset, APPLE_AGX_ASC_U32 Value) {
  ADMISSION_ASC_TRANSPORT *transport = Context;
  if ((Offset & 3u) != 0u ||
      !AdmissionAscRange(transport, Offset, sizeof(ULONG)))
    return APPLE_AGX_ASC_FALSE;
  WRITE_REGISTER_ULONG((volatile ULONG *)(transport->Base + Offset), Value);
  return APPLE_AGX_ASC_TRUE;
}

static APPLE_AGX_ASC_BOOL AdmissionAscWrite64(
    void *Context, APPLE_AGX_ASC_U32 Offset, APPLE_AGX_ASC_U64 Value) {
  ADMISSION_ASC_TRANSPORT *transport = Context;
  if ((Offset & 7u) != 0u ||
      !AdmissionAscRange(transport, Offset, sizeof(ULONG64)))
    return APPLE_AGX_ASC_FALSE;
  WRITE_REGISTER_ULONG64((volatile ULONG64 *)(transport->Base + Offset),
                         Value);
  if (transport->TraceCount < RTL_NUMBER_OF(transport->Trace)) {
    transport->Trace[transport->TraceCount].Offset = Offset;
    transport->Trace[transport->TraceCount].Write = 1;
    transport->Trace[transport->TraceCount++].Value = Value;
  }
  return APPLE_AGX_ASC_TRUE;
}

static APPLE_AGX_ASC_BOOL AdmissionAscPause(void *Context) {
  LARGE_INTEGER interval;
  UNREFERENCED_PARAMETER(Context);
  interval.QuadPart = -10000LL;
  return NT_SUCCESS(
             KeDelayExecutionThread(KernelMode, FALSE, &interval))
             ? APPLE_AGX_ASC_TRUE
             : APPLE_AGX_ASC_FALSE;
}

static BOOLEAN AdmissionHandoffRange(
    ADMISSION_PLATFORM_RUNTIME *Runtime, ULONG Offset, ULONG Width) {
  return Runtime != NULL && Runtime->HandoffBase != NULL && Width != 0u &&
                 Offset <= J313_AGX_G2_HANDOFF_SIZE &&
                 Width <= J313_AGX_G2_HANDOFF_SIZE - Offset
             ? TRUE
             : FALSE;
}

static unsigned char AdmissionHandoffRead8(
    void *Context, unsigned int Offset, unsigned char *Value) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  if (Value == NULL || !AdmissionHandoffRange(runtime, Offset, 1u))
    return 0u;
  *Value = READ_REGISTER_UCHAR(runtime->HandoffBase + Offset);
  return 1u;
}

static unsigned char AdmissionHandoffRead32(
    void *Context, unsigned int Offset, unsigned int *Value) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  if (Value == NULL || (Offset & 3u) != 0u ||
      !AdmissionHandoffRange(runtime, Offset, sizeof(ULONG)))
    return 0u;
  *Value = READ_REGISTER_ULONG(
      (volatile ULONG *)(runtime->HandoffBase + Offset));
  return 1u;
}

static unsigned char AdmissionHandoffRead64(
    void *Context, unsigned int Offset, unsigned long long *Value) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  if (Value == NULL || (Offset & 7u) != 0u ||
      !AdmissionHandoffRange(runtime, Offset, sizeof(ULONG64)))
    return 0u;
  *Value = READ_REGISTER_ULONG64(
      (volatile ULONG64 *)(runtime->HandoffBase + Offset));
  return 1u;
}

static unsigned char AdmissionHandoffWrite8(
    void *Context, unsigned int Offset, unsigned char Value) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  if (!AdmissionHandoffRange(runtime, Offset, 1u))
    return 0u;
  WRITE_REGISTER_UCHAR(runtime->HandoffBase + Offset, Value);
  return 1u;
}

static unsigned char AdmissionHandoffWrite32(
    void *Context, unsigned int Offset, unsigned int Value) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  if ((Offset & 3u) != 0u ||
      !AdmissionHandoffRange(runtime, Offset, sizeof(ULONG)))
    return 0u;
  WRITE_REGISTER_ULONG(
      (volatile ULONG *)(runtime->HandoffBase + Offset), Value);
  return 1u;
}

static unsigned char AdmissionHandoffWrite64(
    void *Context, unsigned int Offset, unsigned long long Value) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  if ((Offset & 7u) != 0u ||
      !AdmissionHandoffRange(runtime, Offset, sizeof(ULONG64)))
    return 0u;
  WRITE_REGISTER_ULONG64(
      (volatile ULONG64 *)(runtime->HandoffBase + Offset), Value);
  return 1u;
}

static void AdmissionHandoffBarrier(void *Context) {
  UNREFERENCED_PARAMETER(Context);
  KeMemoryBarrier();
}

static void AdmissionHandoffRelax(void *Context) {
  UNREFERENCED_PARAMETER(Context);
  KeStallExecutionProcessor(10u);
}

static unsigned long long AdmissionHandoffNow(void *Context) {
  UNREFERENCED_PARAMETER(Context);
  return AdmissionPlatformNowMs();
}

static APPLE_AGX_POWER_U32 AdmissionPowerRead32(
    void *Context, APPLE_AGX_POWER_U32 Offset) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  return READ_REGISTER_ULONG(
      (volatile ULONG *)(runtime->Adapter->BrokerBase + Offset));
}

static APPLE_AGX_POWER_U64 AdmissionPowerRead64(
    void *Context, APPLE_AGX_POWER_U32 Offset) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  return READ_REGISTER_ULONG64(
      (volatile ULONG64 *)(runtime->Adapter->BrokerBase + Offset));
}

static void AdmissionPowerWrite32(
    void *Context, APPLE_AGX_POWER_U32 Offset, APPLE_AGX_POWER_U32 Value) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  WRITE_REGISTER_ULONG(
      (volatile ULONG *)(runtime->Adapter->BrokerBase + Offset), Value);
}

static void AdmissionPowerWrite64(
    void *Context, APPLE_AGX_POWER_U32 Offset, APPLE_AGX_POWER_U64 Value) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  WRITE_REGISTER_ULONG64(
      (volatile ULONG64 *)(runtime->Adapter->BrokerBase + Offset), Value);
}

static void AdmissionPowerIo(
    ADMISSION_PLATFORM_RUNTIME *Runtime, APPLE_AGX_POWER_IO *Io) {
  RtlZeroMemory(Io, sizeof(*Io));
  Io->Context = Runtime;
  Io->Read32 = AdmissionPowerRead32;
  Io->Read64 = AdmissionPowerRead64;
  Io->Write32 = AdmissionPowerWrite32;
  Io->Write64 = AdmissionPowerWrite64;
}

static unsigned char AdmissionFirmwareAtPassive(void *Context) {
  return Context != NULL && KeGetCurrentIrql() == PASSIVE_LEVEL ? 1u : 0u;
}

static unsigned long long AdmissionFirmwareNow(void *Context) {
  UNREFERENCED_PARAMETER(Context);
  return AdmissionPlatformNowMs();
}

static unsigned char AdmissionFirmwarePowerOn(
    void *Context, unsigned long long DeadlineMs) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  APPLE_AGX_POWER_IO io;
  BOOLEAN acquired;
  if (runtime == NULL || runtime->Powered ||
      AdmissionPlatformNowMs() >= DeadlineMs)
    return 0u;
  AdmissionPowerIo(runtime, &io);
  acquired = AppleAgxPowerAcquire(&io) ? TRUE : FALSE;
  /* Preserve the broker's exact terminal response before provider PowerOn
   * reduces this hardware transaction to a boolean transport result. */
  AdmissionRecordFirmwarePowerOn(
      runtime->Adapter, acquired,
      AdmissionPowerRead32(runtime, J313_AGX_G2_POWER_REG_STATE),
      AdmissionPowerRead32(runtime, J313_AGX_G2_POWER_REG_RESULT),
      AdmissionPowerRead64(runtime, J313_AGX_G2_POWER_REG_RECEIPT_SEQUENCE));
  if (!acquired)
    return 0u;
  /* Current m1n1 AGX.poke_sgx(): read then write this preparation register
   * before constructing/starting the ASC-backed AGX runtime. */
  (void)READ_REGISTER_ULONG((volatile ULONG *)(runtime->SgxBase +
                                               ADMISSION_PLATFORM_SGX_PRE_ASC_OFFSET));
  WRITE_REGISTER_ULONG((volatile ULONG *)(runtime->SgxBase +
                                           ADMISSION_PLATFORM_SGX_PRE_ASC_OFFSET),
                       ADMISSION_PLATFORM_SGX_PRE_ASC_VALUE);
  KeMemoryBarrier();
  runtime->Powered = TRUE;
  return 1u;
}

static unsigned char AdmissionFirmwarePowerOff(
    void *Context, unsigned long long DeadlineMs) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  APPLE_AGX_POWER_IO io;
  if (runtime == NULL || !runtime->Powered ||
      AdmissionPlatformNowMs() >= DeadlineMs)
    return 0u;
  AdmissionPowerIo(runtime, &io);
  if (!AppleAgxPowerRelease(&io))
    return 0u;
  runtime->Powered = FALSE;
  return 1u;
}


#include "apple_agx_retained_root_client.h"

static unsigned long long AdmissionRetainedRead(void *ctx,unsigned int offset) {
  ADMISSION_PLATFORM_RUNTIME *r=ctx;
  return READ_REGISTER_ULONG64((volatile ULONG64 *)(r->Adapter->BrokerBase+offset));
}
static void AdmissionRetainedWrite64(void *ctx,unsigned int offset,unsigned long long value) {
  ADMISSION_PLATFORM_RUNTIME *r=ctx;
  WRITE_REGISTER_ULONG64((volatile ULONG64 *)(r->Adapter->BrokerBase+offset),value);
}
static void AdmissionRetainedWrite32(void *ctx,unsigned int offset,unsigned int value) {
  ADMISSION_PLATFORM_RUNTIME *r=ctx;
  WRITE_REGISTER_ULONG((volatile ULONG *)(r->Adapter->BrokerBase+offset),value);
}
static BOOLEAN AdmissionRetainedExchange(ADMISSION_PLATFORM_RUNTIME *runtime,
    AGX_RR_REQUEST *request, AGX_RR_RESPONSE *response) {
  AGX_RR_IO io = {runtime,AdmissionRetainedRead,AdmissionRetainedWrite64,AdmissionRetainedWrite32};
  BOOLEAN exchanged;
  if (!runtime || !request || !response) return FALSE;
  exchanged = AgxRrExchange(&io,request,response) ? TRUE : FALSE;
  AdmissionRecordRetainedRoot(runtime->Adapter,request->Command,response);
  return exchanged &&
      response->Epoch != 0 && response->Root != 0 && !(response->Root & 0x3fff) &&
      (request->Command == AGX_RR_PREPARE ||
       (response->Epoch == runtime->RetainedEpoch && response->Root == runtime->RetainedRoot));
}

static BOOLEAN AdmissionRetainedCommand(ADMISSION_PLATFORM_RUNTIME *runtime,
    ULONG operation, AGX_RR_RESPONSE *response) {
  AGX_RR_REQUEST request = {0};
  request.Command = operation;
  request.Epoch = operation == AGX_RR_PREPARE ? 0 : runtime->RetainedEpoch;
  return AdmissionRetainedExchange(runtime,&request,response);
}

#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
_Use_decl_annotations_ BOOLEAN AdmissionGpuvaB1Context0Hash(
    ADMISSION_CONTEXT *Context, ULONGLONG *Hash, ULONG *PageCount) {
  ADMISSION_PLATFORM_RUNTIME *runtime;
  AGX_RR_RESPONSE response = {0};
  if (Hash == NULL || PageCount == NULL)
    return FALSE;
  *Hash = 0ULL;
  *PageCount = 0u;
  if (Context == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return FALSE;
  runtime = (ADMISSION_PLATFORM_RUNTIME *)Context->PlatformRuntime;
  if (runtime == NULL || !runtime->RetainedPrepared ||
      !AdmissionRetainedCommand(runtime, AGX_RR_QUERY_TABLE_HASH, &response) ||
      response.Status != 0u || response.Count == 0ULL ||
      response.Count > 24ULL)
    return FALSE;
  *Hash = response.Pa;
  *PageCount = (ULONG)response.Count;
  return TRUE;
}
#endif

static BOOLEAN AdmissionRetainedQueryArena(ADMISSION_PLATFORM_RUNTIME *runtime,
    ULONG ArenaClass, AGX_RR_ARENA_DESCRIPTOR *Arena) {
  AGX_RR_REQUEST request = {0};
  AGX_RR_RESPONSE response = {0};
  ULONGLONG expectedVa, expectedBytes;
  if (!runtime || !Arena ||
      (ArenaClass != AGX_RR_ARENA_SHARED &&
       ArenaClass != AGX_RR_ARENA_TIMESTAMP &&
       ArenaClass != AGX_RR_ARENA_COMMAND)) return FALSE;
  request.Command = AGX_RR_QUERY_ARENA;
  request.Epoch = runtime->RetainedEpoch;
  request.Va = ArenaClass;
  if (!AdmissionRetainedExchange(runtime,&request,&response) ||
      response.Epoch != runtime->RetainedEpoch ||
      response.Root != runtime->RetainedRoot ||
      response.ArenaVersion != AGX_RR_ARENA_VERSION ||
      response.ArenaClass != ArenaClass) return FALSE;
  expectedVa = ArenaClass == AGX_RR_ARENA_SHARED
      ? AGX_RR_SHARED_ARENA_VA
      : (ArenaClass == AGX_RR_ARENA_TIMESTAMP
             ? AGX_RR_TIMESTAMP_ARENA_VA : AGX_RR_COMMAND_ARENA_VA);
  expectedBytes = ArenaClass == AGX_RR_ARENA_SHARED
      ? AGX_RR_SHARED_ARENA_BYTES
      : (ArenaClass == AGX_RR_ARENA_TIMESTAMP
             ? AGX_RR_TIMESTAMP_ARENA_BYTES : AGX_RR_COMMAND_ARENA_BYTES);
  if (response.ArenaVa != expectedVa || response.ArenaBytes != expectedBytes ||
      !response.ArenaBytes ||
      ((response.ArenaVa | response.ArenaBytes) & 0x3fffULL) ||
      response.ArenaVa > MAXULONGLONG - response.ArenaBytes) return FALSE;
  Arena->Version = response.ArenaVersion;
  Arena->Class = response.ArenaClass;
  Arena->Va = response.ArenaVa;
  Arena->Bytes = response.ArenaBytes;
  return TRUE;
}


static unsigned char AdmissionContext0Ipa(void *ctx,const APPLE_AGX_MEMORY_OBJECT *object,
    unsigned long long leaf_offset,unsigned long long *ipa) {
  ADMISSION_PLATFORM_RUNTIME *runtime=ctx;
  ADMISSION_PHYSICAL_ALLOCATION *allocation=object?object->AllocationHandle:NULL;
  ULONGLONG offset;
  if(!runtime || !object || !ipa || !allocation || !allocation->PhysicalMemoryObject ||
      !allocation->Adl || allocation->Interface!=&runtime->Adapter->Interface ||
      (PUCHAR)object->CpuAddress<allocation->CpuBase || leaf_offset>object->Length ||
      0x4000>object->Length-leaf_offset) return 0;
  offset=(SIZE_T)((PUCHAR)object->CpuAddress-allocation->CpuBase);
  if(offset>allocation->Size || leaf_offset>allocation->Size-offset ||
      0x4000>allocation->Size-offset-leaf_offset ||
      allocation->GuestIpaBase>MAXULONGLONG-offset ||
      allocation->GuestIpaBase+offset>MAXULONGLONG-leaf_offset) return 0;
  *ipa=allocation->GuestIpaBase+offset+leaf_offset;
  return (*ipa&0x3fff)==0;
}

static unsigned char AdmissionRetainedActivate(ADMISSION_PLATFORM_RUNTIME *runtime) {
  AGX_RR_RESPONSE response={0};
  AGX_RR_ARENA_DESCRIPTOR command={0};
  AGX_RR_IO io={runtime,AdmissionRetainedRead,AdmissionRetainedWrite64,AdmissionRetainedWrite32};
  int result;
  if(!runtime->RetainedPrepared || !runtime->Handoff.Locked || !runtime->Initdata.BrokerOnly)
    return 0;
  if(!AdmissionRetainedCommand(runtime,AGX_RR_ACTIVATE,&response) ||
      !(response.Flags&AGX_RR_FLAG_ACTIVE) || !(response.Flags&AGX_RR_FLAG_PREFIX_UNCHANGED) ||
      response.SystemVa!=0xffffffa080000000ULL || response.SystemBytes!=0x4000) return 0;
  if (!AdmissionRetainedQueryArena(runtime,AGX_RR_ARENA_COMMAND,&command) ||
      command.Va > MAXULONGLONG-command.Bytes ||
      AppleAgxInitdataMemoryApplyCommandArena(
          &runtime->Initdata,command.Va,command.Bytes) !=
          AppleAgxInitdataMemoryResultOk ||
      !AppleAgxRenderSharedMemoryBindRelocationObjects(
          &runtime->Initdata.RenderSharedMemory,
          runtime->Adapter->BackendImage.ArenaCpuAddress,
          runtime->Adapter->BackendImage.ArenaBytes,
          runtime->QueueObjects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT) ||
      !AppleAgxApplyRelocations(
          runtime->QueueObjects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          AppleAgxRenderTemplateRelocations(),
          AppleAgxRenderTemplateRelocationCount())) return 0;
  runtime->Rtkit.CrashlogGpuAddress=response.SystemVa&((1ULL<<44)-1);
  runtime->Rtkit.CrashlogCapacityBytes=(ULONG)response.SystemBytes;
  {
    APPLE_AGX_MEMORY_OBJECT *hwdata_a=
        &runtime->Initdata.RegionBMemory.Objects[AppleAgxRegionBMemoryHwdataA];
    APPLE_AGX_MEMORY_OBJECT *hwdata_b=
        &runtime->Initdata.RegionBMemory.Objects[AppleAgxRegionBMemoryHwdataB];
    unsigned char valid=AgxFwIoReadManifest(AdmissionRetainedRead,runtime,
        runtime->RetainedEpoch,runtime->RetainedRoot,&runtime->FirmwareIoManifest);
    AdmissionRecordFirmwareIo(runtime->Adapter,valid?0u:1u,&runtime->FirmwareIoManifest);
    if(!valid) return 0;
    valid=AgxHwdataReadReceipt(AdmissionRetainedRead,runtime,runtime->RetainedEpoch,
        runtime->RetainedRoot,&runtime->HwdataProfileReceipt);
    if(!valid) {
      AdmissionRecordHwdataProfile(runtime->Adapter,1,&runtime->HwdataProfileReceipt);
      return 0;
    }
    valid=AgxHwdataMaterialize(&runtime->HwdataProfileReceipt,&runtime->FirmwareIoManifest,
        runtime->RetainedEpoch,runtime->RetainedRoot,hwdata_a->CpuAddress,hwdata_a->Length,
        hwdata_b->CpuAddress,hwdata_b->Length);
    AdmissionRecordHwdataProfile(runtime->Adapter,valid?0u:2u,&runtime->HwdataProfileReceipt);
    if(!valid) return 0;
  }
  result=AppleAgxContext0BrokerMap(&runtime->Context0Lease,&runtime->Initdata,&io,
      runtime->RetainedEpoch,runtime->RetainedRoot,AdmissionContext0Ipa,runtime);
  AdmissionRecordContext0Inventory(runtime->Adapter,1,result,&runtime->Context0Lease);
  return result==AppleAgxContext0Ok;
}

static unsigned char AdmissionFirmwareCreateUat(
    void *Context, unsigned long long DeadlineMs,
    APPLE_AGX_UAT_TTBR_PAIR *Pair,
    unsigned long long *InitdataAddress) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  if (runtime == NULL || Pair == NULL || InitdataAddress == NULL ||
      AdmissionPlatformNowMs() >= DeadlineMs || !runtime->Initdata.Built ||
      !runtime->Initdata.BrokerOnly ||
      runtime->Initdata.InitdataVirtualAddress == 0ULL)
    return 0u;
  {
    AGX_RR_RESPONSE response = {0};
    if (!AdmissionRetainedCommand(runtime,AGX_RR_PREPARE,&response)) return 0;
    runtime->RetainedPrepared = TRUE;
    runtime->RetainedEpoch = response.Epoch;
    runtime->RetainedRoot = response.Root;
    runtime->RetainedRoot0 = response.Ttbr0;
    Pair->Ttbr0 = response.Ttbr0 | 1ULL;
    Pair->Ttbr1 = response.Root | 1ULL;
    *InitdataAddress = runtime->Initdata.InitdataVirtualAddress &
        ADMISSION_PLATFORM_INITDATA_ADDRESS_MASK;
    return response.Ttbr0 != 0 && !(response.Ttbr0 & 0x3fff);
  }
}

static unsigned char AdmissionFirmwareDestroyUat(
    void *Context, unsigned long long DeadlineMs) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  AGX_RR_RESPONSE response = {0};
  if (!runtime || AdmissionPlatformNowMs() >= DeadlineMs) return 0;
  if (!runtime->RetainedPrepared) return 1;
  {
    int result=AppleAgxContext0BrokerRetire(&runtime->Context0Lease);
    AdmissionRecordContext0Inventory(runtime->Adapter,3,result,&runtime->Context0Lease);
    if(result!=AppleAgxContext0Ok) return 0;
  }
  if (!AdmissionRetainedCommand(runtime,AGX_RR_CLOSE,&response)) return 0;
  runtime->RetainedPrepared = FALSE;
  return 1;
}

static unsigned char AdmissionFirmwarePublishUat(
    void *Context, const APPLE_AGX_UAT_TTBR_PAIR *Pair);
static unsigned char AdmissionFirmwareUnpublishUat(void *Context);

static void AdmissionFirmwareRecordBootstrap(
    void *Context, unsigned int Phase, unsigned char Success,
    unsigned int State) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  if (runtime == NULL)
    return;
  AdmissionRecordProviderBootstrap(runtime->Adapter, Phase, Success, State);
}

static unsigned char AdmissionFirmwareBootAsc(
    void *Context, unsigned long long DeadlineMs) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  APPLE_AGX_RTKIT_SESSION_RESULT result;
  if (runtime == NULL)
    return 0u;
  result = AppleAgxRtkitSessionStartCpuAndInitializeHandoff(
      &runtime->Rtkit, &runtime->AscIo, &runtime->Handoff, DeadlineMs);
  AdmissionRecordRtkitBoot(runtime->Adapter, result, &runtime->Rtkit);
  return result == AppleAgxRtkitSessionResultOk ? 1u : 0u;
}

static unsigned char AdmissionFirmwareCompleteManagement(
    void *Context, unsigned long long DeadlineMs) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  APPLE_AGX_RTKIT_SESSION_RESULT result;
  if (runtime == NULL)
    return 0u;
  result = AppleAgxRtkitSessionCompleteManagementBootstrap(
      &runtime->Rtkit, &runtime->AscIo, DeadlineMs);
  AdmissionRecordRtkitBoot(runtime->Adapter, result, &runtime->Rtkit);
  AdmissionRecordRetainedTrace(runtime->Adapter,runtime->AscTransport.Trace,
      runtime->AscTransport.TraceCount * sizeof(runtime->AscTransport.Trace[0]));
  if(result==AppleAgxRtkitSessionResultOk) {
    int checked=AppleAgxContext0BrokerVerify(&runtime->Context0Lease);
    AdmissionRecordContext0Inventory(runtime->Adapter,2,checked,&runtime->Context0Lease);
    return checked==AppleAgxContext0Ok;
  }
  return 0u;
}

static unsigned char AdmissionFirmwareStopAsc(
    void *Context, unsigned long long DeadlineMs) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  if (runtime == NULL)
    return 0u;
  if (runtime->Rtkit.Running == APPLE_AGX_RTKIT_FALSE)
    return 1u;
  return AppleAgxRtkitSessionStop(
             &runtime->Rtkit, &runtime->AscIo, DeadlineMs) ==
                 AppleAgxRtkitSessionResultOk
             ? 1u
             : 0u;
}

static unsigned char AdmissionFirmwareEndpoint(
    void *Context, unsigned int Endpoint, unsigned long long DeadlineMs,
    BOOLEAN Start) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  ULONGLONG message;
  if (runtime == NULL || AdmissionPlatformNowMs() >= DeadlineMs ||
      (Endpoint != J313_AGX_G2_FIRMWARE_ENDPOINT &&
       Endpoint != J313_AGX_G2_DOORBELL_ENDPOINT))
    return 0u;
  message = Start ? AppleAgxRtkitStartEndpoint(Endpoint, 2u)
                  : AppleAgxRtkitStopEndpoint(Endpoint);
  return message != APPLE_AGX_RTKIT_INVALID_MESSAGE &&
                 AppleAgxAscSend(
                     &runtime->AscIo, message, 0u, DeadlineMs) ==
                     AppleAgxAscResultOk
             ? 1u
             : 0u;
}

static unsigned char AdmissionFirmwareStartEndpoint(
    void *Context, unsigned int Endpoint, unsigned long long DeadlineMs) {
#ifdef APPLE_AGX_MANAGEMENT_QUALIFICATION
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  UNREFERENCED_PARAMETER(Endpoint);
  UNREFERENCED_PARAMETER(DeadlineMs);
  /* Deliberate stop after real management success; no application endpoint,
   * initdata delivery, queue creation or submission in this profile. */
  AdmissionRecordFirmwarePrefix(runtime->Adapter, 4, NULL, NULL);
  return 0u;
#else
  ADMISSION_PLATFORM_RUNTIME *runtime=Context;
  unsigned char success;
  if(!runtime || !runtime->Rtkit.Boot.EndpointMapComplete ||
      (Endpoint!=0x20 && Endpoint!=0x21) ||
      !(runtime->Rtkit.Boot.EndpointMap[Endpoint>>5] & (1u<<(Endpoint&31)))) return 0;
  success=AdmissionFirmwareEndpoint(Context, Endpoint, DeadlineMs, TRUE);
  AdmissionRecordEndpoint(runtime->Adapter,Endpoint,success);
  AdmissionRecordRetainedTrace(runtime->Adapter,runtime->AscTransport.Trace,
      runtime->AscTransport.TraceCount*sizeof(runtime->AscTransport.Trace[0]));
  return success;
#endif
}

static unsigned char AdmissionFirmwareStopEndpoint(
    void *Context, unsigned int Endpoint, unsigned long long DeadlineMs) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  if (runtime != NULL &&
      runtime->Rtkit.Running == APPLE_AGX_RTKIT_FALSE)
    return 1u;
  return AdmissionFirmwareEndpoint(Context, Endpoint, DeadlineMs, FALSE);
}


static unsigned char AdmissionFirmwarePublishUat(
    void *Context, const APPLE_AGX_UAT_TTBR_PAIR *Pair) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  return runtime && Pair && runtime->RetainedRoot0 &&
      Pair->Ttbr0 == (runtime->RetainedRoot0 | 1ULL) &&
      Pair->Ttbr1 == (runtime->RetainedRoot | 1ULL) &&
      AdmissionRetainedActivate(runtime);
}

static unsigned char AdmissionFirmwareUnpublishUat(void *Context) {
  ADMISSION_PLATFORM_RUNTIME *runtime=Context;
  int result;
  if(!runtime || runtime->Rtkit.Running) return 0;
  result=AppleAgxContext0BrokerRetire(&runtime->Context0Lease);
  AdmissionRecordContext0Inventory(runtime->Adapter,3,result,&runtime->Context0Lease);
  return result==AppleAgxContext0Ok;
}

static unsigned char AdmissionFirmwareSendInitdata(
    void *Context, unsigned long long Address,
    unsigned long long DeadlineMs) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
#ifndef APPLE_AGX_STOP_AFTER_ENDPOINTS
  ULONGLONG message;
#endif
  if (runtime == NULL || AdmissionPlatformNowMs() >= DeadlineMs)
    return 0u;
#ifdef APPLE_AGX_STOP_AFTER_ENDPOINTS
  AdmissionRecordEndpoint(runtime->Adapter,0,1); /* explicit pre-initdata stop */
  UNREFERENCED_PARAMETER(Address);
  return 0u;
#else
  message = AppleAgxRtkitInitdata(Address);
  return message != APPLE_AGX_RTKIT_INVALID_MESSAGE &&
                 AppleAgxAscSend(
                     &runtime->AscIo, message,
                     J313_AGX_G2_FIRMWARE_ENDPOINT, DeadlineMs) ==
                     AppleAgxAscResultOk
             ? 1u
             : 0u;
#endif
}

static APPLE_AGX_BACKEND_BOOL AdmissionTransportFlush(
    void *Context, const void *Address, APPLE_AGX_BACKEND_U32 Bytes) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  if (!AdmissionPlatformContains(runtime, Address, Bytes))
    return APPLE_AGX_BACKEND_FALSE;
  KeMemoryBarrier();
  return APPLE_AGX_BACKEND_TRUE;
}

static void AdmissionTransportBarrier(void *Context) {
  UNREFERENCED_PARAMETER(Context);
  KeMemoryBarrier();
}

static APPLE_AGX_BACKEND_BOOL AdmissionTransportPublishU32(
    void *Context, volatile APPLE_AGX_BACKEND_U32 *Address,
    APPLE_AGX_BACKEND_U32 Value) {
  if (!AdmissionPlatformContains(
          Context, (const void *)Address, sizeof(*Address)))
    return APPLE_AGX_BACKEND_FALSE;
  *Address = Value;
  KeMemoryBarrier();
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AdmissionTransportReadU32(
    void *Context, const volatile APPLE_AGX_BACKEND_U32 *Address,
    APPLE_AGX_BACKEND_U32 *Value) {
  if (Value == NULL || !AdmissionPlatformContains(
          Context, (const void *)Address, sizeof(*Address)))
    return APPLE_AGX_BACKEND_FALSE;
  KeMemoryBarrier();
  *Value = *Address;
  KeMemoryBarrier();
  return APPLE_AGX_BACKEND_TRUE;
}

/* EXP1075 kernel dump: after a firmware Timeout event (kind 4) the FW status
 * read halt_count=1, halted=1, resume=0. Nothing resumed the firmware, the job
 * never completed, and the TDR reset (an RTKit stop the halted firmware cannot
 * acknowledge) failed: bugcheck 0x116. Asahi recover() waits up to 100 ms for
 * halted, then writes halted=0 and resume=1; the firmware drops the reported
 * work and continues with the queues. FW status layout (m1n1
 * InitData_FWStatus): halt_count +0x10, halted +0x20, resume +0x30. The
 * firmware-shared mapping is coherent (AdmissionTransportFlush is a barrier). */
static APPLE_AGX_BACKEND_BOOL AdmissionTransportRecover(
    void *Context, APPLE_AGX_BACKEND_U32 Fence, APPLE_AGX_BACKEND_U32 EventKind) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  volatile ULONG *status;
  ULONGLONG deadline;
  if (runtime == NULL || Fence == 0u ||
      runtime->Provider.QueueProvider.PendingFence != Fence ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return APPLE_AGX_BACKEND_FALSE;
  status = (volatile ULONG *)runtime->Initdata
      .DataObjects[AppleAgxInitdataMemoryFirmwareStatus].CpuAddress;
  if (status == NULL) return APPLE_AGX_BACKEND_FALSE;
  deadline = AdmissionPlatformNowMs() + 100u;
  KeMemoryBarrier();
  while (status[0x20u / 4u] == 0u) {
    if (AdmissionPlatformNowMs() >= deadline) return APPLE_AGX_BACKEND_FALSE;
    KeStallExecutionProcessor(50u);
    KeMemoryBarrier();
  }
  status[0x20u / 4u] = 0u;
  KeMemoryBarrier();
  status[0x30u / 4u] = 1u;
  KeMemoryBarrier();
  InterlockedIncrement(&runtime->FirmwareRecoveries);
  runtime->LastFirmwareRecoveryKind = EventKind;
  runtime->LastFirmwareRecoveryFence = Fence;
  runtime->FirmwareRecoveryUnpublished = TRUE;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AdmissionTransportDoorbell(
    void *Context, APPLE_AGX_BACKEND_U32 Doorbell) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  ULONGLONG message;
  ULONGLONG deadline;
  if (runtime == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL ||
      runtime->Rtkit.Running == APPLE_AGX_RTKIT_FALSE ||
      (Doorbell != APPLE_AGX_PLATFORM_TA_DOORBELL &&
       Doorbell != APPLE_AGX_PLATFORM_D3_DOORBELL &&
       Doorbell != APPLE_AGX_DEVICE_CONTROL_DOORBELL_CHANNEL))
    return APPLE_AGX_BACKEND_FALSE;
  message = AppleAgxRtkitDoorbell(Doorbell);
  deadline = AdmissionPlatformNowMs() + J313_AGX_G2_INITDATA_TIMEOUT_MS;
  return message != APPLE_AGX_RTKIT_INVALID_MESSAGE &&
                 AppleAgxAscSend(
                     &runtime->AscIo, message,
                     J313_AGX_G2_DOORBELL_ENDPOINT, deadline) ==
                     AppleAgxAscResultOk
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AdmissionTransportQuiesce(
    void *Context, APPLE_AGX_BACKEND_U32 Fence) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  ULONGLONG deadline;
  if (runtime == NULL || Fence == 0u ||
      runtime->Provider.QueueProvider.PendingFence != Fence ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return APPLE_AGX_BACKEND_FALSE;
  deadline = AdmissionPlatformNowMs() + J313_AGX_G2_STOP_TIMEOUT_MS;
  return AppleAgxRtkitSessionStop(
             &runtime->Rtkit, &runtime->AscIo, deadline) ==
                 AppleAgxRtkitSessionResultOk
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_U64 AdmissionTransportNow(void *Context) {
  return Context != NULL ? AdmissionPlatformNowMs() : 0ULL;
}

static unsigned char AdmissionFirmwareDeviceControl(
    ADMISSION_PLATFORM_RUNTIME *Runtime, BOOLEAN Idle,
    unsigned long long DeadlineMs) {
  APPLE_AGX_DEVICE_CONTROL_PUBLICATION publication={0};
  for (;;) {
    APPLE_AGX_DEVICE_CONTROL_RESULT result;
    unsigned int polls = 0u;
    result = Idle
                 ? AppleAgxDeviceControlPublishUpdateIdleTimestampG13V13_5(
                       &Runtime->Initdata.ChannelMemory,
                       &Runtime->TransportIo, &publication)
                 : AppleAgxDeviceControlPublishInitG13V13_5(
                       &Runtime->Initdata.ChannelMemory,
                       &Runtime->TransportIo, &publication);
    if (result != AppleAgxDeviceControlResultOk) {
      AdmissionRecordDeviceControl(Runtime->Adapter,Idle,result,0,0,0);
      return 0u;
    }
    for (;;) {
      result = AppleAgxDeviceControlWaitForReceiptG13V13_5(
          &publication, &Runtime->TransportIo, 1u, &polls);
      if (result != AppleAgxDeviceControlResultReceiptTimedOut ||
          AdmissionPlatformNowMs() >= DeadlineMs) {
        APPLE_AGX_BACKEND_U32 read=0,write=0;
        (void)Runtime->TransportIo.ReadU32(Runtime,
            (volatile APPLE_AGX_BACKEND_U32 *)(publication.StateCpuAddress+
                publication.StateReadPointerOffset),&read);
        (void)Runtime->TransportIo.ReadU32(Runtime,
            (volatile APPLE_AGX_BACKEND_U32 *)(publication.StateCpuAddress+
                publication.StateWritePointerOffset),&write);
        AdmissionRecordDeviceControl(Runtime->Adapter,Idle,result,read,write,
            publication.ReceiptCookie);
        return result == AppleAgxDeviceControlResultOk;
      }
      KeStallExecutionProcessor(
          ADMISSION_PLATFORM_DEVICE_CONTROL_STALL_US);
    }
  }
}

static unsigned char AdmissionFirmwareDeviceControlInit(
    void *Context, unsigned long long DeadlineMs) {
  return Context != NULL
             ? AdmissionFirmwareDeviceControl(Context, FALSE, DeadlineMs)
             : 0u;
}

static unsigned char AdmissionFirmwareIdleTimestamp(
    void *Context, unsigned long long DeadlineMs) {
  return Context != NULL
             ? AdmissionFirmwareDeviceControl(Context, TRUE, DeadlineMs)
             : 0u;
}

static void AdmissionFirmwareRecordPhase(
    void *Context, APPLE_AGX_FIRMWARE_PHASE Phase,
    APPLE_AGX_FIRMWARE_RESULT Result, APPLE_AGX_FW_U32 CompletedMask) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = Context;
  ADMISSION_PLATFORM_RUNTIME *runtime;
  if (provider == NULL)
    return;
  runtime = provider->Primitives.Context;
  if (runtime != NULL) {
    if(runtime->CaptureFirmwareStart)
      AppleAgxFirmwareCaptureStartFailure(&runtime->FirmwareStartFailure,Phase,Result,CompletedMask);
    AdmissionRecordFirmwarePhase(runtime->Adapter, Phase, Result,
                                 CompletedMask);
  }
}

static APPLE_AGX_BACKEND_BOOL AdmissionRenderPublish(void *Context) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  if (runtime == NULL || runtime->RenderBorrowed ||
      !AdmissionMemoryRuntimeContextPublished(runtime->Adapter))
    return APPLE_AGX_BACKEND_FALSE;
  runtime->RenderBorrowed = TRUE;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AdmissionRenderUnpublish(void *Context) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  if (runtime == NULL || !runtime->RenderBorrowed)
    return APPLE_AGX_BACKEND_FALSE;
  runtime->RenderBorrowed = FALSE;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AdmissionPrepareG4Manager(
    ADMISSION_PLATFORM_RUNTIME *runtime) {
  ADMISSION_BACKEND_IMAGE *image=&runtime->Adapter->BackendImage;
  if (!image->G4Manager) return APPLE_AGX_BACKEND_TRUE;
  if (!image->G4Native || image->G4Manager->PendingFence ||
      runtime->Initdata.RenderSharedMemory.ManagerFence ||
      runtime->Provider.QueueProvider.Phase!=AppleAgxG13QueueProviderCreated ||
      runtime->Provider.QueueProvider.Runtime.Phase!=AppleAgxG13QueueRuntimeReady ||
      runtime->Provider.QueueProvider.PendingFence)
    return APPLE_AGX_BACKEND_FALSE;
  return !AppleAgxRenderManagerNeedsBind(
      &runtime->Initdata.RenderSharedMemory,&image->G4ManagerKey) ||
      AppleAgxG13QueueProviderRequireInitBm(&runtime->Provider.QueueProvider);
}

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
static APPLE_AGX_BACKEND_BOOL AdmissionSaveG4Manager(
    ADMISSION_PLATFORM_RUNTIME *runtime, APPLE_AGX_U32 Fence) {
  ADMISSION_BACKEND_IMAGE *image=&runtime->Adapter->BackendImage;
  static const APPLE_AGX_U32 objects[]={1u,20u,21u,22u};
  APPLE_AGX_U32 i;
  if (!image->G4Manager) return APPLE_AGX_BACKEND_TRUE;
  if (!image->G4Native || image->BoundFence!=Fence ||
      !runtime->Backend.TaComplete || !runtime->Backend.D3Complete)
    return APPLE_AGX_BACKEND_FALSE;
  for (i=0u;i<RTL_NUMBER_OF(objects);++i) {
    APPLE_AGX_EXP208_RELOCATION_OBJECT *object=&runtime->QueueObjects[objects[i]];
    if (!runtime->TransportIo.FlushForCpu(runtime,object->Data,object->Size))
      return APPLE_AGX_BACKEND_FALSE;
  }
  runtime->TransportIo.MemoryBarrier(runtime);
  return AppleAgxRenderManagerSave(&runtime->Initdata.RenderSharedMemory,
      image->G4Manager,Fence,runtime->QueueObjects);
}
#endif

static APPLE_AGX_BACKEND_BOOL AdmissionExternalBuildJob(
    void *Context, const unsigned char *SubmissionBytes,
    APPLE_AGX_BACKEND_U32 SubmissionByteCount,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    APPLE_AGX_BACKEND_U32 TaEvent, APPLE_AGX_BACKEND_U32 D3Event,
    const APPLE_AGX_G13_QUEUE_JOB_PLAN *Plan,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  APPLE_AGX_BACKEND_JOB_IMAGE staged;
  APPLE_AGX_RENDER_RUNTIME_BINDINGS bindings;
  ADMISSION_DYNAMIC_DMA_VIEW dynamicView;
  ADMISSION_DYNAMIC_OVERLAY_PLAN *dynamicPlan;
  BOOLEAN dynamic = FALSE;
  BOOLEAN overlayApplied = FALSE;
  ULONG submissionMagic = 0u;
  APPLE_AGX_U32 index;
  if (runtime == NULL || Submission == NULL || Plan == NULL ||
      Submission->Submission.Fence == 0u)
    return APPLE_AGX_BACKEND_FALSE;
  RtlZeroMemory(&staged, sizeof(staged));
  RtlZeroMemory(&bindings, sizeof(bindings));
  RtlZeroMemory(&dynamicView, sizeof(dynamicView));
  if (runtime->DynamicOverlayState.Applied != 0u) return APPLE_AGX_BACKEND_FALSE;
  dynamicPlan = &runtime->DynamicOverlayPlan;
  RtlZeroMemory(dynamicPlan, sizeof(*dynamicPlan));
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  RtlZeroMemory(&runtime->DynamicStoreReceipt,
                sizeof(runtime->DynamicStoreReceipt));
#endif
  if (SubmissionBytes != NULL && SubmissionByteCount >= sizeof(ULONG))
    RtlCopyMemory(&submissionMagic, SubmissionBytes, sizeof(submissionMagic));
  if (submissionMagic == ADMISSION_DYNAMIC_DMA_MAGIC) {
    if (AdmissionDynamicDmaOpen(
            SubmissionBytes, SubmissionByteCount, &dynamicView) !=
            AdmissionDynamicDmaSuccess ||
        dynamicView.Header->DestinationGpuVa !=
            runtime->Adapter->RenderPacket.Description.DestinationGpuVa ||
        AdmissionDynamicOverlayPlanFromJob(
            &runtime->Adapter->BackendImage, dynamicView.Bindings,
            dynamicView.Job, dynamicPlan) !=
            AdmissionDynamicOverlaySuccess ||
        runtime->DynamicOverlayState.Applied != 0u ||
        AdmissionDynamicOverlayApply(
            &runtime->Adapter->BackendImage, dynamicPlan,
            dynamicView.Job, dynamicView.Storage,
            dynamicView.StorageBytes, Submission->Submission.Fence,
            &runtime->DynamicOverlayState) !=
            AdmissionDynamicOverlaySuccess)
      return APPLE_AGX_BACKEND_FALSE;
    dynamic = TRUE;
    overlayApplied = TRUE;
  }
  if (!AdmissionBackendImageStageJob(
          &runtime->Adapter->BackendImage,
          Submission->Submission.Fence, TaEvent, D3Event,
          Plan->TaExpectedDonePointer, Plan->D3ExpectedDonePointer,
          Plan->IncludeInitBm, &staged))
    goto BuildFailure;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  AdmissionStampCheckWindows(runtime, Plan->InitializeQueues);
#endif
  if (!AppleAgxInitdataMemoryGetRenderBindings(&runtime->Initdata,
                                               &bindings) ||
      !(runtime->Adapter->BackendImage.G4Manager ?
          AppleAgxRenderSharedMemoryBuildManagedG4Job(
              &runtime->Initdata.RenderSharedMemory,
              runtime->Adapter->BackendImage.G4Manager,
              &runtime->Adapter->BackendImage.G4ManagerKey,
              Submission->Submission.Fence, Plan->InitializeQueues,
              runtime->Adapter->BackendImage.ArenaCpuAddress,
              runtime->Adapter->BackendImage.ArenaBytes,
              runtime->Adapter->BackendImage.Objects,
              APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
              runtime->Adapter->BackendImage.ArenaGpuAddress,
              Plan->IncludeInitBm, &bindings, &staged,
              runtime->QueueObjects, Job) :
          runtime->Adapter->BackendImage.G4Native ?
          AppleAgxRenderSharedMemoryBuildActiveG4Job(
              &runtime->Initdata.RenderSharedMemory,
              runtime->Adapter->BackendImage.ArenaCpuAddress,
              runtime->Adapter->BackendImage.ArenaBytes,
              runtime->Adapter->BackendImage.Objects,
              APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
              runtime->Adapter->BackendImage.ArenaGpuAddress,
              Plan->IncludeInitBm, &bindings, &staged,
              runtime->QueueObjects, Job) :
          AppleAgxRenderSharedMemoryBuildActiveJob(
              &runtime->Initdata.RenderSharedMemory,
              runtime->Adapter->BackendImage.ArenaCpuAddress,
              runtime->Adapter->BackendImage.ArenaBytes,
              runtime->Adapter->BackendImage.Objects,
              APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
              runtime->Adapter->BackendImage.ArenaGpuAddress,
              Plan->IncludeInitBm, &bindings, &staged,
              runtime->QueueObjects, Job)) ||
      (dynamic &&
       ((APPLE_AGX_WIN32_COMMAND_IS_NATIVE(dynamicView.Bindings->CommandVersion)) ?
          AdmissionDynamicOverlayRouteNative(dynamicPlan, dynamicView.Bindings,
             runtime->QueueObjects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT) :
          AdmissionDynamicOverlayRouteEncoder(dynamicPlan, runtime->QueueObjects,
             APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT)) !=
           AdmissionDynamicOverlaySuccess))
    goto BuildFailure;
  if(dynamic && dynamicView.Bindings->CommandVersion==
       APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH) {
    const ADMISSION_DYNAMIC_OVERLAY_ENTRY *compute=NULL;
    APPLE_AGX_RENDER_COMPUTE_INPUT input;
    APPLE_AGX_RENDER_COMPUTE_OUTPUT output;
    for(index=0u;index<dynamicPlan->EntryCount;++index)
      if(dynamicPlan->Entries[index].ReferenceIndex==
           dynamicView.Bindings->NativeBatch.ComputeEncoderReference)
        compute=&dynamicPlan->Entries[index];
    RtlZeroMemory(&input,sizeof(input));RtlZeroMemory(&output,sizeof(output));
    if(!compute||compute->Role!=AppleAgxWin32RoleEncoder||
       compute->Bytes!=dynamicView.Bindings->NativeBatch.ComputeEncoderBytes)
      goto BuildFailure;
    input.CdmStreamBase=compute->GpuVirtualAddress;
    input.CdmStreamBytes=compute->Bytes;
    input.Counter=Submission->Submission.Fence;
    input.UscExecutionBase=0x1100000000ULL;
    input.VmSlot=ADMISSION_MEMORY_UAT_CONTEXT;
    input.EventNumber=runtime->Provider.QueueProvider.Config.Compute.EventNumber;
    input.StampValue=Submission->Submission.Fence;
    input.EventSequence=Submission->Submission.Fence;
    input.ClientSequence=Submission->Submission.Fence&0xffu;
    if(!AppleAgxRenderSharedMemoryBuildCompute(
         &runtime->Initdata.RenderSharedMemory,&input,&output)) goto BuildFailure;
    Job->ComputeWorkAddresses[0]=output.WorkGpuAddress;
    Job->ComputeWorkAddressCount=1u;
    Job->ComputeEvent=input.EventNumber;
    Job->ComputeExpectedStamp=input.StampValue;
    Job->ComputeExpectedDonePointer=Plan->ComputeExpectedDonePointer;
  }
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  if (dynamic && !APPLE_AGX_WIN32_COMMAND_IS_NATIVE(dynamicView.Bindings->CommandVersion))
    (void)AdmissionDynamicOverlayCaptureStoreGraph(
        &runtime->Adapter->BackendImage, &runtime->DynamicOverlayState,
        runtime->QueueObjects,
        APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
        Submission->Submission.Fence, &runtime->DynamicStoreReceipt);
#endif
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  {
    APPLE_AGX_EXP208_RELOCATION_OBJECT *output =
        &runtime->Adapter->BackendImage.Objects[
            APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT];
    if (output->Data == NULL ||
        output->Size < APPLE_AGX_EXP208_GDI_OUTPUT_BYTES)
      goto BuildFailure;
    RtlFillMemory(output->Data, output->Size, 0xa5u);
    if (!runtime->TransportIo.FlushForDevice(
            runtime, output->Data, output->Size))
      goto BuildFailure;
  }
#endif
  if (dynamic && (APPLE_AGX_WIN32_COMMAND_IS_NATIVE(dynamicPlan->CommandVersion))) {
    for (index = 0u; index < dynamicPlan->EntryCount; ++index) {
      const ADMISSION_DYNAMIC_OVERLAY_ENTRY *entry=&dynamicPlan->Entries[index];
      const APPLE_AGX_EXP208_RELOCATION_OBJECT *object=
          &runtime->Adapter->BackendImage.Objects[entry->ObjectIndex];
      if (!runtime->TransportIo.FlushForDevice(runtime,
              object->Data + entry->ObjectOffset, entry->Bytes)) goto BuildFailure;
    }
  }
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index) {
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *object =
        &runtime->QueueObjects[index];
    if (object->Data == NULL || object->Size == 0u ||
        !runtime->TransportIo.FlushForDevice(
            runtime, object->Data, object->Size))
      goto BuildFailure;
  }
  if (runtime->Adapter->BackendImage.Binding.Framebuffer.Active ==
      APPLE_AGX_TRUE) {
    static const APPLE_AGX_U32 expandedObjects[] = {64u, 65u, 67u};
    for (index = 0u;
         index < (APPLE_AGX_U32)(sizeof(expandedObjects) /
                                 sizeof(expandedObjects[0]));
         ++index) {
      const APPLE_AGX_EXP208_RELOCATION_OBJECT *object =
          &runtime->QueueObjects[expandedObjects[index]];
      if (object->Data == NULL || object->Size == 0u ||
          !runtime->TransportIo.FlushForDevice(
              runtime, object->Data, object->Size))
        goto BuildFailure;
    }
  }
  runtime->TransportIo.MemoryBarrier(runtime);
  if (dynamic) {
    runtime->DynamicJob = dynamicView.Job;
    runtime->DynamicStorage = dynamicView.Storage;
    runtime->DynamicStorageBytes = dynamicView.StorageBytes;
    runtime->DynamicBackgroundColor = dynamicView.Header->BackgroundColor;
    runtime->DynamicExpectedForegroundColor =
        dynamicView.Header->ExpectedForegroundColor;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
    runtime->NativeCommandHash =
        (APPLE_AGX_WIN32_COMMAND_IS_NATIVE(dynamicView.Bindings->CommandVersion)) ?
        dynamicView.Header->CommandHash : 0ULL;
    if (runtime->NativeCommandHash) runtime->NativeBindings=*dynamicView.Bindings;
    else RtlZeroMemory(&runtime->NativeBindings,sizeof(runtime->NativeBindings));
#endif
  }
  {
    APPLE_AGX_G13_COMPUTE_IDENTITY_BUILD_SNAPSHOT *build =
        &runtime->Provider.QueueProvider.ComputeIdentityDiagnostic.Build;
    RtlZeroMemory(build, sizeof(*build));
    build->Valid = APPLE_AGX_BACKEND_TRUE;
    build->ContextIdentity = (APPLE_AGX_BACKEND_U64)(ULONG_PTR)runtime;
    build->JobIdentity = (APPLE_AGX_BACKEND_U64)(ULONG_PTR)Job;
    build->ExpectedProviderIdentity =
        (APPLE_AGX_BACKEND_U64)(ULONG_PTR)&runtime->Provider.QueueProvider;
    build->Fence = Submission->Submission.Fence;
    build->CommandVersion = dynamic ? dynamicView.Bindings->CommandVersion : 0u;
    build->MixedBranch = dynamic &&
        dynamicView.Bindings->CommandVersion ==
            APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH
        ? APPLE_AGX_BACKEND_TRUE
        : APPLE_AGX_BACKEND_FALSE;
    build->WorkAddressCount = Job->ComputeWorkAddressCount;
    build->JobEvent = Job->ComputeEvent;
    build->JobExpectedStamp = Job->ComputeExpectedStamp;
    build->JobExpectedDonePointer = Job->ComputeExpectedDonePointer;
    build->WorkAddress = Job->ComputeWorkAddresses[0];
    build->ConfigEvent =
        runtime->Provider.QueueProvider.Config.Compute.EventNumber;
  }
  return APPLE_AGX_BACKEND_TRUE;

BuildFailure:
  if (overlayApplied)
    (void)AdmissionDynamicOverlayRelease(
        &runtime->Adapter->BackendImage, dynamicPlan, dynamicView.Job,
        dynamicView.Storage, dynamicView.StorageBytes,
        Submission->Submission.Fence, &runtime->DynamicOverlayState);
  return APPLE_AGX_BACKEND_FALSE;
}

static BOOLEAN AdmissionDynamicOverlayReleaseActive(
    ADMISSION_PLATFORM_RUNTIME *Runtime, ULONG Fence) {
  if (Runtime == NULL || Fence == 0u)
    return FALSE;
  if (Runtime->DynamicOverlayState.Applied == 0u)
    return Runtime->DynamicJob == NULL && Runtime->DynamicStorage == NULL &&
                   Runtime->DynamicStorageBytes == 0u
               ? TRUE
               : FALSE;
  if (Runtime->DynamicJob == NULL || Runtime->DynamicStorage == NULL ||
      Runtime->DynamicStorageBytes == 0u ||
      AdmissionDynamicOverlayRelease(
          &Runtime->Adapter->BackendImage, &Runtime->DynamicOverlayPlan,
          Runtime->DynamicJob, Runtime->DynamicStorage,
          Runtime->DynamicStorageBytes, Fence,
          &Runtime->DynamicOverlayState) != AdmissionDynamicOverlaySuccess)
    return FALSE;
  RtlZeroMemory(&Runtime->DynamicOverlayPlan,
                sizeof(Runtime->DynamicOverlayPlan));
  Runtime->DynamicJob = NULL;
  Runtime->DynamicStorage = NULL;
  Runtime->DynamicStorageBytes = 0u;
  Runtime->DynamicBackgroundColor = 0u;
  Runtime->DynamicExpectedForegroundColor = 0u;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  Runtime->NativeCommandHash=0;
  RtlZeroMemory(&Runtime->NativeBindings,sizeof(Runtime->NativeBindings));
#endif
  return TRUE;
}

static APPLE_AGX_BACKEND_BOOL AdmissionExternalResolveRange(
    void *Context, APPLE_AGX_BACKEND_U64 GpuAddress,
    const void **CpuAddress, APPLE_AGX_BACKEND_U32 *Bytes) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  APPLE_AGX_U32 index;
  if (runtime == NULL || CpuAddress == NULL || Bytes == NULL ||
      !runtime->Adapter->BackendImage.JobReady)
    return APPLE_AGX_BACKEND_FALSE;
  for (index = 0u; index < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT;
       ++index) {
    APPLE_AGX_EXP208_RELOCATION_OBJECT *object =
        &runtime->QueueObjects[index];
    if (object->GpuVa == GpuAddress && object->Data != NULL &&
        object->Size != 0u) {
      *CpuAddress = object->Data;
      *Bytes = object->Size;
      return APPLE_AGX_BACKEND_TRUE;
    }
  }
  return APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AdmissionBackendMap(
    void *Context, void **CpuAddress, APPLE_AGX_BACKEND_U64 *GpuAddress,
    APPLE_AGX_BACKEND_U32 *Bytes) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  ADMISSION_BACKEND_IMAGE *image;
  if (runtime == NULL || CpuAddress == NULL || GpuAddress == NULL ||
      Bytes == NULL)
    return APPLE_AGX_BACKEND_FALSE;
  image = &runtime->Adapter->BackendImage;
  if (image->Ready != APPLE_AGX_TRUE || image->ArenaCpuAddress == NULL ||
      image->ArenaGpuAddress == 0ULL || image->ArenaBytes == 0u)
    return APPLE_AGX_BACKEND_FALSE;
  *CpuAddress = image->ArenaCpuAddress;
  *GpuAddress = image->ArenaGpuAddress;
  *Bytes = image->ArenaBytes;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AdmissionBackendUnmap(
    void *Context, void *CpuAddress, APPLE_AGX_BACKEND_U32 Bytes) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  return runtime != NULL &&
                 runtime->Adapter->BackendImage.ArenaCpuAddress == CpuAddress &&
                 runtime->Adapter->BackendImage.ArenaBytes == Bytes
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AdmissionBackendResolve(
    void *Context, const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    const unsigned char **Bytes, APPLE_AGX_BACKEND_U32 *ByteCount) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_DMA_SHADOW_VIEW view;
  ADMISSION_BACKEND_IMAGE *image = runtime == NULL ||
      runtime->Adapter == NULL ? NULL :
      &runtime->Adapter->BackendImage;
  if (image != NULL && image->G4Native) {
    if (Submission == NULL || Bytes == NULL || ByteCount == NULL ||
        image->G4CommandBytes == 0u ||
        image->BoundFence != Submission->Submission.Fence ||
        Submission->PrivateData != &image->G4Header ||
        Submission->DmaSubmissionStart != 0u ||
        Submission->DmaSubmissionEnd != image->G4CommandBytes)
      return APPLE_AGX_BACKEND_FALSE;
    *Bytes = image->G4Command;
    *ByteCount = image->G4CommandBytes;
    return APPLE_AGX_BACKEND_TRUE;
  }
  if (runtime == NULL || Submission == NULL || Bytes == NULL ||
      ByteCount == NULL || Submission->PrivateData == NULL ||
      !AppleAgxDmaShadowOpen(
          &shadow, (void *)Submission->PrivateData,
          Submission->PrivateDataBytes) ||
      !AppleAgxDmaShadowIsSealedForFence(
          shadow.Storage, shadow.BytesUsed,
          Submission->Submission.Fence) ||
      Submission->PrivateDataEnd < shadow.BytesUsed ||
      !AppleAgxDmaShadowFind(
          shadow.Storage, shadow.BytesUsed,
          Submission->DmaSubmissionStart,
          Submission->DmaSubmissionEnd -
              Submission->DmaSubmissionStart,
          &view))
    return APPLE_AGX_BACKEND_FALSE;
  *Bytes = view.Bytes;
  *ByteCount = view.DmaBytes;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AdmissionBackendAcquire(
    void *Context, void *Arena, APPLE_AGX_BACKEND_U32 ArenaBytes,
    APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  ADMISSION_BACKEND_IMAGE *image;
  if (runtime == NULL || Roots == NULL)
    return APPLE_AGX_BACKEND_FALSE;
  image = &runtime->Adapter->BackendImage;
  if (image->Ready != APPLE_AGX_TRUE || image->ArenaCpuAddress != Arena ||
      image->ArenaBytes != ArenaBytes)
    return APPLE_AGX_BACKEND_FALSE;
  *Roots = image->Roots;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AdmissionBackendRelocate(
    void *Context, void *Arena, APPLE_AGX_BACKEND_U32 ArenaBytes,
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots,
    const unsigned char *SubmissionBytes,
    APPLE_AGX_BACKEND_U32 SubmissionByteCount,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  return runtime != NULL && runtime->PlatformIo.Image.Relocate != NULL
             ? runtime->PlatformIo.Image.Relocate(
                   runtime->PlatformIo.Context, Arena, ArenaBytes, Roots,
                   SubmissionBytes, SubmissionByteCount, Submission, Job)
             : APPLE_AGX_BACKEND_FALSE;
}

#define ADMISSION_DELEGATE_ZERO(name, member)                              \
  static APPLE_AGX_BACKEND_BOOL name(void *Context) {                      \
    ADMISSION_PLATFORM_RUNTIME *runtime = Context;                         \
    return runtime != NULL && runtime->PlatformIo.member != NULL           \
               ? runtime->PlatformIo.member(runtime->PlatformIo.Context)   \
               : APPLE_AGX_BACKEND_FALSE;                                 \
  }

#define ADMISSION_DELEGATE_JOB(name, member)                               \
  static APPLE_AGX_BACKEND_BOOL name(                                      \
      void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,               \
      APPLE_AGX_BACKEND_U32 Fence) {                                       \
    ADMISSION_PLATFORM_RUNTIME *runtime = Context;                         \
    return runtime != NULL && runtime->PlatformIo.member != NULL           \
               ? runtime->PlatformIo.member(runtime->PlatformIo.Context,   \
                                            Job, Fence)                    \
               : APPLE_AGX_BACKEND_FALSE;                                 \
  }

#define ADMISSION_DELEGATE_FENCE(name, member)                             \
  static APPLE_AGX_BACKEND_BOOL name(                                      \
      void *Context, APPLE_AGX_BACKEND_U32 Fence) {                        \
    ADMISSION_PLATFORM_RUNTIME *runtime = Context;                         \
    return runtime != NULL && runtime->PlatformIo.member != NULL           \
               ? runtime->PlatformIo.member(runtime->PlatformIo.Context,   \
                                            Fence)                         \
               : APPLE_AGX_BACKEND_FALSE;                                 \
  }

ADMISSION_DELEGATE_ZERO(AdmissionQueuesCreate, Queues.Create)
ADMISSION_DELEGATE_ZERO(AdmissionQueuesDestroy, Queues.Destroy)
static APPLE_AGX_BACKEND_BOOL AdmissionQueuesRun3d(
    void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
    APPLE_AGX_BACKEND_U32 Fence) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  AdmissionJobTimingMarkWindows(runtime, Fence, AdmissionJobPhaseKick3d);
#endif
  return runtime != NULL && runtime->PlatformIo.Queues.Run3d != NULL
      ? runtime->PlatformIo.Queues.Run3d(runtime->PlatformIo.Context, Job, Fence)
      : APPLE_AGX_BACKEND_FALSE;
}
static APPLE_AGX_BACKEND_BOOL AdmissionQueuesRunTa(
    void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
    APPLE_AGX_BACKEND_U32 Fence) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  AdmissionJobTimingMarkWindows(runtime, Fence, AdmissionJobPhaseKickTa);
  if (runtime != NULL) AdmissionJobTimingPstateWindows(runtime, Fence, FALSE);
#endif
  return runtime != NULL && runtime->PlatformIo.Queues.RunTa != NULL
      ? runtime->PlatformIo.Queues.RunTa(runtime->PlatformIo.Context, Job, Fence)
      : APPLE_AGX_BACKEND_FALSE;
}
ADMISSION_DELEGATE_FENCE(AdmissionQueuesStop, Queues.Stop)
ADMISSION_DELEGATE_FENCE(AdmissionQueuesReset, Queues.Reset)

static BOOLEAN AdmissionNotifyCompletionAtInterrupt(PVOID Context) {
  ADMISSION_COMPLETION_NOTIFICATION *notification = Context;
  ADMISSION_PLATFORM_RUNTIME *runtime;
  DXGKARGCB_NOTIFY_INTERRUPT_DATA data;
  BOOLEAN queued;
  if (notification == NULL || notification->Runtime == NULL)
    return FALSE;
  runtime = notification->Runtime;
  if (!runtime->Adapter->InterfaceValid ||
      !AppleAgxCompletionTransactionCanReport(
          &runtime->Completion, notification->Fence,
          notification->Node, notification->Engine))
    return FALSE;
  RtlZeroMemory(&data, sizeof(data));
  data.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
  data.DmaCompleted.SubmissionFenceId = notification->Fence;
  data.DmaCompleted.NodeOrdinal = notification->Node;
  data.DmaCompleted.EngineOrdinal = notification->Engine;
  runtime->Adapter->Interface.DxgkCbNotifyInterrupt(
      runtime->Adapter->Interface.DeviceHandle, &data);
  AppleAgxCompletionTransactionMarkReported(&runtime->Completion);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  (void)AdmissionTerminalReceiptNotifyInterrupt(
      &runtime->TerminalReceipt, notification->Fence);
#endif
  InterlockedExchange(&runtime->Adapter->RenderDpcFence, (LONG)notification->Fence);
  InterlockedExchange(&runtime->Adapter->SchedulerDpcPending, 1);
  queued = runtime->Adapter->Interface.DxgkCbQueueDpc(
      runtime->Adapter->Interface.DeviceHandle);
  AdmissionRenderCorrelationNotifyAtInterruptWindows(
      runtime->Adapter, notification->Fence, KeQueryInterruptTime(), queued);
  return TRUE;
}

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
static BOOLEAN AdmissionCompletedOutputPlatformValid(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_BACKEND_OUTPUT_VIEW *Output) {
  ADMISSION_SCANOUT_MEMORY_VIEW memory;
  if (Context == NULL || Output == NULL ||
      !NT_SUCCESS(AdmissionMemoryRuntimeScanoutView(Context, &memory)))
    return FALSE;
  return AdmissionCompletedOutputPlatformRangeValid(
             Output, memory.CpuAddress, memory.GpuVirtualAddress,
             memory.HostPhysicalAddress, memory.Bytes)
             ? TRUE : FALSE;
}

static VOID AdmissionOutputThread(PVOID Context);
static VOID AdmissionOutputProcess(
    ADMISSION_PLATFORM_RUNTIME *Runtime, ULONG Fence);
#endif

#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
static APPLE_AGX_BACKEND_BOOL AdmissionB1Complete(
    ADMISSION_PLATFORM_RUNTIME *runtime, ADMISSION_CONTEXT *adapter,
    APPLE_AGX_BACKEND_U32 fence) {
  if (adapter == NULL || fence != runtime->B1Fence ||
      !runtime->Backend.TaComplete || !runtime->Backend.D3Complete)
    return APPLE_AGX_BACKEND_FALSE;
  InterlockedExchange(&runtime->B1Completed, 1);
  return APPLE_AGX_BACKEND_TRUE;
}
#endif

static APPLE_AGX_BACKEND_BOOL AdmissionBackendComplete(
    void *Context, APPLE_AGX_BACKEND_U32 Fence,
    APPLE_AGX_BACKEND_U32 Node, APPLE_AGX_BACKEND_U32 Engine,
    APPLE_AGX_BACKEND_COMPLETION_STATUS Status) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  ADMISSION_CONTEXT *adapter;
  ADMISSION_COMPLETION_NOTIFICATION notification;
  BOOLEAN local = FALSE;
  BOOLEAN reported = FALSE;
  BOOLEAN preemption_waiting = FALSE;
  NTSTATUS sync_status;
  KIRQL old_irql;

  if (runtime == NULL)
    return APPLE_AGX_BACKEND_FALSE;
  if (Fence == 0u || Node != 0u || Engine != 0u ||
      Status != AppleAgxBackendCompletionSuccess)
    return APPLE_AGX_BACKEND_FALSE;
  adapter = runtime->Adapter;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  AdmissionJobTimingMarkWindows(runtime, Fence, AdmissionJobPhaseComplete);
#endif
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
  if (InterlockedCompareExchange(&runtime->B1Active, 0, 0) != 0)
    return AdmissionB1Complete(runtime, adapter, Fence);
#endif
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  {
    ADMISSION_RENDER_CONTEXT *g3_context = runtime->CompletionContext;
    if (g3_context == NULL && adapter != NULL)
      g3_context = (ADMISSION_RENDER_CONTEXT *)(ULONG_PTR)
          adapter->RenderPacket.Description.ContextToken;
    if (adapter != NULL && g3_context != NULL &&
        g3_context->GpuvaG3Process != NULL &&
        (!runtime->Backend.TaComplete || !runtime->Backend.D3Complete ||
         !AdmissionSaveG4Manager(runtime, Fence) ||
         !AdmissionGpuvaG3CompleteJob(adapter, Fence)))
      return APPLE_AGX_BACKEND_FALSE;
    AdmissionJobTimingMarkWindows(runtime, Fence, AdmissionJobPhaseJobEnd);
    AdmissionJobTimingFirmwareWindows(runtime, Fence);
  }
#endif
  if (adapter == NULL || !adapter->InterfaceValid ||
      adapter->Interface.DxgkCbSynchronizeExecution == NULL ||
      adapter->Interface.DxgkCbNotifyInterrupt == NULL ||
      adapter->Interface.DxgkCbQueueDpc == NULL)
    return APPLE_AGX_BACKEND_FALSE;

  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (runtime->Completion.Phase == AppleAgxCompletionIdle) {
    if (AdmissionRenderPacketState(&adapter->RenderPacket) !=
            AdmissionRenderPacketActive ||
        adapter->RenderPacket.Description.Fence != Fence ||
        AppleAgxSchedulerActiveFence(
            &adapter->Scheduler, Node, Engine) != Fence) {
      KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
      return APPLE_AGX_BACKEND_FALSE;
    }
    runtime->CompletionContext =
        (ADMISSION_RENDER_CONTEXT *)(ULONG_PTR)
            adapter->RenderPacket.Description.ContextToken;
    if (runtime->CompletionContext == NULL ||
        runtime->CompletionContext->Object.FenceOutstanding != Fence ||
        !AppleAgxCompletionTransactionBegin(
            &runtime->Completion, Fence, Node, Engine)) {
      runtime->CompletionContext = NULL;
      KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
      return APPLE_AGX_BACKEND_FALSE;
    }
  }
  if (runtime->Completion.Phase == AppleAgxCompletionClaimed) {
    if (!AppleAgxSchedulerCompleteActiveFence(
            &adapter->Scheduler, Node, Engine, Fence) ||
        !AppleAgxCompletionTransactionAdvance(
            &runtime->Completion, Fence, Node, Engine,
            AppleAgxCompletionClaimed,
            AppleAgxCompletionSchedulerCommitted)) {
      KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
      return APPLE_AGX_BACKEND_FALSE;
    }
  }
  if (runtime->Completion.Phase == AppleAgxCompletionSchedulerCommitted) {
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
    if (runtime->CompletedOutput.Phase == AdmissionCompletedOutputEmpty) {
      ADMISSION_OPEN_ALLOCATION *opened =
          (ADMISSION_OPEN_ALLOCATION *)(ULONG_PTR)
              adapter->RenderPacket.Description.AllocationToken;
      ADMISSION_BACKEND_OUTPUT_VIEW output;
      ULONG generation = (ULONG)InterlockedIncrement(
          &runtime->CompletedOutputGeneration);
      if (opened == NULL || opened->Magic != ADMISSION_OPEN_ALLOCATION_MAGIC ||
          opened->Allocation == NULL ||
          AdmissionBackendImageCaptureOutput(
              &adapter->BackendImage, &adapter->RenderPacket.Description,
              &opened->Allocation->Description, &output) != APPLE_AGX_TRUE) {
        KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
        return APPLE_AGX_BACKEND_FALSE;
      }
      if (runtime->DynamicOverlayState.Applied == 1u &&
          runtime->DynamicOverlayState.Fence == Fence) {
        if (APPLE_AGX_WIN32_COMMAND_IS_NATIVE(runtime->DynamicOverlayPlan.CommandVersion)) {
          output.VerificationKind=AdmissionBackendOutputVerificationNativeCapture;
          output.BackgroundColor=output.ExpectedColor=0;
        } else {
          output.VerificationKind=AdmissionBackendOutputVerificationTriangle;
          output.BackgroundColor=runtime->DynamicBackgroundColor;
          output.ExpectedColor=runtime->DynamicExpectedForegroundColor;
        }
      }
      if (!AdmissionCompletedOutputPlatformValid(adapter, &output) ||
          !AdmissionCompletedOutputCapture(
              &runtime->CompletedOutput, generation, Fence, &output,
              opened->Allocation)) {
        KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
        return APPLE_AGX_BACKEND_FALSE;
      }
      runtime->CompletedPacket = adapter->RenderPacket.Description;
    } else if (runtime->CompletedOutput.Fence != Fence) {
      KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
      return APPLE_AGX_BACKEND_FALSE;
    }
    if (runtime->CompletedOutput.Phase == AdmissionCompletedOutputCaptured &&
        (!AdmissionDynamicOverlayReleaseActive(runtime, Fence) ||
         !AdmissionBackendImageReleaseSubmissionRestore(
             &adapter->BackendImage,
             &adapter->BackendSnapshot, Fence) ||
         !AdmissionCompletedOutputMarkReleased(
             &runtime->CompletedOutput, Fence))) {
      KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
      return APPLE_AGX_BACKEND_FALSE;
    }
    if (runtime->CompletedOutput.Phase ==
            AdmissionCompletedOutputBackendReleased &&
        (!AdmissionRenderPacketComplete(&adapter->RenderPacket, Fence) ||
        runtime->CompletionContext == NULL ||
        runtime->CompletionContext->Object.FenceOutstanding != Fence ||
         !AdmissionCompletedOutputMarkPacketRetired(
             &runtime->CompletedOutput, Fence))) {
      KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
      return APPLE_AGX_BACKEND_FALSE;
    }
    if (runtime->CompletedOutput.Phase !=
        AdmissionCompletedOutputPacketRetired) {
      KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
      return APPLE_AGX_BACKEND_FALSE;
    }
#else
    if (!AdmissionDynamicOverlayReleaseActive(runtime, Fence) ||
        !AdmissionBackendImageReleaseSubmissionRestore(
            &adapter->BackendImage,
            &adapter->BackendSnapshot, Fence) ||
        !AdmissionRenderPacketComplete(&adapter->RenderPacket, Fence) ||
        runtime->CompletionContext == NULL ||
        runtime->CompletionContext->Object.FenceOutstanding != Fence) {
      KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
      return APPLE_AGX_BACKEND_FALSE;
    }
#endif
    if (!AppleAgxCompletionTransactionAdvance(
            &runtime->Completion, Fence, Node, Engine,
            AppleAgxCompletionSchedulerCommitted,
            AppleAgxCompletionLocalCommitted)) {
      KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
      return APPLE_AGX_BACKEND_FALSE;
    }
  }
  if (runtime->Completion.Phase == AppleAgxCompletionLocalCommitted)
    local = AppleAgxCompletionTransactionAdvance(
                &runtime->Completion, Fence, Node, Engine,
                AppleAgxCompletionLocalCommitted,
                AppleAgxCompletionBackendRetired)
                ? TRUE
                : FALSE;
  else
    local = runtime->Completion.Phase == AppleAgxCompletionBackendRetired ||
                    runtime->Completion.Phase == AppleAgxCompletionReported
                ? TRUE
                : FALSE;
  if (local && AppleAgxSchedulerPreemptionPhase(&adapter->Scheduler) ==
                   AppleAgxPreemptionWaitCurrentBoundary) {
    preemption_waiting = AppleAgxSchedulerObserveBoundaryCompletion(
                             &adapter->Scheduler, Node, Engine, Fence)
                             ? TRUE
                             : FALSE;
  }
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  if (!local)
    return APPLE_AGX_BACKEND_FALSE;

  if (runtime->Completion.Phase == AppleAgxCompletionBackendRetired) {
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    AdmissionJobTimingMarkWindows(runtime, Fence, AdmissionJobPhaseNotify);
#endif
    notification.Runtime = runtime;
    notification.Fence = Fence;
    notification.Node = Node;
    notification.Engine = Engine;
    sync_status = adapter->Interface.DxgkCbSynchronizeExecution(
        adapter->Interface.DeviceHandle,
        AdmissionNotifyCompletionAtInterrupt, &notification, 0u,
        &reported);
    AdmissionRenderCorrelationSynchronizeWindows(
        adapter, Fence, sync_status, reported);
    if (!NT_SUCCESS(sync_status) || !reported)
      return APPLE_AGX_BACKEND_FALSE;
    AdmissionGdiReceiptCompleteWindows(adapter, Fence,
        (ULONG)Status, TRUE);
  }
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (runtime->Completion.Phase != AppleAgxCompletionReported ||
      !AdmissionGpuvaG3PrivateReported(adapter,runtime->CompletionContext,Fence))
    return APPLE_AGX_BACKEND_FALSE;
#endif
  if (runtime->Completion.Phase != AppleAgxCompletionReported ||
      !AppleAgxCompletionTransactionFinish(
          &runtime->Completion, Fence, Node, Engine))
    return APPLE_AGX_BACKEND_FALSE;
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  AdmissionDwmFrameRecordCompletion(adapter, runtime->CompletionContext, Fence);
#endif
  KeAcquireSpinLock(&adapter->SchedulerLock,&old_irql);
  if (runtime->CompletionContext != NULL &&
      runtime->CompletionContext->Object.FenceOutstanding == Fence)
    runtime->CompletionContext->Object.FenceOutstanding = 0u;
  runtime->CompletionContext = NULL;
  KeReleaseSpinLock(&adapter->SchedulerLock,old_irql);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  if (!AdmissionCompletedOutputMarkNotified(
          &runtime->CompletedOutput, Fence) ||
      runtime->OutputThread == NULL)
    return APPLE_AGX_BACKEND_FALSE;
  KeAcquireSpinLock(&runtime->OutputLock, &old_irql);
  if (!AdmissionOutputQueueSchedule(
          &runtime->OutputQueue, runtime->CompletedOutput.Generation)) {
    KeReleaseSpinLock(&runtime->OutputLock, old_irql);
    return APPLE_AGX_BACKEND_FALSE;
  }
  KeClearEvent(&runtime->OutputIdle);
  KeSetEvent(&runtime->OutputWake, IO_NO_INCREMENT, FALSE);
  KeReleaseSpinLock(&runtime->OutputLock, old_irql);
#endif
  if (preemption_waiting)
    InterlockedExchange(&adapter->SchedulerDpcPending, 1);
  return APPLE_AGX_BACKEND_TRUE;
}

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
static VOID AdmissionOutputProcess(
    ADMISSION_PLATFORM_RUNTIME *runtime, ULONG fence) {
  if (runtime == NULL || fence == 0u)
    return;
  AdmissionRenderCorrelationOutputWindows(runtime->Adapter, fence,
      AdmissionOutputTraceEntry, (ULONG)STATUS_PENDING);
  AdmissionTerminalObserve(
      runtime, fence, AppleAgxBackendCompletionSuccess,
      &runtime->CompletedOutput);
  AdmissionRenderCorrelationOutputWindows(runtime->Adapter, fence,
      AdmissionOutputTraceVerified,
      runtime->CompletedOutput.AccessAttempted
          ? runtime->CompletedOutput.AccessStatus
          : (ULONG)STATUS_INVALID_DEVICE_STATE);
  if (runtime->CompletedOutput.View.VerificationKind ==
      AdmissionBackendOutputVerificationTriangle)
    AdmissionRecordOutputTerminalSnapshot(
        runtime->Adapter, &runtime->TerminalReceipt);
  if (runtime->DynamicOutputSnapshot.Valid == 1u &&
      runtime->DynamicOutputSnapshot.Fence == fence)
    AdmissionRecordDynamicOutputSnapshot(
        runtime->Adapter, &runtime->DynamicOutputSnapshot);
  if (runtime->DynamicGraphReceipt.Valid == 1u &&
      runtime->DynamicGraphReceipt.Fence == fence)
    AdmissionRecordDynamicGraph(
        runtime->Adapter, &runtime->DynamicGraphReceipt);
  if (runtime->DynamicStoreReceipt.Valid == 1u &&
      runtime->DynamicStoreReceipt.Fence == fence)
    AdmissionRecordDynamicStore(
        runtime->Adapter, &runtime->DynamicStoreReceipt);
  if (runtime->CompletedOutput.View.VerificationKind == AdmissionBackendOutputVerificationNativeCapture &&
      runtime->NativeGraphReceipt.Valid == 1u && runtime->NativeGraphReceipt.Fence == fence)
    AdmissionRecordNativeGraph(runtime->Adapter,&runtime->NativeGraphReceipt);
  {
    volatile APPLE_AGX_BACKEND_U32 *taRead;
    volatile APPLE_AGX_BACKEND_U32 *d3Read;
    APPLE_AGX_BACKEND_U32 currentTaRead;
    APPLE_AGX_BACKEND_U32 currentD3Read;
    ADMISSION_QUEUE_FAULT_SNAPSHOT snapshot;
    if (runtime->Provider.Channels.Ta.StateCpuAddress != NULL &&
        runtime->Provider.Channels.D3.StateCpuAddress != NULL) {
      taRead = (volatile APPLE_AGX_BACKEND_U32 *)(
          runtime->Provider.Channels.Ta.StateCpuAddress +
          APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET);
      d3Read = (volatile APPLE_AGX_BACKEND_U32 *)(
          runtime->Provider.Channels.D3.StateCpuAddress +
          APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET);
      if (runtime->TransportIo.ReadU32(
              runtime, taRead, &currentTaRead) &&
          runtime->TransportIo.ReadU32(
              runtime, d3Read, &currentD3Read) &&
          AdmissionCaptureQueueFaultSnapshot(
              runtime, fence, 0u, currentTaRead, currentD3Read, TRUE, FALSE,
              &snapshot))
        AdmissionRecordQueueFaultSnapshot(runtime->Adapter, &snapshot);
    }
  }
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
  if (runtime->CompletedOutput.View.VerificationKind != AdmissionBackendOutputVerificationNativeCapture &&
      runtime->VisibleAgxValid && runtime->VisibleAgxFence == fence &&
      AdmissionCompletedOutputBeginPresent(
          &runtime->CompletedOutput, fence)) {
    NTSTATUS presentStatus;
    AdmissionRenderCorrelationOutputWindows(runtime->Adapter, fence,
        AdmissionOutputTracePresentEntry, (ULONG)STATUS_PENDING);
    presentStatus = AdmissionScanoutPresentAgxResult(
        runtime->Adapter, &runtime->CompletedPacket,
        &runtime->CompletedOutput, &runtime->TerminalReceipt,
        runtime->VisibleAgxSourceAddress,
        runtime->VisibleAgxSourceBytes, runtime->VisibleAgxGpuAddress,
        runtime->VisibleAgxPhysicalAddress, fence);
    AdmissionRenderCorrelationOutputWindows(runtime->Adapter, fence,
        AdmissionOutputTracePresentExit, (ULONG)presentStatus);
    if (!NT_SUCCESS(presentStatus) &&
        runtime->CompletedOutput.Phase != AdmissionCompletedOutputEmpty)
      (void)AdmissionCompletedOutputAbort(
          &runtime->CompletedOutput, fence);
  } else if (runtime->CompletedOutput.Phase !=
             AdmissionCompletedOutputEmpty) {
    (void)AdmissionCompletedOutputAbort(
        &runtime->CompletedOutput, fence);
  }
  runtime->VisibleAgxValid = FALSE;
  runtime->VisibleAgxSourceAddress = NULL;
  runtime->VisibleAgxSourceBytes = 0u;
#else
  if (runtime->CompletedOutput.Phase != AdmissionCompletedOutputEmpty)
    (void)AdmissionCompletedOutputAbort(
        &runtime->CompletedOutput, fence);
#endif
  RtlZeroMemory(&runtime->CompletedPacket,
                sizeof(runtime->CompletedPacket));
}

static VOID AdmissionOutputThread(PVOID Context) {
  ADMISSION_PLATFORM_RUNTIME *runtime =
      (ADMISSION_PLATFORM_RUNTIME *)Context;
  ULONG fence;
  ULONG generation;
  KIRQL oldIrql;
  if (runtime == NULL) {
    PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
    return;
  }
  for (;;) {
    (void)KeWaitForSingleObject(&runtime->OutputWake, Executive,
                                KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&runtime->OutputLock, &oldIrql);
    if (AdmissionOutputQueueCanExit(&runtime->OutputQueue)) {
      (void)AdmissionOutputQueueMarkExited(&runtime->OutputQueue);
      KeSetEvent(&runtime->OutputExited, IO_NO_INCREMENT, FALSE);
      KeReleaseSpinLock(&runtime->OutputLock, oldIrql);
      PsTerminateSystemThread(STATUS_SUCCESS);
      return;
    }
    generation = runtime->OutputQueue.Generation;
    if (!AdmissionOutputQueueBegin(&runtime->OutputQueue, generation)) {
      KeReleaseSpinLock(&runtime->OutputLock, oldIrql);
      InterlockedCompareExchange(&runtime->Adapter->SchedulerFaulted, 0x40000L | __LINE__, 0);
      continue;
    }
    fence = runtime->CompletedOutput.Fence;
    KeReleaseSpinLock(&runtime->OutputLock, oldIrql);
    AdmissionOutputProcess(runtime, fence);
    KeAcquireSpinLock(&runtime->OutputLock, &oldIrql);
    if (AdmissionOutputQueueFinish(&runtime->OutputQueue, generation))
      KeSetEvent(&runtime->OutputIdle, IO_NO_INCREMENT, FALSE);
    else
      InterlockedCompareExchange(&runtime->Adapter->SchedulerFaulted, 0x40000L | __LINE__, 0);
    if (AdmissionOutputQueueCanExit(&runtime->OutputQueue)) {
      (void)AdmissionOutputQueueMarkExited(&runtime->OutputQueue);
      KeSetEvent(&runtime->OutputExited, IO_NO_INCREMENT, FALSE);
      KeReleaseSpinLock(&runtime->OutputLock, oldIrql);
      PsTerminateSystemThread(STATUS_SUCCESS);
      return;
    }
    KeReleaseSpinLock(&runtime->OutputLock, oldIrql);
  }
}
#endif

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
_Use_decl_annotations_ VOID AdmissionPlatformRecordPostDpcHealth(
    ADMISSION_CONTEXT *Context, ULONG Fence) {
  ADMISSION_PLATFORM_RUNTIME *runtime;
  ADMISSION_POST_DPC_HEALTH_RECEIPT record;
  KIRQL oldIrql;
  if (Context == NULL || Fence == 0u || Context->PlatformRuntime == NULL)
    return;
  runtime = (ADMISSION_PLATFORM_RUNTIME *)Context->PlatformRuntime;
  RtlZeroMemory(&record, sizeof(record));
  record.Version = 1u; record.Bytes = sizeof(record); record.Fence = Fence;
  record.SchedulerFaulted = InterlockedCompareExchange(
      &Context->SchedulerFaulted, 0, 0) != 0 ? 1u : 0u;
  KeAcquireSpinLock(&Context->SchedulerLock, &oldIrql);
  record.CurrentFence = AppleAgxSchedulerCurrentFence(&Context->Scheduler, 0u, 0u);
  record.ActiveFence = AppleAgxSchedulerActiveFence(&Context->Scheduler, 0u, 0u);
  record.DispatchedFence = Context->DispatchedFence;
  record.RenderPacketState = (ULONG)AdmissionRenderPacketState(&Context->RenderPacket);
  KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
  record.BackendPhase = (ULONG)runtime->Backend.Phase;
  record.ProviderPhase = (ULONG)runtime->Provider.QueueProvider.Phase;
  record.CompletionPhase = (ULONG)runtime->Completion.Phase;
  record.CompletedOutputPhase = (ULONG)runtime->CompletedOutput.Phase;
  record.OutputQueuePhase = (ULONG)runtime->OutputQueue.ThreadPhase;
  record.WorkScheduled = (ULONG)InterlockedCompareExchange(&runtime->WorkScheduled, 0, 0);
  record.WorkersActive = (ULONG)InterlockedCompareExchange(&runtime->WorkersActive, 0, 0);
  record.DpcPending = (ULONG)InterlockedCompareExchange(&Context->SchedulerDpcPending, 0, 0);
  Context->PostDpcHealth = record;
  KeMemoryBarrier();
  InterlockedExchange(&Context->PostDpcHealthValid, 1);
}
#endif

static APPLE_AGX_BACKEND_BOOL AdmissionBackendRetire(
    void *Context, APPLE_AGX_BACKEND_U32 Fence,
    APPLE_AGX_BACKEND_U32 Node, APPLE_AGX_BACKEND_U32 Engine) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context;
  ADMISSION_CONTEXT *adapter;
  ADMISSION_RENDER_CONTEXT *render_context;
  APPLE_AGX_U32 ignored = 0u;
  ADMISSION_RENDER_PACKET_STATE state;
  BOOLEAN retired = FALSE;
  KIRQL old_irql;
  if (runtime == NULL || Fence == 0u || Node != 0u || Engine != 0u ||
      (InterlockedCompareExchange(&runtime->Stopping, 0, 0) == 0 &&
       InterlockedCompareExchange(&runtime->Resetting, 0, 0) == 0) ||
      !runtime->Backend.QueuesQuiesced)
    return APPLE_AGX_BACKEND_FALSE;
  adapter = runtime->Adapter;
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  state = AdmissionRenderPacketState(&adapter->RenderPacket);
  if ((state == AdmissionRenderPacketQueued ||
       state == AdmissionRenderPacketActive) &&
      adapter->RenderPacket.Description.Fence == Fence) {
    render_context = (ADMISSION_RENDER_CONTEXT *)(ULONG_PTR)
        adapter->RenderPacket.Description.ContextToken;
    if (render_context != NULL &&
        render_context->Object.FenceOutstanding == Fence &&
        AdmissionDynamicOverlayReleaseActive(runtime, Fence) &&
        AdmissionBackendImageReleaseSubmission(
            &adapter->BackendImage, Fence) &&
        AdmissionRenderPacketReset(
            &adapter->RenderPacket, Fence,
            state == AdmissionRenderPacketActive ? 1u : 0u) &&
        AppleAgxSchedulerResetEngine(
            &adapter->Scheduler, Node, Engine, &ignored)) {
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
      AdmissionGpuvaG3PrivateCancel(render_context,Fence,
          state == AdmissionRenderPacketActive ? TRUE : FALSE);
#endif
      render_context->Object.FenceOutstanding = 0u;
      retired = TRUE;
    }
  }
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  return retired ? APPLE_AGX_BACKEND_TRUE : APPLE_AGX_BACKEND_FALSE;
}

static VOID AdmissionPlatformWorkerFinished(
    ADMISSION_PLATFORM_RUNTIME *Runtime) {
  KIRQL oldIrql;
  KeAcquireSpinLock(&Runtime->Adapter->SchedulerLock, &oldIrql);
  InterlockedExchange(&Runtime->WorkScheduled, 0);
  KeReleaseSpinLock(&Runtime->Adapter->SchedulerLock, oldIrql);
  if (InterlockedCompareExchange(&Runtime->Stopping, 0, 0) == 0 &&
      InterlockedCompareExchange(&Runtime->Resetting, 0, 0) == 0) {
    AdmissionSchedulerWorkerFinished(Runtime->Adapter);
    AdmissionDispatchQueuedWork(Runtime->Adapter);
  }
  KeAcquireSpinLock(&Runtime->Adapter->SchedulerLock, &oldIrql);
  if (InterlockedDecrement(&Runtime->WorkersActive) == 0 &&
      InterlockedCompareExchange(&Runtime->WorkScheduled, 0, 0) == 0)
    KeSetEvent(&Runtime->WorkIdle, IO_NO_INCREMENT, FALSE);
  KeReleaseSpinLock(&Runtime->Adapter->SchedulerLock, oldIrql);
  KeSetEvent(&Runtime->SlotEvent, IO_NO_INCREMENT, FALSE);
}

/* The render slot left a busy state (completion, reset, cancelled prepare). */
_Use_decl_annotations_ VOID AdmissionPlatformRuntimeSlotChanged(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context != NULL
      ? (ADMISSION_PLATFORM_RUNTIME *)Context->PlatformRuntime : NULL;
  if (runtime != NULL)
    KeSetEvent(&runtime->SlotEvent, IO_NO_INCREMENT, FALSE);
}

static VOID AdmissionPlatformWorker(
    _In_ PDEVICE_OBJECT DeviceObject, _In_opt_ PVOID Context) {
  ADMISSION_CONTEXT *adapter = Context;
  ADMISSION_PLATFORM_RUNTIME *runtime;
  ADMISSION_RENDER_PACKET_DESCRIPTION description;
  APPLE_AGX_BACKEND_SUBMISSION submission;
  APPLE_AGX_BACKEND_RUNTIME_RESULT result;
#if !defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  APPLE_AGX_RTKIT_SESSION_RESULT heartbeatResult;
#endif
  BOOLEAN activated = FALSE;
  BOOLEAN cancelled = FALSE;
  BOOLEAN deferred = FALSE;
  LARGE_INTEGER pollStart, pollFrequency;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  APPLE_AGX_G13_QUEUE_PROGRESS finalProgress;
#if !defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  APPLE_AGX_RTKIT_SESSION heartbeatSnapshot;
#endif
  ADMISSION_QUEUE_SUBMISSION_RECEIPT queueSubmissionReceipt;
  ADMISSION_QUEUE_INFO_RECEIPT queueInfoReceipt;
  ADMISSION_BUFFER_MANAGER_RECEIPT bufferManagerReceipt;
  BOOLEAN finalProgressValid = FALSE;
  BOOLEAN queueSubmissionCaptured = FALSE;
  BOOLEAN queueInfoCaptured = FALSE;
  BOOLEAN bufferManagerCaptured = FALSE;
  BOOLEAN channelBaselineValid = FALSE;
  BOOLEAN channelProgressReported = FALSE;
  BOOLEAN faultSnapshotReported = FALSE;
  BOOLEAN taProgressReported = FALSE;
  BOOLEAN taRetireReported = FALSE;
  BOOLEAN taTemporalReported = FALSE;
  ADMISSION_TA_TEMPORAL_RECEIPT taTemporal;
  BOOLEAN ktraceBaselineValid = FALSE;
  ULONG initialTaChannelRead = 0u;
  ULONG initialD3ChannelRead = 0u;
  ULONGLONG queueSubmitMs = 0ULL;
  APPLE_AGX_BACKEND_U32 initialKtraceWrite = 0u;
#endif
  KIRQL old_irql;

  UNREFERENCED_PARAMETER(DeviceObject);
  if (adapter == NULL || adapter->PlatformRuntime == NULL)
    return;
  runtime = (ADMISSION_PLATFORM_RUNTIME *)adapter->PlatformRuntime;
  InterlockedIncrement(&runtime->WorkersActive);
  RtlZeroMemory(&description, sizeof(description));
  RtlZeroMemory(&submission, sizeof(submission));
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  RtlZeroMemory(&finalProgress, sizeof(finalProgress));
  RtlZeroMemory(&taTemporal, sizeof(taTemporal));
#if !defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  RtlZeroMemory(&heartbeatSnapshot, sizeof(heartbeatSnapshot));
#endif
  RtlZeroMemory(&queueSubmissionReceipt, sizeof(queueSubmissionReceipt));
  RtlZeroMemory(&queueInfoReceipt, sizeof(queueInfoReceipt));
  RtlZeroMemory(&bufferManagerReceipt, sizeof(bufferManagerReceipt));
  taTemporal.Version = ADMISSION_TA_TEMPORAL_RECEIPT_VERSION;
  taTemporal.Bytes = sizeof(taTemporal);
#endif

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  /* Phase 5a: a submission that queued while the slot was busy is bound
   * here, at PASSIVE_LEVEL, once its fence heads the queue. */
  if (!AdmissionG4PendingBindHead(adapter)) {
    InterlockedCompareExchange(&adapter->SchedulerFaulted, 0x40000L | __LINE__, 0);
    AdmissionPlatformWorkerFinished(runtime);
    return;
  }
#endif
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  cancelled = AdmissionRenderPacketState(&adapter->RenderPacket) == AdmissionRenderPacketEmpty ||
      InterlockedCompareExchange(&runtime->Stopping, 0, 0) != 0 ||
      InterlockedCompareExchange(&runtime->Resetting, 0, 0) != 0;
  deferred = AdmissionRenderPacketState(&adapter->RenderPacket) == AdmissionRenderPacketQueued &&
      (AppleAgxSchedulerActiveFence(&adapter->Scheduler, 0u, 0u) != 0u ||
       AppleAgxSchedulerQueuedFence(&adapter->Scheduler, 0u, 0u) != adapter->RenderPacket.Description.Fence ||
       (adapter->DispatchedFence != 0u && adapter->DispatchedFence != adapter->RenderPacket.Description.Fence));
  if (!cancelled && !deferred && InterlockedCompareExchange(&runtime->Stopping, 0, 0) == 0 &&
      InterlockedCompareExchange(&runtime->Resetting, 0, 0) == 0 &&
      runtime->BackendStarted &&
      runtime->Backend.Phase == AppleAgxBackendRuntimeReady &&
      AdmissionRenderPacketState(&adapter->RenderPacket) ==
          AdmissionRenderPacketQueued) {
    description = adapter->RenderPacket.Description;
    if (AppleAgxSchedulerActivateFence(
            &adapter->Scheduler, 0u, 0u, description.Fence) &&
        AdmissionRenderPacketActivate(
            &adapter->RenderPacket, description.Fence)) {
      adapter->DispatchedFence = description.Fence;
      activated = TRUE;
    }
  }
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  if (!activated) {
    if (!cancelled && !deferred)
      InterlockedCompareExchange(&adapter->SchedulerFaulted, 0x40000L | __LINE__, 0);
    AdmissionPlatformWorkerFinished(runtime);
    return;
  }
  AdmissionRenderCorrelationWorkerWindows(
      adapter, description.Fence, TRUE, (ULONG)STATUS_PENDING);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  AdmissionJobTimingMarkWindows(runtime, description.Fence,
      AdmissionJobPhaseWorker);
  {
    ADMISSION_OPEN_ALLOCATION *opened =
        (ADMISSION_OPEN_ALLOCATION *)(ULONG_PTR)description.AllocationToken;
    if (opened != NULL && opened->Magic == ADMISSION_OPEN_ALLOCATION_MAGIC &&
        opened->Allocation != NULL)
      AdmissionJobTimingTargetWindows(runtime, description.Fence,
          opened->Allocation->Description.Size);
  }
#endif

  submission.Submission.Kind = AppleAgxSubmissionGdi;
  submission.Submission.Fence = description.Fence;
  submission.Submission.NodeOrdinal = 0u;
  submission.Submission.EngineOrdinal = 0u;
  submission.Submission.DmaBytes =
      description.DmaEnd - description.DmaStart;
  submission.ContextIdentity = ADMISSION_MEMORY_UAT_CONTEXT;
  submission.PrivateData =
      (const void *)(ULONG_PTR)description.PrivateDataToken;
  submission.PrivateDataBytes = description.PrivateDataBytes;
  submission.PrivateDataStart = description.PrivateDataStart;
  submission.PrivateDataEnd = description.PrivateDataEnd;
  submission.DmaSubmissionStart = description.DmaStart;
  submission.DmaSubmissionEnd = description.DmaEnd;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  {
    APPLE_AGX_RTKIT_U32 notifications = 0u;
    APPLE_AGX_RTKIT_SESSION_RESULT notificationResult =
        AppleAgxRtkitSessionDrainRuntime(&runtime->Rtkit, &runtime->AscIo,
            APPLE_AGX_RTKIT_RUNTIME_DRAIN_LIMIT, &notifications);
    if (notificationResult != AppleAgxRtkitSessionResultOk) {
      InterlockedCompareExchange(&adapter->SchedulerFaulted,
          0x40000L | __LINE__, 0);
      AdmissionRenderCorrelationWorkerWindows(
          adapter, description.Fence, FALSE, (ULONG)notificationResult);
      AdmissionPlatformWorkerFinished(runtime);
      return;
    }
  }
#endif
#if !defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  /* Legacy diagnostic profiles retain their management probe.  GPUVA jobs
   * use the queue doorbell and completion protocol, not a management pong
   * deadline between software activation and actual hardware submission. */
  InterlockedIncrement(&runtime->HeartbeatReceipt.Sequence);
  ++runtime->HeartbeatReceipt.Calls;
  runtime->HeartbeatReceipt.Fence = description.Fence;
  runtime->HeartbeatReceipt.Result = MAXULONG;
  runtime->HeartbeatReceipt.RxBefore = runtime->Rtkit.ReceivedCount;
  runtime->HeartbeatReceipt.StartMs = AdmissionPlatformNowMs();
  runtime->HeartbeatReceipt.DeadlineMs =
      runtime->HeartbeatReceipt.StartMs + J313_AGX_G2_HEARTBEAT_TIMEOUT_MS;
  heartbeatResult = AppleAgxRtkitSessionHeartbeat(
      &runtime->Rtkit, &runtime->AscIo,
      runtime->HeartbeatReceipt.DeadlineMs);
  runtime->HeartbeatReceipt.EndMs = AdmissionPlatformNowMs();
  runtime->HeartbeatReceipt.Result = (ULONG)heartbeatResult;
  runtime->HeartbeatReceipt.RxAfter = runtime->Rtkit.ReceivedCount;
  runtime->HeartbeatReceipt.LastRxEndpoint = runtime->Rtkit.LastRxEndpoint;
  runtime->HeartbeatReceipt.LastRxPayload = runtime->Rtkit.LastRxPayload;
  InterlockedIncrement(&runtime->HeartbeatReceipt.Sequence);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  heartbeatSnapshot = runtime->Rtkit;
#endif
  if (heartbeatResult != AppleAgxRtkitSessionResultOk) {
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
    AdmissionRecordPreSubmitHeartbeat(
        adapter, heartbeatResult, &heartbeatSnapshot);
#endif
    InterlockedCompareExchange(&adapter->SchedulerFaulted, 0x40000L | __LINE__, 0);
    AdmissionFlushGdiReceipt(adapter);
    AdmissionRenderCorrelationWorkerWindows(
        adapter, description.Fence, FALSE, (ULONG)heartbeatResult);
    AdmissionPlatformWorkerFinished(runtime);
    return;
  }
#endif
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  {
    APPLE_AGX_MEMORY_OBJECT *state = &runtime->Initdata.ChannelMemory.Objects[
        ADMISSION_CHANNEL_OBJECT_KTRACE_STATE];
    if (state->CpuAddress != NULL && state->Length >= 0x24u) {
      volatile APPLE_AGX_BACKEND_U32 *writePointer =
          (volatile APPLE_AGX_BACKEND_U32 *)(
              (unsigned char *)state->CpuAddress +
              APPLE_AGX_PLATFORM_CHANNEL_WRITE_POINTER_OFFSET);
      ktraceBaselineValid = runtime->TransportIo.ReadU32(
          runtime, writePointer, &initialKtraceWrite) &&
          initialKtraceWrite < ADMISSION_KTRACE_RING_ENTRIES;
    }
  }
  taTemporal.Fence = description.Fence;
#endif
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  {
    ADMISSION_RENDER_CONTEXT *g3_context =
        (ADMISSION_RENDER_CONTEXT *)(ULONG_PTR)description.ContextToken;
    if (g3_context != NULL && g3_context->GpuvaG3Process != NULL) {
      /* Keep the backend owner identity. BeginJob and the G4 materializer
       * select the process VM slot independently of this envelope. */
      AdmissionJobTimingMarkWindows(runtime, description.Fence,
          AdmissionJobPhaseBeginBefore);
      if (!NT_SUCCESS(AdmissionGpuvaG3BeginJob(
              adapter, g3_context, description.Fence))) {
        InterlockedCompareExchange(&adapter->SchedulerFaulted, 0x40000L | __LINE__, 0);
        AdmissionPlatformWorkerFinished(runtime);
        return;
      }
      AdmissionJobTimingMarkWindows(runtime, description.Fence,
          AdmissionJobPhaseBeginAfter);
    }
  }
#endif
  if (!AdmissionPrepareG4Manager(runtime)) {
    InterlockedCompareExchange(&adapter->SchedulerFaulted, 0x40000L | __LINE__, 0);
    AdmissionPlatformWorkerFinished(runtime);
    return;
  }
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  AdmissionJobTimingMarkWindows(runtime, description.Fence,
      AdmissionJobPhaseBackendBefore);
#endif
  result = AppleAgxBackendRuntimeSubmit(&runtime->Backend, &submission);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  AdmissionJobTimingMarkWindows(runtime, description.Fence,
      AdmissionJobPhaseBackendAfter);
#endif
  if (result != AppleAgxBackendRuntimeResultOk)
    AdmissionBackendSubmitResultWindows(
        adapter, (ULONG)result, (ULONG)runtime->Backend.Phase);
  AdmissionGdiReceiptBackendWindows(adapter, description.Fence, (ULONG)result,
      result == AppleAgxBackendRuntimeResultOk
          ? &runtime->Backend.PendingJob : NULL);
  if (result != AppleAgxBackendRuntimeResultOk) {
    AdmissionRecordComputeIdentityDiagnostic(
        adapter, &runtime->Provider.QueueProvider.ComputeIdentityDiagnostic);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
    APPLE_AGX_G13_QUEUE_PROGRESS failedProgress;
    RtlZeroMemory(&failedProgress, sizeof(failedProgress));
    failedProgress.Fence = description.Fence;
    failedProgress.TaDonePointer =
        runtime->Provider.QueueProvider.LastSubmitGuard;
    failedProgress.TaStamp =
        runtime->Provider.QueueProvider.LastSubmitRuntimeResult;
    AdmissionGdiReceiptProgressWindows(
        adapter, description.Fence, &failedProgress,
        (ULONG)runtime->Backend.Phase);
#endif
    InterlockedCompareExchange(&adapter->SchedulerFaulted, 0x40000L | __LINE__, 0);
    AdmissionFlushGdiReceipt(adapter);
    AdmissionRenderCorrelationWorkerWindows(
        adapter, description.Fence, FALSE, (ULONG)result);
    AdmissionPlatformWorkerFinished(runtime);
    return;
  }
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  AdmissionTerminalBegin(runtime, &description);
  if (runtime->DynamicOverlayState.Applied == 1u &&
      (APPLE_AGX_WIN32_COMMAND_IS_NATIVE(runtime->DynamicOverlayPlan.CommandVersion)) &&
      AdmissionDynamicOverlayCaptureNativeGraph(&runtime->NativeBindings,&runtime->DynamicOverlayPlan,
          runtime->DynamicJob,runtime->QueueObjects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          runtime->NativeCommandHash,description.Fence,&runtime->NativeGraphReceipt)==AdmissionDynamicOverlaySuccess) {
    runtime->NativeGraphReceipt.CandidateBuild=APPLE_AGX_VERSION_BUILD;
    runtime->NativeGraphReceipt.BootGeneration=adapter->Win32BootGeneration;
  }
  if (runtime->DynamicOverlayState.Applied == 1u &&
      !APPLE_AGX_WIN32_COMMAND_IS_NATIVE(runtime->DynamicOverlayPlan.CommandVersion))
    (void)AdmissionDynamicOverlayCaptureGraph(
        &adapter->BackendImage, &runtime->DynamicOverlayPlan,
        &runtime->DynamicOverlayState, runtime->QueueObjects,
        APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
        description.Fence, &runtime->DynamicGraphReceipt);
#endif
  RtlZeroMemory(&runtime->Progress, sizeof(runtime->Progress));
  runtime->ProgressValid =
      AppleAgxG13QueueProviderQueryProgress(
          &runtime->Provider.QueueProvider, &runtime->Progress)
          ? TRUE
          : FALSE;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  {
    if (AdmissionCaptureQueueSubmission(runtime, &runtime->Progress,
                                        runtime->ProgressValid,
                                        &queueSubmissionReceipt)) {
      initialTaChannelRead = queueSubmissionReceipt.TaChannelReadPointer;
      initialD3ChannelRead = queueSubmissionReceipt.D3ChannelReadPointer;
      channelBaselineValid = TRUE;
      queueSubmissionCaptured = TRUE;
    }
    if (AdmissionCaptureQueueInfo(
            runtime, description.Fence, &queueInfoReceipt))
      queueInfoCaptured = TRUE;
    if (AdmissionCaptureBufferManager(
            runtime, description.Fence, &bufferManagerReceipt))
      bufferManagerCaptured = TRUE;
  }
#endif
  InterlockedExchange64(
      &runtime->LastProgressMs, (LONG64)AdmissionPlatformNowMs());
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  queueSubmitMs = AdmissionPlatformNowMs();
#endif

  pollStart = KeQueryPerformanceCounter(&pollFrequency);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  runtime->FwTs1Fence = description.Fence;
  runtime->FwTs1Polls = 0u;
  runtime->FwTs1Kick = AdmissionFwTs1Read(runtime);
  runtime->FwTs1First = 0ULL;
  runtime->FwTs1FirstQpc = 0ULL;
#endif
  while (runtime->Backend.Phase == AppleAgxBackendRuntimeSubmitted &&
         InterlockedCompareExchange(&runtime->Stopping, 0, 0) == 0 &&
         InterlockedCompareExchange(&runtime->Resetting, 0, 0) == 0) {
    APPLE_AGX_BACKEND_U32 drained = 0u;
    APPLE_AGX_BACKEND_U32 completed = 0u;
    LARGE_INTEGER interval;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    {
      APPLE_AGX_RTKIT_U32 notifications = 0u;
      APPLE_AGX_RTKIT_SESSION_RESULT notificationResult =
          AppleAgxRtkitSessionDrainRuntime(&runtime->Rtkit, &runtime->AscIo,
              APPLE_AGX_RTKIT_RUNTIME_DRAIN_LIMIT, &notifications);
      if (notificationResult != AppleAgxRtkitSessionResultOk) {
        InterlockedCompareExchange(&adapter->SchedulerFaulted,
            0x40000L | __LINE__, 0);
        AdmissionRenderCorrelationWorkerWindows(
            adapter, description.Fence, FALSE, (ULONG)notificationResult);
        break;
      }
    }
#endif
    if (!AppleAgxPlatformProviderPoll(
            &runtime->Provider, 64u, &drained, &completed)) {
      if (runtime->Provider.LastPollGuard ==
          AppleAgxPlatformPollGuardDrainEvents) {
        ADMISSION_EVENT_DRAIN_RECEIPT eventReceipt;
        RtlZeroMemory(&eventReceipt, sizeof(eventReceipt));
        eventReceipt.Version = ADMISSION_EVENT_DRAIN_RECEIPT_VERSION;
        eventReceipt.Bytes = sizeof(eventReceipt);
        eventReceipt.Fence = description.Fence;
        eventReceipt.PollGuard = runtime->Provider.LastPollGuard;
        eventReceipt.DrainGuard = runtime->Provider.LastDrainGuard;
        eventReceipt.ReadPointer = runtime->Provider.LastEventReadPointer;
        eventReceipt.WritePointer = runtime->Provider.LastEventWritePointer;
        eventReceipt.IngestGuard =
            runtime->Provider.QueueProvider.LastIngestGuard;
        eventReceipt.RuntimeResult =
            runtime->Provider.QueueProvider.LastIngestRuntimeResult;
        eventReceipt.MessageValid = runtime->Provider.LastEventMessageValid;
        if (runtime->Provider.LastEventMessageValid) {
          RtlCopyMemory(
              eventReceipt.Message, runtime->Provider.LastEventMessage,
              sizeof(eventReceipt.Message));
        }
        AdmissionRecordEventDrain(adapter, &eventReceipt);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
        if (!faultSnapshotReported) {
          volatile APPLE_AGX_BACKEND_U32 *taRead =
              (volatile APPLE_AGX_BACKEND_U32 *)(
                  runtime->Provider.Channels.Ta.StateCpuAddress +
                  APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET);
          volatile APPLE_AGX_BACKEND_U32 *d3Read =
              (volatile APPLE_AGX_BACKEND_U32 *)(
                  runtime->Provider.Channels.D3.StateCpuAddress +
                  APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET);
          APPLE_AGX_BACKEND_U32 currentTaRead;
          APPLE_AGX_BACKEND_U32 currentD3Read;
          ADMISSION_QUEUE_FAULT_SNAPSHOT snapshot;
          ULONGLONG nowMs = AdmissionPlatformNowMs();
          if (runtime->TransportIo.ReadU32(runtime, taRead, &currentTaRead) &&
              runtime->TransportIo.ReadU32(runtime, d3Read, &currentD3Read) &&
              AdmissionCaptureQueueFaultSnapshot(
                  runtime, description.Fence, nowMs - queueSubmitMs,
                  currentTaRead, currentD3Read, TRUE, TRUE, &snapshot)) {
            AdmissionRecordQueueFaultSnapshot(adapter, &snapshot);
            faultSnapshotReported = TRUE;
          }
        }
        if (!taProgressReported) {
          ADMISSION_TA_PROGRESS_RECEIPT taProgress;
          ULONGLONG nowMs = AdmissionPlatformNowMs();
          if (AdmissionCaptureTaProgress(
                  runtime, description.Fence, nowMs - queueSubmitMs,
                  &taProgress)) {
            AdmissionRecordTaProgress(adapter, &taProgress);
            taProgressReported = TRUE;
          }
        }
        if (!taRetireReported) {
          ADMISSION_TA_RETIRE_RECEIPT taRetire;
          ULONGLONG nowMs = AdmissionPlatformNowMs();
          if (AdmissionCaptureTaRetire(
                  runtime, description.Fence, nowMs - queueSubmitMs,
                  &taRetire)) {
            AdmissionRecordTaRetire(adapter, &taRetire);
            taRetireReported = TRUE;
          }
        }
#endif
        AdmissionProviderDrainTraceWindows(
            adapter, runtime->Provider.LastDrainGuard,
            runtime->Provider.LastEventReadPointer,
            runtime->Provider.LastEventWritePointer);
      }
      AdmissionProviderPollGuardWindows(
          adapter, runtime->Provider.LastPollGuard,
          (ULONG)runtime->Provider.QueueProvider.Phase,
          (ULONG)runtime->Backend.Phase, description.Fence);
      InterlockedCompareExchange(&adapter->SchedulerFaulted, 0x40000L | __LINE__, 0);
      break;
    }
    UNREFERENCED_PARAMETER(drained);
    UNREFERENCED_PARAMETER(completed);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
    if (channelBaselineValid && !channelProgressReported) {
      volatile APPLE_AGX_BACKEND_U32 *taRead =
          (volatile APPLE_AGX_BACKEND_U32 *)(
              runtime->Provider.Channels.Ta.StateCpuAddress +
              APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET);
      volatile APPLE_AGX_BACKEND_U32 *d3Read =
          (volatile APPLE_AGX_BACKEND_U32 *)(
              runtime->Provider.Channels.D3.StateCpuAddress +
              APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET);
      APPLE_AGX_BACKEND_U32 currentTaRead;
      APPLE_AGX_BACKEND_U32 currentD3Read;
      if (runtime->TransportIo.ReadU32(runtime, taRead, &currentTaRead) &&
          runtime->TransportIo.ReadU32(runtime, d3Read, &currentD3Read) &&
          currentTaRead < APPLE_AGX_PLATFORM_COMMAND_RING_ENTRY_COUNT &&
          currentD3Read < APPLE_AGX_PLATFORM_COMMAND_RING_ENTRY_COUNT &&
          (currentTaRead != initialTaChannelRead ||
           currentD3Read != initialD3ChannelRead)) {
        AdmissionBackendChannelProgressWindows(
            adapter, currentTaRead, currentD3Read, description.Fence);
        channelProgressReported = TRUE;
      }
    }
    if (runtime->Backend.Phase == AppleAgxBackendRuntimeSubmitted &&
        !faultSnapshotReported) {
      ULONGLONG nowMs = AdmissionPlatformNowMs();
      if (nowMs >= queueSubmitMs + ADMISSION_QUEUE_FAULT_SNAPSHOT_DELAY_MS) {
        volatile APPLE_AGX_BACKEND_U32 *taRead =
            (volatile APPLE_AGX_BACKEND_U32 *)(
                runtime->Provider.Channels.Ta.StateCpuAddress +
                APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET);
        volatile APPLE_AGX_BACKEND_U32 *d3Read =
            (volatile APPLE_AGX_BACKEND_U32 *)(
                runtime->Provider.Channels.D3.StateCpuAddress +
                APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET);
        APPLE_AGX_BACKEND_U32 currentTaRead;
        APPLE_AGX_BACKEND_U32 currentD3Read;
        ADMISSION_QUEUE_FAULT_SNAPSHOT snapshot;
        if (runtime->TransportIo.ReadU32(runtime, taRead, &currentTaRead) &&
            runtime->TransportIo.ReadU32(runtime, d3Read, &currentD3Read) &&
            AdmissionCaptureQueueFaultSnapshot(
                runtime, description.Fence, nowMs - queueSubmitMs,
                currentTaRead, currentD3Read, FALSE, TRUE, &snapshot)) {
          AdmissionRecordQueueFaultSnapshot(adapter, &snapshot);
          faultSnapshotReported = TRUE;
        }
      }
    }
    if (runtime->Backend.Phase == AppleAgxBackendRuntimeSubmitted &&
        !taProgressReported) {
      ULONGLONG nowMs = AdmissionPlatformNowMs();
      if (nowMs >= queueSubmitMs + ADMISSION_QUEUE_FAULT_SNAPSHOT_DELAY_MS) {
        ADMISSION_TA_PROGRESS_RECEIPT taProgress;
        if (AdmissionCaptureTaProgress(
                runtime, description.Fence, nowMs - queueSubmitMs,
                &taProgress)) {
          AdmissionRecordTaProgress(adapter, &taProgress);
          taProgressReported = TRUE;
        }
      }
    }
    if (runtime->Backend.Phase == AppleAgxBackendRuntimeSubmitted &&
        !taRetireReported) {
      ULONGLONG nowMs = AdmissionPlatformNowMs();
      if (nowMs >= queueSubmitMs + ADMISSION_QUEUE_FAULT_SNAPSHOT_DELAY_MS) {
        ADMISSION_TA_RETIRE_RECEIPT taRetire;
        if (AdmissionCaptureTaRetire(
                runtime, description.Fence, nowMs - queueSubmitMs,
                &taRetire)) {
          AdmissionRecordTaRetire(adapter, &taRetire);
          taRetireReported = TRUE;
        }
      }
    }
    if (runtime->Backend.Phase == AppleAgxBackendRuntimeSubmitted &&
        !taTemporalReported) {
      ULONGLONG nowMs = AdmissionPlatformNowMs();
      ULONGLONG elapsedMs = nowMs - queueSubmitMs;
      if (taTemporal.SampleCount == 0u &&
          elapsedMs >= ADMISSION_QUEUE_FAULT_SNAPSHOT_DELAY_MS &&
          AdmissionCaptureTaTemporalSample(
              runtime, elapsedMs, &taTemporal.Samples[0]))
        taTemporal.SampleCount = 1u;
      if (taTemporal.SampleCount == 1u &&
          elapsedMs >= ADMISSION_TA_TEMPORAL_SECOND_DELAY_MS &&
          AdmissionCaptureTaTemporalSample(
              runtime, elapsedMs, &taTemporal.Samples[1])) {
        taTemporal.SampleCount = ADMISSION_TA_TEMPORAL_SAMPLE_COUNT;
        AdmissionRecordTaTemporal(adapter, &taTemporal);
        taTemporalReported = TRUE;
      }
    }
#endif
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    /* EXP1113 receipt-only: the TA start timestamp lives in ts1 only until
     * the TA end overwrites it; keep the first change seen while polling. */
    if (runtime->Backend.Phase == AppleAgxBackendRuntimeSubmitted &&
        runtime->FwTs1Fence == description.Fence && runtime->FwTs1First == 0ULL) {
      ULONGLONG ts1 = AdmissionFwTs1Read(runtime);
      ++runtime->FwTs1Polls;
      if (ts1 != runtime->FwTs1Kick) {
        runtime->FwTs1First = ts1;
        runtime->FwTs1FirstQpc = AdmissionJobQpc();
      }
    }
#endif
    if (runtime->Backend.Phase == AppleAgxBackendRuntimeSubmitted) {
      APPLE_AGX_G13_QUEUE_PROGRESS current;
      if (AppleAgxG13QueueProviderQueryProgress(
              &runtime->Provider.QueueProvider, &current) &&
          (!runtime->ProgressValid ||
           AppleAgxG13QueueProgressHasAdvanced(
               &runtime->Progress, &current))) {
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
        AdmissionJobTimingMarkWindows(runtime, description.Fence,
            AdmissionJobPhaseFirstProgress);
#endif
        AdmissionBackendProgressWindows(adapter, &current);
        runtime->Progress = current;
        runtime->ProgressValid = TRUE;
        InterlockedExchange64(
            &runtime->LastProgressMs,
            (LONG64)AdmissionPlatformNowMs());
      }
    }
    if (runtime->Backend.Phase != AppleAgxBackendRuntimeSubmitted)
      break;
    {
      /* EXP1073: poll short jobs at a fine step before sleeping ticks. */
      LARGE_INTEGER pollNow = KeQueryPerformanceCounter(NULL);
      ULONG stallUs = AdmissionJobPollStallUs(pollFrequency.QuadPart > 0 ?
          (ULONGLONG)(pollNow.QuadPart - pollStart.QuadPart) * 1000000ULL /
              (ULONGLONG)pollFrequency.QuadPart : ~0ULL);
      if (stallUs != 0u) {
        KeStallExecutionProcessor(stallUs);
        continue;
      }
    }
    interval.QuadPart = -10000LL;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    AdmissionJobTimingDelayWindows(runtime, description.Fence);
#endif
    if (!NT_SUCCESS(KeDelayExecutionThread(
            KernelMode, FALSE, &interval))) {
      InterlockedCompareExchange(&adapter->SchedulerFaulted, 0x40000L | __LINE__, 0);
      break;
    }
  }
  if (runtime->Backend.Phase != AppleAgxBackendRuntimeReady &&
      InterlockedCompareExchange(&runtime->Stopping, 0, 0) == 0 &&
      InterlockedCompareExchange(&runtime->Resetting, 0, 0) == 0)
    InterlockedCompareExchange(&adapter->SchedulerFaulted, 0x40000L | __LINE__, 0);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
  /* EXP629: successful visible qualification is exported through the bounded
   * per-frame Escape record. Do not hold WorkScheduled across storage-backed
   * legacy receipt writes after a completed D589. */
  UNREFERENCED_PARAMETER(finalProgress);
  UNREFERENCED_PARAMETER(finalProgressValid);
#else
#if !defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  AdmissionRecordPreSubmitHeartbeat(
      adapter, heartbeatResult, &heartbeatSnapshot);
#endif
  if (queueSubmissionCaptured)
    AdmissionRecordQueueSubmission(adapter, &queueSubmissionReceipt);
  if (queueInfoCaptured)
    AdmissionRecordQueueInfo(adapter, &queueInfoReceipt);
  if (bufferManagerCaptured)
    AdmissionRecordBufferManager(adapter, &bufferManagerReceipt);
  if (!taTemporalReported && taTemporal.SampleCount != 0u)
    AdmissionRecordTaTemporal(adapter, &taTemporal);
  if (ktraceBaselineValid) {
    ADMISSION_KTRACE_RECEIPT ktraceReceipt;
    if (AdmissionCaptureKTrace(
            runtime, description.Fence, initialKtraceWrite, &ktraceReceipt))
      AdmissionRecordKTrace(adapter, &ktraceReceipt);
  }
  RtlZeroMemory(&finalProgress, sizeof(finalProgress));
  finalProgressValid = runtime->Backend.Phase ==
                               AppleAgxBackendRuntimeSubmitted &&
                       AppleAgxG13QueueProviderQueryProgress(
                           &runtime->Provider.QueueProvider, &finalProgress)
                   ? TRUE
                   : FALSE;
  if (finalProgressValid)
    AdmissionGdiReceiptProgressWindows(adapter, description.Fence,
        &finalProgress, (ULONG)runtime->Backend.Phase);
  AdmissionFlushGdiReceipt(adapter);
  AdmissionTerminalExit(runtime);
#endif
#endif
  AdmissionRenderCorrelationWorkerWindows(
      adapter, description.Fence, FALSE, (ULONG)runtime->Backend.Phase);
  if (runtime->FirmwareRecoveryUnpublished) {
    /* EXP1075 receipt: {recoveries, last event kind, last fence}. */
    HANDLE key = NULL;
    UNICODE_STRING name;
    ULONG value[3];
    runtime->FirmwareRecoveryUnpublished = FALSE;
    value[0] = (ULONG)InterlockedCompareExchange(&runtime->FirmwareRecoveries, 0, 0);
    value[1] = runtime->LastFirmwareRecoveryKind;
    value[2] = runtime->LastFirmwareRecoveryFence;
    if (NT_SUCCESS(IoOpenDeviceRegistryKey(adapter->PhysicalDeviceObject,
            PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
      RtlInitUnicodeString(&name, L"Wom1FirmwareRecovery");
      if (NT_SUCCESS(ZwSetValueKey(key, &name, 0u, REG_BINARY, value,
                                   sizeof(value))))
        (void)ZwFlushKey(key);
      ZwClose(key);
    }
  }
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  AdmissionJobTimingExportWindows(runtime);
#endif
  AdmissionPlatformWorkerFinished(runtime);
}

_Use_decl_annotations_ BOOLEAN AdmissionPlatformRuntimeSubmit(
    ADMISSION_CONTEXT *Context) {
  KIRQL oldIrql;
  ADMISSION_PLATFORM_RUNTIME *runtime =
      Context != NULL
          ? (ADMISSION_PLATFORM_RUNTIME *)Context->PlatformRuntime
          : NULL;
  if (runtime == NULL || runtime->WorkItem == NULL)
    return FALSE;
  KeAcquireSpinLock(&Context->SchedulerLock, &oldIrql);
  if (InterlockedCompareExchange(&runtime->Stopping, 0, 0) != 0 ||
      InterlockedCompareExchange(&runtime->Resetting, 0, 0) != 0 ||
      runtime->Backend.Phase != AppleAgxBackendRuntimeReady ||
      InterlockedCompareExchange(&runtime->WorkScheduled, 1, 0) != 0) {
    KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
    return FALSE;
  }
  KeClearEvent(&runtime->WorkIdle);
  KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
  IoQueueWorkItem(runtime->WorkItem, AdmissionPlatformWorker,
                  DelayedWorkQueue, Context);
  return TRUE;
}

static NTSTATUS AdmissionPlatformDestroy(
    ADMISSION_PLATFORM_RUNTIME *Runtime) {
  NTSTATUS status = STATUS_SUCCESS;
  KIRQL oldIrql;
  PIO_WORKITEM workItem;
  if (Runtime == NULL)
    return STATUS_SUCCESS;
  InterlockedExchange(&Runtime->Stopping, 1);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  if (Runtime->OutputThread != NULL) {
    KeAcquireSpinLock(&Runtime->OutputLock, &oldIrql);
    if (!AdmissionOutputQueueRequestStop(&Runtime->OutputQueue)) {
      KeReleaseSpinLock(&Runtime->OutputLock, oldIrql);
      return STATUS_DEVICE_BUSY;
    }
    KeSetEvent(&Runtime->OutputWake, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Runtime->OutputLock, oldIrql);
    (void)KeWaitForSingleObject(&Runtime->OutputExited, Executive,
                                KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&Runtime->OutputLock, &oldIrql);
    if (!AdmissionOutputQueueIsIdle(&Runtime->OutputQueue) ||
        !AdmissionOutputQueueThreadExited(&Runtime->OutputQueue)) {
      KeReleaseSpinLock(&Runtime->OutputLock, oldIrql);
      return STATUS_DEVICE_BUSY;
    }
    KeReleaseSpinLock(&Runtime->OutputLock, oldIrql);
    status = ZwClose(Runtime->OutputThread);
    if (!NT_SUCCESS(status))
      return status;
    Runtime->OutputThread = NULL;
  }
#endif
  if (Runtime->WorkItem != NULL) {
    KeWaitForSingleObject(&Runtime->WorkIdle, Executive, KernelMode,
                          FALSE, NULL);
    KeAcquireSpinLock(&Runtime->Adapter->SchedulerLock, &oldIrql);
    if (InterlockedCompareExchange(&Runtime->WorkScheduled, 0, 0) != 0 ||
        InterlockedCompareExchange(&Runtime->WorkersActive, 0, 0) != 0) {
      KeReleaseSpinLock(&Runtime->Adapter->SchedulerLock, oldIrql);
      return STATUS_DEVICE_BUSY;
    }
    workItem = Runtime->WorkItem;
    Runtime->WorkItem = NULL;
    KeReleaseSpinLock(&Runtime->Adapter->SchedulerLock, oldIrql);
    IoFreeWorkItem(workItem);
  }
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  if (Runtime->CompletedOutput.Phase != AdmissionCompletedOutputEmpty &&
      !AdmissionCompletedOutputAbort(
          &Runtime->CompletedOutput, Runtime->CompletedOutput.Fence))
    return STATUS_DEVICE_BUSY;
#endif
  if (Runtime->BackendStarted ||
      Runtime->Backend.Phase != AppleAgxBackendRuntimeStopped) {
    if (AppleAgxBackendRuntimeStop(&Runtime->Backend) !=
        AppleAgxBackendRuntimeResultOk)
      return STATUS_DEVICE_BUSY;
    Runtime->BackendStarted = FALSE;
  }
  if (Runtime->ProviderReady) {
    if (!AppleAgxPlatformProviderDestroy(&Runtime->Provider))
      return STATUS_DEVICE_BUSY;
    Runtime->ProviderReady = FALSE;
  }
  if (Runtime->FirmwareProvider.State != 0u &&
      AppleAgxFirmwareProviderDestroy(&Runtime->FirmwareProvider) !=
          AppleAgxFirmwareProviderResultOk)
    return STATUS_DEVICE_BUSY;
  if (Runtime->RetainedPrepared || Runtime->Context0Lease.Count || Runtime->Context0Lease.Uncertain)
    return STATUS_DEVICE_BUSY;
  if (Runtime->Powered) {
    APPLE_AGX_POWER_IO io;
    AdmissionPowerIo(Runtime, &io);
    if (!AppleAgxPowerRelease(&io))
      return STATUS_DEVICE_BUSY;
    Runtime->Powered = FALSE;
  }
  if (Runtime->Initdata.Initialized &&
      AppleAgxInitdataMemoryDestroy(&Runtime->Initdata) !=
          AppleAgxInitdataMemoryResultOk)
    return STATUS_DEVICE_BUSY;
  if (Runtime->HandoffBase != NULL) {
    status = Runtime->Adapter->Interface.DxgkCbUnmapMemory(
        Runtime->Adapter->Interface.DeviceHandle,
        (PVOID)Runtime->HandoffBase);
    if (!NT_SUCCESS(status))
      return status;
    Runtime->HandoffBase = NULL;
  }
  if (Runtime->SgxBase != NULL) {
    status = Runtime->Adapter->Interface.DxgkCbUnmapMemory(
        Runtime->Adapter->Interface.DeviceHandle,
        (PVOID)Runtime->SgxBase);
    if (!NT_SUCCESS(status))
      return status;
    Runtime->SgxBase = NULL;
  }
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionPlatformRuntimeStart(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_PLATFORM_RUNTIME *runtime;
  APPLE_AGX_GFX_HANDOFF_REGION handoff_region;
  APPLE_AGX_BACKEND_RUNTIME_RESULT backend_result;
  PHYSICAL_ADDRESS address;
  NTSTATUS status;

  if (Context == NULL)
    return STATUS_INVALID_DEVICE_STATE;
  AdmissionRecordPlatformStage(Context, AdmissionPlatformEntered,
                               STATUS_PENDING);
  if (Context->PlatformRuntime != NULL || !Context->InterfaceValid ||
      KeGetCurrentIrql() != PASSIVE_LEVEL ||
      Context->BackendImage.Ready != APPLE_AGX_TRUE ||
      !AdmissionMemoryRuntimeContextPublished(Context)) {
    AdmissionRecordPlatformStage(Context, AdmissionPlatformEntered,
                                 STATUS_INVALID_DEVICE_STATE);
    return STATUS_INVALID_DEVICE_STATE;
  }
  status = AdmissionPlatformValidateResources(Context);
  AdmissionRecordPlatformStage(Context, AdmissionPlatformResources, status);
  if (!NT_SUCCESS(status))
    return status;
  runtime = ExAllocatePool2(
      POOL_FLAG_NON_PAGED, sizeof(*runtime), ADMISSION_PLATFORM_TAG);
  if (runtime == NULL) {
    AdmissionRecordPlatformStage(
        Context, AdmissionPlatformRuntimeAllocated,
        STATUS_INSUFFICIENT_RESOURCES);
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  AdmissionRecordPlatformStage(Context, AdmissionPlatformRuntimeAllocated,
                               STATUS_SUCCESS);
  RtlZeroMemory(runtime, sizeof(*runtime));
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  {
    LARGE_INTEGER frequency;
    (void)KeQueryPerformanceCounter(&frequency);
    KeInitializeSpinLock(&runtime->JobTimingLock);
    AdmissionJobTimingInitialize(&runtime->JobTiming,
        (ULONGLONG)frequency.QuadPart);
  }
#endif
  runtime->Adapter = Context;
  AdmissionDynamicOverlayStateInitialize(&runtime->DynamicOverlayState);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  AdmissionCompletedOutputInitialize(&runtime->CompletedOutput);
  KeInitializeEvent(&runtime->OutputWake, SynchronizationEvent, FALSE);
  KeInitializeEvent(&runtime->OutputExited, NotificationEvent, FALSE);
  KeInitializeEvent(&runtime->OutputIdle, NotificationEvent, TRUE);
  KeInitializeSpinLock(&runtime->OutputLock);
  AdmissionOutputQueueInitialize(&runtime->OutputQueue);
#endif
  Context->PlatformRuntime = runtime;
  status = AdmissionMemoryRuntimeBorrowIo(Context, &runtime->MemoryIo);
  AdmissionRecordPlatformStage(Context, AdmissionPlatformMemoryIo, status);
  if (!NT_SUCCESS(status))
    goto Fail;
  status = AdmissionPlatformReadSnapshot(Context, &runtime->Snapshot);
  AdmissionRecordPlatformStage(Context, AdmissionPlatformSnapshot, status);
  if (!NT_SUCCESS(status))
    goto Fail;

  address.QuadPart = J313_AGX_G2_SGX_MMIO_BASE;
  status = Context->Interface.DxgkCbMapMemory(
      Context->Interface.DeviceHandle, address, J313_AGX_G2_SGX_MMIO_SIZE,
      FALSE, FALSE, MmNonCached, (PVOID *)&runtime->SgxBase);
  if (!NT_SUCCESS(status) || runtime->SgxBase == NULL) {
    status = NT_SUCCESS(status) ? STATUS_NONE_MAPPED : status;
    AdmissionRecordPlatformStage(Context, AdmissionPlatformSgxMap, status);
    goto Fail;
  }
  AdmissionRecordPlatformStage(Context, AdmissionPlatformSgxMap,
                               STATUS_SUCCESS);
  runtime->AscTransport.Base =
      runtime->SgxBase +
      (J313_AGX_G2_ASC_MMIO_BASE - J313_AGX_G2_SGX_MMIO_BASE);
  runtime->AscTransport.Length = J313_AGX_G2_ASC_MMIO_SIZE;
  runtime->AscIo.Context = &runtime->AscTransport;
  runtime->AscIo.NowMs = AdmissionAscNow;
  runtime->AscIo.Read32 = AdmissionAscRead32;
  runtime->AscIo.Read64 = AdmissionAscRead64;
  runtime->AscIo.Write32 = AdmissionAscWrite32;
  runtime->AscIo.Write64 = AdmissionAscWrite64;
  runtime->AscIo.Pause = AdmissionAscPause;
  AppleAgxRtkitSessionInitialize(&runtime->Rtkit);

  address.QuadPart = J313_AGX_G2_HANDOFF_BASE;
  status = Context->Interface.DxgkCbMapMemory(
      Context->Interface.DeviceHandle, address, J313_AGX_G2_HANDOFF_SIZE,
      FALSE, FALSE, MmNonCached, (PVOID *)&runtime->HandoffBase);
  if (!NT_SUCCESS(status) || runtime->HandoffBase == NULL) {
    status = NT_SUCCESS(status) ? STATUS_NONE_MAPPED : status;
    AdmissionRecordPlatformStage(Context, AdmissionPlatformHandoffMap, status);
    goto Fail;
  }
  AdmissionRecordPlatformStage(Context, AdmissionPlatformHandoffMap,
                               STATUS_SUCCESS);
  runtime->HandoffIo.Context = runtime;
  runtime->HandoffIo.Read8 = AdmissionHandoffRead8;
  runtime->HandoffIo.Read32 = AdmissionHandoffRead32;
  runtime->HandoffIo.Read64 = AdmissionHandoffRead64;
  runtime->HandoffIo.Write8 = AdmissionHandoffWrite8;
  runtime->HandoffIo.Write32 = AdmissionHandoffWrite32;
  runtime->HandoffIo.Write64 = AdmissionHandoffWrite64;
  runtime->HandoffIo.Barrier = AdmissionHandoffBarrier;
  runtime->HandoffIo.Relax = AdmissionHandoffRelax;
  runtime->HandoffIo.Now = AdmissionHandoffNow;
  handoff_region.PhysicalBase = J313_AGX_G2_HANDOFF_BASE;
  handoff_region.Length = J313_AGX_G2_HANDOFF_SIZE;
  if (AppleAgxGfxHandoffBindJ313(
          &runtime->Handoff, &handoff_region,
          &runtime->HandoffIo) != AppleAgxGfxHandoffResultOk) {
    status = STATUS_DEVICE_PROTOCOL_ERROR;
    AdmissionRecordPlatformStage(Context, AdmissionPlatformHandoffBind, status);
    goto Fail;
  }
  AdmissionRecordPlatformStage(Context, AdmissionPlatformHandoffBind,
                               STATUS_SUCCESS);
  if (AppleAgxInitdataMemoryPrepareBroker(
          &runtime->Initdata, &runtime->MemoryIo,
          &runtime->Snapshot) != AppleAgxInitdataMemoryResultOk) {
    status = STATUS_INSUFFICIENT_RESOURCES;
    AdmissionRecordPlatformStage(Context, AdmissionPlatformInitdata, status);
    goto Fail;
  }
  RtlCopyMemory(runtime->QueueObjects, Context->BackendImage.Objects,
                sizeof(runtime->QueueObjects));
  if (!AppleAgxRenderSharedMemoryBindRelocationObjects(
          &runtime->Initdata.RenderSharedMemory,
          Context->BackendImage.ArenaCpuAddress,
          Context->BackendImage.ArenaBytes,
          runtime->QueueObjects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT) ||
      !AppleAgxApplyRelocations(
          runtime->QueueObjects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          AppleAgxRenderTemplateRelocations(),
          AppleAgxRenderTemplateRelocationCount()) ||
      !AppleAgxRenderSharedMemoryInitializeComputeQueue(
          &runtime->Initdata.RenderSharedMemory)) {
    status = STATUS_INVALID_IMAGE_FORMAT;
    AdmissionRecordPlatformStage(Context, AdmissionPlatformInitdata, status);
    goto Fail;
  }
  runtime->QueueImageReady = TRUE;
  AdmissionRecordPlatformStage(Context, AdmissionPlatformInitdata,
                               STATUS_SUCCESS);


  runtime->TransportIo.Context = runtime;
  runtime->TransportIo.FlushForDevice = AdmissionTransportFlush;
  runtime->TransportIo.FlushForCpu = AdmissionTransportFlush;
  runtime->TransportIo.MemoryBarrier = AdmissionTransportBarrier;
  runtime->TransportIo.PublishU32 = AdmissionTransportPublishU32;
  runtime->TransportIo.ReadU32 = AdmissionTransportReadU32;
  runtime->TransportIo.RingDoorbell = AdmissionTransportDoorbell;
  runtime->TransportIo.Quiesce = AdmissionTransportQuiesce;
  runtime->TransportIo.NowTicks = AdmissionTransportNow;
  runtime->QueueIo.Context = runtime;
  runtime->QueueIo.FlushForDevice = AdmissionTransportFlush;
  runtime->QueueIo.MemoryBarrier = AdmissionTransportBarrier;
  runtime->QueueIo.PublishU32 = AdmissionTransportPublishU32;
  runtime->QueueIo.ReadU32 = AdmissionTransportReadU32;
  runtime->QueueIo.Quiesce = AdmissionTransportQuiesce;
  runtime->QueueIo.Recover = AdmissionTransportRecover;

  runtime->FirmwarePrimitives.Context = runtime;
  runtime->FirmwarePrimitives.IsPassiveLevel = AdmissionFirmwareAtPassive;
  runtime->FirmwarePrimitives.NowMs = AdmissionFirmwareNow;
  runtime->FirmwarePrimitives.PowerOn = AdmissionFirmwarePowerOn;
  runtime->FirmwarePrimitives.PowerOff = AdmissionFirmwarePowerOff;
  runtime->FirmwarePrimitives.CreateFirmwareUat =
      AdmissionFirmwareCreateUat;
  runtime->FirmwarePrimitives.DestroyFirmwareUat =
      AdmissionFirmwareDestroyUat;
  runtime->FirmwarePrimitives.BootAsc = AdmissionFirmwareBootAsc;
  runtime->FirmwarePrimitives.CompleteManagementBootstrap =
      AdmissionFirmwareCompleteManagement;
  runtime->FirmwarePrimitives.RecordBootstrapPhase =
      AdmissionFirmwareRecordBootstrap;
  runtime->FirmwarePrimitives.StopAsc = AdmissionFirmwareStopAsc;
  runtime->FirmwarePrimitives.StartEndpoint =
      AdmissionFirmwareStartEndpoint;
  runtime->FirmwarePrimitives.StopEndpoint =
      AdmissionFirmwareStopEndpoint;
  runtime->FirmwarePrimitives.PublishUatRoots = AdmissionFirmwarePublishUat;
  runtime->FirmwarePrimitives.UnpublishUatRoots =
      AdmissionFirmwareUnpublishUat;
  runtime->FirmwarePrimitives.RetireMappingsAfterAscStop=1;
  runtime->FirmwarePrimitives.SendInitdata = AdmissionFirmwareSendInitdata;
  runtime->FirmwarePrimitives.SendDeviceControlInit =
      AdmissionFirmwareDeviceControlInit;
  runtime->FirmwarePrimitives.UpdateIdleTimestamp =
      AdmissionFirmwareIdleTimestamp;
  if (AppleAgxFirmwareProviderInitialize(
          &runtime->FirmwareProvider, &runtime->FirmwarePrimitives,
          &runtime->Handoff, &runtime->FirmwareIo) !=
      AppleAgxFirmwareProviderResultOk) {
    status = STATUS_DEVICE_CONFIGURATION_ERROR;
    AdmissionRecordPlatformStage(
        Context, AdmissionPlatformFirmwareProvider, status);
    goto Fail;
  }
  runtime->FirmwareIo.RecordPhase = AdmissionFirmwareRecordPhase;
  AdmissionRecordPlatformStage(Context, AdmissionPlatformFirmwareProvider,
                               STATUS_SUCCESS);
#if defined(APPLE_AGX_MANAGEMENT_QUALIFICATION) || defined(APPLE_AGX_STOP_AFTER_ENDPOINTS) || defined(APPLE_AGX_FIRMWARE_QUALIFICATION)
  {
    APPLE_AGX_FIRMWARE qualification;
    APPLE_AGX_FIRMWARE_RESULT result;
    APPLE_AGX_FIRMWARE_RESULT cleanup=AppleAgxFirmwareResultOk;
    ULONG completed;
    ULONG primary;
    AppleAgxFirmwareInitialize(&qualification);
    runtime->CaptureFirmwareStart=TRUE;
    RtlZeroMemory(&runtime->FirmwareStartFailure,sizeof(runtime->FirmwareStartFailure));
    result = AppleAgxFirmwareStart(&qualification,&runtime->FirmwareIo);
    runtime->CaptureFirmwareStart=FALSE;
    AdmissionRecordRetainedTrace(Context,runtime->AscTransport.Trace,
        runtime->AscTransport.TraceCount*sizeof(runtime->AscTransport.Trace[0]));
    completed=runtime->FirmwareStartFailure.Captured?
        runtime->FirmwareStartFailure.CompletedMask:qualification.CompletedMask;
    primary=runtime->FirmwareStartFailure.Captured?runtime->FirmwareStartFailure.Result:result;
    /* Qualification uses the production firmware path, then stops before any
     * backend/queue provider. Preserve primary result separately from cleanup. */
    if (qualification.CleanupMask)
      cleanup=AppleAgxFirmwareRollback(&qualification,&runtime->FirmwareIo);
    AdmissionRecordFirmwareQualification(Context,primary,result,completed,cleanup);
    status = result == AppleAgxFirmwareResultCleanupFailed || cleanup != AppleAgxFirmwareResultOk
        ? STATUS_DEVICE_BUSY : STATUS_DEVICE_HARDWARE_ERROR;
    goto Fail;
  }
#endif

  AppleAgxBackendRuntimeInitialize(
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
      &runtime->Backend, 1u);
#else
      &runtime->Backend, ADMISSION_MEMORY_UAT_CONTEXT);
#endif
  RtlZeroMemory(&runtime->RenderIo, sizeof(runtime->RenderIo));
  runtime->RenderIo.Context = runtime;
  runtime->RenderIo.RenderContext.Publish = AdmissionRenderPublish;
  runtime->RenderIo.RenderContext.Unpublish = AdmissionRenderUnpublish;
  RtlZeroMemory(&runtime->ProviderConfig, sizeof(runtime->ProviderConfig));
  runtime->ProviderConfig.ChannelMemory =
      &runtime->Initdata.ChannelMemory;
  runtime->ProviderConfig.DeferFirmwareMappings = APPLE_AGX_BACKEND_TRUE;
  runtime->ProviderConfig.Transport = runtime->TransportIo;
  runtime->ProviderConfig.Firmware = &runtime->FirmwareIo;
  runtime->ProviderConfig.Render = &runtime->RenderIo;
  runtime->ProviderConfig.RenderSharedMemory =
      &runtime->Initdata.RenderSharedMemory;
  runtime->ProviderConfig.Runtime = &runtime->Backend;
  runtime->ProviderConfig.QueueConfig.TimeoutTicks =
      ADMISSION_PLATFORM_QUEUE_TIMEOUT_MS;
  runtime->ProviderConfig.QueueRuntimeIo = runtime->QueueIo;
  runtime->ProviderConfig.ExternalRender.Context = runtime;
  runtime->ProviderConfig.ExternalRender.BuildJob =
      AdmissionExternalBuildJob;
  runtime->ProviderConfig.ExternalRender.ResolvePreparedRange =
      AdmissionExternalResolveRange;
  if (!AppleAgxPlatformProviderInitialize(
          &runtime->Provider, &runtime->ProviderConfig,
          &runtime->PlatformIo)) {
    status = STATUS_DEVICE_CONFIGURATION_ERROR;
    AdmissionRecordPlatformStage(Context, AdmissionPlatformQueueProvider,
                                 status);
    goto Fail;
  }
  AdmissionRecordPlatformStage(Context, AdmissionPlatformQueueProvider,
                               STATUS_SUCCESS);
  runtime->ProviderReady = TRUE;

  RtlZeroMemory(&runtime->RuntimeIo, sizeof(runtime->RuntimeIo));
  runtime->RuntimeIo.Context = runtime;
  runtime->RuntimeIo.Firmware = runtime->PlatformIo.Firmware;
  runtime->RuntimeIo.Memory.Map = AdmissionBackendMap;
  runtime->RuntimeIo.Memory.Unmap = AdmissionBackendUnmap;
  runtime->RuntimeIo.Memory.Resolve = AdmissionBackendResolve;
  runtime->RuntimeIo.Memory.FlushForDevice = AdmissionTransportFlush;
  runtime->RuntimeIo.Memory.FlushForCpu = AdmissionTransportFlush;
  runtime->RuntimeIo.Image.AcquirePrepared = AdmissionBackendAcquire;
  runtime->RuntimeIo.Image.Relocate = AdmissionBackendRelocate;
  runtime->RuntimeIo.RenderContext.Publish = AdmissionRenderPublish;
  runtime->RuntimeIo.RenderContext.Unpublish = AdmissionRenderUnpublish;
  runtime->RuntimeIo.Queues.Create = AdmissionQueuesCreate;
  runtime->RuntimeIo.Queues.Destroy = AdmissionQueuesDestroy;
  runtime->RuntimeIo.Queues.Run3d = AdmissionQueuesRun3d;
  runtime->RuntimeIo.Queues.RunTa = AdmissionQueuesRunTa;
  runtime->RuntimeIo.Queues.Stop = AdmissionQueuesStop;
  runtime->RuntimeIo.Queues.Reset = AdmissionQueuesReset;
  runtime->RuntimeIo.Complete = AdmissionBackendComplete;
  runtime->RuntimeIo.Retire = AdmissionBackendRetire;
  AppleAgxCompletionTransactionInitialize(&runtime->Completion);
  KeInitializeEvent(&runtime->WorkIdle, NotificationEvent, TRUE);
  KeInitializeEvent(&runtime->SlotEvent, NotificationEvent, FALSE);
  InterlockedExchange(&runtime->WorkScheduled, 0);
  InterlockedExchange(&runtime->WorkersActive, 0);
  InterlockedExchange(&runtime->Stopping, 0);
  InterlockedExchange(&runtime->Resetting, 0);
  InterlockedExchange64(&runtime->LastProgressMs, 0);
  backend_result = AppleAgxBackendRuntimeStart(
      &runtime->Backend, &runtime->RuntimeIo);
  AdmissionRecordBackendStartResult(Context, backend_result);
#ifdef APPLE_AGX_BACKEND_QUALIFICATION
  AdmissionRecordRetainedTrace(Context,runtime->AscTransport.Trace,
      runtime->AscTransport.TraceCount*sizeof(runtime->AscTransport.Trace[0]));
  AdmissionRecordBackendQualification(Context,1,backend_result,runtime->Backend.Phase,
      (runtime->Backend.ArenaMapped?1u:0u)|(runtime->Backend.ContextPublished?2u:0u)|
      (runtime->Backend.QueuesCreated?4u:0u),runtime->Backend.ArenaGpuAddress,
      runtime->Backend.ArenaBytes);
#endif
  if (backend_result != AppleAgxBackendRuntimeResultOk) {
    status = STATUS_DEVICE_HARDWARE_ERROR;
    AdmissionRecordPlatformStage(Context, AdmissionPlatformBackendStart,
                                 status);
    goto Fail;
  }
  AdmissionRecordPlatformStage(Context, AdmissionPlatformBackendStart,
                               STATUS_SUCCESS);
  runtime->BackendStarted = TRUE;
#ifdef APPLE_AGX_BACKEND_QUALIFICATION
  status=STATUS_DEVICE_HARDWARE_ERROR; /* stop before automatic Windows workloads */
  goto Fail;
#endif
  runtime->WorkItem = IoAllocateWorkItem(Context->PhysicalDeviceObject);
  if (runtime->WorkItem == NULL) {
    status = STATUS_INSUFFICIENT_RESOURCES;
    AdmissionRecordPlatformStage(Context, AdmissionPlatformWorkItem, status);
    goto Fail;
  }
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  {
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(
        &attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!AdmissionOutputQueueStartThread(&runtime->OutputQueue)) {
      status = STATUS_INVALID_DEVICE_STATE;
      AdmissionRecordPlatformStage(Context, AdmissionPlatformWorkItem, status);
      goto Fail;
    }
    status = PsCreateSystemThread(
        &runtime->OutputThread, THREAD_ALL_ACCESS, &attributes,
        NULL, NULL, AdmissionOutputThread, runtime);
  }
  if (!NT_SUCCESS(status)) {
    AdmissionOutputQueueInitialize(&runtime->OutputQueue);
    AdmissionRecordPlatformStage(Context, AdmissionPlatformWorkItem, status);
    goto Fail;
  }
#endif
  AdmissionRecordPlatformStage(Context, AdmissionPlatformWorkItem,
                               STATUS_SUCCESS);
  /* Job completion, ASC replies and paging quiescence are polled with 1 ms
   * KeDelayExecutionThread waits. At the default 15.6 ms clock each wait
   * lasts until the next tick (EXP1043-EXP1054 job timing: one wait per job,
   * kick->complete uniform over 1-16 ms). Hold a 1 ms clock while the GPU
   * runtime is started; AdmissionPlatformRuntimeStop releases it. */
  (void)ExSetTimerResolution(ADMISSION_PLATFORM_TIMER_RESOLUTION, TRUE);
  runtime->TimerResolutionRaised = TRUE;
  AdmissionRecordPlatformStage(Context, AdmissionPlatformComplete,
                               STATUS_SUCCESS);
  return STATUS_SUCCESS;

Fail:
  {
    NTSTATUS cleanup=AdmissionPlatformDestroy(runtime);
#ifdef APPLE_AGX_BACKEND_QUALIFICATION
    AdmissionRecordBackendQualification(Context,2,(ULONG)cleanup,runtime->Backend.Phase,
        (runtime->Backend.ArenaMapped?1u:0u)|(runtime->Backend.ContextPublished?2u:0u)|
        (runtime->Backend.QueuesCreated?4u:0u),runtime->Backend.ArenaGpuAddress,
        runtime->Backend.ArenaBytes);
#endif
    if(!NT_SUCCESS(cleanup)) return STATUS_DEVICE_BUSY;
  }
  Context->PlatformRuntime = NULL;
  ExFreePoolWithTag(runtime, ADMISSION_PLATFORM_TAG);
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionPlatformRuntimeStop(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_PLATFORM_RUNTIME *runtime;
  NTSTATUS status;
  if (Context == NULL)
    return STATUS_INVALID_PARAMETER;
  runtime = (ADMISSION_PLATFORM_RUNTIME *)Context->PlatformRuntime;
  if (runtime == NULL)
    return STATUS_SUCCESS;
  status = AdmissionPlatformDestroy(runtime);
  if (!NT_SUCCESS(status))
    return status;
  if (runtime->TimerResolutionRaised) {
    (void)ExSetTimerResolution(0u, FALSE);
    runtime->TimerResolutionRaised = FALSE;
  }
  Context->PlatformRuntime = NULL;
  ExFreePoolWithTag(runtime, ADMISSION_PLATFORM_TAG);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionPlatformRuntimeReset(
    ADMISSION_CONTEXT *Context, APPLE_AGX_U32 *LastAbortedFence) {
  ADMISSION_PLATFORM_RUNTIME *runtime;
  APPLE_AGX_U32 active;
  NTSTATUS status = STATUS_SUCCESS;
  KIRQL old_irql;

  if (Context == NULL || LastAbortedFence == NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_PARAMETER;
  *LastAbortedFence = 0u;
  runtime = (ADMISSION_PLATFORM_RUNTIME *)Context->PlatformRuntime;
  if (runtime == NULL || !runtime->BackendStarted ||
      InterlockedCompareExchange(&runtime->Stopping, 0, 0) != 0 ||
      InterlockedCompareExchange(&runtime->Resetting, 1, 0) != 0)
    return STATUS_INVALID_DEVICE_STATE;
  if (InterlockedCompareExchange(&runtime->WorkScheduled, 0, 0) != 0 ||
      InterlockedCompareExchange(&runtime->WorkersActive, 0, 0) != 0)
    KeWaitForSingleObject(&runtime->WorkIdle, Executive, KernelMode,
                          FALSE, NULL);
  KeAcquireSpinLock(&Context->SchedulerLock, &old_irql);
  active = AppleAgxSchedulerActiveFence(&Context->Scheduler, 0u, 0u);
  if (active == 0u ||
      AdmissionRenderPacketState(&Context->RenderPacket) !=
          AdmissionRenderPacketActive ||
      Context->RenderPacket.Description.Fence != active) {
    KeReleaseSpinLock(&Context->SchedulerLock, old_irql);
    status = STATUS_INVALID_DEVICE_STATE;
    goto Exit;
  }
  KeReleaseSpinLock(&Context->SchedulerLock, old_irql);
  *LastAbortedFence = active;
  if (AppleAgxBackendRuntimeStop(&runtime->Backend) !=
      AppleAgxBackendRuntimeResultOk) {
    status = STATUS_DEVICE_HARDWARE_ERROR;
    goto Exit;
  }
  runtime->BackendStarted = FALSE;
  if (!AppleAgxPlatformProviderDestroy(&runtime->Provider)) {
    status = STATUS_DEVICE_HARDWARE_ERROR;
    goto Exit;
  }
  runtime->ProviderReady = FALSE;
  if (!AdmissionBackendImageRestartQueueLifetime(&Context->BackendImage)) {
    status = STATUS_INVALID_DEVICE_STATE;
    goto Exit;
  }

  /* The stopped firmware lifetime must not donate its copied private prefix
   * to the next boot. Rebuild only our owned graph before channel providers
   * borrow any addresses; context63 and its physical owner are unchanged. */
  if (runtime->RetainedPrepared || runtime->Context0Lease.Count || runtime->Rtkit.Running ||
      AppleAgxInitdataMemoryDestroy(&runtime->Initdata) !=
          AppleAgxInitdataMemoryResultOk ||
      AppleAgxInitdataMemoryPrepareBroker(&runtime->Initdata, &runtime->MemoryIo,
          &runtime->Snapshot) != AppleAgxInitdataMemoryResultOk) {
    status = STATUS_DEVICE_HARDWARE_ERROR;
    goto Exit;
  }

  AppleAgxBackendRuntimeInitialize(
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
      &runtime->Backend, 1u);
#else
      &runtime->Backend, ADMISSION_MEMORY_UAT_CONTEXT);
#endif
  runtime->ProviderConfig.Runtime = &runtime->Backend;
  RtlZeroMemory(&runtime->PlatformIo, sizeof(runtime->PlatformIo));
  if (!AppleAgxPlatformProviderInitialize(
          &runtime->Provider, &runtime->ProviderConfig,
          &runtime->PlatformIo)) {
    status = STATUS_DEVICE_CONFIGURATION_ERROR;
    goto Exit;
  }
  runtime->ProviderReady = TRUE;
  runtime->RuntimeIo.Firmware = runtime->PlatformIo.Firmware;
  AppleAgxCompletionTransactionInitialize(&runtime->Completion);
  AdmissionDynamicOverlayStateInitialize(&runtime->DynamicOverlayState);
  RtlZeroMemory(&runtime->DynamicOverlayPlan,
                sizeof(runtime->DynamicOverlayPlan));
  runtime->DynamicJob = NULL;
  runtime->DynamicStorage = NULL;
  runtime->DynamicStorageBytes = 0u;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  runtime->NativeCommandHash=0;
  RtlZeroMemory(&runtime->NativeBindings,sizeof(runtime->NativeBindings));
  RtlZeroMemory(&runtime->NativeGraphReceipt,sizeof(runtime->NativeGraphReceipt));
#endif
  runtime->CompletionContext = NULL;
  RtlZeroMemory(&runtime->Progress, sizeof(runtime->Progress));
  runtime->ProgressValid = FALSE;
  InterlockedExchange64(&runtime->LastProgressMs, 0);
  if (AppleAgxBackendRuntimeStart(
          &runtime->Backend, &runtime->RuntimeIo) !=
      AppleAgxBackendRuntimeResultOk) {
    status = STATUS_DEVICE_HARDWARE_ERROR;
    goto Exit;
  }
  runtime->BackendStarted = TRUE;
  InterlockedExchange(&Context->SchedulerFaulted, 0);

Exit:
  InterlockedExchange(&runtime->Resetting, 0);
  if (!NT_SUCCESS(status))
    InterlockedCompareExchange(&Context->SchedulerFaulted, 0x40000L | __LINE__, 0);
  return status;
}

_Use_decl_annotations_ BOOLEAN AdmissionPlatformRuntimeResponsive(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_PLATFORM_RUNTIME *runtime;
  ULONGLONG now;
  ULONGLONG last;
  if (Context == NULL ||
      InterlockedCompareExchange(&Context->SchedulerFaulted, 0, 0) != 0)
    return FALSE;
  runtime = (ADMISSION_PLATFORM_RUNTIME *)Context->PlatformRuntime;
  if (runtime == NULL || !runtime->BackendStarted ||
      InterlockedCompareExchange(&runtime->Stopping, 0, 0) != 0 ||
      InterlockedCompareExchange(&runtime->Resetting, 0, 0) != 0)
    return FALSE;
  if (runtime->Backend.Phase == AppleAgxBackendRuntimeReady)
    return TRUE;
  if (runtime->Backend.Phase != AppleAgxBackendRuntimeSubmitted)
    return FALSE;
  last = (ULONGLONG)InterlockedCompareExchange64(
      &runtime->LastProgressMs, 0, 0);
  now = AdmissionPlatformNowMs();
  return last != 0ULL && now >= last &&
                 now - last < ADMISSION_PLATFORM_QUEUE_TIMEOUT_MS
             ? TRUE
             : FALSE;
}

_Use_decl_annotations_ BOOLEAN AdmissionPlatformRenderWorkerScheduled(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context != NULL
      ? (ADMISSION_PLATFORM_RUNTIME *)Context->PlatformRuntime : NULL;
  return runtime != NULL &&
      InterlockedCompareExchange(&runtime->WorkScheduled, 0, 0) != 0;
}

_Use_decl_annotations_ BOOLEAN AdmissionPlatformRuntimeReadyEx(
    ADMISSION_CONTEXT *Context, ULONG *FailedPredicate) {
  ADMISSION_PLATFORM_RUNTIME *runtime =
      Context != NULL
          ? (ADMISSION_PLATFORM_RUNTIME *)Context->PlatformRuntime
          : NULL;
  ULONG reason = 0u;
/* Keep short-circuit order, including lock-protected output state. */
#define ADMISSION_RUNTIME_REJECTS(id, expression) \
  ((expression) ? (reason = (id), TRUE) : FALSE)
  BOOLEAN ready = !(ADMISSION_RUNTIME_REJECTS(1u, runtime == NULL) ||
      ADMISSION_RUNTIME_REJECTS(2u, !runtime->ProviderReady) ||
      ADMISSION_RUNTIME_REJECTS(3u, !runtime->BackendStarted) ||
      ADMISSION_RUNTIME_REJECTS(4u,
          runtime->Backend.Phase != AppleAgxBackendRuntimeReady) ||
      ADMISSION_RUNTIME_REJECTS(5u, runtime->WorkItem == NULL) ||
      ADMISSION_RUNTIME_REJECTS(6u,
          InterlockedCompareExchange(&runtime->Stopping, 0, 0) != 0) ||
      ADMISSION_RUNTIME_REJECTS(7u,
          InterlockedCompareExchange(&runtime->Resetting, 0, 0) != 0) ||
      ADMISSION_RUNTIME_REJECTS(8u,
          InterlockedCompareExchange(&runtime->WorkScheduled, 0, 0) != 0));
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  if (ready) {
    KIRQL oldIrql;
    KeAcquireSpinLock(&runtime->OutputLock, &oldIrql);
    ready = !(ADMISSION_RUNTIME_REJECTS(9u,
                  !AdmissionOutputQueueIsIdle(&runtime->OutputQueue)) ||
              ADMISSION_RUNTIME_REJECTS(10u,
                  !AdmissionOutputQueueThreadRunning(&runtime->OutputQueue)) ||
              ADMISSION_RUNTIME_REJECTS(11u,
                  runtime->CompletedOutput.Phase != AdmissionCompletedOutputEmpty));
    KeReleaseSpinLock(&runtime->OutputLock, oldIrql);
  }
#endif
#undef ADMISSION_RUNTIME_REJECTS
  if (FailedPredicate != NULL) *FailedPredicate = reason;
  return ready;
}

_Use_decl_annotations_ BOOLEAN AdmissionPlatformRuntimeReady(
    ADMISSION_CONTEXT *Context) {
  return AdmissionPlatformRuntimeReadyEx(Context, NULL);
}

_Use_decl_annotations_ BOOLEAN AdmissionPlatformRuntimeQueueable(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context != NULL
      ? (ADMISSION_PLATFORM_RUNTIME *)Context->PlatformRuntime : NULL;
  return runtime != NULL && runtime->ProviderReady &&
      runtime->BackendStarted && runtime->WorkItem != NULL &&
      InterlockedCompareExchange(&runtime->Stopping, 0, 0) == 0 &&
      InterlockedCompareExchange(&runtime->Resetting, 0, 0) == 0 &&
      (runtime->Backend.Phase == AppleAgxBackendRuntimeReady ||
       runtime->Backend.Phase == AppleAgxBackendRuntimeSubmitted)
      ? TRUE : FALSE;
}

/* EXP1013/EXP1014: the backend worker reports the completed fence before it
 * returns the backend to Ready and clears WorkScheduled, and VidSch may submit
 * the next DMA buffer while a job is still running.  Neither is malformed
 * input, and STATUS_INVALID_PARAMETER from SubmitCommandVirtual puts the device
 * in error (DXGKDDI_SUBMITCOMMANDVIRTUAL), so the single render slot applies
 * bounded backpressure instead: wait (PASSIVE_LEVEL, no lock held) until the
 * runtime is ready and the render slot is empty.  Non-transient refusals
 * (stopping, resetting, failed, missing provider) return immediately. */
_Use_decl_annotations_ BOOLEAN AdmissionPlatformRuntimeAwaitWork(
    ADMISSION_CONTEXT *Context, ULONG TimeoutMs, ULONG *FailedPredicate) {
  ADMISSION_PLATFORM_RUNTIME *runtime = Context != NULL
      ? (ADMISSION_PLATFORM_RUNTIME *)Context->PlatformRuntime : NULL;
  ULONGLONG deadline = KeQueryInterruptTime() + (ULONGLONG)TimeoutMs * 10000ULL;
  LARGE_INTEGER slice;
  ULONG reason = 0u;
  slice.QuadPart = -100000LL; /* 10 ms backstop */
  for (;;) {
    /* Clear before testing: a release after the test sets it again. */
    if (runtime != NULL)
      KeClearEvent(&runtime->SlotEvent);
    if (AdmissionPlatformRuntimeReadyEx(Context, &reason)) {
      KIRQL oldIrql;
      BOOLEAN empty;
      KeAcquireSpinLock(&Context->SchedulerLock, &oldIrql);
      /* Phase 5a: an immediate bind must not overtake queued submissions. */
      empty = AdmissionRenderPacketState(&Context->RenderPacket) ==
          AdmissionRenderPacketEmpty && Context->G4PendingCount == 0u;
      KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
      if (empty) {
        reason = 0u;
        break;
      }
      reason = 16u; /* render slot still owned by the previous job */
    } else if (!(reason == 8u ||
                 (reason == 4u &&
                  runtime->Backend.Phase == AppleAgxBackendRuntimeSubmitted))) {
      break;
    }
    if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
        KeQueryInterruptTime() >= deadline)
      break;
    /* EXP1118/EXP1119: a timer sleep here held a job queued behind a
     * concurrent submitter for ~10 ms after the slot had freed. */
    (void)KeWaitForSingleObject(&runtime->SlotEvent, Executive, KernelMode,
                                FALSE, &slice);
  }
  if (FailedPredicate != NULL) *FailedPredicate = reason;
  return reason == 0u;
}

#undef ADMISSION_DELEGATE_FENCE
#undef ADMISSION_DELEGATE_JOB
#undef ADMISSION_DELEGATE_ZERO

#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
_Use_decl_annotations_ void AdmissionGpuvaB1RecordRetirement(
    ADMISSION_CONTEXT *Context, ULONG Owner, ULONG Step, NTSTATUS Status,
    ULONG BrokerStatus, ULONGLONG BrokerReceipt, ULONGLONG BrokerEpoch) {
  ADMISSION_PLATFORM_RUNTIME *runtime;
  ADMISSION_B1_RETIREMENT_RECEIPT receipt;
  const APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *config;
  volatile APPLE_AGX_BACKEND_U32 *eventRead;
  volatile APPLE_AGX_BACKEND_U32 *eventWrite;
  APPLE_AGX_BACKEND_U32 raw;
  if (Context == NULL || Owner >= 2u ||
      Step >= AdmissionB1RetireStepCount)
    return;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = 1u;
  receipt.Bytes = sizeof(receipt);
  receipt.Owner = Owner;
  receipt.Step = Step;
  receipt.Status = (ULONG)Status;
  receipt.BrokerStatus = BrokerStatus;
  receipt.BrokerReceipt = BrokerReceipt;
  receipt.BrokerEpoch = BrokerEpoch;
  runtime = (ADMISSION_PLATFORM_RUNTIME *)Context->PlatformRuntime;
  if (runtime != NULL) {
    config = &runtime->Provider.QueueProvider.Config;
    receipt.TaExpectedStamp = runtime->Backend.PendingJob.TaExpectedStamp;
    receipt.TaExpectedDone = runtime->Backend.PendingJob.TaExpectedDonePointer;
    receipt.D3ExpectedStamp = runtime->Backend.PendingJob.D3ExpectedStamp;
    receipt.D3ExpectedDone = runtime->Backend.PendingJob.D3ExpectedDonePointer;
    receipt.BackendPhase = (ULONG)runtime->Backend.Phase;
    receipt.TaComplete = runtime->Backend.TaComplete;
    receipt.D3Complete = runtime->Backend.D3Complete;
    receipt.B1Completed = (ULONG)InterlockedCompareExchange(
        &runtime->B1Completed, 0, 0);
    receipt.PollGuard = runtime->Provider.LastPollGuard;
    receipt.DrainGuard = runtime->Provider.LastDrainGuard;
    receipt.IngestGuard = runtime->Provider.QueueProvider.LastIngestGuard;
    if (config->Ta.Stamp != NULL &&
        runtime->TransportIo.ReadU32(runtime, config->Ta.Stamp, &raw))
      receipt.TaStamp = raw;
    if (config->Ta.GpuDonePointer != NULL &&
        runtime->TransportIo.ReadU32(runtime, config->Ta.GpuDonePointer, &raw))
      receipt.TaDone = raw;
    if (config->D3.Stamp != NULL &&
        runtime->TransportIo.ReadU32(runtime, config->D3.Stamp, &raw))
      receipt.D3Stamp = raw;
    if (config->D3.GpuDonePointer != NULL &&
        runtime->TransportIo.ReadU32(runtime, config->D3.GpuDonePointer, &raw))
      receipt.D3Done = raw;
    if (runtime->Provider.Channels.Event.StateCpuAddress != NULL) {
      eventRead = (volatile APPLE_AGX_BACKEND_U32 *)(
          runtime->Provider.Channels.Event.StateCpuAddress +
          APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET);
      eventWrite = (volatile APPLE_AGX_BACKEND_U32 *)(
          runtime->Provider.Channels.Event.StateCpuAddress +
          APPLE_AGX_PLATFORM_CHANNEL_WRITE_POINTER_OFFSET);
      if (runtime->TransportIo.ReadU32(runtime, eventRead, &raw))
        receipt.EventRead = raw;
      if (runtime->TransportIo.ReadU32(runtime, eventWrite, &raw))
        receipt.EventWrite = raw;
    }
  }
  AdmissionRecordB1Retirement(Context, &receipt);
}

typedef struct _ADMISSION_B1_COMPLETION_CONTEXT {
  ADMISSION_CONTEXT *Adapter;
  ADMISSION_PLATFORM_RUNTIME *Runtime;
  ULONG Owner;
} ADMISSION_B1_COMPLETION_CONTEXT;

static unsigned int AdmissionB1FlushOutput(void *Opaque,
                                             const void *Address,
                                             unsigned int Bytes) {
  ADMISSION_B1_COMPLETION_CONTEXT *context = Opaque;
  BOOLEAN ok = context->Runtime->TransportIo.FlushForCpu(
      context->Runtime, Address, Bytes);
  AdmissionGpuvaB1RecordRetirement(context->Adapter, context->Owner,
      AdmissionB1RetireCpuFlush,
      ok ? STATUS_SUCCESS : STATUS_DEVICE_HARDWARE_ERROR, 0u, 0ULL, 0ULL);
  return ok ? 1u : 0u;
}

static unsigned int AdmissionB1ReleaseImage(void *Opaque,
                                              unsigned int Fence) {
  ADMISSION_B1_COMPLETION_CONTEXT *context = Opaque;
  BOOLEAN ok = AdmissionBackendImageReleaseSubmission(
      &context->Adapter->BackendImage, Fence);
  AdmissionGpuvaB1RecordRetirement(context->Adapter, context->Owner,
      AdmissionB1RetireImageRelease,
      ok ? STATUS_SUCCESS : STATUS_DEVICE_HARDWARE_ERROR, 0u, 0ULL, 0ULL);
  return ok ? 1u : 0u;
}

_Use_decl_annotations_ NTSTATUS AdmissionGpuvaB1RunFirmware(
    ADMISSION_CONTEXT *Context, PVOID OutputCpu,
    ULONGLONG OutputPhysical, ULONGLONG OutputVa, ULONG Fence,
    ULONG Owner) {
  ADMISSION_PLATFORM_RUNTIME *runtime;
  APPLE_AGX_GDI_COMMAND_DESCRIPTION description;
  APPLE_AGX_EXP208_GDI_BINDING binding;
  ADMISSION_RENDER_PACKET_DESCRIPTION packet;
  APPLE_AGX_BACKEND_SUBMISSION submission;
  unsigned char shadowStorage[512];
  unsigned char dma[sizeof(APPLE_AGX_GDI_DMA_COMMAND)];
  APPLE_AGX_U32 written = 0u;
  ULONGLONG deadline;
  APPLE_AGX_BACKEND_RUNTIME_RESULT result;
  if (Context == NULL || OutputCpu == NULL || OutputPhysical == 0ULL ||
      OutputVa == 0ULL || Fence == 0u || Owner >= 2u ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_PARAMETER;
  runtime = (ADMISSION_PLATFORM_RUNTIME *)Context->PlatformRuntime;
  if (runtime == NULL || !runtime->BackendStarted ||
      runtime->Backend.Phase != AppleAgxBackendRuntimeReady ||
      runtime->Backend.ContextIdentity != 1u ||
      InterlockedCompareExchange(&runtime->B1Active, 0, 0) != 0)
    return STATUS_INVALID_DEVICE_STATE;
  RtlZeroMemory(&description, sizeof(description));
  RtlZeroMemory(&binding, sizeof(binding));
  RtlZeroMemory(&packet, sizeof(packet));
  RtlZeroMemory(&submission, sizeof(submission));
  RtlZeroMemory(shadowStorage, sizeof(shadowStorage));
  description.Command.Opcode = AppleAgxGdiColorFill;
  description.Command.Destination = (APPLE_AGX_GDI_RECT){
      0u, 0u, APPLE_AGX_EXP208_GDI_WIDTH, APPLE_AGX_EXP208_GDI_HEIGHT};
  description.Command.DestinationAllocationIndex = 0u;
  description.Command.DestinationGpuAddress = OutputVa;
  description.Command.DestinationPitch = APPLE_AGX_EXP208_GDI_PITCH;
  description.Command.Color = APPLE_AGX_EXP208_GDI_COLOR;
  description.Command.Rop = AppleAgxGdiColorFillPatCopy;
  if (!AppleAgxGdiEncodeDmaCommand(
          &description, dma, sizeof(dma), &written) ||
      written != sizeof(dma))
    return STATUS_INVALID_PARAMETER;
  packet.Fence = Fence;
  packet.DestinationCpuToken = (ULONGLONG)(ULONG_PTR)OutputCpu;
  packet.DestinationGpuVa = OutputVa;
  packet.DestinationPhysical = OutputPhysical;
  packet.DestinationBytes = 0x4000u;
  if (!AdmissionBackendImageBindSubmission(
          &Context->BackendImage, &packet, OutputCpu, dma, written,
          &binding))
    return STATUS_INVALID_ADDRESS;
  if (!AppleAgxGpuvaB1PrepareSubmission(
          shadowStorage, sizeof(shadowStorage), dma, written,
          Fence, 1u, &submission))
    return STATUS_INVALID_BUFFER_SIZE;
  runtime->B1Fence = Fence;
  InterlockedExchange(&runtime->B1Completed, 0);
  InterlockedExchange(&runtime->B1Active, 1);
  result = AppleAgxBackendRuntimeSubmit(&runtime->Backend, &submission);
  AdmissionGpuvaB1RecordRetirement(Context, Owner, AdmissionB1RetireSubmit,
      result == AppleAgxBackendRuntimeResultOk ? STATUS_SUCCESS :
          STATUS_DEVICE_HARDWARE_ERROR, 0u, 0ULL, 0ULL);
  if (result != AppleAgxBackendRuntimeResultOk)
    return STATUS_DEVICE_HARDWARE_ERROR;
  AdmissionGpuvaB1RecordRetirement(Context, Owner,
      AdmissionB1RetirePollBefore, STATUS_PENDING, 0u, 0ULL, 0ULL);
  deadline = AdmissionPlatformNowMs() + 2000ULL;
  do {
    APPLE_AGX_BACKEND_U32 drained = 0u, completed = 0u;
    if (!AppleAgxPlatformProviderPoll(
            &runtime->Provider, 32u, &drained, &completed)) {
      AdmissionGpuvaB1RecordRetirement(Context, Owner,
          AdmissionB1RetirePollAfter, STATUS_DEVICE_HARDWARE_ERROR,
          0u, 0ULL, 0ULL);
      return STATUS_DEVICE_HARDWARE_ERROR;
    }
    if (InterlockedCompareExchange(&runtime->B1Completed, 0, 0) != 0 &&
        runtime->Backend.Phase == AppleAgxBackendRuntimeReady) {
      ADMISSION_B1_COMPLETION_CONTEXT completionContext = {
          Context, runtime, Owner};
      APPLE_AGX_GPUVA_B1_COMPLETION_IO completionIo = {
          &completionContext, AdmissionB1FlushOutput,
          AdmissionB1ReleaseImage};
      AdmissionGpuvaB1RecordRetirement(Context, Owner,
          AdmissionB1RetirePollAfter, STATUS_SUCCESS, 0u, 0ULL, 0ULL);
      if (AppleAgxGpuvaB1FinishCompletion(
              &completionIo, OutputCpu, 0x4000u, Fence) !=
          AppleAgxGpuvaB1CompletionOk)
        return STATUS_DEVICE_HARDWARE_ERROR;
      InterlockedExchange(&runtime->B1Active, 0);
      return STATUS_SUCCESS;
    }
    KeStallExecutionProcessor(1000u);
  } while (AdmissionPlatformNowMs() < deadline);
  AdmissionGpuvaB1RecordRetirement(Context, Owner,
      AdmissionB1RetirePollAfter, STATUS_IO_TIMEOUT, 0u, 0ULL, 0ULL);
  return STATUS_IO_TIMEOUT;
}
#endif
