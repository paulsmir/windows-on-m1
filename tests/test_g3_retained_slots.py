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
        self.assertIn('AdmissionPlatformSwitchEnabled(Context, L"Wom1G3RetainedSlots")', platform)

    def test_cross_process_depth_two_stays_refused(self):
        window = (RENDER / 'src/render_job_window.c').read_text()
        self.assertRegex(window, r'm->Process != n->Process')


if __name__ == '__main__':
    unittest.main()
