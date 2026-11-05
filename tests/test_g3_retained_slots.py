"""V1 retained per-process VM slots (investigation/analysis/V1-retained-vm-slots.md).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- A job beginning on a VM slot after a descriptor change of its owner
  without an invalidation of that slot (stale TLB), with the real graph,
  broker client, m1n1 wire/dispatch and m1n1 v5 broker.
- One TLBI per leaf for a paging batch of a process that retains an idle
  lease (EXP2163 measured ~225 leaves per batch: rule R must release once).
- RELOCATE_ROOT/DESTROY refused BUSY for an idle retained lease (poison).
- The allocator stealing a busy lease or using a slot outside 1..K.
- Switch off (Wom1G3RetainedSlots absent/0) differing from the baseline
  LEASE/JOB_BEGIN/JOB_END/RELEASE sequence on slot 1.
- A firmware command built with another slot than the job's lease.
- B1: an unmanaged render keeping a template buffer manager initialised for
  another slot (its InitBM context_id).
- B2: a job without a retained lease reaching another process's retained
  translation (retained slots start at 2; such a render uses slot 1).
- A permanently quarantined slot (bounded retry, counted).
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'
RENDER = ROOT / 'drivers/apple-agx/render-admission'


def function(source, name):
    match = re.search(r'(?m)^(?:static\s+)?[A-Za-z_][\w \t\*]*\b' + re.escape(name) +
                      r'\s*\([^;{]*\)\s*\{', source)
    if match is None:
        raise AssertionError('missing function ' + name)
    depth, pos = 1, match.end()
    while depth:
        depth += {'{': 1, '}': -1}.get(source[pos], 0)
        pos += 1
    return source[match.start():pos]


def m1n1_source():
    m1n1 = Path(os.environ.get('AGX_M1N1_SOURCE', str(ROOT / 'm1n1_windows')))
    return m1n1 if (m1n1 / 'src/hv_agx_gpuva_v5.c').is_file() else None


class RetainedSlotsRealBroker(unittest.TestCase):
    def test_real_broker_replay(self):
        m1n1 = m1n1_source()
        if m1n1 is None:
            self.skipTest('m1n1_windows is uninitialized (set AGX_M1N1_SOURCE); '
                          'never replace the real broker with a stub')
        with tempfile.TemporaryDirectory(prefix='v1-retained-') as tmp:
            d = Path(tmp)
            shutil.copytree(m1n1 / 'src', d / 'm1n1/src')
            shutil.copytree(m1n1 / 'tests', d / 'm1n1/tests')
            (d / 'drivers/apple-agx').mkdir(parents=True)
            (d / 'drivers/apple-agx/shared').symlink_to(SHARED.resolve())
            platform = (m1n1 / 'src/hv_agx_retained_platform.c').read_text()
            case = (ROOT / 'tests/g3_retained_slots_replay.c').read_text()
            assert case.count('/* GPUVA_EXECUTE */') == 1
            (d / 'case.c').write_text(case.replace(
                '/* GPUVA_EXECUTE */', function(platform, 'gpuva_execute')))
            cmd = [os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-function', '-ftrivial-auto-var-init=pattern',
                   '-fsanitize=address,undefined',
                   '-I' + str(d / 'm1n1/src'), '-I' + str(d / 'm1n1/tests'),
                   '-I' + str(SHARED / 'include'), str(d / 'case.c'),
                   str(d / 'm1n1/src/hv_agx_gpuva_v5.c'),
                   str(d / 'm1n1/src/hv_agx_gpuva_v5_mmio.c'),
                   str(SHARED / 'src/apple_agx_uat.c'),
                   str(SHARED / 'src/apple_agx_gpuva_broker_v5_client.c'),
                   str(SHARED / 'src/apple_agx_gpuva_g3_graph.c'),
                   '-o', str(d / 'case')]
            subprocess.run(cmd, check=True, timeout=120)
            ran = subprocess.run([str(d / 'case')], capture_output=True, text=True,
                                 timeout=120)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('g3_retained_slots_replay: PASS', ran.stdout)


class RetainedSlotsTemplate(unittest.TestCase):
    def test_render_template_context_ids_follow_the_job_slot(self):
        """The seven context_id fields (InitBM, TA, 3D) move together from the
        bind's slot to the job's; a torn arena is refused unwritten."""
        with tempfile.TemporaryDirectory(prefix='v1-vm-slot-') as tmp:
            d = Path(tmp)
            (d / 'case.c').write_text(r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "apple_agx_render_template.h"
#include "apple_agx_render_template_vm_slot.h"
static const unsigned fields[7][2] = {{16,4},{18,12},{19,12},{15,0x4c},
    {15,0x284},{17,0x34},{17,0x22c}};
static unsigned field(const unsigned char *a, unsigned i) {
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *l = AppleAgxRenderTemplateObjectLayouts();
  return a[l[fields[i][0]].ArenaOffset + fields[i][1]];
}
int main(void) {
  unsigned bytes = AppleAgxRenderTemplateBytes(), i, previous = 0;
  unsigned char *a = calloc(1, bytes), *copy = malloc(bytes);
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  assert(a && copy && AppleAgxRenderTemplateMaterialize(a, bytes, &roots));
  /* Pristine (63) and selected (1) arenas both retarget. */
  assert(AppleAgxRenderTemplateRetargetVmSlot(a, bytes, 2u, &previous) && previous == 63u);
  for (i = 0; i < 7u; ++i) assert(field(a, i) == 2u);
  assert(AppleAgxRenderTemplateRetargetVmSlot(a, bytes, 4u, &previous) && previous == 2u);
  for (i = 0; i < 7u; ++i) assert(field(a, i) == 4u);
  /* Slot 0, 63 and above are refused. */
  assert(!AppleAgxRenderTemplateRetargetVmSlot(a, bytes, 0u, NULL));
  assert(!AppleAgxRenderTemplateRetargetVmSlot(a, bytes, 63u, NULL));
  /* One torn field: nothing is written. */
  {
    const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *l = AppleAgxRenderTemplateObjectLayouts();
    a[l[17].ArenaOffset + 0x22c] = 1u;
    memcpy(copy, a, bytes);
    assert(!AppleAgxRenderTemplateRetargetVmSlot(a, bytes, 3u, &previous) && previous == 0u);
    assert(memcmp(copy, a, bytes) == 0);
  }
  return 0;
}
''')
            cmd = [os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-I' + str(SHARED / 'include'),
                   str(d / 'case.c'),
                   str(SHARED / 'src/apple_agx_render_template_vm_slot.c'),
                   str(SHARED / 'src/apple_agx_render_template.generated.c'),
                   str(SHARED / 'src/apple_agx_relocation.c'),
                   '-o', str(d / 'case')]
            subprocess.run(cmd, check=True, timeout=120)
            ran = subprocess.run([str(d / 'case')], capture_output=True, text=True,
                                 timeout=60)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_builders_use_the_job_slot(self):
        """Compute, prelude and present take the job's slot, the render
        template is retargeted before staging, and BeginJob publishes the
        slot only with a retained table (switch off: the baseline slot)."""
        text = (RENDER / 'src/backend_platform_windows.c').read_text()
        compute = function(text, 'AdmissionExternalBuildComputeJob')
        self.assertRegex(compute, r'input\.VmSlot = image->JobVmSlot \? image->JobVmSlot :\s*'
                                  r'ADMISSION_GPUVA_G3_JOB_VM_SLOT;')
        build = function(text, 'AdmissionExternalBuildJob')
        self.assertRegex(build, r'input\.VmSlot = image->JobVmSlot \? image->JobVmSlot :\s*'
                                r'ADMISSION_GPUVA_G3_JOB_VM_SLOT;')
        self.assertLess(build.index('AdmissionBackendImageRetargetVmSlot('),
                        build.index('AdmissionBackendImageStageJob('))
        self.assertNotIn('input.VmSlot = ADMISSION_GPUVA_G3_JOB_VM_SLOT', text)
        g3 = (RENDER / 'src/gpuva_g3_windows.c').read_text()
        begin = function(g3, 'AdmissionGpuvaG3BeginJobAs')
        self.assertIn('if (process->Graph.SlotTable)\n'
                      '      adapter->BackendImage.JobVmSlot = process->Graph.Slot;', begin)
        self.assertIn('image->G4ManagerKey.VmSlot=process->Graph.Slot;', begin)
        create = function(g3, 'AdmissionDdiCreateProcess')
        self.assertIn('if (adapter->G3RetainedSlots &&', create)
        platform = (RENDER / 'src/backend_platform_windows.c').read_text()
        self.assertIn('!AdmissionPlatformSwitchDisabled(Context, L"Wom1G3RetainedSlots")', platform)

    def test_unretained_render_takes_the_baseline_slot(self):
        """B2: a render without a retained lease (JobVmSlot 0, e.g. a job
        that skipped BeginJob) is retargeted to slot 1 with the switch on,
        never left on the slot the arena was last retargeted to; switch off
        leaves the arena untouched."""
        image_src = (RENDER / 'src/render_backend_image.c').read_text()
        retarget = function(image_src, 'AdmissionBackendImageRetargetVmSlot')
        with tempfile.TemporaryDirectory(prefix='v1-default-slot-') as tmp:
            d = Path(tmp)
            (d / 'case.c').write_text(r'''
#include <assert.h>
#include <stdlib.h>
#include "apple_agx_render_template.h"
#include "apple_agx_render_template_vm_slot.h"
#define ADMISSION_BACKEND_IMAGE_NULL ((void *)0)
typedef struct { APPLE_AGX_BOOL Ready, G4Compute, PresentCompute;
  APPLE_AGX_U32 BoundFence, JobVmSlot, ArenaCapacity; void *ArenaCpuAddress; } ADMISSION_BACKEND_IMAGE;
''' + retarget + r'''
static unsigned field(const unsigned char *a) {
  return a[AppleAgxRenderTemplateObjectLayouts()[16].ArenaOffset + 4u];
}
int main(void) {
  unsigned bytes = AppleAgxRenderTemplateBytes();
  unsigned char *a = calloc(1, bytes);
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  ADMISSION_BACKEND_IMAGE i = {APPLE_AGX_TRUE, 0, 0, 7u, 4u, 0, a};
  assert(a && AppleAgxRenderTemplateMaterialize(a, bytes, &roots));
  i.ArenaCapacity = bytes;
  assert(AdmissionBackendImageRetargetVmSlot(&i, 7u, 1u) && field(a) == 4u);
  i.JobVmSlot = 0u;
  assert(AdmissionBackendImageRetargetVmSlot(&i, 7u, 0u) && field(a) == 4u);
  assert(AdmissionBackendImageRetargetVmSlot(&i, 7u, 1u) && field(a) == 1u);
  assert(!AdmissionBackendImageRetargetVmSlot(&i, 8u, 1u));
  return 0;
}
''')
            cmd = [os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-I' + str(SHARED / 'include'),
                   str(d / 'case.c'),
                   str(SHARED / 'src/apple_agx_render_template_vm_slot.c'),
                   str(SHARED / 'src/apple_agx_render_template.generated.c'),
                   str(SHARED / 'src/apple_agx_relocation.c'),
                   '-o', str(d / 'case')]
            subprocess.run(cmd, check=True, timeout=120)
            ran = subprocess.run([str(d / 'case')], capture_output=True, text=True, timeout=60)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        build = function((RENDER / 'src/backend_platform_windows.c').read_text(),
                         'AdmissionExternalBuildJob')
        self.assertRegex(build, r'runtime->Adapter->G3RetainedSlots \? '
                                r'ADMISSION_GPUVA_G3_JOB_VM_SLOT : 0u\)')
        private = (RENDER / 'src/gpuva_g3_private.h').read_text()
        self.assertIn('#define ADMISSION_G3_RETAINED_FIRST_SLOT 2u', private)
        create = function((RENDER / 'src/gpuva_g3_windows.c').read_text(), 'AdmissionDdiCreateProcess')
        self.assertIn('ADMISSION_G3_RETAINED_FIRST_SLOT', create)

    def test_unmanaged_render_reissues_initbm_on_a_slot_change(self):
        """B1: the template buffer manager's InitBM context_id is refreshed
        only with InitBM; an unmanaged render in another slot than the last
        unmanaged InitBM must require InitBM (depth 1) or be refused as a
        successor (61). Switch off: no change."""
        text = (RENDER / 'src/backend_platform_windows.c').read_text()
        prepare = function(text, 'AdmissionPrepareUnmanagedInitBm')
        stale = function(text, 'AdmissionUnmanagedInitBmStale')
        with tempfile.TemporaryDirectory(prefix='v1-initbm-') as tmp:
            d = Path(tmp)
            (d / 'case.c').write_text(r'''
#include <assert.h>
typedef unsigned int APPLE_AGX_U32; typedef int APPLE_AGX_BACKEND_BOOL;
typedef unsigned long ULONG; typedef unsigned char BOOLEAN;
#define APPLE_AGX_BACKEND_TRUE 1
#define APPLE_AGX_BACKEND_FALSE 0
#define TRUE 1
#define FALSE 0
#define ADMISSION_GPUVA_G3_JOB_VM_SLOT 1u
#define ADMISSION_JOB_WINDOW_KIND_RENDER 1u
#define ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE 2u
#define ADMISSION_JOB_WINDOW_KIND_COMPUTE 3u
typedef struct { APPLE_AGX_U32 JobVmSlot; void *G4Manager; int G4Compute, PresentCompute; } ADMISSION_BACKEND_IMAGE;
typedef struct { ADMISSION_BACKEND_IMAGE BackendImage; unsigned G3RetainedSlots; } ADMISSION_CONTEXT;
typedef struct { struct { int BufferManagerInitialized; } Runtime; } APPLE_AGX_G13_QUEUE_PROVIDER;
typedef struct { ADMISSION_CONTEXT *Adapter; struct { APPLE_AGX_G13_QUEUE_PROVIDER QueueProvider; } Provider;
  APPLE_AGX_U32 RunningVmSlot, UnmanagedInitBmSlot; } ADMISSION_PLATFORM_RUNTIME;
static int requires;
static APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderRequireInitBm(APPLE_AGX_G13_QUEUE_PROVIDER *p) {
  ++requires; p->Runtime.BufferManagerInitialized = 0; return 1; }
''' + prepare + '\n' + stale + r'''
int main(void) {
  ADMISSION_CONTEXT a = {0}; ADMISSION_PLATFORM_RUNTIME r = {0}; int manager;
  ADMISSION_BACKEND_IMAGE *i = &a.BackendImage;
  r.Adapter = &a;
  /* Switch off: nothing is required or recorded. */
  r.Provider.QueueProvider.Runtime.BufferManagerInitialized = 1;
  assert(AdmissionPrepareUnmanagedInitBm(&r) && !requires && !r.RunningVmSlot);
  assert(!AdmissionUnmanagedInitBmStale(&r, ADMISSION_JOB_WINDOW_KIND_RENDER, FALSE));
  a.G3RetainedSlots = 1u;
  /* First unmanaged render: the queue's first InitBM names slot 2. */
  r.Provider.QueueProvider.Runtime.BufferManagerInitialized = 0;
  i->JobVmSlot = 2u;
  assert(AdmissionPrepareUnmanagedInitBm(&r) && !requires && r.UnmanagedInitBmSlot == 2u);
  r.Provider.QueueProvider.Runtime.BufferManagerInitialized = 1;
  assert(AdmissionPrepareUnmanagedInitBm(&r) && !requires);
  /* Another slot: InitBM again (RED before the fix: the slot-2 BM stays). */
  i->JobVmSlot = 3u;
  assert(AdmissionPrepareUnmanagedInitBm(&r) && requires == 1 && r.UnmanagedInitBmSlot == 3u);
  r.Provider.QueueProvider.Runtime.BufferManagerInitialized = 1;
  /* A job without a retained lease runs in slot 1. */
  i->JobVmSlot = 0u;
  assert(AdmissionPrepareUnmanagedInitBm(&r) && requires == 2 &&
         r.UnmanagedInitBmSlot == 1u && r.RunningVmSlot == 1u);
  r.Provider.QueueProvider.Runtime.BufferManagerInitialized = 1;
  /* Compute leaves the template BM alone. */
  i->G4Compute = 1; i->JobVmSlot = 4u;
  assert(AdmissionPrepareUnmanagedInitBm(&r) && requires == 2 && r.UnmanagedInitBmSlot == 1u);
  i->G4Compute = 0;
  /* A managed InitBM makes the template slot unknown. */
  i->G4Manager = &manager; r.Provider.QueueProvider.Runtime.BufferManagerInitialized = 0;
  assert(AdmissionPrepareUnmanagedInitBm(&r) && r.UnmanagedInitBmSlot == 0u);
  i->G4Manager = 0; r.Provider.QueueProvider.Runtime.BufferManagerInitialized = 1;
  assert(AdmissionPrepareUnmanagedInitBm(&r) && requires == 3 && r.UnmanagedInitBmSlot == 4u);
  /* 61: an unmanaged render successor on a BM of another slot is refused. */
  r.RunningVmSlot = 2u;
  assert(AdmissionUnmanagedInitBmStale(&r, ADMISSION_JOB_WINDOW_KIND_RENDER, FALSE));
  assert(AdmissionUnmanagedInitBmStale(&r, ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE, FALSE));
  assert(!AdmissionUnmanagedInitBmStale(&r, ADMISSION_JOB_WINDOW_KIND_RENDER, TRUE));
  assert(!AdmissionUnmanagedInitBmStale(&r, ADMISSION_JOB_WINDOW_KIND_COMPUTE, FALSE));
  r.RunningVmSlot = 4u;
  assert(!AdmissionUnmanagedInitBmStale(&r, ADMISSION_JOB_WINDOW_KIND_RENDER, FALSE));
  return 0;
}
''')
            cmd = [os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-function', '-fsanitize=address,undefined',
                   str(d / 'case.c'), '-o', str(d / 'case')]
            subprocess.run(cmd, check=True, timeout=120)
            ran = subprocess.run([str(d / 'case')], capture_output=True, text=True, timeout=60)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        worker = text[text.index('  submission.Submission.Kind = AppleAgxSubmissionGdi;'):]
        self.assertLess(worker.index('AdmissionGpuvaG3BeginJob('),
                        worker.index('!AdmissionPrepareUnmanagedInitBm(runtime)'))
        self.assertLess(worker.index('!AdmissionPrepareUnmanagedInitBm(runtime)'),
                        worker.index('AppleAgxBackendRuntimeSubmit('))
        kick = function(text, 'AdmissionJobWindowKickWindows')
        idle = kick[kick.index('s.NextNeedsIdle ='):]
        self.assertIn('AdmissionUnmanagedInitBmStale(runtime, s.Next.Kind, pendingManagerValid)',
                      idle[:idle.index(';')])

    def test_cross_process_depth_two_stays_refused(self):
        window = (RENDER / 'src/render_job_window.c').read_text()
        self.assertRegex(window, r'm->Process != n->Process')


if __name__ == '__main__':
    unittest.main()
