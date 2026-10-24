"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The private CPU-download restore path sets Resident before waiting its paging
fence. A failed/missing fence must stop the device so a retry cannot observe
Resident and return success without waiting. Exercise the production helper.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_umd_persistent_residency import BODY, ROOT, SRC, function

class RestorePagingFailure(unittest.TestCase):
    def test_retry_cannot_skip_failed_restore_wait(self):
        pre=BODY[:BODY.index('@@FUNCS@@')]
        code=pre+r'''
static unsigned missing,waits;
static bool fail_wait;
static HRESULT residency_request(ADMISSION_UMD_DEVICE *,D3DDDI_MAKERESIDENT *r,D3DKMT_HANDLE *,UINT n,UINT *) {
 ++made;r->NumAllocations=n;r->PagingFenceValue=missing?0:7;return E_PENDING;
}
static int wait_paging_at(ADMISSION_UMD_DEVICE *,uint64_t f,UINT,uint64_t){assert(f==7);++waits;return !fail_wait;}
'''+function(SRC.read_text(),'find_slot')+'\n'+function(SRC.read_text(),'restore_residency')+r'''
int main(){
 for(missing=0;missing<2;++missing){
  CB cb={mk,ev};ADMISSION_UMD_DEVICE d={};d.Magic=ADMISSION_UMD_DEVICE_MAGIC;d.KernelCallbacks=&cb;
  auto &s=d.ScreenBuffers[0];s.Active=1;s.Token=10;s.KernelAllocation=0x100;
  made=waits=0;fail_wait=true;
  assert(!restore_residency(&d,10));assert(s.Resident && d.DrawTerminal);
  fail_wait=false;
  assert(!restore_residency(&d,10));assert(made==1 && waits==(missing?0:1));
 }
}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.cpp').write_text(code)
            z=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                '-I'+str(ROOT/'drivers/apple-agx/render-admission/umd/include'),str(p/'test.cpp'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            z=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
