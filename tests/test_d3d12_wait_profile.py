"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Nested CPU timers must not double-count a fence wait in its Gallium caller;
Execute/thread boundaries and disabled collection must not leak attribution.
Bounded top-N receipts must expose omitted time and saturation, not wrap it.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT/'drivers/apple-agx/render-admission/umd/include/umd_wait_profile.h'

class WaitProfile(unittest.TestCase):
    def test_nested_scopes_top_sites_and_reset(self):
        self.assertTrue(HEADER.exists(), 'Execute wait collector is missing')
        code = r'''
#include "umd_wait_profile.h"
#include <assert.h>
int main(void) {
 AgxWaitProfile p = {0}; unsigned w[16], q[8];
 assert(!AgxWaitPush(&p,AgxWaitCpuFence,10));
 AgxWaitReset(&p,7,1000000);
 p.Phase=AgxWaitPhaseQueryResolve;
 unsigned a=AgxWaitPush(&p,AgxWaitQueryCommand,100);
 unsigned b=AgxWaitPush(&p,AgxWaitSyncWriter,110);
 unsigned c=AgxWaitPush(&p,AgxWaitCpuFence,120);
 AgxWaitPop(&p,c,170); AgxWaitPop(&p,b,190); AgxWaitPop(&p,a,200);
 AgxWaitPack(&p,w,q);
 assert(w[0]==1 && w[1]==7 && w[2]==0 && w[3]==0);
 assert(w[4]==(AgxWaitPhaseQueryResolve*256+AgxWaitCpuFence));
 assert(w[5]==1 && w[6]==50); /* deepest wait */
 assert(w[7]==(AgxWaitPhaseQueryResolve*256+AgxWaitSyncWriter));
 assert(w[8]==1 && w[9]==30); /* 80 - 50 nested */
 assert(w[12]==20); /* query's 100 - 80 */
 assert(q[6]==1 && q[7]==100); /* inclusive Resolve */
 AgxWaitReset(&p,8,1000000);
 for(unsigned i=1;i<=6;i++) {a=AgxWaitPush(&p,i,0);AgxWaitPop(&p,a,10*i);}
 AgxWaitPack(&p,w,q); assert(w[1]==8 && w[2]==30); /* smallest 10+20 */
 assert(w[4]==6 && w[6]==60 && w[13]==3 && w[15]==30);
 assert(q[2]==0 && q[4]==0 && q[6]==0);
 AgxWaitReset(&p,9,24000000);
 a=AgxWaitPush(&p,AgxWaitCpuFence,1);AgxWaitPop(&p,a,25);
 AgxWaitPack(&p,w,q);assert(w[6]==1); /* QPC conversion */
 AgxWaitReset(&p,10,1);
 a=AgxWaitPush(&p,AgxWaitCpuFence,0);AgxWaitPop(&p,a,~0ull/2);
 AgxWaitPack(&p,w,q);assert(w[6]==0xffffffffu && w[3]);
 AgxWaitReset(&p,11,1000000);
 for(unsigned i=0;i<AGX_WAIT_STACK;i++)assert(AgxWaitPush(&p,1,i));
 assert(!AgxWaitPush(&p,1,999));
 for(unsigned i=AGX_WAIT_STACK;i;i--)AgxWaitPop(&p,i,1000);
 AgxWaitPack(&p,w,q);assert(w[3] && !p.Depth);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/'test.c';src.write_text(code)
            for lang,std in [('c','c11'),('c++','c++17')]:
                exe=Path(d)/('run'+lang)
                run=subprocess.run(['clang','-x',lang,'-std='+std,'-fsanitize=address,undefined',
                    '-I'+str(HEADER.parent),str(src),'-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(run.returncode,0,run.stderr)
                run=subprocess.run([str(exe)],capture_output=True,text=True)
                self.assertEqual(run.returncode,0,run.stderr)



class WaitRuntime(unittest.TestCase):
    def test_execute_threads_gate_linkage_failure_and_nested_paths(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Actual UMD glue must isolate concurrent Executes, preserve failed status,
        read no clock when disabled, and receive C-native calls in a C++ build.
        Inclusive caller paths must still explain nested exclusive fence cost.
        """
        runtime=(ROOT/'drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c').read_text()
        runtime=runtime[runtime.index('static __declspec(thread) AgxWaitProfile'):runtime.index('/* End RC42 wait collector. */')]
        code=r'''
