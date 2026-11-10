/* RC74: offline AGX cost of a D3D12 compute shader.  Translates a DXIL
 * program part or container with agx_dxil_to_nir (the owner's slot limits),
 * runs the steps Gallium Asahi applies to a D3D12 owner shader
 * (agx_state.c agx_shader_initialize with the Windows D3D robustness of the
 * native projection, then agx_compile_nir) and compiles it with the pinned
 * AGX backend.  The Gallium-only binding and sysval lowerings are replaced
 * by stand-ins with the same shape (uniform loads for buffer bases, sizes
 * and texture handles), so instruction counts, GPRs, scratch and spills are
 * the backend's own; uniform numbering is not.
 * --production (compute) replaces the stand-ins by the passes themselves
 * (dxil_agx_cost_gallium.c: agx_nir_lower_bindings, the serialized NIR round
 * trip between agx_shader_initialize and agx_compile_variant, then
 * agx_nir_lower_sysvals with the D3D12 C47 projection and
 * agx_nir_layout_uniforms, whose result is the key's reserved_preamble):
 * the compile of the guest's D3D12 owner, comparable with its
 * measure-d3d12-shader-stats row.  It needs the shader's own
 * --root-views masks (its measure-d3d12-root-views receipt; 0,0 when the
 * receipt says none were applied) and refuses --no-d3d-robust,
 * --assume-in-bounds, --soft-fault and --fs-backend, which leave that shape;
 * --soft-fault-loads stays a flag because it is the device's feature
 * (measure-soft-faults).  The COST line ends in "shape production" or
 * "shape stand-in": only the former may be compared with guest receipts.
 *
 * usage: dxil_agx_cost_fixture <dxil>|--synthetic-uav-reads=K|--synthetic-local-array=N|
 *        --synthetic-array-samples=N[,heap]|--synthetic-break-loops=N|--synthetic-shared-reads=K
 *        [--no-d3d-robust] [--assume-in-bounds] [--root-views=UAV,CBV]
 *        [--print-lowered] [--print-nir] [--print-selected] [--soft-fault|--soft-fault-loads]
 *        [--crawl-bindful] [--print-bindings] [--print-samples]
 * --crawl-bindful: T5b step A (the owner's APPLE_AGX_D3D12_CRAWL_BINDFUL);
 * with it or --print-samples a second line follows COST:
 *   SAMPLES reg <inner> <loop> <other> bindless <inner> <loop> <other> moved <n>
 * (non-query tex instructions of the NIR handed to the backend, by form and
 * place: innermost loop, other loop, outside loops; moved: step A's count);
 * --print-bindings prints the frontend's map, one BINDING line per register
 * --root-views: hex masks of the UAV and constant-buffer slots that are
 * D3D12 root descriptors (RC75 dxbc_nir_mark_root_views: left unchecked) 
 * --soft-fault: Asahi's compiler key with soft faults (set_speculate: every
 * access-qualified intrinsic and every texture instruction);
 * --soft-fault-loads: RC82's Windows projection (agx_win32_soft_fault.h:
 * memory loads only, key soft_fault false)
 * prints one line:
 *   COST stage <s> block <x>x<y>x<z> shared <B> nir-ifs <n> guarded-loads <n>
 *        loads <n> stores <n> txf <n> mem-blocks <n> hot-mem-blocks <n>
 *        hot-nir <n> hot-branches <n> instrs <n> alu <n> fscib <n> ic <n>
 *        gprs <n> threads <n> scratch <n> spills <n> fills <n> loops <n>
 *        code <n> preamble <n> nir-alu <n> nir-mem <n> nir-tex <n>
 *        nir-shared <n> barriers <n> atomics <n> root-marked <n>
 *        crawl-speculated <n> tex-speculated <n> binary-fnv <hex>
 *        shape production|stand-in
 * (RC75 op classes of the final NIR handed to the backend: ALU, device
 * memory, texture/image, threadgroup memory, workgroup barriers, atomics;
 * RC78 crawl-speculated: texture descriptor loads that
 * agx_win32_speculate_texture_crawls lets run in the preamble)
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
#include "asahi/isa/disasm.h"

#include "agx_win32_d3d_robustness.h"
#include "agx_win32_d3d_texture_crawl.h"
#include "agx_win32_soft_fault.h"
#include "asahi/lib/agx_abi.h"
#include "genxml/agx_pack.h"

/* --production: dxil_agx_cost_gallium.c */
void agx_cost_production_bindings(nir_shader *nir);
nir_shader *agx_cost_production_round_trip(nir_shader *nir);
unsigned agx_cost_production_sysvals(nir_shader *nir);

/* Gallium agx_state.c should_lower_robustness for a robust context, minus
 * UBO/SSBO accesses (agx_win32_should_lower_robustness_d3d); data: the
 * context's robustness (false with --no-d3d-robust) */
static bool
gl_part_of_d3d_robustness(const nir_intrinsic_instr *intr, const void *data)
{
   switch (intr->intrinsic) {
   /* agx_win32_should_lower_robustness_d3d excludes load_ubo, load_ssbo and
    * store_ssbo only: the atomics keep should_lower_robustness's *gl_robust */
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
      return *(const bool *)data;
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
 * textures past the state registers or needing a descriptor crawl bindless,
 * at an index clamped to the shader's textures (images) as Gallium does */
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
      index = nir_umin(b, index, nir_imm_intN_t(b, b->shader->info.num_textures - 1, index->bit_size));
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
   nir_def *image = nir_umin(b, intr->src[0].ssa,
                             nir_imm_intN_t(b, b->shader->info.num_images - 1, intr->src[0].ssa->bit_size));
   nir_def *slot = nir_iadd_imm(b, nir_imul_imm(b, nir_u2u32(b, image), 2), offset);
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

static unsigned
fixture_type_size(const struct glsl_type *type, bool bindless)
{
   (void)bindless;
   return glsl_count_attribute_slots(type, false);
}

/* stand-in for Gallium agx_nir_lower_fs_output_to_epilog (colour
 * outputs become epilog register exports; layout of agx_abi.h) */
#define FIXTURE_FOUT_COLOUR(rt) (2 * (4 + (4 * (rt))))
static bool
lower_fs_outputs(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   (void)data;
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;
   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   if (sem.location < FRAG_RESULT_DATA0)
      return false;
   const unsigned loc = sem.location - FRAG_RESULT_DATA0 + nir_src_as_uint(intr->src[1]);
   b->cursor = nir_instr_remove(&intr->instr);
   nir_def *vec = intr->src[0].ssa;
   const unsigned comp = nir_intrinsic_component(intr), stride = vec->bit_size / 16;
   u_foreach_bit(c, nir_intrinsic_write_mask(intr))
      nir_export_agx(b, nir_channel(b, vec, c), .base = FIXTURE_FOUT_COLOUR(loc) + (comp + c) * stride);
   return true;
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
   case nir_intrinsic_load_is_first_fan_agx:
      /* rc123 FS: a draw uniform, as Gallium's */
      value = nir_ine_imm(b, nir_load_preamble(b, 1, 16, .base = 8), 0);
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

/* RC78: descriptor loads marked speculatable */
static unsigned
count_speculated(nir_shader *nir)
{
   unsigned speculated = 0;
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            speculated += intr->intrinsic == nir_intrinsic_load_global_constant &&
                          (nir_intrinsic_access(intr) & ACCESS_CAN_SPECULATE);
         }
      }
   }
   return speculated;
}

