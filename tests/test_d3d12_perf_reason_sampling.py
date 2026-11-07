"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The process-wide 4096-row cap permanently hid steady-state synchronization
reasons. Sampling must resume after 64 presents and remain bounded between
samples, with the accumulated counts intact.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class PerfSampling(unittest.TestCase):
    def test_reasons_survive_startup_cap(self):
        source=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        section=source[source.index('static SRWLOCK owner_perf_lock'):source.index('/* C85')]
        prelude=r'''
#include <cstring>
#include <cassert>
using UINT=unsigned;using LONG=long;using SRWLOCK=int;
#define SRWLOCK_INIT 0
#define S_OK 0
#define ARRAYSIZE(x) (sizeof(x)/sizeof(x[0]))
static unsigned rows,last_count;
static void AcquireSRWLockExclusive(SRWLOCK*){}
static void ReleaseSRWLockExclusive(SRWLOCK*){}
static void AdmissionUmdDiagnostic(const char *,int,const UINT *w,unsigned n){assert(n==14);++rows;last_count=w[1];}
'''
        main=r'''
int main(){
 owner_perf_lines=4096;owner_presents=100;owner_perf_reported_presents=100;
 for(unsigned i=0;i<512;i++)owner_perf_note("Sync writer due to: GPU read staging blit");
 assert(rows==0);
 owner_presents=164;owner_perf_note("Sync writer due to: GPU read staging blit");
 assert(rows==1 && last_count==513);
 for(unsigned i=0;i<512;i++)owner_perf_note("Sync writer due to: GPU read staging blit");
 assert(rows==1);
 owner_presents=228;owner_perf_note("Sync writer due to: GPU read staging blit");
 assert(rows==2 && last_count==513);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/'sample.cpp';src.write_text(prelude+section+main)
            run=subprocess.run(['clang++','-std=c++17',str(src),'-o',str(Path(d)/'sample')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            run=subprocess.run([str(Path(d)/'sample')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)

if __name__=='__main__':unittest.main()
