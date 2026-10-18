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

#define DXBC_BINDING_MAP_MAX 166

struct dxbc_binding {
   uint32_t kind, id, space, reg, slot;
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
