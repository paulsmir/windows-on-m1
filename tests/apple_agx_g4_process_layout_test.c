#include "apple_agx_g4_submit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  APPLE_AGX_G4_NATIVE_RENDER render;
  unsigned int bytes[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
  memset(&render, 0, sizeof(render));
  render.WidthPx = render.HeightPx = 32;
  render.Layers = 1;
  render.UtileWidthPx = render.UtileHeightPx = 16;
  assert(AppleAgxG4ProcessRequiredBytes(&render, bytes));
  assert(bytes[0] == 0x10000 && bytes[1] == 0x10000);
  assert(bytes[2] == 0x100000 && bytes[3] == 0x20000);
  assert(bytes[4] == 0x10000 && bytes[5] == 0x10000);
  assert(bytes[6] == 0x10000 && bytes[7] == 0x10000 &&
         bytes[8] == 0x10000);
  render.WidthPx = 2560;
  render.HeightPx = 1600;
  assert(AppleAgxG4ProcessRequiredBytes(&render, bytes));
  assert(bytes[2] == 0x400000);
  assert(bytes[4] == 0x20000);
  assert(bytes[6] == 0x140000);
  render.WidthPx = 0;
  assert(!AppleAgxG4ProcessRequiredBytes(&render, bytes));
  render.WidthPx = 2560;
  render.UtileWidthPx = 8;
  assert(!AppleAgxG4ProcessRequiredBytes(&render, bytes));
  puts("apple_agx_g4_process_layout_test: PASS");
  return 0;
}
