"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
WDK MakeResident.NumAllocations reports accepted allocations on partial failure.
If their paging fence has not been waited, releasing holds and keeping Resident
true permits a later submit to bypass paging. Return uncertain ownership and
retain the entire submission until teardown, as the winsys requires.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_umd_persistent_residency import BODY, ROOT, SRC, function
class PartialResidency(unittest.TestCase):
    def test_partial_acceptance_is_terminal_and_retained(self):
        funcs='\n'.join(function(SRC.read_text(),n) for n in (
            'watch_slots','find_slot','release_copies','allocation_handle','translate_handles',
            'trim_residency_with','trim_residency','residency_request','make_resident'))
        code=BODY[:BODY.index('@@FUNCS@@')]+funcs+r'''
static HRESULT partial(HANDLE,D3DDDI_MAKERESIDENT*r){assert(r->NumAllocations==2);r->NumAllocations=1;r->PagingFenceValue=7;return E_FAIL;}
int main(){
 CB cb={partial,ev};ADMISSION_UMD_DEVICE d={};d.Magic=ADMISSION_UMD_DEVICE_MAGIC;
 d.PagingQueue=(HANDLE)1;d.KernelCallbacks=&cb;
 for(unsigned i=0;i<2;++i){auto &s=d.ScreenBuffers[i];s.Active=1;s.Token=10+i;s.KernelAllocation=0x100+i;
  s.StagingAllocation=0x200+i;s.CanonicalGpuVa=0x10000*(i+1);s.Bytes=65536;}
 uint64_t tokens[]={10,11},fence=0;
 assert(make_resident(&d,tokens,2,&fence)==3);
 assert(d.DrawTerminal && d.ScreenBuffers[0].Resident && !d.ScreenBuffers[1].Resident);
 for(unsigned i=0;i<2;++i) assert(d.ScreenBuffers[i].CopyHeld && d.ScreenBuffers[i].SubmissionHolds==1);
 assert(!make_resident(&d,tokens,2,&fence));assert(!evicted);
}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.cpp').write_text(code)
            z=subprocess.run(['clang++','-std=c++17', '-include', str(ROOT/'tests/d3d12_wait_disabled_fixture.h'),'-fsanitize=address,undefined','-fno-sanitize-recover=all',
                 '-I'+str(ROOT/'drivers/apple-agx/render-admission/umd/include'),str(p/'test.cpp'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            z=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
