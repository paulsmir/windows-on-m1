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

#endif
