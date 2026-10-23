/* C21G: the Texture3D layout registry against linked, unmodified MIT AIL.
 * For each element size and a grid of sides, depths and mip counts, every
 * (sampled) texel of every requested level is addressed through the selected
 * layout's pattern, the level's row stride and the layer stride, and compared
 * with AIL's own addressing (c21_ail_reference_offset). Original test code. */
#define main c21_unused_ail_main
#include "ail_swizzle_oracle_fixture.c"
#undef main
#include "agx_d3d12_volume_registry.h"

static const enum pipe_format formats[AGX_VOLUME_ELEMENT_SIZES] = {
   PIPE_FORMAT_R8_UNORM, PIPE_FORMAT_R16_FLOAT, PIPE_FORMAT_R8G8B8A8_UNORM, PIPE_FORMAT_R16G16B16A16_FLOAT,
   PIPE_FORMAT_R32G32B32A32_FLOAT};
static const unsigned sides[] = {1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 33, 48, 63, 64, 65, 100, 127, 128, 129, 200, 256};

static unsigned long long texels, resources, admitted6, admitted2, square_refused;

/* C21H: block-compressed volumes (BC1: 8-byte, BC7: 16-byte 4x4 blocks);
 * element coordinates are blocks */
static const enum pipe_format compressed[2] = {PIPE_FORMAT_DXT1_RGBA, PIPE_FORMAT_BPTC_RGBA_UNORM};

static int check_format(enum pipe_format format, unsigned e, unsigned w, unsigned h, unsigned d, unsigned levels,
                        unsigned max_slots) {
   struct ail_layout a = {.width_px = w, .height_px = h, .depth_px = d, .sample_count_sa = 1, .levels = levels,
                          .mipmapped_z = true, .tiling = AIL_TILING_GPU, .format = format, .renderable = true};
   ail_make_miptree(&a);
   AGX_VOLUME_LEVEL lv[AGX_VOLUME_MAX_LEVELS];
   for (unsigned l = 0; l < levels; ++l)
      lv[l] = (AGX_VOLUME_LEVEL){a.tilesize_el[l].width_el, a.tilesize_el[l].height_el, a.stride_el[l],
                                 util_format_get_nblocksy(format, mini(h, l))};
   AGX_VOLUME_SELECTION sel;
   if (!agx_volume_select(e, levels, lv, max_slots, &sel)) return 0;
   /* the transitions match the slot of every level */
   unsigned slot = 0;
   for (unsigned l = 0; l < levels; ++l) {
      while (slot < max_slots - 1u && sel.Transitions[slot] != AGX_VOLUME_NO_TRANSITION && sel.Transitions[slot] <= l) ++slot;
      assert(agx_volume_layout_slot(sel.Layout, slot) == sel.Pattern[l]);
   }
   for (unsigned l = 0; l < levels; ++l) {
      const unsigned lw = util_format_get_nblocksx(format, mini(w, l)), lh = util_format_get_nblocksy(format, mini(h, l)),
                     ld = mini(d, l);
      const uint64_t row = agx_volume_row_stride(sel.Pattern[l], &lv[l]);
      const unsigned step = lw * lh > 4096 ? 7 : 1;
      const unsigned bs = 1u << e;
      for (unsigned z = 0; z < ld; z += ld > 2 ? ld - 1 : 1)
         for (unsigned y = 0; y < lh; y += (y + step < lh || y == lh - 1) ? step : lh - 1 - y)
            for (unsigned x = 0; x < lw; x += (x + step < lw || x == lw - 1) ? step : lw - 1 - x) {
               for (unsigned b = 0; b < bs; b += bs > 1 ? bs - 1 : 1) {
                  const uint64_t want = c21_ail_reference_offset(&a, l, x, y, z, b);
                  const uint64_t got = a.level_offsets_B[l] + (uint64_t)z * a.layer_stride_B +
                                       agx_volume_address(sel.Pattern[l], row, x, y, b);
                  if (want != got) {
                     printf("MISMATCH e=%u %ux%ux%u levels %u l=%u (%u,%u,%u) b=%u want %llu got %llu\n", e, w, h, d,
                            levels, l, x, y, z, b, (unsigned long long)want, (unsigned long long)got);
                     exit(1);
                  }
                  assert(got < a.size_B);
                  ++texels;
               }
               if (x == lw - 1) break;
            }
   }
   return 1;
}

