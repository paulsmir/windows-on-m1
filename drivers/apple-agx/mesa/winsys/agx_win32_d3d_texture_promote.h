/* T5: texture-state promotion of constant texture handles (D3D12, opt-in).
 *
 * A sample names its texture either as a texture-state register (an
 * immediate index) or as heap uniform plus a 32-bit offset register (the
 * bindless form).  Gallium's agx_nir_lower_bindings keeps a texture bindful
 * only for a table index below AGX_NUM_TEXTURE_STATE_REGS (16) without an
 * offset or a descriptor crawl; every other texture of a D3D12 shader, also
 * one at a compile-time constant table index, is sampled bindless, and the
 * handle is rematerialised next to the sample.  GravityMark's lighting
 * kernel 806ee7b1bdb1944a samples its shadow map (table index 17, a 2D
 * array) bindless 16 times inside the filter loops, the planet PS
 * 428487dae22a5191 has 75 bindless samples of 79.  Apple's binaries of the
 * same kernels copy those descriptors once into texture-state registers in
 * the preamble (tex_state_store) and keep only the data-dependent handles
 * bindless.
 *
 * The compiler has the mechanism (agx_nir_opt_preamble's image class) behind
 * agx_shader_key::promote_textures, which the gallium driver never sets.
 * With APPLE_AGX_D3D_PROMOTE_TEX=1 (read by the D3D12 owner at device
 * creation into AGX_WIN32_ASAHI_BACKEND::D3dPromoteTextures; default off)
 * agx_compile_nir sets the key bit for the application's compute and
 * fragment shaders (and for the pre-GS kernel, which Gallium compiles as an
 * ordinary compute shader; it has no textures, is never promoted, and is
 * launched through the same CDM word), and the compiler projection
 * (scripts/native-asahi-latency-schedule.py, PROMOTE_TEXTURES):
 *  - promotes only a shader that samples a constant handle inside a loop;
 *    every other shader compiles byte-identical to the switch being off;
 *  - merges the handle definitions of one table entry into one, so an entry
 *    takes one register;
 *  - places promoted handles from register 16 up.  The USC TEXTURE word
 *    loads registers [0, min(descriptors, 16)) from the bindful table and a
 *    fragment shader's spilled render targets make that count a property of
 *    the batch, so 16 is the first register no batch loads; the registers
 *    from 16 are written only by the shader's own preamble.
 * The promoted handle reads the same descriptor (same table or heap, same
 * offset) the bindless sample would read, once per draw or dispatch instead
 * of per sample; D3D12 fixes a draw's descriptors before it executes.
 * Every constant handle of a promoted shader takes a register, also the
 * PBE descriptor of an image write (the lighting kernel's output), which
 * a bindful image write names through a texture-state register as well.
 * The handle offsets no longer take preamble uniform storage, so the pass
 * may hoist another value in their place; in the two GravityMark kernels
 * compiled as the driver compiles them only the handle copies leave the
 * main function.
 *
 * agx_shader_info::texture_state_count is then the end of the promoted
 * range in the preamble pass's 16-bit units, two per register, and 0 for a
 * shader without promoted handles.  The USC "Texture state register count"
 * of the draw or launch must cover both ranges:
 * agx_win32_d3d_texture_state_registers.  The field holds up to 256
 * registers in groups of 8 (5 bits, 0 = all); tex_state_store reaches
 * register 127.  How many registers the hardware has beyond the 28 Apple
 * uses in one shader is not known; the count is not capped here.
 */
#ifndef AGX_WIN32_D3D_TEXTURE_PROMOTE_H
#define AGX_WIN32_D3D_TEXTURE_PROMOTE_H

#include <stdbool.h>
#include <stdint.h>

#include "compiler/shader_enums.h"

/* The shaders whose compiler key asks for promotion: the application's
 * compute and fragment shaders of a backend that opted in. */
static inline bool
agx_win32_d3d_promote_textures(bool enabled, mesa_shader_stage stage,
                               bool internal_kernel, bool secondary)
{
   return enabled && !internal_kernel && !secondary &&
          (stage == MESA_SHADER_COMPUTE || stage == MESA_SHADER_FRAGMENT);
}

/* USC texture-state register count: the bindful descriptors of the batch
 * (registers from 0) and the promoted range, whose end the compiler returns
 * in 16-bit units (agx_shader_info::texture_state_count, 0: none). */
static inline uint32_t
agx_win32_d3d_texture_state_registers(uint32_t descriptors,
                                      uint32_t promoted_units)
{
   const uint32_t promoted_end = (promoted_units + 1u) / 2u;

   return descriptors > promoted_end ? descriptors : promoted_end;
}

#endif
