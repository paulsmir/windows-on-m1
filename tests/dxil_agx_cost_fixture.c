/* RC74: offline AGX cost of a D3D12 compute shader.  Translates a DXIL
 * program part or container with agx_dxil_to_nir (the owner's slot limits),
 * runs the steps Gallium Asahi applies to a D3D12 owner shader
 * (agx_state.c agx_shader_initialize with the Windows D3D robustness of the
 * native projection, then agx_compile_nir) and compiles it with the pinned
 * AGX backend.  The Gallium-only binding and sysval lowerings are replaced
 * by stand-ins with the same shape (uniform loads for buffer bases, sizes
 * and texture handles), so instruction counts, GPRs, scratch and spills are
 * the backend's own; uniform numbering is not.
 *
 * usage: dxil_agx_cost_fixture <dxil>|--synthetic-uav-reads=K|--synthetic-local-array=N
 *        [--no-d3d-robust] [--assume-in-bounds] [--root-views=UAV,CBV]
 *        [--print-lowered] [--print-nir]
 * --root-views: hex masks of the UAV and constant-buffer slots that are
 * D3D12 root descriptors (RC75 dxbc_nir_mark_root_views: left unchecked) 
 * prints one line:
 *   COST stage <s> block <x>x<y>x<z> shared <B> nir-ifs <n> guarded-loads <n>
 *        loads <n> stores <n> txf <n> mem-blocks <n> hot-mem-blocks <n>
 *        hot-nir <n> hot-branches <n> instrs <n> alu <n> fscib <n> ic <n>
 *        gprs <n> threads <n> scratch <n> spills <n> fills <n> loops <n>
 *        code <n> preamble <n> nir-alu <n> nir-mem <n> nir-tex <n>
 *        nir-shared <n> barriers <n> atomics <n> root-marked <n>
 * (RC75 op classes of the final NIR handed to the backend: ALU, device
 * memory, texture/image, threadgroup memory, workgroup barriers, atomics)
 * or "REFUSED ..." when the translation refuses; a graphics stage prints
 * "NIRCOST stage <s> hot-mem-blocks <n> hot-nir <n> hot-branches <n>", the
 * D3D lowering's in-bounds path before Gallium's I/O lowering. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agx_dxil_to_nir.h"
#include "dxbc_to_nir.h"

#include "nir.h"
#include "nir_builder.h"
#include "asahi/compiler/agx_compile.h"
#include "asahi/compiler/agx_nir.h"

#include "agx_win32_d3d_robustness.h"

/* Gallium agx_state.c should_lower_robustness for a robust context, minus
 * UBO/SSBO accesses (agx_win32_should_lower_robustness_d3d) */
static bool
gl_part_of_d3d_robustness(const nir_intrinsic_instr *intr, const void *data)
{
   (void)data;
   switch (intr->intrinsic) {
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
      return nir_intrinsic_image_dim(intr) == GLSL_SAMPLER_DIM_BUF;
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_image_atomic_swap:
      return true;
   default:
      return false;
   }
}

/* stand-in for Gallium agx_nir_lower_bindings: bound images to texture
 * state registers (reads at 2i + textures, writes at 2i + textures + 1),
 * textures past the state registers or needing a descriptor crawl bindless */
