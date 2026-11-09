"""TDR stage 2b: the KMD half of the warm firmware restart.

Design: investigation/analysis/TDR-FIRMWARE-RESTART-RECOVERY.md sections 8-10.
Every test compiles the production functions out of backend_platform_windows.c
against a small model; nothing here proves that the firmware boots twice
(unknown U1, hardware stage H1).

WHAT REAL BUG OR INVARIANT WILL EACH TEST CATCH?
- warm publish: on an Active retained root the bench m1n1 answers a second
  AGX_RR_ACTIVATE with STATE and does nothing, and ACTIVATE is the only
  command that re-initialises GPUVA v5 under a new epoch
  (TDR-RESTART-U4-U8-SOURCES.md A.5). A warm boot that sent ACTIVATE could
  never publish its roots; one that ignored the STATE would hide a real
  refusal on the first boot; a PREPARE would make every process record STALE.
- stop primitives: the ISR may lose the runtime only after the ASC stop is
  proven (the stop conversation's replies come through its ring), and a
  restart must not clear RUN on a firmware that did not acknowledge.
- restart sequence: nothing firmware-visible is destroyed before the stop
  proof; the ISR is reopened before the CPU can send again; a failed boot
  never leaves a half-booted CPU running or an open ISR; the cold
  initialization that forgets the retained UAT and power is never used.
- entry and drill gate: the restart takes the runtime only when nothing is in
  flight, never clears another owner's Resetting, never faults the adapter for
  a refusal, and is unreachable without Wom1FwRestartDrillAllowed = 1.
- drill tool: a payload that differs from the KMD's is refused on the bench.
Receipt v5 itself: tests/test_render_gpu_reset_receipt_v5.py; the submitter's
wait during a restart: tests/test_g4_envelope_backpressure.py.
"""
import pathlib
import re
import subprocess
import tempfile
import unittest

from test_render_irq_quiesce import function

ROOT = pathlib.Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"
SHARED = ROOT / "drivers" / "apple-agx" / "shared"
BACKEND = (RENDER / "src" / "backend_platform_windows.c").read_text()
HEADER = (RENDER / "include" / "render_gpu_fault.h").read_text()
TOOL = (ROOT / "drivers" / "apple-agx" / "windows" / "icd" / "agx_fw_restart_drill.c").read_text()

RESTART_FUNCTIONS = (
    "AdmissionFwRestartFirmwareStatus", "AdmissionFwRestartIdleLocked",
    "AdmissionFwRestartRebuildGraph", "AdmissionFwRestartResetKmdState",
    "AdmissionPlatformRuntimeRestartOwned", "AdmissionFwRestartPublish",
    "AdmissionPlatformRuntimeRestart", "AdmissionPlatformFwRestartDrillEscape",
    "AdmissionFirmwareStopAscForRestart", "AdmissionFirmwareForceStopAsc",
    "AdmissionFirmwareRevalidateUat", "AdmissionRetainedRepublishWarm")


def run_c(source, includes=(), extra=()):
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "model.c").write_text(source)
        command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined"]
        for include in includes:
            command += ["-I", str(include)]
        command += [str(tmp / "model.c"), *map(str, extra), "-o", str(tmp / "model")]
        subprocess.run(command, check=True)
        return subprocess.run([str(tmp / "model")], check=True, capture_output=True,
                              timeout=60).stdout


TYPES = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
/* WDK-width types; no host unsigned-long substitution for ULONG/LONG. */
typedef unsigned int ULONG;
typedef int LONG, NTSTATUS;
typedef long long LONGLONG, LONG64;
typedef unsigned long long ULONGLONG;
typedef unsigned char UCHAR, BOOLEAN, KIRQL;
typedef void *PVOID, *HANDLE;
typedef void VOID;
#define TRUE 1
#define FALSE 0
#define MAXULONG 0xffffffffu
#define _Use_decl_annotations_
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_PENDING ((NTSTATUS)0x00000103)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011u)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000Du)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBu)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xC00000A3u)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184u)
#define STATUS_DEVICE_CONFIGURATION_ERROR ((NTSTATUS)0xC0000182u)
#define STATUS_DEVICE_HARDWARE_ERROR ((NTSTATUS)0xC0000483u)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009Au)
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define RtlZeroMemory(p, n) memset((p), 0, (n))
#define RtlCopyMemory memcpy
#define KeMemoryBarrier() ((void)0)
#define MAYBE_UNUSED __attribute__((unused))
MAYBE_UNUSED static LONG InterlockedCompareExchange(volatile LONG *t, LONG x, LONG c) {
  LONG old = *t; if (old == c) *t = x; return old;
}
MAYBE_UNUSED static LONG InterlockedExchange(volatile LONG *t, LONG v) { LONG old = *t; *t = v; return old; }
'''

WARM_PUBLISH = TYPES + r'''
#include "apple_agx_retained_root_client.h"
#define APPLE_AGX_RTKIT_FALSE 0
#define ADMISSION_PLATFORM_INITDATA_ADDRESS_MASK ((1ULL << 44u) - 1ULL)
enum { AppleAgxContext0Ok = 0 };
typedef struct { unsigned long long Ttbr0, Ttbr1; } APPLE_AGX_UAT_TTBR_PAIR;
typedef struct { int unused; } ADMISSION_CONTEXT;
typedef struct {
  ADMISSION_CONTEXT *Adapter;
  BOOLEAN RetainedPrepared, RetainedActive, FwRestartWarmPublished;
  ULONGLONG RetainedEpoch, RetainedRoot, RetainedRoot0, FwRestartEpochSeen;
  volatile LONG FwRestartActive;
  ULONG FwRestartRetainedMask, FwRestartRetainedFlags;
  struct { int Locked; } Handoff;
  struct { int BrokerOnly, Built; ULONGLONG InitdataVirtualAddress; } Initdata;
  struct { int Running; } Rtkit;
  struct { int unused; } Context0Lease;
} ADMISSION_PLATFORM_RUNTIME;

/* The bench broker's retained-root state machine, as far as these paths use
 * it (TDR-RESTART-U4-U8-SOURCES.md A.3, A.5): PREPARE and ACTIVATE on a root
 * that is already Prepared / Active return STATE and change nothing;
 * ACTIVATE re-initialises GPUVA v5; every response carries flags, epoch,
 * root and the system page. */
