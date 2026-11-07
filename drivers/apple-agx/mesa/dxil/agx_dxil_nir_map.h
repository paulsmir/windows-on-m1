/* Original unlinked descriptor-namespace adapter proposal. SPDX-License-Identifier: MIT */
#ifndef AGX_DXIL_NIR_MAP_H
#define AGX_DXIL_NIR_MAP_H
#include <stdint.h>
#include "dxbc_bindings.h"
struct nir_shader;
enum agx_dxil_nir_map_status {
   AGX_DXIL_NIR_MAP_OK = 0,
   AGX_DXIL_NIR_MAP_INVALID = 1,
   AGX_DXIL_NIR_MAP_UNSUPPORTED = 2,
};
struct agx_dxil_nir_map_stats { uint32_t ubo_indices, ssbo_indices, descriptors, srv_fetches; };
/* Caller supplies valid owned NIR from the admitted compute6.0 singleton
 * CBV/rawSRV/rawUAV translator profile. This is descriptor adaptation, not a
 * shader-operation/capability admission validator. NIR must be inlined and
 * deref-optimized, before nir_lower_samplers/explicit_io. UBO/SSBO frontend
 * address formats MUST both be nir_address_format_32bit_index_offset.
 * Shader, map and stats must be valid and disjoint.
 * Structural refusal mutates neither shader nor map; stats stays zero.
 * Success mutates shader, never map. No allocation-failure recovery claim. */
enum agx_dxil_nir_map_status agx_dxil_nir_lower_map(
   struct nir_shader *, const struct dxbc_binding_map *, struct agx_dxil_nir_map_stats *);
#endif