/* RC82: texture instructions the backend may speculate (before its own
 * set_speculate; the Windows soft-fault pass must leave them alone) */
static unsigned
count_tex_speculated(nir_shader *nir)
{
   unsigned speculated = 0;
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            speculated += instr->type == nir_instr_type_tex && nir_instr_as_tex(instr)->can_speculate;
         }
      }
   }
   return speculated;
}

/* T5b: the shader's samples (tex instructions that are not queries) by
 * form -- bindless (a texture_handle source) or texture state register --
 * and place: inside an innermost loop, inside another loop, outside loops */
struct sample_forms {
   unsigned reg[3], bindless[3]; /* [0] innermost loop, [1] other loop, [2] no loop */
};

static bool
cf_has_loop(struct exec_list *list)
{
   foreach_list_typed(nir_cf_node, node, node, list) {
      if (node->type == nir_cf_node_loop)
         return true;
      if (node->type == nir_cf_node_if) {
         nir_if *nif = nir_cf_node_as_if(node);
         if (cf_has_loop(&nif->then_list) || cf_has_loop(&nif->else_list))
            return true;
      }
   }
   return false;
}

static void
count_sample_forms(struct exec_list *list, int place, struct sample_forms *f)
{
   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block:
         nir_foreach_instr(instr, nir_cf_node_as_block(node)) {
            if (instr->type != nir_instr_type_tex || nir_tex_instr_is_query(nir_instr_as_tex(instr)))
               continue;
            const bool handle = nir_tex_instr_src_index(nir_instr_as_tex(instr), nir_tex_src_texture_handle) >= 0;
            (handle ? f->bindless : f->reg)[place]++;
         }
         break;
      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(node);
         count_sample_forms(&nif->then_list, place, f);
         count_sample_forms(&nif->else_list, place, f);
         break;
      }
      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(node);
         count_sample_forms(&loop->body, cf_has_loop(&loop->body) ? 1 : 0, f);
         break;
      }
      default:
         break;
      }
   }
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

/* M1: four index-to-byte conversions; agx_dxil_drop_index_masks must drop
 * the mask exactly when it keeps the low 32 - s bits of a * 2^s. Prints
 * "MASK <case> dropped|kept" per store and exits. */
/* L4: a loop whose constant-buffer address comes from groupshared memory,
 * the load inside a conditional (as in C4's traversal).  Case 0: the loop
 * stores no shared memory -> load_shared and the constant load leave the
 * loop.  Case 1: the loop stores shared memory -> both stay.  Prints
 * "LICMCONST <case> shared-in-loop <n> const-in-loop <n>". */
static unsigned
count_in_loops(struct exec_list *list, bool in_loop, nir_intrinsic_op op)
{
   unsigned n = 0;
   foreach_list_typed(nir_cf_node, node, node, list) {
      if (node->type == nir_cf_node_block) {
         if (!in_loop)
            continue;
         nir_foreach_instr(instr, nir_cf_node_as_block(node))
            n += instr->type == nir_instr_type_intrinsic && nir_instr_as_intrinsic(instr)->intrinsic == op;
      } else if (node->type == nir_cf_node_if) {
         n += count_in_loops(&nir_cf_node_as_if(node)->then_list, in_loop, op);
         n += count_in_loops(&nir_cf_node_as_if(node)->else_list, in_loop, op);
      } else {
         n += count_in_loops(&nir_cf_node_as_loop(node)->body, true, op);
      }
   }
   return n;
}

static int
selftest_const_licm(void)
{
   for (unsigned c = 0; c < 2; ++c) {
      nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &agx_nir_options, "l4-const-licm");
      b.shader->info.num_ssbos = 1;
      b.shader->info.shared_size = 64;
      nir_def *x = nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);
      nir_def *cb = nir_imm_int64(&b, 0x100000);
      nir_variable *i = nir_local_variable_create(b.impl, glsl_uint_type(), "i");
      nir_variable *acc = nir_local_variable_create(b.impl, glsl_uint_type(), "acc");
      nir_store_var(&b, i, nir_imm_int(&b, 0), 1);
      nir_store_var(&b, acc, nir_imm_int(&b, 0), 1);
      nir_push_loop(&b);
      {
         nir_def *iv = nir_load_var(&b, i);
         nir_push_if(&b, nir_ine_imm(&b, nir_iand_imm(&b, nir_iadd(&b, iv, x), 1), 0));
         {
            nir_def *s = nir_load_shared(&b, 1, 32, nir_imm_int(&b, 48), .align_mul = 4);
            nir_def *addr = nir_iadd(&b, cb, nir_u2u64(&b, nir_imul_imm(&b, nir_iadd_imm(&b, nir_imul_imm(&b, s, 57), 48), 4)));
            nir_def *v = nir_load_global_constant(&b, 1, 32, addr, .align_mul = 4);
            nir_store_var(&b, acc, nir_iadd(&b, nir_load_var(&b, acc), v), 1);
         }
         nir_pop_if(&b, NULL);
         if (c == 1)
            nir_store_shared(&b, iv, nir_imm_int(&b, 48), .align_mul = 4);
         nir_store_var(&b, i, nir_iadd_imm(&b, iv, 1), 1);
         nir_break_if(&b, nir_uge_imm(&b, nir_iadd_imm(&b, iv, 1), 8));
      }
      nir_pop_loop(&b, NULL);
      nir_store_ssbo(&b, nir_load_var(&b, acc), nir_imm_int(&b, 0), nir_amul_imm(&b, x, 4), .write_mask = 1,
                     .access = ACCESS_NON_READABLE, .align_mul = 4, .align_offset = 0);
      nir_lower_vars_to_ssa(b.shader);
      nir_opt_dce(b.shader);
      nir_metadata_require(b.impl, nir_metadata_block_index | nir_metadata_dominance);
      agx_win32_hoist_constant_loads(b.shader, true);
      nir_validate_shader(b.shader, "l4");
      struct exec_list *body = &nir_shader_get_entrypoint(b.shader)->body;
      printf("LICMCONST %u shared-in-loop %u const-in-loop %u\n", c,
             count_in_loops(body, false, nir_intrinsic_load_shared),
             count_in_loops(body, false, nir_intrinsic_load_global_constant));
      ralloc_free(b.shader);
   }
   return 0;
}

/* T1: agx_win32_d3d_max_layer evaluated on packed descriptors.  The
 * helper's descriptor loads are replaced by the packed words and the NIR is
 * constant folded; the reference is libagx_txs's layer count (agx_unpack,
 * libagx/texture.cl) minus one. */
static uint32_t
max_layer_reference(const struct agx_texture_packed *packed, enum glsl_sampler_dim dim, bool *null)
{
   agx_unpack(NULL, packed, TEXTURE, d);
   *null = d.address == AGX_ZERO_PAGE_ADDRESS;
   if (*null)
      return 0u - 1u; /* Asahi: umin(layer, 0 - 1) */
   const uint32_t layers = dim == GLSL_SAMPLER_DIM_2D && d.layout == AGX_LAYOUT_LINEAR ? d.depth_linear : d.depth;
   return layers - 1u;
}