#define HV_STATE 1u
static struct {
  int prepared, active, prefix_changed;
  unsigned long long epoch, next_epoch, report_epoch;
  unsigned count[16], v5_inits;
  unsigned char request[sizeof(AGX_RR_REQUEST)];
  AGX_RR_RESPONSE response;
} hv;
static const unsigned long long hv_root = 0x8fc000000ULL, hv_ttbr0 = 0x8fc004000ULL;
static unsigned long long AdmissionRetainedRead(void *c, unsigned int offset) {
  unsigned long long word;
  (void)c;
  assert(offset >= AGX_RR_OFFSET + AGX_RR_RESPONSE_OFFSET);
  offset -= AGX_RR_OFFSET + AGX_RR_RESPONSE_OFFSET;
  assert(offset + 8u <= sizeof(hv.response));
  memcpy(&word, (const unsigned char *)&hv.response + offset, 8u);
  return word;
}
static void AdmissionRetainedWrite64(void *c, unsigned int offset, unsigned long long value) {
  (void)c;
  offset -= AGX_RR_OFFSET;
  assert(offset + 8u <= sizeof(hv.request));
  memcpy(hv.request + offset, &value, 8u);
}
static void AdmissionRetainedWrite32(void *c, unsigned int offset, unsigned int value) {
  AGX_RR_REQUEST q;
  AGX_RR_RESPONSE *r = &hv.response;
  unsigned status = HV_STATE;
  (void)c;
  assert(offset == AGX_RR_OFFSET + AGX_RR_DOORBELL && value == 1u);
  memcpy(&q, hv.request, sizeof(q));
  assert(q.Command < 16u);
  ++hv.count[q.Command];
  memset(r, 0, sizeof(*r));
  switch (q.Command) {
  case AGX_RR_PREPARE:
    if (!hv.prepared && q.Epoch == 0ULL) { hv.prepared = 1; hv.epoch = ++hv.next_epoch; status = 0u; }
    break;
  case AGX_RR_ACTIVATE:
    if (hv.prepared && !hv.active && q.Epoch == hv.epoch) { hv.active = 1; ++hv.v5_inits; status = 0u; }
    break;
  case AGX_RR_QUERY_ARENA:
    if (q.Epoch == hv.epoch && q.Va == AGX_RR_ARENA_COMMAND) {
      r->ArenaVersion = AGX_RR_ARENA_VERSION; r->ArenaClass = AGX_RR_ARENA_COMMAND;
      r->ArenaVa = AGX_RR_COMMAND_ARENA_VA; r->ArenaBytes = AGX_RR_COMMAND_ARENA_BYTES;
      status = 0u;
    }
    break;
  case AGX_RR_CLOSE:
    if (hv.prepared && q.Epoch == hv.epoch) { hv.prepared = hv.active = 0; status = 0u; }
    break;
  default:
    break;
  }
  r->Receipt = q.Sequence; r->Status = status;
  r->Epoch = hv.report_epoch ? hv.report_epoch : hv.epoch;
  r->Root = hv_root; r->Ttbr0 = hv_ttbr0;
  r->SystemVa = 0xffffffa080000000ULL; r->SystemBytes = 0x4000ULL;
  r->Flags = (hv.prepared ? AGX_RR_FLAG_PREPARED : 0u) | (hv.active ? AGX_RR_FLAG_ACTIVE : 0u) |
             (hv.prefix_changed ? 0u : AGX_RR_FLAG_PREFIX_UNCHANGED);
}
static void AdmissionRecordRetainedRoot(ADMISSION_CONTEXT *a, ULONG command, const AGX_RR_RESPONSE *r) {
  (void)a; (void)command; (void)r;
}
static unsigned graph_publishes, graph_publish_fails;
static unsigned long long graph_system_va;
static unsigned char AdmissionRetainedPublishGraph(ADMISSION_PLATFORM_RUNTIME *runtime,
    unsigned long long SystemVa, unsigned long long SystemBytes) {
  (void)runtime;
  assert(SystemBytes == 0x4000ULL);
  graph_system_va = SystemVa;
  ++graph_publishes;
  return graph_publish_fails ? 0 : 1;
}
static int AppleAgxContext0BrokerRetire(void *lease) { (void)lease; return AppleAgxContext0Ok; }
static void AdmissionRecordContext0Inventory(ADMISSION_CONTEXT *a, int stage, int result, void *lease) {
  (void)a; (void)stage; (void)result; (void)lease;
}
static unsigned long long AdmissionPlatformNowMs(void) { return 100ULL; }

%CODE%

static unsigned commands(void) {
  unsigned total = 0u, index;
  for (index = 0u; index < 16u; ++index) total += hv.count[index];
  return total;
}

int main(void) {
  ADMISSION_CONTEXT adapter;
  ADMISSION_PLATFORM_RUNTIME rt;
  APPLE_AGX_UAT_TTBR_PAIR pair, again;
  unsigned long long initdata = 0ULL;
  unsigned before;
  memset(&adapter, 0, sizeof(adapter));
  memset(&rt, 0, sizeof(rt));
  rt.Adapter = &adapter;
  rt.Initdata.BrokerOnly = rt.Initdata.Built = 1;
  rt.Initdata.InitdataVirtualAddress = 0xffffffa000104000ULL;

  /* First boot: PREPARE with the CPU stopped, then one ACTIVATE. */
  assert(AdmissionFirmwareCreateUat(&rt, 1000ULL, &pair, &initdata));
  assert(rt.RetainedPrepared && !rt.RetainedActive);
  rt.Handoff.Locked = 1; rt.Rtkit.Running = 1;
  assert(AdmissionFirmwarePublishUat(&rt, &pair));
  assert(hv.count[AGX_RR_PREPARE] == 1u && hv.count[AGX_RR_ACTIVATE] == 1u && hv.v5_inits == 1u);
  assert(rt.RetainedActive && graph_publishes == 1u && !rt.FwRestartWarmPublished);
  assert(rt.FwRestartRetainedMask == 0u); /* nothing is recorded outside a restart */

  /* The invariant itself: the broker refuses a second ACTIVATE on the Active
   * root, so a warm boot built on AdmissionRetainedActivate cannot publish. */
  assert(!AdmissionRetainedActivate(&rt));
  assert(hv.count[AGX_RR_ACTIVATE] == 2u && hv.v5_inits == 1u && graph_publishes == 1u);
  hv.count[AGX_RR_ACTIVATE] = 1u;

  /* Warm restart: CPU stopped, graph rebuilt at another address. */
  rt.Handoff.Locked = 0; rt.Rtkit.Running = 0;
  rt.FwRestartActive = 1;
  rt.Initdata.InitdataVirtualAddress = 0xffffffa000208000ULL;
  before = commands();
  assert(AdmissionFirmwareRevalidateUat(&rt, 1000ULL, &again, &initdata));
  assert(commands() == before); /* no PREPARE, no command at all */
  assert(again.Ttbr0 == pair.Ttbr0 && again.Ttbr1 == pair.Ttbr1);
  assert(initdata == (0xffffffa000208000ULL & ADMISSION_PLATFORM_INITDATA_ADDRESS_MASK));
  rt.Handoff.Locked = 1; rt.Rtkit.Running = 1;
  assert(AdmissionFirmwarePublishUat(&rt, &again));
  assert(hv.count[AGX_RR_PREPARE] == 1u && hv.count[AGX_RR_ACTIVATE] == 1u && hv.v5_inits == 1u);
  assert(hv.count[AGX_RR_QUERY_ARENA] == 1u && commands() == before + 1u);
  assert(graph_publishes == 2u && graph_system_va == 0xffffffa080000000ULL);
  assert(rt.FwRestartWarmPublished && rt.FwRestartEpochSeen == rt.RetainedEpoch);
  assert(rt.FwRestartRetainedMask == 1u << AGX_RR_QUERY_ARENA);
  assert(rt.FwRestartRetainedFlags ==
         (AGX_RR_FLAG_PREPARED | AGX_RR_FLAG_ACTIVE | AGX_RR_FLAG_PREFIX_UNCHANGED));

  /* Fail closed: a firmware that rewrote its TTBR1 prefix, a broker under
   * another epoch, a failed graph publish, a publish without the handoff lock. */
  rt.FwRestartWarmPublished = FALSE;
  hv.prefix_changed = 1;
  assert(!AdmissionFirmwarePublishUat(&rt, &again) && graph_publishes == 2u);
  hv.prefix_changed = 0; hv.report_epoch = rt.RetainedEpoch + 1ULL;
  assert(!AdmissionFirmwarePublishUat(&rt, &again) && graph_publishes == 2u);
  hv.report_epoch = 0ULL; graph_publish_fails = 1u;
  assert(!AdmissionFirmwarePublishUat(&rt, &again) && graph_publishes == 3u);
  graph_publish_fails = 0u; rt.Handoff.Locked = 0;
  assert(!AdmissionFirmwarePublishUat(&rt, &again) && graph_publishes == 3u);
  assert(!rt.FwRestartWarmPublished);
  assert(hv.count[AGX_RR_PREPARE] == 1u && hv.count[AGX_RR_ACTIVATE] == 1u);

  /* Revalidation refuses a running CPU, an unbuilt graph, a passed deadline. */
  rt.Rtkit.Running = 1;
  assert(!AdmissionFirmwareRevalidateUat(&rt, 1000ULL, &again, &initdata));
  rt.Rtkit.Running = 0; rt.Initdata.Built = 0;
  assert(!AdmissionFirmwareRevalidateUat(&rt, 1000ULL, &again, &initdata));
  rt.Initdata.Built = 1;
  assert(!AdmissionFirmwareRevalidateUat(&rt, 100ULL, &again, &initdata));

  /* Cold stop: CLOSE ends the Active state, so the next cold boot sends
   * ACTIVATE again and a revalidation without a retained root is refused. */
  rt.FwRestartActive = 0;
  assert(AdmissionFirmwareDestroyUat(&rt, 1000ULL));
  assert(!rt.RetainedPrepared && !rt.RetainedActive && hv.count[AGX_RR_CLOSE] == 1u);
  assert(!AdmissionFirmwareRevalidateUat(&rt, 1000ULL, &again, &initdata));
  assert(AdmissionFirmwareCreateUat(&rt, 1000ULL, &pair, &initdata));
  rt.Handoff.Locked = 1;
  assert(AdmissionFirmwarePublishUat(&rt, &pair));
  assert(hv.count[AGX_RR_PREPARE] == 2u && hv.count[AGX_RR_ACTIVATE] == 2u && hv.v5_inits == 2u);
  return 0;
}
'''

STOP_PRIMITIVES = TYPES + r'''
#define APPLE_AGX_RTKIT_FALSE 0
enum { AppleAgxRtkitSessionResultOk = 0, AppleAgxRtkitSessionResultTimeout = 5,
       AppleAgxRtkitSessionResultCleanupFailed = 7 };