static bool
bind_resources(nir_builder *b, nir_instr *instr, void *data)
{
   (void)data;
   const bool crawl = agx_nir_needs_texture_crawl(instr);
   b->cursor = nir_before_instr(instr);
   if (instr->type == nir_instr_type_tex) {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      if (nir_tex_instr_src_index(tex, nir_tex_src_texture_handle) >= 0)
         return false;
      if (tex->texture_index < 16 && nir_tex_instr_src_index(tex, nir_tex_src_texture_offset) < 0 && !crawl)
         return false;
      nir_def *index = nir_steal_tex_src(tex, nir_tex_src_texture_offset);
      if (!index)
         index = nir_imm_int(b, (int)tex->texture_index);
      nir_tex_instr_add_src(tex, nir_tex_src_texture_handle, nir_load_texture_handle_agx(b, index));
      return true;
   }
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   switch (intr->intrinsic) {
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
   case nir_intrinsic_image_size:
   case nir_intrinsic_image_samples:
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_image_atomic_swap:
      break;
   default:
      return false;
   }
   unsigned offset = BITSET_LAST_BIT(b->shader->info.textures_used);
   if (intr->intrinsic != nir_intrinsic_image_load && intr->intrinsic != nir_intrinsic_image_size &&
       intr->intrinsic != nir_intrinsic_image_samples)
      offset++;
   nir_scalar index = nir_scalar_resolved(intr->src[0].ssa, 0);
   if (nir_scalar_is_const(index) && !crawl && nir_scalar_as_uint(index) * 2 + offset < 16) {
      nir_src_rewrite(&intr->src[0], nir_imm_intN_t(b, (int)(nir_scalar_as_uint(index) * 2 + offset), 16));
      return true;
   }
   nir_def *slot = nir_iadd_imm(b, nir_imul_imm(b, nir_u2u32(b, intr->src[0].ssa), 2), offset);
   nir_rewrite_image_intrinsic(intr, nir_load_texture_handle_agx(b, slot), nir_image_intrinsic_type_bindless);
   return true;
}

/* uniforms: 64-bit bases at 16 + 4k, 32-bit values at 144 + 2k (k = 16 table + slot) */
static nir_def *
uniform64(nir_builder *b, nir_def *index, unsigned table)
{
   if (nir_src_is_const(nir_src_for_ssa(index)))
      return nir_load_preamble(b, 1, 64, .base = 16 + 4 * (table * 16 + nir_src_as_uint(nir_src_for_ssa(index))));
   nir_def *base = nir_load_preamble(b, 1, 64, .base = 12);
   return nir_load_global_constant(b, 1, 64, nir_iadd(b, base, nir_u2u64(b, nir_imul_imm(b, index, 8))),
                                   .align_mul = 8);
}

static nir_def *
uniform32(nir_builder *b, nir_def *index, unsigned table)
{
   if (nir_src_is_const(nir_src_for_ssa(index)))
      return nir_load_preamble(b, 1, 32, .base = 144 + 2 * (table * 16 + nir_src_as_uint(nir_src_for_ssa(index))));
   nir_def *base = nir_load_preamble(b, 1, 64, .base = 12);
   return nir_load_global_constant(b, 1, 32, nir_iadd(b, base, nir_u2u64(b, nir_imul_imm(b, index, 4))),
                                   .align_mul = 4);
}

/* stand-in for Gallium agx_nir_lower_sysvals */
static bool
lower_sysvals(nir_builder *b, nir_instr *instr, void *data)
{
   (void)data;
   b->cursor = nir_before_instr(instr);
   if (instr->type == nir_instr_type_tex) {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      if (tex->op != nir_texop_lod_bias)
         return false;
      const int offset = nir_tex_instr_src_index(tex, nir_tex_src_sampler_offset);
      nir_def *index = offset >= 0 ? tex->src[offset].src.ssa : nir_imm_int(b, (int)tex->sampler_index);
      nir_def_replace(&tex->def, nir_u2u16(b, uniform32(b, index, 2)));
      return true;
   }
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   nir_def *value = NULL;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_ubo: {
      nir_def *base = uniform64(b, intr->src[0].ssa, 0);
      value = nir_load_global_constant(b, intr->num_components, intr->def.bit_size,
                                       nir_iadd(b, base, nir_u2u64(b, intr->src[1].ssa)),
                                       .align_mul = nir_intrinsic_align(intr));
      break;
   }
   case nir_intrinsic_load_ssbo_address:
      value = uniform64(b, intr->src[0].ssa, 1);
      break;
   case nir_intrinsic_get_ubo_size:
      value = uniform32(b, intr->src[0].ssa, 0);
      break;
   case nir_intrinsic_get_ssbo_size:
      value = uniform32(b, intr->src[0].ssa, 1);
      break;
   case nir_intrinsic_load_texture_handle_agx:
      value = nir_bindless_image_agx(b, nir_imul_imm(b, nir_u2u32(b, intr->src[0].ssa), 24), .desc_set = 8);
      break;
   case nir_intrinsic_load_sampler_handle_agx:
      value = nir_u2u16(b, uniform32(b, intr->src[0].ssa, 2));
      break;
   case nir_intrinsic_load_num_workgroups:
      value = nir_load_preamble(b, 3, 32, .base = 0);
      break;
   case nir_intrinsic_bindless_image_agx:
      /* D3D12 C47 heap handles (DXBC_HEAP_HANDLE_MARK): the heap mirror */
      if (nir_intrinsic_desc_set(intr) != DXBC_HEAP_HANDLE_MARK)
         return false;
      nir_intrinsic_set_desc_set(intr, 8);
      return true;
   default:
      return false;
   }
   nir_def_replace(&intr->def, value);
   return true;
}

