"""rc83: job-timing receipt v3, the render build and completion split.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2012 (Wom1JobTiming971 v2) put 266 us of every render job between
BackendBefore and the 3D kick and 77 us between completion and job end, with
no stamp inside either, and its 64 slots were the idle desktop's jobs after
GravityMark had ended (54 dwm, 1 explorer, 0 GravityMark; the ring spanned
1.75 s). The tests check:
- the v3 slot layout the decoder reads (v2 fields, then 8 Sub stamps), the
  first stamp of a Sub wins and an unknown Sub is refused;
- AdmissionJobTimingDense: a full ring whose 64 submissions fall within
  ADMISSION_JOB_TIMING_HOT_SPAN_MS is dense (GravityMark HD/FHD: 64 jobs in
  180-300 ms), the EXP2012 desktop ring (1.75 s) and a partly filled ring
  are not, so Wom1JobTiming971Hot keeps the application's steady state;
- the export writes Wom1JobTiming971Hot only for a dense snapshot;
- the build stamps each sub-interval in order (staged, shared builder's
  copied/relocated/managed, prelude, flushed) for the fence being built and
  stops stamping after it, the completion stamps the manager save and the
  arena release, and the release stamp is taken outside SchedulerLock;
- the shared render builder reports its points in order and records whether
  a managed build restored saved buffer-manager state;
- the decoder's sub_us medians and build_flags counts.
"""
from pathlib import Path
import importlib.util
import os
import re
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "drivers/apple-agx/render-admission/include"
SOURCE = ROOT / "drivers/apple-agx/render-admission/src/render_job_timing.c"
BACKEND = ROOT / "drivers/apple-agx/render-admission/src/backend_platform_windows.c"
SHARED = ROOT / "drivers/apple-agx/shared"


def function_body(source, name):
    """The last definition of a C function named name (any return type)."""
    matches = list(re.finditer(r"(?m)^[A-Za-z_][A-Za-z0-9_ \t*]*\b" + name + r"\s*\([^;{]*?\)\s*\{",
                               source, re.S))
    if not matches:
        raise AssertionError(f"missing production function {name}")
    match = matches[-1]
    depth, pos = 1, match.end()
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[match.start():pos]


def load():
    spec = importlib.util.spec_from_file_location('breakdown', ROOT / 'tools/agx_gpu_frame_breakdown.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def build_and_run(test, program, sources, include):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "model.c"
        exe = Path(directory) / "model"
        path.write_text(program)
        built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                                "-Werror", "-fsanitize=address,undefined", "-I", str(include),
                                str(path), *[str(s) for s in sources], "-o", str(exe)],
                               capture_output=True, text=True)
        test.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(exe)], capture_output=True, text=True)
        test.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


