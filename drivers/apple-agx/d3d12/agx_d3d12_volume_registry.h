/* C21G (investigation/analysis/D3D12-C21G-volume-generality.md): the
 * device-dependent Texture3D layout registry and per-resource selection.
 *
 * AGX GPU-tiled levels (Asahi ail_initialize_gpu_tiled) use either the max
 * tile (16 KiB) or a square power-of-two tile; inside a tile the element
 * offset interleaves X and Y bits, X first, with the longer side's leftover
 * bits on top (ail_space_bits). Per element size the registry lists every
 * tile shape a level can have, largest first: pattern i describes shape i
 * exactly, and layout i has the pattern slots [i, i + 1, ...]. A resource
 * picks the layout of its first level's shape and advances one slot per
 * shape change (an unused shape is an empty slot: equal transitions).
 * Exact shapes are used while they fit the slots (the C21 layouts keep their
 * values); otherwise a level that is one square tile no larger than the
 * current shape keeps the current slot: its Morton order is the low bits of
 * the larger interleave.
 * Pure C, no Windows or Mesa headers: the DDI and the host oracle test
 * (tests/test_d3d12_volume_registry.py, against AIL) include it. */
#ifndef AGX_D3D12_VOLUME_REGISTRY_H
#define AGX_D3D12_VOLUME_REGISTRY_H

#include <stdint.h>

#define AGX_VOLUME_ELEMENT_SIZES 5u /* 1, 2, 4, 8 and 16 bytes */
#define AGX_VOLUME_PATTERNS 38u
#define AGX_VOLUME_MAX_SLOTS 6u
#define AGX_VOLUME_MAX_LEVELS 16u
#define AGX_VOLUME_NO_TRANSITION 0xffu

typedef struct {
   uint16_t Width, Height; /* tile in elements */
} AGX_VOLUME_SHAPE;

/* the shapes of element size 1 << e (e = 0..4), largest first: the max tile
 * (ail_get_max_tile_size), then the square POT tiles (2 and 8 bytes also
 * have a square tile of the long side: POT(min(w, h)) can exceed the max
 * tile's height) */
static inline unsigned
agx_volume_shape_count(unsigned e)
{
   static const unsigned counts[AGX_VOLUME_ELEMENT_SIZES] = {8, 9, 7, 8, 6};
   return e < AGX_VOLUME_ELEMENT_SIZES ? counts[e] : 0u;
}

static inline AGX_VOLUME_SHAPE
agx_volume_shape(unsigned e, unsigned i)
{
   static const AGX_VOLUME_SHAPE max_tile[AGX_VOLUME_ELEMENT_SIZES] = {
      {128, 128}, {128, 64}, {64, 64}, {64, 32}, {32, 32}};
   AGX_VOLUME_SHAPE shape = {0, 0};
   if (e >= AGX_VOLUME_ELEMENT_SIZES || i >= agx_volume_shape_count(e))
      return shape;
   if (!i)
      return max_tile[e];
   /* square sides after the max tile: 2 and 8 bytes start at the long side */
   const unsigned first = max_tile[e].Width == max_tile[e].Height ? max_tile[e].Width / 2u : max_tile[e].Width;
   shape.Width = shape.Height = (uint16_t)(first >> (i - 1u));
   return shape;
}

/* global pattern (= layout) index of shape i of element size e */
static inline unsigned
agx_volume_pattern_index(unsigned e, unsigned i)
{
   unsigned base = 0;
   for (unsigned k = 0; k < e && k < AGX_VOLUME_ELEMENT_SIZES; ++k)
      base += agx_volume_shape_count(k);
   return base + i;
}

/* the element size and shape of a global pattern index; 0 when out of range */
static inline int
agx_volume_pattern_shape(unsigned pattern, unsigned *e, unsigned *i)
{
   for (unsigned k = 0; k < AGX_VOLUME_ELEMENT_SIZES; ++k) {
      if (pattern < agx_volume_shape_count(k)) {
         *e = k;
         *i = pattern;
         return 1;
      }
      pattern -= agx_volume_shape_count(k);
   }
   return 0;
}

