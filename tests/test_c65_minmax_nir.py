"""C65: the min/max reduction lowering builds valid NIR of the right shape.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? dxbc_minmax_wrap rewrites
a sample's uses to a run-time choice between the hardware sample and the
min/max emulation. A malformed instruction (wrong source count, a lod query
with array coordinates, a phi that does not dominate) only fails inside the
AGX compiler on hardware; a use left on the raw sample silently ignores the
sampler's reduction; a sample it cannot emulate exactly (shadow, 3D, texel
offsets) must be left alone. Built against the pinned host libnir with
validation.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_dxbc_to_nir import local_tree, host_flags, ROOT, DXBC

TEST = r'''
#include "nir.h"
#include "nir_builder.h"
#include "dxbc_minmax.h"
#include <assert.h>
#include <stdio.h>

static const nir_shader_compiler_options options = {0};

static nir_tex_instr *sample(nir_builder *b, nir_texop op, enum glsl_sampler_dim dim, bool array, bool shadow) {
   const unsigned coords = (dim == GLSL_SAMPLER_DIM_3D ? 3 : 2) + (array ? 1 : 0);
   nir_tex_instr *t = nir_tex_instr_create(b->shader, op == nir_texop_txl ? 4 : 3);
   t->op = op; t->sampler_dim = dim; t->is_array = array; t->is_shadow = shadow;
   t->coord_components = coords; t->dest_type = nir_type_float32;
   nir_def *c[3] = {nir_imm_float(b, 0.3f), nir_imm_float(b, 0.6f), nir_imm_float(b, 1.0f)};
   t->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, nir_vec(b, c, coords));
   t->src[1] = nir_tex_src_for_ssa(nir_tex_src_texture_handle, nir_imm_int(b, 1));
   t->src[2] = nir_tex_src_for_ssa(nir_tex_src_sampler_handle, nir_imm_intN_t(b, 2, 16));
   if (op == nir_texop_txl) t->src[3] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_float(b, 1.5f));
   nir_def_init(&t->instr, &t->def, 4, 32);
   nir_builder_instr_insert(b, &t->instr);
   return t;
}

static unsigned count_tex(nir_shader *s, nir_texop op) {
   unsigned n = 0;
   nir_foreach_function_impl(impl, s)
      nir_foreach_block(block, impl)
         nir_foreach_instr(instr, block)
            if (instr->type == nir_instr_type_tex && nir_instr_as_tex(instr)->op == op) ++n;
   return n;
}

static void check(nir_texop op, bool array) {
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, &options, "minmax");
   /* the map word is read before the sample, as the frontends do */
   nir_def *word = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 0), .range = 4);
   nir_def *bits = nir_iand_imm(&b, nir_ushr_imm(&b, word, 11), 31);
   nir_tex_instr *t = sample(&b, op, GLSL_SAMPLER_DIM_2D, array, false);
   nir_def *use = nir_fneg(&b, &t->def);
   nir_def *phi = dxbc_minmax_wrap(&b, t, bits);
   assert(phi && nir_def_instr(phi)->type == nir_instr_type_phi);
   nir_validate_shader(b.shader, "minmax");
   /* the use now reads the phi, which reads the raw sample */
   nir_instr *user = nir_def_instr(use);
   nir_def *src = nir_instr_as_alu(user)->src[0].src.ssa;
   assert(nir_def_instr(src)->type == nir_instr_type_phi);
   /* two levels x (4 texels + the point sample), one size per level, the
    * level count, and (implicit LOD) one lod query */
   assert(count_tex(b.shader, nir_texop_txl) == 10u + (op == nir_texop_txl ? 1u : 0u));
   assert(count_tex(b.shader, nir_texop_txs) == 2u);
   assert(count_tex(b.shader, nir_texop_query_levels) == 1u);
   assert(count_tex(b.shader, nir_texop_lod) == (op == nir_texop_tex ? 1u : 0u));
   ralloc_free(b.shader);
}

static void refused(enum glsl_sampler_dim dim, bool shadow) {
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, &options, "minmax");
   nir_tex_instr *t = sample(&b, nir_texop_tex, dim, false, shadow);
   assert(!dxbc_minmax_wrap(&b, t, nir_imm_int(&b, 2)));
   assert(count_tex(b.shader, nir_texop_txl) == 0u);
   ralloc_free(b.shader);
}

/* C65S: slot samplers (sampler_index, no handle) with constant bits */
static nir_tex_instr *slot_sample(nir_builder *b, unsigned slot) {
   nir_tex_instr *t = nir_tex_instr_create(b->shader, 1);
   t->op = nir_texop_tex; t->sampler_dim = GLSL_SAMPLER_DIM_2D; t->coord_components = 2;
   t->dest_type = nir_type_float32; t->texture_index = slot; t->sampler_index = slot;
   t->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, nir_vec2(b, nir_imm_float(b, 0.3f), nir_imm_float(b, 0.6f)));
   nir_def_init(&t->instr, &t->def, 4, 32);
   nir_builder_instr_insert(b, &t->instr);
   return t;
}

static void slots(void) {
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, &options, "minmax-slots");
   unsigned char bits[DXBC_MINMAX_SLOTS] = {0};
   bits[3] = 2u | 4u | 8u | 16u; /* MINIMUM_MIN_MAG_MIP_LINEAR */
   bits[5] = 1u | 4u | 8u;       /* comparison: left alone */
   nir_tex_instr *wrapped = slot_sample(&b, 3), *plain = slot_sample(&b, 5), *heap = sample(&b, nir_texop_tex, GLSL_SAMPLER_DIM_2D, false, false);
   heap->sampler_index = 3; /* a heap sample (sampler handle) is the frontends' to wrap */
   nir_def *uses[3] = {nir_fneg(&b, &wrapped->def), nir_fneg(&b, &plain->def), nir_fneg(&b, &heap->def)};
   assert(dxbc_minmax_wrap_slots(b.shader, bits));
   nir_validate_shader(b.shader, "minmax-slots");
   assert(nir_def_instr(nir_instr_as_alu(nir_def_instr(uses[0]))->src[0].src.ssa)->type == nir_instr_type_phi);
   assert(nir_instr_as_alu(nir_def_instr(uses[1]))->src[0].src.ssa == &plain->def);
   assert(nir_instr_as_alu(nir_def_instr(uses[2]))->src[0].src.ssa == &heap->def);
   assert(count_tex(b.shader, nir_texop_txl) == 10u && count_tex(b.shader, nir_texop_lod) == 1u);
   /* no slot with reduction 2/3: nothing to do */
   unsigned char none[DXBC_MINMAX_SLOTS] = {0};
   none[2] = 1u;
   assert(!dxbc_minmax_wrap_slots(b.shader, none));
   ralloc_free(b.shader);
}

int main(void) {
   glsl_type_singleton_init_or_ref();
   slots();
   check(nir_texop_tex, false);
   check(nir_texop_txl, false);
   check(nir_texop_tex, true);
   refused(GLSL_SAMPLER_DIM_3D, false);
   refused(GLSL_SAMPLER_DIM_2D, true);
   /* the map-word bits of D3D12 filters */
   assert(dxbc_minmax_bits(0x155) == (2u | 4u | 8u | 16u)); /* MINIMUM_MIN_MAG_MIP_LINEAR */
   assert(dxbc_minmax_bits(0x180) == 3u);                   /* MAXIMUM_MIN_MAG_MIP_POINT */
   assert(dxbc_minmax_bits(0x1d5) == (3u | 4u | 8u | 16u)); /* MAXIMUM_ANISOTROPIC */
   assert(dxbc_minmax_bits(0x14) == (4u | 8u));             /* MIN_MAG_LINEAR_MIP_POINT */
   puts("c65 minmax nir: PASS");
   return 0;
}
'''


class C65MinmaxNir(unittest.TestCase):
    def test_lowering_builds_valid_nir(self):
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        mesa = local_tree('reference', 'mesa')
        if not build or not mesa or not (build / 'src/compiler/nir/libnir.a').exists():
            self.skipTest('host Mesa/libnir not available')
        libs = [str(build / p) for p in ('src/compiler/nir/libnir.a', 'src/compiler/libcompiler.a',
                                         'src/util/libmesa_util.a', 'src/util/libmesa_util_simd.a',
                                         'src/c11/impl/libmesa_util_c11.a', 'src/util/blake3/libblake3.a')]
        with tempfile.TemporaryDirectory(prefix='c65-minmax-') as tmp:
            src = Path(tmp) / 'minmax.c'
            exe = Path(tmp) / 'minmax'
            src.write_text(TEST)
            built = subprocess.run(['clang', '-std=c11', '-Werror=implicit-function-declaration', *host_flags(build),
                                    '-I', str(mesa / 'src'), '-I', str(build / 'src'), '-I', str(DXBC), str(src),
                                    str(DXBC / 'dxbc_minmax.c'), '-o', str(exe), *libs,
                                    '-L/opt/homebrew/opt/zstd/lib', '-lzstd', '-lz', '-lpthread'],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr[-4000:])
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr[-4000:])
            self.assertIn('c65 minmax nir: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
