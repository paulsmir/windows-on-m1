"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The bounded capture must not repeat old ring slots, drop new boot generations,
or turn an unsupported/failed escape into a successful capture. Compile the
actual probe function as C and C++ (the native x86 build uses /TP).
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_scanout_vsync_replay import function

ROOT = Path(__file__).resolve().parents[1]


class ScanoutTraceTests(unittest.TestCase):
    def test_bounded_capture_deduplicates_and_handles_restart_and_failure(self):
        source = (ROOT / 'drivers/apple-agx/windows/one-shot/apple_agx_blt_probe.c').read_text()
        self.assertIn('static int follow_vsync(', source)
        body = function(source, 'static int follow_vsync(')
        shim = r'''
#include <stdio.h>
#include <wchar.h>
#include <assert.h>
#include <string.h>
#include "apple_agx_vsync.h"
typedef unsigned UINT; typedef int NTSTATUS; typedef unsigned D3DKMT_HANDLE;
typedef unsigned long long ULONGLONG;
typedef struct {long long QuadPart;} LARGE_INTEGER;
typedef struct {D3DKMT_HANDLE hAdapter;int Type;void *pPrivateDriverData;unsigned PrivateDriverDataSize;} D3DKMT_ESCAPE;
#define D3DKMT_ESCAPE_DRIVERPRIVATE 0
static unsigned calls, sleeps, failure;
static ULONGLONG now;
static ULONGLONG GetTickCount64(void){return now;}
static void Sleep(unsigned ms){assert(ms==50);now+=ms;++sleeps;}
static int QueryPerformanceCounter(LARGE_INTEGER*p){p->QuadPart=(long long)now*24000;return 1;}
static int QueryPerformanceFrequency(LARGE_INTEGER*p){p->QuadPart=24000000;return 1;}
static void QueryInterruptTimePrecise(ULONGLONG*p){*p=now*10000;}
static NTSTATUS D3DKMTEscape(D3DKMT_ESCAPE*e) {
 assert(e->PrivateDriverDataSize==sizeof(APPLE_AGX_VSYNC_QUERY));
 APPLE_AGX_VSYNC_QUERY*q=(APPLE_AGX_VSYNC_QUERY*)e->pPrivateDriverData;
 ++calls;if(failure && calls>=failure)return -1;
 assert(q->Magic==APPLE_AGX_VSYNC_QUERY_MAGIC && q->Version==1);
 memset(q->Events,0,sizeof(q->Events));
 q->Generation=calls<3?7:8;
 q->EventCount=calls<3?100:1;
 q->Events[0].Sequence=calls<3?1:1;
 q->Events[63].Sequence=calls<3?100:0;
 return 0;
}
'''
        scenario = r'''
int main(void){
 assert(follow_vsync(1,1)==0);assert(calls>=20 && calls<=21);
 assert(sleeps>=19 && sleeps<=20);
 calls=0;sleeps=0;now=0;failure=1;
 assert(follow_vsync(1,1)!=0);assert(sleeps==0);
 calls=0;sleeps=0;now=0;failure=2;
 assert(follow_vsync(1,1)!=0);assert(sleeps==1);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'trace.c'
            src.write_text(shim + body + scenario)
            for lang, std in [('c', 'c11'), ('c++', 'c++17')]:
                exe = Path(tmp) / lang.replace('+', 'p')
                subprocess.run(['clang', '-x', lang, '-std='+std, '-Werror',
                                '-I'+str(ROOT/'drivers/apple-agx/shared/include'),
                                str(src), '-o', str(exe)], check=True)
                result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
                # First capture: two old slots once, then the new generation.
                events = [l for l in result.stdout.splitlines() if l.startswith('VSYNC_EVENT')]
                self.assertEqual(len(events), 5)  # 3 successful + 2 before late failure
                self.assertEqual(sum('generation=8 ' in l for l in events), 1)
