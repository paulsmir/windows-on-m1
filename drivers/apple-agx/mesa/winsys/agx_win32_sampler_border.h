/* RC108: D3D sampler border colours as AGX's built-in border colours.
 * SPDX-License-Identifier: MIT
 *
 * Asahi's Gallium driver uses a border colour only when the sampler state
 * names the texture format (pipe_sampler_state::border_color_format, then
 * a custom border, agx_state.c agx_create_sampler_state); otherwise the
 * packed sampler keeps "Transparent black". A D3D sampler has no format, so
 * every BORDER sampler returned (0,0,0,0) whatever its BorderColor. The AGX
 * sampler also selects opaque black and opaque white itself ("Border colour",
 * src/asahi/genxml/cmdbuf.xml), as Honeykrisp's translate_border_color uses
 * them (hk_sampler.c); those cover the D3D12 static border colours. */
#ifndef AGX_WIN32_SAMPLER_BORDER_H
#define AGX_WIN32_SAMPLER_BORDER_H

/* the AGX "Border colour" values (cmdbuf.xml) */
#define AGX_WIN32_BORDER_TRANSPARENT_BLACK 0u
#define AGX_WIN32_BORDER_OPAQUE_BLACK 1u
#define AGX_WIN32_BORDER_OPAQUE_WHITE 2u
/* any other colour: a custom border needs the view format, which a D3D
 * sampler does not have (the sampler keeps transparent black) */
#define AGX_WIN32_BORDER_OTHER 3u

/* D3D10_DDI_TEXTURE_ADDRESS_BORDER / D3D12DDI_TEXTURE_ADDRESS_MODE_BORDER */
#define AGX_WIN32_ADDRESS_BORDER 4u

/* The built-in border colour a sampler with these address modes and
 * BorderColor (RGBA) needs; transparent black when no mode is BORDER (the
 * colour is never read). */
static inline unsigned
AgxWin32SamplerBorderColour(unsigned address_u, unsigned address_v, unsigned address_w, const float colour[4])
{
  if (address_u != AGX_WIN32_ADDRESS_BORDER && address_v != AGX_WIN32_ADDRESS_BORDER &&
      address_w != AGX_WIN32_ADDRESS_BORDER)
    return AGX_WIN32_BORDER_TRANSPARENT_BLACK;
  if (colour[0] == 0.0f && colour[1] == 0.0f && colour[2] == 0.0f) {
    if (colour[3] == 0.0f) return AGX_WIN32_BORDER_TRANSPARENT_BLACK;
    if (colour[3] == 1.0f) return AGX_WIN32_BORDER_OPAQUE_BLACK;
  }
  if (colour[0] == 1.0f && colour[1] == 1.0f && colour[2] == 1.0f && colour[3] == 1.0f)
    return AGX_WIN32_BORDER_OPAQUE_WHITE;
  return AGX_WIN32_BORDER_OTHER;
}

#if defined(AGX_SAMPLER_LENGTH) && !defined(__cplusplus)
/* Sets the border colour field of a packed AGX sampler and nothing else.
 * The field is where a Custom packing differs from a Transparent black one,
 * so no bit position is assumed here (agx_pack.h AGX_SAMPLER_pack). */
static inline void
AgxWin32SamplerSetBorder(struct agx_sampler_packed *sampler, unsigned colour)
{
  struct agx_sampler_packed zero, field, value;
  agx_pack(&zero, SAMPLER, cfg) {
    cfg.border_colour = AGX_BORDER_COLOUR_TRANSPARENT_BLACK;
  }
  agx_pack(&field, SAMPLER, cfg) {
    cfg.border_colour = AGX_BORDER_COLOUR_CUSTOM;
  }
  agx_pack(&value, SAMPLER, cfg) {
    cfg.border_colour = (enum agx_border_colour)(colour & 3u);
  }
  for (unsigned i = 0; i < AGX_SAMPLER_LENGTH / 4u; ++i) {
    const uint32_t mask = field.opaque[i] ^ zero.opaque[i];
    sampler->opaque[i] = (sampler->opaque[i] & ~mask) | ((value.opaque[i] ^ zero.opaque[i]) & mask);
  }
}
#endif
#endif