typedef struct { int Running; } APPLE_AGX_RTKIT_SESSION;
typedef struct { int unused; } APPLE_AGX_ASC_IO;
typedef struct {
  APPLE_AGX_RTKIT_SESSION Rtkit;
  APPLE_AGX_ASC_IO AscIo;
  volatile LONG FwRestartActive;
  struct { int RenderSharedMemory; } Initdata;
} ADMISSION_PLATFORM_RUNTIME;
enum { EV_STOP = 1, EV_STOP_RESTART, EV_FORCE, EV_ISR_CLOSE, EV_FLIGHT_STOPPED };
static int events[16], event_count, session_result;
static void ev(int e) { assert(event_count < 16); events[event_count++] = e; }
static int AppleAgxRtkitSessionStop(APPLE_AGX_RTKIT_SESSION *s, const APPLE_AGX_ASC_IO *io,
                                    unsigned long long deadline) {
  (void)io; (void)deadline; ev(EV_STOP);
  if (session_result == AppleAgxRtkitSessionResultOk) s->Running = 0;
  return session_result;
}
static int AppleAgxRtkitSessionStopForRestart(APPLE_AGX_RTKIT_SESSION *s,
    const APPLE_AGX_ASC_IO *io, unsigned long long deadline) {
  (void)io; (void)deadline; ev(EV_STOP_RESTART);
  if (session_result == AppleAgxRtkitSessionResultOk) s->Running = 0;
  return session_result;
}
static int AppleAgxRtkitSessionForceStop(APPLE_AGX_RTKIT_SESSION *s, const APPLE_AGX_ASC_IO *io) {
  (void)io; ev(EV_FORCE);
  s->Running = session_result == AppleAgxRtkitSessionResultOk ? 0 : 1;
  return session_result;
}
static void AdmissionMailboxIsrClose(ADMISSION_PLATFORM_RUNTIME *r) {
  /* the sender must be stopped before the ISR loses the runtime */
  assert(!r->Rtkit.Running);
  ev(EV_ISR_CLOSE);
}
static void AppleAgxRenderFlightStopped(int *owner) { (void)owner; ev(EV_FLIGHT_STOPPED); }

%CODE%

static void expect(const int *want, int count) {
  int index;
  assert(event_count == count);
  for (index = 0; index < count; ++index) assert(events[index] == want[index]);
  event_count = 0;
}

int main(void) {
  ADMISSION_PLATFORM_RUNTIME rt;
  memset(&rt, 0, sizeof(rt));

  /* Ordinary stop (no restart): unchanged, and the ISR stays open here
   * (the power release closes it). */
  rt.Rtkit.Running = 1;
  assert(AdmissionFirmwareStopAsc(&rt, 1000ULL) == 1u);
  expect((const int[]){EV_STOP, EV_FLIGHT_STOPPED}, 2);

  /* Restart: the read-back stop, then the ISR close, then the flights. */
  rt.FwRestartActive = 1; rt.Rtkit.Running = 1;
  assert(AdmissionFirmwareStopAsc(&rt, 1000ULL) == 1u);
  expect((const int[]){EV_STOP_RESTART, EV_ISR_CLOSE, EV_FLIGHT_STOPPED}, 3);
  /* Already stopped (a failed warm start that never booted the CPU). */
  assert(AdmissionFirmwareStopAsc(&rt, 1000ULL) == 1u);
  expect((const int[]){EV_ISR_CLOSE, EV_FLIGHT_STOPPED}, 2);
  /* An ack timeout: the firmware still runs and still answers through the
   * ring. Nothing is closed, nothing is forced. */
  rt.Rtkit.Running = 1; session_result = AppleAgxRtkitSessionResultTimeout;
  assert(AdmissionFirmwareStopAsc(&rt, 1000ULL) == 0u);
  expect((const int[]){EV_STOP_RESTART}, 1);
  assert(rt.Rtkit.Running);

  /* The named forced stop: no close unless RUN is proven clear. */
  session_result = AppleAgxRtkitSessionResultCleanupFailed;
  assert(AdmissionFirmwareForceStopAsc(&rt, 1000ULL) == 0u);
  expect((const int[]){EV_FORCE}, 1);
  session_result = AppleAgxRtkitSessionResultOk;
  assert(AdmissionFirmwareForceStopAsc(&rt, 1000ULL) == 1u);
  expect((const int[]){EV_FORCE, EV_ISR_CLOSE, EV_FLIGHT_STOPPED}, 3);
  assert(AdmissionFirmwareForceStopAsc(NULL, 1000ULL) == 0u);
  return 0;
}
'''

SEQUENCE = TYPES + r'''
#include "render_gpu_fault.h"
#define APPLE_AGX_RTKIT_FALSE 0
#define APPLE_AGX_ASC_CPU_RUN (1u << 4u)
typedef unsigned int APPLE_AGX_ASC_U32;
enum { AppleAgxAscResultOk = 0, AppleAgxAscResultTransportFailed = 3 };
enum { AppleAgxBackendRuntimeResultOk = 0, AppleAgxBackendRuntimeResultFirmwareFailed = 6,
       AppleAgxBackendRuntimeResultCleanupFailed = 9 };
