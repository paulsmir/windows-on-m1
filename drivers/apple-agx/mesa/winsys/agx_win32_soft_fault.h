/* RC82: speculative shader memory loads under GPU soft faults.
 *
 * Asahi's DRM_ASAHI_FEATURE_SOFT_FAULTS (include/drm-uapi/asahi_drm.h:204-
 * 215, set by the kernel only when fault_control is 0xb, file.rs:333-335):
 * "Shader loads of unmapped memory will return zero.  Shader stores to
 * unmapped memory will be silently discarded.  Note that only shader
 * load/store is affected.  Other hardware units are not affected, notably
 * including texture sampling."  Mesa consumes the feature in three places:
 *
 *  1. agx_optimize_nir (compiler/agx_compile.c:2865-2871, set_speculate)
 *     sets ACCESS_CAN_SPECULATE on every intrinsic that has an access
 *     qualifier and can_speculate on every texture instruction, so
 *     nir_opt_peephole_select and nir_opt_preamble may run them on paths the
 *     shader would not take;
 *  2. agx_nir_lower_vbo (lib/agx_nir_lower_vbo.c:197-245) drops the vertex
 *     index clamp and, for vec3+ attributes, replaces an out-of-bounds
 *     vertex buffer base with address 0, relying on Mesa's unmapped
 *     2^36-byte carveout at the bottom of the VM (lib/agx_device.c:565-590);
 *  3. hk only (Vulkan): unbounded constant loads.
 *
 * The Windows port takes 1 for shader memory loads only:
 *  - texture and image instructions stay as they are: the texture unit is
 *    not covered by soft faults, and a D3D12 shader's descriptor-heap index
 *    on a path it does not take may name any descriptor (an unwritten heap
 *    entry), so hoisting the sample could fault the texture unit;
 *  - 2 is not taken: on Windows the bottom of the GPU VA space is not
 *    reserved (agx_win32_gpuva.c places BOs from AGX_GPUVA_PAGE, the D3D12
 *    heaps from 0x10000), so a load at "address 0 + offset" can read a live
 *    allocation instead of zero.  The projection keeps the vertex prolog's
 *    robustness.soft_fault false and the compiler key's soft_fault false,
 *    and runs this pass right before agx_compile_shader_nir instead.
 *
 * nir_instr_can_speculate also requires nir_intrinsic_can_reorder, so a
 * load that is not ACCESS_CAN_REORDER (a writable UAV read, a volatile or
 * coherent access) is not moved even with the flag; this matches Asahi.
 * Running a speculated load on a path the shader would not take reads
 * either mapped memory of the same process VM (the value is discarded) or
 * an unmapped page (zero, no fault, under soft faults).
 */
#ifndef AGX_WIN32_SOFT_FAULT_H
#define AGX_WIN32_SOFT_FAULT_H

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "util/set.h"
#include <stdlib.h>

static inline bool
agx_win32_soft_fault_load(nir_intrinsic_op op)
{
   switch (op) {
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
   case nir_intrinsic_load_global_constant_offset:
   case nir_intrinsic_load_global_constant_bounded:
   case nir_intrinsic_load_global_bounded:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_agx:
   case nir_intrinsic_load_constant_agx:
      return true;
   default:
      return false;
   }
}

static inline bool
agx_win32_speculate_soft_fault_load(nir_builder *b, nir_intrinsic_instr *intr,
                                    void *data)
{
   (void)b;
   (void)data;
   if (!agx_win32_soft_fault_load(intr->intrinsic) ||
       !nir_intrinsic_has_access(intr))
      return false;
   const enum gl_access_qualifier access = nir_intrinsic_access(intr);
   if (access & ACCESS_CAN_SPECULATE)
      return false;
   /* explicit cast: the x86 build compiles some C as C++ (/TP) */
   nir_intrinsic_set_access(intr, (enum gl_access_qualifier)(access | ACCESS_CAN_SPECULATE));
   return true;
}

/* Marks the shader's memory loads ACCESS_CAN_SPECULATE (Asahi's
 * set_speculate restricted to the load/store unit).  Call it only when the
 * KMD reports soft faults active (AGX_WIN32_DEVICE_FEATURE_SOFT_FAULTS). */
static inline bool
agx_win32_speculate_soft_fault_loads(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, agx_win32_speculate_soft_fault_load,
                                     nir_metadata_all, NULL);
}

/* L4 (C4 traversal): GravityMark's BVH traversal loop reads four constant
 * buffer words at cb[shared[12] * 57 + 48 + k]; shared[12] is written once
 * before the workgroup barrier and the loop body stores no shared memory,
 * so the loads are loop-invariant, but two sit in conditional blocks and
 * load_shared is not ACCESS_CAN_REORDER, so M3's non-speculative LICM keeps
 * all four (two serialized memory round trips of nine per node visit; the
 * macOS compile keeps these values in uniforms).
 *
 * In a loop nest that contains no shared store, shared atomic, barrier or
 * call, a load_shared that this invocation reads repeatedly returns the
 * value this invocation last observed: no instruction of the nest changes
 * it, and another invocation's write without an intervening barrier is a
 * data race (D3D: groupshared writes are visible to the group only after a
 * GroupMemoryBarrier), so reading it once before the nest is a permitted
 * execution.  Only such loads that feed the address of a reorderable
 * memory load (<= 6 ALU levels) are marked CAN_REORDER, for the duration of
 * this pass only, which then hoists with nir_opt_licm: blocks that dominate
 * the exit as M3 does; elsewhere (speculatively) only constants, those
 * marked loads, ALU feeding such an address and reorderable memory loads,
 * which only soft faults make safe on a path the shader would not take
 * (soft_faults = agx_win32_soft_faults(dev); the pass does nothing without
 * them).  It runs beside M3, before the address lowering turns the
 * 32-bit offset chains into 64-bit address arithmetic.
 * APPLE_AGX_D3D_LICM_CONST=1 enables it (default off pending hardware). */
