/* RC108: agx_win32_sampler_border.h against Asahi's generated sampler
 * packing (agx_pack.h). Prints one line per case and SAMPLER_BORDER_PASS
 * when every check holds. */
#include <stdio.h>
#include <string.h>

#include "asahi/genxml/agx_pack.h"
#include "agx_win32_sampler_border.h"

static int failures;

static void
check(int ok, const char *what)
{
   printf("%s %s\n", ok ? "OK" : "FAIL", what);
   failures += !ok;
}

static int
same_except_border(const struct AGX_SAMPLER *a, const struct AGX_SAMPLER *b)
{
   return a->minimum_lod == b->minimum_lod && a->maximum_lod == b->maximum_lod &&
          a->maximum_anisotropy == b->maximum_anisotropy && a->magnify == b->magnify && a->minify == b->minify &&
          a->mip_filter == b->mip_filter && a->wrap_s == b->wrap_s && a->wrap_t == b->wrap_t &&
          a->wrap_r == b->wrap_r && a->pixel_coordinates == b->pixel_coordinates &&
          a->compare_func == b->compare_func && a->compare_enable == b->compare_enable &&
          a->seamful_cube_maps == b->seamful_cube_maps;
}

int
main(void)
{
   /* the D3D12DDI_STATIC_BORDER_COLOR values as agx_d3d12_ddi.cpp expands them */
   static const float statics[5][4] = {{0, 0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1, 1}, {0, 0, 0, 1}, {1, 1, 1, 1}};
   static const unsigned expected[5] = {AGX_BORDER_COLOUR_TRANSPARENT_BLACK, AGX_BORDER_COLOUR_OPAQUE_BLACK,
                                        AGX_BORDER_COLOUR_OPAQUE_WHITE, AGX_BORDER_COLOUR_OPAQUE_BLACK,
                                        AGX_BORDER_COLOUR_OPAQUE_WHITE};
   for (unsigned i = 0; i < 5; ++i) {
      char what[96];
      snprintf(what, sizeof(what), "static border %u -> %u", i, expected[i]);
      check(AgxWin32SamplerBorderColour(3, 4, 3, statics[i]) == expected[i], what);
   }
   static const float grey[4] = {0.5f, 0.5f, 0.5f, 1.0f}, white_clear[4] = {1, 1, 1, 0};
   check(AgxWin32SamplerBorderColour(4, 4, 4, grey) == AGX_WIN32_BORDER_OTHER, "custom colour is not built in");
   check(AgxWin32SamplerBorderColour(4, 4, 4, white_clear) == AGX_WIN32_BORDER_OTHER, "(1,1,1,0) is not built in");
   check(AgxWin32SamplerBorderColour(1, 3, 5, statics[2]) == AGX_WIN32_BORDER_TRANSPARENT_BLACK,
         "no BORDER address mode leaves the sampler alone");
   check(AGX_WIN32_BORDER_OPAQUE_WHITE == AGX_BORDER_COLOUR_OPAQUE_WHITE &&
            AGX_WIN32_BORDER_OPAQUE_BLACK == AGX_BORDER_COLOUR_OPAQUE_BLACK &&
            AGX_WIN32_BORDER_TRANSPARENT_BLACK == AGX_BORDER_COLOUR_TRANSPARENT_BLACK,
         "header values are the cmdbuf.xml enum");

   /* a sampler with every other field away from its default, as
    * agx_create_sampler_state packs a D3D comparison sampler */
   struct agx_sampler_packed packed;
   agx_pack(&packed, SAMPLER, cfg) {
      cfg.minimum_lod = 1.5f;
      cfg.maximum_lod = 13.0f;
      cfg.maximum_anisotropy = 16;
      cfg.magnify = AGX_FILTER_LINEAR;
      cfg.minify = AGX_FILTER_LINEAR;
      cfg.mip_filter = AGX_MIP_FILTER_LINEAR;
      cfg.wrap_s = AGX_WRAP_CLAMP_TO_BORDER;
      cfg.wrap_t = AGX_WRAP_CLAMP_TO_BORDER;
      cfg.wrap_r = AGX_WRAP_MIRRORED_REPEAT;
      cfg.pixel_coordinates = true;
      cfg.compare_func = AGX_COMPARE_FUNC_GEQUAL;
      cfg.compare_enable = true;
      cfg.seamful_cube_maps = true;
   }
   agx_unpack(NULL, &packed, SAMPLER, before);
   static const unsigned order[] = {AGX_WIN32_BORDER_OPAQUE_WHITE, AGX_WIN32_BORDER_OPAQUE_BLACK,
                                    AGX_WIN32_BORDER_TRANSPARENT_BLACK, AGX_WIN32_BORDER_OPAQUE_WHITE};
   for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
      AgxWin32SamplerSetBorder(&packed, order[i]);
      agx_unpack(NULL, &packed, SAMPLER, after);
      char what[96];
      snprintf(what, sizeof(what), "set border %u keeps every other field", order[i]);
      check(after.border_colour == order[i] && same_except_border(&before, &after), what);
   }
   printf(failures ? "SAMPLER_BORDER_FAIL %d\n" : "SAMPLER_BORDER_PASS\n", failures);
   return failures != 0;
}