enum { AppleAgxFirmwareResultOk = 0, AppleAgxFirmwareResultCleanupFailed = 4 };
typedef int APPLE_AGX_BACKEND_RUNTIME_RESULT;
typedef struct { int Token; } APPLE_AGX_FIRMWARE_IO;
typedef struct { APPLE_AGX_FIRMWARE_IO Firmware; } APPLE_AGX_BACKEND_IO;
typedef struct { unsigned Phase, CompletedMask, CleanupMask, LastResult; } APPLE_AGX_FIRMWARE;
typedef struct { APPLE_AGX_FIRMWARE Firmware; APPLE_AGX_BACKEND_IO Io; } APPLE_AGX_BACKEND_RUNTIME;
typedef struct { int unused; } APPLE_AGX_ASC_IO;
typedef struct { int unused; } ADMISSION_BACKEND_IMAGE;
typedef struct { int Runtime; } APPLE_AGX_QUEUE_PROVIDER;
typedef struct { APPLE_AGX_QUEUE_PROVIDER QueueProvider; } APPLE_AGX_PLATFORM_PROVIDER;
typedef struct _RUNTIME ADMISSION_PLATFORM_RUNTIME;
typedef struct {
  ULONG MailboxInterruptVector;
  PVOID volatile MailboxIsrRuntime;
  ADMISSION_BACKEND_IMAGE BackendImage;
} ADMISSION_CONTEXT;
struct _RUNTIME {
  struct { int Running, InboxAtFailureValid, OutboxAtFailureValid, CrashlogCrashed; } Rtkit;
  ULONGLONG RetainedEpoch, FwRestartEpochSeen;
  ULONG FwRestartRetainedMask, FwRestartRetainedFlags;
  BOOLEAN FwRestartWarmPublished, BackendStarted, ProviderReady;
  APPLE_AGX_BACKEND_RUNTIME Backend;
  APPLE_AGX_ASC_IO AscIo;
  struct { unsigned Count; unsigned char Uncertain; } Context0Lease;
  APPLE_AGX_PLATFORM_PROVIDER Provider;
  int ProviderConfig;
  APPLE_AGX_BACKEND_IO PlatformIo, RuntimeIo;
  struct { unsigned State; } FirmwareProvider;
};

enum { EV_STOP_WARM = 1, EV_CPU_CONTROL, EV_INBOX, EV_OUTBOX, EV_FLUSH_DPCS, EV_PROVIDER_DESTROY,
       EV_QUEUE_LIFETIME, EV_REBUILD, EV_PROVIDER_INIT, EV_RESET_STATE, EV_ISR_REOPEN,
       EV_START_WARM, EV_RECORD_START, EV_FORCE_STOP, EV_ISR_CLOSE, EV_LIFETIME };
static int events[32], event_count;
static void ev(int e) { assert(event_count < 32); events[event_count++] = e; }
static ADMISSION_CONTEXT *ctx;
/* Model knobs. */
static struct {
  int stop_fails, run_stays, isr_stays, cpu_read_fails, lease_left, provider_destroy_fails;
  int lifetime_refused, rebuild_fails, provider_init_fails, start_fails, start_leaves_running;
  int force_fails, not_fresh, cpu_running;
} k;
static unsigned long long now;
static unsigned long long AdmissionPlatformNowMs(void) { return now += 3ULL; }
static BOOLEAN AdmissionFwRestartFirmwareStatus(const ADMISSION_PLATFORM_RUNTIME *r, unsigned int *w) {
  unsigned i; (void)r;
  for (i = 0u; i < 16u; ++i) w[i] = 0x100u + i + (k.cpu_running ? 0u : 0x1000u);
  return TRUE;
}
static void model_isr_close(ADMISSION_PLATFORM_RUNTIME *r) {
  if (ctx->MailboxIsrRuntime == (PVOID)r) ctx->MailboxIsrRuntime = NULL;
}
static int AppleAgxBackendRuntimeStopWarm(APPLE_AGX_BACKEND_RUNTIME *b) {
  ADMISSION_PLATFORM_RUNTIME *r = (ADMISSION_PLATFORM_RUNTIME *)((char *)b -
      offsetof(ADMISSION_PLATFORM_RUNTIME, Backend));
  ev(EV_STOP_WARM);
  if (k.stop_fails) return AppleAgxBackendRuntimeResultCleanupFailed;
  /* what the real StopAsc primitive does once the stop is proven */
  r->Rtkit.Running = 0; k.cpu_running = k.run_stays;
  if (!k.isr_stays) model_isr_close(r);
  r->Context0Lease.Count = k.lease_left ? 3u : 0u;
  b->Firmware.Phase = 2u; b->Firmware.CompletedMask = 3u;
  return AppleAgxBackendRuntimeResultOk;
}
static int AppleAgxAscReadCpuControl(const APPLE_AGX_ASC_IO *io, APPLE_AGX_ASC_U32 *v) {
  (void)io; ev(EV_CPU_CONTROL);
  if (k.cpu_read_fails) return AppleAgxAscResultTransportFailed;
  *v = 0x100u | (k.cpu_running ? APPLE_AGX_ASC_CPU_RUN : 0u);
  return AppleAgxAscResultOk;
}
static int AppleAgxAscReadInboxControl(const APPLE_AGX_ASC_IO *io, APPLE_AGX_ASC_U32 *v) {
  (void)io; ev(EV_INBOX); *v = 0x20001u; return AppleAgxAscResultOk;
}
static int AppleAgxAscReadOutboxControl(const APPLE_AGX_ASC_IO *io, APPLE_AGX_ASC_U32 *v) {
  /* raw read: legal only while the ISR does not own the FIFO */
  (void)io; assert(ctx->MailboxIsrRuntime == NULL);
  ev(EV_OUTBOX); *v = 0x20101u; return AppleAgxAscResultOk;
}
static void KeFlushQueuedDpcs(void) { ev(EV_FLUSH_DPCS); }
static int AppleAgxPlatformProviderDestroy(APPLE_AGX_PLATFORM_PROVIDER *p) {
  (void)p; assert(!k.cpu_running); /* firmware-derived state only goes with the CPU stopped */
  ev(EV_PROVIDER_DESTROY); return !k.provider_destroy_fails;
}
static int AdmissionBackendImageRestartQueueLifetime(ADMISSION_BACKEND_IMAGE *i) {
  (void)i; ev(EV_QUEUE_LIFETIME); return !k.lifetime_refused;
}
static BOOLEAN AdmissionFwRestartRebuildGraph(ADMISSION_CONTEXT *c, ADMISSION_PLATFORM_RUNTIME *r) {
  (void)c; (void)r; assert(!k.cpu_running);
  ev(EV_REBUILD); return !k.rebuild_fails;
}
static int AppleAgxPlatformProviderInitialize(APPLE_AGX_PLATFORM_PROVIDER *p, int *config,
                                              APPLE_AGX_BACKEND_IO *io) {
  (void)p; (void)config; assert(io->Firmware.Token == 0); /* zeroed first */
  ev(EV_PROVIDER_INIT);
  if (k.provider_init_fails) return 0;
  io->Firmware.Token = 77;
  return 1;
}
static void AdmissionFwRestartResetKmdState(ADMISSION_PLATFORM_RUNTIME *r) {
  /* the ring is reset while the ISR cannot produce into it */
  assert(ctx->MailboxIsrRuntime == NULL); (void)r; ev(EV_RESET_STATE);
}
static void AdmissionMailboxIsrReopen(ADMISSION_PLATFORM_RUNTIME *r) {
  assert(!k.cpu_running); /* before the CPU can send again */
  ev(EV_ISR_REOPEN);
  if (ctx->MailboxInterruptVector != 0u) ctx->MailboxIsrRuntime = r;
}
static void AdmissionMailboxIsrClose(ADMISSION_PLATFORM_RUNTIME *r) {
  assert(!r->Rtkit.Running); ev(EV_ISR_CLOSE); model_isr_close(r);
}
static int AppleAgxBackendRuntimeStartWarm(APPLE_AGX_BACKEND_RUNTIME *b, const APPLE_AGX_BACKEND_IO *io) {
  ADMISSION_PLATFORM_RUNTIME *r = (ADMISSION_PLATFORM_RUNTIME *)((char *)b -
      offsetof(ADMISSION_PLATFORM_RUNTIME, Backend));
  ev(EV_START_WARM);
  assert(io->Firmware.Token == 77); /* the re-initialized provider's firmware IO */
  if (ctx->MailboxInterruptVector != 0u) assert(ctx->MailboxIsrRuntime == (PVOID)r);
  b->Io = *io;
  if (k.start_fails) {
    r->Rtkit.Running = k.start_leaves_running; k.cpu_running = k.start_leaves_running;
    if (!k.start_leaves_running) model_isr_close(r); /* the warm stop inside the failed start */
    b->Firmware.Phase = k.start_leaves_running ? 9u : 2u;
    return AppleAgxBackendRuntimeResultFirmwareFailed;
  }
  r->Rtkit.Running = 1; k.cpu_running = 1;
  r->FwRestartWarmPublished = TRUE; r->FwRestartRetainedMask = 1u << 8;
  r->FwRestartRetainedFlags = 7u; r->FwRestartEpochSeen = r->RetainedEpoch;
  b->Firmware.Phase = 8u; b->Firmware.CompletedMask = 0xffu;
  return AppleAgxBackendRuntimeResultOk;
}
static void AdmissionRecordBackendStartResult(ADMISSION_CONTEXT *c, int result) {
  (void)c; (void)result; ev(EV_RECORD_START);
}
static int AppleAgxFirmwareForceStopWarm(APPLE_AGX_FIRMWARE *f, const APPLE_AGX_FIRMWARE_IO *io) {
  (void)f; assert(io->Token == 77);
  ev(EV_FORCE_STOP);
  if (k.force_fails) return AppleAgxFirmwareResultCleanupFailed;
  /* the real ForceStopAsc primitive: RUN off, read back, ISR closed */
  ((ADMISSION_PLATFORM_RUNTIME *)ctx->MailboxIsrRuntime)->Rtkit.Running = 0;
  k.cpu_running = 0; ctx->MailboxIsrRuntime = NULL;
  return AppleAgxFirmwareResultOk;
}
static int AppleAgxG13QueueRuntimeLifetimeIsFresh(const int *runtime) {
  (void)runtime; ev(EV_LIFETIME); return !k.not_fresh;
}