TIMING = r'''
#include <assert.h>
#include <stddef.h>
#include "render_job_timing.h"
int main(void) {
  static ADMISSION_JOB_TIMING_STATE s;
  const ADMISSION_JOB_TIMING_SLOT *slot;
  unsigned i;
  /* decoder '<IIIIQQ12Q4QIIIIII8Q' (v3), rc95 v4 '...13Q': the v3 stamps
   * keep their indices, five are appended */
  assert(ADMISSION_JOB_TIMING_VERSION == 4u && ADMISSION_JOB_SUB_COUNT == 13);
  assert(sizeof(ADMISSION_JOB_TIMING_SLOT) == 288u);
  assert(AdmissionJobSubSuccessorBound == 8 && AdmissionJobSubTaDone == 9 &&
         AdmissionJobSubTaStamp == 10 && AdmissionJobSubPreludeDone == 11 &&
         AdmissionJobSubPreludeStamp == 12);
  assert(offsetof(ADMISSION_JOB_TIMING_SLOT, Flags) == 176u);
  assert(offsetof(ADMISSION_JOB_TIMING_SLOT, Sub) == 184u);
  assert(AdmissionJobSubStaged == 0 && AdmissionJobSubCopied == 1 &&
         AdmissionJobSubRelocated == 2 && AdmissionJobSubManaged == 3 &&
         AdmissionJobSubPrelude == 4 && AdmissionJobSubFlushed == 5 &&
         AdmissionJobSubSaved == 6 && AdmissionJobSubReleased == 7);
  AdmissionJobTimingInitialize(&s, 24000000ull);
  assert(AdmissionJobTimingBegin(&s, 5u, 7u, 1ull, 280u, 100ull));
  assert(AdmissionJobTimingSub(&s, 5u, AdmissionJobSubCopied, 150ull));
  assert(!AdmissionJobTimingSub(&s, 5u, AdmissionJobSubCopied, 160ull)); /* first wins */
  assert(!AdmissionJobTimingSub(&s, 5u, ADMISSION_JOB_SUB_COUNT, 160ull));
  assert(!AdmissionJobTimingSub(&s, 5u, (ADMISSION_JOB_SUB)-1, 160ull));
  assert(!AdmissionJobTimingSub(&s, 5u, AdmissionJobSubSaved, 0ull));
  assert(!AdmissionJobTimingSub(&s, 0u, AdmissionJobSubSaved, 170ull)); /* no fence */
  assert(!AdmissionJobTimingSub(&s, 9u, AdmissionJobSubSaved, 170ull));
  assert(AdmissionJobTimingFlag(&s, 5u, ADMISSION_JOB_FLAG_PRELUDE | ADMISSION_JOB_FLAG_SLOT1));
  slot = AdmissionJobTimingFind(&s, 5u);
  assert(slot->Sub[AdmissionJobSubCopied] == 150ull && !slot->Sub[AdmissionJobSubSaved]);
  assert(slot->Flags == (ADMISSION_JOB_FLAG_PRELUDE | ADMISSION_JOB_FLAG_SLOT1));
  /* bits do not collide with BeginJob's */
  assert(((ADMISSION_JOB_FLAG_MAPPING_UNCHANGED | ADMISSION_JOB_FLAG_PRIVATE_SCENE |
           ADMISSION_JOB_FLAG_BEGIN_JOB) &
          (ADMISSION_JOB_FLAG_INIT_BM | ADMISSION_JOB_FLAG_INIT_QUEUES |
           ADMISSION_JOB_FLAG_PRELUDE | ADMISSION_JOB_FLAG_SLOT1 |
           ADMISSION_JOB_FLAG_MANAGER_RESTORED)) == 0u);
  /* a partly filled ring is never dense */
  assert(!AdmissionJobTimingDense(&s));
  /* a reused slot starts with no stamps */
  for (i = 1; i < ADMISSION_JOB_TIMING_CAPACITY; ++i)
    assert(AdmissionJobTimingBegin(&s, 100u + i, 7u, 1ull, 280u, 1000ull + i));
  assert(AdmissionJobTimingBegin(&s, 500u, 7u, 1ull, 280u, 2000ull));
  slot = AdmissionJobTimingFind(&s, 500u);
  assert(slot && !slot->Sub[AdmissionJobSubCopied] && !slot->Flags);
  /* GravityMark HD: 9 jobs per 24.75 ms frame -> 64 jobs in ~176 ms */
  AdmissionJobTimingInitialize(&s, 24000000ull);
  for (i = 0; i < 100; ++i)
    assert(AdmissionJobTimingBegin(&s, 1u + i, 9452u, 1ull, 280u,
                                   1000000ull + i * (24000000ull * 2750ull / 1000000ull)));
  assert(AdmissionJobTimingDense(&s));
  /* exactly the limit is dense, one tick more is not */
  AdmissionJobTimingInitialize(&s, 24000000ull);
  for (i = 0; i < 64; ++i)
    assert(AdmissionJobTimingBegin(&s, 1u + i, 1u, 1ull, 280u,
        1000ull + (i == 63 ? 24000ull * ADMISSION_JOB_TIMING_HOT_SPAN_MS : i)));
  assert(AdmissionJobTimingDense(&s));
  AdmissionJobTimingInitialize(&s, 24000000ull);
  for (i = 0; i < 64; ++i)
    assert(AdmissionJobTimingBegin(&s, 1u + i, 1u, 1ull, 280u,
        1000ull + (i == 63 ? 24000ull * ADMISSION_JOB_TIMING_HOT_SPAN_MS + 1ull : i)));
  assert(!AdmissionJobTimingDense(&s));
  /* EXP2012's desktop ring: 64 dwm jobs over 1.75 s */
  AdmissionJobTimingInitialize(&s, 24000000ull);
  for (i = 0; i < 64; ++i)
    assert(AdmissionJobTimingBegin(&s, 1u + i, 1228u, 1ull, 280u,
                                   5000ull + i * (24000000ull * 1752ull / 1000ull / 63ull)));
  assert(!AdmissionJobTimingDense(&s));
  assert(!AdmissionJobTimingDense(NULL));
  return 0;
}
'''