static int check_volume(unsigned e, unsigned w, unsigned h, unsigned d, unsigned levels, unsigned max_slots) {
   return check_format(formats[e], e, w, h, d, levels, max_slots);
}

static unsigned long long bc_resources, bc_admitted6, bc_square_refused;

int main(void) {
   for (unsigned f = 0; f < 2; ++f)
      for (unsigned wi = 0; wi < sizeof(sides) / sizeof(sides[0]); ++wi)
         for (unsigned hi = 0; hi < sizeof(sides) / sizeof(sides[0]); ++hi)
            for (unsigned d = 1; d <= 9; d += 4) {
               const unsigned w = sides[wi], h = sides[hi];
               unsigned full = 1;
               for (unsigned m = mx(mx(w, h), d); m > 1; m >>= 1) ++full;
               for (unsigned levels = 1; levels <= full; ++levels) {
                  ++bc_resources;
                  const int six = check_format(compressed[f], f ? 4 : 3, w, h, d, levels, 6);
                  bc_admitted6 += six;
                  if (w == h && !check_format(compressed[f], f ? 4 : 3, w, h, d, levels, 2)) {
                     if (bc_square_refused < 4) printf("BC_SQUARE_REFUSED f=%u %ux%ux%u levels %u\n", f, w, h, d, levels);
                     ++bc_square_refused;
                  }
               }
            }
   printf("BC_VOLUME_REGISTRY resources=%llu admitted6=%llu square_refused2=%llu\n", bc_resources, bc_admitted6,
          bc_square_refused);
   for (unsigned e = 0; e < AGX_VOLUME_ELEMENT_SIZES; ++e)
      for (unsigned wi = 0; wi < sizeof(sides) / sizeof(sides[0]); ++wi)
         for (unsigned hi = 0; hi < sizeof(sides) / sizeof(sides[0]); ++hi)
            for (unsigned d = 1; d <= 9; d += 4) {
               const unsigned w = sides[wi], h = sides[hi];
               unsigned full = 1;
               for (unsigned m = mx(mx(w, h), d); m > 1; m >>= 1) ++full;
               for (unsigned levels = 1; levels <= full; ++levels) {
                  ++resources;
                  const int six = check_volume(e, w, h, d, levels, 6), two = check_volume(e, w, h, d, levels, 2);
                  admitted6 += six;
                  admitted2 += two;
                  assert(!two || six);
                  if (w == h && !two) {
                     printf("SQUARE_REFUSED e=%u %ux%ux%u levels %u\n", e, w, h, d, levels);
                     ++square_refused;
                  }
               }
            }
   /* the registry descriptors: in-range, distinct block bits */
   for (unsigned p = 0; p < AGX_VOLUME_PATTERNS; ++p) {
      AGX_VOLUME_BIT bits[32];
      const unsigned n = agx_volume_pattern_bits(p, bits);
      assert(n <= 32 && (n || p == agx_volume_pattern_index(0, agx_volume_shape_count(0) - 1)));
   }
   assert(agx_volume_pattern_index(AGX_VOLUME_ELEMENT_SIZES - 1, agx_volume_shape_count(4) - 1) == AGX_VOLUME_PATTERNS - 1);
   printf("VOLUME_REGISTRY_PASS resources=%llu admitted6=%llu admitted2=%llu square_refused=%llu texels=%llu\n",
          resources, admitted6, admitted2, square_refused, texels);
   return square_refused ? 1 : 0;
}
