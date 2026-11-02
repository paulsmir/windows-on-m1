"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Skipping per-render manager snapshots must not lose firmware-written state on
LRU eviction, InitBM/reinit, or teardown. Failed eviction must not overwrite
resident bytes or bindings. Stats must still feed TVB growth each completion,
and neither a live job nor a stale process generation may be snapshotted as
completed. This executes the real shared builder with ASan/UBSan and pattern
stack initialization, not a replacement manager model.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_render_manager_slots_rc124 import SHARED, NAMES, FIXTURE

CALLBACK = r'''
static unsigned saves, fail_save;
static APPLE_AGX_BOOL evict(void *ctx, const APPLE_AGX_RENDER_MANAGER_KEY *key,
    APPLE_AGX_U32 fence, const APPLE_AGX_EXP208_RELOCATION_OBJECT *objects) {
  APPLE_AGX_RENDER_MANAGER_STATE snapshot = {0};
  APPLE_AGX_RENDER_MANAGER_STATE *states = ctx;
  if (fail_save) return APPLE_AGX_FALSE;
  assert(AppleAgxRenderManagerSnapshot(&snapshot,key,fence,objects,0));
  assert(AppleAgxRenderManagerAcceptSnapshot(&states[key->Owner-41u],&snapshot));
  ++saves;
  return APPLE_AGX_TRUE;
}
'''


