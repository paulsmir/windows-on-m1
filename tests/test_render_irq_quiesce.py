"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The ASC mailbox recv-not-empty line (AIC 578 -> vINTID 887) is level
triggered and the ASC mailbox variant has no enable or acknowledge register:
the only dismissal is popping the outbox FIFO until EMPTY.  An ISR that
returns with a listed, mapped, non-empty FIFO is re-entered at DIRQL without
end (investigation/analysis/KMD-IRQ-COMPLETION.md, "Default-on plan", H1).

The production ISR code (AdmissionDdiInterruptRoutine,
AdmissionPlatformMailboxInterrupt, AdmissionMailboxServiceFifoAtDirql and the
pure AdmissionMailboxIsrAction) is compiled on the host against a model FIFO
and run over LineListed x BaseMapped x Armed x fill x RingDepth {0, 63, 64} x
the ready/ingress gates:
- the handler never returns with the line asserted and the FIFO non-empty
  (bounded by the per-call cap, after which it claims the interrupt);
- it never touches a runtime or a base the ISR may not use (closed, or no
  runtime): it reaches both only through the adapter's MailboxIsrRuntime,
  never through PlatformRuntime;
- MOVE only while armed with ring room; a full ring drops, counts RingDrops
  and latches the overflow; not armed drops and counts Discarded.