struct counts {
   unsigned ifs, guarded_loads, loads, stores, txf;
};

/* blocks issuing a device memory access: the AGX backend waits for every
 * outstanding access at the end of each one (agx_insert_waits.c), so this
 * bounds how many accesses of one invocation overlap */
static unsigned
memory_blocks(nir_shader *nir)
{
   unsigned blocks = 0;
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            const nir_intrinsic_op op = nir_instr_as_intrinsic(instr)->intrinsic;
            if (op == nir_intrinsic_load_global || op == nir_intrinsic_load_global_constant ||
                op == nir_intrinsic_store_global) {
               blocks++;
               break;
            }
         }
      }
   }
   return blocks;
}

/* RC75: what the final NIR spends its instructions on */
struct classes {
   unsigned alu, mem, tex, shared, barriers, atomics;
};

static void
count_classes(nir_shader *nir, struct classes *c)
{
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type == nir_instr_type_alu) {
               const nir_op op = nir_instr_as_alu(instr)->op;
               c->alu += op != nir_op_mov && op != nir_op_vec2 && op != nir_op_vec3 && op != nir_op_vec4;
               continue;
            }
            if (instr->type == nir_instr_type_tex) {
               c->tex++;
               continue;
            }
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            switch (intr->intrinsic) {
            case nir_intrinsic_load_global:
            case nir_intrinsic_load_global_constant:
            case nir_intrinsic_store_global:
            case nir_intrinsic_load_constant_agx:
               c->mem++;
               break;
            case nir_intrinsic_global_atomic:
            case nir_intrinsic_global_atomic_swap:
            case nir_intrinsic_global_atomic_agx:
            case nir_intrinsic_global_atomic_swap_agx:
               c->mem++;
               c->atomics++;
               break;
            case nir_intrinsic_load_shared:
            case nir_intrinsic_store_shared:
               c->shared++;
               break;
            case nir_intrinsic_shared_atomic:
            case nir_intrinsic_shared_atomic_swap:
               c->shared++;
               c->atomics++;
               break;
            case nir_intrinsic_barrier:
               c->barriers += nir_intrinsic_execution_scope(intr) >= SCOPE_WORKGROUP;
               break;
            default: {
               const char *name = nir_intrinsic_infos[intr->intrinsic].name;
               if (strstr(name, "image")) {
                  c->tex++;
                  c->atomics += strstr(name, "atomic") != NULL;
               }
               break;
            }
            }
         }
      }
   }
}

/* RC75: root-descriptor accesses the frontend marked */
static unsigned
count_marked(nir_shader *nir)
{
   unsigned marked = 0;
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            marked += (intr->intrinsic == nir_intrinsic_load_ubo || intr->intrinsic == nir_intrinsic_load_ssbo ||
                       intr->intrinsic == nir_intrinsic_store_ssbo) &&
                      (nir_intrinsic_access(intr) & ACCESS_IN_BOUNDS);
         }
      }
   }
   return marked;
}