def shared_marks_fixture():
    return r'''
  {
    APPLE_AGX_RENDER_MANAGER_STATE m={0};
    APPLE_AGX_RENDER_MANAGER_KEY k={0};
    k.Owner=31;k.Generation=2;k.RootIpa=0x50000000ULL;
    k.Backing[0]=(APPLE_AGX_G4_PROCESS_RANGE){0x8100000ULL,0x10000u,0u};
    k.Backing[1]=(APPLE_AGX_G4_PROCESS_RANGE){0x8120000ULL,0x10000u,0u};
    k.Backing[2]=(APPLE_AGX_G4_PROCESS_RANGE){0x8200000ULL,0x400000u,0u};
    source_objects[41].GpuVa=k.Backing[0].Va;
    source_objects[42].GpuVa=k.Backing[1].Va;
    source_objects[43].GpuVa=k.Backing[2].Va;
    owner.BuildMark=rc83_mark; owner.BuildMarkContext=&owner;
    rc83_count=0;
    assert(AppleAgxRenderSharedMemoryBuildManagedG4Job(&owner,&m,&k,90u,APPLE_AGX_FALSE,
        arena,APPLE_AGX_EXP208_ARENA_BYTES,source_objects,
        APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,0x1500800000ULL,APPLE_AGX_TRUE,
        &bindings,&staged_job,active_objects,&active_job));
    assert(rc83_count==3u && rc83_points[0]==APPLE_AGX_RENDER_BUILD_MARK_COPIED &&
           rc83_points[1]==APPLE_AGX_RENDER_BUILD_MARK_RELOCATED &&
           rc83_points[2]==APPLE_AGX_RENDER_BUILD_MARK_MANAGED);
    assert(!owner.LastBuildRestored);
    assert(AppleAgxRenderManagerSave(&owner,&m,90u,active_objects));
    rc83_count=0;
    assert(AppleAgxRenderSharedMemoryBuildManagedG4Job(&owner,&m,&k,91u,APPLE_AGX_FALSE,
        arena,APPLE_AGX_EXP208_ARENA_BYTES,source_objects,
        APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,0x1500800000ULL,APPLE_AGX_FALSE,
        &bindings,&staged_job,active_objects,&active_job));
    assert(rc83_count==3u && owner.LastBuildRestored);
    assert(AppleAgxRenderManagerSave(&owner,&m,91u,active_objects));
    /* an unmanaged build reports copy and relocation only */
    rc83_count=0;
    assert(AppleAgxRenderSharedMemoryBuildActiveG4Job(&owner,arena,
        APPLE_AGX_EXP208_ARENA_BYTES,source_objects,
        APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,0x1500800000ULL,APPLE_AGX_FALSE,
        &bindings,&staged_job,active_objects,&active_job));
    assert(rc83_count==2u && rc83_points[1]==APPLE_AGX_RENDER_BUILD_MARK_RELOCATED);
    owner.BuildMark=NULL; owner.BuildMarkContext=NULL;
  }
'''


SHARED_PRELUDE = r'''
static unsigned rc83_points[8], rc83_count;
static void rc83_mark(void *Context, APPLE_AGX_U32 Point) {
  assert(Context != NULL && rc83_count < 8u);
  rc83_points[rc83_count++] = Point;
}
'''


