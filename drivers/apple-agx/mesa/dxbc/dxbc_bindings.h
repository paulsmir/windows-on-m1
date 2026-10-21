/* Immutable shader-declaration namespace map shared by lowering and replay.
 * SPDX-License-Identifier: MIT
 */
#ifndef DXBC_BINDINGS_H
#define DXBC_BINDINGS_H

#include <stdint.h>

enum dxbc_binding_kind {
   DXBC_BINDING_CBV = 0,
   DXBC_BINDING_SRV = 1,
   DXBC_BINDING_UAV = 2,
   DXBC_BINDING_SAMPLER = 3,
   DXBC_BINDING_KIND_COUNT = 4,
};

/* the slots (14 CBVs, 128 SRVs, 8 UAVs, 16 samplers) and the heap ranges
 * (DXBC_HEAP_RANGE_MAX) */
#define DXBC_BINDING_MAP_MAX (166 + 64)

/* C34: SM5.1 SRV and sampler ranges flatten to one binding per register in
 * consecutive slots; the tail of an unbounded range (T[n:*]) is optional --
 * replay leaves a register the root signature does not cover unbound. */
#define DXBC_BINDING_OPTIONAL 1u

/* C47 (Tier 2): a caller sets DXBC_BINDING_HEAP_RANGES in the SRV limit to
 * place an SM5.1 typed SRV range that is unbounded or does not fit the free
 * slots in the descriptor heap instead: one binding flagged
 * DXBC_BINDING_HEAP, its slot the heap range index (< DXBC_HEAP_RANGE_MAX)
 * and reg the range's first register. The shader reads the heap index of
 * that first register from constant buffer DXBC_HEAP_TABLE_CB (vec4 per
 * range, .x), adds the accessed register's offset, and takes the AGX
 * descriptor at that index of the shader-visible heap (DXBC_HEAP_DESCRIPTOR_STRIDE
 * bytes each, the AGX texture descriptor DXBC_HEAP_DESCRIPTOR_AGX_OFFSET into
 * it; the heap is bound as constant buffer DXBC_HEAP_MIRROR_CB)
 * through a bindless handle marked DXBC_HEAP_HANDLE_MARK. */
#define DXBC_BINDING_HEAP 2u
#define DXBC_BINDING_HEAP_RANGES 0x80000000u
/* Tier 2: 64 UAVs per pipeline, which a shader may declare one register at
 * a time: 8 in slots, 56 heap ranges */
#define DXBC_HEAP_RANGE_MAX 64u
#define DXBC_HEAP_TABLE_CB 14u
#define DXBC_HEAP_MIRROR_CB 15u
#define DXBC_HEAP_DESCRIPTOR_STRIDE 160u
#define DXBC_HEAP_DESCRIPTOR_AGX_OFFSET 64u /* the AGX texture descriptor in each */
#define DXBC_HEAP_DESCRIPTOR_PBE_DELTA 24u /* its PBE descriptor (typed and buffer UAVs) follows */
#define DXBC_HEAP_DESCRIPTOR_COUNTER_DELTA 48u /* a structured UAV counter's PBE (R32_UINT word) */
#define DXBC_HEAP_HANDLE_MARK 0xd312u
/* C47: heap sampler ranges read their sampler from the batch's AGX sampler
 * heap: constant buffer DXBC_HEAP_TABLE_CB holds, after the vec4 per range
 * (.x first heap index, .y the D3D12 sampler-heap index of the map's first
 * entry), one dword per sampler-heap entry from that index: the AGX sampler
 * handle in bits 0-15 and the sampler's MipLODBias as fp16 in bits 16-31,
 * which the shader applies (sampler index DXBC_HEAP_SAMPLER_INDEX keeps
 * Asahi's own bias lowering at 0) */
#define DXBC_HEAP_SAMPLER_MAP_OFFSET (16u * DXBC_HEAP_RANGE_MAX)
#define DXBC_HEAP_SAMPLER_MAP_MAX 2048u
#define DXBC_HEAP_SAMPLER_INDEX 31u

struct dxbc_binding {
   uint32_t kind, id, space, reg, slot, flags;
};

struct dxbc_binding_map {
   uint32_t active, count;
   struct dxbc_binding bindings[DXBC_BINDING_MAP_MAX];
};

/* IDs identify declarations inside one shader.  Root replay instead looks
 * up the semantic namespace key (kind, space, logical register). */
static inline const struct dxbc_binding *
dxbc_binding_find(const struct dxbc_binding_map *map, uint32_t kind,
                  uint32_t space, uint32_t reg)
{
   if (!map || !map->active || map->count > DXBC_BINDING_MAP_MAX)
      return 0;
   for (uint32_t i = 0; i < map->count; ++i) {
      const struct dxbc_binding *binding = &map->bindings[i];
      if (binding->kind == kind && binding->space == space && binding->reg == reg)
         return binding;
   }
   return 0;
}

#endif /* DXBC_BINDINGS_H */