static void
count_cf(struct exec_list *list, unsigned depth, struct counts *c)
{
   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block: {
         nir_block *block = nir_cf_node_as_block(node);
         nir_foreach_instr(instr, block) {
            if (instr->type == nir_instr_type_tex) {
               if (nir_instr_as_tex(instr)->op == nir_texop_txf)
                  c->txf++;
               continue;
            }
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            const bool load = intr->intrinsic == nir_intrinsic_load_ssbo;
            const bool store = intr->intrinsic == nir_intrinsic_store_ssbo;
            c->loads += load;
            c->stores += store;
            c->guarded_loads += load && depth;
         }
         break;
      }
      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(node);
         c->ifs++;
         count_cf(&nif->then_list, depth + 1, c);
         count_cf(&nif->else_list, depth + 1, c);
         break;
      }
      case nir_cf_node_loop:
         /* a loop body is not a guard */
         count_cf(&nir_cf_node_as_loop(node)->body, depth, c);
         break;
      default:
         break;
      }
   }
}

static bool
is_buffer_size(nir_def *def)
{
   nir_instr *instr = nir_def_instr(def);
   return instr->type == nir_instr_type_intrinsic &&
          nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_get_ssbo_size;
}

/* the bounds test of a D3D-robustness branch as agx_win32_d3d_in_bounds
 * builds it, ult(offset, bcsel(uge(size, bytes), size + 1 - bytes, 0)) (or
 * the D3D-H6 uge(size, bytes) & uge(size - bytes, offset)), or an and of
 * such tests (a run of stores); a shader's own branch on a loaded value is
 * not one */
static bool
reads_buffer_size(nir_def *def, unsigned depth)
{
   nir_instr *instr = nir_def_instr(def);
   if (depth && instr->type == nir_instr_type_load_const)
      return true; /* the run test's initial true */
   if (instr->type != nir_instr_type_alu || depth > 64)
      return false;
   nir_alu_instr *alu = nir_instr_as_alu(instr);
   if (alu->op == nir_op_iand)
      return reads_buffer_size(alu->src[0].src.ssa, depth + 1) && reads_buffer_size(alu->src[1].src.ssa, depth + 1);
   if (alu->op == nir_op_uge) {
      /* the D3D-H6 form: uge(size, bytes) and uge(size - bytes, offset) */
      nir_instr *lhs = nir_def_instr(alu->src[0].src.ssa);
      if (is_buffer_size(alu->src[0].src.ssa))
         return true;
      return lhs->type == nir_instr_type_alu &&
             (nir_instr_as_alu(lhs)->op == nir_op_isub || nir_instr_as_alu(lhs)->op == nir_op_iadd) &&
             is_buffer_size(nir_instr_as_alu(lhs)->src[0].src.ssa);
   }
   if (alu->op != nir_op_ult)
      return false;
   nir_instr *limit = nir_def_instr(alu->src[1].src.ssa);
   if (limit->type != nir_instr_type_alu || nir_instr_as_alu(limit)->op != nir_op_bcsel)
      return false;
   nir_instr *test = nir_def_instr(nir_instr_as_alu(limit)->src[0].src.ssa);
   return test->type == nir_instr_type_alu && nir_instr_as_alu(test)->op == nir_op_uge &&
          is_buffer_size(nir_instr_as_alu(test)->src[0].src.ssa);
}

/* the path every in-bounds invocation takes: then-lists of bounds tests,
 * both sides of the shader's own branches */
struct hot {
   unsigned memory_blocks, instrs, branches; /* branches on a bounds test */
};

static void
count_hot(struct exec_list *list, struct hot *h)
{
   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block: {
         bool memory = false;
         nir_foreach_instr(instr, nir_cf_node_as_block(node)) {
            h->instrs++;
            if (instr->type == nir_instr_type_intrinsic) {
               const nir_intrinsic_op op = nir_instr_as_intrinsic(instr)->intrinsic;
               memory |= op == nir_intrinsic_load_ssbo || op == nir_intrinsic_store_ssbo;
            }
         }
         h->memory_blocks += memory;
         break;
      }
      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(node);
         count_hot(&nif->then_list, h);
         if (!reads_buffer_size(nif->condition.ssa, 0))
            count_hot(&nif->else_list, h);
         else
            h->branches++;
         break;
      }
      case nir_cf_node_loop:
         count_hot(&nir_cf_node_as_loop(node)->body, h);
         break;
      default:
         break;
      }
   }
}

