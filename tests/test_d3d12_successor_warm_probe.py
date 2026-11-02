"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Q3CR/Q3RCR first use of the blue render resources delays the measured successor;
warm those exact bindings at depth one without letting stale blue output pass.
"""
import unittest
from test_d3d12_rc_pair_probe import PROBE, function
import test_d3d12_rc_pair_probe as pair_tests


class SuccessorWarmProbe(unittest.TestCase):
    build = pair_tests.RcPairProbe.build

    def test_exact_successor_retired_before_reuse_and_failures_stop(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Catch wrong successor bindings, calibration-sized warm draws, reset of
        outstanding work, reused fence values, or continuation after failure.
        Run the real helper with COM boundary fakes, including each pass failing.
        """
        self.assertIn('static HRESULT q3_warm_successor(', PROBE.read_text())
        self.build(r'''
#include <assert.h>
#include <stdio.h>
typedef unsigned UINT;
typedef unsigned long long UINT64;
typedef int HRESULT, ID3D12CommandQueue, ID3D12Fence, ID3D12GraphicsCommandList;
typedef int ID3D12CommandAllocator, ID3D12PipelineState, ID3D12RootSignature;
typedef int ID3D12Resource, D3D12_CPU_DESCRIPTOR_HANDLE;
#define S_OK 0
#define E_FAIL (-1)
#define SUCCEEDED(h) ((h)>=0)
#define FAILED(h) ((h)<0)
static int phase, passes, resets, terminated, fail_pass, fail_kind;
static void ID3D12GraphicsCommandList_SetPipelineState(ID3D12GraphicsCommandList *l,ID3D12PipelineState *p) {
 assert(*l==3 && *p==10 && phase==0);phase=1;
}
static void q3_record_render(ID3D12GraphicsCommandList *l,ID3D12RootSignature *r,
 D3D12_CPU_DESCRIPTOR_HANDLE h,ID3D12Resource *v,UINT n) {
 assert(*l==3 && *r==20 && h==31 && *v==41 && n==1 && phase==1);phase=2;
}
static HRESULT run_list(ID3D12CommandQueue *q,ID3D12Fence *f,ID3D12GraphicsCommandList *l,UINT64 value,const char *s) {
 (void)s;assert(*q==1 && *f==2 && *l==3 && phase==2 && value==98u+(unsigned)passes);
 ++passes;phase=3;
 return passes==fail_pass && fail_kind==1 ? E_FAIL:S_OK;
}
static HRESULT ID3D12CommandAllocator_Reset(ID3D12CommandAllocator *a) {
 assert(*a==4 && phase==3 && !(passes==fail_pass && fail_kind==1));++resets;phase=4;
 return passes==fail_pass && fail_kind==2 ? E_FAIL:S_OK;
}
static HRESULT ID3D12GraphicsCommandList_Reset(ID3D12GraphicsCommandList *l,ID3D12CommandAllocator *a,void *p) {
 assert(*l==3 && *a==4 && !p && phase==4 && !(passes==fail_pass && fail_kind==2));phase=0;
 return passes==fail_pass && fail_kind==3 ? E_FAIL:S_OK;
}
static void *GetCurrentProcess(void) {return NULL;}
static void TerminateProcess(void *p,unsigned code) {(void)p;assert(code==1);++terminated;}
'''+function('q3_warm_successor')+r'''
int main(void) {
 int q=1,f=2,l=3,a=4,p=10,r=20,v=41;
 for(fail_pass=0;fail_pass<=2;++fail_pass) for(fail_kind=1;fail_kind<=3;++fail_kind) {
  phase=passes=resets=terminated=0;
  HRESULT hr=q3_warm_successor(&q,&f,&l,&a,&p,&r,31,&v,98);
  assert((hr==S_OK)==(!fail_pass));
  if(!fail_pass) assert(passes==2 && resets==2 && phase==0 && !terminated);
  else {
   assert(passes==fail_pass);
   assert(resets==fail_pass-(fail_kind==1));
   assert(terminated==(fail_kind==1));
  }
 }
}
''')

    def test_warmup_is_before_red_sentinel_and_measured_sequence(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Warm-up must use the measured blue objects in both C->R modes, and must
        precede the existing retired red reset so omitting measured R still fails.
        These wiring checks complement the executed helper and output oracle.
        """
        body = function('q3_sequence')
        self.assertIn('q3_warm_successor(', body)
        start = body.index('q3_warm_successor(')
        call = body[start:body.index(';', start)]
        self.assertIn('lists[3]', call)
        self.assertIn('allocators[3]', call)
        self.assertIn('psos[0], roots[0], rtv2, vertices2, base - 2', call)
        self.assertIn('if (SUCCEEDED(hr) && (mode == 1 || mode == 3))', body[start-75:start])
        self.assertLess(body.index('q3_calibrate_compute('), start)
        init = body.index('/* Initialize measured resources')
        self.assertLess(start, init)
        self.assertLess(init, body.index('q3_record_sequence('))
        self.assertIn('ClearRenderTargetView(lists[3], rtv2, red, 0, NULL)', body[init:])
        self.assertIn('run_list(queue, fence, lists[3], base, "Q3RC init")', body[init:])
        self.build('#include "'+str(PROBE.with_name('agx_d3d12_probe_q3_compute.h'))+'"\n'+r'''
#include <assert.h>
typedef unsigned UINT;
'''+function('q3_bad_sequence_outputs')+r'''
int main(void) {
 unsigned char green[16384],blue[16384];unsigned words[64];
 for(unsigned i=0;i<4096;++i) {
  green[4*i]=green[4*i+2]=0;green[4*i+1]=green[4*i+3]=255;
  blue[4*i]=blue[4*i+1]=0;blue[4*i+2]=blue[4*i+3]=255;
 }
 for(unsigned i=0;i<64;++i) words[i]=q3_compute_expected(i,1024);
 assert(!q3_bad_sequence_outputs(green,blue,words,1,1024));
 /* Actual init replaces warm blue with red. Skipping measured R must fail
  * even with the correct compute output and (for R/C/R) correct green R. */
 for(unsigned i=0;i<4096;++i) {blue[4*i]=255;blue[4*i+2]=0;}
 assert(q3_bad_sequence_outputs(green,blue,words,1,1024)==8192);
 assert(q3_bad_sequence_outputs(0,blue,words,1,1024)==8192);
}
''')
