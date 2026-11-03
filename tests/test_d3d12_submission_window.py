"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A fifth GPU-timeline job currently CPU-blocks solely on the four-entry UMD
window. More tracking must retain every pending set until its exact fence,
including shared BO holds, FIFO results, failed waits and uncertain submits.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'

class SubmissionWindow(unittest.TestCase):
    def test_real_submit_retire_and_capacity(self):
        configure = 's.SubmissionWindow=16;'
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "agx_win32_gpuva.h"
static uint64_t next_fence=0x100000000ull, completed=0, fail_wait;
static unsigned holds[64], waits, accepted, submit_result=1;
static uint64_t results[64];
static int reserve(void *c,uint64_t a,uint64_t b,uint64_t d,uint64_t *v){return 1;}
static int map(void*c,uint64_t a,uint64_t b,uint64_t d,unsigned e,uint64_t*f){return 1;}
static int free_va(void*c,uint64_t a,uint64_t b){return 1;}
static int resident(void*c,const uint64_t*h,unsigned n,uint64_t*f){
 for(unsigned i=0;i<n;i++)++holds[h[i]];*f=0;return 1;}
static int paging(void*c,uint64_t f){return 1;}
static int submit(void*c,const uint64_t*w,unsigned n,uint64_t va,uint32_t bytes,
                  const void*p,uint32_t size,uint64_t*f){
 if(!submit_result)return 0;
 ++accepted;*f=++next_fence;return submit_result;}
static int wait_render(void*c,uint64_t f){
 ++waits;if(f==fail_wait)return 0;
 /* Ordered GPU model: each job produces a distinct full-width result. */
 while(completed<f) {++completed;if(completed>0x100000000ull)
   results[completed-0x100000001ull]=completed*7;}
 return 1;}
static int evict(void*c,const uint64_t*h,unsigned n){
 for(unsigned i=0;i<n;i++){assert(holds[h[i]]);--holds[h[i]];}return 1;}