%CODE%

static ADMISSION_CONTEXT context;
static ADMISSION_PLATFORM_RUNTIME rt;
static ADMISSION_GPU_FW_RESTART_RECEIPT receipt;

static NTSTATUS attempt(int listed) {
  unsigned restarts = receipt.Restarts, succeeded = receipt.Succeeded, refused = receipt.Refused;
  NTSTATUS status;
  memset(&context, 0, sizeof(context));
  memset(&rt, 0, sizeof(rt));
  ctx = &context;
  context.MailboxInterruptVector = listed ? 887u : 0u;
  context.MailboxIsrRuntime = listed ? &rt : NULL;
  rt.Rtkit.Running = 1; k.cpu_running = 1;
  rt.RetainedEpoch = 5ULL;
  rt.BackendStarted = rt.ProviderReady = TRUE;
  rt.PlatformIo.Firmware.Token = 11; rt.RuntimeIo.Firmware.Token = 11;
  rt.Backend.Io.Firmware.Token = 11;
  event_count = 0;
  status = AdmissionPlatformRuntimeRestartOwned(&context, &rt, &receipt);
  assert(receipt.Restarts == restarts + 1u && receipt.Refused == refused);
  assert(receipt.Succeeded == succeeded + (NT_SUCCESS(status) ? 1u : 0u));
  assert(receipt.Status == (unsigned)status);
  return status;
}

static int seen(int e) {
  int index;
  for (index = 0; index < event_count; ++index) if (events[index] == e) return 1;
  return 0;
}
static int last(void) { return event_count ? events[event_count - 1] : 0; }

