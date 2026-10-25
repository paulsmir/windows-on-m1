"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP1939's successful invalidation loses the old allocation token: the incoming
hAllocation is NULL and the logical PTEs are zeroed before fault capture.
Execute the production successful-commit fragment and observer, retaining exact
old-owner runs without fabricating live ownership or changing mapping contents.
This is an observability regression, not a reproduction of the GPU fault.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import g3_vidmm_replay as replay
from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
SHARED = ROOT / 'drivers/apple-agx/shared'

PROGRAM = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "apple_agx_gpuva_g3_translation.h"
typedef void VOID;
typedef unsigned ULONG, UINT;
typedef unsigned long long ULONGLONG;
#define RtlZeroMemory(p,n) memset(p,0,n)
#define RtlCopyMemory(p,q,n) memcpy(p,q,n)
#define STATUS_SUCCESS 0
@@TYPES@@
typedef struct { ADMISSION_G3_LEAF_HISTORY LeafHistory[ADMISSION_G3_LEAF_RING];
  ULONG LeafHistoryNext; } ADMISSION_G3_STATE;
typedef struct { ADMISSION_G3_STATE *State;
  struct { ULONGLONG ProcessId, MappingGeneration; } Graph; } ADMISSION_G3_PROCESS;
typedef struct { APPLE_AGX_GPUVA_G3_LOGICAL_PTE *LogicalPtes,*ResidentPtes; } ADMISSION_G3_TABLE_SHADOW;
typedef struct { ULONGLONG FirstPteVirtualAddress; } DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE;
typedef struct { ULONGLONG QuadPart; } LARGE_INTEGER;
static LARGE_INTEGER KeQueryPerformanceCounter(void *unused) {
  (void)unused; return (LARGE_INTEGER){99};
}
@@OBSERVER@@
static void commit(ADMISSION_G3_PROCESS *process, ULONGLONG table_ipa,
    const DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *update,
    ADMISSION_G3_TABLE_SHADOW *shadow, UINT first, UINT count) {
  (void)process; (void)table_ipa; (void)update;
  @@COMMIT@@
}
static ADMISSION_G3_STATE state;
static APPLE_AGX_GPUVA_G3_LOGICAL_PTE old[8192], next[8192], saved[8192];
static void reset(void) { memset(&state,0,sizeof(state));memset(old,0,sizeof(old));memset(next,0,sizeof(next)); }
static void populate(unsigned first,unsigned count,ULONGLONG token,unsigned seg) {
  for(unsigned i=first;i<first+count;++i)
    old[i]=(APPLE_AGX_GPUVA_G3_LOGICAL_PTE){0xa00000000ULL+i*4096,seg,3,token,i*4096,0};
}
static void check(unsigned n,unsigned first,unsigned count,ULONGLONG token,unsigned seg,
                  ULONGLONG va) {
  ADMISSION_G3_LEAF_HISTORY *h=&state.LeafHistory[n%ADMISSION_G3_LEAF_RING];
  assert(h->Flags==0x40 && h->Status==0 && h->Allocation==token);
  assert(h->ProcessId==11 && h->TableIpa==0x900004000ULL);
  assert(h->First==first && h->Count==count && h->ValidCount==count);
  assert(h->FirstVa==va && h->FirstSegment==seg && h->Qpc==99);
  assert(h->MappingGeneration==77);
}
int main(void) {
  ADMISSION_G3_PROCESS p={&state,{11,77}};
  ADMISSION_G3_TABLE_SHADOW shadow={old,next};
  DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE update={0x1113ba0000ULL};
  /* EXP1939 shape is a regression input, never an admission rule. */
  reset();populate(7072,16,0xabc,3);
  commit(&p,0x900004000ULL,&update,&shadow,7072,16);
  assert(state.LeafHistoryNext==1);
  check(0,7072,16,0xabc,3,update.FirstPteVirtualAddress);
  assert(!memcmp(old,next,sizeof(old)));
  /* No-op update, valid replacement and already-invalid entries are not unmaps. */
  reset();populate(5,9,0x123,0);memcpy(next,old,sizeof(old));
  next[6].Allocation=0x456;next[6].GuestIpa+=0x10000;
  commit(&p,0x900004000ULL,&update,&shadow,5,10);
  assert(!state.LeafHistoryNext && !memcmp(old,next,sizeof(old)));
  /* Runs cannot bridge holes, different owners/segments, anonymous PTEs,
   * unchanged mappings, or a valid replacement. */
  reset();populate(8,3,0xaaa,0);populate(11,2,0xbbb,0);
  populate(14,2,0xbbb,3);populate(16,1,0,3);populate(17,2,0xbbb,3);
  next[18]=old[18];memcpy(saved,next,sizeof(next));
  commit(&p,0x900004000ULL,&update,&shadow,8,12);
  assert(state.LeafHistoryNext==4);
  check(0,8,3,0xaaa,0,update.FirstPteVirtualAddress);
  check(1,11,2,0xbbb,0,update.FirstPteVirtualAddress+3*4096);
  check(2,14,2,0xbbb,3,update.FirstPteVirtualAddress+6*4096);
  check(3,17,1,0xbbb,3,update.FirstPteVirtualAddress+9*4096);
  assert(!memcmp(next,saved,sizeof(next)) && !memcmp(old,next,sizeof(old)));
  /* Last PTE and physical-ring rollover must preserve current fields. */
  reset();populate(8191,1,0xdef,2);state.LeafHistoryNext=ADMISSION_G3_LEAF_RING;
  memset(&state.LeafHistory[0],0xff,sizeof(state.LeafHistory[0]));
  commit(&p,0x900004000ULL,&update,&shadow,8191,1);
  assert(state.LeafHistoryNext==ADMISSION_G3_LEAF_RING+1);
  check(ADMISSION_G3_LEAF_RING,8191,1,0xdef,2,update.FirstPteVirtualAddress);
  puts("UNMAP_OWNER_HISTORY_OK");
}
'''


class UnmapOwnerHistory(unittest.TestCase):
    def test_historical_extraction_can_use_new_observer_helper(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Adding a helper to FUNCTIONS must not make historical RED replays look
        for a function that did not yet exist. Supply historical source lacking
        the observer; the generator must take the compatibility helper from the
        current tree. This tests extraction only, not a substitute broker.
        """
        paging = (RENDER/'src/gpuva_g3_paging_windows.c').read_text()
        old = paging.replace(body(paging, 'AdmissionG3RecordUnmapOwners'), '')
        old = old.replace('  AdmissionG3RecordUnmapOwners(process, table_ipa, update,\n'
                          '      shadow->LogicalPtes, shadow->ResidentPtes, first, count);\n', '')

        def historical_source(argv, **kwargs):
            path = argv[-1].split(':', 1)[1]
            return old if path.endswith('gpuva_g3_paging_windows.c') else (ROOT/path).read_text()

        with tempfile.TemporaryDirectory() as tmp:
            # Generation merely extracts these function texts. No broker code
            # executes in this compatibility test.
            platform = Path(tmp)/'hv_agx_retained_platform.c'
            platform.write_text('int translate_guest(void) { return 0; }\n'
                                'int gpuva_execute(void) { return 0; }\n')
            with patch.object(replay, 'M1N1', Path(tmp)), patch.object(
                    replay.subprocess, 'check_output', side_effect=historical_source):
                source = replay.generate('before-observer')
        self.assertIn('static VOID AdmissionG3RecordUnmapOwners(', source)
        self.assertNotIn('AdmissionG3RecordUnmapOwners(process, table_ipa, update,', source)

    def test_successful_commit_preserves_invalidated_owner_runs(self):
        text = (RENDER/'src/gpuva_g3_paging_windows.c').read_text()
        update = body(text, 'AdmissionG3UpdateLeaf')
        # The real success-only logical commit; the following generation bump
        # and cleanup are outside this narrowly scoped observer regression.
        commit = update[update.index('  /* M2-a:'):update.index('  ++process->Graph.MappingGeneration;', update.index('  /* M2-a:'))]
        name = 'AdmissionG3RecordUnmapOwners'
        observer = body(text, name) if name + '(' in text else ''
        types = (RENDER/'include/render_admission.h').read_text()
        types = types[types.index('#define ADMISSION_G3_LEAF_RING '):]
        types = types[:types.index('} ADMISSION_G3_LEAF_HISTORY;')+len('} ADMISSION_G3_LEAF_HISTORY;')]
        source = PROGRAM.replace('@@TYPES@@', types).replace('@@OBSERVER@@', observer).replace('@@COMMIT@@', commit)
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp); (tmp/'replay.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                '-I', str(SHARED/'include'), str(tmp/'replay.c'), '-o', str(tmp/'replay')], check=True)
            run = subprocess.run([str(tmp/'replay')], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('UNMAP_OWNER_HISTORY_OK', run.stdout)

if __name__ == '__main__':
    unittest.main()