static bool
max_layer_evaluated(const struct agx_texture_packed *packed, enum glsl_sampler_dim dim, uint32_t *value)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &agx_nir_options, "t1-max-layer");
   nir_tex_instr *tex = nir_tex_instr_create(b.shader, 0);
   tex->sampler_dim = dim;
   tex->is_array = true;
   nir_def *result = agx_win32_d3d_max_layer(&b, tex, nir_imm_int(&b, 0));
   nir_store_global(&b, result, nir_imm_int64(&b, 0x1000), .write_mask = 1, .align_mul = 4);
   const uint32_t *words = (const uint32_t *)packed;
   nir_foreach_block(block, b.impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic ||
             nir_instr_as_intrinsic(instr)->intrinsic != nir_intrinsic_load_global_constant)
            continue;
         nir_intrinsic_instr *load = nir_instr_as_intrinsic(instr);
         nir_scalar addr = nir_scalar_resolved(load->src[0].ssa, 0);
         uint64_t offset = 0;
         if (nir_scalar_is_alu(addr) && nir_scalar_alu_op(addr) == nir_op_iadd) {
            offset = nir_scalar_as_uint(nir_scalar_chase_alu_src(addr, 1));
            addr = nir_scalar_chase_alu_src(addr, 0);
         }
         if (!nir_scalar_is_intrinsic(addr) || nir_scalar_intrinsic_op(addr) != nir_intrinsic_load_from_texture_handle_agx ||
             offset % 4 || offset >= AGX_TEXTURE_LENGTH)
            return false;
         b.cursor = nir_before_instr(instr);
         nir_def_replace(&load->def, nir_imm_int(&b, (int)words[offset / 4]));
      }
   }
   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, b.shader, nir_opt_constant_folding);
      NIR_PASS(progress, b.shader, nir_opt_copy_prop);
      NIR_PASS(progress, b.shader, nir_opt_algebraic);
      NIR_PASS(progress, b.shader, nir_opt_dce);
   } while (progress);
   bool ok = false;
   nir_foreach_block(block, b.impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_intrinsic &&
             nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_store_global &&
             nir_src_is_const(nir_instr_as_intrinsic(instr)->src[0])) {
            *value = (uint32_t)nir_src_as_uint(nir_instr_as_intrinsic(instr)->src[0]);
            ok = true;
         }
      }
   }
   ralloc_free(b.shader);
   return ok;
}

/* the pass rewrites array samples whose handle is divergent only, and only
 * with APPLE_AGX_D3D_MAX_LAYER=1 */
static unsigned
max_layer_rewritten(void)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &agx_nir_options, "t1-max-layer-pass");
   nir_def *x = nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);
   nir_def *sum = nir_imm_float(&b, 0.0f);
   for (unsigned i = 0; i < 2; ++i) {
      nir_def *index = i ? nir_imul_imm(&b, nir_iand_imm(&b, x, 7), 24) : nir_imm_int(&b, 48);
      nir_tex_instr *tex = nir_tex_instr_create(b.shader, 3);
      tex->op = nir_texop_txl;
      tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
      tex->is_array = true;
      tex->coord_components = 3;
      tex->dest_type = nir_type_float32;
      tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, nir_vec3(&b, nir_imm_float(&b, 0.5f),
                                                                    nir_imm_float(&b, 0.5f), nir_u2f32(&b, x)));
      tex->src[1] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_float(&b, 0.0f));
      tex->src[2] = nir_tex_src_for_ssa(nir_tex_src_texture_handle,
                                        nir_bindless_image_agx(&b, index, .desc_set = DXBC_HEAP_HANDLE_MARK));
      nir_def_init(&tex->instr, &tex->def, 4, 32);
      nir_builder_instr_insert(&b, &tex->instr);
      sum = nir_fadd(&b, sum, nir_channel(&b, &tex->def, 0));
   }
   nir_store_global(&b, sum, nir_imm_int64(&b, 0x1000), .write_mask = 1, .align_mul = 4);
   agx_win32_lower_d3d_layer_clamp(b.shader);
   nir_validate_shader(b.shader, "t1");
   unsigned rewritten = 0;
   nir_foreach_block(block, b.impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_tex) {
            nir_tex_instr *tex = nir_instr_as_tex(instr);
            const bool divergent_handle =
               !nir_src_is_const(tex->src[nir_tex_instr_src_index(tex, nir_tex_src_texture_handle)].src);
            if (tex->backend_flags & AGX_TEXTURE_FLAG_NO_CLAMP)
               rewritten |= divergent_handle ? 1u : 2u;
         }
      }
   }
   ralloc_free(b.shader);
   return rewritten;
}

static int
selftest_max_layer(void)
{
   struct agx_texture_packed packed[8];
   enum glsl_sampler_dim dims[8];
   const char *names[8];
   unsigned n = 0;
   /* twiddled 2D arrays of 1, 6 and 2048 layers */
   static const unsigned layers[3] = {1, 6, 2048};
   for (unsigned i = 0; i < 3; ++i, ++n) {
      agx_pack(&packed[n], TEXTURE, cfg) {
         cfg.dimension = AGX_TEXTURE_DIMENSION_2D_ARRAY;
         cfg.layout = AGX_LAYOUT_TWIDDLED;
         cfg.width = 256;
         cfg.height = 128;
         cfg.depth = layers[i];
         cfg.address = 0x123450000ull;
      }
      dims[n] = GLSL_SAMPLER_DIM_2D;
      names[n] = "twiddled-2d-array";
   }
   /* agx_pack_texture's linear 2D array view: depth_linear, extended */
   agx_pack(&packed[n], TEXTURE, cfg) {
      cfg.dimension = AGX_TEXTURE_DIMENSION_2D_ARRAY;
      cfg.layout = AGX_LAYOUT_LINEAR;
      cfg.width = 1920;
      cfg.height = 1080;
      cfg.stride = 1920 * 4 - 16;
      cfg.depth_linear = 2;
      cfg.layer_stride_linear = 0x800000 - 0x80;
      cfg.extended = true;
      cfg.address = 0x200000000ull;
   }
   dims[n] = GLSL_SAMPLER_DIM_2D;
   names[n++] = "linear-2d-array";
   /* a linear 2D view (D3D12 presentation surface) sampled as an array */
   agx_pack(&packed[n], TEXTURE, cfg) {
      cfg.dimension = AGX_TEXTURE_DIMENSION_2D;
      cfg.layout = AGX_LAYOUT_LINEAR;
      cfg.width = 1920;
      cfg.height = 1080;
      cfg.stride = 1920 * 4 - 16;
      cfg.address = 0x200000000ull;
   }
   dims[n] = GLSL_SAMPLER_DIM_2D;
   names[n++] = "linear-2d";
   agx_pack(&packed[n], TEXTURE, cfg) {
      cfg.dimension = AGX_TEXTURE_DIMENSION_CUBE_ARRAY;
      cfg.layout = AGX_LAYOUT_TWIDDLED;
      cfg.width = cfg.height = 64;
      cfg.depth = 3;
      cfg.address = 0x300000000ull;
   }
   dims[n] = GLSL_SAMPLER_DIM_CUBE;
   names[n++] = "cube-array";
   agx_pack(&packed[n], TEXTURE, cfg) {
      cfg.dimension = AGX_TEXTURE_DIMENSION_1D_ARRAY;
      cfg.layout = AGX_LAYOUT_TWIDDLED;
      cfg.width = 512;
      cfg.depth = 9;
      cfg.address = 0x300000000ull;
   }
   dims[n] = GLSL_SAMPLER_DIM_1D;
   names[n++] = "1d-array";
   /* agx_helpers.h agx_set_null_texture (Gallium unbound slots, D3D12 null
    * SRVs); test_d3d_max_layer.py holds the pinned body to these fields */
   agx_pack(&packed[n], TEXTURE, cfg) {
      cfg.layout = AGX_LAYOUT_TWIDDLED;
      cfg.channels = AGX_CHANNELS_R8;
      cfg.type = AGX_TEXTURE_TYPE_UNORM;
      cfg.swizzle_r = AGX_CHANNEL_0;
      cfg.swizzle_g = AGX_CHANNEL_0;
      cfg.swizzle_b = AGX_CHANNEL_0;
      cfg.swizzle_a = AGX_CHANNEL_0;
      cfg.address = AGX_ZERO_PAGE_ADDRESS;
   }
   dims[n] = GLSL_SAMPLER_DIM_2D;
   names[n++] = "null";
   int failures = 0;
   for (unsigned i = 0; i < n; ++i) {
      bool null = false;
      uint32_t got = 0;
      const uint32_t want = max_layer_reference(&packed[i], dims[i], &null);
      if (!max_layer_evaluated(&packed[i], dims[i], &got)) {
         printf("MAXLAYER %s not-constant\n", names[i]);
         ++failures;
         continue;
      }
      bool ok = got == want;
      if (null) {
         /* clamped to the encoded single layer; every channel reads 0 */
         agx_unpack(NULL, &packed[i], TEXTURE, d);
         ok = got == d.depth - 1u && d.swizzle_r == AGX_CHANNEL_0 && d.swizzle_g == AGX_CHANNEL_0 &&
              d.swizzle_b == AGX_CHANNEL_0 && d.swizzle_a == AGX_CHANNEL_0;
      }
      printf("MAXLAYER %s got %u want %u%s %s\n", names[i], got, want, null ? " (null)" : "", ok ? "ok" : "FAIL");
      failures += !ok;
   }
   printf("MAXLAYER rewritten %u\n", max_layer_rewritten());
   return failures ? 4 : 0;
}

