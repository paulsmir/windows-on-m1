"""rc83: the render build and the buffer-manager save without serial uncached reads.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2012 (Wom1JobTiming971, render jobs): BackendBefore -> 3D kick 266 us and
Complete -> JobEnd 77 us, while a compute build that stores ~4400 bytes to
the same MmNonCached (Device) objects but loads only 4 took 4-17 us. The
render build read the 12 KiB per-submission template objects from the
write-combined arena with 1527 serial 8-byte volatile loads, and
AppleAgxRenderManagerSave read the 380 manager bytes one Device load at a
time (State's arrays are 4 bytes off the objects' 8-byte alignment, so
copy_shared_bytes never took its word path). The tests check, through the
production builder compiled with the host read probe:
- RED/GREEN cost model: with Owner->DirectReads (the pre-rc83 reads, the
  Wom1BuildDirectReads A/B) a managed render build issues >1500 element-wise
  arena loads and the save 380 Device loads; by default a build issues no
  element-wise arena load and no Device load (one aligned load of the
  InitBM control word, one of the notifier generation for a prelude), reads
  each arena object with one bulk copy, and the save issues 48 aligned
  loads;
- equivalence: the same job sequence (queue initialisation, InitBM,
  manager switch, restore, both slots, firmware mutations of the manager
  objects, v5 prelude compute, unmanaged G4) leaves every firmware-shared
  object, the arena, the job images, the manager states and the notifier
  counts byte-identical in both modes;
- read_shared_bytes reproduces memcpy for every source alignment and length
  with naturally aligned loads of at most 8 bytes;
- the Windows build hands the device value Wom1BuildDirectReads to the
  owner before every build and every save.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'
SRC = SHARED / 'src/apple_agx_render_shared_memory.c'
BACKEND = ROOT / 'drivers/apple-agx/render-admission/src/backend_platform_windows.c'
NAMES = ['render_shared_memory', 'g13_compute_work', 'render_template.generated',
         'render_template_rebase', 'exp208_adapter', 'exp208_gdi',
         'exp208_framebuffer', 'gdi', 'relocation', 'memory', 'exp208_dynamic']


def function(text, name):
    m = re.search(r'(?m)^static [A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


PRELUDE = r'''
#define RC83_PROBE AppleAgxRenderSharedReadProbe
static void rc83_probe_reset(void) { memset(&RC83_PROBE, 0, sizeof(RC83_PROBE)); }
typedef struct { unsigned char *Data; unsigned long long Bytes; } RC83_SAVE;
static unsigned long long rc83_hash(unsigned long long h, const void *p, unsigned long long n) {
  const unsigned char *b = p;
  while (n--) { h ^= *b++; h *= 1099511628211ull; }
  return h;
}
'''

FIXTURE = r'''
  {
    /* rc83: run one job sequence in both read modes from the same state. */
    enum { STEPS = 12 };
    static RC83_SAVE objects_saved[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT];
    static unsigned long long hashes[2][STEPS];
    static unsigned long arena_loads[2][STEPS], shared_loads[2][STEPS], bulk[2][STEPS];
    static APPLE_AGX_RENDER_SHARED_MEMORY_OWNER owner_saved;
    static APPLE_AGX_EXP208_RELOCATION_OBJECT sources_saved[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
    static APPLE_AGX_EXP208_RELOCATION_OBJECT active_saved[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
    static unsigned char *final_objects[2][APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT];
    static unsigned char *final_arena[2];
    unsigned char *arena_saved = malloc(APPLE_AGX_EXP208_ARENA_BYTES);
    APPLE_AGX_BACKEND_JOB_IMAGE staged_saved = staged_job;
    unsigned mode, o;
    assert(arena_saved);
    memcpy(arena_saved, arena, APPLE_AGX_EXP208_ARENA_BYTES);
    owner_saved = owner;
    memcpy(sources_saved, source_objects, sizeof(sources_saved));
    memcpy(active_saved, active_objects, sizeof(active_saved));
    for (o = 0; o < owner.ObjectCount; ++o) {
      objects_saved[o].Bytes = owner.Objects[o].Length;
      objects_saved[o].Data = malloc(objects_saved[o].Bytes);
      memcpy(objects_saved[o].Data, owner.Objects[o].CpuAddress, objects_saved[o].Bytes);
    }
    for (mode = 0; mode < 2; ++mode) {
      APPLE_AGX_RENDER_MANAGER_STATE ma = {0}, mb = {0};
      APPLE_AGX_RENDER_MANAGER_KEY ka = {0}, kb = {0};
      APPLE_AGX_RENDER_COMPUTE_INPUT input;
      APPLE_AGX_RENDER_COMPUTE_OUTPUT output;
      unsigned step = 0, i;
      /* restore the common starting state */
      memcpy(arena, arena_saved, APPLE_AGX_EXP208_ARENA_BYTES);
      owner = owner_saved;
      memcpy(source_objects, sources_saved, sizeof(sources_saved));
      memcpy(active_objects, active_saved, sizeof(active_saved));
      for (o = 0; o < owner.ObjectCount; ++o)
        memcpy(owner.Objects[o].CpuAddress, objects_saved[o].Data, objects_saved[o].Bytes);
      staged_job = staged_saved;
      owner.DirectReads = mode == 0 ? APPLE_AGX_TRUE : APPLE_AGX_FALSE;
      ka.Owner=41;ka.Generation=3;ka.RootIpa=0x60000000ULL;
      ka.Backing[0]=(APPLE_AGX_G4_PROCESS_RANGE){0x9100000ULL,0x10000u,0u};
      ka.Backing[1]=(APPLE_AGX_G4_PROCESS_RANGE){0x9120000ULL,0x10000u,0u};
      ka.Backing[2]=(APPLE_AGX_G4_PROCESS_RANGE){0x9200000ULL,0x800000u,0u};
      kb=ka;kb.Owner=42;kb.RootIpa=0x70000000ULL;
      kb.Backing[0].Va=0xa100000ULL;kb.Backing[1].Va=0xa120000ULL;kb.Backing[2].Va=0xa200000ULL;
#define RC83_SOURCE(k) do { source_objects[41].GpuVa=(k).Backing[0].Va; \
    source_objects[42].GpuVa=(k).Backing[1].Va;source_objects[43].GpuVa=(k).Backing[2].Va; } while(0)
#define RC83_BUILD(m,k,init,initbm,f,stamp) (staged_job.TaExpectedStamp=0x7a000000u+(stamp), \
    staged_job.D3ExpectedStamp=0x3d000000u+(stamp), \
    AppleAgxRenderSharedMemoryBuildManagedG4Job(&owner,&(m),&(k),(f),(init),arena, \
    APPLE_AGX_EXP208_ARENA_BYTES,source_objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, \
    0x1500800000ULL,(initbm),&bindings,&staged_job,active_objects,&active_job))
#define RC83_STEP() do { unsigned long long h = 14695981039346656037ull; unsigned q; \
    for (q = 0; q < owner.ObjectCount; ++q) h = rc83_hash(h, owner.Objects[q].CpuAddress, owner.Objects[q].Length); \
    h = rc83_hash(h, &active_job, sizeof(active_job)); h = rc83_hash(h, &ma, sizeof(ma)); \
    h = rc83_hash(h, &mb, sizeof(mb)); h = rc83_hash(h, &owner.NotifierRenderCount, 12u); \
    h = rc83_hash(h, arena, APPLE_AGX_EXP208_ARENA_BYTES); \
    assert(step < STEPS); hashes[mode][step] = h; arena_loads[mode][step] = RC83_PROBE.ArenaLoads; \
    shared_loads[mode][step] = RC83_PROBE.SharedLoads; bulk[mode][step] = RC83_PROBE.ArenaBulk; \
    ++step; rc83_probe_reset(); } while (0)
      /* emulate the G4 bind patching the arena's per-submission objects */
#define RC83_PATCH(seed) do { static const unsigned po[7] = {12u,13u,14u,15u,17u,18u,19u}; unsigned q, b; \
    for (q = 0; q < 7u; ++q) for (b = (seed) % 3u; b < layouts[po[q]].Size; b += 61u) \
      arena[layouts[po[q]].ArenaOffset + b] ^= (unsigned char)((seed) * 37u + b); } while (0)
      rc83_probe_reset();
      RC83_SOURCE(ka);
      /* 0: queue (re)initialisation with InitBM: every template object, both slots */
      assert(RC83_BUILD(ma,ka,APPLE_AGX_TRUE,APPLE_AGX_TRUE,100u,0x100u)); RC83_STEP();
      /* firmware advances the manager objects, then the completion saves them */
      for (i = 0; i < 188u; i += 7u) active_objects[1].Data[i] ^= (unsigned char)(i + 1u);
      for (i = 0; i < 64u; i += 5u) { active_objects[20].Data[i] ^= 0x11u;
        active_objects[21].Data[i] ^= 0x22u; active_objects[22].Data[i] ^= 0x33u; }
      assert(AppleAgxRenderManagerSave(&owner,&ma,100u,active_objects)); RC83_STEP();
      /* 2: the same manager, restored, second slot */
      RC83_PATCH(1u);
      assert(RC83_BUILD(ma,ka,APPLE_AGX_FALSE,APPLE_AGX_FALSE,101u,0x200u)); RC83_STEP();
      /* 3: its v5 compute prelude */
      memset(&input, 0, sizeof(input)); memset(&output, 0, sizeof(output));
      input.CdmStreamBase=0x1234000ULL; input.CdmStreamBytes=0x400u; input.Counter=101u;
      input.UscExecutionBase=0x1100000000ULL; input.VmSlot=2u; input.EventNumber=5u;
      input.StampValue=101u; input.EventSequence=101u; input.ClientSequence=101u&0xffu;
      input.PreemptionGpuAddress=0x5550000ULL; input.Timestamps=1u;
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output)); RC83_STEP();
      for (i = 0; i < 64u; i += 3u) active_objects[22].Data[i] += 1u;
      assert(AppleAgxRenderManagerSave(&owner,&ma,101u,active_objects)); RC83_STEP();
      /* 5: another manager: InitBM, no saved state (template sources) */
      RC83_SOURCE(kb); RC83_PATCH(2u);
      assert(AppleAgxRenderManagerNeedsBind(&owner,&kb));
      assert(RC83_BUILD(mb,kb,APPLE_AGX_FALSE,APPLE_AGX_TRUE,102u,0x300u)); RC83_STEP();
      assert(AppleAgxRenderManagerSave(&owner,&mb,102u,active_objects)); RC83_STEP();
      /* 7: back to the first: InitBM and its restored state */
      RC83_SOURCE(ka); RC83_PATCH(3u);
      assert(RC83_BUILD(ma,ka,APPLE_AGX_FALSE,APPLE_AGX_TRUE,103u,0x400u)); RC83_STEP();
      assert(AppleAgxRenderManagerSave(&owner,&ma,103u,active_objects)); RC83_STEP();
      /* 9: an unmanaged G4 build, slot 0 */
      RC83_PATCH(4u);
      staged_job.TaExpectedStamp=0x7a000500u; staged_job.D3ExpectedStamp=0x3d000500u;
      assert(AppleAgxRenderSharedMemoryBuildActiveG4Job(&owner,arena,APPLE_AGX_EXP208_ARENA_BYTES,
          source_objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,0x1500800000ULL,
          APPLE_AGX_FALSE,&bindings,&staged_job,active_objects,&active_job)); RC83_STEP();
      assert(step == 10u);
      final_arena[mode] = malloc(APPLE_AGX_EXP208_ARENA_BYTES);
      memcpy(final_arena[mode], arena, APPLE_AGX_EXP208_ARENA_BYTES);
      for (o = 0; o < owner.ObjectCount; ++o) {
        final_objects[mode][o] = malloc(owner.Objects[o].Length);
        memcpy(final_objects[mode][o], owner.Objects[o].CpuAddress, owner.Objects[o].Length);
      }
#undef RC83_PATCH
#undef RC83_STEP
#undef RC83_BUILD
#undef RC83_SOURCE
    }
    /* equivalence: every step leaves identical bytes */
    for (o = 0; o < 10u; ++o) {
      if (hashes[0][o] != hashes[1][o]) { fprintf(stderr, "step %u differs\n", o); abort(); }
    }
    for (o = 0; o < owner.ObjectCount; ++o)
      assert(memcmp(final_objects[0][o], final_objects[1][o], owner.Objects[o].Length) == 0);
    assert(memcmp(final_arena[0], final_arena[1], APPLE_AGX_EXP208_ARENA_BYTES) == 0);
    /* RED: the pre-rc83 reads (mode 0) */
    assert(arena_loads[0][2] > 1500u && arena_loads[0][9] > 1500u);
    assert(shared_loads[0][1] == 380u && shared_loads[0][4] == 380u);
    assert(shared_loads[0][2] == 8u && shared_loads[0][3] == 4u); /* threshold, counter; generation */
    /* GREEN: no element-wise arena load anywhere, Device loads only where a
     * firmware-written word is needed */
    for (o = 0; o < 10u; ++o) assert(arena_loads[1][o] == 0u);
    assert(shared_loads[1][0] == 1u && bulk[1][0] == 36u + 5u); /* InitBM control word; all objects, template manager */
    assert(shared_loads[1][1] == 48u && shared_loads[1][4] == 48u && shared_loads[1][6] == 48u);
    assert(shared_loads[1][2] == 0u && bulk[1][2] == 6u + 1u);   /* 12,14,15,17,18,19 + scene */
    assert(shared_loads[1][3] == 1u && bulk[1][3] == 0u);         /* notifier generation */
    assert(shared_loads[1][5] == 1u && bulk[1][5] == 7u + 5u);    /* + InitBM, template manager */
    assert(shared_loads[1][7] == 1u && bulk[1][7] == 7u + 1u);    /* + InitBM, saved manager */
    assert(shared_loads[1][9] == 0u && bulk[1][9] == 6u);
    owner.DirectReads = APPLE_AGX_FALSE;
    for (o = 0; o < owner.ObjectCount; ++o) {
      free(objects_saved[o].Data); free(final_objects[0][o]); free(final_objects[1][o]);
    }
    free(final_arena[0]); free(final_arena[1]); free(arena_saved);
  }
'''

READS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned APPLE_AGX_U32; typedef unsigned long long APPLE_AGX_U64;
static unsigned loads, misaligned;
#define READ_PROBE(field, count) (loads += (count))
@@FUNCTION@@
int main(void) {
  static unsigned char src[600], dst[600], ref[600];
  unsigned so, n;
  for (unsigned i = 0; i < sizeof(src); ++i) src[i] = (unsigned char)(i * 151u + 3u);
  for (so = 0; so < 16; ++so)
    for (n = 0; n < 400; ++n) {
      memset(dst, 0xee, sizeof(dst)); memset(ref, 0xee, sizeof(ref));
      read_shared_bytes(dst + 3, src + so, n);
      memcpy(ref + 3, src + so, n);
      assert(!memcmp(dst, ref, sizeof(dst)));
    }
  /* the manager objects: Info at 0x...f44 (4 mod 8), 20/21/22 at 0x...fc0 */
  loads = 0; read_shared_bytes(dst, src + 4, 188); assert(loads == 24u);
  loads = 0; read_shared_bytes(dst + 4, src + 64, 64); assert(loads == 8u);
  assert(!misaligned);
  puts("PASS");
  return 0;
}
'''


class RenderBuildReads(unittest.TestCase):
    def test_reads_model_and_equivalence(self):
        source = (SHARED / 'tests/apple_agx_render_shared_memory_test.c').read_text()
        marker = '  assert(AppleAgxRenderSharedMemoryDestroy(&owner) =='
        self.assertEqual(source.count(marker), 1)
        source = source.replace('#include <string.h>', '#include <string.h>\n#include <stdio.h>', 1)
        source = source.replace('int main(void) {', PRELUDE + 'int main(void) {', 1)
        source = source.replace(marker, FIXTURE + marker)
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'probe.c').write_text(source)
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-fsanitize=address,undefined', '-DAPPLE_AGX_RENDER_SHARED_READ_PROBE',
                '-I', str(SHARED / 'include'), str(tmp / 'probe.c'),
                *[str(SHARED / 'src' / ('apple_agx_' + n + '.c')) for n in NAMES],
                '-o', str(tmp / 'probe')], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(tmp / 'probe')], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_read_shared_bytes_matches_memcpy_with_aligned_loads(self):
        body = function(SRC.read_text(), 'read_shared_bytes')
        for width in ('APPLE_AGX_U64', 'APPLE_AGX_U32', 'unsigned char'):
            cast = f'*(const volatile {width} *)(Source + i)'
            self.assertIn(cast, body)
            check = {'APPLE_AGX_U64': 7, 'APPLE_AGX_U32': 3, 'unsigned char': 0}[width]
            body = body.replace(cast, f'(misaligned += (((APPLE_AGX_U64)(Source + i)) & {check}u) != 0u, {cast})')
        program = READS.replace('@@FUNCTION@@', body)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'reads.c'; exe = Path(directory) / 'reads'
            path.write_text(program)
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O1', '-Wall', '-Wextra',
                                    '-Werror', '-Wno-unused-function', str(path), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_windows_passes_the_switch(self):
        text = BACKEND.read_text()
        self.assertIn('AdmissionPlatformSwitchEnabled(Context, L"Wom1BuildDirectReads")', text)
        for name in ('AdmissionExternalBuildJob', 'AdmissionSaveG4Manager'):
            body = re.search(r'(?ms)^static APPLE_AGX_BACKEND_BOOL ' + name + r'\(.*?^\}', text).group(0)
            self.assertIn('runtime->Initdata.RenderSharedMemory.DirectReads =\n'
                          '      runtime->BuildDirectReads;', body, name)


if __name__ == '__main__':
    unittest.main()
