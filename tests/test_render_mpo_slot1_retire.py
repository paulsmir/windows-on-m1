"""S2 A1.2: slot 1 is taken off the panel before the source is hidden or a
new source mode is committed.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?  The broker removes DCP
slot 1 only on a one-layer commit or RELEASE (m1n1 gpu/s2-broker-v3
hv_agx_scanout_broker.c:322-328) and refuses every V2 PRESENT while slot 1
is latched (:253-254).  SetVidPnSourceVisibility(FALSE) and a mode commit
send nothing to the broker, so without the kernel's removal the overlay
plane would stay on screen across a hide or mode change and the next
primary flip would be refused.  The test runs the production functions
(AdmissionScanoutSetVisible, AdmissionScanoutMpoRetireSlot1, the commit
helper) against the real shared scanout client and a fake broker page.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_scanout_vsync_replay import function, ROOT

DRIVER = ROOT / "drivers/apple-agx/render-admission"
SHARED = ROOT / "drivers/apple-agx/shared"


def typedef(text, name):
    start = text.index(f"typedef struct _{name} {{")
    end = text.index(f"}} {name};", start) + len(name) + 3
    return text[start:end]


SHIM = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "apple_agx_fixed_panel.h"
#include "apple_agx_vsync.h"
#include "mpo_layers.h"
typedef unsigned long long ULONGLONG; typedef long long LONGLONG;
typedef unsigned ULONG; typedef unsigned UINT; typedef void *HANDLE;
typedef int32_t LONG; typedef int64_t LONG64; typedef int BOOLEAN;
typedef int NTSTATUS; typedef void VOID; typedef int KSPIN_LOCK;
typedef unsigned long long APPLE_AGX_U64;
typedef struct { long long QuadPart; } LARGE_INTEGER;
typedef struct { int Armed; } KTIMER;
typedef struct { int Queued; } KDPC;
#define TRUE 1
#define FALSE 0
#define PASSIVE_LEVEL 0
#define KernelMode 0
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_NOT_READY -1
#define STATUS_DEVICE_BUSY -3
#define STATUS_INVALID_PARAMETER -4
#define STATUS_DEVICE_HARDWARE_ERROR -6
#define NT_SUCCESS(x) ((x) >= 0)
#define _Use_decl_annotations_
#define RtlZeroMemory(p, n) memset((p), 0, (n))
static LONG InterlockedExchange(volatile LONG *p, LONG v) { LONG o = *p; *p = v; return o; }
static LONG InterlockedIncrement(volatile LONG *p) { return ++*p; }
static LONG InterlockedCompareExchange(volatile LONG *p, LONG v, LONG c) { LONG o = *p; if (o == c) *p = v; return o; }
static LONG64 InterlockedExchange64(volatile LONG64 *p, LONG64 v) { LONG64 o = *p; *p = v; return o; }
static unsigned irql, delays;
static unsigned KeGetCurrentIrql(void) { return irql; }
static NTSTATUS KeDelayExecutionThread(int m, int a, LARGE_INTEGER *i) { (void)m; (void)a; (void)i; ++delays; return 0; }
static ULONGLONG AdmissionScanoutTime100ns(void) { return 1234; }
"""