static int
selftest_index_masks(void)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &agx_nir_options, "m1-index-masks");
   b.shader->info.num_ssbos = 1;
   nir_def *x = nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);
   nir_def *v[7];
   v[0] = nir_amul(&b, nir_iand_imm(&b, x, 0x3fffffff), nir_imm_int(&b, 4));  /* drop */
   v[1] = nir_imul(&b, nir_iand_imm(&b, x, 0x0fffffff), nir_imm_int(&b, 4));  /* keep: bits 28-29 */
   v[2] = nir_ishl(&b, nir_iand_imm(&b, x, 0x1fffffff), nir_imm_int(&b, 3));  /* drop */
   v[3] = nir_imul(&b, nir_imm_int(&b, 8), nir_iand_imm(&b, x, 0x3fffffff));  /* drop: covers 0x1fffffff */
   /* M1b: the masked index plus an element offset */
   v[4] = nir_amul(&b, nir_iadd_imm(&b, nir_iand_imm(&b, x, 0x3fffffff), 3), nir_imm_int(&b, 4));  /* drop */
   v[5] = nir_imul(&b, nir_iadd_imm(&b, nir_iand_imm(&b, x, 0x0fffffff), 3), nir_imm_int(&b, 4));  /* keep */
   v[6] = nir_ishl(&b, nir_iadd(&b, nir_imm_int(&b, 5), nir_iand_imm(&b, x, 0x1fffffff)), nir_imm_int(&b, 3)); /* drop */
   for (unsigned i = 0; i < 7; ++i)
      nir_store_ssbo(&b, v[i], nir_imm_int(&b, 0), nir_imm_int(&b, 4 * i), .write_mask = 1,
                     .access = ACCESS_NON_READABLE, .align_mul = 4, .align_offset = 0);
   agx_dxil_drop_index_masks(b.shader);
   unsigned i = 0;
   nir_foreach_block(block, nir_shader_get_entrypoint(b.shader)) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic ||
             nir_instr_as_intrinsic(instr)->intrinsic != nir_intrinsic_store_ssbo)
            continue;
         nir_alu_instr *mul = nir_instr_as_alu(nir_def_instr(nir_instr_as_intrinsic(instr)->src[0].ssa));
         bool masked = false;
         for (unsigned k = 0; k < 2; ++k) {
            nir_instr *src = nir_def_instr(mul->src[k].src.ssa);
            if (src->type != nir_instr_type_alu)
               continue;
            nir_alu_instr *s = nir_instr_as_alu(src);
            masked |= s->op == nir_op_iand;
            if (s->op == nir_op_iadd) {
               for (unsigned j = 0; j < 2; ++j) {
                  nir_instr *a = nir_def_instr(s->src[j].src.ssa);
                  masked |= a->type == nir_instr_type_alu && nir_instr_as_alu(a)->op == nir_op_iand;
               }
            }
         }
         printf("MASK %u %s\n", i++, masked ? "kept" : "dropped");
      }
   }
   ralloc_free(b.shader);
   return i == 7 ? 0 : 3;
}

/* Independent shared-memory reads with real producer stores and a group
 * barrier. Rotate the lane index so the reads require shared memory. */
static nir_shader *
synthetic_shared_reads(unsigned reads)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &agx_nir_options, "k1-shared-reads");
   b.shader->info.internal = false;
   b.shader->info.workgroup_size[0] = 64;
   b.shader->info.workgroup_size[1] = b.shader->info.workgroup_size[2] = 1;
   b.shader->info.shared_size = 64 * 4 * reads;
   b.shader->info.num_ssbos = 1;
   nir_def *lane = nir_load_local_invocation_index(&b);
   for (unsigned k = 0; k < reads; ++k)
      nir_store_shared(&b, nir_u2f32(&b, nir_iadd_imm(&b, lane, k)),
                       nir_iadd_imm(&b, nir_imul_imm(&b, lane, 4), k * 256),
                       .write_mask = 1, .align_mul = 4);
   nir_barrier(&b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_WORKGROUP,
               .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_shared);
   nir_def *offset = nir_imul_imm(&b, nir_iand_imm(&b, nir_iadd_imm(&b, lane, 1), 63), 4);
   nir_def *sum = nir_imm_float(&b, 0);
   for (unsigned k = 0; k < reads; ++k)
      sum = nir_fadd(&b, sum, nir_load_shared(&b, 1, 32, nir_iadd_imm(&b, offset, k * 256), .align_mul = 4));
   nir_store_ssbo(&b, sum, nir_imm_int(&b, 0), nir_imul_imm(&b, lane, 4),
                  .write_mask = 1, .access = ACCESS_NON_READABLE, .align_mul = 4);
   nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
   return b.shader;
}

