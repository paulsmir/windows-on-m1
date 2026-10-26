"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

Native-ring allocations must carry a complete versioned token/range, retain
ordinary CPU/GPU access rules, and reject truncated, wrapped or foreign-form
requests. This compiles and executes the production allocation decoder.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
D = ROOT / 'drivers/apple-agx'

class NativeStagingAllocation(unittest.TestCase):
    def test_real_kmd_placement_and_no_duplicate_promise(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        The KMD must authenticate a native slice and expose only its original
        system backing, without charging a second segment-3 allocation. This
        executes the real CreateAllocation body with WDK/allocator boundaries.
        """
        from test_g4_segment_placement_replay import SHIM, allocation_body
        shim = SHIM.replace('ULONGLONG PoolReserved;', 'ULONGLONG PoolReserved;void *NativeStagingBinding;')
        shim = shim.replace('unsigned Value, CpuVisible, AccessedPhysically;',
                            'unsigned Value, CpuVisible, AccessedPhysically, ExistingSysMem;')
        shim = shim.replace('assert(bytes == sizeof(*input));',
                            'assert(bytes == sizeof(*input) || bytes == sizeof(*input)+16);')
        extra = r'''
#define APPLE_AGX_NATIVE_STAGING 1
typedef struct { INPUT Create; ULONGLONG Token, Offset; } ADMISSION_NATIVE_STAGING_ALLOCATION;
static int bind_calls;
static int AdmissionNativeStagingBind(ADMISSION_CONTEXT *c, ULONGLONG token,
    ULONGLONG offset,ULONGLONG bytes,void **out) {
 (void)c;++bind_calls;assert(offset==65536 && bytes==65536);
 if(token!=7)return 0;*out=(void*)1;return 1;
}
'''
        main = r'''
int main(void) {
 ADMISSION_CONTEXT context={1,NULL,0,0};
 ADMISSION_NATIVE_STAGING_ALLOCATION n={{1,1,65536},7,65536};
 DXGK_ALLOCATIONINFO info={0};DXGKARG_CREATEALLOCATION args={0};
 args.NumAllocations=1;args.pAllocationInfo=&info;
 info.pPrivateDriverData=&n;info.PrivateDriverDataSize=sizeof(n);
 replay_pool_present=1;replay_reserve_fail=1;
 assert(AdmissionCreateAllocationImpl(&context,&args)==STATUS_SUCCESS);
 assert(bind_calls==1 && !replay_reserved);
 assert(info.SupportedWriteSegmentSet==1 && info.SupportedReadSegmentSet==1);
 assert(info.PreferredSegment.SegmentId0==1 && !info.EvictionSegmentSet);
 assert(info.FlagsWddm2.ExistingSysMem && info.FlagsWddm2.CpuVisible);
 assert(!info.FlagsWddm2.AccessedPhysically);
 assert(!((ADMISSION_ALLOCATION_HANDLE*)info.hAllocation)->PoolReserved);
 free(info.hAllocation);info.hAllocation=NULL;n.Token=8;
 assert(AdmissionCreateAllocationImpl(&context,&args)==STATUS_INVALID_PARAMETER);
 assert(!info.hAllocation && !replay_reserved);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'kmd.c';source.write_text(shim+extra+allocation_body()+main)
            exe=Path(tmp)/'kmd'
            result=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined',str(source),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_decoder(self):
        source = r'''
#include <assert.h>
#include <string.h>
#include "render_win32_transport.h"
int main(void) {
  ADMISSION_NATIVE_STAGING_ALLOCATION c = {0};
  ADMISSION_ALLOCATION_DESCRIPTION d;
  unsigned cls, flags;
  c.Create.Magic = ADMISSION_WIN32_ALLOCATION_MAGIC;
  c.Create.Version = ADMISSION_WIN32_ALLOCATION_VERSION_NATIVE;
  c.Create.Bytes = sizeof(c);
  c.Create.ClassId = 1; c.Create.Flags = 15;
  c.Token = 7; c.Offset = 65536;
  assert(AdmissionAllocationDescribe(65536, 1, 1,
      ADMISSION_WIN32_ALLOCATION_STAGING_CPUVISIBLE,
      ADMISSION_WIN32_ALLOCATION_FORMAT_A8, 1, &c.Create.Allocation));
#define VALID(n) (AdmissionWin32AllocationCreateValidateEx(&c, (n), &d, &cls, &flags, 256ULL<<20) == AdmissionWin32TransportSuccess)
  assert(VALID(sizeof(c)) && cls == 1 && flags == 15 && d.CpuVisible);
  assert(!VALID(sizeof(c)-1));
  c.Token = 0; assert(!VALID(sizeof(c))); c.Token = 7;
  c.Offset = ~0ULL - 65535; assert(!VALID(sizeof(c)));
  c.Offset = 1; assert(!VALID(sizeof(c))); c.Offset = 65536;
  c.Create.Reserved[0] = 1; assert(!VALID(sizeof(c))); c.Create.Reserved[0] = 0;
  c.Create.Version = 1; assert(!VALID(sizeof(c)));
  c.Create.Version = ADMISSION_WIN32_ALLOCATION_VERSION_NATIVE;
  c.Create.Allocation.CpuVisible = 0; assert(!VALID(sizeof(c))); c.Create.Allocation.CpuVisible = 1;
  c.Create.Flags = AppleAgxWin32BufferGpuOnly | AppleAgxWin32BufferGpuRead;
  assert(!VALID(sizeof(c)));
  c.Create.Flags = AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead;
  for (unsigned k=1;k<=3;++k) {c.Create.ClassId=k;assert(VALID(sizeof(c)));}
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'allocation.c'
            path.write_text(source)
            exe = Path(tmp) / 'allocation'
            result = subprocess.run([os.environ.get('CC', 'cc'), '-std=c11',
                '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-I', str(D/'render-admission/include'), '-I', str(D/'shared/include'),
                str(path), str(D/'render-admission/src/render_allocation.c'),
                str(D/'render-admission/src/render_win32_transport.c'),
                str(D/'shared/src/apple_agx_win32_abi.c'), '-o', str(exe)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
