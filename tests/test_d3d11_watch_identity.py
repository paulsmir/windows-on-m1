"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
DWM uses the D3D11 owner, which never installed the BO identity hook. Verify
installation before native allocation and execute full-width labelled receipts.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_umd_persistent_residency import function, ROOT

class DwmIdentity(unittest.TestCase):
    def test_d3d11_installs_and_emits_watch_identity(self):
        s=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp').read_text()
        create=s[s.index('HRESULT AgxD3d10WindowsCreateDevice('):]
        self.assertIn('AgxWin32BoIdentityHook=AgxD3d10BoIdentity',create)
        self.assertLess(create.index('AgxWin32BoIdentityHook=AgxD3d10BoIdentity'),create.index('HeapAlloc('))
        fn=function(s,'AgxD3d10BoIdentity')
        code=r'''
#include <cassert>
#include <cstring>
using UINT=unsigned;
#define S_OK 0
#define ARRAYSIZE(a) (sizeof(a)/sizeof(a[0]))
static bool enabled;static unsigned calls;static UINT data[16];
static unsigned long long AdmissionUmdWatchVa(){return enabled?1:0;}
static void AdmissionUmdDiagnostic(const char *n,int hr,const UINT *v,unsigned count){assert(!strcmp(n,"measure-watch-bo") && hr==0 && count==16);++calls;memcpy(data,v,sizeof(data));}
'''+fn+r'''
int main(){
 AgxD3d10BoIdentity(1,9,0x123456789aull,0x23456789aull,7,"Helper");assert(!calls);
 enabled=true;
 for(unsigned event=1;event<=3;++event){
 AgxD3d10BoIdentity(event,9,0x123456789aull,0x23456789aull,7,"Helper");
 assert(data[0]==event && data[1]==9 && data[2]==0);
 assert(data[3]==0x3456789a && data[4]==0x12 && data[5]==0x3456789a && data[6]==2 && data[7]==7);
 assert(!strcmp((const char *)&data[8],"Helper"));}
 AgxD3d10BoIdentity(1,1,1,1,0,nullptr);for(unsigned i=8;i<16;++i)assert(!data[i]);
}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.cpp').write_text(code)
            z=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            z=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
