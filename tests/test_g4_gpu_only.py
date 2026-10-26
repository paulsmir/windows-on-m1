"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

A GPU-only General request must cross the real private-data validator without
CPU permissions. Contradictory requests must be rejected, and KMD residency
must never admit aperture fallback. Replays execute production C bodies.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_g4_segment_placement_replay import SHIM, allocation_body

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'drivers/apple-agx'


def run_c(test, source, extra=()):
    with tempfile.TemporaryDirectory(prefix='t7h-') as tmp:
        src, exe = Path(tmp)/'test.c', Path(tmp)/'test'
        src.write_text(source)
        built = subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra',
            '-fsanitize=address,undefined', '-I', str(DRIVER/'render-admission/include'),
            '-I', str(DRIVER/'shared/include'), str(src), *map(str, extra),
            '-o', str(exe)], capture_output=True, text=True)
        test.assertEqual(built.returncode, 0, built.stderr)
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        test.assertEqual(run.returncode, 0, run.stdout+run.stderr)


class GpuOnly(unittest.TestCase):
    def test_private_request_validation(self):
        run_c(self, r'''
#include <assert.h>
#include <string.h>
#include "render_win32_transport.h"
/* The new wire request uses the next free bit, not CPU bits. */
#define GPU_ONLY 0x20u
int main(void) {
  ADMISSION_WIN32_ALLOCATION_CREATE c={0};
  ADMISSION_ALLOCATION_DESCRIPTION out;
  unsigned cls=0, flags=0;
  c.Magic=ADMISSION_WIN32_ALLOCATION_MAGIC;
  c.Version=ADMISSION_WIN32_ALLOCATION_VERSION_LOCAL;
  c.Bytes=sizeof(c);c.ClassId=AgxWin32BufferClassGeneral;
  c.Flags=GPU_ONLY|AppleAgxWin32BufferGpuRead|AppleAgxWin32BufferGpuWrite;
  assert(AdmissionAllocationDescribe(65536,1,1,
      ADMISSION_WIN32_ALLOCATION_STAGING_CPUVISIBLE,
      ADMISSION_WIN32_ALLOCATION_FORMAT_A8,1,&c.Allocation));
  c.Allocation.Type=ADMISSION_WIN32_ALLOCATION_GPU_LOCAL;
  c.Allocation.CpuVisible=0;
  assert(AdmissionWin32AllocationCreateValidate(&c,sizeof(c),&out,&cls,&flags)==AdmissionWin32TransportSuccess);
  assert(!out.CpuVisible && cls==1 && flags==0x2c);
  for(unsigned bit=1;bit<=2;bit<<=1) {
    c.Flags=0x2c|bit;
    assert(AdmissionWin32AllocationCreateValidate(&c,sizeof(c),&out,&cls,&flags)!=AdmissionWin32TransportSuccess);
  }
  c.Flags=GPU_ONLY;
  assert(AdmissionWin32AllocationCreateValidate(&c,sizeof(c),&out,&cls,&flags)!=AdmissionWin32TransportSuccess);
  c.Flags=0x3c;
  assert(AdmissionWin32AllocationCreateValidate(&c,sizeof(c),&out,&cls,&flags)!=AdmissionWin32TransportSuccess);
  c.Flags=0x2c;c.ClassId=AgxWin32BufferClassShader;
  assert(AdmissionWin32AllocationCreateValidate(&c,sizeof(c),&out,&cls,&flags)!=AdmissionWin32TransportSuccess);
  c.ClassId=1;c.Version=ADMISSION_WIN32_ALLOCATION_VERSION;
  c.Allocation.Type=ADMISSION_WIN32_ALLOCATION_STAGING_CPUVISIBLE;c.Allocation.CpuVisible=1;
  assert(AdmissionWin32AllocationCreateValidate(&c,sizeof(c),&out,&cls,&flags)!=AdmissionWin32TransportSuccess);
  c.Flags=0xf;
  assert(AdmissionWin32AllocationCreateValidate(&c,sizeof(c),&out,&cls,&flags)==AdmissionWin32TransportSuccess);
  assert(out.CpuVisible==1 && flags==0xf);
}
''', [DRIVER/'render-admission/src/render_win32_transport.c',
        DRIVER/'render-admission/src/render_allocation.c', DRIVER/'shared/src/apple_agx_win32_abi.c'])

    def test_native_only_and_cpu_visible_placement(self):
        shim=SHIM.replace('unsigned ClassId, CpuVisible;', 'unsigned ClassId, CpuVisible, Flags;')
        shim=shim.replace('*flags = 0;', '*flags = input->Flags;')
        shim += '\n#define AppleAgxWin32BufferGpuOnly 0x20u\n#define AgxWin32BufferClassGeneral 1u\n'
        run_c(self, shim+allocation_body()+r'''
int main(void) {
  const uint64_t sizes[]={65536, 16777216, 16842752, 135266304, 268435456};
  ADMISSION_CONTEXT ctx={1,0};
  for(unsigned pool=0;pool<2;++pool) for(unsigned cpu=0;cpu<2;++cpu)
  for(unsigned n=0;n<sizeof(sizes)/sizeof(sizes[0]);++n) {
    INPUT in={1,cpu,cpu?0xf:0x2c,sizes[n]};
    DXGK_ALLOCATIONINFO info={0};info.pPrivateDriverData=&in;info.PrivateDriverDataSize=sizeof(in);
    DXGKARG_CREATEALLOCATION args={1,&info,0,0,0};
    replay_pool_present=pool;replay_reserved=0;
    assert(AdmissionCreateAllocationImpl(&ctx,&args)==STATUS_SUCCESS);
    assert(info.FlagsWddm2.CpuVisible==cpu && !info.FlagsWddm2.AccessedPhysically);
    assert(info.SupportedReadSegmentSet==(pool?(cpu?5u:4u):(cpu?3u:2u)));
    assert(info.SupportedWriteSegmentSet==info.SupportedReadSegmentSet);
    assert(info.PreferredSegment.SegmentId0==(pool?3u:2u));
    assert(info.PreferredSegment.SegmentId1==(pool && cpu?1u:0u));
    assert(!info.EvictionSegmentSet && replay_reserved==(pool?sizes[n]:0));
    free(info.hAllocation);
  }
  INPUT in={1,0,0x2c,65536};DXGK_ALLOCATIONINFO info={0};
  info.pPrivateDriverData=&in;info.PrivateDriverDataSize=sizeof(in);
  DXGKARG_CREATEALLOCATION args={1,&info,0,0,0};
  replay_pool_present=1;replay_reserve_fail=1;
  assert(AdmissionCreateAllocationImpl(&ctx,&args)==STATUS_NO_MEMORY && !info.hAllocation);
}
''')

    def test_winsys_access_translation(self):
        src=(DRIVER/'mesa/winsys/agx_win32_asahi_bo.c').read_text()
        start=src.index('  cls=(flags&AGX_BO_EXEC)')
        end=src.index('#ifdef APPLE_AGX_GPUVA_WINSYS',start)
        body=src[start:end]
        run_c(self, r'''
#include <assert.h>
#include "apple_agx_win32_device_info.h"
#define AGX_BO_EXEC 2u
#define AGX_BO_LOW_VA 1u
#define AGX_BO_READONLY 8u
#define AGX_WIN32_BO_GPU_ONLY (1u << 29)
static unsigned access_for(unsigned flags) {
 struct { int EncoderAllocationIntent; } backend={0}, *b=&backend;
 unsigned cls,access;
@@BODY@@
 return access;
}
int main(void) {
 assert(access_for(0)==15);
 assert(access_for(AGX_WIN32_BO_GPU_ONLY)==0x2c);
 assert(access_for(AGX_WIN32_BO_GPU_ONLY|AGX_BO_READONLY)==0x24);
 assert(access_for(AGX_BO_LOW_VA)==6);
 assert(access_for(AGX_BO_LOW_VA|AGX_BO_EXEC)==6);
}
'''.replace('@@BODY@@',body))

    def test_direct_bo_map_refuses_before_lock(self):
        from test_umd_deallocated_slot_no_evict import function
        body=function((DRIVER/'mesa/winsys/agx_win32_asahi_bo.c').read_text(),'native_map')
        run_c(self, r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define APPLE_AGX_GPUVA_WINSYS 1
#define AGX_WIN32_BO_GPU_ONLY (1u << 29)
#define AGX_WIN32_BO_SHADOW (1u << 30)
#define AppleAgxWin32BufferCpuWrite 2
#define AgxWin32NativeBoSuccess 0
struct agx_bo { unsigned flags,handle; int refcnt; size_t size; void *_map; };
typedef struct { int Failed; struct { void *Screen; } Buffers; int Gpuva; } AGX_WIN32_ASAHI_BACKEND;
struct agx_device { AGX_WIN32_ASAHI_BACKEND *windows_private; };
struct windows_bo { struct agx_bo Base; AGX_WIN32_ASAHI_BACKEND *Backend; int Virtual,Backing; struct { uint64_t Allocation; } Gpuva; void *Shadow; };
static unsigned locks,receipts,retires;
static char storage[16];
#define AGX_WIN32_ASAHI_FAIL(b,n) ((b)->Failed=(n))
static void report(unsigned token,unsigned flags) { assert(token==42 && flags==AGX_WIN32_BO_GPU_ONLY);++receipts; }
static void (*AgxWin32GpuOnlyMapHook)(unsigned,unsigned)=report;
static uint64_t AgxWin32GpuvaHoldingFence(int *g,uint64_t a) { (void)g;(void)a;++retires;return 0; }
static int AgxWin32GpuvaRetire(int *g,uint64_t f) { (void)g;(void)f;return 1; }
static int AgxWin32NativeBoMap(void *s,int *b,unsigned a,void **p) { (void)s;(void)b;(void)a;++locks;*p=storage;return 0; }
static void *shadow_alloc(size_t n) { (void)n;return storage; }
@@BODY@@
int main(void) {
 AGX_WIN32_ASAHI_BACKEND b={0};struct agx_device d={&b};struct windows_bo bo={0};
 bo.Base.flags=AGX_WIN32_BO_GPU_ONLY;bo.Base.handle=42;bo.Base.refcnt=1;bo.Backend=&b;
 native_map(&d,&bo.Base,NULL);
 assert(!bo.Base._map && !locks && !retires && receipts==1 && !b.Failed);
 bo.Base.flags=0;native_map(&d,&bo.Base,NULL);
 assert(bo.Base._map==storage && locks==1 && retires==1);
}
'''.replace('@@BODY@@',body))


    def test_screen_builds_local_gpu_only_request(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The UMD must send LOCAL/CpuVisible=0 and preserve GPU-only access through the real parser, without changing ordinary SystemDirect."""
        screen=(DRIVER/'render-admission/umd/src/umd_win32_screen.c').read_text()
        a=screen.index('  description.Magic = ADMISSION_WIN32_ALLOCATION_MAGIC;')
        b=screen.index('  ZeroMemory(&allocationInfo',a)
        body=screen[a:b]
        run_c(self,r'''
#include <assert.h>
#include <string.h>
#include "render_win32_transport.h"
#define APPLE_AGX_GPUVA_WINSYS 1
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define FALSE 0
static unsigned heap_staging;
static int make(unsigned Flags,unsigned ClassId,unsigned BorrowedStaging,unsigned Bytes) {
 struct {unsigned HeapStaging, DirectCpuPool;} d={heap_staging,0},*device=&d;
 struct {unsigned MaximumBytes;} c={268435456},*classInfo=&c;
 ADMISSION_WIN32_ALLOCATION_CREATE description={0};ADMISSION_ALLOCATION_DESCRIPTION stagingDescription,out;
 unsigned systemDirect=0,cls=0,flags=0;
 assert(AdmissionAllocationDescribe(Bytes,1,1,ADMISSION_WIN32_ALLOCATION_STAGING_CPUVISIBLE,
      ADMISSION_WIN32_ALLOCATION_FORMAT_A8,1,&description.Allocation));
@@BODY@@
 const unsigned staged=heap_staging && !BorrowedStaging &&
     (Flags&(AppleAgxWin32BufferCpuRead|AppleAgxWin32BufferCpuWrite)) && !(Flags&AppleAgxWin32BufferShareable);
 assert(systemDirect==(!BorrowedStaging && !staged));
 int ok=AdmissionWin32AllocationCreateValidate(&description,sizeof(description),&out,&cls,&flags)==AdmissionWin32TransportSuccess;
 if(ok) {assert(out.CpuVisible==(!(Flags&32) && !staged));assert(flags==Flags && cls==ClassId);}
 return ok;
}
int main(void) {
 assert(make(0x2c,1,0,65536));assert(make(0xf,1,0,65536));
 assert(!make(0x2f,1,0,65536));assert(!make(0x2c,1,7,65536));
 assert(!make(0x20,1,0,65536));assert(!make(0x2c,2,0,65536));
 /* T13 CPU-facing access survives validation while canonical is CpuVisible=0. */
 heap_staging=1;
 assert(make(0xf,1,0,65536));assert(make(0x6,2,0,65536));assert(make(0x6,3,0,65536));
 assert(make(0x2c,1,0,65536));
}
'''.replace('@@BODY@@',body),[DRIVER/'render-admission/src/render_win32_transport.c',
        DRIVER/'render-admission/src/render_allocation.c',DRIVER/'shared/src/apple_agx_win32_abi.c'])
