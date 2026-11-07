"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
General depth two owns only two context fence slots. Publishing no node bound
lets VidSch call the KMD with more packets than that context can retain. Execute
the actual scheduling-cap assignment using the WDK 26100 bitfield, then drive
real G4 queue/adoption and the real firmware/provider/backend runtime under
that advertised bound. This models Windows respecting its documented cap; it
cannot establish which Prepare predicate fired in EXP2092.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_g4_sysmem_page_caps import declaration, WDK
from test_g4_submit_virtual_replay import function_body, replay_functions, SOURCE, ROOT
from test_render_job_window import RC102_MODEL, RC102_SOURCES, SHARED, INCLUDE

RA = ROOT / 'drivers/apple-agx/render-admission'


def caps_source():
    wdk = (WDK / 'd3dkmddi.h').read_text()
    versions = '\n'.join(line for line in (WDK / 'd3dukmdt.h').read_text().splitlines()
                         if line.startswith('#define DXGKDDI_INTERFACE_VERSION_'))
    lifecycle = (RA / 'src/lifecycle.c').read_text()
    start = lifecycle.index('        caps->MaxAllocationListSlotId =')
    end = lifecycle.index('        caps->PreemptionCaps.GraphicsPreemptionGranularity', start)
    assignments = lifecycle[start:end]
    worker = (RA / 'src/backend_platform_windows.c').read_text()
    name = 'AdmissionPlatformRuntimeGeneralDepth2'
    getter = function_body(worker, name).replace('ADMISSION_CONTEXT', 'CAP_CONTEXT') if name in worker else ''
    return versions + r'''
#define DXGKDDI_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM3_0
#define _Use_decl_annotations_
#define TRUE 1
#define FALSE 0
#define ADMISSION_GDI_ALLOCATION_LIST_SIZE 32u
''' + declaration(wdk, 'DXGK_VIDSCHCAPS') + r'''
typedef struct { unsigned char Depth2General; } ADMISSION_PLATFORM_RUNTIME;
typedef struct { void *PlatformRuntime; } CAP_CONTEXT;
''' + getter + r'''
static DXGK_VIDSCHCAPS advertised(unsigned enabled, unsigned present) {
  ADMISSION_PLATFORM_RUNTIME runtime = {(unsigned char)enabled};
  CAP_CONTEXT context_storage = {present ? &runtime : NULL};
  CAP_CONTEXT *context = &context_storage;
  struct {
    unsigned MaxAllocationListSlotId, MaxQueuedFlipOnVSync;
    struct { unsigned NbAsymetricProcessingNodes; } GpuEngineTopology;
    DXGK_VIDSCHCAPS SchedulingCaps;
  } storage = {0}, *caps = &storage;
  (void)context;
''' + assignments + r'''
  return caps->SchedulingCaps;
}
'''


class Depth2PacketCap(unittest.TestCase):
    def build_run(self, code, sources=(), extra=()):
        with tempfile.TemporaryDirectory(prefix='q8-packet-cap-') as d:
            p = Path(d)
            (p / 'test.c').write_text(code)
            production = SOURCE.read_text()
            funcs = replay_functions(production)
            funcs += '\n' + function_body(production, 'AdmissionG4PendingKickLocked')
            funcs += '\n' + function_body(production, 'AdmissionG4PendingAdoptLocked')
            (p / 'g4_submit_virtual_functions.inc').write_text(funcs)
            cmd = [os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                   '-Werror', '-Wno-unused-function', '-ftrivial-auto-var-init=pattern',
                   '-fsanitize=address,undefined', '-I', str(p), '-I', str(INCLUDE),
                   '-I', str(SHARED / 'include'), *extra, str(p / 'test.c'),
                   *map(str, sources), '-o', str(p / 'test')]
            built = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(p / 'test')], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_real_g4_queue_same_and_other_context_third_request(self):
        shim = (ROOT / 'tests/g4_submit_virtual_replay.c').read_text()
        prefix, main = shim.split('int main(void) {', 1)
        prefix = prefix.replace('#include "g4_submit_virtual_functions.inc"', r'''static int AdmissionRenderPacketActivate(ADMISSION_RENDER_PACKET *p, unsigned f) {
  if(p->State!=AdmissionRenderPacketQueued || p->Description.Fence!=f) return 0;
  p->State=AdmissionRenderPacketActive; return 1;
}
#include "g4_submit_virtual_functions.inc"
''')
        setup = main.split('  assert(AdmissionG4SubmitVirtualEnvelope', 1)[0]
        code = prefix + caps_source() + 'int main(void) {\n' + setup + CASES
        self.build_run(code, [RA / 'src/render_job_ahead.c',
                             SHARED / 'src/apple_agx_g4_submit.c',
                             SHARED / 'src/apple_agx_scheduler.c'], ['-DREPLAY_JOB_AHEAD'])

    def test_real_runtime_rr_then_waiting_requests_retire_exactly(self):
        source = RC102_MODEL.read_text()
        source = source.replace('  s.Enabled = 1; s.Resetting',
            '  s.General = (w->caps & ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER) != 0;\n'
            '  s.SameManagerSlot = s.General;\n  s.Enabled = 1; s.Resetting')
        source = source.replace('if (AdmissionJobWindowUsesManager(w->job[a].kind) &&',
            'if (!(w->caps & ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER) &&\n'
            '      AdmissionJobWindowUsesManager(w->job[a].kind) &&')
        # Keep the established firmware/queue/backend implementation intact.
        source = source[:source.index('int main(void) {')]
        code = '#include <assert.h>\ntypedef unsigned UINT; typedef unsigned char BOOLEAN;\n'
        code += source + caps_source() + RUNTIME_CASES
        self.build_run(code, RC102_SOURCES.values())


