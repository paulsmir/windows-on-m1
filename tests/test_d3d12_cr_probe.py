"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2091's tiny C predecessor could finish before R was queued. Validate the
bounded compute workload oracle, strict own-timestamp selection and C/R wiring.
"""
from pathlib import Path
import ast
import re
import subprocess
import tempfile
import unittest
from test_d3d12_rc_pair_probe import PROBE, function
import test_d3d12_rc_pair_probe as pair_tests

HEADER = PROBE.with_name('agx_d3d12_probe_q3_compute.h')

class CrProbe(unittest.TestCase):
    build = pair_tests.RcPairProbe.build

    def test_compute_oracle_matches_loop_and_rejects_stale_words(self):
        self.assertTrue(HEADER.exists(), 'bounded compute workload missing')
        self.build('#include "'+str(HEADER)+'"\n'+r'''
#include <assert.h>
int main(void) {
 unsigned words[64];
 for(unsigned n=0;n<=262144;n=n? n*4:1) {
  for(unsigned i=0;i<64;++i) {
   unsigned x=3*i+7;
   for(unsigned k=0;k<n;++k) x=x*1664525u+1013904223u;
   words[i]=x;assert(q3_compute_expected(i,n)==x);
  }
  assert(!q3_bad_compute(words,n));
  for(unsigned i=0;i<64;++i) {words[i]^=1;assert(q3_bad_compute(words,n)==1);words[i]^=1;}
  if(n) assert(q3_bad_compute(words,0));
 }
}
''')

    def test_compute_duration_uses_own_timestamps_only(self):
        self.assertIn('static int q3_compute_duration(', PROBE.read_text())
        self.build('#include "'+str(PROBE.with_name('agx_d3d12_probe_completion.h'))+'"\n'+r'''
#include <assert.h>
#include <stdio.h>
typedef unsigned UINT;typedef unsigned long long UINT64;
'''+function('q3_compute_duration')+r'''
int main(void) {
 Q3_GPU_COMPLETION_QUERY q={0};double ms=0;
 q.Count=2;q.Frequency=1000000;
 Q3_GPU_COMPLETION *r=&q.Record[0];
 r->Sequence=11;r->ProcessId=42;r->Kind=2;r->Fence=99;
 r->SubmitQpc=100;r->CompleteQpc=10000;r->Start=1000;r->End=8500;
 q.Record[1]=*r;q.Record[1].ProcessId=77;q.Record[1].Sequence=12;
 assert(q3_compute_duration(&q,10,42,100,&ms)==1 && ms==7.5);
 r->Start=0;assert(!q3_compute_duration(&q,10,42,100,&ms));r->Start=1000;
 r->End=1000;assert(!q3_compute_duration(&q,10,42,100,&ms));r->End=8500;
 r->Kind=1;assert(!q3_compute_duration(&q,10,42,100,&ms));r->Kind=2;
 r->Status=1;assert(!q3_compute_duration(&q,10,42,100,&ms));r->Status=0;
 q.Lost=1;assert(!q3_compute_duration(&q,10,42,100,&ms));q.Lost=0;
 q.Reset=1;assert(!q3_compute_duration(&q,10,42,100,&ms));q.Reset=0;
 q.Frequency=0;assert(!q3_compute_duration(&q,10,42,100,&ms));q.Frequency=1000000;
 assert(!q3_compute_duration(&q,11,42,100,&ms));
 assert(!q3_compute_duration(&q,10,42,101,&ms));
 r->CompleteQpc=99;assert(!q3_compute_duration(&q,10,42,100,&ms));
}
''')

    def test_named_cr_step_calibrates_before_sentinel_reset(self):
        self.assertIn('{"Q3CR", q3_cr}', PROBE.read_text())
        body=function('q3_sequence')
        self.assertLess(body.index('q3_calibrate_compute('), body.index('/* Initialize measured resources'))
        self.assertIn('q3_compute_expected',HEADER.read_text())
        self.assertIn('c_only_gpu_ms=',function('q3_calibrate_compute'))
        isolated=PROBE.read_text().split('static int run_isolated(',1)[1].split('\n}',1)[0]
        self.assertEqual(isolated.count('!strcmp(name, "Q3CR")'),2)

    def test_calibration_retires_warmup_and_measures_bounded_dispatches(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Catch using an unmeasured iteration count, counting warm-up/CPU time,
        resetting an outstanding list, or silently accepting missing C timing.
        """
        self.build('#include "'+str(PROBE.with_name('agx_d3d12_probe_completion.h'))+'"\n'+r'''
#include <assert.h>
#include <stdio.h>
typedef unsigned UINT;typedef unsigned long long UINT64;
typedef int HRESULT,ID3D12Device,ID3D12CommandQueue,ID3D12Fence;
typedef int ID3D12GraphicsCommandList,ID3D12CommandAllocator,ID3D12PipelineState;
typedef int ID3D12RootSignature,ID3D12Resource;
typedef struct {long long QuadPart;} LARGE_INTEGER;
#define S_OK 0
#define E_FAIL (-1)
#define SUCCEEDED(h) ((h)>=0)
#define FAILED(h) ((h)<0)
static unsigned phase,iterations_recorded,trials,mode,reads,resets,terminated;
static void QueryPerformanceCounter(LARGE_INTEGER *q){q->QuadPart=100;}
static UINT GetCurrentProcessId(void){return 42;}
static HRESULT q3_completions(ID3D12Device *d,UINT64 cursor,Q3_GPU_COMPLETION_QUERY *q){
 (void)d;*q=(Q3_GPU_COMPLETION_QUERY){0};++reads;
 q->Newest=q->Cursor=cursor+1;q->Frequency=1000000;
 if(reads==1){assert(trials==1 && phase==0);return mode==3?E_FAIL:S_OK;}
 assert(phase==3);q->Count=1;
 Q3_GPU_COMPLETION *r=&q->Record[0];
 r->Sequence=cursor+1;r->ProcessId=42;r->Kind=2;r->Fence=100+trials;
 r->SubmitQpc=100;r->CompleteQpc=1000000;
 r->Start=1000;r->End=r->Start+(mode==2?1:iterations_recorded);
 if(mode==1)q->Count=0;
 return 0;
}
static void ID3D12GraphicsCommandList_SetPipelineState(ID3D12GraphicsCommandList *l,ID3D12PipelineState *p){(void)l;(void)p;assert(phase==0);phase=1;}
static void q3_record_compute(ID3D12GraphicsCommandList *l,ID3D12RootSignature *r,ID3D12Resource *u,UINT n){
 (void)l;(void)r;(void)u;assert(phase==1);phase=2;iterations_recorded=n;}
static HRESULT run_list(ID3D12CommandQueue *q,ID3D12Fence *f,ID3D12GraphicsCommandList *l,UINT64 base,const char *s){
 (void)q;(void)f;(void)l;(void)s;assert(base==84+trials && phase==2);++trials;phase=3;return mode==4?E_FAIL:S_OK;}
static HRESULT ID3D12CommandAllocator_Reset(ID3D12CommandAllocator *a){(void)a;assert(phase==3);phase=4;++resets;return 0;}
static HRESULT ID3D12GraphicsCommandList_Reset(ID3D12GraphicsCommandList *l,ID3D12CommandAllocator *a,void *p){(void)l;(void)a;(void)p;assert(phase==4);phase=0;return 0;}
static int GetCurrentProcess(void){return 0;}
static void TerminateProcess(int p,unsigned code){(void)p;assert(code==1);++terminated;}
'''+function('q3_next_instances')+'\n'+function('q3_compute_duration')+'\n'+function('q3_calibrate_compute')+r'''
int main(void){unsigned iterations=0;int o=0;
 for(mode=0;mode<5;++mode){phase=trials=reads=resets=terminated=0;
 int hr=q3_calibrate_compute(&o,&o,&o,&o,&o,&o,&o,&o,84,"Q3CR",&iterations);
 assert((hr==S_OK)==(mode==0 || mode==2));
 assert(trials<=9 && iterations==iterations_recorded);
 if(!mode)assert(iterations==7500 && trials==4 && resets==4);
 if(mode==1)assert(trials==2 && resets==1);
 if(mode==2)assert(iterations==262144 && trials==6 && resets==6);
 if(mode==3)assert(trials==1 && resets==1);
 if(mode==4)assert(trials==1 && !resets && terminated);
 }
}
''')

    def test_actual_shader_recurrence_against_independent_oracle(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Catch shader/oracle drift in the actual HLSL loop, seed and lane/store
        address. Execute its integer body as C++; this is not an FXC compile.
        """
        source=HEADER.read_text().split('static const char q3_compute_hlsl[] =',1)[1].split('static unsigned',1)[0]
        hlsl=''.join(ast.literal_eval(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"',source))
        body=hlsl.split('[numthreads(64,1,1)]',1)[1]
        body=body.replace('void main(uint3 id:SV_DispatchThreadID)', 'void shader_main(uint3 id)').replace('[loop]','')
        code='#include "'+str(HEADER)+'"\n'+r'''
#include <assert.h>
typedef unsigned uint;
struct uint3 {uint x,y,z;};
static uint iterations;
struct Output {
 uint words[64];
 void Store(uint offset,uint value) {assert(offset<256 && !(offset%4));words[offset/4]=value;}
} output;
'''+body+r'''
int main() {
 for(uint n: {1u,1024u,7500u,262144u}) {
  iterations=n;
  for(uint i=0;i<64;++i) { output.words[i]=0;shader_main({i,0,0}); }
  assert(!q3_bad_compute(output.words,n));
 }
}
'''
        code='#include <initializer_list>\n'+code
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'shader.cpp';exe=Path(d)/'shader';c.write_text(code)
            subprocess.run(['clang++','-std=c++11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_compiler_module_outlives_shader_and_error_blobs(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A dynamically loaded D3D compiler owns the returned blob vtables. Unload
        only after PSO creation and release of every shader/error blob, including
        failed compilation and failed PSO creation paths.
        """
        self.build(r'''
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <wchar.h>
typedef int HRESULT,HMODULE,ID3DBlob,ID3D12Device,ID3D12PipelineState;
typedef struct {struct {void *pShaderBytecode;size_t BytecodeLength;} CS;} D3D12_COMPUTE_PIPELINE_STATE_DESC;
#define LOAD_LIBRARY_SEARCH_SYSTEM32 1
#define D3DCOMPILE_ENABLE_STRICTNESS 2
#define E_FAIL (-1)
#define SUCCEEDED(h) ((h)>=0)
static const char q3_compute_hlsl[]="shader";
static int loaded,live,mode,creates,shader,error;
static HMODULE LoadLibraryExW(const wchar_t *s,void *p,int flags){(void)s;(void)p;(void)flags;loaded=mode!=1;return loaded;}
typedef HRESULT (*pD3DCompile)(const char *,size_t,const char *,void *,void *,const char *,const char *,unsigned,unsigned,ID3DBlob **,ID3DBlob **);
static HRESULT compile_stub(const char *s,size_t n,const char *name,void *a,void *b,const char *entry,const char *profile,unsigned x,unsigned y,ID3DBlob **out,ID3DBlob **err){
 (void)s;(void)n;(void)name;(void)a;(void)b;(void)entry;(void)profile;(void)x;(void)y;assert(loaded);
 *err=&error;++live;
 if(mode==3)return E_FAIL;
 *out=&shader;++live;return 0;
}
static void *GetProcAddress(HMODULE h,const char *name){(void)h;(void)name;return mode==2?NULL:(void *)compile_stub;}
static void FreeLibrary(HMODULE h){(void)h;assert(loaded && !live);loaded=0;}
static void *ID3D10Blob_GetBufferPointer(ID3DBlob *b){(void)b;assert(loaded && live);return (void *)"bytes";}
static size_t ID3D10Blob_GetBufferSize(ID3DBlob *b){(void)b;assert(loaded && live);return 5;}
static void ID3D10Blob_Release(ID3DBlob *b){(void)b;assert(loaded && live);--live;}
static const int IID_ID3D12PipelineState=1;
static HRESULT ID3D12Device_CreateComputePipelineState(ID3D12Device *d,const D3D12_COMPUTE_PIPELINE_STATE_DESC *cp,const int *iid,void **out){
 (void)d;(void)iid;(void)out;assert(loaded && live && cp->CS.pShaderBytecode && cp->CS.BytecodeLength==5);++creates;return mode==4?E_FAIL:0;
}
'''+function('q3_compute_shader')+r'''
int main(void) {
 for(mode=0;mode<5;++mode) {
  ID3D12Device device=0;ID3D12PipelineState *pso=NULL;
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc={0};
  loaded=live=creates=0;
  assert((q3_compute_shader(&device,&desc,&pso)==0)==(mode==0));
  assert(!loaded && !live && !desc.CS.pShaderBytecode && !desc.CS.BytecodeLength);
  assert(creates==((mode==0 || mode==4)?1:0));
 }
}
''')
