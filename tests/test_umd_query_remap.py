"""EXP1006: remap_canonical re-maps the same allocation at the same VA.

Invariant: the request names the slot's allocation, its canonical VA and the
64 KiB-rounded size of the original mapping, keeps the slot's GPU write and
execute rights, waits a pending paging fence, and fails if VidMm returns a
different VA.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

SRC = Path(__file__).resolve().parents[1] / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'


def function(text, name):
    m = re.search(r'(?m)^static [A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


BODY = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
typedef int HRESULT; typedef unsigned UINT; typedef unsigned D3DKMT_HANDLE; typedef int BOOL;
typedef unsigned long long ULONGLONG;
#define S_OK 0
#define E_PENDING ((HRESULT)0x8000000Au)
#define SUCCEEDED(h) ((h)>=0)
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
enum { AppleAgxWin32BufferGpuWrite = 4u };
enum { AgxWin32BufferClassShader = 2u };
struct PROT { UINT Write:1; UINT Execute:1; };
struct D3DDDI_MAPGPUVIRTUALADDRESS { D3DKMT_HANDLE hPagingQueue; ULONGLONG BaseAddress; D3DKMT_HANDLE hAllocation; ULONGLONG OffsetInPages, SizeInPages; PROT Protection; ULONGLONG VirtualAddress, PagingFenceValue; };
struct SLOT { uint64_t Token; D3DKMT_HANDLE KernelAllocation; uint64_t CanonicalGpuVa, Bytes; UINT Flags, ClassId; };
typedef SLOT ADMISSION_UMD_SCREEN_BUFFER;
struct HANDLE_ { void *handle; };
struct CB { HRESULT (*pfnMapGpuVirtualAddressCb)(void*, D3DDDI_MAPGPUVIRTUALADDRESS*); };
struct ADMISSION_UMD_DEVICE { CB *KernelCallbacks; HANDLE_ RuntimeDevice; D3DKMT_HANDLE PagingQueue; };
static D3DDDI_MAPGPUVIRTUALADDRESS seen; static ULONGLONG returned_va; static int waits;
static HRESULT map(void*, D3DDDI_MAPGPUVIRTUALADDRESS *m){ seen=*m; m->VirtualAddress=returned_va?returned_va:m->BaseAddress; m->PagingFenceValue=5; return E_PENDING; }
static int wait_paging(void*, uint64_t f){ ++waits; return f==5; }
static void va_record(const void*, UINT, ULONGLONG, D3DKMT_HANDLE, ULONGLONG, ULONGLONG, HRESULT) {}
static void AdmissionUmdDiagnostic(const char*, HRESULT, const UINT*, UINT) {}
@@FUNC@@
int main(){
  CB cb={map}; ADMISSION_UMD_DEVICE d={&cb,{(void*)1},3};
  SLOT s={1,0x40,0x1100100000ULL,0x9000,AppleAgxWin32BufferGpuWrite,AgxWin32BufferClassShader};
  assert(remap_canonical(&d,&s)==1 && waits==1);
  assert(seen.hAllocation==0x40 && seen.BaseAddress==0x1100100000ULL && seen.SizeInPages==16 &&
         seen.Protection.Write && seen.Protection.Execute && seen.hPagingQueue==3);
  s.Flags=0; s.ClassId=1; assert(remap_canonical(&d,&s)==1 && !seen.Protection.Write && !seen.Protection.Execute);
  returned_va=0x2000000; assert(remap_canonical(&d,&s)==0);
  puts("EXP1006 query remap: PASS");
}
'''


class QueryRemap(unittest.TestCase):
    def test_remap_request(self):
        func = function(SRC.read_text(), 'remap_canonical')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'; exe = Path(tmp) / 'replay'
            src.write_text(BODY.replace('@@FUNC@@', func))
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-fsanitize=address,undefined',
                                    str(src), '-o', str(exe)], text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP1006 query remap: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
