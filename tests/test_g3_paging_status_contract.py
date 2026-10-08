"""EXP1026: BuildPagingBuffer never returns a status Microsoft disallows.

EXP1014 and EXP1025 bugchecked 0x10E/0xB (VIDEO_MEMORY_MANAGEMENT_INTERNAL,
"Driver returned an invalid error code from BuildPagingBuffer", P3
0xC0000184) in VIDMM_GLOBAL::UpdatePageTable during EvictResource: the
UpdatePageTable path returned STATUS_INVALID_DEVICE_STATE for a destroyed or
poisoned process. DXGKDDI_BUILDPAGINGBUFFER permits only STATUS_SUCCESS,
STATUS_GRAPHICS_ALLOCATION_BUSY and STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER.
Invariants:
- permitted statuses pass through unchanged;
- any other status from UpdatePageTable or FlushTlb completes with
  STATUS_SUCCESS and poisons the owning process (its jobs are refused);
- other operations are not rewritten.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c'


def function(text, name):
    m = re.search(r'(?m)^(?:static )?[A-Z][A-Za-z0-9_ *]*\b' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


BODY = r'''
#include <cassert>
#include <cstdio>
typedef long NTSTATUS; typedef unsigned char BOOLEAN; typedef void *HANDLE; typedef unsigned long ULONG; typedef long LONG;
#define TRUE 1
#define FALSE 0
#define NULL 0
#define PASSIVE_LEVEL 0
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_GRAPHICS_ALLOCATION_BUSY ((NTSTATUS)0xC01E0102L)
#define STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER ((NTSTATUS)0xC01E0001L)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184L)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
enum { DXGK_OPERATION_FLUSH_TLB = 12, DXGK_OPERATION_UPDATE_PAGE_TABLE = 11, DXGK_OPERATION_VIRTUAL_FILL = 13 };
struct UPT { HANDLE hProcess; }; struct FT { HANDLE hProcess; };
struct DXGKARG_BUILDPAGINGBUFFER { int Operation; UPT UpdatePageTable; FT FlushTlb; };
struct ADMISSION_G3_PROCESS { BOOLEAN Poisoned; ULONG PoisonSite; };
struct ADMISSION_G3_STATE { int Lock; ADMISSION_G3_PROCESS *only; HANDLE handle; };
struct ADMISSION_CONTEXT { void *GpuvaG3State; volatile LONG G3PagingContractCompletions, G3PagingContractLastStatus; };
#define ADMISSION_G3_POISON(Process, File) do { (Process)->Poisoned = TRUE; if (!(Process)->PoisonSite) (Process)->PoisonSite = ((ULONG)(File) << 16) | 1u; } while (0)
static int KeGetCurrentIrql(void){ return PASSIVE_LEVEL; }
static void ExAcquireFastMutex(int*){} static void ExReleaseFastMutex(int*){}
static LONG InterlockedIncrement(volatile LONG *p){ return ++*p; }
static LONG InterlockedExchange(volatile LONG *p, LONG v){ LONG o=*p; *p=v; return o; }
static ADMISSION_G3_PROCESS *AdmissionGpuvaG3FindProcess(ADMISSION_G3_STATE *s, HANDLE h){ return h==s->handle ? s->only : NULL; }
static NTSTATUS inner_status;
static NTSTATUS AdmissionGpuvaG3BuildPagingBuffer(ADMISSION_CONTEXT*, DXGKARG_BUILDPAGINGBUFFER*){ return inner_status; }
@@FUNCS@@
int main(){
  ADMISSION_G3_PROCESS p={0,0}; ADMISSION_G3_STATE st={0,&p,(HANDLE)0x55}; ADMISSION_CONTEXT c={&st,0,0};
  DXGKARG_BUILDPAGINGBUFFER a={DXGK_OPERATION_UPDATE_PAGE_TABLE,{(HANDLE)0x55},{0}};
  NTSTATUS ok[3]={STATUS_SUCCESS,STATUS_GRAPHICS_ALLOCATION_BUSY,STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER};
  for(int i=0;i<3;++i){ inner_status=ok[i]; assert(AdmissionGpuvaG3BuildPagingBufferChecked(&c,&a)==ok[i]); }
  assert(!p.Poisoned && c.G3PagingContractCompletions==0);
  inner_status=STATUS_INVALID_DEVICE_STATE;
  NTSTATUS r=AdmissionGpuvaG3BuildPagingBufferChecked(&c,&a);
  if(r!=STATUS_SUCCESS){printf("disallowed status 0x%lx reached VidMm\n",(unsigned long)r);return 1;}
  assert(p.Poisoned && (p.PoisonSite>>16)==0x26u && c.G3PagingContractCompletions==1);
  /* Destroyed process: still completes. */
  a.UpdatePageTable.hProcess=(HANDLE)0x99; assert(AdmissionGpuvaG3BuildPagingBufferChecked(&c,&a)==STATUS_SUCCESS);
  ADMISSION_G3_PROCESS q={0,0}; st.only=&q; st.handle=(HANDLE)0x77;
  DXGKARG_BUILDPAGINGBUFFER f={DXGK_OPERATION_FLUSH_TLB,{0},{(HANDLE)0x77}}; inner_status=STATUS_INVALID_PARAMETER;
  assert(AdmissionGpuvaG3BuildPagingBufferChecked(&c,&f)==STATUS_SUCCESS && q.Poisoned);
  DXGKARG_BUILDPAGINGBUFFER v={DXGK_OPERATION_VIRTUAL_FILL,{0},{0}};
  assert(AdmissionGpuvaG3BuildPagingBufferChecked(&c,&v)==STATUS_INVALID_PARAMETER);
  puts("EXP1026 paging status contract: PASS");
}
'''


class PagingStatusContract(unittest.TestCase):
    def test_disallowed_status_never_reaches_vidmm(self):
        text = SRC.read_text()
        funcs = function(text, 'AdmissionG3PagingStatusAllowed') + '\n' + \
            function(text, 'AdmissionGpuvaG3BuildPagingBufferChecked')
        self.assertIn('AdmissionG3PagingStatusAllowed', funcs, 'paging status contract is missing')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'; exe = Path(tmp) / 'replay'
            src.write_text(BODY.replace('@@FUNCS@@', funcs))
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP1026 paging status contract: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
