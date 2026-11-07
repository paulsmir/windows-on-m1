"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The real rc124 builder must accept two distinct scenes using one live manager
without restoring firmware state, and retain its busy summary until both exact
users terminate. Rebind, eviction, stale completion and unpublished rollback
must not release or overwrite the other user's manager. Pattern stack init and
ASan/UBSan exercise production C, including both terminal orders.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_render_manager_slots_rc124 import SHARED, NAMES, FIXTURE
from test_render_manager_deferred import CALLBACK

BODY = r'''
    owner.ManagerDepth2 = 1;
    for (unsigned order = 0; order < 2; ++order) {
      unsigned a = 110u + 4u * order, b = a + 1u;
      unsigned counter = SL_U32(21,0);
      assert(SL_BUILD(0, 0, 0, a));
      assert(SL_BUILD(0, 0, 0, b));
      assert(SL_U32(21,0) == counter + 2u);
      assert(!SL_BUILD(0, 0, 0, b + 1u)); /* depth bound */
      assert(!SL_BUILD(1, 0, 1, b + 1u)); /* another key */
      assert(!AppleAgxRenderManagerDrain(&owner));
      assert(!AppleAgxRenderManagerSave(&owner,&m[0],999,active_objects));
      assert(!AppleAgxRenderManagerCancelBuild(&owner,&m[0],a,active_objects));
      unsigned first = order ? b : a, last = order ? a : b;
      assert(AppleAgxRenderManagerSave(&owner,&m[0],first,active_objects));
      assert(m[0].PendingFence == last && owner.ManagerFence == last);
      assert(!AppleAgxRenderManagerDrain(&owner));
      assert(AppleAgxRenderManagerSave(&owner,&m[0],first,active_objects)); /* retry */
      assert(m[0].PendingFence == last);
      assert(AppleAgxRenderManagerSave(&owner,&m[0],last,active_objects));
      assert(!m[0].PendingFence && !owner.ManagerFence);
      assert(m[0].Resident && m[0].Valid);
    }
    /* Changed backing after a resident snapshot: cancel/retry must never
     * relabel and restore bytes from the previous backing allocation. */
    k[0].Backing[2].Bytes += 0x20000u;
    assert(SL_BUILD(0, 0, 1, 116));
    assert(!owner.LastBuildRestored);
    assert(AppleAgxRenderManagerCancelBuild(&owner,&m[0],116,active_objects));
    assert(SL_BUILD(0, 0, 1, 117));
    assert(!owner.LastBuildRestored);
    assert(SL_U32(20,4) == k[0].Backing[2].Bytes / 0x20000u);
    assert(AppleAgxRenderManagerSave(&owner,&m[0],117,active_objects));
    /* Failed successor publication rolls back only its increment. */
    assert(SL_BUILD(0, 0, 0, 120));
    unsigned before = SL_U32(21,0);
    assert(SL_BUILD(0, 0, 0, 121));
    assert(AppleAgxRenderManagerCancelBuild(&owner,&m[0],121,active_objects));
    assert(SL_U32(21,0) == before && m[0].PendingFence == 120);
    assert(AppleAgxRenderManagerSave(&owner,&m[0],120,active_objects));
    /* A changed generation/root/backing can never join pending ownership. */
    assert(SL_BUILD(0, 0, 0, 122));
    k[0].Generation++;
    assert(!SL_BUILD(0, 0, 1, 123));
    k[0].Generation--;
    k[0].RootIpa += 0x4000;
    assert(!SL_BUILD(0, 0, 1, 123));
    k[0].RootIpa -= 0x4000;
    k[0].Backing[2].Bytes += 0x20000;
    assert(!SL_BUILD(0, 0, 1, 123));
    k[0].Backing[2].Bytes -= 0x20000;
    assert(SL_BUILD(0, 0, 0, 123));
    assert(AppleAgxRenderManagerAbort(&owner,&m[0],122));
    assert(m[0].PendingFence == 123 && owner.ManagerFence == 123);
    assert(!m[0].Valid && !m[0].Resident);
    assert(!AppleAgxRenderManagerDrain(&owner));
    assert(!SL_BUILD(0, 0, 0, 124));
    assert(AppleAgxRenderManagerAbort(&owner,&m[0],123));
    assert(!m[0].PendingFence && !owner.ManagerFence);
    assert(AppleAgxRenderManagerNeedsBind(&owner,&k[0]));
    assert(AppleAgxRenderManagerDrain(&owner));
    /* Both observation orders for success/success, abort/success,
     * success/abort and abort/abort. Exact terminal retries free nothing. */
    for (unsigned order=0;order<2;++order) for (unsigned faults=0;faults<4;++faults) {
      unsigned a=200u+8u*order+2u*faults,b=a+1u;
      assert(SL_BUILD(0,0,AppleAgxRenderManagerNeedsBind(&owner,&k[0]),a));
      assert(SL_BUILD(0,0,0,b));
      for (unsigned at=0;at<2;++at) {
        unsigned index=at^order, fence=index ? b:a, other=index ? a:b;
        for (unsigned retry=0;retry<2;++retry) {
          if (faults & (1u<<index)) assert(AppleAgxRenderManagerAbort(&owner,&m[0],fence));
          else assert(AppleAgxRenderManagerSave(&owner,&m[0],fence,active_objects));
          assert(owner.ManagerFence==(at ? 0u:other));
          assert(m[0].PendingFence==owner.ManagerFence);
        }
        if (!at) assert(!AppleAgxRenderManagerDrain(&owner));
      }
      assert((m[0].Valid!=0)==(faults==0));
      assert(AppleAgxRenderManagerDrain(&owner));
    }
    owner.ManagerDepth2 = 0;
    assert(SL_BUILD(0, 0, 1, 500));
    assert(!SL_BUILD(0, 0, 0, 501)); /* default-off serial behavior */
    assert(AppleAgxRenderManagerSave(&owner,&m[0],500,active_objects));
    assert(AppleAgxRenderManagerDrain(&owner));
    owner.ManagerSaveOnEvict = 0;
    owner.ManagerSlotCount = 0;
#undef SL_SOURCE
#undef SL_BUILD
#undef SL_FIRMWARE
#undef SL_U32
#undef SL_U64
#undef SL_SLOT_FIELDS
  }
'''

class GeneralManager(unittest.TestCase):
    def test_exact_users(self):
        fixture = FIXTURE[:FIXTURE.index('    /* 2: key 0 again:')]
        fixture = fixture.replace('    owner.ManagerSlotCount = 4u;', '''
    owner.ManagerSlotCount = 4u;
    owner.ManagerSaveOnEvict = 1;
    owner.ManagerEvict = evict;
    owner.ManagerEvictContext = m;
''') + BODY
        source = (SHARED/'tests/apple_agx_render_shared_memory_test.c').read_text()
        source = source.replace('int main(', CALLBACK+'\nint main(', 1)
        marker = '  assert(AppleAgxRenderSharedMemoryDestroy(&owner) =='
        source = source.replace(marker, fixture+marker)
        with tempfile.TemporaryDirectory() as d:
            p = Path(d); (p/'test.c').write_text(source)
            result = subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra',
                '-Werror','-ftrivial-auto-var-init=pattern','-fsanitize=address,undefined',
                '-I',str(SHARED/'include'),str(p/'test.c'),
                *[str(SHARED/'src'/('apple_agx_'+n+'.c')) for n in NAMES],
                '-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result = subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