/* --assume-in-bounds: every branch on a bounds test of the D3D lowering
 * takes its in-bounds side; the tests themselves stay (selects still
 * evaluate them), leaving the code an in-bounds invocation runs */
static bool
assume_in_bounds(nir_shader *nir)
{
   bool progress = false;
   nir_foreach_function_impl(impl, nir) {
      nir_builder b = nir_builder_create(impl);
      nir_foreach_block(block, impl) {
         nir_if *nif = nir_block_get_following_if(block);
         if (!nif || !reads_buffer_size(nif->condition.ssa, 0))
            continue;
         b.cursor = nir_before_cf_node(&nif->cf_node);
         nir_src_rewrite(&nif->condition, nir_imm_true(&b));
         progress = true;
      }
      nir_progress(progress, impl, nir_metadata_control_flow);
   }
   return progress;
}

/* --synthetic-uav-reads=K: the shape of GravityMark's structured-buffer
 * kernels (EXP1969 captures) without their bytecode: each invocation reads K
 * float4 fields of its 16K-byte element from raw UAV u0, sums them and
 * writes the float4 to u1 (dxil-spirv's offsets: element * stride + 16k) */
static nir_shader *
synthetic_uav_reads(unsigned reads)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &agx_nir_options, "rc74-uav-reads");
   b.shader->info.internal = false;
   b.shader->info.workgroup_size[0] = 64;
   b.shader->info.workgroup_size[1] = b.shader->info.workgroup_size[2] = 1;
   b.shader->info.num_ssbos = 2;
   nir_def *element = nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);
   nir_def *sum = nir_imm_vec4(&b, 0, 0, 0, 0);
   for (unsigned k = 0; k < reads; ++k) {
      nir_def *offset = nir_iadd_imm(&b, nir_amul_imm(&b, element, 16 * reads), 16 * k);
      sum = nir_fadd(&b, sum, nir_load_ssbo(&b, 4, 32, nir_imm_int(&b, 0), offset, .access = ACCESS_NON_WRITEABLE,
                                            .align_mul = 4, .align_offset = 0));
   }
   nir_store_ssbo(&b, sum, nir_imm_int(&b, 1), nir_amul_imm(&b, element, 16), .write_mask = 0xf,
                  .access = ACCESS_NON_READABLE, .align_mul = 4, .align_offset = 0);
   nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
   return b.shader;
}

/* --synthetic-local-array=N: an indexable temp of N dwords (DXBC x#, a
 * DXIL alloca) filled from u0 and read at a per-thread index, through the
 * frontend tail (dxbc_nir_finalize) as every translated shader */
static nir_shader *
synthetic_local_array(unsigned elements)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &agx_nir_options, "rc74-local-array");
   b.shader->info.internal = false; /* an application shader (agx_compile treats internal scratch specially) */
   b.shader->info.workgroup_size[0] = 64;
   b.shader->info.workgroup_size[1] = b.shader->info.workgroup_size[2] = 1;
   b.shader->info.num_ssbos = 2;
   nir_variable *array = nir_local_variable_create(b.impl, glsl_array_type(glsl_uint_type(), elements, 4), "x0");
   nir_def *thread = nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);
   nir_deref_instr *base = nir_build_deref_var(&b, array);
   for (unsigned i = 0; i < elements; ++i)
      nir_store_deref(&b, nir_build_deref_array_imm(&b, base, i),
                      nir_load_ssbo(&b, 1, 32, nir_imm_int(&b, 0), nir_imm_int(&b, (int)(4 * i)),
                                    .access = ACCESS_NON_WRITEABLE, .align_mul = 4, .align_offset = 0),
                      1);
   nir_def *index = nir_umod_imm(&b, thread, elements);
   nir_def *value = nir_load_deref(&b, nir_build_deref_array(&b, base, index));
   nir_store_deref(&b, nir_build_deref_array(&b, base, nir_iadd_imm(&b, index, 1)), thread, 1);
   value = nir_iadd(&b, value, nir_load_deref(&b, nir_build_deref_array(&b, base, nir_umod_imm(&b, nir_iadd_imm(&b, thread, 7), elements))));
   nir_store_ssbo(&b, value, nir_imm_int(&b, 1), nir_amul_imm(&b, thread, 4), .write_mask = 1,
                  .access = ACCESS_NON_READABLE, .align_mul = 4, .align_offset = 0);
   dxbc_nir_finalize(b.shader);
   return b.shader;
}

