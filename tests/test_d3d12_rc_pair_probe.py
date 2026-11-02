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
        body=function('q3_rc_pair')
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
