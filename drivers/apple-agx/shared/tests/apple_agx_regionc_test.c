#include "apple_agx_regionc.h"

#include <assert.h>
#include <string.h>

static unsigned long long fnv1a64(const unsigned char *data,
                                  unsigned int size) {
  unsigned long long hash = 0xcbf29ce484222325ULL;
  unsigned int index;
  for (index = 0u; index < size; ++index) {
    hash ^= data[index];
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

static APPLE_AGX_CONFIG_SNAPSHOT physical_snapshot(void) {
  static const unsigned int frequencies[7] = {
      0u, 396000000u, 528000000u, 720000000u,
      924000000u, 1128000000u, 1278000000u};
  static const unsigned int voltages[7] = {
      400u, 612u, 650u, 687u, 778u, 871u, 943u};
  static const unsigned int scalars[APPLE_AGX_CONFIG_SCALAR_COUNT] = {
      0x000003e8u, 0x40f00000u, 0x40800000u, 0x00000028u,
      0x0000007du, 0x43480000u, 0x00000000u, 0x40a00000u,
      0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
      0x00000005u, 0x00000032u, 0x00000000u, 0x3e4a2121u,
      0x00000000u, 0x00000000u, 0x40db53d0u, 0x00000000u,
      0x00000055u, 0x00000064u, 0x42b70000u, 0x40dccccdu,
      0x00000139u, 0x3ca59586u, 0x00000000u, 0x00000028u,
      0x40a90fdbu, 0x00000000u, 0x00000000u, 0x00000000u,
      0x00000000u};
  APPLE_AGX_CONFIG_SNAPSHOT snapshot;
  unsigned int index;
  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.PerfStateCount = 7u;
  snapshot.PerfStateTableCount = 1u;
  snapshot.BasePstate = 1u;
  snapshot.MaxPstate = 6u;
  snapshot.PowerSamplePeriodMs = 8u;
  snapshot.GpuRegionBase = 0x9fffb8000ULL;
  for (index = 0u; index < 7u; ++index) {
    snapshot.PerfStates[index].FrequencyHz = frequencies[index];
    snapshot.PerfStates[index].VoltageMv = voltages[index];
  }
  snapshot.ScalarPresence = 0x1ff5b8bfULL;
  memcpy(snapshot.ScalarBits, scalars, sizeof(scalars));
  return snapshot;
}

static void test_exact_physical_j313_regionc(void) {
  unsigned char destination[J313_AGX_G2_INITDATA_REGION_C_SIZE];
  APPLE_AGX_CONFIG_SNAPSHOT snapshot = physical_snapshot();
  APPLE_AGX_REGIONC_MANIFEST manifest;
  memset(destination, 0, sizeof(destination));
  memset(&manifest, 0, sizeof(manifest));
  assert(AppleAgxRegionCEncodeJ313G13V13_5(
             &snapshot, destination, sizeof(destination), &manifest) ==
         AppleAgxRegionCResultOk);
  assert(manifest.EncodedSize == sizeof(destination));
  assert(manifest.OracleFnv1a64 == 0xc3bc91a9acf61290ULL);
  assert(fnv1a64(destination, sizeof(destination)) ==
         0xc3bc91a9acf61290ULL);
  assert(destination[0x24u] == 0xb8u);
  assert(destination[0x10fdcu] == 1u);
}

static void test_rejection_preserves_output(void) {
  unsigned char destination[J313_AGX_G2_INITDATA_REGION_C_SIZE];
  unsigned char before[J313_AGX_G2_INITDATA_REGION_C_SIZE];
  APPLE_AGX_CONFIG_SNAPSHOT snapshot = physical_snapshot();
  APPLE_AGX_REGIONC_MANIFEST manifest;
  memset(destination, 0x5a, sizeof(destination));
  memcpy(before, destination, sizeof(destination));
  assert(AppleAgxRegionCEncodeJ313G13V13_5(
             &snapshot, destination, sizeof(destination), &manifest) ==
         AppleAgxRegionCResultDestinationNotZero);
  assert(memcmp(destination, before, sizeof(destination)) == 0);

  memset(destination, 0, sizeof(destination));
  memcpy(before, destination, sizeof(destination));
  snapshot.ScalarBits[AppleAgxConfigScalarAvgPowerKp] ^= 1u;
  assert(AppleAgxRegionCEncodeJ313G13V13_5(
             &snapshot, destination, sizeof(destination), &manifest) ==
         AppleAgxRegionCResultSnapshotMismatch);
  assert(memcmp(destination, before, sizeof(destination)) == 0);
}

int main(void) {
  test_exact_physical_j313_regionc();
  test_rejection_preserves_output();
  return 0;
}
