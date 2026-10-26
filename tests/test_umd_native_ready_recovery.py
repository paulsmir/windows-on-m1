"""RC50 native-proof retry lifetime, using the production UMD bodies.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? An accepted scheduling touch
with unknown completion must retain the submission set, and a bounded retry
must never convert a permanent native-backing or identity failure into success.
"""
import unittest

import test_g4_native_allocation_guard as guard
from test_umd_async_render_completion import function


def replay(cases):
    fixture = guard.UMD_FIXTURE[:guard.UMD_FIXTURE.index('int main() {')]
    old_touch = function(fixture.replace('[[maybe_unused]] ', ''), 'touch_device')
    fixture = fixture.replace('[[maybe_unused]] ' + old_touch, '')
    fixture = fixture.replace('#include "apple_agx_g3_copy_abi.h"',
        '#include "apple_agx_g3_copy_abi.h"\n'
        '#pragma clang diagnostic push\n'
        '#pragma clang diagnostic ignored "-Wmissing-field-initializers"\n'
        '#include "apple_agx_g4_submit.h"\n#pragma clang diagnostic pop')
    fixture = fixture.replace('static int signal_render(',
        'static bool signal_fail, wait_fail, submit_fail, malformed_index, outside_va;\n'
        'static uint64_t signalled; static unsigned waits;\n'
        'static int signal_render(')
    fixture = fixture.replace('ADMISSION_UMD_DEVICE *,uint64_t) {++signals;return 1;}',
        'ADMISSION_UMD_DEVICE *,uint64_t n) {++signals;signalled=n;return !signal_fail;}')
    fixture = fixture.replace('static HRESULT cb(UINT,D3DDDICB_SUBMITCOMMAND*) {++submits;return 0;}', r'''
static HRESULT cb(UINT,D3DDDICB_SUBMITCOMMAND *r) {
 assert(!locks); ++submits;
 auto *t=(APPLE_AGX_G4_TOUCH *)r->pPrivateDriverData;
 if(t && r->PrivateDriverDataSize==sizeof(*t) && t->Magic==APPLE_AGX_G4_TOUCH_MAGIC) {
  ++touches; if(submit_fail) return -17;
  assert(r->Commands==0x10000 && r->CommandLength==4);
  if(deferred) bad=0;
 }
 return S_OK;
}
static int wait_object(ADMISSION_UMD_DEVICE *,D3DKMT_HANDLE object,uint64_t n) {
 assert(object==8 && n==signalled); ++waits; return !wait_fail;
}
''')
    # The callback above uses the existing fixture's deferred-publication state.
    fixture = fixture.replace('static unsigned touches;\nstatic bool deferred, touch_fail;', '')
    fixture = fixture.replace('static HRESULT cb(',
                              'static unsigned touches; static bool deferred;\nstatic HRESULT cb(')
    fixture = fixture.replace('b->FailedIndex=i;return escape_fail',
        'b->FailedIndex=malformed_index ? b->Count : i;return escape_fail')
    fixture = fixture.replace('q->FailedVa=q->Ready?0:q->GpuVa+32768;',
        'q->FailedVa=q->Ready?0:q->GpuVa+(outside_va?q->AllocationBytes:32768);')
    funcs = '\n'.join(function(guard.UMD.read_text(), n) for n in
                      ('touch_device', 'native_held_ready', 'submit'))
    fixture = fixture.replace('@@FUNCS@@', funcs)
    fixture += r'''
static void init(ADMISSION_UMD_DEVICE &d, CALLBACKS &cbks, ADAPTER &a) {
 d={};d.Adapter=&a;d.KernelCallbacks=&cbks;d.KernelContext=7;
 d.RuntimeDevice.handle=3;d.RenderSyncObject=8;d.NextRenderFence=11;
 for(unsigned i=0;i<3;++i) {auto &s=d.ScreenBuffers[i];s.Active=s.CopyHeld=true;
  s.KernelAllocation=i+1;s.CanonicalGpuVa=0x10000ull*(i+1);s.Bytes=65536;s.SubmissionHolds=1;}
 escapes=queries=touches=submits=signals=waits=uploads=0;
 signal_fail=wait_fail=submit_fail=malformed_index=outside_va=escape_fail=deferred=false;
 bad=2;reason=AppleAgxG3WalkLeafAbsent;
}
int main() {
 (void)hold;
 CALLBACKS cbks={cb,signal_cb,escape_cb};ADAPTER a={ADMISSION_UMD_ADAPTER_MAGIC,{9}};
 ADMISSION_UMD_DEVICE d={}; uint64_t fence=0; unsigned private_data=0;
''' + cases + '\n}\n'
    guard.NativeAllocationGuard().compile_run(fixture, cpp=True)


