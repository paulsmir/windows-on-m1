/* Original CPU fixture boundary, field-derived from pinned agx_state.h.
 * Not a native-resource ABI/full lifecycle claim. No DRM declarations/shims.
 * Real pipe, AIL, BO/VA and packing types are included from their real headers.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pipe/p_state.h"
#include "util/u_inlines.h"
#include "util/u_helpers.h"
#include "util/bitset.h"
#include "util/format/u_format.h"
#include "asahi/layout/layout.h"
#include "asahi/lib/agx_bo.h"
#include "asahi/lib/agx_helpers.h"
#include "asahi/compiler/agx_nir_texture.h"
#include "asahi/genxml/agx_pack.h"
struct agx_batch; /* actual PBE packer does not dereference this argument */
struct renderonly_scanout;
/* Prefix fields through layout match actual inspected declaration; trailing
 * unrelated lifecycle state is never used by the selected functions. */
struct agx_resource {
 struct pipe_resource base;
 uint64_t modifier;
 bool mipmapped;
 struct agx_bo *bo;
 struct renderonly_scanout *scanout;
 BITSET_DECLARE(data_valid, PIPE_MAX_TEXTURE_LEVELS);
 struct ail_layout layout;
};
