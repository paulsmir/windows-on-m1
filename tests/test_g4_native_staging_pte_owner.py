"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

RC56 read NativeStagingBinding from every non-NULL UpdatePageTable hAllocation.
Every other reader in gpuva_g3_paging_windows.c (NoteSystemShape, PoolPlace,
the omitted-leaf ring) first proves alignment and ADMISSION_ALLOCATION_OBJECT_MAGIC
before touching extended metadata; a handle that is not our allocation object
must neither be dereferenced past the magic nor rejected (which poisons the
whole process via AdmissionGpuvaG3BuildPagingBufferChecked, site 0x26).
Microsoft documents hAllocation as the KMD CreateAllocation handle (NULL for
page tables), so this is a fail-closed hardening invariant, not the proven
EXP1979 cause. Execute the production leaf-validation block with ASan/UBSan,
including real native owners whose wrong segment, offset or PFN must still be
rejected (validation stays strict).
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_umd_staging_write_watch import function

ROOT=Path(__file__).resolve().parents[1]

class NativeStagingPteOwner(unittest.TestCase):
    def test_real_leaf_binding_validation(self):
        leaf=function((ROOT/'drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c').read_text(),
                      'AdmissionG3UpdateLeaf')
        block=leaf.split('#ifdef APPLE_AGX_NATIVE_STAGING\n',1)[1].split('#endif',1)[0]
        header=(ROOT/'drivers/apple-agx/render-admission/include/render_admission.h').read_text()
        allocation=header.split('typedef struct _ADMISSION_ALLOCATION_HANDLE {',1)[1].split('} ADMISSION_ALLOCATION_HANDLE;',1)[0]
        allocation='typedef struct _ADMISSION_ALLOCATION_HANDLE {'+allocation+'} ADMISSION_ALLOCATION_HANDLE;'
        code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uintptr_t ULONG_PTR;
typedef unsigned long long ULONGLONG;
typedef unsigned ULONG, UINT;
typedef void *PVOID;
#define APPLE_AGX_NATIVE_STAGING 1
#include "render_allocation.h"
#include "render_pool_demand.h"
@@ALLOCATION@@
static int page_calls;
static int AdmissionNativeStagingPage(void *binding,ULONGLONG offset,ULONGLONG ipa) {
 ++page_calls;
 return binding==(void*)1 && offset<65536 && ipa==0x900000000ULL+offset;
}
static int validate(void *owner,unsigned segment,ULONGLONG offset,ULONGLONG ipa) {
 struct {void *hAllocation;} input={owner},*update=&input;
 struct {unsigned Segment;} page={segment},*pte=&page;
 struct {ULONGLONG AllocationOffset,GuestIpa;} result={offset,ipa},*out=&result;
 @@BLOCK@@
 return 1;
Done:return 0;
}
int main(void) {
 /* Only the identity prefix is ours to inspect. Reading the extension is
  * an ASan failure on this opaque allocation, not a mock-status assertion. */
 unsigned *opaque=(unsigned*)malloc(sizeof(*opaque));assert(opaque);*opaque=0;
 assert(validate(opaque,2,0,0x900000000ULL));free(opaque);
 assert(!page_calls);
 ADMISSION_ALLOCATION_HANDLE own;memset(&own,0,sizeof(own));
 own.Object.Magic=ADMISSION_ALLOCATION_OBJECT_MAGIC;
 assert(validate(&own,3,0,0x900000000ULL));assert(!page_calls);
 own.NativeStagingBinding=(void*)1;
 assert(validate(&own,0,0,0x900000000ULL));
 assert(validate(&own,0,61440,0x90000f000ULL));
 assert(!validate(&own,3,0,0x900000000ULL));
 assert(!validate(&own,0,65536,0x900010000ULL));
 assert(!validate(&own,0,0,0x900001000ULL));
 assert(validate(NULL,2,0,0x900000000ULL));
 return 0;
}
'''.replace('@@BLOCK@@',block).replace('@@ALLOCATION@@',allocation)
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'pte.c';src.write_text(code)
            for lang,std in [('c','c11'),('c++','c++17')]:
                exe=Path(tmp)/std
                built=subprocess.run(['clang','-x',lang,'-std='+std,'-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-I',str(ROOT/'drivers/apple-agx/render-admission/include'),str(src),'-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(built.returncode,0,built.stdout+built.stderr)
                run=subprocess.run([str(exe)],capture_output=True,text=True)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
