"""rc124: firmware buffer-manager slots for managed (private-scene) renders.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The KMD keeps one set of buffer-manager objects {1 Info, 20 BlockControl,
21 Counter, 22 Stats} per adapter: every render job of another key needs an
InitBM and a restore of that key's saved state, and every job's start state
is the previous job's saved end state, so a second job cannot be built while
the first runs (EXP2051 manager_bind). Asahi (buffer.rs NUM_BUFFERS 127,
queue/render.rs InitBuffer only on rebind) gives each buffer its own
firmware slot and objects. The test runs the production builder on the host
with four slots and checks:
- a key keeps its slot: its next job is live (no InitBM, no restore), the
  firmware-written manager bytes stay as the firmware left them, and only
  Counter advances;
- two keys alternate without rebinding (the app and DWM pattern) and never
  touch each other's manager objects;
- every reference to the manager objects (objects 1, 13, 15-19) points at the
  slot's copy, which keeps the original's alignment modulo 64 bytes, and the
  slot number is written to InitBM/TA/StartTA/Start3D;
- the least recently used slot is rebound when all are taken; a queue
  initialisation forgets every binding; an unmanaged build releases slot 0
  only; ManagerSlotCount 0 keeps the pre-rc124 single slot.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'
NAMES = ['render_shared_memory', 'g13_compute_work', 'render_template.generated',
         'render_template_rebase', 'exp208_adapter', 'exp208_gdi',
         'exp208_framebuffer', 'gdi', 'relocation', 'memory', 'exp208_dynamic']

FIXTURE = r'''
  {
    /* rc124: managed renders with four buffer-manager slots */
    APPLE_AGX_RENDER_MANAGER_STATE m[4] = {{0}};
    APPLE_AGX_RENDER_MANAGER_KEY k[4];
    APPLE_AGX_U64 info0, va1;
    unsigned char snap[4][188];
    unsigned i, q;
    memset(k, 0, sizeof(k));
    for (q = 0; q < 4u; ++q) {
      k[q].Owner = 41u + q; k[q].Generation = 3u; k[q].RootIpa = 0x60000000ULL + 0x10000000ULL * q;
      k[q].Backing[0] = (APPLE_AGX_G4_PROCESS_RANGE){0x9100000ULL + 0x1000000ULL * q, 0x10000u, 0u};
      k[q].Backing[1] = (APPLE_AGX_G4_PROCESS_RANGE){0x9120000ULL + 0x1000000ULL * q, 0x10000u, 0u};
      k[q].Backing[2] = (APPLE_AGX_G4_PROCESS_RANGE){0x9200000ULL + 0x1000000ULL * q, 0x800000u, 0u};
    }
#define SL_SOURCE(kk) (source_objects[41].GpuVa=(kk).Backing[0].Va, \
    source_objects[42].GpuVa=(kk).Backing[1].Va, source_objects[43].GpuVa=(kk).Backing[2].Va)
#define SL_BUILD(q,init,initbm,f) (SL_SOURCE(k[q]), staged_job.TaExpectedStamp=0x7a000000u+((f)<<8), \
    staged_job.D3ExpectedStamp=0x3d000000u+((f)<<8), \
    AppleAgxRenderSharedMemoryBuildManagedG4Job(&owner,&m[q],&k[q],(f),(init),arena, \
    APPLE_AGX_EXP208_ARENA_BYTES,source_objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, \
    0x1500800000ULL,(initbm),&bindings,&staged_job,active_objects,&active_job))
    /* bytes of Info the build relocates (pointers): the firmware leaves them */
    static unsigned char relocated[188];
    {
      const APPLE_AGX_EXP208_RELOCATION *r = AppleAgxRenderTemplateRelocations();
      unsigned n = AppleAgxRenderTemplateRelocationCount(), b;
      for (i = 0; i < n; ++i)
        if (r[i].SourceObject == 1u)
          for (b = 0; b < 8u && r[i].SourceOffset + b < 188u; ++b) relocated[r[i].SourceOffset + b] = 1;
    }
#define SL_FIRMWARE(seed) do { unsigned b; for (b = 0; b < 188u; b += 9u) \
    if (!relocated[b]) active_objects[1].Data[b] ^= (unsigned char)((seed) + b); \
    for (b = 0; b < 64u; b += 7u) { active_objects[20].Data[b] ^= (unsigned char)(seed); \
      active_objects[22].Data[b] ^= (unsigned char)((seed) * 3u); } } while (0)
#define SL_U32(o,off) ((APPLE_AGX_U32)active_objects[o].Data[off] | ((APPLE_AGX_U32)active_objects[o].Data[(off)+1] << 8) | \
    ((APPLE_AGX_U32)active_objects[o].Data[(off)+2] << 16) | ((APPLE_AGX_U32)active_objects[o].Data[(off)+3] << 24))
#define SL_U64(o,off) ((APPLE_AGX_U64)SL_U32(o,off) | ((APPLE_AGX_U64)SL_U32(o,(off)+4) << 32))
#define SL_SLOT_FIELDS(s,initbm) do { assert(SL_U64(19,0x1c) == (s)); assert(SL_U32(15,0x58) == (s)); \
    assert(SL_U64(17,0x40) == (s)); if (initbm) assert(SL_U32(16,0x08) == (s)); } while (0)
    /* reference: slot 0 address of Info under the legacy single slot */
    owner.ManagerSlotCount = 0u;
    assert(SL_BUILD(0, APPLE_AGX_TRUE, APPLE_AGX_TRUE, 100u));
    info0 = active_objects[1].GpuVa;
    assert(SL_U64(16,0x14) == info0 && SL_U64(19,0x24) == info0);
    assert(AppleAgxRenderManagerSave(&owner,&m[0],100u,active_objects));
    memset(m, 0, sizeof(m));

    owner.ManagerSlotCount = 4u;
    /* 1: queue initialisation, key 0 binds slot 1 with InitBM */
    assert(AppleAgxRenderManagerNeedsBind(&owner,&k[0]));
    assert(SL_BUILD(0, APPLE_AGX_TRUE, APPLE_AGX_TRUE, 101u));
    assert(owner.ManagerSlot == 1u && !owner.ManagerSlotLive);
    va1 = active_objects[1].GpuVa;
    assert(va1 != info0 && (va1 & 63u) == (info0 & 63u) && va1 < info0);
    for (q = 20u; q <= 22u; ++q) assert((active_objects[q].GpuVa & 63u) == 0u);
    SL_SLOT_FIELDS(1u, 1);
    /* every reference to Info follows the slot copy */
    assert(SL_U64(16,0x14) == va1 && SL_U64(17,0x14) == va1 && SL_U64(17,0x214) == va1 &&
           SL_U64(18,0x28) == va1 && SL_U64(19,0x24) == va1 && SL_U64(15,0x24c) == va1);
    assert(SL_U64(1,0x40) == active_objects[20].GpuVa && SL_U64(1,0x64) == active_objects[21].GpuVa);
    SL_FIRMWARE(0x11u);
    memcpy(snap[0], active_objects[1].Data, 188u);
    assert(AppleAgxRenderManagerSave(&owner,&m[0],101u,active_objects));
    /* 2: key 0 again: live, no InitBM, the firmware's manager bytes stay */
    assert(!AppleAgxRenderManagerNeedsBind(&owner,&k[0]));
    {
      APPLE_AGX_U32 before = SL_U32(21,0);
      assert(SL_BUILD(0, APPLE_AGX_FALSE, APPLE_AGX_FALSE, 102u));
      assert(owner.ManagerSlot == 1u && owner.ManagerSlotLive && !owner.LastBuildRestored);
      assert(memcmp(snap[0], active_objects[1].Data, 188u) == 0);
      assert(SL_U32(21,0) == before + 1u);
      SL_SLOT_FIELDS(1u, 0);
    }
    assert(AppleAgxRenderManagerSave(&owner,&m[0],102u,active_objects));
    /* 3: key 1 binds slot 2 and leaves slot 1 alone */
    assert(AppleAgxRenderManagerNeedsBind(&owner,&k[1]));
    assert(!SL_BUILD(1, APPLE_AGX_FALSE, APPLE_AGX_FALSE, 103u)); /* a bind needs InitBM */
    assert(owner.LastBuildRefusal == APPLE_AGX_RENDER_BUILD_REFUSAL_MANAGER_BIND);
    assert(SL_BUILD(1, APPLE_AGX_FALSE, APPLE_AGX_TRUE, 103u));
    assert(owner.ManagerSlot == 2u && !owner.ManagerSlotLive);
    SL_SLOT_FIELDS(2u, 1);
    assert(active_objects[1].GpuVa != va1);
    SL_FIRMWARE(0x22u);
    memcpy(snap[1], active_objects[1].Data, 188u);
    assert(AppleAgxRenderManagerSave(&owner,&m[1],103u,active_objects));
    /* 4: alternate keys 0,1,0,1: always live, never InitBM */
    for (i = 0; i < 4u; ++i) {
      unsigned key = i & 1u;
      assert(!AppleAgxRenderManagerNeedsBind(&owner,&k[key]));
      assert(SL_BUILD(key, APPLE_AGX_FALSE, APPLE_AGX_FALSE, 104u + i));
      assert(owner.ManagerSlotLive && owner.ManagerSlot == 1u + key);
      assert(memcmp(snap[key], active_objects[1].Data, 188u) == 0);
      assert(AppleAgxRenderManagerSave(&owner,&m[key],104u + i,active_objects));
    }
    /* 5: keys 2 and 3: slot 3, then the least recently used slot (1) */
    assert(SL_BUILD(2, APPLE_AGX_FALSE, APPLE_AGX_TRUE, 110u));
    assert(owner.ManagerSlot == 3u);
    assert(AppleAgxRenderManagerSave(&owner,&m[2],110u,active_objects));
    assert(SL_BUILD(3, APPLE_AGX_FALSE, APPLE_AGX_TRUE, 111u));
    assert(owner.ManagerSlot == 1u);
    assert(AppleAgxRenderManagerSave(&owner,&m[3],111u,active_objects));
    assert(AppleAgxRenderManagerNeedsBind(&owner,&k[0]));
    assert(!AppleAgxRenderManagerNeedsBind(&owner,&k[1]));
    /* 6: key 0 rebinds into the least recently used slot (2, key 1's, last
     * used at 107) and restores its saved state */
    assert(SL_BUILD(0, APPLE_AGX_FALSE, APPLE_AGX_TRUE, 112u));
    assert(!owner.ManagerSlotLive && owner.LastBuildRestored && owner.ManagerSlot == 2u);
    assert(AppleAgxRenderManagerSave(&owner,&m[0],112u,active_objects));
    /* 7: an unmanaged build releases slot 0 only */
    staged_job.TaExpectedStamp=0x7a00f000u; staged_job.D3ExpectedStamp=0x3d00f000u;
    assert(AppleAgxRenderSharedMemoryBuildActiveG4Job(&owner,arena,APPLE_AGX_EXP208_ARENA_BYTES,
        source_objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,0x1500800000ULL,
        APPLE_AGX_FALSE,&bindings,&staged_job,active_objects,&active_job));
    assert(active_objects[1].GpuVa == info0);
    assert(!AppleAgxRenderManagerNeedsBind(&owner,&k[0]));
    /* 8: a queue initialisation forgets every binding */
    assert(SL_BUILD(0, APPLE_AGX_TRUE, APPLE_AGX_TRUE, 113u));
    assert(!owner.ManagerSlotLive);
    assert(AppleAgxRenderManagerSave(&owner,&m[0],113u,active_objects));
    assert(AppleAgxRenderManagerNeedsBind(&owner,&k[2]) && AppleAgxRenderManagerNeedsBind(&owner,&k[3]));
    owner.ManagerSlotCount = 0u;
#undef SL_SLOT_FIELDS
#undef SL_U64
#undef SL_U32
#undef SL_FIRMWARE
#undef SL_BUILD
#undef SL_SOURCE
    puts("RC124_SLOTS_PASS");
  }
'''


class RenderManagerSlots(unittest.TestCase):
    def test_slots(self):
        source = (SHARED / 'tests/apple_agx_render_shared_memory_test.c').read_text()
        marker = '  assert(AppleAgxRenderSharedMemoryDestroy(&owner) =='
        self.assertEqual(source.count(marker), 1)
        source = source.replace('#include <string.h>', '#include <string.h>\n#include <stdio.h>', 1)
        source = source.replace(marker, FIXTURE + marker)
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'probe.c').write_text(source)
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-fsanitize=address,undefined', '-I', str(SHARED / 'include'), str(tmp / 'probe.c'),
                *[str(SHARED / 'src' / ('apple_agx_' + n + '.c')) for n in NAMES],
                '-o', str(tmp / 'probe')], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(tmp / 'probe')], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('RC124_SLOTS_PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
