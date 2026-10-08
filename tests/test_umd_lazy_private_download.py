"""EXP995: GPU-written private slots without a CPU mapping sync lazily.

EXP994 DWM profile (251 s): 3.16 GB of downloads of seven never-mapped
2560x1600 private render targets (193 downloads, ~320 ms each) and ~50 s of
per-submit staging hashes of the same slots. Their staging can change only
through a CPU map, so it is read or written by nobody until such a map exists.
Invariants:
- a written slot that is neither mapped nor borrowed (shared) is not downloaded
  after the submission; its GpuWritten mark stays pending;
- such a slot with a valid staging record is not re-inspected for upload
  (staging cannot have changed), so a pending GPU result is never clobbered;
- mapped or borrowed slots keep the eager download;
- the first CPU map of a slot with a pending mark downloads it first.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'


def function(text, name):
    m = re.search(r'(?:static )?[A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
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
#include <cstring>
typedef int HRESULT; typedef unsigned UINT; typedef unsigned D3DKMT_HANDLE; typedef int BOOL;
typedef unsigned long long ULONGLONG; typedef long long LONGLONG;
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#define E_FAIL ((HRESULT)0x80004005u)
#define ADMISSION_UMD_SCREEN_BUFFER_LIMIT 16u
enum { AppleAgxWin32BufferGpuWrite = 4u };
typedef int SRWLOCK;
static void AcquireSRWLockExclusive(SRWLOCK*){} static void ReleaseSRWLockExclusive(SRWLOCK*){}
static void AcquireSRWLockShared(SRWLOCK*){} static void ReleaseSRWLockShared(SRWLOCK*){}
struct SYNC { BOOL Valid; };
struct SLOT { uint64_t Token; uint64_t Bytes; BOOL SystemDirect,Active,Transition,CopyHeld,Direct,GpuWritten,Mapped,Borrowed,Queried;
  UINT Flags,SubmissionHolds; D3DKMT_HANDLE KernelAllocation; SYNC Sync; uint64_t CanonicalGpuVa;  unsigned char *PrivateStaging; D3DKMT_HANDLE StagingAllocation; const void *NativeBo; int (*NativeMapRelease)(const void*,const void*,int); };
static int touches;
typedef SLOT ADMISSION_UMD_SCREEN_BUFFER;
struct CB { void *pfnLockCb, *pfnUnlockCb; };
struct ADMISSION_UMD_DEVICE { SLOT ScreenBuffers[16]; SRWLOCK ScreenBufferLock; CB *KernelCallbacks; };
static unsigned downloads[16], uploads[16];
static int touch_device(ADMISSION_UMD_DEVICE*, uint64_t){ ++touches; return 1; }
static int transfer_slot(ADMISSION_UMD_DEVICE *d, SLOT *s, bool download, UINT *, ULONGLONG *) {
  unsigned i=(unsigned)(s-d->ScreenBuffers); if(download) ++downloads[i]; else ++uploads[i];
  s->Sync.Valid=TRUE; return 1; }
static SLOT *find_slot(ADMISSION_UMD_DEVICE *d,uint64_t t){
  for(unsigned i=0;i<16;++i) if(d->ScreenBuffers[i].Active && d->ScreenBuffers[i].Token==t) return &d->ScreenBuffers[i];
  return nullptr; }
static void AdmissionUmdDiagnostic(const char *, HRESULT, const UINT *, UINT) {}
@@FUNCS@@
int main(){
 CB cb={(void*)1,(void*)1}; ADMISSION_UMD_DEVICE d={}; d.KernelCallbacks=&cb;
 for(unsigned i=0;i<3;++i){SLOT&s=d.ScreenBuffers[i];s.Active=1;s.CopyHeld=1;s.Token=10+i;
   s.KernelAllocation=0x100+i;s.Flags=AppleAgxWin32BufferGpuWrite;s.Sync.Valid=TRUE;}
 d.ScreenBuffers[1].Mapped=TRUE; d.ScreenBuffers[2].Borrowed=TRUE;
 uint64_t w[3]={10,11,12};
 assert(mark_written(&d,w,3)==1);
 assert(transfer_held(&d,true)==1);
 if(downloads[0]){printf("unmapped private slot downloaded eagerly\n");return 1;}
 assert(downloads[1]==1 && downloads[2]==1);
 assert(d.ScreenBuffers[0].GpuWritten && !d.ScreenBuffers[1].GpuWritten && !d.ScreenBuffers[2].GpuWritten);
 /* Next submission: unchanged private staging is not inspected or uploaded. */
 assert(transfer_held(&d,false)==1);
 if(uploads[0]){printf("unmapped private staging re-inspected\n");return 1;}
 assert(uploads[1]==1 && uploads[2]==1);
 /* A private slot never synchronized (fresh) is still uploaded once. */
 d.ScreenBuffers[0].Sync.Valid=FALSE; d.ScreenBuffers[0].GpuWritten=FALSE;
 assert(transfer_held(&d,false)==1 && uploads[0]==1);
 /* First CPU map downloads a pending GPU result, exactly once. */
 d.ScreenBuffers[0].GpuWritten=TRUE;
 assert(AdmissionUmdGpuvaPrepareCpuMap(&d,10)==1 && downloads[0]==1 && !d.ScreenBuffers[0].GpuWritten);
 assert(AdmissionUmdGpuvaPrepareCpuMap(&d,10)==1 && downloads[0]==1);
 /* Mapped or unknown tokens need no work here. */
 assert(AdmissionUmdGpuvaPrepareCpuMap(&d,11)==1 && downloads[1]==1);
 assert(AdmissionUmdGpuvaPrepareCpuMap(&d,99)==1);
 puts("EXP995 lazy private download: PASS");
}
'''


class LazyPrivateDownload(unittest.TestCase):
    def test_unmapped_private_slots_sync_lazily(self):
        text = SRC.read_text()
        funcs = '\n'.join(function(text, n) for n in
                          ('cpu_quiet', 'direct_shadow_slot', 'mark_written', 'transfer_held',
                           'AdmissionUmdGpuvaPrepareCpuMap'))
        self.assertIn('AdmissionUmdGpuvaPrepareCpuMap', funcs,
                      'map-time download of pending GPU results is missing')
        funcs = funcs.replace('auto *slot', 'SLOT *slot')
        funcs = re.sub(r'#if defined\(APPLE_AGX_EXP907_FRAME_RECEIPT\).*?#endif\n', '', funcs, flags=re.S)
        body = BODY.replace('@@FUNCS@@', funcs)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'; exe = Path(tmp) / 'replay'
            src.write_text(body)
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wno-unused-function',
                                    '-Wno-unused-variable', '-fsanitize=address,undefined',
                                    str(src), '-o', str(exe)], text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP995 lazy private download: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
