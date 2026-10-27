"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

CPU-facing D3D12 BOs must pass the exact native slice as pSystemMem, avoid a
second CPU shadow/local promise, and preserve ordinary fallback and failed
allocation ownership. Execute the real UMD allocation body and wire decoder.
RC66 (EXP1986): a Shader-class BO is written by the CPU once and must not take
a native slice that a per-batch query/pool/encoder BO needs; it keeps the
ordinary path (reason 6) even with native space free. A full arena (reason 1/5)
reports which classes hold the native slices.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_umd_cpu_pool_allocation import HARNESS, DRIVER
from test_umd_staging_write_watch import function

class NativeStagingRoute(unittest.TestCase):
    def test_real_allocation_route(self):
        harness=HARNESS[:HARNESS.index('int main()')]
        harness=harness.replace('#define APPLE_AGX_GPUVA_WINSYS 1',
            '#define APPLE_AGX_GPUVA_WINSYS 1\n#define APPLE_AGX_NATIVE_STAGING 1\n#include "umd_native_arena.h"')
        harness=harness.replace('struct D3DDDI_ALLOCATIONINFO {',
            'struct D3DDDI_ALLOCATIONINFO {void *pSystemMem;')
        harness=harness.replace('BYTE *PrivateStaging;',
            'BYTE *PrivateStaging;BYTE *NativeStagingAddress;unsigned long long NativeStagingTicket,NativeStagingToken;')
        harness=harness.replace('static bool fail_allocate,fail_private,fail_release;',
            'static bool fail_allocate,fail_private,fail_release,fail_native;static unsigned release_failures_left;')
        harness=harness.replace('return fail_release?E_FAIL:S_OK;',
            'if(release_failures_left){--release_failures_left;return E_FAIL;}return fail_release?E_FAIL:S_OK;')
        harness=harness.replace('r->pAllocationInfo->hAllocation=42;return fail_allocate?E_OUTOFMEMORY:S_OK;',
            'bool native=r->pAllocationInfo->PrivateDriverDataSize==sizeof(ADMISSION_NATIVE_STAGING_ALLOCATION);'
            'assert((r->pAllocationInfo->pSystemMem!=nullptr)==native);'
            'r->pAllocationInfo->hAllocation=42;return (fail_allocate || (native && fail_native))?E_OUTOFMEMORY:S_OK;')
        helpers=r'''
using ULONGLONG=unsigned long long;
#define MemoryBarrier() ((void)0)
#define AgxWaitAllocate 0u
static unsigned AdmissionUmdWaitBegin(unsigned){return 0;}
static void AdmissionUmdWaitEnd(unsigned){}
static ADMISSION_UMD_STAGING_RING arena;
static bool native_enabled;
static unsigned fallback_count,fallback_reason,reserve_calls;
static BYTE native_memory[3 * 65536];
static bool AdmissionUmdNativeStagingReserve(ADMISSION_UMD_DEVICE *d,unsigned long long n,
    unsigned long long align,ADMISSION_UMD_RING_SLICE *s,BYTE **p,unsigned long long *token) {
 assert(d->ScreenBuffers[0].Active && d->ScreenBuffers[0].Transition);
 ++reserve_calls;
 if(!native_enabled)return false;
 if(!AdmissionUmdRingReserveOwned(&arena,(unsigned)n,(unsigned)align,s))return false;
 *p=native_memory+s->Offset;*token=99;return true;
}
static bool AdmissionUmdNativeStagingRelease(ADMISSION_UMD_DEVICE *d,unsigned long long token,unsigned long long ticket,
    unsigned long long fence) {assert(d->ScreenBuffers[0].Active && d->ScreenBuffers[0].Transition);
    assert(token==99);return AdmissionUmdRingReleaseOwned(&arena,ticket,fence,arena.Completed);}
static void AdmissionUmdNativeStagingFallback(ADMISSION_UMD_DEVICE*,unsigned r,unsigned long long,HRESULT){++fallback_count;fallback_reason=r;}
static_assert(ADMISSION_UMD_NATIVE_SHADER_CLASS==AgxWin32BufferClassShader,"native class policy");
'''
        body=function((DRIVER/'render-admission/umd/src/umd_win32_screen.c').read_text(),
                      'AdmissionUmdScreenCreateClassBufferImpl')
        harness=harness.replace('@@FUNCTION@@',helpers+body)
        harness+=r'''
int main() {
 CB cb={allocate,deallocate};ADMISSION_UMD_DEVICE d={};
 d.Magic=ADMISSION_UMD_DEVICE_MAGIC;d.KernelCallbacks=&cb;d.HeapStaging=1;
 APPLE_AGX_U64 token=0;native_enabled=true;
 assert(AdmissionUmdRingInit(&arena,65536));
 memset(native_memory,0xcc,sizeof(native_memory));
 assert(AdmissionUmdScreenCreateClassBufferImpl(&d,1,65536,65536,15,&token,0));
 auto &s=d.ScreenBuffers[0];
 assert(s.NativeStagingAddress==native_memory && s.NativeStagingTicket);
 assert(s.SystemDirect && !s.PrivateStaging && !private_allocs && parsed.CpuVisible);
 for(unsigned i=0;i<65536;++i)assert(native_memory[i]==0);
 assert(arena.Used==65536);

 /* Legal caller sizes need complete native pages in the encoded allocation,
  * ring ownership and zero-on-reuse, including the padding bytes. The real
  * KMD decoder above must admit every request without an ordinary fallback. */
 d.ScreenBuffers[0]={};
 assert(AdmissionUmdRingInit(&arena,sizeof(native_memory)));
 for(unsigned bytes : {16384u, 32768u, 81920u}) {
  d.ScreenBuffers[0]={};memset(native_memory,0xcc,sizeof(native_memory));
  assert(AdmissionUmdScreenCreateClassBufferImpl(&d,1,bytes,65536,15,&token,0));
  unsigned extent=bytes==81920u ? 131072u : 65536u;
  assert(s.NativeStagingAddress && !s.PrivateStaging && s.Bytes==bytes);
  assert(parsed.Size==extent && arena.Used==extent);
  for(unsigned i=0;i<extent;++i)assert(native_memory[i]==0);
  assert(native_memory[extent]==0xcc);
  assert(AdmissionUmdRingReleaseOwned(&arena,s.NativeStagingTicket,0,0));
 }
 /* RC66: native space is free, yet a Shader-class BO (CPU writes its
  * binary once) keeps write-watched private staging: no reservation. */
 d.ScreenBuffers[0]={};private_allocs=0;fallback_count=reserve_calls=0;
 assert(AdmissionUmdScreenCreateClassBufferImpl(&d,AgxWin32BufferClassShader,65536,65536,0x6,&token,0));
 assert(!s.NativeStagingAddress && !s.NativeStagingTicket && s.PrivateStaging && s.HeapStaging);
 assert(!s.SystemDirect && !arena.Used && !reserve_calls && private_allocs==1);
 assert(fallback_count==1 && fallback_reason==6u);
 VirtualFree(s.PrivateStaging,0,0);
 /* The Encoder class (rewritten every batch) still takes the native slice. */
 d.ScreenBuffers[0]={};private_allocs=0;fallback_count=reserve_calls=0;
 assert(AdmissionUmdScreenCreateClassBufferImpl(&d,AgxWin32BufferClassEncoder,65536,65536,0x6,&token,0));
 assert(s.NativeStagingAddress && !s.PrivateStaging && reserve_calls==1 && !fallback_count);
 assert(AdmissionUmdRingReleaseOwned(&arena,s.NativeStagingTicket,0,0));
 d.ScreenBuffers[0]={}; native_enabled=false;private_allocs=0;
 assert(AdmissionUmdScreenCreateClassBufferImpl(&d,1,65536,65536,15,&token,0));
 assert(!s.NativeStagingAddress && !s.NativeStagingTicket && s.PrivateStaging);
 VirtualFree(s.PrivateStaging,0,0);assert(fallback_count && fallback_reason==1u);
 /* RC62: a BO over the native slice limit keeps the ordinary path and says
  * so (reason 4) instead of reporting a full arena. */
 d.ScreenBuffers[0]={};private_allocs=0;
 assert(AdmissionUmdScreenCreateClassBufferImpl(&d,1,ADMISSION_UMD_NATIVE_SLICE_LIMIT+65536ull,65536,15,&token,0));
 assert(!s.NativeStagingAddress && s.PrivateStaging && fallback_reason==4u);
 VirtualFree(s.PrivateStaging,0,0);
 /* RC64: a refused slice over 1 MiB names the exhausted large-slice share
  * (reason 5), separately from a full small arena set (reason 1). */
 d.ScreenBuffers[0]={};private_allocs=0;
 assert(AdmissionUmdScreenCreateClassBufferImpl(&d,1,ADMISSION_UMD_NATIVE_SMALL_SLICE+65536ull,65536,15,&token,0));
 assert(!s.NativeStagingAddress && s.PrivateStaging && fallback_reason==5u);
 VirtualFree(s.PrivateStaging,0,0);
 d.ScreenBuffers[0]={};private_allocs=0;
 assert(AdmissionUmdScreenCreateClassBufferImpl(&d,1,ADMISSION_UMD_NATIVE_SMALL_SLICE,65536,15,&token,0));
 assert(!s.NativeStagingAddress && s.PrivateStaging && fallback_reason==1u);
 VirtualFree(s.PrivateStaging,0,0);
 /* Native AllocateCb fails after returning a handle: clean rollback permits
  * ordinary staging, while failed rollback retains the original slice. */
 d.ScreenBuffers[0]={};native_enabled=true;fail_native=true;
 assert(AdmissionUmdRingInit(&arena,65536));
 allocs=frees=private_allocs=fallback_count=0;
 assert(AdmissionUmdScreenCreateClassBufferImpl(&d,1,65536,65536,15,&token,0));
 assert(allocs==2 && frees==1 && !arena.Used && s.PrivateStaging);
 assert(!s.NativeStagingAddress && !s.NativeStagingTicket && fallback_count==1);
 VirtualFree(s.PrivateStaging,0,0);
 d.ScreenBuffers[0]={};release_failures_left=1;allocs=frees=private_allocs=0;
 assert(!AdmissionUmdScreenCreateClassBufferImpl(&d,1,65536,65536,15,&token,0));
 assert(!d.DrawTerminal && !arena.Used && !s.Active && frees==2);
 d.ScreenBuffers[0]={};fail_release=true;allocs=frees=private_allocs=0;
 assert(!AdmissionUmdScreenCreateClassBufferImpl(&d,1,65536,65536,15,&token,0));
 assert(allocs==1 && d.DrawTerminal && arena.Used==65536);
 assert(s.NativeStagingTicket && s.KernelAllocation==42 && !private_allocs);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp);source=tmp/'route.cpp';source.write_text(harness)
            includes=['-I',str(DRIVER/'render-admission/include'),'-I',str(DRIVER/'shared/include'),
                '-I',str(DRIVER/'render-admission/umd/include')]
            objects=[]
            for rel in ('render-admission/src/render_allocation.c',
                        'render-admission/src/render_win32_transport.c','shared/src/apple_agx_win32_abi.c'):
                obj=tmp/(Path(rel).stem+'.o')
                subprocess.run(['cc','-std=c11',*includes,'-c',str(DRIVER/rel),'-o',str(obj)],check=True)
                objects.append(str(obj))
            exe=tmp/'route'
            result=subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                '-Wno-unused-function','-fsanitize=address,undefined',*includes,str(source),*objects,
                '-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