/* K2: dense per-lane words, read from lane zero after publication. Mode 1
 * splits reads with a barrier, 2 leaves holes, 3 uses a dynamic lane address
 * whose 16-bit wrap prevents the current vectorizer proving adjacency. */
static nir_shader *
synthetic_shared_adjacent(unsigned mode)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &agx_nir_options, "k2-shared-adjacent");
   b.shader->info.internal = false;
   b.shader->info.workgroup_size[0] = 64;
   b.shader->info.workgroup_size[1] = b.shader->info.workgroup_size[2] = 1;
   b.shader->info.shared_size = 4096;
   b.shader->info.num_ssbos = 1;
   nir_def *lane = nir_load_local_invocation_index(&b);
   nir_def *base = nir_imul_imm(&b, lane, 64);
   for (unsigned k = 0; k < 16; ++k)
      nir_store_shared(&b, nir_iadd_imm(&b, lane, k), nir_iadd_imm(&b, base, k * 4),
                       .write_mask = 1, .align_mul = 4);
   nir_barrier(&b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_WORKGROUP,
               .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_shared);
   base = mode == 3 ? nir_imul_imm(&b, nir_iand_imm(&b, nir_iadd_imm(&b, lane, 1), 63), 64)
                    : nir_imm_int(&b, 0);
   nir_def *sum = nir_imm_int(&b, 0);
   for (unsigned k = 0; k < 8; ++k) {
      if (mode == 1 && k == 3)
         nir_barrier(&b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_WORKGROUP,
                     .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_shared);
      sum = nir_iadd(&b, sum, nir_load_shared(&b, 1, 32,
         nir_iadd_imm(&b, base, k * (mode == 2 ? 8 : 4)), .align_mul = 4));
   }
   nir_store_ssbo(&b, sum, nir_imm_int(&b, 0), nir_imul_imm(&b, lane, 4),
                  .write_mask = 1, .access = ACCESS_NON_READABLE, .align_mul = 4);
   nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
   return b.shader;
}

/* K3: one invocation publishes known bytes. Keep every read observable in
 * an SSBO so the retained compiler output and optimized NIR can be checked.
 * Modes: dense, barrier split, holes, overlap, masked, atomic, aliasing read,
 * 16-bit and 8-bit stores. This is a compiler oracle, not a GPU emulator. */
static nir_shader *
synthetic_shared_stores(unsigned mode)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &agx_nir_options, "k3-shared-stores");
   b.shader->info.internal = false;
   b.shader->info.workgroup_size[0] = 1;
   b.shader->info.workgroup_size[1] = b.shader->info.workgroup_size[2] = 1;
   b.shader->info.shared_size = 128;
   b.shader->info.num_ssbos = 1;
   unsigned bits = mode == 7 ? 16 : mode == 8 ? 8 : 32;
   for (unsigned k = 0; k < 8; ++k) {
      if (mode == 1 && k == 3)
         nir_barrier(&b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_WORKGROUP,
                     .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_shared);
      if ((mode == 5 || mode == 6) && k == 3) {
         nir_def *old = mode == 5 ?
            nir_shared_atomic(&b, 32, nir_imm_int(&b, 0), nir_imm_int(&b, 7), .atomic_op = nir_atomic_op_iadd) :
            nir_load_shared(&b, 1, 32, nir_imm_int(&b, 0), .align_mul = 4);
         nir_store_ssbo(&b, old, nir_imm_int(&b, 0), nir_imm_int(&b, 64),
                        .write_mask = 1, .access = ACCESS_NON_READABLE, .align_mul = 4);
      }
      nir_store_shared(&b, nir_imm_intN_t(&b, 0x40 + k, bits),
                       nir_imm_int(&b, k * (mode == 2 ? 8 : bits / 8)),
                       .write_mask = 1, .align_mul = bits / 8);
   }
   if (mode == 3 || mode == 6)
      nir_store_shared(&b, nir_imm_int(&b, 0xabcd), nir_imm_int(&b, 0),
                       .write_mask = 1, .align_mul = 4);
   if (mode == 4)
      nir_store_shared(&b, nir_imm_ivec4(&b, 0x81, 0xdead, 0x83, 0xbeef), nir_imm_int(&b, 0),
                       .write_mask = 5, .align_mul = 4);
   nir_barrier(&b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_WORKGROUP,
               .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_shared);
   for (unsigned k = 0; k < 8; ++k) {
      nir_def *value = nir_load_shared(&b, 1, bits,
         nir_imm_int(&b, k * (mode == 2 ? 8 : bits / 8)), .align_mul = bits / 8);
      nir_store_ssbo(&b, nir_u2u32(&b, value), nir_imm_int(&b, 0), nir_imm_int(&b, k * 4),
                     .write_mask = 1, .access = ACCESS_NON_READABLE, .align_mul = 4);
   }
   nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
   return b.shader;
}

/* Emit the actual optimized NIR's shared-memory operations, including values
 * and masks, for a byte-addressed Python oracle. Fail rather than invent a
 * value when a fixture stops being constant. Backend code shape is checked
 * separately in the retained executable. */
static unsigned
shared_oracle_constant(nir_def *def, unsigned component)
{
   nir_scalar s = nir_scalar_resolved(def, component);
   if (!nir_scalar_is_const(s)) {
      fputs("nonconstant shared oracle operand\n", stderr);
      exit(3);
   }
   return nir_scalar_as_uint(s);
}

static void
print_shared_oracle(nir_shader *nir)
{
   nir_foreach_block(block, nir_shader_get_entrypoint(nir)) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic == nir_intrinsic_store_shared) {
            printf("ORACLE_STORE %u %u %u", shared_oracle_constant(intr->src[1].ssa, 0),
                   intr->src[0].ssa->bit_size, nir_intrinsic_write_mask(intr));
            for (unsigned c = 0; c < intr->num_components; ++c)
               printf(" %u", shared_oracle_constant(intr->src[0].ssa, c));
            puts("");
         } else if (intr->intrinsic == nir_intrinsic_load_shared) {
            printf("ORACLE_LOAD %u %u %u\n", shared_oracle_constant(intr->src[0].ssa, 0),
                   intr->def.bit_size, intr->def.num_components);
         } else if (intr->intrinsic == nir_intrinsic_shared_atomic) {
            assert(nir_intrinsic_atomic_op(intr) == nir_atomic_op_iadd);
            printf("ORACLE_ADD %u %u\n", shared_oracle_constant(intr->src[0].ssa, 0),
                   shared_oracle_constant(intr->src[1].ssa, 0));
         } else if (intr->intrinsic == nir_intrinsic_barrier) {
            puts("ORACLE_BARRIER");
         }
      }
   }
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

/* --synthetic-array-samples=N[,heap]: RC78, the shape of GravityMark's
 * lighting kernel 806ee7b1bdb1944a: N samples (txl, lod 0) of a
 * Texture2DArray at layers 0..N-1, each in its own branch on a per-thread
 * bit (divergent control flow), summed and written to u0.  Every array
 * sample clamps its layer to the texture's layer count through a descriptor
 * crawl.  ",heap": the texture is a D3D12 descriptor-heap handle
 * (DXBC_HEAP_HANDLE_MARK) instead of bound slot t0. */
