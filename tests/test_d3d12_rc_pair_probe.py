"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The old run_list waits after R and cannot exercise a queued C successor.
Execute the real pair helper with delayed fences/errors; never accept removed
or timed-out work, and check every pixel and compute word independently.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / 'drivers/apple-agx/windows/icd/agx_d3d12_probe.c'


def function(name):
    text = PROBE.read_text()
    start = text.index('static ', text.index('/* Q3RC:'))
    start = text.rfind('static ', start, text.index(name + '(', start))
    return text[start:text.index('\n}', start) + 2]


class RcPairProbe(unittest.TestCase):
    def test_submission_order_and_bounded_failures(self):
        self.assertTrue('static HRESULT q3_submit_pair(' in PROBE.read_text())
        helper = function('q3_submit_pair')
        code = r'''
#include <assert.h>
#include <stdio.h>
typedef unsigned long long UINT64, ULONGLONG;
typedef int HRESULT, ID3D12CommandQueue, ID3D12Fence, ID3D12GraphicsCommandList, ID3D12CommandList;
#define S_OK 0
#define E_FAIL (-1)
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
static unsigned calls, closes, signals, polls, mode;
static ULONGLONG ticks;
static HRESULT ID3D12GraphicsCommandList_Close(ID3D12GraphicsCommandList *p) {
 assert(*p==(int)closes); ++closes;return mode==1 && closes==2 ? E_FAIL:S_OK;
}
static void ID3D12CommandQueue_ExecuteCommandLists(ID3D12CommandQueue *q,unsigned n,ID3D12CommandList **p) {
 (void)q;assert(closes==2 && n==1 && **p==(int)calls);++calls;
}
static HRESULT ID3D12CommandQueue_Signal(ID3D12CommandQueue *q,ID3D12Fence *f,UINT64 v) {
 (void)q;(void)f;++signals;assert(calls==signals && v==10+signals);
 return mode==2 && signals==1 ? E_FAIL:S_OK;
}
static UINT64 ID3D12Fence_GetCompletedValue(ID3D12Fence *f) {
 (void)f;assert(calls==2 && signals==2);++polls;
 return mode==3 ? ~(UINT64)0 : mode==4 ? 10 : polls<4?11:12;
}
static ULONGLONG GetTickCount64(void) { return ticks; }
static void Sleep(unsigned ms) { ticks+=ms; }
'''+helper+r'''
int main(void) {
 int q=0,f=0,r=0,c=1,submitted;
 for(mode=0;mode<5;++mode) {
 calls=closes=signals=polls=0;ticks=0;submitted=0;
 HRESULT h=q3_submit_pair(&q,&f,&r,&c,10,&submitted);
 assert((h==S_OK)==(mode==0));assert(ticks<=3000);
 if(mode==1) assert(!submitted && calls==0);
 else assert(submitted);
 if(mode==2) assert(calls==1 && !polls);
 }
}
'''
        self.build(code)

    def test_exact_outputs(self):
        self.assertTrue('static unsigned q3_bad_outputs(' in PROBE.read_text())
        code = r'''
#include <assert.h>
typedef unsigned int UINT;
'''+function('q3_bad_outputs')+r'''
int main(void) {
 unsigned char pixels[64*256];UINT words[64];unsigned i;
 for(i=0;i<4096;++i) { pixels[4*i]=0;pixels[4*i+1]=255;pixels[4*i+2]=0;pixels[4*i+3]=255; }
 for(i=0;i<64;++i) words[i]=i*3+7;
 assert(!q3_bad_outputs(pixels,words));
 for(i=0;i<sizeof(pixels);++i) { pixels[i]^=1;assert(q3_bad_outputs(pixels,words)==1);pixels[i]^=1; }
 for(i=0;i<64;++i) { words[i]^=1;assert(q3_bad_outputs(pixels,words)==1);words[i]^=1; }
}
'''
        self.build(code)

    def test_named_only_watchdog_and_separate_readback(self):
        text=PROBE.read_text()
        self.assertIn('{"Q3RC", q3_rc_pair}', text)
        body=function('q3_sequence')
        self.assertIn('q3_submit_pair(', body)
        self.assertLess(body.index('q3_submit_pair('),body.index('CopyTextureRegion('))
        self.assertIn('q3_bad_outputs(', body)
        self.assertIn('overlap=UNVERIFIED', body)
        self.assertIn('TerminateProcess(GetCurrentProcess(), 1)', body)
        isolated=text[text.index('static int run_isolated('):]
        isolated=isolated[:isolated.index('\n}')+2]
        self.assertIn('TerminateProcess(GetCurrentProcess(), 1); return 1;', isolated)
        self.assertLess(isolated.index('CreateThread('),isolated.index('D3D12CreateDevice('))
        self.assertLess(isolated.index('TerminateProcess('),isolated.index('ID3D12Fence_Release('))
        self.assertNotIn('WaitForSingleObject(watchdog',body)

    def build(self, code):
        with tempfile.TemporaryDirectory() as tmp:
            c=Path(tmp)/'p.c';exe=Path(tmp)/'p';c.write_text(code)
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-ftrivial-auto-var-init=pattern',str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_calibration_is_bounded_and_records_real_instanced_work(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A tiny R retires before C reaches KMD; scale actual raster work with a
        bounded measured calibration, never a sleep or an unchecked duration.
        """
        self.assertIn('static UINT q3_next_instances(',PROBE.read_text())
        self.build(r'''
#include <assert.h>
typedef unsigned int UINT;
'''+function('q3_next_instances')+r'''
int main(void){
 assert(q3_next_instances(1024,7.5)==1024);
 assert(q3_next_instances(1024,1.0)==4096);
 assert(q3_next_instances(1024,30.0)==256);
 assert(q3_next_instances(262144,0.1)==262144);
 assert(q3_next_instances(1,1000.0)==1);
 assert(q3_next_instances(1024,0.0)==1024);
}
''')
        body=function('q3_sequence')
        self.assertIn('q3_calibrate_render(',body)
        self.assertLess(body.index('q3_calibrate_render('),body.index('/* Initialize measured resources'))
        self.assertIn('q3_record_sequence(lists, roots, rtv, rtv2, vertices, vertices2, uav, instances, mode)',body)
        calibrate=function('q3_calibrate_render')
        self.assertIn('q3_render_duration(',calibrate)
        self.assertNotIn('EndQuery(',calibrate)
        self.assertIn('q3_record_render(',calibrate)
        self.assertIn('attempt < 8',calibrate)
        self.assertIn('r_only_gpu_ms=',calibrate)


    def test_firmware_duration_excludes_cpu_delay_and_rejects_stale_records(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A 7 ms API timestamp span with only 1 ms of GPU work must not satisfy
        calibration. Only a unique new successful render's firmware start/end
        counts; reject lost/reset, foreign-only, failed, stale or wrong-kind
        records. EXP2086: DWM jobs and preludes retire in the same window on
        hardware, so a foreign record beside ours is accepted (ours is used) and
        of two of ours the newest is used.
        """
        self.assertIn('static int q3_render_duration(',PROBE.read_text())
        header=PROBE.with_name('agx_d3d12_probe_completion.h')
        self.build('#include "'+str(header)+'"\n'+r'''
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
typedef unsigned long long UINT64;typedef unsigned UINT;
'''+function('q3_render_duration')+r'''
int main(void){
 Q3_GPU_COMPLETION_QUERY q={0};double ms=0;
 q.Count=1;q.Frequency=1000000;q.Cursor=q.Newest=11;
 Q3_GPU_COMPLETION *r=&q.Record[0];
 r->Sequence=11;r->ProcessId=42;r->Kind=1;r->Fence=99;
 r->SubmitQpc=100;r->CompleteQpc=7100;r->Start=6100;r->End=7100;
 assert(q3_render_duration(&q,10,0,42,100,6000,&ms) && ms==1.0);
 r->Start=6100;r->End=13100;assert(q3_render_duration(&q,10,0,42,100,6000,&ms) && ms==7.0);
 q.Lost=1;assert(!q3_render_duration(&q,10,0,42,100,6000,&ms));q.Lost=0;
 q.Reset=1;assert(!q3_render_duration(&q,10,0,42,100,6000,&ms));q.Reset=0;
 q.Count=2;q.Record[1]=*r;q.Record[1].ProcessId=77;q.Record[1].Sequence=12;
 assert(q3_render_duration(&q,10,0,42,100,6000,&ms)==1 && ms==7.0);
 q.Record[1].ProcessId=42;q.Record[1].Start=13200;q.Record[1].End=14200;
 assert(q3_render_duration(&q,10,0,42,100,6000,&ms)==2 && ms==1.0);
 q.Count=1;
 r->Status=1;assert(!q3_render_duration(&q,10,0,42,100,6000,&ms));r->Status=0;
 r->Start=5999;assert(q3_render_duration(&q,10,0,42,100,6000,&ms) && ms==7.1);r->Start=6100;
 r->End=5900;assert(!q3_render_duration(&q,10,0,42,100,6000,&ms));r->End=13100;
 r->Sequence=10;assert(!q3_render_duration(&q,10,0,42,100,6000,&ms));r->Sequence=11;
 r->Kind=2;assert(!q3_render_duration(&q,10,0,42,100,6000,&ms));r->Kind=1;
 assert(!q3_render_duration(&q,10,0,43,100,6000,&ms));
 assert(!q3_render_duration(&q,10,0,42,101,6000,&ms));
 q.Frequency=0;assert(!q3_render_duration(&q,10,0,42,100,6000,&ms));
}
''')

    def test_calibration_loop_uses_firmware_and_measured_instances(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Exercise the actual calibration loop: completed R-only trials, bounded
        growth, allocator retirement/reset, invalid records and measured count.
        """
        header=PROBE.with_name('agx_d3d12_probe_completion.h')
        self.build('#include "'+str(header)+'"\n'+r'''
#include <assert.h>
#include <stdio.h>
typedef unsigned UINT;typedef unsigned long long UINT64;
typedef int HRESULT,ID3D12Device,ID3D12CommandQueue,ID3D12Fence;
typedef int ID3D12GraphicsCommandList,ID3D12CommandAllocator,ID3D12PipelineState;
typedef int ID3D12RootSignature,ID3D12Resource,D3D12_CPU_DESCRIPTOR_HANDLE;
typedef struct {long long QuadPart;} LARGE_INTEGER;
#define S_OK 0
#define E_FAIL (-1)
#define SUCCEEDED(h) ((h)>=0)
#define FAILED(h) ((h)<0)
static unsigned phase,draw_count,trials,mode,reads;
static UINT64 last_end;
static void QueryPerformanceCounter(LARGE_INTEGER *q){q->QuadPart=100;}
static UINT GetCurrentProcessId(void){return 42;}
static HRESULT q3_completions(ID3D12Device *d,UINT64 cursor,Q3_GPU_COMPLETION_QUERY *q){
 (void)d;*q=(Q3_GPU_COMPLETION_QUERY){0};++reads;
 q->Newest=q->Cursor=cursor+1;q->Frequency=1000000;
 if(reads==1){last_end=1000;return 0;}
 assert(phase==3);phase=4;q->Count=1;
 Q3_GPU_COMPLETION *r=&q->Record[0];
 r->Sequence=cursor+1;r->ProcessId=42;r->Kind=1;r->Fence=100+trials;
 r->SubmitQpc=100;r->CompleteQpc=10000;
 r->Start=last_end+100;r->End=r->Start+(mode==2?1:draw_count);last_end=r->End;
 if(mode==1)q->Count=0;
 return 0;
}
static void ID3D12GraphicsCommandList_SetPipelineState(ID3D12GraphicsCommandList *l,ID3D12PipelineState *p){(void)l;(void)p;assert(phase==0);phase=1;}
static void q3_record_render(ID3D12GraphicsCommandList *l,ID3D12RootSignature *r,D3D12_CPU_DESCRIPTOR_HANDLE h,ID3D12Resource *v,UINT n){
 (void)l;(void)r;(void)h;(void)v;assert(phase==1);phase=2;draw_count=n;}
static HRESULT run_list(ID3D12CommandQueue *q,ID3D12Fence *f,ID3D12GraphicsCommandList *l,UINT64 base,const char *s){
 (void)q;(void)f;(void)l;(void)s;assert(base==84+trials && phase==2);++trials;phase=3;return 0;}
static HRESULT ID3D12CommandAllocator_Reset(ID3D12CommandAllocator *a){(void)a;assert(phase==4);phase=5;return 0;}
static HRESULT ID3D12GraphicsCommandList_Reset(ID3D12GraphicsCommandList *l,ID3D12CommandAllocator *a,void *p){(void)l;(void)a;(void)p;assert(phase==5);phase=0;return 0;}
static int GetCurrentProcess(void){return 0;}
static void TerminateProcess(int p,unsigned code){(void)p;(void)code;assert(0);}
'''+function('q3_next_instances')+'\n'+function('q3_render_duration')+'\n'+function('q3_calibrate_render')+r'''
int main(void){unsigned instances=0;int o=0;
 for(mode=0;mode<3;++mode){phase=trials=reads=0;
 int hr=q3_calibrate_render(&o,&o,&o,&o,&o,&o,&o,0,&o,84,&instances);
 assert((hr==S_OK)==(mode!=1));assert(trials<=8 && instances==draw_count);
 if(!mode)assert(instances==7500 && trials==3);
 if(mode==1)assert(trials==1);
 if(mode==2)assert(instances==262144);
 }
}
''')

    def test_completion_wire_layout_on_windows_architectures(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        The existing completion escape must retain identical wire offsets on
        x86, x64 and ARM64; this is an ABI check, not a native WDK build claim.
        """
        header=ROOT/'drivers/apple-agx/render-admission/include/render_job_timing.h'
        probe_header=PROBE.with_name('agx_d3d12_probe_completion.h')
        self.assertTrue(probe_header.exists())
        checks=[]
        pairs=[('ADMISSION_GPU_COMPLETION','Q3_GPU_COMPLETION',
                'Sequence CommandVa SubmitQpc CompleteQpc Start End TaStart TaEnd PreludeStart PreludeEnd Fence ProcessId Kind Status TvbOverflows TvbMaxPages'),
               ('ADMISSION_GPU_COMPLETION_QUERY','Q3_GPU_COMPLETION_QUERY',
                'Magic Version Bytes Count Cursor Newest Lost Frequency MissingTiming Reset Reserved Record')]
        for canonical,probe,fields in pairs:
            checks.append(f'_Static_assert(sizeof({canonical})==sizeof({probe}),"wire size");')
            for field in fields.split():
                checks.append(f'_Static_assert(__builtin_offsetof({canonical},{field})==__builtin_offsetof({probe},{field}),"{field} offset");')
                checks.append(f'_Static_assert(sizeof((({canonical}*)0)->{field})==sizeof((({probe}*)0)->{field}),"{field} size");')
        for name in ['GPU_TIMESTAMP_QUERY_MAGIC','GPU_COMPLETION_VERSION','GPU_COMPLETION_PAGE','GPU_TIMESTAMP_RENDER']:
            checks.append(f'_Static_assert(ADMISSION_{name}==Q3_{name},"{name}");')
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'abi.c'
            c.write_text('#include "'+str(header)+'"\n#include "'+str(probe_header)+'"\n'+'\n'.join(checks))
            for target in ['i686-pc-windows-msvc','x86_64-pc-windows-msvc','aarch64-pc-windows-msvc']:
                subprocess.run(['clang','-target',target,'-std=c11','-Werror','-fsyntax-only',str(c)],check=True)

    def test_warp_control_does_not_require_private_kmd_telemetry(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        The existing WARP API/output control cannot provide Apple's private
        completion escape. Keep it usable, explicitly without GPU timing proof.
        """
        body=function('q3_sequence')
        self.assertIn('if (SUCCEEDED(hr) && g_warp)',body)
        self.assertIn('r_only_gpu_ms=unavailable source=warp-control',body)
        self.assertIn('else if (SUCCEEDED(hr)) hr = q3_calibrate_render',body)