FAKE = r"""
/* Fake broker page: V3 advertised; records COMMIT_LAYERS. */
static unsigned active_layers, refuse, commits, count_written, v3_writes;
static unsigned long long receipt, request_seq, rec_offset;
static unsigned rec_flags, rec_width;
static APPLE_AGX_SCANOUT_U64 now_ms(void *c) { (void)c; return 1; }
static APPLE_AGX_SCANOUT_BOOL r32(void *c, unsigned o, unsigned *v) {
  (void)c; o -= APPLE_AGX_SCANOUT_MMIO_OFFSET;
  *v = o == APPLE_AGX_SCANOUT_REG_ACTIVE_LAYERS ? active_layers :
       o == APPLE_AGX_SCANOUT_REG_RESULT ? (commits && refuse ? refuse : 0u) : 0u;
  return 1; }
static APPLE_AGX_SCANOUT_BOOL r64(void *c, unsigned o, APPLE_AGX_SCANOUT_U64 *v) {
  (void)c; *v = o - APPLE_AGX_SCANOUT_MMIO_OFFSET ==
      APPLE_AGX_SCANOUT_REG_RECEIPT_SEQUENCE ? receipt : 0ULL; return 1; }
static APPLE_AGX_SCANOUT_BOOL w32(void *c, unsigned o, unsigned v) {
  (void)c; o -= APPLE_AGX_SCANOUT_MMIO_OFFSET;
  if (o >= APPLE_AGX_SCANOUT_REG_NEGOTIATED) ++v3_writes;
  if (o == APPLE_AGX_SCANOUT_REG_LAYER0 + APPLE_AGX_SCANOUT_LAYER_FLAGS) rec_flags = v;
  if (o == APPLE_AGX_SCANOUT_REG_LAYER0 + APPLE_AGX_SCANOUT_LAYER_WIDTH) rec_width = v;
  if (o == APPLE_AGX_SCANOUT_REG_LAYER_COUNT) count_written = v;
  if (o == APPLE_AGX_SCANOUT_REG_COMMAND && v == APPLE_AGX_SCANOUT_CMD_COMMIT_LAYERS) {
    ++commits; if (refuse) receipt = request_seq; }
  return 1; }
static APPLE_AGX_SCANOUT_BOOL w64(void *c, unsigned o, APPLE_AGX_SCANOUT_U64 v) {
  (void)c; o -= APPLE_AGX_SCANOUT_MMIO_OFFSET;
  if (o == APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE) request_seq = v;
  if (o == APPLE_AGX_SCANOUT_REG_LAYER0 + APPLE_AGX_SCANOUT_LAYER_SURFACE_OFFSET) rec_offset = v;
  return 1; }
"""

