"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

Pinned staging pages must be charged in addition to the native segment's live
promise, never consume its already promised pages, and must unwind exactly on
failed creation. Low-memory/stop and aggregate-limit refusals are fallible.

RC62 (EXP1984): every native staging BO is an aperture-only ExistingSysMem
allocation. The aggregate charge must therefore stay within a quarter of the
aperture segment's CommitLimit (64 MiB for the 256 MiB aperture). The old 512
MiB cap admitted three 128 MiB arenas, and MakeResident then failed against
the 256 MiB NON_LOCAL budget. A missing aperture limit refuses every charge.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def native_section(text):
    """The production #ifdef APPLE_AGX_NATIVE_STAGING block of pool_windows.c."""
    anchor = text.index('BOOLEAN AdmissionMemoryPoolChargeNative(')
    start = text.rindex('#ifdef APPLE_AGX_NATIVE_STAGING', 0, anchor)
    end = text.index('#endif', anchor)
    return text[start + len('#ifdef APPLE_AGX_NATIVE_STAGING'):end]


class NativeStagingBudget(unittest.TestCase):
    def test_real_pool_charge(self):
        path = ROOT/'drivers/apple-agx/render-admission/src/pool_windows.c'
        bodies = native_section(path.read_text())
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
#include "render_win32_transport_budget.h"
typedef struct {ULONGLONG NativeStagingBytes,StagedNativeBytes;int Stopping;} ADMISSION_POOL_RUNTIME;
typedef struct {ULONGLONG CommitLimit;} SEGMENT;
typedef struct {void *PoolDemand;struct {struct {SEGMENT Aperture;} Topology;} Memory;} ADMISSION_CONTEXT;
static int held,may_grow=1;
static void *AdmissionMemoryPoolLock(ADMISSION_CONTEXT *c){if(!c->PoolDemand)return 0;assert(!held);held=1;return c->PoolDemand;}
static void AdmissionMemoryPoolUnlock(ADMISSION_CONTEXT *c){(void)c;assert(held);held=0;}
static int AdmissionPoolMayGrow(void *p){(void)p;assert(held);return may_grow;}
@@BODIES@@
int main(void) {
 ADMISSION_POOL_RUNTIME p={0};ADMISSION_CONTEXT c={&p,{{{256ULL<<20}}}};
 assert(!AdmissionMemoryPoolChargeNative(&c,0));
 assert(!AdmissionMemoryPoolChargeNative(&c,1));
 /* One old 128 MiB arena is already half of the aperture. */
 assert(!AdmissionMemoryPoolChargeNative(&c,128ULL<<20));
 assert(!p.NativeStagingBytes);
 assert(AdmissionMemoryPoolChargeNative(&c,32ULL<<20));
 assert(p.NativeStagingBytes==(32ULL<<20));
 may_grow=0;assert(!AdmissionMemoryPoolChargeNative(&c,65536));
 assert(p.NativeStagingBytes==(32ULL<<20));may_grow=1;
 assert(AdmissionMemoryPoolChargeNative(&c,16ULL<<20));
 assert(AdmissionMemoryPoolChargeNative(&c,16ULL<<20));
 assert(p.NativeStagingBytes==(64ULL<<20));
 assert(!AdmissionMemoryPoolChargeNative(&c,65536));
 AdmissionMemoryPoolUnchargeNative(&c,16ULL<<20);
 assert(p.NativeStagingBytes==(48ULL<<20));
 assert(AdmissionMemoryPoolChargeNative(&c,16ULL<<20));
 assert(!AdmissionMemoryPoolChargeNative(&c,65536));
 /* A smaller aperture lowers the cap to a quarter of its CommitLimit. */
 AdmissionMemoryPoolUnchargeNative(&c,64ULL<<20);assert(!p.NativeStagingBytes);
 c.Memory.Topology.Aperture.CommitLimit=128ULL<<20;
 assert(AdmissionMemoryPoolChargeNative(&c,32ULL<<20));
 assert(!AdmissionMemoryPoolChargeNative(&c,65536));
 AdmissionMemoryPoolUnchargeNative(&c,32ULL<<20);
 c.Memory.Topology.Aperture.CommitLimit=0;
 assert(!AdmissionMemoryPoolChargeNative(&c,65536));
 c.Memory.Topology.Aperture.CommitLimit=256ULL<<20;
 assert(AdmissionMemoryPoolChargeNative(&c,65536));
 p.Stopping=1;assert(!AdmissionMemoryPoolChargeNative(&c,65536));
 AdmissionMemoryPoolUnchargeNative(&c,65536);assert(!p.NativeStagingBytes);
 assert(!held);return 0;
}
'''.replace('@@BODIES@@', bodies)
        header = ROOT/'drivers/apple-agx/render-admission/include/render_win32_transport.h'
        budget = '\n'.join(line for line in header.read_text().splitlines()
                           if line.startswith('#define ADMISSION_NATIVE_STAGING_BUDGET_BYTES') or
                           line.startswith('#define ADMISSION_NATIVE_STAGING_APERTURE_SHARE'))
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, 'render_win32_transport_budget.h').write_text(budget + '\n')
            source=Path(tmp)/'budget.c';source.write_text(program)
            exe=Path(tmp)/'budget'
            result=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-I',tmp,str(source),'-o',str(exe)],
                capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__ == '__main__':
    unittest.main()
