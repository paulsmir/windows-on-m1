"""EXP985: only slots a submission actually wrote are downloaded to staging.

EXP984: a consumer (DWM) holds an opened shared CPU-visible surface whose
private canonical copy it only samples. Downloading every held slot with the
GpuWrite capability after each submission wrote that stale canonical back into
the shared staging and overwrote the producer's content (black desktop) and cost
3.7 MB per DWM frame (EXP983R). Invariant: a slot is downloaded after a
submission only if that submission named it as written; the mark is consumed by
the download.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'


def function(text, name):
    m = re.search(r'static [A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
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
#define E_FAIL ((HRESULT)0x80004005u)
#define ADMISSION_UMD_SCREEN_BUFFER_LIMIT 16u
#define ADMISSION_UMD_DEVICE_MAGIC 0x55u
enum { AppleAgxWin32BufferGpuWrite = 4u };
typedef int SRWLOCK;
static void AcquireSRWLockExclusive(SRWLOCK*){} static void ReleaseSRWLockExclusive(SRWLOCK*){}
static void AcquireSRWLockShared(SRWLOCK*){} static void ReleaseSRWLockShared(SRWLOCK*){}
struct LARGE_INTEGER { LONGLONG QuadPart; };
struct SYNC { BOOL Valid; };
struct SLOT { uint64_t Token; BOOL SystemDirect,Active,Transition,CopyHeld,Direct,GpuWritten,Mapped,Borrowed,Queried; UINT Flags; D3DKMT_HANDLE KernelAllocation; SYNC Sync; uint64_t CanonicalGpuVa; };
static int touches;
typedef SLOT ADMISSION_UMD_SCREEN_BUFFER;
struct CB { void *pfnLockCb, *pfnUnlockCb; };
struct ADMISSION_UMD_DEVICE { UINT Magic; SLOT ScreenBuffers[16]; SRWLOCK ScreenBufferLock; CB *KernelCallbacks; };
static unsigned downloads[16], uploads[16];
static int touch_device(ADMISSION_UMD_DEVICE*, uint64_t){ ++touches; return 1; }
static int transfer_slot(ADMISSION_UMD_DEVICE *d, SLOT *s, bool download, UINT *, ULONGLONG *) {
  unsigned i=(unsigned)(s-d->ScreenBuffers); if(download) ++downloads[i]; else ++uploads[i]; return 1; }
@@FUNCS@@
int main(){
 CB cb={(void*)1,(void*)1}; ADMISSION_UMD_DEVICE d={}; d.Magic=ADMISSION_UMD_DEVICE_MAGIC; d.KernelCallbacks=&cb;
 for(unsigned i=0;i<3;++i){SLOT&s=d.ScreenBuffers[i];s.Active=1;s.CopyHeld=1;s.Token=10+i;s.KernelAllocation=0x100+i;s.Flags=AppleAgxWin32BufferGpuWrite;s.Mapped=TRUE;}
 /* Held, GpuWrite-capable, but never written by this submission: no download. */
 assert(transfer_held(&d,true)==1);
 if(downloads[0]||downloads[1]||downloads[2]){printf("held-but-unwritten slots downloaded: %u %u %u\n",downloads[0],downloads[1],downloads[2]);return 1;}
 /* Uploads still consider every held slot (hash-gated in transfer_slot). */
 assert(transfer_held(&d,false)==1 && uploads[0]==1 && uploads[1]==1 && uploads[2]==1);
 /* The submission writes only token 11: only slot 1 is downloaded, once. */
 uint64_t w[1]={11};
 assert(mark_written(&d,w,1)==1);
 assert(transfer_held(&d,true)==1 && downloads[0]==0 && downloads[1]==1 && downloads[2]==0);
 assert(transfer_held(&d,true)==1 && downloads[1]==1);
 /* An unknown written token is refused. */
 uint64_t bad[1]={99}; assert(mark_written(&d,bad,1)==0);
 puts("EXP985 written-only download: PASS");
}
'''


class WrittenOnlyDownload(unittest.TestCase):
    def test_only_written_slots_are_downloaded(self):
        text = SRC.read_text()
        funcs = function(text, 'cpu_quiet') + '\n' + function(text, 'mark_written') + '\n' + function(text, 'transfer_held')
        if 'mark_written' not in funcs:
            funcs += '\nstatic int mark_written(ADMISSION_UMD_DEVICE*,const uint64_t*,unsigned){return 1;}\n'
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
            self.assertIn('EXP985 written-only download: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