/* the pattern of layout slot k: slot k of layout (e, i) is shape i + k,
 * clamped at the last shape */
static inline unsigned
agx_volume_layout_slot(unsigned layout, unsigned k)
{
   unsigned e = 0, i = 0;
   if (!agx_volume_pattern_shape(layout, &e, &i))
      return 0;
   const unsigned last = agx_volume_shape_count(e) - 1u;
   return agx_volume_pattern_index(e, i + k < last ? i + k : last);
}

/* one interleave entry: channel 0 = X (in bytes), 1 = Y */
typedef struct {
   uint8_t Channel, SourceBit;
} AGX_VOLUME_BIT;

/* (no compound literals: the DDI includes this header as C++) */
static inline AGX_VOLUME_BIT
agx_volume_bit(unsigned channel, unsigned source)
{
   AGX_VOLUME_BIT bit;
   bit.Channel = (uint8_t)channel;
   bit.SourceBit = (uint8_t)source;
   return bit;
}

/* the interleave bits of a pattern, lowest address bit first (at most 32);
 * returns the count, 0 for an unknown pattern */
static inline unsigned
agx_volume_pattern_bits(unsigned pattern, AGX_VOLUME_BIT bits[32])
{
   unsigned e = 0, i = 0, n = 0;
   if (!agx_volume_pattern_shape(pattern, &e, &i))
      return 0;
   const AGX_VOLUME_SHAPE shape = agx_volume_shape(e, i);
   unsigned xbits = 0, ybits = 0;
   while ((1u << xbits) < shape.Width) ++xbits;
   while ((1u << ybits) < shape.Height) ++ybits;
   for (unsigned b = 0; b < e; ++b)
      bits[n++] = agx_volume_bit(0, b);
   for (unsigned b = 0; b < xbits || b < ybits; ++b) {
      if (b < xbits) bits[n++] = agx_volume_bit(0, e + b);
      if (b < ybits) bits[n++] = agx_volume_bit(1, b);
   }
   return n;
}

/* a requested level of the resource, from the native (AIL) layout */
typedef struct {
   uint32_t TileWidth, TileHeight; /* tilesize_el */
   uint32_t StrideEl;              /* stride_el: the level's width in elements */
   uint32_t HeightEl;              /* the level's height in elements */
} AGX_VOLUME_LEVEL;

typedef struct {
   unsigned Layout;                               /* global layout index */
   uint8_t Transitions[AGX_VOLUME_MAX_SLOTS - 1]; /* first level of slot k + 1 */
   unsigned Pattern[AGX_VOLUME_MAX_LEVELS];       /* each level's slot pattern */
   unsigned SlotsUsed;
} AGX_VOLUME_SELECTION;

static inline int
agx_volume_shape_of(unsigned e, uint32_t w, uint32_t h, unsigned *i)
{
   for (unsigned k = 0; k < agx_volume_shape_count(e); ++k) {
      const AGX_VOLUME_SHAPE s = agx_volume_shape(e, k);
      if (s.Width == w && s.Height == h) {
         *i = k;
         return 1;
      }
   }
   return 0;
}

/* one selection pass: `prefix` merges single square tiles into the current
 * slot */
