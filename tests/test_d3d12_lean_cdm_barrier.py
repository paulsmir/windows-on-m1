"""rc101: lean CDM barriers between consecutive application dispatches
(drivers/apple-agx/mesa/winsys/agx_win32_cdm_barrier.h, projected by
mesa/scripts/native-asahi-lean-cdm-barrier.py; APPLE_AGX_D3D12_LEAN_CDM_BARRIER).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- With the switch off, the CDM control stream must stay byte-identical to
  Asahi's (agx_cdm_launch + agx_cdm_barrier after every launch).
- With it on, only the barrier between two consecutive application dispatches
  may be lean (0x60000168 on G13G: USC cache invalidate, Unk 5/6/8); a
  precompiled kernel or compute blit on either side, an image writer before
  (mode 1), a texture reader or indirect grid after (mode 1) and the end of
  the stream all get Asahi's full word 0x600fffff.
- The shader classifier must flag every texture instruction and image access
  (bindful, bindless) and every image store or atomic.
"""
from pathlib import Path
import importlib.util
import os
import re
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import ROOT, local_tree, project
from test_d3d12_owner_allocation_failure import function

WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'
SCRIPT = ROOT / 'drivers/apple-agx/mesa/scripts/native-asahi-lean-cdm-barrier.py'
FULL, LEAN = 0x600fffff, 0x60000168

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#include "libagx_dgc.h"
#pragma clang diagnostic pop
struct agx_device {enum agx_chip chip;};
struct agx_screen_stub {struct agx_device dev;};
struct agx_context {
   struct {struct agx_screen_stub *screen;} base;
   unsigned windows_cdm_app_class, windows_cdm_launch_class;
   uint32_t windows_cdm_barriers[4];
};
struct agx_encoder {uint8_t *current, *end;};
struct agx_batch {
   struct agx_context *ctx;
   struct agx_encoder cdm;
   uint32_t *windows_cdm_lean;
   unsigned windows_cdm_lean_writer;
};
static struct agx_device *agx_device(struct agx_screen_stub *s) {return &s->dev;}
static void agx_flush_batch_for_reason(struct agx_context *c, struct agx_batch *b, const char *r)
{(void)c;(void)b;(void)r;assert(!"the harness encoder never fills");}
#define AGX_WIN32_CDM_BARRIER_NO_NIR
#include "agx_win32_cdm_barrier.h"
LAUNCH_INTERNAL
FINISH
static uint32_t stream[4096], reference[4096];
static struct agx_screen_stub screen = {{AGX_CHIP_G13G}};
static struct agx_context ctx = {{&screen}, 0, 0, {0, 0, 0, 0}};
static struct agx_batch batch = {&ctx, {(uint8_t *)stream, (uint8_t *)(stream + 4096)}, NULL, 0};
static uint32_t *ref = reference;
static unsigned launches;
static void launch(unsigned cls, bool indirect)
{
   struct agx_grid grid = indirect ? agx_grid_indirect(0x1500000040ull) : agx_3d(64, 1, 1);
   struct agx_workgroup wg = agx_workgroup(64, 1, 1);
   struct agx_cdm_launch_word_0_packed word;
   agx_pack(&word, CDM_LAUNCH_WORD_0, cfg) {cfg.uniform_register_count = 8;}
   /* agx_launch: the class of the application dispatch, for its final launch */
   ctx.windows_cdm_launch_class = cls;
   agx_launch_internal(&batch, grid, wg, word, MESA_SHADER_COMPUTE, 0x1040 + 64 * launches);
   assert(!ctx.windows_cdm_launch_class);
   ref = agx_cdm_launch(ref, AGX_CHIP_G13G, grid, wg, word, 0x1040 + 64 * launches);
   ref = agx_cdm_barrier(ref, AGX_CHIP_G13G);
   ++launches;
}
int main(void)
{
   const unsigned app = AGX_WIN32_CDM_APP;
   const unsigned memory = app | AGX_WIN32_CDM_NO_TEXTURE_READS | AGX_WIN32_CDM_NO_IMAGE_WRITES;
   launch(memory, false);                                /* A */
   launch(memory, false);                                /* B */
   launch(0, false);                                     /* C: precompiled kernel or blit */
   launch(memory, false);                                /* D */
   launch(app | AGX_WIN32_CDM_NO_IMAGE_WRITES, false);   /* E: reads textures */
   launch(app | AGX_WIN32_CDM_NO_TEXTURE_READS, false);  /* F: writes images */
   launch(memory, false);                                /* G */
   launch(memory, true);                                 /* H: indirect grid */
   launch(memory, false);                                /* I */
   agx_win32_cdm_batch_finish(&batch);
   assert(!batch.windows_cdm_lean);
   size_t bytes = (uint8_t *)batch.cdm.current - (uint8_t *)stream;
   assert(bytes == (size_t)((uint8_t *)ref - (uint8_t *)reference));
   printf("identical %d\n", !memcmp(stream, reference, bytes));
   printf("mode %u word %u lean %u full %u\n", ctx.windows_cdm_barriers[2],
          ctx.windows_cdm_barriers[3], ctx.windows_cdm_barriers[0], ctx.windows_cdm_barriers[1]);
   printf("barriers");
   for (uint32_t *w = stream; (uint8_t *)w < batch.cdm.current; ++w)
      if ((*w >> 29) == AGX_CDM_BLOCK_TYPE_BARRIER)
         printf(" %08x", *w);
   printf("\n");
   return 0;
}
'''

CLASSIFIER = r'''
#include <assert.h>
#include <stdio.h>
#include "nir.h"
#include "nir_builder.h"
#define AGX_WIN32_CDM_BARRIER_NO_STREAM
#include "agx_win32_cdm_barrier.h"
static const nir_shader_compiler_options options = {0};
static nir_builder begin(void) {
   return nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &options, "rc101");
}
static unsigned finish(nir_builder *b) {
   unsigned cls = agx_win32_cdm_shader_class(b->shader);
   ralloc_free(b->shader);
   return cls;
}
int main(void)
{
   glsl_type_singleton_init_or_ref();
   const unsigned none = AGX_WIN32_CDM_NO_TEXTURE_READS | AGX_WIN32_CDM_NO_IMAGE_WRITES;
   nir_builder b = begin();
   nir_def *addr = nir_imm_int64(&b, 0x1500000000ll);
   nir_build_store_global(&b, nir_build_load_global(&b, 1, 32, addr, .align_mul = 4),
                          addr, .align_mul = 4);
   nir_bindless_image_agx(&b, nir_imm_int(&b, 0)); /* a handle alone accesses nothing */
   assert(finish(&b) == none);
   b = begin();
   nir_txf(&b, nir_imm_int(&b, 0), .dim = GLSL_SAMPLER_DIM_BUF, .dest_type = nir_type_uint32);
   assert(finish(&b) == AGX_WIN32_CDM_NO_IMAGE_WRITES);
   b = begin();
   nir_def *h = nir_bindless_image_agx(&b, nir_imm_int(&b, 0));
   nir_def *c = nir_imm_ivec4(&b, 1, 0, 0, 0), *z = nir_imm_int(&b, 0);
   nir_bindless_image_load(&b, 4, 32, h, c, z, z, .image_dim = GLSL_SAMPLER_DIM_BUF);
   assert(finish(&b) == AGX_WIN32_CDM_NO_IMAGE_WRITES);
   b = begin();
   h = nir_bindless_image_agx(&b, nir_imm_int(&b, 0));
   c = nir_imm_ivec4(&b, 1, 0, 0, 0); z = nir_imm_int(&b, 0);
   nir_bindless_image_store(&b, h, c, z, nir_imm_ivec4(&b, 1, 2, 3, 4), z,
                                  .image_dim = GLSL_SAMPLER_DIM_BUF);
   assert(finish(&b) == AGX_WIN32_CDM_NO_TEXTURE_READS); /* a PBE write, no read */
   b = begin();
   h = nir_bindless_image_agx(&b, nir_imm_int(&b, 0));
   c = nir_imm_ivec4(&b, 1, 0, 0, 0); z = nir_imm_int(&b, 0);
   nir_bindless_image_atomic(&b, 32, h, c, z, nir_imm_int(&b, 1),
                                   .image_dim = GLSL_SAMPLER_DIM_BUF,
                                   .atomic_op = nir_atomic_op_iadd);
   assert(finish(&b) == 0);
   b = begin();
   nir_image_load(&b, 4, 32, nir_imm_int(&b, 0), nir_imm_ivec4(&b, 0, 0, 0, 0),
                        nir_imm_int(&b, 0), nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_2D);
   assert(finish(&b) == AGX_WIN32_CDM_NO_IMAGE_WRITES);
   glsl_type_singleton_decref();
   puts("classifier PASS");
   return 0;
}
'''


def pack_header(directory):
    mesa = local_tree('reference', 'mesa')
    genxml = mesa / 'src/asahi/genxml'
    header = subprocess.run(['python3', str(genxml / 'gen_pack.py'), str(genxml / 'cmdbuf.xml'),
                             str(genxml / 'agx_pack_header.h')], capture_output=True, text=True, check=True)
    (Path(directory) / 'agx_pack.h').write_text(header.stdout)


@unittest.skipUnless(local_tree('reference', 'mesa') and
                     local_tree('accelerated-desktop-ad03', 'mesa-build'),
                     'pinned Mesa reference and host build not present')
class LeanCdmBarrier(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = project(Path(cls.tmp.name) / 'native')
        asahi = cls.out / 'src/gallium/drivers/asahi'
        cls.state = (asahi / 'agx_state.c').read_text()
        cls.header = (asahi / 'agx_state.h').read_text()
        cls.batch = (asahi / 'agx_batch.c').read_text()
        cls.pipe = (asahi / 'agx_pipe.c').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def stream(self, value, bits=None):
        """The harness's barrier words, receipt and identity for one setting."""
        if not hasattr(self, '_exe'):
            mesa = local_tree('reference', 'mesa')
            d = Path(self.tmp.name) / 'harness'
            d.mkdir(exist_ok=True)
            pack_header(d)
            internal = function(self.state, 'static void\nagx_launch_internal(')
            finish = function(self.state, 'void\nagx_win32_cdm_batch_finish(')
            (d / 't.c').write_text(HARNESS.replace('LAUNCH_INTERNAL', internal).replace('FINISH', finish))
            exe = d / 't'
            r = subprocess.run(['clang', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                                '-Wno-c23-extensions', '-Wno-unused-parameter',
                                '-fsanitize=address,undefined', '-DHAVE_PTHREAD=1', '-DHAVE_TIMESPEC_GET=1',
                                '-DHAVE_STRUCT_TIMESPEC=1', '-I', str(d), '-I', str(WINSYS),
                                '-I', str(mesa / 'src/asahi/libagx'), '-I', str(mesa / 'src'),
                                '-I', str(mesa / 'include'), '-I', str(mesa / 'src/asahi/genxml'),
                                str(d / 't.c'), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr[-4000:])
            type(self)._exe = exe
        env = {k: v for k, v in os.environ.items() if not k.startswith('APPLE_AGX_D3D12_LEAN_CDM_')}
        if value is not None:
            env['APPLE_AGX_D3D12_LEAN_CDM_BARRIER'] = value
        if bits is not None:
            env['APPLE_AGX_D3D12_LEAN_CDM_BITS'] = bits
        r = subprocess.run([str(self._exe)], capture_output=True, text=True, env=env)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        lines = r.stdout.splitlines()
        identical = lines[0] == 'identical 1'
        receipt = tuple(map(int, re.findall(r'\d+', lines[1])))
        words = [int(w, 16) for w in lines[2].split()[1:]]
        return identical, receipt, words

    def test_default_stream_is_byte_identical(self):
        """Switch unset, 0 or not exactly 1/2: Asahi's stream, byte for byte,
        and a full barrier after every launch, whatever the bits override."""
        for value in (None, '0', '', '3', '12', '1 ', 'on'):
            for bits in (None, '0x168'):
                with self.subTest(value=value, bits=bits):
                    identical, receipt, words = self.stream(value, bits)
                    self.assertTrue(identical)
                    self.assertEqual(words, [FULL] * 9)
                    self.assertEqual(receipt, (0, 0, 0, 9))

    def test_mode1_lean_between_any_application_dispatches(self):
        """Only the precompiled launch (both sides) and the stream end keep
        the full barrier."""
        identical, receipt, words = self.stream('1')
        self.assertFalse(identical)
        self.assertEqual(words, [LEAN, FULL, FULL, LEAN, LEAN, LEAN, LEAN, LEAN, FULL])
        self.assertEqual(receipt, (1, LEAN, 6, 3))

    def test_mode2_lean_only_between_memory_dispatches(self):
        """A-B lean; B-C (precomp), C-D, D-E (texture reader) full; E-F
        lean (E writes no image, F reads no texture); F-G full (F writes
        images); G-H full (indirect grid); H-I lean; I full (stream end)."""
        identical, receipt, words = self.stream('2')
        self.assertFalse(identical)
        self.assertEqual(words, [LEAN, FULL, FULL, FULL, LEAN, FULL, FULL, LEAN, FULL])
        self.assertEqual(receipt, (2, LEAN, 3, 6))

    def test_bits_override_bisects_the_lean_word(self):
        """APPLE_AGX_D3D12_LEAN_CDM_BITS replaces bits 0..19 of the lean word
        only: 0xfffff gives back Asahi's stream; invalid values keep the
        default lean word."""
        identical, receipt, words = self.stream('1', '0xfffff')
        self.assertTrue(identical)
        self.assertEqual(receipt, (1, FULL, 6, 3))
        identical, receipt, words = self.stream('1', '0x16f')
        self.assertEqual(words, [0x6000016f, FULL, FULL] + [0x6000016f] * 5 + [FULL])
        identical, receipt, words = self.stream('2', '367')
        self.assertEqual(words[0], 0x6000016f)
        for bits in ('0x100000', 'zz', '', '0x168 ', '-1'):
            with self.subTest(bits=bits):
                identical, receipt, words = self.stream('1', bits)
                self.assertEqual(words, [LEAN, FULL, FULL, LEAN, LEAN, LEAN, LEAN, LEAN, FULL])

    def test_lean_word_is_the_batch_start_barrier(self):
        """The lean word is the one Asahi already emits at the start of every
        G13G compute batch (agx_batch_init_state), not a new encoding."""
        init = function(self.state, 'void\nagx_batch_init_state(')
        block = init[init.index('agx_push(out, CDM_BARRIER, cfg) {'):]
        block = block[:block.index('return;')]
        for field in ('cfg.usc_cache_inval = true;', 'cfg.unk_5 = true;', 'cfg.unk_6 = true;',
                      'cfg.unk_8 = true;'):
            self.assertIn(field, block)
        self.assertEqual(LEAN, (3 << 29) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8))

    def test_projection_placement(self):
        """Only agx_launch's final launch carries the class, which only
        agx_launch_grid sets, never for a compute blit; the batch init
        clears the provisional barrier; agx_flush_compute writes the full
        barrier before the terminator; new fields are appended."""
        launch = function(self.state, 'void\nagx_launch(')
        self.assertTrue(launch.rstrip().endswith(
            'ctx->windows_cdm_launch_class = ctx->windows_cdm_app_class;\n'
            '   agx_launch_internal(batch, grid, wg, launch, stage, usc);\n'
            '   ctx->windows_cdm_launch_class = 0;\n}'))
        grid = function(self.state[self.state.index('static void\nagx_launch_grid('):],
                        'static void\nagx_launch_grid(')
        self.assertIn('ctx->windows_cdm_app_class = ctx->compute_blitter.active ? 0u :\n'
                      '      AGX_WIN32_CDM_APP | uncompiled->info.windows_cdm_class;\n'
                      '   agx_launch(batch, grid, wg, cs, NULL, MESA_SHADER_COMPUTE,', grid)
        self.assertIn('   ctx->windows_cdm_app_class = 0;', grid)
        self.assertEqual(self.state.count('ctx->windows_cdm_app_class ='), 2)
        precomp = function(self.state, 'void\nagx_launch_precomp(')
        self.assertNotIn('windows_cdm', precomp)
        self.assertEqual(self.state.count('agx_cdm_barrier(out, dev->chip)'), 0,
                         'every launch barrier goes through agx_win32_cdm_after_launch')
        shader = function(self.state, 'static void\nagx_shader_initialize(')
        self.assertIn('so->info.windows_cdm_class = agx_win32_cdm_shader_class(nir);', shader)
        init = function(self.batch, 'void\nagx_batch_init(')
        self.assertIn('batch->windows_cdm_lean = NULL;', init)
        flush = function(self.pipe, 'static void\nagx_flush_compute(')
        self.assertLess(flush.index('agx_win32_cdm_batch_finish(batch);'),
                        flush.index('CDM_STREAM_TERMINATE'))
        batch = self.header[self.header.index('struct agx_batch {'):]
        batch = batch[:batch.index('\n};')]
        self.assertTrue(batch.endswith('unsigned windows_cdm_lean_writer;'))
        context = self.header[self.header.index('\nstruct agx_context {'):]
        context = context[:context.index('\n};')]
        self.assertTrue(context.endswith('uint32_t windows_cdm_barriers[4];'))
        self.assertIn('void agx_win32_cdm_batch_finish(struct agx_batch *batch);', self.header)

    def test_projection_is_idempotent_guarded(self):
        """A second projection over projected sources must refuse."""
        spec = importlib.util.spec_from_file_location('lean_cdm_again', SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for path, transform in module.FILES.items():
            with self.subTest(path=path):
                with self.assertRaises(RuntimeError):
                    transform((self.out / path).read_text())

    def test_shader_classifier(self):
        """Texture instructions and image accesses other than plain stores
        are texture reads; image stores and atomics are image writes; plain
        memory and the bindless handle producer are neither."""
        from test_dxbc_to_nir import host_flags, HOST_LIBS, BREW_LINK
        from test_dxil_to_nir import MESA_LIBS
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        mesa = local_tree('reference', 'mesa')
        d = Path(self.tmp.name) / 'classifier'
        d.mkdir(exist_ok=True)
        (d / 'c.c').write_text(CLASSIFIER)
        libs = [str(build / p) for p in MESA_LIBS]
        r = subprocess.run(['clang', '-std=gnu11', '-Wall', '-Werror', '-Wno-unused-function',
                            *host_flags(build), '-I', str(WINSYS), '-I', str(mesa / 'src'),
                            '-I', str(build / 'src'), str(d / 'c.c'), '-o', str(d / 'c'), *libs,
                            '-L/opt/homebrew/opt/zstd/lib', '-lzstd', '-lz', '-lpthread', *HOST_LIBS,
                            *BREW_LINK], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-4000:])
        r = subprocess.run([str(d / 'c')], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('classifier PASS', r.stdout)

    def test_projected_pipe_unit_declares_the_finish(self):
        """agx_pipe.c calls a function injected into agx_state.c: it must be
        declared through agx_state.h (implicit declarations are build
        errors on the Windows toolchain)."""
        from test_dxbc_to_nir import host_flags
        from test_d3d12_indirect_stream import WINSYS as winsys
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        p = Path(self.tmp.name) / 'pipe'
        p.mkdir(exist_ok=True)
        (p / 'xf86drm.h').write_text('#pragma once\nint drmSyncobjImportSyncFile(int, unsigned, int);\n')
        includes = [p, self.out / 'src', self.out / 'include', self.out / 'include/drm-uapi',
                    self.out / 'src/asahi/lib', self.out / 'src/asahi/libagx', self.out / 'src/asahi/layout',
                    self.out / 'src/asahi/compiler', self.out / 'src/gallium/drivers/asahi',
                    build / 'src/asahi', build / 'src/asahi/genxml', build / 'src/asahi/libagx',
                    build / 'src/poly', build / 'src/compiler/nir', mesa / 'src/gallium/auxiliary',
                    mesa / 'src/gallium/include', mesa / 'src/virtio/vdrm', winsys,
                    ROOT / 'drivers/apple-agx/shared/include']
        r = subprocess.run(['clang', '-std=c11', '-DAPPLE_AGX_GPUVA_WINSYS', '-Wno-c23-extensions',
                            '-Werror=implicit-function-declaration', '-include', str(p / 'xf86drm.h'),
                            *['-I' + str(i) for i in includes], *host_flags(build), '-fsyntax-only',
                            str(self.out / 'src/gallium/drivers/asahi/agx_pipe.c')],
                           capture_output=True, text=True)
        # agx_pipe.c has unrelated host-only gaps (_strnicmp, renderonly);
        # none may involve this projection.
        self.assertNotIn('agx_win32_cdm', r.stderr)
        self.assertNotIn('windows_cdm', r.stderr)
        self.assertNotIn('fatal error', r.stderr)


if __name__ == '__main__':
    unittest.main()