class NativeReadyRecovery(unittest.TestCase):
    def test_deferred_topology_and_interior_leaf_keep_bounded_retry(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Missing parent
        edges and interior leaves are deferred publication too; the real
        graph reports an interior missing component as TailShort.
        """
        replay(r'''
 for(unsigned why: {AppleAgxG3WalkNoRoot,AppleAgxG3WalkNoTable,
                    AppleAgxG3WalkLeafAbsent,AppleAgxG3WalkLeafNotPublished,
                    AppleAgxG3WalkTailShort}) {
  init(d,cbks,a);reason=why;deferred=true;
  assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==1);
  assert(!d.DrawTerminal && touches==1 && escapes==2 && queries==3 && fence==13);
 }
''')

    def test_accepted_touch_uncertainty_retains_submission_ownership(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A successful
        SubmitCommandCb followed by failed signalling/waiting must propagate
        Submit=2, the real winsys branch that retains all holds, not Submit=0.
        """
        replay(r'''
 for(unsigned phase=0;phase<2;++phase) {
  init(d,cbks,a);signal_fail=phase==0;wait_fail=phase==1;
  assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==2);
  assert(d.DrawTerminal && d.GpuvaFailure==E_FAIL);
  assert(submits==1 && touches==1 && signals==1 && waits==phase);
  assert(escapes==1 && !fence && d.NextRenderFence==11 && !allocations);
  for(unsigned i=0;i<3;++i) assert(d.ScreenBuffers[i].CopyHeld && d.ScreenBuffers[i].SubmissionHolds==1);
  assert(!touch_device(&d,0x10000) && submits==1);
 }
''')

    def test_clean_refusal_and_deferred_success_keep_distinct_outcomes(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A refused touch
        has no accepted work; successful deferred publication retries exactly
        once, reproves both entries and uses a later final submission fence.
        """
        replay(r'''
 init(d,cbks,a);submit_fail=true;
 assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==0);
 assert(!d.DrawTerminal && d.GpuvaFailure==-17 && submits==1 && !signals && !waits && !fence);
 init(d,cbks,a);deferred=true;
 assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==1);
 assert(!d.DrawTerminal && d.GpuvaFailure==S_OK && fence==13 && signalled==13);
 assert(submits==2 && touches==1 && signals==2 && waits==1 && escapes==2 && queries==3);
 init(d,cbks,a);fence=0;
 assert(!submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence));
 assert(!d.DrawTerminal && d.GpuvaFailure==E_OUTOFMEMORY && !fence);
 assert(submits==1 && touches==1 && escapes==2 && queries==2 && !allocations);
''')

    def test_identity_transport_and_malformed_proofs_never_schedule_retry(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A successful
        escape with an identity/translation failure or invalid failed index
        must not schedule a touch or be classified as transient residency.
        """
        replay(r'''
 for(unsigned fault=0;fault<6;++fault) {
  init(d,cbks,a);
  if(fault==0)reason=APPLE_AGX_G3_NATIVE_QUERY_IDENTITY;
  if(fault==1)reason=APPLE_AGX_G3_NATIVE_QUERY_TRANSLATION;
  if(fault==2)malformed_index=true;
  if(fault==3)escape_fail=true;
  if(fault==4)reason=~0u;
  if(fault==5)outside_va=true;
  assert(!submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence));
  assert(!d.DrawTerminal && !touches && !submits && !signals && !waits && !fence);
  assert(escapes==1 && d.GpuvaFailure==E_FAIL && !allocations);
 }
''')


if __name__ == '__main__':
    unittest.main()