#include "umd_wait_profile.h"
#include <cassert>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#define __declspec(x) thread_local
using LONG=long;
struct LARGE_INTEGER {long long QuadPart;};
static thread_local unsigned long long now;
static thread_local unsigned clocks;
static bool enabled;
static bool AdmissionUmdMeasureEnabled(){return enabled;}
static void QueryPerformanceCounter(LARGE_INTEGER *p){++clocks;p->QuadPart=now;}
static void QueryPerformanceFrequency(LARGE_INTEGER *p){++clocks;p->QuadPart=1000000;}
static LONG InterlockedIncrement(volatile LONG *p){return __sync_add_and_fetch(p,1);}
struct Row{std::string tag;long status;std::vector<unsigned>w;};
static std::vector<Row>rows;static std::mutex mutex;
static void AdmissionUmdDiagnostic(const char *tag,long status,const unsigned *w,unsigned n){
 std::lock_guard<std::mutex>lock(mutex);rows.push_back({tag,status,{w,w+n}});
}
extern "C" void advance(unsigned n){now+=n;}
extern "C" void native_wait(void);
'''+runtime+r'''
int main(){
 assert(!AdmissionUmdWaitExecuteBegin());assert(!AdmissionUmdWaitBegin(AgxWaitCpuFence));
 AdmissionUmdWaitEnd(0);assert(!clocks && rows.empty());
 enabled=true;AgxWin32WaitBeginHook=AdmissionUmdWaitBegin;AgxWin32WaitEndHook=AdmissionUmdWaitEnd;
 auto work=[](){
  assert(AdmissionUmdWaitExecuteBegin());assert(!AdmissionUmdWaitExecuteBegin());
  AdmissionUmdWaitSetPhase(AgxWaitPhaseQueryResolve);
  {AgxUmdWaitScope q(AgxWaitQueryCommand);AgxUmdWaitScope path(AgxWaitInflightSlot);
   advance(10);native_wait();advance(20);}
  AdmissionUmdWaitExecuteEnd(-7);
  unsigned before=clocks;native_wait();assert(before==clocks);
 };
 std::thread a(work),b(work);a.join();b.join();
 unsigned exclusive=0,paths=0,queries=0;unsigned serial[2]={};
 for(const auto &r:rows){assert(r.status==-7);
  if(r.tag=="measure-d3d12-wait"){
   assert(r.w[4]==3*256+AgxWaitRenderFence && r.w[6]==50);
   serial[exclusive++]=r.w[1];
  }else if(r.tag=="measure-d3d12-query-time"){
   ++queries;assert(r.w[6]==1 && r.w[7]==80);
  }else if(r.tag=="measure-d3d12-wait-path"){
   ++paths;assert(r.w[4]==3*256+AgxWaitInflightSlot && r.w[6]==80);
  }else assert(false);
 }
 assert(exclusive==2 && queries==2 && paths==2 && serial[0]!=serial[1]);
}
'''
        native=r'''
#include "agx_win32_wait.h"
unsigned (*AgxWin32WaitBeginHook)(unsigned);
void (*AgxWin32WaitEndHook)(unsigned);
extern void advance(unsigned);
void native_wait(void){unsigned token=AgxWin32WaitBegin(AgxWaitRenderFence);advance(50);AgxWin32WaitEnd(token);}
'''
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/'runtime.cpp';src.write_text(code)
            c=Path(d)/'native.c';c.write_text(native);obj=Path(d)/'native.o';exe=Path(d)/'runtime'
            run=subprocess.run(['clang','-std=c11','-I'+str(ROOT/'drivers/apple-agx/mesa/winsys'),
                '-c',str(c),'-o',str(obj)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            run=subprocess.run(['clang++','-std=c++17','-pthread','-fsanitize=address,undefined',
                '-I'+str(HEADER.parent),str(src),str(obj),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)

if __name__=='__main__': unittest.main()