Source-text checks pin the teardown order (stop the sender, close the ISR's
access, then power off / flush DPCs / unmap; a failed power release reopens),
the DPC's runtime lifetime, the worker's overflow fault and the default.
"""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"
SHARED = ROOT / "drivers" / "apple-agx" / "shared"
BACKEND = (RENDER / "src" / "backend_platform_windows.c").read_text()
INTERRUPT = (RENDER / "src" / "interrupt.c").read_text()
HEADER = (RENDER / "include" / "render_admission.h").read_text()


def function(source, name):
    """The whole definition of `name` (from its first line to the closing brace)."""
    match = re.search(rf"^[^\n;{{}}#]*\b{name}\s*\([^;{{]*?\)\s*\{{", source, re.S | re.M)
    assert match, name
    depth, index = 1, match.end()
    while depth:
        depth += {"{": 1, "}": -1}.get(source[index], 0)
        index += 1
    return source[match.start():index]


def body(source, name):
    text = function(source, name)
    return text[text.index("{"):]


def receipt_typedef():
    match = re.search(r"typedef struct _ADMISSION_IRQ_RX_RECEIPT \{.*?\} ADMISSION_IRQ_RX_RECEIPT;",
                      HEADER, re.S)
    assert match
    return match.group(0)


MODEL = r'''
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "render_mailbox_isr.h"
#include "j313_agx_g2.generated.h"
#include "apple_agx_asc_transport.h"
/* WDK-width types; no host unsigned-long substitution for ULONG/LONG. */
typedef unsigned int ULONG;
typedef int LONG;
typedef unsigned long long ULONGLONG, ULONG64;
typedef unsigned char UCHAR, BOOLEAN;
typedef void *PVOID, *HANDLE;
typedef void VOID;
#define TRUE 1
#define FALSE 0
#define _Use_decl_annotations_
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define KeMemoryBarrier() ((void)0)
#define ADMISSION_IRQ_RING_SLOTS 64u
''' + "%RECEIPT%" + r'''
typedef struct {
  struct { volatile UCHAR *Base; } AscTransport;
  volatile LONG MailboxIrqArmed, IrqOverflowLatch;
  struct { ULONGLONG Payload; ULONGLONG Selector; } IrqRing[64];
  volatile LONG IrqRingHead, IrqRingTail;
  ADMISSION_IRQ_RX_RECEIPT IrqRx;
  BOOLEAN IrqCompletion;
} ADMISSION_PLATFORM_RUNTIME;
typedef struct {
  volatile LONG InterruptReady, InterruptIngressEnabled;
  volatile LONG IrqIsrByMessage[4], IrqMailboxHandled, IrqMailboxMessageMask;
  volatile LONG IrqScanoutHandled, LastInterruptStatus, InterruptCount, InterruptAckCount;
  ULONG MailboxInterruptVector;
  PVOID ScanoutRuntime, PlatformRuntime;
  PVOID volatile MailboxIsrRuntime;
  BOOLEAN InterfaceValid;
  struct { HANDLE DeviceHandle; BOOLEAN (*DxgkCbQueueDpc)(HANDLE); } Interface;
} ADMISSION_CONTEXT;

static LONG InterlockedCompareExchange(volatile LONG *t, LONG x, LONG c) {
  LONG old = *t; if (old == c) *t = x; return old;
}
static LONG InterlockedExchange(volatile LONG *t, LONG v) { LONG old = *t; *t = v; return old; }
static LONG InterlockedIncrement(volatile LONG *t) { return ++*t; }
static LONG InterlockedOr(volatile LONG *t, LONG v) { LONG old = *t; *t |= v; return old; }

/* The model ASC: an outbox FIFO behind an MMIO window that may be unmapped. */
static UCHAR mmio[J313_AGX_G2_ASC_OUTBOX1_OFFSET + 8u];
static int mapped;
static unsigned fifo_count, fifo_next, unmapped_touches, control_reads;
static unsigned offset_of(const volatile void *p) {
  ptrdiff_t offset = (const volatile UCHAR *)p - mmio;
  assert(offset >= 0 && (size_t)offset < sizeof(mmio));
  return (unsigned)offset;
}
static ULONG READ_REGISTER_ULONG(volatile ULONG *p) {
  if (!mapped) { ++unmapped_touches; return 0u; }
  assert(offset_of(p) == J313_AGX_G2_ASC_OUTBOX_CTRL_OFFSET);
  ++control_reads;
  return fifo_count == 0u ? APPLE_AGX_ASC_MAILBOX_EMPTY : 0u;
}
static ULONG64 READ_REGISTER_ULONG64(volatile ULONG64 *p) {
  unsigned offset;
  if (!mapped) { ++unmapped_touches; return 0u; }
  offset = offset_of(p);
  assert(fifo_count != 0u); /* the code never pops an empty FIFO */
  if (offset == J313_AGX_G2_ASC_OUTBOX0_OFFSET)
    return 0x1000u + fifo_next;
  assert(offset == J313_AGX_G2_ASC_OUTBOX1_OFFSET); /* this read pops */
  --fifo_count;
  return 0x2000u + fifo_next++;
}
static unsigned dpc_queued;
static BOOLEAN QueueDpc(HANDLE h) { (void)h; ++dpc_queued; return TRUE; }
static unsigned scanout_calls, broker_calls;
static BOOLEAN AdmissionScanoutInterrupt(ADMISSION_CONTEXT *c) { (void)c; ++scanout_calls; return FALSE; }
static ULONG AdmissionAcknowledgeInterrupt(ADMISSION_CONTEXT *c) { (void)c; ++broker_calls; return 0u; }

''' + "%CODE%" + r'''

static unsigned min_u(unsigned a, unsigned b) { return a < b ? a : b; }

static void action_table(void) {
  int listed, base, armed, empty; unsigned d;
  static const unsigned depths[] = {0u, 63u, 64u};
  for (listed = 0; listed < 2; ++listed) for (base = 0; base < 2; ++base)
  for (armed = 0; armed < 2; ++armed) for (empty = 0; empty < 2; ++empty)
  for (d = 0u; d < 3u; ++d) {
    ADMISSION_MAILBOX_ISR_ACTION a = AdmissionMailboxIsrAction(
        listed, base, armed, empty, depths[d], ADMISSION_IRQ_RING_SLOTS);
    ADMISSION_MAILBOX_ISR_ACTION want =
        !listed || !base || empty ? AdmissionMailboxIsrNotOurs
        : !armed ? AdmissionMailboxIsrDiscard
        : depths[d] < 64u ? AdmissionMailboxIsrMove : AdmissionMailboxIsrDiscardFault;
    assert(a == want);
    /* never leave a poppable message: a listed, mapped, non-empty FIFO pops */
    if (listed && base && !empty) assert(a != AdmissionMailboxIsrNotOurs);
    if (a == AdmissionMailboxIsrMove) assert(armed && depths[d] < 64u);
  }
}

int main(void) {
  static const unsigned depths[] = {0u, 63u, 64u};
  static const unsigned fills[] = {0u, 1u, 3u, 200u, ADMISSION_MAILBOX_ISR_POP_CAP + 44u};
  unsigned states = 0u, livelock_states = 0u;
  int listed, has_runtime, open, armed, ready, ingress, completion;
  unsigned di, fi;
  action_table();
  for (listed = 0; listed < 2; ++listed) for (has_runtime = 0; has_runtime < 2; ++has_runtime)
  for (open = 0; open < 2; ++open) for (armed = 0; armed < 2; ++armed)
  for (ready = 0; ready < 2; ++ready) for (ingress = 0; ingress < 2; ++ingress)
  for (completion = 0; completion < 2; ++completion)
  for (di = 0u; di < 3u; ++di) for (fi = 0u; fi < 5u; ++fi) {
    static ADMISSION_PLATFORM_RUNTIME runtime;
    ADMISSION_CONTEXT context;
    const unsigned fill = fills[fi], depth = depths[di];
    /* the ISR may touch the base only with a runtime whose access is open */
    const int base_mapped = has_runtime && open;
    const int poppable = listed && base_mapped;
    const unsigned pops = poppable ? min_u(fill, ADMISSION_MAILBOX_ISR_POP_CAP) : 0u;
    const unsigned room = 64u - depth;
    const unsigned moved = poppable && armed ? min_u(pops, room) : 0u;
    const unsigned dropped_full = poppable && armed ? pops - moved : 0u;
    const unsigned discarded = poppable && !armed ? pops : 0u;
    BOOLEAN handled;
    unsigned i;
    memset(&runtime, 0, sizeof(runtime));
    memset(&context, 0, sizeof(context));
    /* after the close the stale base and PlatformRuntime stay behind: only
     * MailboxIsrRuntime may lead the ISR to the runtime */
    runtime.AscTransport.Base = mmio;
    runtime.MailboxIrqArmed = armed;
    runtime.IrqCompletion = (BOOLEAN)completion;
    runtime.IrqRingTail = 7; runtime.IrqRingHead = 7 + (LONG)depth;
    context.MailboxInterruptVector = listed ? 887u : 0u;
    context.PlatformRuntime = has_runtime ? &runtime : NULL;
    context.MailboxIsrRuntime = base_mapped ? &runtime : NULL;
    context.InterruptReady = ready; context.InterruptIngressEnabled = ingress;
    context.InterfaceValid = TRUE; context.Interface.DxgkCbQueueDpc = QueueDpc;
    mapped = base_mapped;
    fifo_count = fill; fifo_next = 0u;
    unmapped_touches = control_reads = dpc_queued = scanout_calls = broker_calls = 0u;

    handled = AdmissionDdiInterruptRoutine(&context, 0u);
    ++states;

    assert(unmapped_touches == 0u);
    assert(fifo_count == fill - pops);
    /* THE invariant: the line is asserted while the FIFO is non-empty; the
     * handler returns with it asserted only after a full capped pass, and
     * then it claims the interrupt (the next pass continues). */
    if (poppable && fifo_count != 0u) {
      assert(pops == ADMISSION_MAILBOX_ISR_POP_CAP && handled);
      ++livelock_states;
      handled = AdmissionDdiInterruptRoutine(&context, 0u);
      assert(handled && fifo_count == 0u && unmapped_touches == 0u);
      continue;
    }
    if (poppable) assert(fifo_count == 0u);
    /* scanout and broker do not interrupt in this model: TRUE == popped */
    assert((handled != FALSE) == (pops != 0u));
    assert(context.IrqMailboxHandled == (pops != 0u ? 1 : 0));
    /* the ready/ingress gates still gate the other sources, and only them */
    assert(scanout_calls == 0u && broker_calls == (unsigned)(ready && ingress));
    assert(context.IrqIsrByMessage[0] == (ready && ingress ? 1 : 0));
    if (!base_mapped) {
      /* a closed runtime is not even counted in */
      assert(runtime.IrqRx.IsrEmptyCalls == 0u && runtime.IrqRingHead == 7 + (LONG)depth);
      continue;
    }
    assert((unsigned)(runtime.IrqRingHead - runtime.IrqRingTail) == depth + moved);
    for (i = 0u; i < moved; ++i) {
      const unsigned slot = (unsigned)(7 + (LONG)depth + (LONG)i) % 64u;
      assert(runtime.IrqRing[slot].Payload == 0x1000u + i);
      assert(runtime.IrqRing[slot].Selector == 0x2000u + i);
    }
    assert(runtime.IrqRx.IsrMessages == moved);
    assert(runtime.IrqRx.RingDrops == dropped_full);
    assert(runtime.IrqRx.Overflow == (dropped_full != 0u ? 1u : 0u));
    assert(runtime.IrqOverflowLatch == (dropped_full != 0u ? 1 : 0));
    assert(runtime.IrqRx.Discarded == discarded);
    assert(runtime.IrqRx.IsrEmptyCalls == (listed && pops == 0u ? 1u : 0u));
    assert(dpc_queued == (moved != 0u && completion ? 1u : 0u));
    assert(runtime.IrqRx.DpcQueued == dpc_queued);
    if (moved != 0u) assert(runtime.IrqRx.RingHighWater == depth + moved);
  }
  printf("%u %u\n", states, livelock_states);
  return 0;
}
'''


def model_source(backend=BACKEND, interrupt=INTERRUPT):
    code = "\n".join((
        function(backend, "AdmissionMailboxServiceFifoAtDirql"),
        function(backend, "AdmissionPlatformMailboxInterrupt"),
        function(interrupt, "AdmissionDdiInterruptRoutine"),
    ))
    return MODEL.replace("%RECEIPT%", receipt_typedef()).replace("%CODE%", code)


def build_and_run(source, directory):
    path = pathlib.Path(directory)
    (path / "model.c").write_text(source)
    built = subprocess.run(
        ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wconversion",
         # host long is 64-bit; the WDK's LONG and `1L` are 32-bit
         "-Wno-sign-conversion", "-Wno-shorten-64-to-32",
         "-fsanitize=address,undefined",
         "-fno-sanitize-recover=undefined",
         "-I" + str(RENDER / "include"), "-I" + str(SHARED / "include"),
         str(path / "model.c"), "-o", str(path / "model")],
        capture_output=True, text=True)
    if built.returncode:
        raise AssertionError(built.stderr)
    return subprocess.run([str(path / "model")], capture_output=True, text=True)


class MailboxQuiesceModel(unittest.TestCase):
    def test_isr_never_returns_with_a_poppable_fifo_and_never_touches_a_closed_base(self):
        with tempfile.TemporaryDirectory(prefix="irq-quiesce-") as directory:
            run = build_and_run(model_source(), directory)
        self.assertEqual(run.returncode, 0, run.stderr)
        states, capped = map(int, run.stdout.split())
        # listed, runtime, open, armed, ready, ingress, switch: 2^7; 3 depths; 5 fills
        self.assertEqual(states, 128 * 3 * 5)
        self.assertGreater(capped, 0)  # the over-cap fill exercised the bounded pass

    def test_model_detects_the_h1_defects(self):
        """The model is not vacuous: each of the three H1 paths, reintroduced
        into the production text, fails it."""
        gate = ("  if (InterlockedCompareExchange(&context->InterruptReady, 0, 0) == 0 ||\n"
                "      InterlockedCompareExchange(&context->InterruptIngressEnabled, 0, 0) == 0)\n"
                "    return handled;\n")
        self.assertIn(gate, INTERRUPT)
        isr = function(INTERRUPT, "AdmissionDdiInterruptRoutine")
        early = isr.replace(gate, "").replace(
            "  /* The mailbox level line has no enable", gate.replace("handled", "FALSE") +
            "  /* The mailbox level line has no enable", 1)
        self.assertNotEqual(early, isr)
        mutants = {
            "gates before the mailbox handler": (BACKEND, INTERRUPT.replace(isr, early)),
        }
        service = function(BACKEND, "AdmissionMailboxServiceFifoAtDirql")
        for name, old, new in (
                ("ring full leaves the message",
                 "    if (action == AdmissionMailboxIsrNotOurs)\n      break;",
                 "    if (action == AdmissionMailboxIsrNotOurs ||\n"
                 "        action == AdmissionMailboxIsrDiscardFault)\n      break;"),
                ("disarmed returns before popping",
                 "  *Moved = 0u;\n", "  *Moved = 0u;\n  if (!armed) return 0u;\n"),
                ):
            self.assertIn(old, service, name)
            mutants[name] = (BACKEND.replace(service, service.replace(old, new)), INTERRUPT)
        handler = function(BACKEND, "AdmissionPlatformMailboxInterrupt")
        old = "runtime = (ADMISSION_PLATFORM_RUNTIME *)Context->MailboxIsrRuntime;"
        self.assertIn(old, handler)
        mutants["closed runtime reached through PlatformRuntime"] = (BACKEND.replace(
            handler, handler.replace(old, old.replace("MailboxIsrRuntime", "PlatformRuntime"))),
            INTERRUPT)
        for name, (backend, interrupt) in mutants.items():
            with self.subTest(name), tempfile.TemporaryDirectory(prefix="irq-quiesce-") as directory:
                run = build_and_run(model_source(backend, interrupt), directory)
                self.assertNotEqual(run.returncode, 0, name)


class MailboxQuiesceWiring(unittest.TestCase):
    def test_isr_code_is_dirql_safe(self):
        code = (function(BACKEND, "AdmissionMailboxServiceFifoAtDirql") +
                function(BACKEND, "AdmissionPlatformMailboxInterrupt") +
                function(INTERRUPT, "AdmissionDdiInterruptRoutine"))
        for forbidden in ("KeWait", "KeDelay", "Zw", "ExAllocate", "DbgPrint",
                          "KeAcquire", "KeStall", "PAGED_CODE", "Record"):
            self.assertNotIn(forbidden, code)
        self.assertIn("popped < ADMISSION_MAILBOX_ISR_POP_CAP", code)
        self.assertIn("#define ADMISSION_MAILBOX_ISR_POP_CAP 256u",
                      (RENDER / "include" / "render_mailbox_isr.h").read_text())

    def test_mailbox_handler_runs_before_the_ready_and_ingress_gates(self):
        isr = body(INTERRUPT, "AdmissionDdiInterruptRoutine")
        handler = isr.index("AdmissionPlatformMailboxInterrupt(")
        self.assertLess(isr.index("MailboxInterruptVector != 0u"), handler)
        self.assertLess(handler, isr.index("InterruptReady"))
        self.assertLess(handler, isr.index("InterruptIngressEnabled"))
        self.assertLess(isr.index("InterruptIngressEnabled"),
                        isr.index("AdmissionScanoutInterrupt("))
        # before the gates only the context / MessageNumber guard returns
        self.assertEqual(isr[:handler].count("return"), 1)

    def test_teardown_stops_the_sender_then_closes_the_isr_then_unmaps(self):
        destroy = body(BACKEND, "AdmissionPlatformDestroy")
        stop = destroy.index("AppleAgxBackendRuntimeStop(")
        close = destroy.index("AdmissionMailboxIsrClose(Runtime);")
        release = destroy.index("AppleAgxPowerRelease(")
        unmap = destroy.index("(PVOID)Runtime->SgxBase")
        flush = destroy.index("KeFlushQueuedDpcs();")
        self.assertLess(stop, close)
        self.assertLess(close, release)
        self.assertLess(release, flush)
        self.assertLess(flush, unmap)
        # a device without the line queues no mailbox DPC and is not flushed
        self.assertRegex(destroy, r"MailboxInterruptVector != 0u\)\s*KeFlushQueuedDpcs\(\);")
        # the release failed: the ASC is still powered, the close is undone
        self.assertRegex(destroy[release:flush],
                         r"\{\s*AdmissionMailboxIsrReopen\(Runtime\);\s*"
                         r"return STATUS_DEVICE_BUSY;")
        # nothing disarms before the sender is stopped: the stop conversation's
        # replies must still reach the ring, not be discarded by the ISR
        self.assertNotIn("MailboxIrqArmed", destroy[:stop])
        self.assertNotIn("AdmissionMailboxIsrClose(Runtime)", destroy[:stop])
        # the firmware cleanup powers the ASC off right after StopAsc: the
        # primitive closes the ISR's access before the release
        firmware = (SHARED / "src" / "apple_agx_firmware.c").read_text()
        self.assertLess(firmware.index("Io->StopAsc(Io->Context, deadline_ms)"),
                        firmware.index("Io->PowerOff(Io->Context, deadline_ms)"))
        power_off = body(BACKEND, "AdmissionFirmwarePowerOff")
        self.assertLess(power_off.index("AdmissionMailboxIsrClose(runtime);"),
                        power_off.index("AppleAgxPowerRelease("))
        self.assertRegex(power_off, r"if \(!AppleAgxPowerRelease\(&io\)\) \{\s*"
                                    r"AdmissionMailboxIsrReopen\(runtime\);\s*return 0u;")
        reopen = body(BACKEND, "AdmissionMailboxIsrReopen")
        self.assertIn("InterlockedExchange(&Runtime->MailboxIrqArmed, 1);", reopen)
        self.assertIn("MailboxInterruptVector == 0u", reopen)
        # StopAsc is the sender stop: RTKit shutdown, then the CPU RUN bit
        self.assertIn("AppleAgxRtkitSessionStop(&runtime->Rtkit",
                      body(BACKEND, "AdmissionFirmwareStopAsc"))
        session = (SHARED / "src" / "apple_agx_rtkit_session.c").read_text()
        self.assertIn("AppleAgxAscSetRun(Io, APPLE_AGX_ASC_FALSE)",
                      body(session, "AppleAgxRtkitSessionStop"))

    def test_close_runs_under_both_interrupt_locks_and_is_idempotent(self):
        close = body(BACKEND, "AdmissionMailboxIsrClose")
        closed = body(BACKEND, "AdmissionMailboxIsrClosed")
        self.assertIn("message < 2u", close)
        self.assertIn("DxgkCbSynchronizeExecution", close)
        self.assertIn("AdmissionMailboxIsrClosed, Runtime, message", close)
        self.assertLess(close.index("adapter->MailboxIsrRuntime != (PVOID)Runtime"),
                        close.index("message < 2u"))
        self.assertIn("InterlockedExchange(&runtime->MailboxIrqArmed, 0);", closed)
        self.assertRegex(closed, r"InterlockedCompareExchangePointer\(\s*\(PVOID volatile \*\)"
                                 r"&runtime->Adapter->MailboxIsrRuntime, NULL, runtime\);")
        # the access opens with the mapping, only when the line is listed
        start = body(BACKEND, "AdmissionPlatformRuntimeStart")
        self.assertRegex(start, r"&Context->MailboxIsrRuntime,\s*"
                                r"Context->MailboxInterruptVector != 0u \? runtime : NULL\);")
        self.assertLess(start.index("runtime->AscTransport.Base ="),
                        start.index("&Context->MailboxIsrRuntime"))
        # armed before published: an ISR that finds the runtime unarmed drops
        # the message (AdmissionMailboxIsrDiscard) instead of ringing it
        self.assertLess(start.index("InterlockedExchange(&runtime->MailboxIrqArmed,"),
                        start.index("&Context->MailboxIsrRuntime"))

    def test_isr_and_dpc_never_reach_the_runtime_through_platform_runtime(self):
        # RuntimeStop clears PlatformRuntime and frees the runtime without the
        # interrupt lock; the ISR and the mailbox DPC use the synchronized pointer
        for name in ("AdmissionPlatformMailboxInterrupt", "AdmissionPlatformMailboxDpc"):
            text = body(BACKEND, name)
            self.assertIn("Context->MailboxIsrRuntime", text)
            self.assertNotIn("PlatformRuntime", text)
        self.assertNotIn("PlatformRuntime", body(INTERRUPT, "AdmissionDdiInterruptRoutine"))
        self.assertIn("PVOID volatile MailboxIsrRuntime;", HEADER)

    def test_worker_routes_an_overflow_to_the_engine_reset_path(self):
        latch = BACKEND.index("InterlockedExchange(&runtime->IrqOverflowLatch, 0) != 0")
        drain = BACKEND.index(
            "AppleAgxRtkitSessionDrainRuntime(&runtime->Rtkit, &runtime->AscIo,", latch)
        loop = BACKEND.rindex(
            "while (runtime->Backend.Phase == AppleAgxBackendRuntimeSubmitted &&", 0, latch)
        self.assertLess(loop, latch)
        self.assertLess(drain - latch, 700)  # checked right before the RTKit drain
        self.assertNotIn("\n  }\n", BACKEND[loop:latch])  # still inside the poll loop
        self.assertRegex(BACKEND[latch:latch + 220],
                         r"\{\s*InterlockedCompareExchange\(&adapter->SchedulerFaulted,\s*"
                         r"0x40000L \| __LINE__, 0\);\s*break;\s*\}")
        service = function(BACKEND, "AdmissionMailboxServiceFifoAtDirql")
        self.assertIn("runtime->IrqRx.Overflow = 1u;", service)
        self.assertIn("InterlockedExchange(&runtime->IrqOverflowLatch, 1);", service)

    def test_default_is_on_and_inert_without_the_line(self):
        self.assertRegex(BACKEND, r"runtime->IrqCompletion =\s*"
                                  r'!AdmissionPlatformSwitchDisabled\(Context, L"Wom1IrqCompletion"\);')
        self.assertNotIn('AdmissionPlatformSwitchEnabled(Context, L"Wom1IrqCompletion")', BACKEND)
        active = body(BACKEND, "AdmissionIrqWaitActive")
        self.assertIn("runtime->IrqCompletion &&", active)
        self.assertIn("MailboxIrqArmed, 0, 0) != 0", active)
        self.assertIn("Context->MailboxInterruptVector != 0u ? 1 : 0", BACKEND)

    def test_receipts_are_exported_without_job_diagnostics_only_with_the_line(self):
        self.assertIn("ADMISSION_IRQ_RX_RECEIPT_VERSION 3u", HEADER)
        typedef = receipt_typedef()
        self.assertLess(typedef.index("WaitSkipped"), typedef.index("Discarded"))
        self.assertLess(typedef.index("Discarded"), typedef.index("Overflow"))
        export = body(BACKEND, "AdmissionJobTimingExportWindows")
        head = "if (!runtime->JobDiagnostics && runtime->Adapter->MailboxInterruptVector != 0u) {"
        block = export[export.index(head):export.index(
            "if (!runtime->JobDiagnostics && runtime->PresentTsEnabled) {")]
        self.assertIn("runtime->IrqRxExportQpc >= 2ULL * (ULONGLONG)frequency.QuadPart", block)
        self.assertIn('L"Wom1IrqRx"', block)
        self.assertIn('L"Wom1JobPoll"', block)
        self.assertNotIn("ZwFlushKey", block)
        # the worker reaches the export for the line alone
        self.assertRegex(BACKEND, r"runtime->Ov5Waiver \|\|\s*(/\*.*?\*/\s*)?"
                                  r"adapter->MailboxInterruptVector != 0u\)\s*"
                                  r"AdmissionJobTimingExportWindows\(runtime\);")
        # without the line and without any receipt switch the export returns at once
        self.assertIn("runtime->Adapter->MailboxInterruptVector == 0u &&",
                      export[:export.index("KeGetCurrentIrql() != PASSIVE_LEVEL) return;")])


if __name__ == "__main__":
    unittest.main()
