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
  assert(bytes[2] == 0x200000 && bytes[3] == 0x20000);
  assert(bytes[4] == 0x10000 && bytes[5] == 0x10000);
  assert(bytes[6] == 0x10000 && bytes[7] == 0x10000 &&
         bytes[8] == 0x10000);
  render.WidthPx = 2560;
  render.HeightPx = 1600;
  assert(AppleAgxG4ProcessRequiredBytes(&render, bytes));
  assert(bytes[2] == 0x400000);
  assert(bytes[4] == 0x20000);
  assert(bytes[6] == 0x140000);
  for (unsigned i = 0; i < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++i) {
    ranges[i].Va = 0x1000000ULL + (unsigned long long)i * 0x1000000ULL;
    ranges[i].Bytes = bytes[i];
  }
  assert(AppleAgxG4ComposeHeaderV2(&header, &render, 0x20000ULL,
                                    248u, ranges));
  assert(header.Base.Version == APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA &&
         header.Base.HeaderBytes == sizeof(header) &&
         header.Base.CommandVa == 0x20000ULL &&
         header.Process[2].Bytes == 0x400000u);
  ranges[4].Va = ranges[2].Va;
  assert(!AppleAgxG4ComposeHeaderV2(&header, &render, 0x20000ULL,
                                     248u, ranges));
  ranges[4].Va = 0x1000000ULL + 4ULL * 0x1000000ULL;
  render.WidthPx = 0;
  assert(!AppleAgxG4ProcessRequiredBytes(&render, bytes));
  render.WidthPx = 2560;
  render.UtileWidthPx = 8;
  assert(!AppleAgxG4ProcessRequiredBytes(&render, bytes));
  puts("apple_agx_g4_process_layout_test: PASS");
  return 0;
}
