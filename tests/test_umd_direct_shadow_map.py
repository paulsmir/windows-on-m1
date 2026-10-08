"""EXP1059: CPU maps of Direct (imported presentation) slots use a private shadow.

EXP1058 Settings probe: ApplicationFrameHost faulted (c0000005) in memcpy under
UpdateSubresourceUP. Its swapchain buffer is a Direct slot: GPU-local, no CPU
staging allocation, so the native CPU map was refused and the caller wrote
through a null view. Invariants:
- the first CPU map of a Direct slot allocates one shadow and downloads the
  canonical content into it; later maps reuse the shadow and download again;
- other slots (staged, SystemDirect, unknown) get no shadow and no transfer;
- a shadow allocation failure fails the map instead of exposing no memory;
- a held submission uploads a CPU-mapped Direct shadow and never downloads a
  Direct slot; an unmapped Direct slot is not transferred;
- a present publishes a mapped, unheld shadow; an unmap uploads it;
- the uncache path of transfer_slot never calls UnlockCb for a shadow map.
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
#include <cstdlib>
#include <cstring>
typedef int HRESULT; typedef unsigned UINT; typedef unsigned D3DKMT_HANDLE; typedef int BOOL;
typedef unsigned long long ULONGLONG; typedef unsigned char BYTE; typedef size_t SIZE_T;
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ADMISSION_UMD_SCREEN_BUFFER_LIMIT 8u
#define MEM_COMMIT 1u
#define MEM_RESERVE 2u
#define MEM_RELEASE 4u
#define PAGE_READWRITE 4u
enum { AppleAgxWin32BufferGpuWrite = 4u };
typedef int SRWLOCK;
static void AcquireSRWLockExclusive(SRWLOCK*){} static void ReleaseSRWLockExclusive(SRWLOCK*){}
static void AcquireSRWLockShared(SRWLOCK*){} static void ReleaseSRWLockShared(SRWLOCK*){}
static int allocs, frees, fail_alloc;
static void *VirtualAlloc(void *, SIZE_T bytes, unsigned, unsigned) {
  if (fail_alloc) return nullptr; ++allocs; return calloc(1, bytes); }
static int VirtualFree(void *p, SIZE_T, unsigned) { ++frees; free(p); return 1; }
struct SYNC { BOOL Valid; };
struct SLOT { uint64_t Token; uint64_t Bytes; BOOL SystemDirect,Active,Transition,CopyHeld,Direct,GpuWritten,Mapped,Borrowed,Queried;
  UINT Flags,SubmissionHolds,SourceHolds; D3DKMT_HANDLE KernelAllocation,StagingAllocation; SYNC Sync; uint64_t CanonicalGpuVa;
  BYTE *PrivateStaging; const void *NativeBo; int (*NativeMapRelease)(const void*,const void*,int); };
typedef SLOT ADMISSION_UMD_SCREEN_BUFFER;
struct CB { void *pfnLockCb, *pfnUnlockCb; };
struct ADMISSION_UMD_DEVICE { SLOT ScreenBuffers[8]; SRWLOCK ScreenBufferLock; CB *KernelCallbacks; };
static unsigned downloads[8], uploads[8];
static int touch_device(ADMISSION_UMD_DEVICE*, uint64_t){ return 1; }
static int release(const void*,const void*,int){ return 1; }
/* Upload of a borrowed mapped slot drops the map, as transfer_slot does. */
static int transfer_slot(ADMISSION_UMD_DEVICE *d, SLOT *s, bool download, UINT *, ULONGLONG *) {
  unsigned i=(unsigned)(s-d->ScreenBuffers); if(download) ++downloads[i]; else ++uploads[i];
  if(!download && s->Borrowed && s->Mapped) s->Mapped=FALSE;
  s->Sync.Valid=TRUE; return 1; }
static SLOT *find_slot(ADMISSION_UMD_DEVICE *d,uint64_t t){
  for(unsigned i=0;i<8;++i) if(d->ScreenBuffers[i].Active && d->ScreenBuffers[i].Token==t) return &d->ScreenBuffers[i];
  return nullptr; }
static void AdmissionUmdDiagnostic(const char *, HRESULT, const UINT *, UINT) {}
@@FUNCS@@
int main(){
 CB cb={(void*)1,(void*)1}; ADMISSION_UMD_DEVICE d={}; d.KernelCallbacks=&cb;
 for(unsigned i=0;i<4;++i){SLOT&s=d.ScreenBuffers[i];s.Active=1;s.Token=10+i;s.Bytes=0x30000;
   s.KernelAllocation=0x100+i;s.CanonicalGpuVa=0x1000000ull*(i+1);s.Flags=AppleAgxWin32BufferGpuWrite;
   s.NativeBo=&s;s.NativeMapRelease=release;s.Borrowed=TRUE;}
 SLOT &direct=d.ScreenBuffers[0], &staged=d.ScreenBuffers[1], &system=d.ScreenBuffers[2], &other=d.ScreenBuffers[3];
 direct.Direct=TRUE; staged.StagingAllocation=0x200; system.Direct=TRUE; system.SystemDirect=TRUE;
 other.Direct=TRUE;
 /* First map: one shadow, one download. */
 assert(AdmissionUmdGpuvaPrepareDirectMap(&d,10)==1);
 if(!direct.PrivateStaging || allocs!=1 || downloads[0]!=1){printf("direct map has no shadow\n");return 1;}
 BYTE *shadow=direct.PrivateStaging;
 /* Staged, SystemDirect and unknown slots are not shadowed. */
 assert(AdmissionUmdGpuvaPrepareDirectMap(&d,11)==1 && !staged.PrivateStaging && !downloads[1]);
 assert(AdmissionUmdGpuvaPrepareDirectMap(&d,12)==1 && !system.PrivateStaging && !downloads[2]);
 assert(AdmissionUmdGpuvaPrepareDirectMap(&d,99)==1 && allocs==1);
 /* Allocation failure fails the map. */
 fail_alloc=1; assert(AdmissionUmdGpuvaPrepareDirectMap(&d,13)==0 && !other.PrivateStaging); fail_alloc=0;
 /* Mapped and held: the submission uploads it and drops the map; no download. */
 direct.Mapped=TRUE; direct.CopyHeld=TRUE; direct.GpuWritten=TRUE;
 assert(transfer_held(&d,false)==1);
 if(uploads[0]!=1 || direct.Mapped){printf("held direct shadow not uploaded\n");return 1;}
 assert(transfer_held(&d,true)==1 && downloads[0]==1);
 /* Unmapped held Direct slot: nothing to transfer. */
 assert(transfer_held(&d,false)==1 && uploads[0]==1);
 /* Next map reuses the shadow and fetches the GPU result. */
 assert(AdmissionUmdGpuvaPrepareDirectMap(&d,10)==1 && direct.PrivateStaging==shadow && allocs==1 && downloads[0]==2);
 /* Present publishes a mapped, unheld shadow; a held one waits for its submission. */
 direct.CopyHeld=FALSE; direct.Mapped=TRUE; direct.SubmissionHolds=1;
 assert(AdmissionUmdGpuvaPublishDirectMap(&d,10)==1 && uploads[0]==1);
 direct.SubmissionHolds=0;
 assert(AdmissionUmdGpuvaPublishDirectMap(&d,10)==1 && uploads[0]==2 && !direct.Mapped);
 assert(AdmissionUmdGpuvaPublishDirectMap(&d,10)==1 && uploads[0]==2);
 assert(AdmissionUmdGpuvaPublishDirectMap(&d,11)==1 && !uploads[1]);
 /* Unmap uploads the shadow; slots without one are untouched. */
 assert(AdmissionUmdGpuvaFinishDirectMap(&d,10)==1 && uploads[0]==3);
 assert(AdmissionUmdGpuvaFinishDirectMap(&d,13)==1 && !uploads[3]);
 assert(AdmissionUmdGpuvaFinishDirectMap(&d,12)==1 && !uploads[2]);
 free(direct.PrivateStaging);
 puts("EXP1059 direct shadow map: PASS");
}
'''


class DirectShadowMap(unittest.TestCase):
    def test_direct_slots_map_through_a_shadow(self):
        text = SRC.read_text()
        names = ('direct_shadow_slot', 'cpu_quiet', 'transfer_held',
                 'AdmissionUmdGpuvaPrepareDirectMap', 'AdmissionUmdGpuvaFinishDirectMap',
                 'AdmissionUmdGpuvaPublishDirectMap')
        parts = [function(text, n) for n in names]
        for name, part in zip(names, parts):
            self.assertTrue(part, f'{name} is missing')
        funcs = '\n'.join(parts).replace('auto *slot', 'SLOT *slot')
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
            self.assertIn('EXP1059 direct shadow map: PASS', ran.stdout)

    def test_shadow_uncache_skips_unlock(self):
        body = function(SRC.read_text(), 'transfer_slot')
        unlock = body.index('pfnUnlockCb(device->RuntimeDevice.handle,&unlock)')
        guard = body.rfind('address==slot->PrivateStaging', 0, unlock)
        self.assertGreater(guard, body.index('bool uncache='),
                           'a shadow map must not be unlocked through VidMm')


if __name__ == '__main__':
    unittest.main()