SCENARIOS = r"""
static ADMISSION_CONTEXT ctx; static ADMISSION_SCANOUT_RUNTIME run;
static ADMISSION_SCANOUT_RUNTIME *AdmissionScanoutGet(ADMISSION_CONTEXT *c) { return c && c->ScanoutRuntime ? c->ScanoutRuntime : 0; }
static void setup(int two_layers, int slot1, int stored) {
  memset(&ctx, 0, sizeof(ctx)); memset(&run, 0, sizeof(run));
  active_layers = refuse = commits = count_written = v3_writes = 0;
  rec_flags = rec_width = 0; rec_offset = 0; receipt = 0; delays = 0; irql = 0;
  ctx.ScanoutRuntime = &run; run.Adapter = &ctx;
  run.Panel.Started = run.Panel.Committed = run.Panel.Visible = 1;
  run.Panel.Scanout.Qualified = 1; run.Panel.Scanout.NextSequence = 5;
  run.Panel.Scanout.AbiVersion = 2;
  run.Panel.Scanout.Capabilities = APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES |
      APPLE_AGX_SCANOUT_CAP_TWO_LAYERS;
  run.Panel.Scanout.TwoLayers = two_layers ? 1 : 0;
  run.Panel.Scanout.Io.NowMs = now_ms; run.Panel.Scanout.Io.Read32 = r32;
  run.Panel.Scanout.Io.Read64 = r64; run.Panel.Scanout.Io.Write32 = w32;
  run.Panel.Scanout.Io.Write64 = w64;
  run.Slot1Shown = slot1 ? 1 : 0;
  if (stored) {
    ADMISSION_MPO_PLANE_INPUT *p = &ctx.MpoPlanes[0].Input;
    ctx.MpoPlanes[0].Valid = ctx.MpoPlanes[0].Enabled = 1;
    ctx.MpoPlanes[0].PrimaryAddress = 0x150000000LL;
    p->LayerIndex = 0; p->Enabled = 1; p->FormatClass = AdmissionMpoFormatBgra8;
    p->InScanoutPool = p->PlacementKnown = 1; p->PoolOffset = 0x10000ULL;
    p->SurfaceWidth = 2560; p->SurfaceHeight = 1600; p->SurfaceStride = 10240;
    p->SurfaceBytes = 10240ULL * 1600u;
    p->Source.Right = p->Destination.Right = 2560;
    p->Source.Bottom = p->Destination.Bottom = 1600;
    ctx.MpoPlanes[1].Valid = ctx.MpoPlanes[1].Enabled = 1;
  }
}
int main(void) {
  /* Slot 1 shown: the hide queues exactly the one-record removal of the
   * stored LayerIndex 0 plane (flags 0), clears the plane state, and the
   * pending flip is a removal (its latch clears Slot1Shown). */
  setup(1, 1, 1);
  assert(AdmissionScanoutSetVisible(&ctx, FALSE) == STATUS_SUCCESS);
  assert(commits == 1 && count_written == 1 && rec_flags == 0u &&
         rec_width == 2560u && rec_offset == 0x10000ULL);
  assert(!run.Panel.Visible && run.PendingValid && run.PresentGate == 1);
  assert(run.PendingMpoKind == ADMISSION_SCANOUT_MPO_KIND_REMOVAL &&
         run.PendingMpoCount == 0 && run.Slot1Shown == 1);
  assert(!ctx.MpoPlanes[0].Valid && !ctx.MpoPlanes[1].Valid);
  assert(ctx.Mpo.Slot1Retires == 1 &&
         ctx.Mpo.LastSlot1RetireStage == ADMISSION_MPO_RETIRE_STAGE_HIDE);
  /* The broker reports slot 1 (ACTIVE_LAYERS bit 1) though the driver
   * flag is clear: still removed. */
  setup(1, 0, 1); active_layers = 3;
  assert(AdmissionScanoutSetVisible(&ctx, FALSE) == STATUS_SUCCESS);
  assert(commits == 1 && count_written == 1);
  /* Slot 1 off: no commit, only the ACTIVE_LAYERS read; state cleared. */
  setup(1, 0, 1); active_layers = 1;
  assert(AdmissionScanoutSetVisible(&ctx, FALSE) == STATUS_SUCCESS);
  assert(commits == 0 && v3_writes == 0 && !ctx.MpoPlanes[0].Valid &&
         ctx.Mpo.Slot1Retires == 0 && !run.Panel.Visible);
  /* Not negotiated (switch off): nothing touches the broker. */
  setup(0, 1, 1);
  assert(AdmissionScanoutSetVisible(&ctx, FALSE) == STATUS_SUCCESS);
  assert(commits == 0 && v3_writes == 0 && ctx.MpoPlanes[0].Valid);
  /* A flip outstanding past the bounded wait: counted, hide not failed. */
  setup(1, 1, 1); run.PresentGate = 1;
  assert(AdmissionScanoutSetVisible(&ctx, FALSE) == STATUS_SUCCESS);
  assert(commits == 0 && delays == ADMISSION_SCANOUT_RETIRE_WAIT_MS &&
         ctx.Mpo.Slot1RetireFailures == 1 && !run.Panel.Visible);
  /* The broker refuses the removal: recorded, hide not failed. */
  setup(1, 1, 1); refuse = APPLE_AGX_SCANOUT_BROKER_LAYER_OUTSIDE_POOL;
  assert(AdmissionScanoutSetVisible(&ctx, FALSE) == STATUS_SUCCESS);
  assert(commits == 1 && ctx.Mpo.Slot1RetireRefusals == 1 &&
         ctx.Mpo.LastBrokerRefusal == (LONG)APPLE_AGX_SCANOUT_BROKER_LAYER_OUTSIDE_POOL &&
         ctx.Mpo.BrokerRefusals == 0 && ctx.Mpo.Slot1RetireFailures == 1 &&
         run.PresentGate == 0 && !run.PendingValid);
  /* No stored LayerIndex 0 plane: nothing to show in slot 0; counted. */
  setup(1, 1, 0);
  assert(AdmissionScanoutSetVisible(&ctx, FALSE) == STATUS_SUCCESS);
  assert(commits == 0 && ctx.Mpo.Slot1RetireFailures == 1);
  /* Showing the source never removes anything. */
  setup(1, 1, 1);
  assert(AdmissionScanoutSetVisible(&ctx, TRUE) == STATUS_SUCCESS);
  assert(commits == 0 && ctx.MpoPlanes[0].Valid);
  return 0;
}
"""


