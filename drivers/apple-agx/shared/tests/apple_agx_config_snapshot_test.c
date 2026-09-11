#include "apple_agx_config_snapshot.h"

#include <assert.h>
#include <string.h>

static void write_u32(unsigned char *destination, unsigned int value) {
  destination[0] = (unsigned char)value;
  destination[1] = (unsigned char)(value >> 8u);
  destination[2] = (unsigned char)(value >> 16u);
  destination[3] = (unsigned char)(value >> 24u);
}

static void valid_window(unsigned char *window) {
  static const unsigned int frequencies[7] = {
      0u, 396000000u, 528000000u, 720000000u,
      924000000u, 1128000000u, 1278000000u};
  static const unsigned int voltagesMv[7] = {
      400u, 612u, 650u, 687u, 778u, 871u, 943u};
  unsigned char *wire = window + APPLE_AGX_CONFIG_MMIO_OFFSET;
  unsigned int index;
  memset(window, 0, 0x1000u);
  write_u32(wire + 0x00u, APPLE_AGX_CONFIG_MAGIC);
  write_u32(wire + 0x04u, APPLE_AGX_CONFIG_ABI_VERSION);
  write_u32(wire + 0x08u, APPLE_AGX_CONFIG_WIRE_SIZE);
  write_u32(wire + 0x0cu, 1u);
  write_u32(wire + 0x10u, 7u);
  write_u32(wire + 0x14u, 1u);
  write_u32(wire + 0x18u, 1u);
  write_u32(wire + 0x1cu, 6u);
  write_u32(wire + 0x20u, 8u);
  write_u32(wire + 0x28u, 0xfffb8000u);
  write_u32(wire + 0x2cu, 9u);
  for (index = 0u; index < 7u; ++index) {
    write_u32(wire + 0x30u + index * 8u, frequencies[index]);
    write_u32(wire + 0x34u + index * 8u, voltagesMv[index]);
  }
  write_u32(wire + 0xb0u, APPLE_AGX_CONFIG_SCALAR_COUNT);
  write_u32(wire + 0xb8u, 0x23u);
  write_u32(wire + 0xc0u, 1000u);
  write_u32(wire + 0xc4u, 0x40f00000u);
  write_u32(wire + 0xd4u, 0x43480000u);
}

static void test_decodes_exact_snapshot(void) {
  unsigned char window[0x1000];
  APPLE_AGX_CONFIG_SNAPSHOT snapshot;
  assert(APPLE_AGX_CONFIG_ABI_VERSION == 2u);
  valid_window(window);
  memset(&snapshot, 0xa5, sizeof(snapshot));
  assert(AppleAgxConfigSnapshotDecodeJ313(window, sizeof(window), &snapshot) ==
         AppleAgxConfigResultOk);
  assert(snapshot.PerfStateCount == 7u);
  assert(snapshot.GpuRegionBase == 0x9fffb8000ULL);
  assert(snapshot.PerfStates[0].FrequencyHz == 0u);
  assert(snapshot.PerfStates[0].VoltageMv == 400u);
  assert(snapshot.PerfStates[6].FrequencyHz == 1278000000u);
  assert(snapshot.PerfStates[6].VoltageMv == 943u);
  assert(snapshot.PerfStates[7].FrequencyHz == 0u);
  assert(snapshot.ScalarPresence == 0x23u);
  assert(snapshot.ScalarBits[0] == 1000u);
  assert(snapshot.ScalarBits[1] == 0x40f00000u);
}

static void test_rejections_preserve_output(void) {
  unsigned char window[0x1000];
  APPLE_AGX_CONFIG_SNAPSHOT snapshot;
  APPLE_AGX_CONFIG_SNAPSHOT before;
  valid_window(window);
  memset(&snapshot, 0x5a, sizeof(snapshot));
  before = snapshot;
  write_u32(window + APPLE_AGX_CONFIG_MMIO_OFFSET, 0u);
  assert(AppleAgxConfigSnapshotDecodeJ313(window, sizeof(window), &snapshot) ==
         AppleAgxConfigResultHeader);
  assert(memcmp(&snapshot, &before, sizeof(snapshot)) == 0);

  valid_window(window);
  write_u32(window + APPLE_AGX_CONFIG_MMIO_OFFSET + 0x10u, 8u);
  assert(AppleAgxConfigSnapshotDecodeJ313(window, sizeof(window), &snapshot) ==
         AppleAgxConfigResultGeometry);
  assert(memcmp(&snapshot, &before, sizeof(snapshot)) == 0);

  valid_window(window);
  write_u32(window + APPLE_AGX_CONFIG_MMIO_OFFSET + 0x40u, 0u);
  assert(AppleAgxConfigSnapshotDecodeJ313(window, sizeof(window), &snapshot) ==
         AppleAgxConfigResultPerfState);
  assert(memcmp(&snapshot, &before, sizeof(snapshot)) == 0);
}

int main(void) {
  test_decodes_exact_snapshot();
  test_rejections_preserve_output();
  return 0;
}
