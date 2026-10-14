#include "apple_agx_g4_submit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  APPLE_AGX_G4_NATIVE_RENDER render;
  unsigned int bytes[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
  APPLE_AGX_G4_PROCESS_RANGE ranges[APPLE_AGX_G4_PROCESS_RANGE_COUNT] = {0};
  APPLE_AGX_G4_PRIVATE_HEADER_V2 header = {0};
  memset(&render, 0, sizeof(render));
  render.WidthPx = render.HeightPx = 32;
  render.Layers = 1;
  render.UtileWidthPx = render.UtileHeightPx = 16;
  assert(AppleAgxG4ProcessRequiredBytes(&render, bytes));
  assert(bytes[0] == 0x10000 && bytes[1] == 0x10000);
  assert(bytes[2] == 0x400000 && bytes[3] == 0x20000);
  assert(bytes[4] == 0x10000 && bytes[5] == 0x10000);
  assert(bytes[6] == 0x10000 && bytes[7] == 0x10000 &&
         bytes[8] == 0x10000);
  render.WidthPx = 2560;
  render.HeightPx = 1600;
  assert(AppleAgxG4ProcessRequiredBytes(&render, bytes));
  assert(bytes[2] == 0x400000);
  assert(bytes[2] / 0x20000u == 32u); /* 80x50 tiles, 32 blocks. */
  assert(bytes[4] == 0x20000);
  assert(bytes[6] == 0x140000);
  for (unsigned i = 0; i < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++i) {
    ranges[i].Va = 0x1000000ULL + (unsigned long long)i * 0x1000000ULL;
    ranges[i].Bytes = bytes[i];
  }
  assert(AppleAgxG4ComposeHeaderV2(&header, &render, 0x20000ULL,
                                    248u, APPLE_AGX_G4_COLOR_BGRA8, ranges));
  assert(header.Base.Version == APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA &&
         header.Base.HeaderBytes == sizeof(header) &&
         header.Base.CommandVa == 0x20000ULL &&
         header.Process[2].Bytes == 0x400000u);
  ranges[4].Va = ranges[2].Va;
  assert(!AppleAgxG4ComposeHeaderV2(&header, &render, 0x20000ULL,
                                     248u, APPLE_AGX_G4_COLOR_BGRA8, ranges));
  ranges[4].Va = 0x1000000ULL + 4ULL * 0x1000000ULL;
  /* M2-f9a (EXP1269): a render above the 32-block TVB window asks for the
   * window, not its tile-count minimum (2048x8192 asked 16 MiB and was
   * refused). Asahi buffer.rs: when the heap runs out of pages the firmware
   * performs a partial render transparently. The scene's tile array and TPC
   * still scale with the render. */
  render.UtileWidthPx = render.UtileHeightPx = 32;
  render.WidthPx = 2048;
  render.HeightPx = 8192;
  assert(AppleAgxG4ProcessRequiredBytes(&render, bytes));
  assert(bytes[2] == APPLE_AGX_G4_TVB_WINDOW_BLOCKS * 0x20000u);
  assert(bytes[6] == 0x100000u); /* 64 x 256 tiles: TPC grows with the scene */
  render.WidthPx = render.HeightPx = 8192;
  assert(AppleAgxG4ProcessRequiredBytes(&render, bytes));
  assert(bytes[2] == 0x400000u && bytes[6] == 0x400000u);
  assert(AppleAgxG4MinTvbBlocks(2048, 4096) == 32u &&
         AppleAgxG4MinTvbBlocks(2048, 8192) == 32u &&
         AppleAgxG4MinTvbBlocks(16384, 16384) == 32u &&
         AppleAgxG4MinTvbBlocks(1280, 800) == 8u);
  render.WidthPx = 2560;
  render.HeightPx = 1600;
  render.UtileWidthPx = render.UtileHeightPx = 16;
  render.WidthPx = 0;
  assert(!AppleAgxG4ProcessRequiredBytes(&render, bytes));
  render.WidthPx = 2560;
  render.UtileWidthPx = 8;
  assert(!AppleAgxG4ProcessRequiredBytes(&render, bytes));
  puts("apple_agx_g4_process_layout_test: PASS");
  return 0;
}
