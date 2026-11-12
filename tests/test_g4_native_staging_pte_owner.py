"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

RC56 read NativeStagingBinding from every non-NULL UpdatePageTable hAllocation.
Every other reader in gpuva_g3_paging_windows.c (NoteSystemShape, PoolPlace,
the omitted-leaf ring) first proves alignment and ADMISSION_ALLOCATION_OBJECT_MAGIC
before touching extended metadata; a handle that is not our allocation object
must neither be dereferenced past the magic nor rejected (which poisons the
whole process via AdmissionGpuvaG3BuildPagingBufferChecked, site 0x26).
Microsoft documents hAllocation as the KMD CreateAllocation handle (NULL for
page tables), so this is a fail-closed hardening invariant, not the proven
EXP1979 cause. Execute the production binding reader
(AdmissionG3NativeStagingBinding) with ASan/UBSan on the real allocation
layout. RC60 moved the page rule to publication; its strictness (wrong
segment, offset or PFN never becomes a native leaf) is covered by
test_g4_native_staging_leaf.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class NativeStagingPteOwner(unittest.TestCase):
    def test_real_leaf_binding_validation(self):
        paging=(ROOT/'drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c').read_text()
        start=paging.index('static PVOID AdmissionG3NativeStagingBinding(')
        block=paging[start:paging.index('\n}\n',start)+3]
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
typedef long long LONGLONG;
typedef unsigned ULONG, UINT;
typedef void *PVOID, *HANDLE;
#define APPLE_AGX_NATIVE_STAGING 1
#include "render_allocation.h"
#include "render_pool_demand.h"
@@ALLOCATION@@
@@BLOCK@@
int main(void) {
 /* Only the identity prefix is ours to inspect. Reading the extension is
  * an ASan failure on this opaque allocation, not a mock-status assertion. */
 unsigned *opaque=(unsigned*)malloc(sizeof(*opaque));assert(opaque);*opaque=0;
 assert(AdmissionG3NativeStagingBinding(opaque)==NULL);free(opaque);
 ADMISSION_ALLOCATION_HANDLE own;memset(&own,0,sizeof(own));
 own.NativeStagingBinding=(void*)1;
 assert(AdmissionG3NativeStagingBinding(&own)==NULL);
 own.Object.Magic=ADMISSION_ALLOCATION_OBJECT_MAGIC;
 assert(AdmissionG3NativeStagingBinding(&own)==(void*)1);
 assert(AdmissionG3NativeStagingBinding((char*)&own+1)==NULL);
 assert(AdmissionG3NativeStagingBinding(NULL)==NULL);
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
