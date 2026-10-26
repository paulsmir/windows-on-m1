"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

Pinned staging pages must be charged in addition to the native segment's live
promise, never consume its already promised pages, and must unwind exactly on
failed creation. Low-memory/stop and aggregate-limit refusals are fallible.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_apple_agx_pool_headroom import function

ROOT = Path(__file__).resolve().parents[1]

class NativeStagingBudget(unittest.TestCase):
    def test_real_pool_charge(self):
        path = ROOT/'drivers/apple-agx/render-admission/src/pool_windows.c'
        bodies = '\n'.join(function(path.read_text(), n) for n in (
            'AdmissionMemoryPoolChargeNative', 'AdmissionMemoryPoolUnchargeNative'))
        program = r'''
#include <assert.h>
#include <string.h>
typedef unsigned long long ULONGLONG;
typedef int BOOLEAN;
typedef void VOID;
#define TRUE 1
#define FALSE 0
#define _Use_decl_annotations_
#define ADMISSION_NATIVE_STAGING_MAX_BYTES (128u*1024u*1024u)
typedef struct {ULONGLONG NativeStagingBytes,StagedNativeBytes;int Stopping;} ADMISSION_POOL_RUNTIME;
typedef struct {void *PoolDemand;} ADMISSION_CONTEXT;
static int held,may_grow=1;
static void *AdmissionMemoryPoolLock(ADMISSION_CONTEXT *c){if(!c->PoolDemand)return 0;assert(!held);held=1;return c->PoolDemand;}
static void AdmissionMemoryPoolUnlock(ADMISSION_CONTEXT *c){(void)c;assert(held);held=0;}
static int AdmissionPoolMayGrow(void *p){(void)p;assert(held);return may_grow;}
@@BODIES@@
int main(void) {
 ADMISSION_POOL_RUNTIME p={0};ADMISSION_CONTEXT c={&p};
 assert(!AdmissionMemoryPoolChargeNative(&c,0));
 assert(!AdmissionMemoryPoolChargeNative(&c,1));
 assert(AdmissionMemoryPoolChargeNative(&c,128ULL<<20));
 assert(p.NativeStagingBytes==(128ULL<<20));
 may_grow=0;assert(!AdmissionMemoryPoolChargeNative(&c,65536));
 assert(p.NativeStagingBytes==(128ULL<<20));may_grow=1;
 assert(AdmissionMemoryPoolChargeNative(&c,128ULL<<20));
 assert(AdmissionMemoryPoolChargeNative(&c,128ULL<<20));
 assert(AdmissionMemoryPoolChargeNative(&c,128ULL<<20));
 assert(!AdmissionMemoryPoolChargeNative(&c,65536));
 AdmissionMemoryPoolUnchargeNative(&c,128ULL<<20);
 assert(p.NativeStagingBytes==(384ULL<<20));
 p.Stopping=1;assert(!AdmissionMemoryPoolChargeNative(&c,65536));
 AdmissionMemoryPoolUnchargeNative(&c,384ULL<<20);assert(!p.NativeStagingBytes);
 assert(!held);return 0;
}
'''.replace('@@BODIES@@', bodies)
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'budget.c';source.write_text(program)
            exe=Path(tmp)/'budget'
            result=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined',str(source),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