class DeferredManager(unittest.TestCase):
    def test_eviction_reinit_and_failure(self):
        fixture = FIXTURE.replace('    owner.ManagerSlotCount = 4u;', '''
    owner.ManagerSlotCount = 4u;
    owner.ManagerSaveOnEvict = 1;
    owner.ManagerEvict = evict;
    owner.ManagerEvictContext = m;
''')
        fixture = fixture.replace('    /* 2: key 0 again:', '''
    assert(m[0].Resident && m[0].Valid && !m[0].PendingFence);
    assert(memcmp(m[0].Info,snap[0],188u) != 0); /* not copied at completion */
    assert(memcmp(m[0].Stats,active_objects[22].Data,64u) == 0);
    assert(saves == 0);
    /* 2: key 0 again:''')
        fixture = fixture.replace('    assert(SL_BUILD(3, APPLE_AGX_FALSE, APPLE_AGX_TRUE, 111u));', '''
    fail_save = 1;
    assert(!SL_BUILD(3, APPLE_AGX_FALSE, APPLE_AGX_TRUE, 111u));
    assert(!AppleAgxRenderManagerNeedsBind(&owner,&k[0]));
    assert(m[0].Resident && saves == 0);
    fail_save = 0;
    {
      unsigned bytes=source_objects[72].Size;
      source_objects[72].Size=0; /* late Scene13 relocation failure */
      assert(!SL_BUILD(3, APPLE_AGX_FALSE, APPLE_AGX_TRUE, 111u));
      assert(AppleAgxRenderManagerNeedsBind(&owner,&k[0]));
      assert(!m[0].Resident && memcmp(m[0].Info,snap[0],188u)==0);
      source_objects[72].Size=bytes;
    }
    assert(SL_BUILD(3, APPLE_AGX_FALSE, APPLE_AGX_TRUE, 111u));
    assert(saves == 1 && !m[0].Resident);
    assert(memcmp(m[0].Info,snap[0],188u) == 0);
    /* Uncompleted work cannot be drained, even at reset. */
    assert(!AppleAgxRenderManagerDrain(&owner));
''')
        fixture = fixture.replace('    assert(SL_BUILD(0, APPLE_AGX_TRUE, APPLE_AGX_TRUE, 113u));', '''
    fail_save = 1;
    assert(!SL_BUILD(0, APPLE_AGX_TRUE, APPLE_AGX_TRUE, 113u));
    assert(!AppleAgxRenderManagerNeedsBind(&owner,&k[0]));
    assert(!AppleAgxRenderManagerNeedsBind(&owner,&k[2]));
    fail_save = 0;
    assert(SL_BUILD(0, APPLE_AGX_TRUE, APPLE_AGX_TRUE, 113u));
    assert(owner.LastBuildRestored); /* snapshot taken BEFORE queue init */
''')
        fixture = fixture.replace('    owner.ManagerSlotCount = 0u;\n#undef', '''
    assert(AppleAgxRenderManagerDrain(&owner));
    assert(!m[0].Resident);
    assert(AppleAgxRenderManagerDrain(&owner)); /* idempotent */
    /* Construction succeeded, but no firmware publication occurred.
     * Cancellation must restore the live counter and release pending fences. */
    {
      unsigned before=SL_U32(21,0);
      assert(SL_BUILD(0, APPLE_AGX_FALSE, APPLE_AGX_FALSE, 114u));
      assert(AppleAgxRenderManagerCancelBuild(&owner,&m[0],114u,active_objects));
      assert(!owner.ManagerFence && !m[0].PendingFence);
      assert(SL_U32(21,0)==before);
      assert(!AppleAgxRenderManagerNeedsBind(&owner,&k[0]));
    }
    {
      unsigned bytes=source_objects[72].Size, before=SL_U32(21,0);
      source_objects[72].Size=0;
      assert(!SL_BUILD(0, APPLE_AGX_FALSE, APPLE_AGX_FALSE, 114u));
      assert(SL_U32(21,0)==before && !AppleAgxRenderManagerNeedsBind(&owner,&k[0]));
      source_objects[72].Size=bytes;
    }
    /* Same process, changed manager key (e.g. TVB grows) while a slot is
     * resident: preserve the old bytes before replacing its CPU record. */
    assert(SL_BUILD(0, APPLE_AGX_FALSE, APPLE_AGX_FALSE, 114u));
    assert(AppleAgxRenderManagerSave(&owner,&m[0],114u,active_objects));
    k[0].Generation++;
    assert(SL_BUILD(0, APPLE_AGX_FALSE, APPLE_AGX_TRUE, 115u));
    assert(!owner.ManagerSlotLive && !owner.LastBuildRestored);
    assert(AppleAgxRenderManagerCancelBuild(&owner,&m[0],115u,active_objects));
    assert(!owner.ManagerFence && !m[0].PendingFence);
    assert(AppleAgxRenderManagerNeedsBind(&owner,&k[0]));
    assert(SL_BUILD(0, APPLE_AGX_FALSE, APPLE_AGX_TRUE, 115u));
    assert(AppleAgxRenderManagerSave(&owner,&m[0],115u,active_objects));
    fail_save = 1;
    assert(AppleAgxRenderSharedMemoryDestroy(&owner)==AppleAgxRenderSharedMemoryResultReleaseFailed);
    assert(owner.Initialized && m[0].Resident);
    fail_save = 0;
    assert(AppleAgxRenderSharedMemoryDestroy(&owner)==AppleAgxRenderSharedMemoryResultOk);
    assert(!m[0].Resident);
    {
      APPLE_AGX_RENDER_MANAGER_STATE stale = m[0], next = m[0];
      next.Key.Generation++;
      next.Resident = 1;
      assert(AppleAgxRenderManagerAcceptSnapshot(&next,&stale));
      assert(next.Resident); /* old generation must not replace a new one */
      next = m[0]; next.Resident = 1; next.SavedFence++;
      assert(!AppleAgxRenderManagerAcceptSnapshot(&next,&stale));
      next = m[0]; next.PendingFence = 999;
      assert(!AppleAgxRenderManagerAcceptSnapshot(&next,&stale));
    }
    owner.ManagerSaveOnEvict = 0;
    owner.ManagerEvict = NULL;
    owner.ManagerEvictContext = NULL;
    owner.ManagerSlotCount = 0u;
#undef''')
        source = (SHARED / 'tests/apple_agx_render_shared_memory_test.c').read_text()
        source = source.replace('#include <string.h>', '#include <string.h>\n#include <stdio.h>')
        source = source.replace('int main(', CALLBACK + '\nint main(', 1)
        marker = '  assert(AppleAgxRenderSharedMemoryDestroy(&owner) =='
        self.assertEqual(source.count(marker), 1)
        source = source.replace(marker, fixture + marker)
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'probe.c').write_text(source)
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-ftrivial-auto-var-init=pattern', '-fsanitize=address,undefined',
                '-I', str(SHARED / 'include'), str(tmp / 'probe.c'),
                *[str(SHARED / 'src' / ('apple_agx_' + n + '.c')) for n in NAMES],
                '-o', str(tmp / 'probe')], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(tmp / 'probe')], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == '__main__':
    unittest.main()