class RenderMpoSlot1RetireTests(unittest.TestCase):
    def test_hide_removes_slot1_through_the_real_helpers(self):
        source = (DRIVER / "src/scanout_windows.c").read_text()
        header = (DRIVER / "include/render_admission.h").read_text()
        runtime = source[source.index("typedef struct _ADMISSION_SCANOUT_RUNTIME"):
                         source.index("} ADMISSION_SCANOUT_RUNTIME;") +
                         len("} ADMISSION_SCANOUT_RUNTIME;")]
        receipt = typedef(header, "ADMISSION_MPO_RECEIPT")
        stages = "\n".join(re.findall(
            r"^#define ADMISSION_MPO_RETIRE_STAGE_\w+ \w+$", header, re.M))
        wait = re.search(r"^#define ADMISSION_SCANOUT_RETIRE_WAIT_MS \w+$",
                         source, re.M).group(0)
        code = SHIM
        reasons = re.search(r"^#define ADMISSION_MPO_RECEIPT_REASONS \w+$",
                            header, re.M).group(0)
        clip_defines = "\n".join(re.findall(
            r"^#define ADMISSION_MPO_CLIP_\w+ \w+$", header, re.M))
        code += reasons + "\n" + clip_defines + "\n"
        code += typedef(header, "ADMISSION_MPO_CLIP_ROW") + "\n"
        code += typedef(header, "ADMISSION_MPO_CLIP_SAMPLE") + "\n"
        code += receipt + "\n"
        code += stages + "\n"
        code += typedef(header, "ADMISSION_MPO_COMPLETION") + "\n"
        code += typedef(header, "ADMISSION_MPO_PLANE_STATE") + "\n"
        code += ("typedef struct _ADMISSION_CONTEXT ADMISSION_CONTEXT;\n" +
                 runtime + "\n" +
                 "struct _ADMISSION_CONTEXT { void *ScanoutRuntime; "
                 "ADMISSION_MPO_RECEIPT Mpo; ADMISSION_MPO_PLANE_STATE "
                 "MpoPlanes[2]; };\n")
        code += FAKE
        code += "static ADMISSION_SCANOUT_RUNTIME *AdmissionScanoutGet(ADMISSION_CONTEXT *c);\n"
        code += wait + "\n"
        for signature in (
            "static VOID AdmissionScanoutRetryRecordV3(",  # RQ-1: QueueHeld records the commit
            "static VOID AdmissionScanoutSetNextMpo(",
            "static VOID AdmissionScanoutTakeNextMpo(",
            "_Use_decl_annotations_ BOOLEAN AdmissionScanoutMpoSlot1MayShow(",
            "static NTSTATUS AdmissionScanoutMpoQueueHeld(",
            "static VOID AdmissionScanoutMpoRetireSlot1(ADMISSION_CONTEXT *Context,\n                                           ULONG Stage) {",
            "_Use_decl_annotations_ NTSTATUS AdmissionScanoutSetVisible(",
        ):
            code += function(source, signature) + "\n"
        code += SCENARIOS
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "retire.c"
            path.write_text(code)
            binary = Path(tmp) / "retire"
            subprocess.run([
                "clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-function", "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"), "-I", str(DRIVER / "include"),
                str(path), str(SHARED / "src/apple_agx_scanout.c"),
                str(SHARED / "src/apple_agx_fixed_panel.c"),
                str(DRIVER / "src/mpo_contract.c"),
                str(DRIVER / "src/mpo_layers.c"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)

    def test_mode_commit_and_stop_clear_slot1(self):
        source = (DRIVER / "src/scanout_windows.c").read_text()
        commit = function(source,
                          "_Use_decl_annotations_ NTSTATUS AdmissionScanoutCommit(")
        self.assertLess(commit.index("AdmissionScanoutMpoRetireSlot1("),
                        commit.index("AppleAgxFixedPanelCommitMode("))
        self.assertIn("ADMISSION_MPO_RETIRE_STAGE_COMMIT", commit)
        stop = function(source,
                        "_Use_decl_annotations_ NTSTATUS AdmissionScanoutStop(")
        # RELEASE removes slot 1 in the broker; the kernel clears its state.
        self.assertLess(stop.index("AppleAgxFixedPanelStop("),
                        stop.index("RtlZeroMemory(Context->MpoPlanes"))


if __name__ == "__main__":
    unittest.main()