static nir_shader *
synthetic_array_samples(unsigned samples, bool heap)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &agx_nir_options, "rc78-array-samples");
   b.shader->info.internal = false;
   b.shader->info.workgroup_size[0] = b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.num_ssbos = 1;
   nir_variable *texture = nir_variable_create(b.shader, nir_var_uniform,
                                               glsl_texture_type(GLSL_SAMPLER_DIM_2D, true, GLSL_TYPE_FLOAT), "t0");
   texture->data.binding = 0;
   nir_variable *sampler = nir_variable_create(b.shader, nir_var_uniform, glsl_bare_sampler_type(), "s0");
   sampler->data.binding = 0;
   /* as the frontends record them (agx_dxil_to_nir record_slots) */
   BITSET_SET(b.shader->info.textures_used, 0);
   BITSET_SET(b.shader->info.samplers_used, 0);
   nir_def *id = nir_load_global_invocation_id(&b, 32);
   nir_def *x = nir_channel(&b, id, 0);
   nir_def *u = nir_fmul_imm(&b, nir_u2f32(&b, x), 1.0 / 64.0);
   nir_def *v = nir_fmul_imm(&b, nir_u2f32(&b, nir_channel(&b, id, 1)), 1.0 / 64.0);
   nir_def *sum = nir_imm_float(&b, 0.0f);
   for (unsigned i = 0; i < samples; ++i) {
      nir_push_if(&b, nir_ine_imm(&b, nir_iand_imm(&b, nir_ushr_imm(&b, x, i), 1), 0));
      nir_tex_instr *tex = nir_tex_instr_create(b.shader, heap ? 3 : 2);
      tex->op = nir_texop_txl;
      tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
      tex->is_array = true;
      tex->coord_components = 3;
      tex->dest_type = nir_type_float32;
      tex->texture_index = 0;
      tex->sampler_index = 0;
      tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, nir_vec3(&b, u, v, nir_imm_float(&b, (float)i)));
      tex->src[1] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_float(&b, 0.0f));
      if (heap)
         tex->src[2] = nir_tex_src_for_ssa(nir_tex_src_texture_handle,
                                           nir_bindless_image_agx(&b, nir_imm_int(&b, 64), .desc_set = DXBC_HEAP_HANDLE_MARK));
      nir_def_init(&tex->instr, &tex->def, 4, 32);
      nir_builder_instr_insert(&b, &tex->instr);
      nir_def *taken = nir_fadd(&b, sum, nir_channel(&b, &tex->def, 0));
      nir_pop_if(&b, NULL);
      sum = nir_if_phi(&b, taken, sum);
   }
   nir_store_ssbo(&b, sum, nir_imm_int(&b, 0), nir_amul_imm(&b, x, 4), .write_mask = 1,
                  .access = ACCESS_NON_READABLE, .align_mul = 4, .align_offset = 0);
   nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
   return b.shader;
}

/* --synthetic-loop-samples=N: M3, the shape of GravityMark's planet PS
 * ray-march loops: a do-while loop of a runtime trip count (u1[0]) whose body samples
 * one Texture2DArray (heap handle, constant index) at a varying layer, so
 * every iteration clamps the layer through a descriptor crawl of the same
 * descriptor. Repeated N times (independent loops). */
static nir_shader *
synthetic_loop_samples(unsigned loops)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &agx_nir_options, "m3-loop-samples");
   b.shader->info.internal = false;
   b.shader->info.workgroup_size[0] = b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.num_ssbos = 2;
   nir_variable *sampler = nir_variable_create(b.shader, nir_var_uniform, glsl_bare_sampler_type(), "s0");
   sampler->data.binding = 0;
   BITSET_SET(b.shader->info.samplers_used, 0);
   nir_def *id = nir_load_global_invocation_id(&b, 32);
   nir_def *x = nir_channel(&b, id, 0);
   nir_def *u = nir_fmul_imm(&b, nir_u2f32(&b, x), 1.0 / 64.0);
   nir_def *count = nir_load_ssbo(&b, 1, 32, nir_imm_int(&b, 1), nir_imm_int(&b, 0),
                                  .access = ACCESS_NON_WRITEABLE, .align_mul = 4, .align_offset = 0);
   nir_def *sum = nir_imm_float(&b, 0.0f);
   for (unsigned l = 0; l < loops; ++l) {
      nir_variable *i = nir_local_variable_create(b.impl, glsl_uint_type(), "i");
      nir_variable *acc = nir_local_variable_create(b.impl, glsl_float_type(), "acc");
      nir_store_var(&b, i, nir_imm_int(&b, 0), 1);
      nir_store_var(&b, acc, sum, 1);
      nir_push_loop(&b);
      {
         nir_def *iv = nir_load_var(&b, i);
         nir_tex_instr *tex = nir_tex_instr_create(b.shader, 3);
         tex->op = nir_texop_txl;
         tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
         tex->is_array = true;
         tex->coord_components = 3;
         tex->dest_type = nir_type_float32;
         tex->texture_index = 0;
         tex->sampler_index = 0;
         tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, nir_vec3(&b, u, u, nir_u2f32(&b, iv)));
         tex->src[1] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_float(&b, 0.0f));
         tex->src[2] = nir_tex_src_for_ssa(nir_tex_src_texture_handle,
                                           nir_bindless_image_agx(&b, nir_imm_int(&b, 64 + 24 * l), .desc_set = DXBC_HEAP_HANDLE_MARK));
         nir_def_init(&tex->instr, &tex->def, 4, 32);
         nir_builder_instr_insert(&b, &tex->instr);
         nir_store_var(&b, acc, nir_fadd(&b, nir_load_var(&b, acc), nir_channel(&b, &tex->def, 0)), 1);
         nir_store_var(&b, i, nir_iadd_imm(&b, iv, 1), 1);
         /* do-while: the exit test ends the body, as in the planet PS */
         nir_break_if(&b, nir_uge(&b, nir_iadd_imm(&b, iv, 1), count));
      }
      nir_pop_loop(&b, NULL);
      sum = nir_load_var(&b, acc);
   }
   nir_store_ssbo(&b, sum, nir_imm_int(&b, 0), nir_amul_imm(&b, x, 4), .write_mask = 1,
                  .access = ACCESS_NON_READABLE, .align_mul = 4, .align_offset = 0);
   nir_lower_vars_to_ssa(b.shader);
   nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
   return b.shader;
}

/* --synthetic-break-loops=N: RC78, the single-pass loops dxil-spirv emits
 * to structure early exits (GravityMark's lighting kernel keeps 3 of them
 * through Asahi's optimizations): N times
 *    loop { if (bit i of the thread id) { a += u0[i]; break; }
 *           else { a *= 0.5; break; } }
 * through the frontend tail (dxbc_nir_finalize), the sum written to u1 */
