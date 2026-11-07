"""rc102 (EXP2047): the first real depth-2 kick, a C->R build failure, a later hang.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2047 kicked an R->C pair (fence 6092, adopted), then a C->R kick
(6355 behind compute 6354) failed in the render successor's build: the
Wom1JobWindowKickFail record says image_failed at build step render_staged.
The KMD timing ring puts the refusal 1.4 us after StageJob, before the
template copy (normally 120 us later): one of the managed/active build's
precondition checks. The reset recovered, then a depth-1 render (0x1994)
hung about 190 fences later (no further kick). The tests pin:
  - the shared render builder names its refusal (Owner->LastBuildRefusal,
    the v2 record's build_refusal), so the next run pins the failing check;
  - a refused successor gives its staged stamp sequence back
    (AdmissionBackendImageUnstageJob) instead of leaving the image one
    sequence ahead of what the firmware ran into the reset;
  - Barrier classes that wait on N's 3D stamp (R->R, R->C) are kicked only
    with Wom1JobDepth2KickOn3d = 1 (refusal 64), and the rc95 receipt mode
    (KickClasses 0) is unrestricted;
  - the v1 record of EXP2047 and a v2 record compiled from the C struct
    decode.
"""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"
SHARED = ROOT / "drivers/apple-agx/shared"
WORKER = RENDER / "src/backend_platform_windows.c"
TOOL = ROOT / "tools/agx_gpu_frame_breakdown.py"

SHARED_SOURCES = [
    'apple_agx_render_shared_memory.c', 'apple_agx_g13_compute_work.c',
    'apple_agx_render_template.generated.c', 'apple_agx_render_template_rebase.c',
    'apple_agx_exp208_adapter.c', 'apple_agx_exp208_gdi.c',
    'apple_agx_exp208_framebuffer.c', 'apple_agx_gdi.c', 'apple_agx_relocation.c',
    'apple_agx_memory.c', 'apple_agx_exp208_dynamic.c',
]
IMAGE_SOURCES = [
    RENDER / "src/render_backend_image.c", RENDER / "src/render_completed_output.c",
    RENDER / "src/render_allocation.c",
] + [SHARED / "src" / n for n in (
    "apple_agx_render_template.generated.c", "apple_agx_render_template_rebase.c",
    "apple_agx_render_template_vm_slot.c", "apple_agx_g4_builder.c",
    "apple_agx_present_job.c", "apple_agx_cdm.c", "apple_agx_relocation.c",
    "apple_agx_exp208_gdi.c", "apple_agx_exp208_framebuffer.c", "apple_agx_gdi.c",
    "apple_agx_memory.c", "apple_agx_exp208_adapter.c", "apple_agx_exp208_dynamic.c",
    "apple_agx_g13_codec.c")]

# Wom1JobWindowKickFail of EXP2047 (v1), as the probe exported it.
EXP2047_RECORD = (
    "010000006c00000001000000d3180000d2180000831400000000000000000000"
    "0500000003000000d218000006000000d2180000000000000000000000000000"
    "0000000001000000d21800000000000020010000030000000300000001000000"
    "01000000d218000001000000")