CASES = r'''
  DXGK_VIDSCHCAPS off = advertised(0,1), absent = advertised(1,0);
  /* Whole scheduling bitfield stays byte-identical to the pre-fix profile. */
  assert(off.Value == 5u && absent.Value == off.Value);
  unsigned cap = advertised(1,1).HwQueuePacketCap;
  /* Zero gives no explicit two-packet bound; do not silently assume two. */
  assert(cap != 0u && cap <= 2u);
  for (unsigned same = 0; same < 2; ++same) {
    ADMISSION_RENDER_CONTEXT other = context;
    ADMISSION_RENDER_CONTEXT *third = same ? &context : &other;
    memset(&adapter.RenderPacket,0,sizeof(adapter.RenderPacket));
    memset(adapter.G4Pending,0,sizeof(adapter.G4Pending));
    adapter.G4PendingHead=adapter.G4PendingCount=adapter.G4PrebindFence=0;
    adapter.BackendImage.BoundFence=0;
    adapter.RuntimeQueueable=1;
    context.Object.FenceOutstanding=context.GpuvaG3SecondFence=0;
    other.Object.FenceOutstanding=other.GpuvaG3SecondFence=0;
    AppleAgxSchedulerInitialize(&adapter.Scheduler);
    ahead_run_a(&adapter,&context,20);
    args.hContext=&context; args.SubmissionFenceId=21;
    assert(AdmissionG4SubmitVirtualEnvelope(&adapter,&context,&args)==STATUS_SUCCESS);
    ADMISSION_JOB_AHEAD_STATE st; ULONG refusal=0;
    ahead_state(&st,20);
    assert(AdmissionG4PendingPrebindHead(&adapter,&st,&refusal));
    assert(AdmissionG4PendingKickLocked(&adapter,21));
    assert(adapter.Scheduler.ActiveFence==20 && adapter.Scheduler.KickedFence==21);
    assert(context.Object.FenceOutstanding==20 && context.GpuvaG3SecondFence==21);
    /* The third request exists in VidSch, but the advertised node bound
     * forbids calling SubmitCommandVirtual while both packets remain unreported. */
    unsigned outstanding=2, ddi_calls=2;
    if (outstanding < cap) ++ddi_calls;
    assert(ddi_calls==2);
    /* Complete head, adopt its already-kicked successor, then admit third. */
    ahead_complete_a(&adapter,&context,20); --outstanding;
    assert(AdmissionG4PendingAdoptLocked(&adapter,21));
    assert(outstanding < cap);
    args.hContext=third; args.SubmissionFenceId=22;
    assert(AdmissionG4SubmitVirtualEnvelope(&adapter,third,&args)==STATUS_SUCCESS);
    assert(adapter.Scheduler.LastSubmittedFence==22);
    ahead_complete_a(&adapter,&context,21);
    adapter.BackendImage.BoundFence=0;
    assert(AdmissionG4PendingBindHead(&adapter));
    assert(AppleAgxSchedulerActivateFence(&adapter.Scheduler,0,0,22));
    ahead_complete_a(&adapter,third,22);
    assert(adapter.Scheduler.CompletedFence==22 && adapter.G4PendingCount==0);
    assert(!context.Object.FenceOutstanding && !context.GpuvaG3SecondFence);
    assert(!other.Object.FenceOutstanding && !other.GpuvaG3SecondFence);
    assert(!AppleAgxSchedulerCompleteActiveFence(&adapter.Scheduler,0,0,22));
  }
  puts("Q8 G4 same/other-context bounded admission: PASS");
  return 0;
}
'''

RUNTIME_CASES = r'''
int main(void) {
  for (unsigned foreign=0; foreign<2; ++foreign) {
    world *w=&W; start(w,"q8-cap-rr");
    w->caps |= ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER;
    w->gate=advertised(1,1).HwQueuePacketCap;
    assert(w->gate && w->gate<=2);
    /* Warm queue/manager epoch, then two render jobs and a waiting third. */
    add(w,K_R,OUT_OK,7); run(w,20);
    add(w,K_R,OUT_OK,7); add(w,K_R,OUT_OK,7);
    add(w,K_R,OUT_OK,foreign ? 9 : 7);
    unsigned saw_pair=0;
    for(unsigned tick=0;tick<100;++tick) {
      run(w,1);
      assert(w->nsub-finished(w)<=2);
      if (w->kicked_behind[3] && !w->final_[2]) {
        saw_pair=1; assert(w->nexplan==3 && w->nsub==3);
      }
    }
    assert(saw_pair && !finish(w));
    assert(w->nr==4 && w->max_fw==2 && !w->resets);
    for(unsigned i=0;i<4;++i) assert(w->rep[i]==i+1 && !w->repd[i+1]);
    unsigned reports=w->nr; run(w,10); assert(w->nr==reports);
  }
  puts("Q8 real runtime RR and waiting third: PASS");
  return 0;
}
'''