static nir_shader *
synthetic_break_loops(unsigned count)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &agx_nir_options, "rc78-break-loops");
   b.shader->info.internal = false;
   b.shader->info.workgroup_size[0] = 64;
   b.shader->info.workgroup_size[1] = b.shader->info.workgroup_size[2] = 1;
   b.shader->info.num_ssbos = 2;
   nir_variable *acc = nir_local_variable_create(b.impl, glsl_float_type(), "a");
   nir_store_var(&b, acc, nir_imm_float(&b, 1.0f), 1);
   nir_def *x = nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);
   for (unsigned i = 0; i < count; ++i) {
      nir_push_loop(&b);
      nir_push_if(&b, nir_ine_imm(&b, nir_iand_imm(&b, nir_ushr_imm(&b, x, i), 1), 0));
      nir_def *value = nir_load_ssbo(&b, 1, 32, nir_imm_int(&b, 0), nir_imm_int(&b, (int)(4 * i)),
                                     .access = ACCESS_NON_WRITEABLE, .align_mul = 4, .align_offset = 0);
      nir_store_var(&b, acc, nir_fadd(&b, nir_load_var(&b, acc), value), 1);
      nir_jump(&b, nir_jump_break);
      nir_push_else(&b, NULL);
      nir_store_var(&b, acc, nir_fmul_imm(&b, nir_load_var(&b, acc), 0.5), 1);
      nir_jump(&b, nir_jump_break);
      nir_pop_if(&b, NULL);
      nir_pop_loop(&b, NULL);
   }
   nir_store_ssbo(&b, nir_load_var(&b, acc), nir_imm_int(&b, 1), nir_amul_imm(&b, x, 4), .write_mask = 1,
                  .access = ACCESS_NON_READABLE, .align_mul = 4, .align_offset = 0);
   dxbc_nir_finalize(b.shader);
   return b.shader;
}

/* RC96: FNV-1a of the shader binary, so equal compiles can be compared
 * byte for byte (APPLE_AGX_LATENCY_SCHED=0 against the pinned compiler) */
static unsigned
binary_fnv(const void *data, unsigned size)
{
   unsigned h = 2166136261u;
   for (unsigned i = 0; i < size; ++i)
      h = (h ^ ((const unsigned char *)data)[i]) * 16777619u;
   return h;
}

