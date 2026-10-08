"""EXP1003: schedule the device once before copying into a never-queried slot.

EXP1001 leaf ring: an explorer mapping's last KMD update stayed the invalid
one VidMm wrote at Map time (hAllocation NULL); MakeResident's paging fence
completed 33 s before the failing copy QUERY, yet no valid UpdatePageTable
followed. VidMm populates the PTEs only once the device is scheduled
(Residency overview), so the UMD submits an empty touch first.
Invariants:
- an upload pass with a held, never-queried slot issues exactly one touch;
- an upload pass of queried slots, a download pass, and unchanged private
  slots issue none;
- a failed QUERY is retried once after a touch; success marks the slot queried.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'
KMD = ROOT / 'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c'
QUEUE = ROOT / 'drivers/apple-agx/render-admission/src/work_queue_windows.c'


def function(text, name):
    m = re.search(r'(?m)^static [A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


BODY = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
typedef int HRESULT; typedef unsigned UINT; typedef unsigned D3DKMT_HANDLE; typedef int BOOL;
typedef unsigned long long ULONGLONG; typedef long long LONGLONG;
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#define E_FAIL ((HRESULT)0x80004005u)
#define ADMISSION_UMD_SCREEN_BUFFER_LIMIT 16u
#define APPLE_AGX_G3_COPY_QUERY 0u
enum { AppleAgxWin32BufferGpuWrite = 4u };
typedef int SRWLOCK;
static void AcquireSRWLockExclusive(SRWLOCK*){} static void ReleaseSRWLockExclusive(SRWLOCK*){}
struct LARGE_INTEGER { LONGLONG QuadPart; };
struct SYNC { BOOL Valid; };
struct SLOT { uint64_t Token; uint64_t Bytes; BOOL SystemDirect,Active,Transition,CopyHeld,Direct,GpuWritten,Mapped,Borrowed,Queried;
  UINT Flags; D3DKMT_HANDLE KernelAllocation; SYNC Sync; uint64_t CanonicalGpuVa;  unsigned char *PrivateStaging; D3DKMT_HANDLE StagingAllocation; const void *NativeBo; int (*NativeMapRelease)(const void*,const void*,int); };
typedef SLOT ADMISSION_UMD_SCREEN_BUFFER;
struct CB { void *pfnLockCb, *pfnUnlockCb; };
struct ADMISSION_UMD_DEVICE { SLOT ScreenBuffers[16]; SRWLOCK ScreenBufferLock; CB *KernelCallbacks; };
struct APPLE_AGX_G3_COPY_REQUEST { D3DKMT_HANDLE Allocation; UINT Operation; ULONGLONG Offset; UINT TransferBytes; ULONGLONG ProcessGeneration, MappingGeneration; };
static int touches, queries, query_ok_after, remaps;
static int cycles;
static int replace_canonical(ADMISSION_UMD_DEVICE*, SLOT *s){ assert(s->CanonicalGpuVa); ++remaps; ++cycles; return 1; }
static int touch_device(ADMISSION_UMD_DEVICE*, uint64_t va){ assert(va); ++touches; return 1; }
static int copy_escape(ADMISSION_UMD_DEVICE*, APPLE_AGX_G3_COPY_REQUEST *q){
  ++queries; if(queries>query_ok_after){q->ProcessGeneration=q->MappingGeneration=1;return 1;} return 0; }
static int transfer_slot(ADMISSION_UMD_DEVICE*, SLOT*, bool, UINT*, ULONGLONG*) { return 1; }
static void AdmissionUmdDiagnostic(const char *, HRESULT, const UINT *, UINT) {}
@@FUNCS@@
int main(){
  CB cb={(void*)1,(void*)1}; ADMISSION_UMD_DEVICE d={}; d.KernelCallbacks=&cb;
  for(unsigned i=0;i<3;++i){SLOT&s=d.ScreenBuffers[i];s.Active=s.CopyHeld=s.Mapped=1;s.Token=10+i;
    s.CanonicalGpuVa=0x10000*(i+1);s.Flags=AppleAgxWin32BufferGpuWrite;s.Queried=1;s.Sync.Valid=1;}
  assert(transfer_held(&d,false)==1 && touches==0);
  d.ScreenBuffers[2].Queried=0;
  assert(transfer_held(&d,false)==1);
  if(touches!=1){printf("expected one touch before first copy, got %d\n",touches);return 1;}
  touches=0; assert(transfer_held(&d,true)==1 && touches==0);
  /* An unmapped private slot with a valid record is skipped and needs no touch. */
  d.ScreenBuffers[2].Mapped=0; touches=0; assert(transfer_held(&d,false)==1 && touches==0);
  /* EXP1006: QUERY failure -> one remap of the same VA, one retry. */
  APPLE_AGX_G3_COPY_REQUEST q={}; SLOT &s=d.ScreenBuffers[1]; s.Queried=0;
  remaps=0; queries=0; query_ok_after=1;
  assert(query_canonical(&d,&s,&q) && queries==2 && remaps==1 && cycles==1 && s.Queried);
  remaps=0; queries=0; query_ok_after=5; s.Queried=0;
  assert(!query_canonical(&d,&s,&q) && queries==2 && remaps==1 && !s.Queried);
  puts("EXP1003 residency touch: PASS");
}
'''


class ResidencyTouch(unittest.TestCase):
    def test_touch_policy(self):
        text = SRC.read_text()
        funcs = '\n'.join(function(text, n) for n in ('cpu_quiet', 'direct_shadow_slot', 'query_canonical', 'transfer_held'))
        self.assertIn('query_canonical', funcs, 'touch before first copy is missing')
        funcs = funcs.replace('auto *slot', 'SLOT *slot')
        funcs = re.sub(r'#if defined\(APPLE_AGX_EXP907_FRAME_RECEIPT\).*?#endif\n', '', funcs, flags=re.S)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'; exe = Path(tmp) / 'replay'
            src.write_text(BODY.replace('@@FUNCS@@', funcs))
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP1003 residency touch: PASS', ran.stdout)

    def test_kmd_routes_touch_to_cpu_queue(self):
        kmd = KMD.read_text()
        inner = kmd[kmd.index('static NTSTATUS AdmissionDdiSubmitCommandVirtualInner('):]
        self.assertLess(inner.index('AppleAgxG4IsTouch('), inner.index('AdmissionG4SubmitVirtualEnvelope('))
        touch = function(kmd, 'AdmissionG4SubmitTouch')
        self.assertIn('ADMISSION_CPU_PACKET_NOP, NULL, 0u', touch)
        queue = QUEUE.read_text()
        self.assertIn('Kind == ADMISSION_CPU_PACKET_NOP ? (Data != NULL || Bytes != 0u)', queue)


if __name__ == '__main__':
    unittest.main()
