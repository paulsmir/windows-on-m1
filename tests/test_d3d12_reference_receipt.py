"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The first ref violation (16 words) must fit AdmissionUmdDiagnostic's wire limit (32 since ab2ea6db);
otherwise both requested lines silently disappear. Preserve list/slot/root,
64-bit VA/allocation/object/Execute, length, and registration path.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d12_owner_allocation_failure import function
ROOT=Path(__file__).resolve().parents[1]
W=ROOT/'drivers/apple-agx/mesa/winsys'
class ReferenceReceipt(unittest.TestCase):
    def test_receipt_fits_real_sink(self):
        sink=(ROOT/'drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c').read_text()
        self.assertIn('Count > 32u',sink)  # ab2ea6db: cap 16 -> 32
        code=r'''
#include "agx_win32_gpuva.h"
#include <cassert>
#include <cstring>
using UINT=unsigned;
#define E_FAIL (-1)
#define ARRAYSIZE(x) (sizeof(x)/sizeof(x[0]))
static unsigned calls,words[16];
static void AdmissionUmdDiagnostic(const char *,int,const UINT *v,unsigned n){
 if(n>16)return;assert(n==16);++calls;memcpy(words,v,n*sizeof(UINT));
}
'''+function((W/'agx_d3d12_windows.cpp').read_text(),'static void owner_ref_violation(')+r'''
int main(){AGX_WIN32_REF r={};r.Va=0x123456789ull;r.Allocation=0xabcdef012ull;
 r.Execute=0x555666777ull;r.Object=0x8899aabbccull;r.Bytes=0x100000011ull;
 r.Kind=3;r.Stage=5;r.Slot=7;r.Root=9;r.List=4;r.Command=13;r.Path=2;
 owner_ref_violation(nullptr,&r,AgxRefMissing);assert(calls==2);
 assert(words[0]==(1u|(AgxRefMissing<<8)|(3u<<16)|(5u<<24)));
 assert(words[1]==7 && words[2]==9 && words[3]==4 && words[4]==13);
 assert(words[5]==(UINT)r.Execute && words[6]==(UINT)(r.Execute>>32));
 assert(words[9]==(UINT)r.Va && words[10]==(UINT)(r.Va>>32));
 assert(words[13]==(UINT)r.Allocation && words[14]==(UINT)(r.Allocation>>32));
 assert(words[15]==2);
}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.cpp').write_text(code)
            z=subprocess.run(['clang++','-std=c++17','-I'+str(W),'-I'+str(ROOT/'drivers/apple-agx/shared/include'),str(p/'test.cpp'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            z=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