static inline int
agx_volume_select_pass(unsigned e, unsigned levels, const AGX_VOLUME_LEVEL *level, unsigned max_slots, int prefix,
                       AGX_VOLUME_SELECTION *out)
{
   unsigned first = 0, cur = 0, slot = 0;
   if (!out || !level || !levels || levels > AGX_VOLUME_MAX_LEVELS || !max_slots ||
       max_slots > AGX_VOLUME_MAX_SLOTS || e >= AGX_VOLUME_ELEMENT_SIZES)
      return 0;
   for (unsigned k = 0; k < AGX_VOLUME_MAX_SLOTS - 1u; ++k)
      out->Transitions[k] = AGX_VOLUME_NO_TRANSITION;
   if (!agx_volume_shape_of(e, level[0].TileWidth, level[0].TileHeight, &first))
      return 0;
   cur = first;
   out->Pattern[0] = agx_volume_pattern_index(e, cur);
   for (unsigned l = 1; l < levels; ++l) {
      const AGX_VOLUME_LEVEL *lv = &level[l];
      const AGX_VOLUME_SHAPE current = agx_volume_shape(e, cur);
      const unsigned limit = current.Width < current.Height ? current.Width : current.Height;
      const int single = lv->TileWidth && lv->TileHeight && lv->StrideEl <= lv->TileWidth &&
                         lv->HeightEl <= lv->TileHeight;
      unsigned exact = 0;
      if (prefix && single && lv->TileWidth == lv->TileHeight && lv->TileWidth <= limit) {
         out->Pattern[l] = agx_volume_pattern_index(e, cur);
         continue; /* the square-prefix rule */
      }
      if (!agx_volume_shape_of(e, lv->TileWidth, lv->TileHeight, &exact) || exact < cur)
         return 0;
      for (; cur < exact; ++cur) {
         if (slot + 1u >= max_slots)
            return 0;
         out->Transitions[slot++] = (uint8_t)l; /* skipped shapes: empty slots */
      }
      out->Pattern[l] = agx_volume_pattern_index(e, cur);
   }
   out->Layout = agx_volume_pattern_index(e, first);
   out->SlotsUsed = slot + 1u;
   return 1;
}

/* the layout and transitions of a resource with element size 1 << e and
 * `levels` requested levels, within `max_slots` pattern slots (2: the 0003
 * interface, 6: 0022): exact shapes when they fit, else the square-prefix
 * merge; 0 when it cannot be described */
static inline int
agx_volume_select(unsigned e, unsigned levels, const AGX_VOLUME_LEVEL *level, unsigned max_slots,
                  AGX_VOLUME_SELECTION *out)
{
   return agx_volume_select_pass(e, levels, level, max_slots, 0, out) ||
          agx_volume_select_pass(e, levels, level, max_slots, 1, out);
}

/* a level's bytes per row of blocks: tiles per row times the slot shape's
 * block (one row for a single-tile level) */
static inline uint64_t
agx_volume_row_stride(unsigned pattern, const AGX_VOLUME_LEVEL *level)
{
   unsigned e = 0, i = 0;
   if (!agx_volume_pattern_shape(pattern, &e, &i) || !level)
      return 0;
   const AGX_VOLUME_SHAPE s = agx_volume_shape(e, i);
   const uint64_t block = (uint64_t)s.Width * s.Height << e;
   return (uint64_t)((level->StrideEl + s.Width - 1u) / s.Width) * block;
}

/* the byte offset inside the level slice of element (x, y), byte b, through
 * the pattern and row stride (the D3D parameterized swizzle model) */
static inline uint64_t
agx_volume_address(unsigned pattern, uint64_t row_stride, uint32_t x, uint32_t y, uint32_t b)
{
   unsigned e = 0, i = 0;
   AGX_VOLUME_BIT bits[32];
   if (!agx_volume_pattern_shape(pattern, &e, &i))
      return ~0ull;
   const AGX_VOLUME_SHAPE s = agx_volume_shape(e, i);
   const unsigned n = agx_volume_pattern_bits(pattern, bits);
   const uint64_t block = (uint64_t)s.Width * s.Height << e;
   const uint32_t xb = ((x % s.Width) << e) | b, yb = y % s.Height;
   uint64_t offset = 0;
   for (unsigned k = 0; k < n; ++k)
      offset |= (uint64_t)(((bits[k].Channel ? yb : xb) >> bits[k].SourceBit) & 1u) << k;
   return (uint64_t)(y / s.Height) * row_stride + (uint64_t)(x / s.Width) * block + offset;
}

#endif
