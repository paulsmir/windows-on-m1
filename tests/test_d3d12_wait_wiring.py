"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Diagnostic instrumentation must preserve the runtime CPU-map branch contract.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d12_wait_profile import ROOT

class WaitWiring(unittest.TestCase):
    def test_private_staging_does_not_call_runtime_lock(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A timer inserted after an unbraced else must not make LockCb unconditional.
        Compile/replay the actual private-staging branch under the x86 C++ mode.
        """
        s=(ROOT/'drivers/apple-agx/render-admission/umd/src/umd_win32_screen.c').read_text()
        start=s.index('#ifdef APPLE_AGX_GPUVA_WINSYS\n    if (privateStaging)')
        end=s.index('    {\n      UINT values[2]',start)
        code='''
#include <cassert>
#include "agx_win32_wait.h"
#define S_OK 0
static unsigned calls,starts,ends;
unsigned AdmissionUmdWaitBegin(unsigned){++starts;return 1;}
void AdmissionUmdWaitEnd(unsigned){++ends;}
struct Lock {void *pData;};
static int cb(void *,Lock *p){++calls;p->pData=(void *)22;return 7;}
struct Callbacks {int (*pfnLockCb)(void *,Lock *);};
struct Device {Callbacks *KernelCallbacks;struct {void *handle;}RuntimeDevice;};
static void test(void *privateStaging){
 Lock lock={};int result=-1;Callbacks callbacks{cb};Device d{&callbacks,{}};Device *device=&d;
'''+s[start:end]+'''
 if(privateStaging){assert(result==0 && lock.pData==privateStaging);}
 else{assert(result==7 && lock.pData==(void*)22);}
}
int main(){test((void*)11);assert(!calls && !starts && !ends);
 test(nullptr);assert(calls==1 && starts==1 && ends==1);}
'''
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/'map.cpp';src.write_text(code);exe=Path(d)/'map'
            run=subprocess.run(['clang++','-std=c++17','-DAPPLE_AGX_GPUVA_WINSYS',
                '-I'+str(ROOT/'drivers/apple-agx/mesa/winsys'),str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)


if __name__=='__main__': unittest.main()