static int query(void*c,uint64_t f){return f<=completed;}
int main(void){
 AGX_WIN32_GPUVA_OPS ops={reserve,map,free_va,resident,paging,submit,wait_render,evict};
 ops.QueryRender=query;AGX_WIN32_GPUVA_SPACE s;int owner=1;
 assert(AgxWin32GpuvaInit(&s,&ops,&owner));completed=0x100000000ull;
 CONFIGURE
 AGX_WIN32_GPUVA_BO shared={1,0x10000,0x10000,1};
 AGX_WIN32_GPUVA_BO command={2,0x20000,0x10000,1};
 const AGX_WIN32_GPUVA_BO *refs[]={&shared,&command};uint64_t fences[17],out;unsigned payload=1;
 for(unsigned i=0;i<16;i++) {
   int ok=AgxWin32GpuvaSubmit(&s,refs,2,&command,32,NULL,0,&payload,4,&fences[i]);
   assert(ok && "opt-in must queue sixteen jobs without a CPU wait");
   assert(!waits && holds[1]==i+1 && results[i]==0);
 }
 assert(s.OlderCount==15 && accepted==16);
 assert(!AgxWin32GpuvaSubmit(&s,refs,2,&command,32,NULL,0,&payload,4,&out));
 assert(!out && accepted==16 && holds[1]==16);
 assert(!AgxWin32GpuvaUnbindAfter(&s,&shared,fences[15]));
 fail_wait=fences[0];assert(!AgxWin32GpuvaRetire(&s,fences[0]));
 assert(s.OlderCount==15 && holds[1]==16 && !results[0]);
 fail_wait=0;assert(AgxWin32GpuvaRetire(&s,fences[0]));
 assert(s.OlderCount==14 && holds[1]==15 && results[0]==fences[0]*7 && !results[1]);
 assert(AgxWin32GpuvaSubmit(&s,refs,2,&command,32,NULL,0,&payload,4,&fences[16]));
 assert(holds[1]==16 && AgxWin32GpuvaHoldingFence(&s,1)==fences[16]);
 assert(AgxWin32GpuvaRetire(&s,fences[16]));
 assert(!holds[1] && !holds[2] && !s.Held && !s.OlderCount);
 for(unsigned i=0;i<17;i++)assert(results[i]==fences[i]*7);
 assert(AgxWin32GpuvaUnbindAfter(&s,&shared,fences[16]));
 /* Default and invalid configurations keep four. */
 for(unsigned mode=0;mode<3;mode++) {
   assert(AgxWin32GpuvaInit(&s,&ops,&owner));shared=(AGX_WIN32_GPUVA_BO){1,0x10000,0x10000,1};
   INVALID_CONFIG
   for(unsigned i=0;i<4;i++)assert(AgxWin32GpuvaSubmit(&s,refs,2,&command,32,NULL,0,&payload,4,&out));
   uint64_t last=out;
   assert(!AgxWin32GpuvaSubmit(&s,refs,2,&command,32,NULL,0,&payload,4,&out));
   assert(AgxWin32GpuvaRetire(&s,last));
 }
 /* Failed submission drops only its new set. Accepted/uncertain keeps all. */
 assert(AgxWin32GpuvaInit(&s,&ops,&owner));s.SubmissionWindow=16;
 for(unsigned i=0;i<15;i++)assert(AgxWin32GpuvaSubmit(&s,refs,2,&command,32,NULL,0,&payload,4,&out));
 submit_result=0;
 assert(!AgxWin32GpuvaSubmit(&s,refs,2,&command,32,NULL,0,&payload,4,&out));
 assert(!s.Terminal && s.OlderCount==14 && holds[1]==15);
 submit_result=2;
 assert(!AgxWin32GpuvaSubmit(&s,refs,2,&command,32,NULL,0,&payload,4,&out));
 assert(s.Terminal && s.OlderCount==15 && holds[1]==16 && s.Held);
 assert(!AgxWin32GpuvaRetire(&s,s.Older[0].Fence));
 assert(!AgxWin32GpuvaUnbind(&s,&shared));
 /* End the simulated device after the terminal-ownership assertions. */
 for(unsigned i=0;i<s.OlderCount;i++)free(s.Older[i].Handles);
 free(s.Held);
 return 0;
}
'''.replace('CONFIGURE', configure).replace('INVALID_CONFIG',
            's.SubmissionWindow=mode==1?17:mode==2?1:0;')
        self.compile_run(code, [WINSYS / 'agx_win32_gpuva.c'])

    def test_runtime_holds_match_expanded_window(self):
        from test_umd_persistent_residency import BODY, PersistentResidency
        prefix = BODY[:BODY.index('int main(){')]
        if 'UINT SubmissionWindow;' not in prefix:
            prefix = prefix.replace('struct ADMISSION_UMD_DEVICE {',
                                    'struct ADMISSION_UMD_DEVICE { UINT SubmissionWindow;')
        main = r"""
int main(){
 CB cb={mk,ev}; ADMISSION_UMD_DEVICE d={}; d.Magic=ADMISSION_UMD_DEVICE_MAGIC; d.PagingQueue=(HANDLE)1; d.KernelCallbacks=&cb;
 d.SubmissionWindow=16;
 SLOT &s=d.ScreenBuffers[0]; s.Active=1;s.Resident=1;s.Token=10;
 s.KernelAllocation=0x100;s.StagingAllocation=0x200;s.CanonicalGpuVa=0x10000;s.Bytes=0x10000;
 uint64_t token=10,fence=0;
 for(unsigned i=0;i<16;i++) {
   int ok=make_resident(&d,&token,1,&fence);
   if(!ok)fprintf(stderr,"refused hold %u\n",i+1);
   assert(ok==1);
   assert(s.SubmissionHolds==i+1 && s.CopyHeld);s.CopyHeld=0;
 }
 assert(!make_resident(&d,&token,1,&fence) && s.SubmissionHolds==16);
 for(unsigned i=16;i;i--) {assert(evict(&d,&token,1));assert(s.SubmissionHolds==i-1);}
 assert(s.Resident && !made && !evicted);
 d.SubmissionWindow=0;
 for(unsigned i=0;i<4;i++){assert(make_resident(&d,&token,1,&fence)==1);s.CopyHeld=0;}
 assert(!make_resident(&d,&token,1,&fence) && s.SubmissionHolds==4);
 for(unsigned i=0;i<4;i++)assert(evict(&d,&token,1));
 puts("Q19 holds: PASS");return 0;
}
"""
        PersistentResidency.run_replay(self, prefix+main, 'Q19 holds: PASS')

    def test_batch_backpressure_waits_only_at_selected_bound(self):
        from test_g4_batch_begin_keeps_active import BatchBeginKeepsActive
        BatchBeginKeepsActive.replay(self, r"""
int main(void) {
 AGX_WIN32_ASAHI_BACKEND b={0}; struct pipe_screen screen={{&b}};
 struct agx_context ctx={0};struct agx_batch slots[AGX_MAX_BATCHES]={0};
 ctx.base.screen=&screen;ctx.batches.slots=slots;slots[0].ctx=&ctx;
 b.Gpuva.SubmissionWindow=16;b.Gpuva.Ops.WaitRender=wait_render;
 b.Gpuva.Ops.Evict=evict;b.Gpuva.Ops.QueryRender=query_render;
 completed_fence=0;
 for(unsigned i=0;i<15;i++) {
   if(i>=3) {assert(retire_held(&slots[0],&b));assert(!waits && !evicts);}
   b.Gpuva.Older[i].Fence=100+i;
   b.Gpuva.Older[i].Handles=calloc(1,sizeof(uint64_t));
   b.Gpuva.Older[i].Count=1;b.Gpuva.OlderCount=i+1;
 }
 assert(retire_held(&slots[0],&b));
 assert(waits==1 && evicts==1 && completed_fence==100);
 assert(b.Gpuva.OlderCount==14 && b.Gpuva.Older[0].Fence==101);
 assert(AgxWin32GpuvaRetire(&b.Gpuva,114));assert(!b.Gpuva.OlderCount);
 return 0;
}
""")

    def test_exact_switch_configures_both_owners(self):
        configure = body((WINSYS/'agx_d3d12_windows.cpp').read_text(),
                         'configure_submission_window')
        code = r"""
#include <assert.h>
#include <string.h>
#include "agx_win32_gpuva.h"
#define ADMISSION_UMD_IN_FLIGHT_SETS 4u
struct AGX_D3D12_OWNER {
 struct { AGX_WIN32_GPUVA_SPACE Gpuva; } Backend;
 struct { unsigned SubmissionWindow; } Runtime;
};
static const char *setting;
static unsigned GetEnvironmentVariableA(const char *name,char *out,unsigned capacity) {
 assert(!strcmp(name,"APPLE_AGX_D3D12_SUBMISSION_WINDOW"));
 if(!setting)return 0;
 unsigned n=(unsigned)strlen(setting);if(n>=capacity)return n+1;
 memcpy(out,setting,n+1);return n;
}
""" + configure + r"""
int main() {
 const char *cases[]={nullptr,"","0","1","16","01","true","1 ","-1"};
 for(const char *value:cases) {
  setting=value; AGX_D3D12_OWNER owner={};configure_submission_window(&owner);
  unsigned expected=value && !strcmp(value,"1")?16:4;
  assert(owner.Backend.Gpuva.SubmissionWindow==expected);
  assert(owner.Runtime.SubmissionWindow==expected);
 }
}
"""
        self.compile_run(code, cxx=True)

    def compile_run(self, code, sources=(), cxx=False):
        with tempfile.TemporaryDirectory(prefix='q19-window-') as td:
            p=Path(td); (p/'test.c').write_text(code)
            run=subprocess.run(['clang++' if cxx else 'clang','-x','c++' if cxx else 'c',
                '-std=c++17' if cxx else '-std=c11','-DAPPLE_AGX_GPUVA_WINSYS',
                '-fsanitize=address,undefined','-ftrivial-auto-var-init=pattern',
                '-I'+str(WINSYS), '-I'+str(ROOT/'drivers/apple-agx/shared/include'),
                str(p/'test.c'), *map(str,sources), '-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            run=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)

if __name__=='__main__':unittest.main()
