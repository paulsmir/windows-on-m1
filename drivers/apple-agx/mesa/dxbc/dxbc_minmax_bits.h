/* C65: the heap sampler map word's min/max bits (dxbc_minmax.h), without
 * NIR, for the owner that packs them. */
#ifndef DXBC_MINMAX_BITS_H
#define DXBC_MINMAX_BITS_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

#define DXBC_SAMPLER_HANDLE_MASK 0x7ffu
#define DXBC_SAMPLER_MINMAX_SHIFT 11u

/* The 5 map-word bits of a D3D12 filter (D3D12_FILTER encoding: mip bits
 * 0-1, mag 2-3, min 4-5, anisotropic 0x40, reduction 7-8). */
static inline unsigned
dxbc_minmax_bits(unsigned filter)
{
   const unsigned aniso = (filter & 0x40u) != 0;
   return ((filter >> 7) & 3u) |
          ((aniso || ((filter >> 4) & 3u)) ? 4u : 0u) |
          ((aniso || ((filter >> 2) & 3u)) ? 8u : 0u) |
          ((aniso || (filter & 3u)) ? 16u : 0u);
}

/* C65S: slot (and static) samplers, whose sampler is known when a PSO's
 * min/max variant is compiled: wraps every sample through slot sampler s
 * (sampler_index s < DXBC_MINMAX_SLOTS, no sampler handle, deref or
 * dynamic offset) whose bits[s] (dxbc_minmax_bits) carry reduction 2 or 3,
 * with those bits as a constant -- the branch folds to the emulation alone.
 * Returns whether it wrapped any (dxbc_minmax.c; declared here without NIR
 * headers for the C++ owner). */
#define DXBC_MINMAX_SLOTS 16u
struct nir_shader;
#ifdef __cplusplus
extern "C"
#endif
bool dxbc_minmax_wrap_slots(struct nir_shader *shader, const unsigned char bits[DXBC_MINMAX_SLOTS]);

#endif
