/* C65 (investigation/analysis/D3D12-C65-minmax-filtering.md): min/max
 * reduction filtering, which the AGX sampler lacks, emulated in the shader.
 * A heap sampler's map word carries, besides its handle (bits 0-10) and its
 * fp16 MipLODBias (bits 16-31), the reduction and filter bits 11-15:
 *   bits 11-12: D3D12_FILTER_REDUCTION_TYPE (0 standard, 1 comparison,
 *               2 minimum, 3 maximum); bit 13 linear minification;
 *   bit 14 linear magnification; bit 15 linear mip filter.
 * The owner packs them (AgxD3d12OwnerSamplerDescriptor); the frontends mask
 * the handle and hand the bits to dxbc_minmax_wrap. */
#ifndef DXBC_MINMAX_H
#define DXBC_MINMAX_H

#include "nir.h"
#include "nir_builder.h"
#include "dxbc_minmax_bits.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Replaces the uses of tex's result (a float 2D or 2D-array sample:
 * tex/txb/txl/txd, no comparison) by a run-time choice: the hardware sample
 * for standard reduction, or the component-wise min/max of the texels the
 * filter would weight (zero-weight texels excluded) for bits 2/3. minmax is
 * the 5-bit field (32-bit) and must dominate tex. Returns the replacement
 * (the cursor is left after it), or NULL for a sample it leaves alone. */
nir_def *dxbc_minmax_wrap(nir_builder *b, nir_tex_instr *tex, nir_def *minmax);
/* dxbc_minmax_wrap_slots: dxbc_minmax_bits.h */

#ifdef __cplusplus
}
#endif

#endif