WINDOW = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_render_shared_memory.h"
#include "render_job_window.h"
#define K_R ADMISSION_JOB_WINDOW_KIND_RENDER
#define K_C ADMISSION_JOB_WINDOW_KIND_COMPUTE
static unsigned refusal(unsigned n, unsigned m, unsigned classes) {
  ADMISSION_JOB_WINDOW_KICK_STATE s;
  memset(&s, 0, sizeof(s));
  s.Enabled = 1u; s.InFlight = 1u; s.NextPrebound = 1u;
  s.Capabilities = AdmissionJobWindowCapabilities(1u, 1u);
  s.Running.Fence = 100u; s.Running.Kind = n; s.Running.Process = 7u; s.Running.SceneId = 1u;
  s.Next.Fence = 101u; s.Next.Kind = m; s.Next.Process = 7u; s.Next.SceneId = 1u;
  s.QueuedFence = 101u; s.KickClasses = classes;
  return AdmissionJobWindowKickRefusal(&s);
}
int main(void) {
  unsigned base = AdmissionJobWindowKickClasses(0u), on3d = AdmissionJobWindowKickClasses(1u);
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER owner;
  APPLE_AGX_RENDER_MANAGER_STATE manager;
  APPLE_AGX_RENDER_MANAGER_KEY key;
  /* EXP2047 C->R (TA on compute): kicked by default */
  assert(refusal(K_C, K_R, base) == 0u && refusal(K_C, K_R, on3d) == 0u);
  /* R->C (compute on 3D): only with Wom1JobDepth2KickOn3d */
  assert(refusal(K_R, K_C, base) == 64u && refusal(K_R, K_C, on3d) == 0u);
  /* the rc95 receipt mode measures eligibility unrestricted */
  assert(refusal(K_R, K_C, 0u) == 0u);
  /* the shared render builder names its refusal */
  memset(&owner, 0, sizeof(owner)); memset(&manager, 0, sizeof(manager));
  memset(&key, 0, sizeof(key));
  owner.ManagerFence = 6353u;
  assert(!AppleAgxRenderSharedMemoryBuildManagedG4Job(&owner, &manager, &key, 6355u, 0, NULL,
      0u, NULL, 0u, 0ULL, 0, NULL, NULL, NULL, NULL) &&
      owner.LastBuildRefusal == APPLE_AGX_RENDER_BUILD_REFUSAL_MANAGER_FENCE);
  owner.ManagerFence = 0u; manager.PendingFence = 6353u;
  assert(!AppleAgxRenderSharedMemoryBuildManagedG4Job(&owner, &manager, &key, 6355u, 0, NULL,
      0u, NULL, 0u, 0ULL, 0, NULL, NULL, NULL, NULL) &&
      owner.LastBuildRefusal == APPLE_AGX_RENDER_BUILD_REFUSAL_MANAGER_PENDING);
  manager.PendingFence = 0u;
  assert(!AppleAgxRenderSharedMemoryBuildManagedG4Job(&owner, &manager, &key, 6355u, 0, NULL,
      0u, NULL, 0u, 0ULL, 0, NULL, NULL, NULL, NULL) &&
      owner.LastBuildRefusal == APPLE_AGX_RENDER_BUILD_REFUSAL_MANAGER_KEY);
  owner.LastBuildRefusal = 99u;
  assert(!AppleAgxRenderSharedMemoryBuildActiveG4Job(&owner, NULL, 0u, NULL, 0u, 0ULL, 0, NULL,
      NULL, NULL, NULL) &&
      owner.LastBuildRefusal == APPLE_AGX_RENDER_BUILD_REFUSAL_JOB_ARGUMENTS);
  puts("EXP2047_WINDOW_OK");
  return 0;
}
'''

UNSTAGE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "render_backend_image.h"
static ADMISSION_BACKEND_IMAGE image;
int main(void) {
  APPLE_AGX_EXP208_DYNAMIC_RESULT previous;
  memset(&previous, 0x5a, sizeof(previous));
  /* the state a refused successor's StageJob left: one sequence ahead */
  image.BoundFence = image.JobFence = 6355u; image.JobReady = APPLE_AGX_TRUE;
  image.Sequence = 0xba5u; memset(&image.Dynamic, 0x11, sizeof(image.Dynamic));
  image.Job.TaExpectedStamp = 0x7a0ba500u;
  /* not that job, or not one sequence ahead: refused, nothing changes */
  assert(!AdmissionBackendImageUnstageJob(&image, 6356u, 0xba4u, &previous));
  assert(!AdmissionBackendImageUnstageJob(&image, 6355u, 0xba3u, &previous));
  assert(image.Sequence == 0xba5u && image.JobReady);
  assert(AdmissionBackendImageUnstageJob(&image, 6355u, 0xba4u, &previous));
  assert(image.Sequence == 0xba4u && !image.JobReady && image.JobFence == 0u &&
         image.BoundFence == 6355u && image.Job.TaExpectedStamp == 0u &&
         memcmp(&image.Dynamic, &previous, sizeof(previous)) == 0);
  /* a second call finds nothing staged */
  assert(!AdmissionBackendImageUnstageJob(&image, 6355u, 0xba3u, &previous));
  puts("EXP2047_UNSTAGE_OK");
  return 0;
}
'''

KICK_FAIL_V2 = r'''
#include <stdio.h>
#include <string.h>
#include "apple_agx_render_shared_memory.h"
#include "render_job_window.h"
int main(void) {
  ADMISSION_JOB_WINDOW_KICK_FAILURE f;
  const unsigned char *p = (const unsigned char *)&f;
  unsigned i;
  memset(&f, 0, sizeof(f));
  f.Version = ADMISSION_JOB_WINDOW_KICK_FAILURE_VERSION; f.Bytes = sizeof(f);
  f.Fence = 6355u; f.BackendResult = 5u;
  f.Flags = ADMISSION_KICK_FAIL_G3_ABORTED | ADMISSION_KICK_FAIL_UNSTAGED;
  f.BuildStep = ADMISSION_KICK_BUILD_RENDER_BINDINGS;
  f.BuildRefusal = APPLE_AGX_RENDER_BUILD_REFUSAL_MANAGER_BACKING;
  for (i = 0; i < sizeof(f); ++i) printf("%02x", p[i]);
  putchar('\n');
  return 0;
}
'''


