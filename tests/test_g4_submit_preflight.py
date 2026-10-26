"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Malformed native envelopes must be refused on the fallible UMD escape path
before scheduling a DMA; no fence, signal, dispatch, or accepted residency set
may be fabricated. KMD tests use the production parser/graph replay.
"""
from pathlib import Path
import unittest
import os
import subprocess
import tempfile
import test_g4_native_allocation_guard as guard
from test_umd_async_render_completion import function


class SubmitPreflight(unittest.TestCase):
    def test_wire_layout_is_identical_for_c_cpp_and_windows_abis(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The escape's
        embedded 64-bit addresses and metadata must have the same fixed layout
        in x86 /TP, x64, ARM64 and KMD C, with no pointer-sized wire fields.
        """
        code='''#include "apple_agx_g4_preflight.h"
#define CHECK(c) typedef char check_##__LINE__[(c)?1:-1]
CHECK(sizeof(APPLE_AGX_G4_PREFLIGHT)==4384);
CHECK(__builtin_offsetof(APPLE_AGX_G4_PREFLIGHT,CommandVa)==16);
CHECK(__builtin_offsetof(APPLE_AGX_G4_PREFLIGHT,Failure)==40);
CHECK(__builtin_offsetof(APPLE_AGX_G4_PREFLIGHT,Private)==72);
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/'wire.c';p.write_text(code)
            for arch in ('i686','x86_64','aarch64'):
                for lang in ('c','c++'):
                    r=subprocess.run(['clang','-target',arch+'-pc-windows-msvc',
                        '-x',lang,'-fsyntax-only','-Werror','-I'+str(guard.SHARED/'include'),str(p)],capture_output=True,text=True)
                    self.assertEqual(r.returncode,0,r.stderr)

    def test_umd_rejects_without_scheduling_or_advancing_fences(self):
        source = Path(os.environ.get('RC57_UMD_SOURCE',guard.UMD)).read_text()
        self.assertIn('static int g4_preflight(', source)
        fixture = guard.UMD_FIXTURE[:guard.UMD_FIXTURE.index('int main() {')]
        fixture = fixture.replace('#include "apple_agx_g3_copy_abi.h"',
            '#include "apple_agx_g3_copy_abi.h"\n'
            '#pragma clang diagnostic ignored "-Wmissing-field-initializers"\n'
            '#include "apple_agx_g4_preflight.h"')
        fixture = fixture.replace('static HRESULT escape_cb(',
            'static unsigned preflights, refuse_preflight;\nstatic HRESULT escape_cb(')
        at = fixture.index('auto *b=', fixture.index('static HRESULT escape_cb('))
        fixture = fixture[:at] + '''if (*(unsigned *)e->pPrivateDriverData == APPLE_AGX_G4_PREFLIGHT_MAGIC) {
 ++preflights;auto *q=(APPLE_AGX_G4_PREFLIGHT *)e->pPrivateDriverData;
 assert(q->CommandVa==0x10000 && q->CommandBytes==256 && q->PrivateBytes==4);
 if(refuse_preflight==2)return E_FAIL;
 q->Ready=refuse_preflight?0:1;q->Status=refuse_preflight?0xc000000du:0;
 return S_OK;
 }
 ''' + fixture[at:]
        fixture=fixture.replace(function(fixture,'g4_preflight'),'')
        fixture=fixture.replace('finish_submission(ADMISSION_UMD_DEVICE *,uint64_t n,UINT,uint64_t *f) {*f=n;return 1;}',
            'finish_submission(ADMISSION_UMD_DEVICE *d,uint64_t n,UINT,uint64_t *f) {d->NextRenderFence=n;*f=n;return 1;}')
        funcs = '\n'.join(function(source,n) for n in ('g4_preflight','native_held_ready','submit'))
        fixture=fixture.replace('@@FUNCS@@',funcs)
        fixture+=r'''
int main() {
 (void)hold;(void)g4_preflight;
 CALLBACKS cbks={cb,signal_cb,escape_cb};ADAPTER a={ADMISSION_UMD_ADAPTER_MAGIC,{9}};
 for(unsigned failure=0;failure<3;++failure) {
  ADMISSION_UMD_DEVICE d={};d.Adapter=&a;d.KernelCallbacks=&cbks;d.KernelContext=7;
  d.RuntimeDevice.handle=3;d.RenderSyncObject=8;d.NextRenderFence=11;
  for(unsigned i=0;i<3;++i){auto &s=d.ScreenBuffers[i];s.Active=s.CopyHeld=true;
   s.KernelAllocation=i+1;s.CanonicalGpuVa=0x10000ull*(i+1);s.Bytes=65536;s.SubmissionHolds=1;}
  refuse_preflight=failure;preflights=submits=signals=0;bad=0;
  uint64_t fence=0;unsigned data=0;
  assert(submit(&d,nullptr,0,0x10000,256,&data,4,&fence)==(failure?0:1));
  assert(preflights==1 && submits==(failure?0u:1u) && signals==(failure?0u:1u));
  assert(d.NextRenderFence==(failure?11u:12u) && fence==(failure?0u:12u));
  assert(!allocations);
  if(failure)assert(d.GpuvaFailure==E_FAIL);
 }
}
'''
        guard.NativeAllocationGuard().compile_run(fixture,cpp=True)


if __name__=='__main__':unittest.main()