int
main(int argc, char **argv)
{
   bool production = false, root_views = false;
   bool d3d_robust = true, print_nir = false, print_lowered = false, in_bounds = false;
   bool soft_fault = false, soft_fault_loads = false, fs_backend = false, print_selected = false, hex_main = false;
   bool shared_store_oracle = false;
   bool crawl_bindful = false, print_bindings = false, print_samples = false;
   unsigned root_uavs = 0, root_cbvs = 0;
   for (int a = 2; a < argc; ++a) {
      unsigned uav, cbv;
      production |= !strcmp(argv[a], "--production");
      root_views |= sscanf(argv[a], "--root-views=%x,%x", &uav, &cbv) == 2;
      if (sscanf(argv[a], "--root-views=%x,%x", &root_uavs, &root_cbvs) == 2)
         continue;
      d3d_robust &= strcmp(argv[a], "--no-d3d-robust") != 0;
      print_nir |= !strcmp(argv[a], "--print-nir");
      print_selected |= !strcmp(argv[a], "--print-selected");
      hex_main |= !strcmp(argv[a], "--hex-main"); /* N6: the main function's bytes */
      fs_backend |= !strcmp(argv[a], "--fs-backend"); /* rc123: FS through the backend */
      print_lowered |= !strcmp(argv[a], "--print-lowered"); /* after the D3D lowering */
      crawl_bindful |= !strcmp(argv[a], "--crawl-bindful"); /* T5b step A */
      print_bindings |= !strcmp(argv[a], "--print-bindings"); /* T5b: the frontend's slot map */
      print_samples |= !strcmp(argv[a], "--print-samples"); /* T5b: the SAMPLES line */
      in_bounds |= !strcmp(argv[a], "--assume-in-bounds");
      soft_fault |= !strcmp(argv[a], "--soft-fault");
      soft_fault_loads |= !strcmp(argv[a], "--soft-fault-loads");
   }
   glsl_type_singleton_init_or_ref();
   if (argc > 1 && !strcmp(argv[1], "--selftest-const-licm"))
      return selftest_const_licm();
   if (argc > 1 && !strcmp(argv[1], "--selftest-index-masks"))
      return selftest_index_masks();
   if (argc > 1 && !strcmp(argv[1], "--selftest-max-layer"))
      return selftest_max_layer();
   if (production && (!root_views || !d3d_robust || in_bounds || soft_fault || fs_backend)) {
      fprintf(stderr, "--production: %s\n",
              !root_views ? "--root-views=UAV,CBV is required (the shader's measure-d3d12-root-views masks, 0,0 for none)"
                          : "--no-d3d-robust, --assume-in-bounds, --soft-fault and --fs-backend are not the production shape");
      return 2;
   }
   nir_shader *nir = NULL;
   void *bytes = NULL;
   unsigned reads = 0;
   if (argc > 1 && sscanf(argv[1], "--synthetic-uav-reads=%u", &reads) == 1 && reads && reads <= 64) {
      nir = synthetic_uav_reads(reads);
   } else if (argc > 1 && sscanf(argv[1], "--synthetic-shared-reads=%u", &reads) == 1 && reads && reads <= 32) {
      nir = synthetic_shared_reads(reads);
   } else if (argc > 1 && sscanf(argv[1], "--synthetic-shared-adjacent=%u", &reads) == 1 && reads <= 3) {
      nir = synthetic_shared_adjacent(reads);
   } else if (argc > 1 && sscanf(argv[1], "--synthetic-shared-stores=%u", &reads) == 1 && reads <= 8) {
      nir = synthetic_shared_stores(reads);
      shared_store_oracle = true;
   } else if (argc > 1 && sscanf(argv[1], "--synthetic-local-array=%u", &reads) == 1 && reads && reads <= 256) {
      nir = synthetic_local_array(reads);
   } else if (argc > 1 && sscanf(argv[1], "--synthetic-array-samples=%u", &reads) == 1 && reads && reads <= 16) {
      nir = synthetic_array_samples(reads, strstr(argv[1], ",heap") != NULL);
   } else if (argc > 1 && sscanf(argv[1], "--synthetic-loop-samples=%u", &reads) == 1 && reads && reads <= 8) {
      nir = synthetic_loop_samples(reads);
   } else if (argc > 1 && sscanf(argv[1], "--synthetic-break-loops=%u", &reads) == 1 && reads && reads <= 16) {
      nir = synthetic_break_loops(reads);
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
      if (print_bindings)
         for (unsigned i = 0; i < r.bindings.count; ++i)
            printf("BINDING kind %u space %u reg %u slot %u flags %u\n", r.bindings.bindings[i].kind,
                   r.bindings.bindings[i].space, r.bindings.bindings[i].reg, r.bindings.bindings[i].slot,
                   r.bindings.bindings[i].flags);
      nir = r.nir;
   }
   if (production && nir->info.stage != MESA_SHADER_COMPUTE) {
      fprintf(stderr, "--production: compute shaders only (stage %s)\n", mesa_shader_stage_name(nir->info.stage));
      return 2;
   }
   const bool fragment = fs_backend && nir->info.stage == MESA_SHADER_FRAGMENT;
   if (nir->info.stage != MESA_SHADER_COMPUTE && !fragment) {
      /* other stages: the D3D lowering's in-bounds path only (no backend
       * compile: their I/O lowering is Gallium's) */
      if (d3d_robust)
         NIR_PASS(_, nir, agx_win32_lower_d3d_buffer_access);
      /* rc119: inspect graphics stages too (discard, depth/sample-mask
       * writes, side effects that cost AGX hidden-surface removal). */
      if (print_nir || print_lowered)
         nir_print_shader(nir, stdout);
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
   NIR_PASS(_, nir, nir_lower_robust_access, gl_part_of_d3d_robustness, &d3d_robust);
   NIR_PASS(_, nir, agx_nir_lower_texture_early, true);
   if (fragment) {
      /* agx_shader_initialize: I/O lowering (fp16 varyings: see rc123) */
      NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out, fixture_type_size,
               nir_lower_io_lower_64bit_to_32 | nir_lower_io_use_interpolated_input_intrinsics);
      nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
      const uint64_t flat = nir->info.inputs_read & ~nir->info.linear_varyings & ~nir->info.perspective_varyings;
      NIR_PASS(_, nir, nir_lower_mediump_io, nir_var_shader_in | nir_var_shader_out,
               ~(flat | agx_gather_texcoords(nir)), false);
   }
   if (production) {
      /* agx_shader_initialize: agx_nir_lower_bindings (its own nir_lower_tex
       * and constant folding included) */
      agx_cost_production_bindings(nir);
   } else {
   NIR_PASS(_, nir, nir_lower_tex, &(nir_lower_tex_options){.lower_index_to_offset = true});
   NIR_PASS(_, nir, nir_opt_constant_folding);
   NIR_PASS(_, nir, nir_shader_instructions_pass, bind_resources, nir_metadata_control_flow, NULL);
   }
   /* agx_shader_initialize: so->info.nr_bindful_textures / _images */
   const unsigned descriptors = BITSET_LAST_BIT(nir->info.textures_used) + 2 * BITSET_LAST_BIT(nir->info.images_used);
   const unsigned bindful_textures = BITSET_LAST_BIT(nir->info.textures_used);
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
   /* T1: the D3D layer clamp without libagx_txs (APPLE_AGX_D3D_MAX_LAYER=1) */
   if (d3d_robust)
      NIR_PASS(_, nir, agx_win32_lower_d3d_layer_clamp);
   NIR_PASS(_, nir, agx_nir_lower_texture);
   /* T5b step A (native projection: project_crawl_bindful) */
   unsigned bindful_moved = 0;
   if (crawl_bindful)
      NIR_PASS(_, nir, agx_win32_d3d_crawl_bindful, bindful_textures, &bindful_moved);
   /* RC78: the D3D context's crawl placement (native projection) */
   if (d3d_robust)
      NIR_PASS(_, nir, agx_win32_speculate_texture_crawls, descriptors);
   /* M3: loop-invariant descriptor crawls leave their loops (native projection) */
   NIR_PASS(_, nir, agx_win32_hoist_loop_invariants);
   /* L4 (native projection: agx_win32_soft_faults(dev)) */
   NIR_PASS(_, nir, agx_win32_hoist_constant_loads, soft_fault_loads);
   const unsigned speculated = count_speculated(nir);
   NIR_PASS(_, nir, nir_lower_ssbo, NULL);
   agx_preprocess_nir(nir);
   if (fragment) {
      /* agx_state.c agx_compile_variant, FRAGMENT */
      NIR_PASS(_, nir, agx_nir_lower_discard_zs_emit);
      NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_fs_outputs, nir_metadata_control_flow, NULL);
      NIR_PASS(_, nir, agx_nir_lower_sample_mask);
      /* agx_compile_variant */
      NIR_PASS(_, nir, agx_nir_lower_interpolation);
   }

   /* agx_compile_nir */
   unsigned reserved_preamble = 0;
   if (production) {
      nir = agx_cost_production_round_trip(nir);
      reserved_preamble = agx_cost_production_sysvals(nir);
   } else
   NIR_PASS(_, nir, nir_shader_instructions_pass, lower_sysvals, nir_metadata_control_flow, NULL);
   if (print_nir)
      nir_print_shader(nir, stdout);
   /* J313: G13G (M1), no G13X cluster coherency; soft faults per flag */
   const unsigned mem_blocks = memory_blocks(nir);
   struct classes classes = {0};
   count_classes(nir, &classes);
   if (soft_fault_loads)
      NIR_PASS(_, nir, agx_win32_speculate_soft_fault_loads);
   const unsigned tex_speculated = count_tex_speculated(nir);
   struct sample_forms forms = {{0}};
   count_sample_forms(&nir_shader_get_entrypoint(nir)->body, 2, &forms);
   struct agx_shader_key key = {.dev = {.needs_g13x_coherency = U_TRISTATE_NO, .soft_fault = soft_fault},
                                .has_scratch = true, .promote_constants = true, .reserved_preamble = 240};
   struct agx_shader_part part = {0};
   if (production)
      key.reserved_preamble = reserved_preamble; /* agx_nir_layout_uniforms */
   agx_compile_shader_nir(nir, &key, &part);
   if (shared_store_oracle)
      print_shared_oracle(nir);
   if (print_selected) {
      puts("SELECTED MAIN BEGIN");
      agx2_disassemble((uint8_t *)part.binary + part.info.main_offset, part.info.main_size, stdout);
      puts("SELECTED MAIN END");
   }
   if (hex_main) {
      printf("MAIN HEX ");
      for (unsigned i = 0; i < part.info.main_size; ++i)
         printf("%02x", ((const uint8_t *)part.binary)[part.info.main_offset + i]);
      putchar('\n');
   }
   const struct agx2_stats *s = &part.info.stats;
   printf("COST stage %s block %ux%ux%u shared %u nir-ifs %u guarded-loads %u loads %u stores %u txf %u mem-blocks %u hot-mem-blocks %u hot-nir %u hot-branches %u "
          "instrs %u alu %u fscib %u ic %u gprs %u threads %u scratch %u spills %u fills %u loops %u code %u "
          "preamble %u nir-alu %u nir-mem %u nir-tex %u nir-shared %u barriers %u atomics %u root-marked %u "
          "crawl-speculated %u tex-speculated %u binary-fnv %08x shape %s\n",
          mesa_shader_stage_name(fragment ? MESA_SHADER_FRAGMENT : MESA_SHADER_COMPUTE), block[0], block[1], block[2], shared, counts.ifs,
          counts.guarded_loads, counts.loads, counts.stores, counts.txf, mem_blocks, hot.memory_blocks, hot.instrs, hot.branches, s->instrs, s->alu, s->fscib, s->ic,
          s->gprs, s->threads, s->scratch, s->spills, s->fills, s->loops, s->code_size, s->preamble_inst,
          classes.alu, classes.mem, classes.tex, classes.shared, classes.barriers, classes.atomics, marked,
          speculated, tex_speculated, binary_fnv(part.binary, part.info.binary_size),
          production ? "production" : "stand-in");
   /* T5b: samples by form (register / bindless) in innermost loops, other
    * loops, outside loops; step A's moved count */
   if (print_samples || crawl_bindful)
      printf("SAMPLES reg %u %u %u bindless %u %u %u moved %u\n", forms.reg[0], forms.reg[1], forms.reg[2],
          forms.bindless[0], forms.bindless[1], forms.bindless[2], bindful_moved);
   free(part.binary);
   ralloc_free(nir);
   glsl_type_singleton_decref();
   free(bytes);
   return 0;
}