MANAGER = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_render_shared_memory.h"
typedef unsigned char BOOLEAN;
#define TRUE 1
#define FALSE 0
/*HELPER*/
int main(void) {
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER owner;
  APPLE_AGX_RENDER_MANAGER_STATE manager, previous;
  APPLE_AGX_RENDER_MANAGER_KEY a, b;
  memset(&owner, 0, sizeof(owner)); memset(&manager, 0, sizeof(manager));
  memset(&previous, 0, sizeof(previous));
  memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
  a.Owner = b.Owner = 41u; a.Generation = b.Generation = 3u; a.RootIpa = b.RootIpa = 0x60000000ULL;
  a.Backing[2].Va = 0x9200000ULL; a.Backing[2].Bytes = 0x800000u;
  b.Backing[2].Va = 0x9200000ULL; b.Backing[2].Bytes = 0x400000u; /* other backed blocks */
  owner.ManagerKey = a; /* the last render built (before the compute job) */
  /* EXP2051: the image still holds a's binding; BeginJob will give b's */
  assert(AdmissionJobWindowManagerNeedsIdle(&owner, TRUE, TRUE, &manager, &b,
                                            &previous, &a));
  /* the same scene's binding: no rebind, the pair may kick */
  assert(!AdmissionJobWindowManagerNeedsIdle(&owner, TRUE, TRUE, &manager, &a,
                                             &previous, &b));
  /* no private scene: BeginJob leaves the image's binding (the builder's) */
  assert(!AdmissionJobWindowManagerNeedsIdle(&owner, TRUE, FALSE, NULL, NULL,
                                             &previous, &a));
  assert(AdmissionJobWindowManagerNeedsIdle(&owner, TRUE, FALSE, NULL, NULL,
                                            &previous, &b));
  /* the head changed after the lookup: unknown, refuse */
  assert(AdmissionJobWindowManagerNeedsIdle(&owner, FALSE, TRUE, &manager, &a,
                                            &previous, &a));
  /* a pending save or an unsaved build of the manager */
  manager.PendingFence = 7u;
  assert(AdmissionJobWindowManagerNeedsIdle(&owner, TRUE, TRUE, &manager, &a,
                                            &previous, &a));
  manager.PendingFence = 0u; owner.ManagerFence = 7u;
  assert(AdmissionJobWindowManagerNeedsIdle(&owner, TRUE, TRUE, &manager, &a,
                                            &previous, &a));
  puts("EXP2051_MANAGER_OK");
  return 0;
}
'''


def load():
    spec = importlib.util.spec_from_file_location("agx_gpu_frame_breakdown", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def section(text, start):
    body = text[text.index(start):]
    return body[:body.index("\n}\n")]


def build_run(source, sources, extra=()):
    with tempfile.TemporaryDirectory(prefix="exp2047-") as tmp:
        path = Path(tmp) / "t.c"
        path.write_text(source)
        exe = Path(tmp) / "t"
        subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", *extra,
                        "-I", str(SHARED / "include"), "-I", str(RENDER / "include"),
                        str(path), *[str(s) for s in sources], "-o", str(exe)],
                       check=True, cwd=ROOT)
        return subprocess.run([str(exe)], capture_output=True, text=True, timeout=60, cwd=ROOT)


class Exp2047(unittest.TestCase):
    def test_kick_classes_and_builder_refusals(self):
        ran = build_run(WINDOW, [RENDER / "src/render_job_window.c"] +
                        [SHARED / "src" / s for s in SHARED_SOURCES])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("EXP2047_WINDOW_OK", ran.stdout)

    def test_unstage_gives_the_sequence_back(self):
        ran = build_run(UNSTAGE, IMAGE_SOURCES)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("EXP2047_UNSTAGE_OK", ran.stdout)

    def test_refused_kick_unstages_after_ending_the_g3_job(self):
        kick = section(WORKER.read_text(), "static BOOLEAN AdmissionJobWindowKickWindows(")
        save = kick.index("stageSequence = image->Sequence;")
        self.assertLess(save, kick.index("result = AppleAgxBackendRuntimeSubmitSuccessor("))
        failed = kick[kick.index("if (result != AppleAgxBackendRuntimeResultOk) {"):]
        failed = failed[:failed.index("(ULONG)result, flags);\n    return TRUE;")
                        + len("(ULONG)result, flags);")]
        abort = failed.index("AdmissionGpuvaG3AbortJob(adapter, next);")
        unstage = failed.index("AdmissionBackendImageUnstageJob(&adapter->BackendImage, next,")
        self.assertLess(abort, unstage)
        self.assertLess(unstage, failed.index("AdmissionJobWindowKickFailedWindows("))
        helper = section(WORKER.read_text(), "static VOID AdmissionJobWindowKickFailureRecordWindows(")
        self.assertIn("f->BuildRefusal = runtime->Initdata.RenderSharedMemory.LastBuildRefusal;",
                      helper)

    def test_render_bindings_are_a_separate_build_step(self):
        build = section(WORKER.read_text(), "static APPLE_AGX_BACKEND_BOOL AdmissionExternalBuildJob(")
        bindings = build.index("AppleAgxInitdataMemoryGetRenderBindings(")
        step = build.index("runtime->Depth2BuildStep = ADMISSION_KICK_BUILD_RENDER_BINDINGS;")
        managed = build.index("AppleAgxRenderSharedMemoryBuildManagedG4Job(")
        self.assertLess(bindings, step)
        self.assertLess(step, managed)

    def test_3d_wait_classes_need_their_switch(self):
        text = WORKER.read_text()
        self.assertIn('L"Wom1JobDepth2KickOn3d"', text)
        kick = section(text, "static BOOLEAN AdmissionJobWindowKickWindows(")
        self.assertIn("s.KickClasses = runtime->Depth2KickClasses;", kick)
        note = section(text, "static VOID AdmissionJobWindowNoteWindows(")
        self.assertNotIn("KickClasses", note)

    def test_exp2047_v1_and_v2_records_decode(self):
        b = load()
        out = b.kmd_kick_failure(b"Wom1JobWindowKickFail " + EXP2047_RECORD.encode() + b"\n")
        self.assertEqual((out["fence"], out["running_fence"], out["line"]), (6355, 6354, 5251))
        self.assertEqual((out["backend_result"], out["build_step"], out["running_kind"],
                          out["next_kind"], out["barrier"]),
                         ("image_failed", "render_staged", "compute", "render", "ta_on_compute"))
        self.assertNotIn("build_refusal", out)
        ran = build_run(KICK_FAIL_V2, [])
        self.assertEqual(ran.returncode, 0, ran.stderr)
        raw = bytes.fromhex(ran.stdout.strip())
        out = b.kmd_kick_failure(raw)
        self.assertEqual((out["fence"], out["build_step"], out["build_refusal"]),
                         (6355, "render_bindings", "manager_backing"))
        self.assertEqual(out["flags"], ["g3_aborted", "unstaged"])


class Exp2051(unittest.TestCase):
    """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP2051: a C->R
    successor passed refusal 61, then its build refused with manager_bind.
    BeginJob binds a private render's manager (its scene's key) after the
    window decision, so 61 compared the image's binding of the previous
    render. The decision must evaluate the builder's predicate on the
    binding BeginJob will give (AdmissionGpuvaG3PendingManager; the G4
    replay proves it equals BeginJob's), and recheck it after BeginJob."""

    def helper(self):
        text = WORKER.read_text()
        return section(text, "static BOOLEAN AdmissionJobWindowManagerNeedsIdle(") + "\n}\n"

    def test_decision_uses_the_binding_beginjob_will_give(self):
        ran = build_run(MANAGER.replace("/*HELPER*/", self.helper()),
                        [SHARED / "src" / s for s in SHARED_SOURCES])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("EXP2051_MANAGER_OK", ran.stdout)

    def test_pre_fix_predicate_lets_the_exp2051_pair_through(self):
        # RED: the pre-fix decision read the image's binding only.
        old = self.helper().replace(
            "PendingValid ? PendingManager : ImageManager",
            "((void)PendingValid, (void)PendingManager, ImageManager)").replace(
            "PendingValid ? PendingKey : ImageKey", "((void)PendingKey, ImageKey)")
        ran = build_run(MANAGER.replace("/*HELPER*/", old),
                        [SHARED / "src" / s for s in SHARED_SOURCES])
        self.assertNotEqual(ran.returncode, 0)
        self.assertIn("Assertion failed", ran.stderr)

    def test_kick_looks_up_before_and_rechecks_after_beginjob(self):
        kick = section(WORKER.read_text(), "static BOOLEAN AdmissionJobWindowKickWindows(")
        lookup = kick.index("AdmissionGpuvaG3PendingManager(adapter,")
        decide = kick.index("AdmissionJobWindowKickRefusal(&s)")
        self.assertLess(lookup, kick.index("KeAcquireSpinLock(&adapter->PagingLock"))
        self.assertLess(lookup, decide)
        self.assertIn("pendingManagerValid, pendingManager, &pendingKey", kick)
        begin = kick.index("AdmissionGpuvaG3BeginSuccessorJob(")
        recheck = kick.index("AdmissionJobWindowManagerNeedsIdle(owner, TRUE, FALSE, NULL, NULL,")
        self.assertLess(begin, recheck)
        self.assertLess(recheck, kick.index("AppleAgxBackendRuntimeSubmitSuccessor("))


if __name__ == "__main__":
    unittest.main()
