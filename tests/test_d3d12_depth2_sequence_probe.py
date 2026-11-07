"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
R/C/R and R/R must submit every measured list before waiting, and skipped or
cross-wired render outputs cannot pass even when the other work completed.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_d3d12_rc_pair_probe import PROBE, function
import test_d3d12_rc_pair_probe as pair_tests
from test_d3d12_material_probe import ICD, ROOT, SDK


class Depth2SequenceProbe(unittest.TestCase):
    build = pair_tests.RcPairProbe.build

    def test_sequence_submission_and_terminal_failures(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Catch an intermediate CPU wait, partial-close submission, a lost Signal
        error, fence removal accepted as completion, and an unbounded last wait.
        """
        self.assertTrue('static HRESULT q3_submit_sequence(' in PROBE.read_text())
        self.build(r'''
#include <assert.h>
typedef unsigned UINT;
typedef unsigned long long UINT64, ULONGLONG;
typedef int HRESULT, ID3D12CommandQueue, ID3D12Fence, ID3D12GraphicsCommandList, ID3D12CommandList;
#define S_OK 0
#define E_FAIL (-1)
#define FAILED(h) ((h)<0)
static unsigned n, closes, calls, signals, polls, fail_close, fail_signal, outcome;
static ULONGLONG ticks;
static HRESULT ID3D12GraphicsCommandList_Close(ID3D12GraphicsCommandList *p) {
 assert(*p==(int)closes); ++closes; return closes==fail_close ? E_FAIL:S_OK;
}
static void ID3D12CommandQueue_ExecuteCommandLists(ID3D12CommandQueue *q,UINT c,ID3D12CommandList **p) {
 (void)q;assert(closes==n && c==1 && **p==(int)calls && !polls);++calls;
}
static HRESULT ID3D12CommandQueue_Signal(ID3D12CommandQueue *q,ID3D12Fence *f,UINT64 v) {
 (void)q;(void)f;++signals;assert(signals==calls && v==100+signals);
 return signals==fail_signal ? E_FAIL:S_OK;
}
static UINT64 ID3D12Fence_GetCompletedValue(ID3D12Fence *f) {
 (void)f;assert(calls==n && signals==n);++polls;
 return outcome==1 ? ~(UINT64)0 : outcome==2 || polls<4 ? 100+n-1 : 100+n;
}
static ULONGLONG GetTickCount64(void) {return ticks;}
static void Sleep(unsigned ms) {ticks+=ms;}
'''+function('q3_submit_sequence')+r'''
int main(void) {
 int q=0,f=0,a=0,b=1,c=2,submitted;
 ID3D12GraphicsCommandList *lists[]={&a,&b,&c};
 for(n=2;n<=3;++n) for(fail_close=0;fail_close<=n;++fail_close)
 for(fail_signal=0;fail_signal<=n;++fail_signal) for(outcome=0;outcome<3;++outcome) {
  closes=calls=signals=polls=0;ticks=0;submitted=7;
  HRESULT hr=q3_submit_sequence(&q,&f,lists,n,100,&submitted);
  assert((hr==S_OK)==(!fail_close && !fail_signal && !outcome));
  assert(ticks<=3000);
  if(fail_close) assert(!submitted && !calls && !polls);
  else { assert(submitted); if(fail_signal) assert(calls==fail_signal && !polls); }
 }
 for(n=0;n<=4;n+=4) {
  closes=calls=signals=polls=0;submitted=7;
  assert(q3_submit_sequence(&q,&f,lists,n,100,&submitted)==E_FAIL);
  assert(!submitted && !closes && !calls);
 }
}
''')

    def test_every_output_and_distinct_vertex_colors(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Each render must change its own red sentinel to a different exact color;
        swapping/omitting either render or corrupting any compute word must fail.
        """
        self.assertTrue('static unsigned q3_bad_sequence_outputs(' in PROBE.read_text())
        self.build(r'''
#include <assert.h>
#include <string.h>
typedef unsigned UINT;
'''+ '#include "'+str(PROBE.with_name('agx_d3d12_probe_q3_compute.h'))+'"\n'+function('q3_fill_vertices')+'\n'+function('q3_bad_sequence_outputs')+r'''
int main(void) {
 unsigned char green[16384],blue[16384],v[65536];UINT words[64];unsigned i;
 for(i=0;i<4096;++i) {
  green[4*i]=green[4*i+2]=0;green[4*i+1]=green[4*i+3]=255;
  blue[4*i]=blue[4*i+1]=0;blue[4*i+2]=blue[4*i+3]=255;
 }
 for(i=0;i<64;++i) words[i]=i*3+7;
 assert(!q3_bad_sequence_outputs(green,blue,words,1,0));
 assert(!q3_bad_sequence_outputs(green,blue,0,0,0));
 assert(q3_bad_sequence_outputs(blue,green,words,1,0));
 for(i=0;i<16384;++i) {
  green[i]^=1;assert(q3_bad_sequence_outputs(green,blue,words,1,0)==1);green[i]^=1;
  blue[i]^=1;assert(q3_bad_sequence_outputs(green,blue,words,1,0)==1);blue[i]^=1;
 }
 for(i=0;i<64;++i) {words[i]^=1;assert(q3_bad_sequence_outputs(green,blue,words,1,0)==1);words[i]^=1;}
 for(unsigned color=0;color<2;++color) {
  q3_fill_vertices(v,color);const float *f=(const float *)v;
  for(i=0;i<4;++i) {assert(f[6*i+2]==0 && f[6*i+3]==(color?0:1));assert(f[6*i+4]==(color?1:0) && f[6*i+5]==1);}
  for(i=256;i<512;++i) assert(v[i]==0);
 }
}
''')

    def test_recorded_classes_and_resource_identity(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A correctly bounded submitter is useless if R/R accidentally records C,
        R/C/R omits C, or the two renders alias a target or vertex color source.
        Execute the actual recorder for all three named mode selections.
        """
        self.assertTrue('static void q3_record_sequence(' in PROBE.read_text())
        self.build(r'''
#include <assert.h>
typedef unsigned UINT;
typedef unsigned long long UINT64;
typedef int ID3D12GraphicsCommandList,ID3D12RootSignature,ID3D12Resource,D3D12_CPU_DESCRIPTOR_HANDLE;
#define D3D12_RESOURCE_STATE_COMMON 0
#define D3D12_RESOURCE_STATE_UNORDERED_ACCESS 8
static int mode, renders, dispatches, phase;
static void q3_record_render(ID3D12GraphicsCommandList *l,ID3D12RootSignature *r,D3D12_CPU_DESCRIPTOR_HANDLE h,ID3D12Resource *v,UINT n) {
 assert(*r==10 && n==7500);
 if(!renders && mode!=3) {assert(*l==0 && h==20 && *v==30 && !phase);phase=1;}
 else {assert(mode && *l==(mode==1?2:1) && h==21 && *v==31);assert(phase==(mode==2?1:5));phase=6;}
 ++renders;
}
static void transition(ID3D12GraphicsCommandList *l,ID3D12Resource *u,int before,int after) {
 assert(mode!=2 && *l==(mode==3?0:1) && *u==40 && before==0 && after==8 && phase==(mode==3?0:1));phase=2;
}
static void ID3D12GraphicsCommandList_SetComputeRootSignature(ID3D12GraphicsCommandList *l,ID3D12RootSignature *r) {
 assert(*l==(mode==3?0:1) && *r==11 && phase==2);phase=3;
}
static UINT64 ID3D12Resource_GetGPUVirtualAddress(ID3D12Resource *u) {assert(*u==40);return 4000;}
static void ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList *l,UINT index,UINT64 va) {
 assert(*l==(mode==3?0:1) && index==0 && va==4000 && phase==3);phase=4;
}
static void ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(ID3D12GraphicsCommandList *l,UINT index,UINT value,UINT offset) {
 assert(*l==(mode==3?0:1) && index==1 && value==7500 && offset==0 && phase==4);phase=7;
}
static void ID3D12GraphicsCommandList_Dispatch(ID3D12GraphicsCommandList *l,UINT x,UINT y,UINT z) {
 assert(*l==(mode==3?0:1) && x==1 && y==1 && z==1 && phase==(mode?7:4));phase=5;++dispatches;
}
'''+function('q3_record_compute')+'\n'+function('q3_record_sequence')+r'''
int main(void) {
 int a=0,b=1,c=2,r=10,cr=11,v=30,v2=31,u=40;
 ID3D12GraphicsCommandList *lists[]={&a,&b,&c};ID3D12RootSignature *roots[]={&r,&cr};
 for(mode=0;mode<4;++mode) {
  renders=dispatches=phase=0;
  q3_record_sequence(lists,roots,20,21,&v,&v2,&u,7500,mode,mode?7500:0);
  assert(renders==((mode==1 || mode==2)?2:1) && dispatches==(mode==2?0:1));
 }
}
''')

    def test_fresh_stage_windows_abi(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Catch probe helpers missing from the builder staging closure and SDK
        signature/pointer-width mistakes on every supported Windows target.
        """
        if not (SDK/'msvc/vcruntime.h').exists():
            self.skipTest('pinned Windows SDK/MSVC headers absent')
        with tempfile.TemporaryDirectory() as d:
            stage=Path(d)
            for source in [PROBE,*ICD.glob('agx_d3d12_probe_*.h'),ROOT/'drivers/apple-agx/mesa/dxbc/dxbc_tir.h']:
                shutil.copy2(source,stage/source.name)
            for target in ['i686-pc-windows-msvc','x86_64-pc-windows-msvc','aarch64-pc-windows-msvc']:
                cmd=['clang','-target',target,'-fms-extensions','-fms-compatibility','-fsyntax-only',
                     '-Werror=implicit-function-declaration','-Wno-ignored-attributes',
                     '-Wno-ignored-pragma-intrinsic','-Wno-nonportable-include-path','-Wno-pragma-pack']
                for sub in ('um','shared','ucrt','msvc'):cmd+=['-I',str(SDK/sub)]
                r=subprocess.run(cmd+[str(stage/PROBE.name)],capture_output=True,text=True)
                self.assertEqual(r.returncode,0,r.stdout+r.stderr)