class JobTimingV3(unittest.TestCase):
    def test_slot_sub_stamps_and_dense_ring(self):
        build_and_run(self, TIMING, [SOURCE], INCLUDE)

    def test_shared_builder_reports_points(self):
        source = (SHARED / 'tests/apple_agx_render_shared_memory_test.c').read_text()
        marker = '  assert(AppleAgxRenderSharedMemoryDestroy(&owner) =='
        self.assertEqual(source.count(marker), 1)
        source = source.replace('int main(void) {', SHARED_PRELUDE + 'int main(void) {', 1)
        source = source.replace(marker, shared_marks_fixture() + marker)
        names = ['render_shared_memory', 'g13_compute_work', 'render_template.generated',
                 'render_template_rebase', 'exp208_adapter', 'exp208_gdi',
                 'exp208_framebuffer', 'gdi', 'relocation', 'memory', 'exp208_dynamic']
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'probe.c').write_text(source)
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-fsanitize=address,undefined', '-I', str(SHARED / 'include'),
                str(tmp / 'probe.c'), *[str(SHARED / 'src' / ('apple_agx_' + n + '.c')) for n in names],
                '-o', str(tmp / 'probe')], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(tmp / 'probe')], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_windows_build_and_completion_stamps(self):
        text = BACKEND.read_text()
        build = function_body(text, 'AdmissionExternalBuildJob')
        order = ['runtime->BuildMarkFence = Submission->Submission.Fence;',
                 'AdmissionBackendImageStageJob(',
                 'AdmissionJobSubStaged',
                 'AppleAgxRenderSharedMemoryBuildManagedG4Job(',
                 'AdmissionJobSubPrelude',
                 'AdmissionJobSubFlushed',
                 'runtime->BuildMarkFence = 0u;',
                 'return APPLE_AGX_BACKEND_TRUE;',
                 'BuildFailure:',
                 'runtime->BuildMarkFence = 0u;']
        at = 0
        for item in order:
            found = build.find(item, at)
            self.assertNotEqual(found, -1, item)
            at = found + len(item)
        # the prelude stamp belongs to the v5 prelude branch
        prelude = build[build.index('BackendImage.G4WithCompute) {'):build.index('AdmissionJobSubPrelude')]
        self.assertIn('AdmissionBuildTaComputeBarrier(runtime, &input, &output, Plan, Job)', prelude)
        mark = function_body(text, 'AdmissionRenderBuildMarkWindows')
        self.assertIn('runtime->BuildMarkFence', mark)
        for point, sub in (('COPIED', 'Copied'), ('RELOCATED', 'Relocated'), ('MANAGED', 'Managed')):
            self.assertIn(f'APPLE_AGX_RENDER_BUILD_MARK_{point} ? AdmissionJobSub{sub}', mark)
        save = function_body(text, 'AdmissionSaveG4Manager')
        self.assertLess(save.index('AppleAgxRenderManagerSave('), save.index('AdmissionJobSubSaved'))
        complete = function_body(text, 'AdmissionBackendComplete')
        taken = complete.index('releasedQpc = AdmissionJobQpc();')
        released = complete.index('AdmissionJobSubReleased')
        unlock = complete.rfind('KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);', 0, released)
        self.assertLess(taken, unlock)
        self.assertLess(complete.rfind('AppleAgxCompletionLocalCommitted)) {', 0, taken),
                        taken)
        self.assertLess(released, complete.index('AdmissionJobPhaseNotify'))
        # the stamp helper never runs under SchedulerLock: it takes JobTimingLock
        sub = function_body(text, 'AdmissionJobTimingSubAtWindows')
        self.assertIn('KeAcquireSpinLock(&runtime->JobTimingLock', sub)

    def test_export_writes_hot_only_when_dense(self):
        export = function_body(BACKEND.read_text(), 'AdmissionJobTimingExportWindows')
        hot = export.index('L"Wom1JobTiming971Hot"')
        guard = export.rfind('if (', 0, hot)
        self.assertIn('AdmissionJobTimingDense(&runtime->JobTimingSnapshot)', export[guard:hot])
        self.assertLess(export.index('L"Wom1JobTiming971"'), hot)

    def test_decoder_sub_intervals(self):
        b = load()
        P = b.KMD_PHASES
        def render(at, prelude):
            q = dict(submit=at, worker=at + 10, begin_before=at + 11, begin_after=at + 50,
                     backend_before=at + 51, kick_3d=at + 320, kick_ta=at + 321,
                     backend_after=at + 326, first_progress=at + 700, complete=at + 800,
                     job_end=at + 880, notify=at + 920)
            sub = dict(staged=at + 61, copied=at + 261, relocated=at + 266, managed=at + 276,
                       flushed=at + 318, saved=at + 870, released=at + 915)
            if prelude:
                sub['prelude'] = at + 300
            return q, sub
        slots = []
        for i in range(4):
            q, sub = render(1000 * (i + 1), prelude=i < 2)
            flags = 0x4 | 0x20 * (i < 2) | 0x40 * (i & 1) | 0x80 * (i > 0) | 0x8 * (i == 0)
            slots.append((i + 1, 9452, q, (0, 0, flags), sub))
        data = bytearray(struct.pack('<IIIIQQ', 3, 32 + 64 * 248, 4, 0, 1000000, 0))
        for i in range(64):
            if i < len(slots):
                fence, pid, q, extra, sub = slots[i]
            else:
                fence, pid, q, extra, sub = 0, 0, {}, (0, 0, 0), {}
            data += struct.pack('<IIIIQQ12Q4QII', fence, pid, 0, 0, 0, 0,
                                *[q.get(p, 0) for p in P], 0, 0, 0, 0, 0, 0)
            data += struct.pack('<IIII', *extra, 0)
            data += struct.pack('<8Q', *[sub.get(n, 0) for n in b.KMD_SUBS])
        out = b.kmd_job_timing(bytes(data))
        self.assertEqual(out['version'], 3)
        m = out['sub_us']['median_us']
        self.assertEqual(m['stage'], 10)
        self.assertEqual(m['copy'], 200)
        self.assertEqual(m['relocate'], 5)
        self.assertEqual(m['manager'], 10)
        self.assertEqual(out['sub_us']['samples']['prelude'], 2)
        self.assertEqual(m['prelude'], 24)
        # without a prelude the flush span starts at the manager restore
        self.assertEqual(m['flush'], 30)
        self.assertEqual(m['kick'], 2)
        self.assertEqual(m['save'], 70)
        self.assertEqual(m['timestamp'], 10)
        self.assertEqual(m['release'], 35)
        self.assertEqual(m['notify'], 5)
        self.assertEqual(out['build_flags'],
                         dict(init_bm=1, init_queues=0, prelude=2, slot1=2, manager_restored=3))
        self.assertEqual(out['by_kind']['render']['median_us']['build'], 275)


if __name__ == '__main__':
    unittest.main()