int main(void) {
  static const int happy[] = {EV_STOP_WARM, EV_CPU_CONTROL, EV_INBOX, EV_OUTBOX, EV_FLUSH_DPCS,
      EV_PROVIDER_DESTROY, EV_QUEUE_LIFETIME, EV_REBUILD, EV_PROVIDER_INIT, EV_RESET_STATE,
      EV_ISR_REOPEN, EV_START_WARM, EV_RECORD_START, EV_LIFETIME};
  int listed, index;
  memset(&receipt, 0, sizeof(receipt));
  receipt.Refused = 2u; receipt.RefusedReason = ADMISSION_FW_RESTART_REFUSED_BUSY;

  /* The whole sequence, with and without the ACPI mailbox line. */
  for (listed = 1; listed >= 0; --listed) {
    memset(&k, 0, sizeof(k));
    assert(attempt(listed) == STATUS_SUCCESS);
    assert(event_count == (int)(sizeof(happy) / sizeof(happy[0])) - (listed ? 0 : 1));
    for (index = 0; index < event_count; ++index)
      assert(events[index] == happy[index + (!listed && index >= 4 ? 1 : 0)]);
    assert(rt.BackendStarted && rt.ProviderReady && rt.RuntimeIo.Firmware.Token == 77);
    assert(receipt.Stage == AdmissionFwRestartStageComplete);
    assert(receipt.StopKind == ADMISSION_FW_RESTART_STOP_ORDERLY);
    assert(receipt.Flags == ((listed ? ADMISSION_FW_RESTART_FLAG_LINE_LISTED |
                                       ADMISSION_FW_RESTART_FLAG_ISR_REOPENED : 0u) |
        ADMISSION_FW_RESTART_FLAG_CPU_CONTROL_READ | ADMISSION_FW_RESTART_FLAG_ISR_CLOSED |
        ADMISSION_FW_RESTART_FLAG_WARM_PUBLISH | ADMISSION_FW_RESTART_FLAG_LIFETIME_FRESH |
        ADMISSION_FW_RESTART_FLAG_STATUS_BEFORE | ADMISSION_FW_RESTART_FLAG_STATUS_AFTER |
        ADMISSION_FW_RESTART_FLAG_INBOX_READ | ADMISSION_FW_RESTART_FLAG_OUTBOX_READ));
    assert(receipt.CpuControlAfterStop == 0x100u && receipt.OutboxControlAfterStop == 0x20101u);
    assert(receipt.EpochBefore == 5ULL && receipt.EpochAfter == 5ULL);
    assert(receipt.RetainedCommandMask == 1u << 8 && receipt.RetainedFlags == 7u);
    assert(receipt.FirmwareStatusBefore[0] == 0x100u && receipt.FirmwareStatusAfter[15] == 0x10fu);
    assert(receipt.FirmwarePhase == 8u && receipt.FirmwareCompletedMask == 0xffu);
    assert(receipt.TotalMs >= receipt.StopMs + receipt.RebuildMs + receipt.BootMs);
  }
  assert(receipt.Restarts == 2u && receipt.Succeeded == 2u && receipt.Refused == 2u &&
         receipt.RefusedReason == ADMISSION_FW_RESTART_REFUSED_BUSY);

  /* H1 "not Ok": the orderly stop failed. Nothing else happens; no force. */
  memset(&k, 0, sizeof(k)); k.stop_fails = 1;
  assert(attempt(1) == STATUS_DEVICE_HARDWARE_ERROR);
  assert(event_count == 1 && receipt.Stage == AdmissionFwRestartStageStopWarm);
  assert(rt.BackendStarted && context.MailboxIsrRuntime == &rt);

  /* No stop proof, no teardown: RUN still reads 1, the read failed, the ISR
   * still has the runtime, or context-0 mappings of the old graph remain. */
  for (index = 0; index < 4; ++index) {
    memset(&k, 0, sizeof(k));
    if (index == 0) k.run_stays = 1;
    if (index == 1) k.cpu_read_fails = 1;
    if (index == 2) k.isr_stays = 1;
    if (index == 3) k.lease_left = 1;
    assert(attempt(1) == STATUS_DEVICE_HARDWARE_ERROR);
    assert(receipt.Stage == AdmissionFwRestartStageStopProof);
    assert(!seen(EV_PROVIDER_DESTROY) && !seen(EV_REBUILD) && !seen(EV_START_WARM) &&
           !seen(EV_ISR_REOPEN) && !seen(EV_OUTBOX));
    assert(rt.ProviderReady);
  }

  /* A failure between the stop and the boot never starts the CPU and never
   * reopens the ISR. */
  for (index = 0; index < 4; ++index) {
    static const unsigned stages[] = {AdmissionFwRestartStageProviderDestroy,
        AdmissionFwRestartStageQueueLifetime, AdmissionFwRestartStageGraphRebuild,
        AdmissionFwRestartStageProviderInitialize};
    memset(&k, 0, sizeof(k));
    if (index == 0) k.provider_destroy_fails = 1;
    if (index == 1) k.lifetime_refused = 1;
    if (index == 2) k.rebuild_fails = 1;
    if (index == 3) k.provider_init_fails = 1;
    assert(!NT_SUCCESS(attempt(1)));
    assert(receipt.Stage == stages[index]);
    assert(!seen(EV_ISR_REOPEN) && !seen(EV_START_WARM) && !seen(EV_FORCE_STOP));
    assert(!rt.BackendStarted && context.MailboxIsrRuntime == NULL && !k.cpu_running);
    assert(rt.ProviderReady == (index == 0 ? TRUE : FALSE));
  }

  /* Failed boot that the warm stop cleaned up: no force; the ISR is closed. */
  memset(&k, 0, sizeof(k)); k.start_fails = 1;
  assert(attempt(1) == STATUS_DEVICE_HARDWARE_ERROR);
  assert(receipt.Stage == AdmissionFwRestartStageStartWarm && !seen(EV_FORCE_STOP));
  assert(last() == EV_ISR_CLOSE && context.MailboxIsrRuntime == NULL && !rt.BackendStarted);
  assert(receipt.StopKind == ADMISSION_FW_RESTART_STOP_ORDERLY);
  assert(!(receipt.Flags & ADMISSION_FW_RESTART_FLAG_STATUS_AFTER));

  /* Failed boot that left RUN on (no HELLO): the named forced stop, then
   * the ISR is closed; never a half-booted CPU behind an open or closed ISR. */
  memset(&k, 0, sizeof(k)); k.start_fails = 1; k.start_leaves_running = 1;
  assert(attempt(1) == STATUS_DEVICE_HARDWARE_ERROR);
  assert(seen(EV_FORCE_STOP) && last() == EV_ISR_CLOSE && !k.cpu_running);
  assert(receipt.StopKind == ADMISSION_FW_RESTART_STOP_FORCED &&
         (receipt.Flags & ADMISSION_FW_RESTART_FLAG_FORCED_AFTER_BOOT));
  assert(context.MailboxIsrRuntime == NULL && !rt.BackendStarted);
  /* The forced stop failed too: the CPU may run, so the ISR keeps the
   * runtime (a level line nobody dismisses would storm). */
  memset(&k, 0, sizeof(k)); k.start_fails = 1; k.start_leaves_running = 1; k.force_fails = 1;
  assert(attempt(1) == STATUS_DEVICE_HARDWARE_ERROR);
  assert(seen(EV_FORCE_STOP) && !seen(EV_ISR_CLOSE) && context.MailboxIsrRuntime == &rt);
  assert(receipt.StopKind == ADMISSION_FW_RESTART_STOP_ORDERLY && !rt.BackendStarted);

  /* Ring indices of the old lifetime survived the rebuild: no submission. */
  memset(&k, 0, sizeof(k)); k.not_fresh = 1;
  assert(attempt(1) == STATUS_INVALID_DEVICE_STATE);
  assert(receipt.Stage == AdmissionFwRestartStageLifetimeFresh && !rt.BackendStarted);
  assert(!(receipt.Flags & ADMISSION_FW_RESTART_FLAG_LIFETIME_FRESH));
  assert(receipt.Restarts == 15u && receipt.Succeeded == 2u);
  return 0;
}
'''

ENTRY = TYPES + r'''
#include "render_gpu_fault.h"
#define PASSIVE_LEVEL 0
#define Executive 0
#define KernelMode 0
#define IO_NO_INCREMENT 0
#define ADMISSION_RESET_QUIESCE_TIMEOUT_MS 1000u
typedef struct { LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct { int Signaled; } KEVENT;
typedef struct { int Held; } KSPIN_LOCK;
enum { AdmissionRenderPacketEmpty = 0, AdmissionRenderPacketQueued = 1 };
enum { AppleAgxBackendRuntimeReady = 2, AppleAgxBackendRuntimeSubmitted = 3 };
enum { AppleAgxG13QueueProviderCreated = 1 };
typedef struct { ULONG ActiveFence, KickedFence; } APPLE_AGX_SCHEDULER;
typedef struct { int State; } ADMISSION_RENDER_PACKET;
typedef struct _RUNTIME ADMISSION_PLATFORM_RUNTIME;
typedef struct {
  PVOID PlatformRuntime;
  volatile LONG SchedulerFaulted;
  KSPIN_LOCK SchedulerLock;
  ADMISSION_RENDER_PACKET RenderPacket;
  ULONG G4PendingCount, DispatchedFence;
  APPLE_AGX_SCHEDULER Scheduler;
  ADMISSION_GPU_RESET_RECEIPT GpuReset;
  BOOLEAN FwRestartDrillAllowed;
} ADMISSION_CONTEXT;
struct _RUNTIME {
  BOOLEAN BackendStarted, ProviderReady;
  struct { void *RevalidateFirmwareUat; } FirmwareIo;
  volatile LONG Stopping, Resetting, FwRestartActive, WorkScheduled, WorkersActive;
  KEVENT WorkIdle, SlotEvent;
  struct {
    int Phase, SuccessorPending;
    struct { struct { ULONG Fence; } Submission; } PendingSubmission;
  } Backend;
  struct { struct {
    int Phase; ULONG PendingFence;
    struct { ULONG PendingFence; struct { ULONG Fence; } Successor; } Runtime;
  } QueueProvider; } Provider;
};
typedef struct { void *pPrivateDriverData; ULONG PrivateDriverDataSize; } DXGKARG_ESCAPE;

static int irql, worker_stuck, restart_calls, publishes, dispatches;
static NTSTATUS restart_result;
static int KeGetCurrentIrql(void) { return irql; }
static void KeAcquireSpinLock(KSPIN_LOCK *l, KIRQL *o) { assert(!l->Held); l->Held = 1; *o = 0; }
static void KeReleaseSpinLock(KSPIN_LOCK *l, KIRQL o) { (void)o; assert(l->Held); l->Held = 0; }
static NTSTATUS KeWaitForSingleObject(KEVENT *e, int a, int b, int c, LARGE_INTEGER *t) {
  (void)e; (void)a; (void)b; (void)c;
  assert(t != NULL && t->QuadPart < 0); /* bounded */
  return worker_stuck ? (NTSTATUS)0x102 : STATUS_SUCCESS;
}
static void KeSetEvent(KEVENT *e, int i, int w) { (void)i; (void)w; e->Signaled = 1; }
static int AdmissionRenderPacketState(const ADMISSION_RENDER_PACKET *p) { return p->State; }
static ULONG AppleAgxSchedulerActiveFence(const APPLE_AGX_SCHEDULER *s, ULONG n, ULONG e) {
  (void)n; (void)e; return s->ActiveFence;
}
static ULONG AppleAgxSchedulerKickedFence(const APPLE_AGX_SCHEDULER *s) { return s->KickedFence; }
static void AdmissionRecordGpuReset(ADMISSION_CONTEXT *c) {
  assert(c->GpuReset.Version == ADMISSION_GPU_RESET_RECEIPT_VERSION &&
         c->GpuReset.Bytes == sizeof(c->GpuReset));
  ++publishes;
}
static void AdmissionDispatchQueuedWork(ADMISSION_CONTEXT *c) {
  ADMISSION_PLATFORM_RUNTIME *r = c->PlatformRuntime;
  assert(r->Resetting == 0 && r->FwRestartActive == 0); /* else the worker is refused again */
  ++dispatches;
}
static NTSTATUS AdmissionPlatformRuntimeRestartOwned(ADMISSION_CONTEXT *c,
    ADMISSION_PLATFORM_RUNTIME *r, ADMISSION_GPU_FW_RESTART_RECEIPT *receipt) {
  assert(r->Resetting == 1 && r->FwRestartActive == 1 && !c->SchedulerLock.Held);
  assert(receipt == &c->GpuReset.Restart);
  ++restart_calls; ++receipt->Restarts;
  receipt->Stage = NT_SUCCESS(restart_result) ? AdmissionFwRestartStageComplete
                                              : AdmissionFwRestartStageStartWarm;
  receipt->Status = (unsigned)restart_result;
  if (NT_SUCCESS(restart_result)) ++receipt->Succeeded;
  return restart_result;
}

%CODE%

static ADMISSION_CONTEXT context;
static ADMISSION_PLATFORM_RUNTIME rt;
static int revalidate_marker;

static void idle(void) {
  memset(&context, 0, sizeof(context));
  memset(&rt, 0, sizeof(rt));
  context.PlatformRuntime = &rt;
  context.FwRestartDrillAllowed = TRUE;
  rt.BackendStarted = rt.ProviderReady = TRUE;
  rt.FirmwareIo.RevalidateFirmwareUat = &revalidate_marker;
  rt.Backend.Phase = AppleAgxBackendRuntimeReady;
  rt.Provider.QueueProvider.Phase = AppleAgxG13QueueProviderCreated;
  irql = PASSIVE_LEVEL; worker_stuck = 0; restart_calls = publishes = dispatches = 0;
  restart_result = STATUS_SUCCESS;
}

static NTSTATUS escape(ADMISSION_FW_RESTART_DRILL *d, ULONG bytes) {
  DXGKARG_ESCAPE e = {d, bytes};
  return AdmissionPlatformFwRestartDrillEscape(&context, &e);
}
static ADMISSION_FW_RESTART_DRILL payload(void) {
  ADMISSION_FW_RESTART_DRILL d;
  memset(&d, 0, sizeof(d));
  d.Magic = ADMISSION_FW_RESTART_DRILL_MAGIC; d.Version = ADMISSION_FW_RESTART_DRILL_VERSION;
  d.Bytes = sizeof(d); d.Operation = ADMISSION_FW_RESTART_DRILL_WARM_ORDERLY;
  return d;
}

int main(void) {
  ADMISSION_FW_RESTART_DRILL d;
  int index;

  /* Gate closed (the default: value absent): refused, nothing touched. */
  idle(); context.FwRestartDrillAllowed = FALSE; d = payload();
  assert(escape(&d, sizeof(d)) == STATUS_NOT_SUPPORTED);
  assert(restart_calls == 0 && rt.Resetting == 0 && context.SchedulerFaulted == 0);
  assert(context.GpuReset.Restart.RefusedReason == ADMISSION_FW_RESTART_REFUSED_NOT_ALLOWED);

  /* Malformed payloads never restart anything. */
  for (index = 0; index < 5; ++index) {
    idle(); d = payload();
    if (index == 0) d.Magic ^= 1u;
    if (index == 1) d.Version = 2u;
    if (index == 2) d.Bytes -= 4u;
    if (index == 3) d.Operation = 2u; /* forced (H1b) is not offered */
    assert(escape(&d, index == 4 ? sizeof(d) - 4u : sizeof(d)) == STATUS_INVALID_PARAMETER);
    assert(restart_calls == 0 && rt.Resetting == 0);
  }

  /* Idle: one restart, ownership taken and returned, queued work redispatched. */
  idle(); d = payload();
  assert(escape(&d, sizeof(d)) == STATUS_SUCCESS);
  assert(restart_calls == 1 && rt.Resetting == 0 && rt.FwRestartActive == 0);
  assert(rt.SlotEvent.Signaled && dispatches == 1 && publishes == 1);
  assert(context.SchedulerFaulted == 0);
  assert(d.Status == 0u && d.Stage == AdmissionFwRestartStageComplete && d.Restarts == 1u &&
         d.Succeeded == 1u);

  /* Anything in flight: refused as busy, without taking Resetting, without
   * faulting the adapter. */
  for (index = 0; index < 13; ++index) {
    idle(); d = payload();
    switch (index) {
    case 0: context.RenderPacket.State = AdmissionRenderPacketQueued; break;
    case 1: context.G4PendingCount = 1u; break;
    case 2: context.DispatchedFence = 9u; break;
    case 3: context.Scheduler.ActiveFence = 9u; break;
    case 4: context.Scheduler.KickedFence = 9u; break;
    case 5: rt.WorkScheduled = 1; break;
    case 6: rt.WorkersActive = 1; break;
    case 7: rt.Backend.Phase = AppleAgxBackendRuntimeSubmitted; break;
    case 8: rt.Backend.PendingSubmission.Submission.Fence = 9u; break;
    case 9: rt.Backend.SuccessorPending = 1; break;
    case 10: rt.Provider.QueueProvider.PendingFence = 9u; break;
    case 11: rt.Provider.QueueProvider.Runtime.PendingFence = 9u; break;
    default: rt.Provider.QueueProvider.Runtime.Successor.Fence = 9u; break;
    }
    assert(escape(&d, sizeof(d)) == STATUS_SUCCESS);
    assert(d.Status == (unsigned)STATUS_DEVICE_BUSY);
    assert(restart_calls == 0 && rt.Resetting == 0 && rt.FwRestartActive == 0);
    assert(context.SchedulerFaulted == 0 && dispatches == 0 && publishes == 1);
    assert(context.GpuReset.Restart.Refused == 1u && context.GpuReset.Restart.Restarts == 0u &&
           context.GpuReset.Restart.RefusedReason == ADMISSION_FW_RESTART_REFUSED_BUSY);
  }

  /* A reset owns the runtime: refused, and ITS Resetting is left alone. */
  idle(); rt.Resetting = 1;
  assert(AdmissionPlatformRuntimeRestart(&context) == STATUS_DEVICE_BUSY);
  assert(restart_calls == 0 && rt.Resetting == 1);
  /* The worker's finished path did not return in time: ownership is given back. */
  idle(); worker_stuck = 1;
  assert(AdmissionPlatformRuntimeRestart(&context) == STATUS_DEVICE_BUSY);
  assert(restart_calls == 0 && rt.Resetting == 0 &&
         context.GpuReset.Restart.RefusedReason == ADMISSION_FW_RESTART_REFUSED_WORKER);

  /* No started runtime, stopping, raised IRQL, no warm primitive, a faulted
   * scheduler: refused before any ownership. */
  for (index = 0; index < 7; ++index) {
    idle();
    if (index == 0) context.PlatformRuntime = NULL;
    if (index == 1) rt.BackendStarted = FALSE;
    if (index == 2) rt.ProviderReady = FALSE;
    if (index == 3) rt.Stopping = 1;
    if (index == 4) irql = 2;
    if (index == 5) rt.FirmwareIo.RevalidateFirmwareUat = NULL;
    if (index == 6) context.SchedulerFaulted = 0x40123;
    assert(AdmissionPlatformRuntimeRestart(&context) ==
           (index == 6 ? STATUS_DEVICE_NOT_READY : STATUS_INVALID_DEVICE_STATE));
    assert(restart_calls == 0 && rt.Resetting == 0 && publishes == (index == 4 ? 0 : 1));
    assert(context.SchedulerFaulted == (index == 6 ? 0x40123 : 0));
  }
  assert(AdmissionPlatformRuntimeRestart(NULL) == STATUS_INVALID_PARAMETER);

  /* A restart that failed after taking the runtime fails closed: the
   * scheduler is faulted, no work is dispatched, the caller still gets the
   * verdict in the payload. */
  idle(); restart_result = STATUS_DEVICE_HARDWARE_ERROR; d = payload();
  assert(escape(&d, sizeof(d)) == STATUS_SUCCESS);
  assert(restart_calls == 1 && rt.Resetting == 0 && rt.FwRestartActive == 0);
  assert(context.SchedulerFaulted != 0 && (context.SchedulerFaulted >> 16) == 4);
  assert(dispatches == 0 && publishes == 1 && rt.SlotEvent.Signaled);
  assert(d.Status == (unsigned)STATUS_DEVICE_HARDWARE_ERROR &&
         d.Stage == AdmissionFwRestartStageStartWarm && d.Succeeded == 0u);
  return 0;
}
'''

def code(*names):
    return "\n".join(function(BACKEND, name) for name in names)


class WarmPublish(unittest.TestCase):
    def test_warm_publish_sends_no_second_activate_and_no_prepare(self):
        run_c(WARM_PUBLISH.replace("%CODE%", code(
            "AdmissionRetainedExchange", "AdmissionRetainedCommand",
            "AdmissionRetainedActivate", "AdmissionRetainedRepublishWarm",
            "AdmissionFirmwareCreateUat", "AdmissionFirmwareDestroyUat",
            "AdmissionFirmwareRevalidateUat", "AdmissionFirmwarePublishUat")),
            includes=(SHARED / "include",))

    def test_the_graph_publish_is_one_function_for_both_paths(self):
        # The warm path must redo exactly "the rest of AdmissionRetainedActivate"
        # (design 9.1 U9): one body, so a step added to the first boot cannot be
        # missing from the warm one.
        graph = function(BACKEND, "AdmissionRetainedPublishGraph")
        for step in ("AdmissionRetainedQueryArena", "AppleAgxInitdataMemoryApplyCommandArena",
                     "AppleAgxRenderSharedMemoryBindRelocationObjects", "AppleAgxApplyRelocations",
                     "CrashlogGpuAddress", "AgxFwIoReadManifest", "AgxHwdataMaterialize",
                     "AppleAgxContext0BrokerMap"):
            self.assertIn(step, graph)
        for name in ("AdmissionRetainedActivate", "AdmissionRetainedRepublishWarm"):
            body = function(BACKEND, name)
            self.assertEqual(body.count("AdmissionRetainedPublishGraph("), 1, name)
            self.assertNotIn("AppleAgxContext0BrokerMap", body)
        self.assertNotIn("AGX_RR_", graph.replace("AGX_RR_ARENA_COMMAND", "").replace(
            "AGX_RR_ARENA_DESCRIPTOR", "").replace("AGX_RR_IO", ""))


class StopPrimitives(unittest.TestCase):
    def test_isr_is_closed_only_after_a_proven_stop(self):
        run_c(STOP_PRIMITIVES.replace("%CODE%", code(
            "AdmissionFirmwareStopAscForRestart", "AdmissionFirmwareStopAsc",
            "AdmissionFirmwareForceStopAsc")))

    def test_the_provider_receives_both_warm_primitives(self):
        start = function(BACKEND, "AdmissionPlatformRuntimeStart")
        provider = start.index("AppleAgxFirmwareProviderInitialize(")
        for line in ("FirmwarePrimitives.ForceStopAsc = AdmissionFirmwareForceStopAsc;",
                     "FirmwarePrimitives.RevalidateFirmwareUat ="):
            # the provider copies the primitives when it is initialized
            self.assertLess(start.index(line), provider)


class RestartSequence(unittest.TestCase):
    def test_sequence_order_stop_proof_and_failure_exits(self):
        run_c(SEQUENCE.replace("%CODE%", code("AdmissionPlatformRuntimeRestartOwned")),
              includes=(RENDER / "include",))

    def test_entry_ownership_and_drill_gate(self):
        run_c(ENTRY.replace("%CODE%", code(
            "AdmissionFwRestartIdleLocked", "AdmissionFwRestartPublish",
            "AdmissionPlatformRuntimeRestart", "AdmissionPlatformFwRestartDrillEscape")),
            includes=(RENDER / "include",))

    def test_restart_code_keeps_to_its_layer(self):
        text = "\n".join(function(BACKEND, name) for name in RESTART_FUNCTIONS)
        body = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        # The cold entry points forget the retained UAT and power (8.3 step 5);
        # power and root lifetime are not the restart's (4.4).
        for forbidden in ("AppleAgxBackendRuntimeInitialize", "AppleAgxBackendRuntimeStart(",
                          "AppleAgxBackendRuntimeStop(", "AppleAgxRtkitSessionStop(",
                          "AppleAgxPowerRelease", "AdmissionFirmwarePowerOn",
                          "AGX_RR_PREPARE", "AGX_RR_ACTIVATE", "AGX_RR_CLOSE",
                          # the ISR's state has one owner (design 9.4)
                          "MailboxIrqArmed"):
            self.assertNotIn(forbidden, body, forbidden)
        self.assertIsNone(re.search(r"MailboxIsrRuntime\s*=[^=]", body))
        self.assertIsNone(re.search(r"InterlockedExchangePointer[^;]*MailboxIsrRuntime", body))

    def test_only_the_drill_escape_calls_the_restart(self):
        calls = []
        for path in sorted((RENDER / "src").glob("*.c")):
            source = re.sub(r"/\*.*?\*/", "", path.read_text(), flags=re.S)
            for match in re.finditer(r"\bAdmissionPlatformRuntimeRestart\s*\(", source):
                line_start = source.rfind("\n", 0, match.start()) + 1
                if "NTSTATUS" in source[line_start:match.start()]:
                    continue  # the definition
                calls.append((path.name, source[:match.start()].count("\n")))
        self.assertEqual(len(calls), 1, calls)
        self.assertEqual(calls[0][0], "backend_platform_windows.c")
        self.assertEqual(function(BACKEND, "AdmissionPlatformFwRestartDrillEscape").count(
            "AdmissionPlatformRuntimeRestart(Context)"), 1)
        # The gate value is read in one place, with the "= 1 enables" reader.
        self.assertEqual(len(re.findall(r"FwRestartDrillAllowed\s*=[^=]", "".join(
            p.read_text() for p in sorted((RENDER / "src").glob("*.c"))))), 1)
        self.assertRegex(BACKEND, r"Context->FwRestartDrillAllowed =\s*"
                                  r"AdmissionPlatformSwitchEnabled\(Context, L\"Wom1FwRestartDrillAllowed\"\);")

    def test_teardown_finishes_a_warm_stopped_firmware(self):
        # Design 8.3 failure exits: after a failed restart the backend is
        # Stopped but the firmware still holds the UAT and the power.
        destroy = function(BACKEND, "AdmissionPlatformDestroy")
        self.assertIn("Runtime->Backend.Firmware.CompletedMask != 0u", destroy)
        self.assertIn("AppleAgxFirmwareRollback(&Runtime->Backend.Firmware,", destroy)
        self.assertLess(destroy.index("AppleAgxFirmwareRollback("),
                        destroy.index("AppleAgxFirmwareProviderDestroy("))


class DrillTool(unittest.TestCase):
    def test_tool_payload_matches_the_kmd_abi(self):
        for name in ("ADMISSION_FW_RESTART_DRILL_MAGIC", "ADMISSION_FW_RESTART_DRILL_VERSION",
                     "ADMISSION_FW_RESTART_DRILL_WARM_ORDERLY"):
            kmd = re.search(r"#define " + name + r" (0x[0-9a-f]+u|\d+u)", HEADER).group(1)
            self.assertIn(f"#define {name} {kmd}", TOOL)
        layout = (r"typedef struct[^{]*\{\s*unsigned int Magic, Version, Bytes, Operation;\s*"
                  r"unsigned int Status, Stage, Restarts, Succeeded;\s*\}")
        self.assertRegex(HEADER, layout)
        self.assertRegex(TOOL, layout)


if __name__ == "__main__":
    unittest.main()
