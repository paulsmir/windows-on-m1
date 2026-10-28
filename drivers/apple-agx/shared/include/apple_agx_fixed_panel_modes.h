#ifndef APPLE_AGX_FIXED_PANEL_MODES_H
#define APPLE_AGX_FIXED_PANEL_MODES_H

/* rc106: the source modes the fixed 2560x1600 J313 panel offers.  Index 0
 * is the native mode; the others are scaled into the panel by DCP and are
 * offered only when the scanout broker advertises CAP_SCALED_SOURCE.
 *
 * Freestanding, no hosted headers: the KMD's VidPN source-mode table
 * (apple_agx_fixed_panel.c) and the UMD's primary and direct-flip gates
 * (direct_flip_contract.c, umd.c) share it, so every layer admits exactly
 * the same linear 32-bpp primary geometry.
 *
 * Every width is a multiple of 16 pixels, so the stride (Width * 4) meets
 * both the 16-byte allocation pitch and DCP's 64-byte stride rule (m1n1
 * dcp_iomfb_present_build_v13_5: stride & 63 must be zero); 1366x768 is
 * therefore not offered.  Every height is even, so Stride * Height is a
 * multiple of 128 and the cache-line padded UMD allocation size equals it. */
#define APPLE_AGX_FIXED_PANEL_MODE_COUNT 11u

static const unsigned int
    AppleAgxFixedPanelModeSizes[APPLE_AGX_FIXED_PANEL_MODE_COUNT][2] = {
        {2560u, 1600u}, {2048u, 1280u}, {1920u, 1200u}, {1920u, 1080u},
        {1680u, 1050u}, {1600u, 900u},  {1440u, 900u},  {1280u, 800u},
        {1280u, 720u},  {1024u, 768u},  {800u, 600u},
};

/* Index of Width x Height in the table, or APPLE_AGX_FIXED_PANEL_MODE_COUNT
 * when it is not an offered source mode. */
static __inline unsigned int AppleAgxFixedPanelModeIndex(unsigned int Width,
                                                         unsigned int Height) {
  unsigned int index;
  for (index = 0u; index < APPLE_AGX_FIXED_PANEL_MODE_COUNT; ++index) {
    if (AppleAgxFixedPanelModeSizes[index][0] == Width &&
        AppleAgxFixedPanelModeSizes[index][1] == Height)
      return index;
  }
  return APPLE_AGX_FIXED_PANEL_MODE_COUNT;
}

/* A linear 32-bpp primary of an offered mode: Pitch == Width * 4 and
 * Size == Pitch * Height (the native mode: 10240 x 1600 = 0xfa0000).  The
 * geometry the KMD's shared primary, flip and PRESENT gates require. */
static __inline int AppleAgxFixedPanelPrimaryGeometryOffered(
    unsigned int Width, unsigned int Height, unsigned int Pitch,
    unsigned long long Size) {
  return AppleAgxFixedPanelModeIndex(Width, Height) <
                 APPLE_AGX_FIXED_PANEL_MODE_COUNT &&
                 Pitch == Width * 4u &&
                 Size == (unsigned long long)Pitch * Height
             ? 1
             : 0;
}

#endif /* APPLE_AGX_FIXED_PANEL_MODES_H */