static inline bool
agx_win32_nest_writes_shared(nir_loop *loop)
{
   nir_foreach_block_in_cf_node(block, &loop->cf_node) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_call)
            return true;
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         switch (nir_instr_as_intrinsic(instr)->intrinsic) {
         case nir_intrinsic_store_shared:
         case nir_intrinsic_store_shared_block_intel:
         case nir_intrinsic_shared_atomic:
         case nir_intrinsic_shared_atomic_swap:
         case nir_intrinsic_shared_append_amd:
         case nir_intrinsic_shared_consume_amd:
         case nir_intrinsic_barrier:
            return true;
         default:
            break;
         }
      }
   }
   return false;
}

static inline bool
agx_win32_reorderable_load(nir_instr *instr)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *in = nir_instr_as_intrinsic(instr);
   /* load_global_constant / load_ubo are reorderable by definition
    * (access=none); a UAV load only with ACCESS_CAN_REORDER */
   return agx_win32_soft_fault_load(in->intrinsic) && nir_intrinsic_can_reorder(in);
}

static inline bool
agx_win32_feeds_load_address(nir_def *def, unsigned depth)
{
   if (depth > 6)
      return false;
   nir_foreach_use(src, def) {
      nir_instr *use = nir_src_use_instr(src);
      if (agx_win32_reorderable_load(use))
         return true;
      if (use->type == nir_instr_type_alu &&
          agx_win32_feeds_load_address(&nir_instr_as_alu(use)->def, depth + 1))
         return true;
   }
   return false;
}

static inline void
agx_win32_mark_invariant_shared(struct exec_list *list, struct set *marked)
{
   foreach_list_typed(nir_cf_node, node, node, list) {
      if (node->type == nir_cf_node_if) {
         nir_if *nif = nir_cf_node_as_if(node);
         agx_win32_mark_invariant_shared(&nif->then_list, marked);
         agx_win32_mark_invariant_shared(&nif->else_list, marked);
      } else if (node->type == nir_cf_node_loop) {
         nir_loop *loop = nir_cf_node_as_loop(node);
         /* Only outermost nests without writers: nir_opt_licm may carry a
          * marked load out of every enclosing loop, so no enclosing loop may
          * change shared memory; nested loops of a writing loop stay
          * unmarked. */
         if (agx_win32_nest_writes_shared(loop))
            continue;
         nir_foreach_block_in_cf_node(block, &loop->cf_node) {
            nir_foreach_instr(instr, block) {
               if (instr->type != nir_instr_type_intrinsic)
                  continue;
               nir_intrinsic_instr *in = nir_instr_as_intrinsic(instr);
               if (in->intrinsic != nir_intrinsic_load_shared)
                  continue;
               const enum gl_access_qualifier a = nir_intrinsic_access(in);
               if ((a & (ACCESS_VOLATILE | ACCESS_COHERENT | ACCESS_CAN_REORDER)) ||
                   !agx_win32_feeds_load_address(&in->def, 0))
                  continue;
               nir_intrinsic_set_access(in, (enum gl_access_qualifier)(a | ACCESS_CAN_REORDER));
               _mesa_set_add(marked, in);
            }
         }
      }
   }
}

/* Stateless (shader compiles may run on several threads): the only
 * CAN_REORDER load_shared this filter admits are those feeding a load
 * address, i.e. the ones the marking step considers. */
static inline bool
agx_win32_const_licm_filter(nir_instr *instr, nir_loop *loop, bool dominates_exit)
{
   (void)loop;
   if (dominates_exit || instr->type == nir_instr_type_load_const)
      return true;
   if (instr->type == nir_instr_type_alu)
      return agx_win32_feeds_load_address(&nir_instr_as_alu(instr)->def, 0);
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *in = nir_instr_as_intrinsic(instr);
   if (in->intrinsic == nir_intrinsic_load_shared)
      return agx_win32_feeds_load_address(&in->def, 0);
   /* a reorderable memory load on a path the shader would not take reads
    * mapped memory (discarded) or an unmapped page (zero) under soft faults */
   return agx_win32_reorderable_load(instr);
}

static inline bool
agx_win32_hoist_constant_loads(nir_shader *nir, bool soft_faults)
{
   static int enabled = -1;
   if (enabled < 0) {
      const char *env = getenv("APPLE_AGX_D3D_LICM_CONST");
      enabled = env && env[0] == '1' && !env[1];
   }
   if (!enabled || !soft_faults)
      return false;
   struct set *marked = _mesa_pointer_set_create(NULL);
   nir_foreach_function_impl(impl, nir)
      agx_win32_mark_invariant_shared(&impl->body, marked);
   bool progress = false;
   if (marked->entries) {
      NIR_PASS(progress, nir, nir_opt_cse);
   }
   NIR_PASS(progress, nir, nir_opt_licm, agx_win32_const_licm_filter);
   /* Restore the marked loads' qualifiers; look up live instructions only
    * (CSE may have removed some marked ones). */
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type == nir_instr_type_intrinsic && _mesa_set_search(marked, instr)) {
               nir_intrinsic_instr *in = nir_instr_as_intrinsic(instr);
               nir_intrinsic_set_access(in, (enum gl_access_qualifier)(nir_intrinsic_access(in) &
                                                                       ~ACCESS_CAN_REORDER));
            }
         }
      }
   }
   _mesa_set_destroy(marked, NULL);
   return progress;
}

#endif