int
main(int argc, char **argv)
{
   bool d3d_robust = true, print_nir = false, print_lowered = false, in_bounds = false;
   unsigned root_uavs = 0, root_cbvs = 0;
   for (int a = 2; a < argc; ++a) {
      if (sscanf(argv[a], "--root-views=%x,%x", &root_uavs, &root_cbvs) == 2)
         continue;
      d3d_robust &= strcmp(argv[a], "--no-d3d-robust") != 0;
      print_nir |= !strcmp(argv[a], "--print-nir");
      print_lowered |= !strcmp(argv[a], "--print-lowered"); /* after the D3D lowering */
      in_bounds |= !strcmp(argv[a], "--assume-in-bounds");
   }
   glsl_type_singleton_init_or_ref();
   nir_shader *nir = NULL;
   void *bytes = NULL;
   unsigned reads = 0;
   if (argc > 1 && sscanf(argv[1], "--synthetic-uav-reads=%u", &reads) == 1 && reads && reads <= 64) {
      nir = synthetic_uav_reads(reads);
   } else if (argc > 1 && sscanf(argv[1], "--synthetic-local-array=%u", &reads) == 1 && reads && reads <= 256) {
      nir = synthetic_local_array(reads);
   } else {
      FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
      if (!f)
         return 2;
      fseek(f, 0, SEEK_END);
      const long size = ftell(f);
      fseek(f, 0, SEEK_SET);
      bytes = malloc((size_t)size);
      if (!bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size)
         return 2;
      fclose(f);
      /* the owner's capacities (agx_d3d12_windows.cpp owner_binding_limits) */
      static const uint32_t limits[DXBC_BINDING_KIND_COUNT] = {14, 128 | DXBC_BINDING_HEAP_RANGES,
                                                              8 | DXBC_BINDING_HEAP_RANGES,
                                                              16 | DXBC_BINDING_HEAP_RANGES};
      struct agx_dxil_nir_result r;
      if (agx_dxil_to_nir(bytes, (size_t)size, limits, &agx_nir_options, &r) != AGX_DXIL_NIR_OK) {
         printf("REFUSED %s\n", r.error);
         return 0;
      }
      nir = r.nir;
   }
   if (nir->info.stage != MESA_SHADER_COMPUTE) {
      /* other stages: the D3D lowering's in-bounds path only (no backend
       * compile: their I/O lowering is Gallium's) */
      if (d3d_robust)
         NIR_PASS(_, nir, agx_win32_lower_d3d_buffer_access);
      struct hot hot = {0};
      count_hot(&nir_shader_get_entrypoint(nir)->body, &hot);
      printf("NIRCOST stage %s hot-mem-blocks %u hot-nir %u hot-branches %u\n", mesa_shader_stage_name(nir->info.stage),
             hot.memory_blocks, hot.instrs, hot.branches);
      ralloc_free(nir);
      glsl_type_singleton_decref();
      free(bytes);
      return 0;
   }
   const unsigned block[3] = {nir->info.workgroup_size[0], nir->info.workgroup_size[1], nir->info.workgroup_size[2]};
   const unsigned shared = nir->info.shared_size;

   /* RC75: the owner marks root-descriptor slots after translation */
   dxbc_nir_mark_root_views(nir, root_cbvs, root_uavs);
   const unsigned marked = count_marked(nir);
   /* agx_shader_initialize (native projection, D3D context) */
   if (d3d_robust)
      NIR_PASS(_, nir, agx_win32_lower_d3d_buffer_access);
   if (in_bounds) {
      NIR_PASS(_, nir, assume_in_bounds);
      bool progress;
      do {
         progress = false;
         NIR_PASS(progress, nir, nir_opt_constant_folding);
         NIR_PASS(progress, nir, nir_opt_algebraic);
         NIR_PASS(progress, nir, nir_opt_dead_cf);
         NIR_PASS(progress, nir, nir_opt_remove_phis);
         NIR_PASS(progress, nir, nir_opt_dce);
      } while (progress);
   }
   if (print_lowered)
      nir_print_shader(nir, stdout);
   struct hot hot = {0};
   count_hot(&nir_shader_get_entrypoint(nir)->body, &hot);
   NIR_PASS(_, nir, nir_lower_robust_access, gl_part_of_d3d_robustness, NULL);
   NIR_PASS(_, nir, agx_nir_lower_texture_early, true);
   NIR_PASS(_, nir, nir_lower_tex, &(nir_lower_tex_options){.lower_index_to_offset = true});
   NIR_PASS(_, nir, nir_opt_constant_folding);
   NIR_PASS(_, nir, nir_shader_instructions_pass, bind_resources, nir_metadata_control_flow, NULL);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   NIR_PASS(_, nir, nir_lower_alu_to_scalar, NULL, NULL);
   NIR_PASS(_, nir, nir_lower_load_const_to_scalar);
   NIR_PASS(_, nir, agx_nir_cleanup_amul);
   NIR_PASS(_, nir, nir_opt_constant_folding);
   NIR_PASS(_, nir, nir_opt_copy_prop);
   NIR_PASS(_, nir, nir_opt_cse);
   NIR_PASS(_, nir, nir_opt_dce);
   NIR_PASS(_, nir, nir_opt_shrink_vectors, true);
   NIR_PASS(_, nir, nir_opt_copy_prop);
   NIR_PASS(_, nir, nir_opt_load_store_vectorize,
            &(const nir_load_store_vectorize_options){
               .modes = nir_var_mem_global | nir_var_mem_constant | nir_var_mem_ssbo,
               .callback = agx_mem_vectorize_cb,
            });
   struct counts counts = {0};
   count_cf(&nir_shader_get_entrypoint(nir)->body, 0, &counts);
   NIR_PASS(_, nir, agx_nir_lower_texture);
   NIR_PASS(_, nir, nir_lower_ssbo, NULL);
   agx_preprocess_nir(nir);

   /* agx_compile_nir */
   NIR_PASS(_, nir, nir_shader_instructions_pass, lower_sysvals, nir_metadata_control_flow, NULL);
   if (print_nir)
      nir_print_shader(nir, stdout);
   /* J313: G13G (M1), no G13X cluster coherency, no soft fault */
   const unsigned mem_blocks = memory_blocks(nir);
   struct classes classes = {0};
   count_classes(nir, &classes);
   struct agx_shader_key key = {.dev = {.needs_g13x_coherency = U_TRISTATE_NO},
                                .has_scratch = true, .promote_constants = true, .reserved_preamble = 240};
   struct agx_shader_part part = {0};
   agx_compile_shader_nir(nir, &key, &part);
   const struct agx2_stats *s = &part.info.stats;
   printf("COST stage %s block %ux%ux%u shared %u nir-ifs %u guarded-loads %u loads %u stores %u txf %u mem-blocks %u hot-mem-blocks %u hot-nir %u hot-branches %u "
          "instrs %u alu %u fscib %u ic %u gprs %u threads %u scratch %u spills %u fills %u loops %u code %u "
          "preamble %u nir-alu %u nir-mem %u nir-tex %u nir-shared %u barriers %u atomics %u root-marked %u\n",
          mesa_shader_stage_name(MESA_SHADER_COMPUTE), block[0], block[1], block[2], shared, counts.ifs,
          counts.guarded_loads, counts.loads, counts.stores, counts.txf, mem_blocks, hot.memory_blocks, hot.instrs, hot.branches, s->instrs, s->alu, s->fscib, s->ic,
          s->gprs, s->threads, s->scratch, s->spills, s->fills, s->loops, s->code_size, s->preamble_inst,
          classes.alu, classes.mem, classes.tex, classes.shared, classes.barriers, classes.atomics, marked);
   free(part.binary);
   ralloc_free(nir);
   glsl_type_singleton_decref();
   free(bytes);
   return 0;
}
