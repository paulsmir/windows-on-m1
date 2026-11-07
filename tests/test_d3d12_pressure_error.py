"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A refused residency/native-backing request is currently converted to DEVICE_HUNG
by the D3D12 owner. Preserve the actual callback error, without calling accepted
but uncertain work recoverable OOM. No claim about runtime per-DDI OOM policy.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_umd_persistent_residency import function
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
class PressureError(unittest.TestCase):
    def test_owner_keeps_failure_cause_and_uncertainty(self):
        fn = function(SOURCE.read_text(), 'owner_failure')
        code = r'''
#include <cassert>
using HRESULT=int;
#define FAILED(h) ((h)<0)
#define E_OUTOFMEMORY -2
#define DXGI_ERROR_DEVICE_HUNG -3
struct AGX_D3D12_OWNER { struct {HRESULT GpuvaFailure;bool DrawTerminal;} Runtime; };
''' + fn + r'''
int main() {
 AGX_D3D12_OWNER o={};
 assert(owner_failure(&o,DXGI_ERROR_DEVICE_HUNG)==DXGI_ERROR_DEVICE_HUNG);
 o.Runtime.GpuvaFailure=E_OUTOFMEMORY;
 assert(owner_failure(&o,DXGI_ERROR_DEVICE_HUNG)==E_OUTOFMEMORY);
 o.Runtime.GpuvaFailure=-4; // already removed: never mask as OOM
 assert(owner_failure(&o,E_OUTOFMEMORY)==-4);
 o.Runtime.GpuvaFailure=E_OUTOFMEMORY;o.Runtime.DrawTerminal=true;
 assert(owner_failure(&o,DXGI_ERROR_DEVICE_HUNG)==DXGI_ERROR_DEVICE_HUNG);
}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'t.cpp').write_text(code)
            r=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'t')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(p/'t')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
    def test_fault_consumers_do_not_invent_hang(self):
        s=SOURCE.read_text()
        self.assertNotIn('if (AgxWin32AsahiContextFaulted(pipe)) return DXGI_ERROR_DEVICE_HUNG;',s)
        self.assertIn('? S_OK : owner_failure(owner, DXGI_ERROR_DEVICE_HUNG);',s)
