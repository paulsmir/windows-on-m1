#include "apple_agx_regionb.h"

#include <assert.h>
#include <string.h>

static unsigned long long read_u64(const unsigned char *bytes) {
  unsigned long long value = 0ULL;
  unsigned int index;
  for (index = 0u; index < 8u; ++index)
    value |= (unsigned long long)bytes[index] << (index * 8u);
  return value;
}

static APPLE_AGX_REGIONB_INPUT valid_input(void) {
  APPLE_AGX_REGIONB_INPUT input;
  unsigned long long address = J313_AGX_G2_KERNEL_VA_BASE + 0x1000000ULL;
  memset(&input, 0, sizeof(input));
  input.StatsTaAddress = address; address += 0x8000ULL;
  input.Stats3dAddress = address; address += 0x8000ULL;
  input.StatsCpAddress = address; address += 0x8000ULL;
  input.HwdataAAddress = address; address += 0x8000ULL;
  input.FaultInfoAddress = address; address += 0x8000ULL;
  input.TimestampAddress = address; address += 0x8000ULL;
  input.HwdataBAddress = address; address += 0x8000ULL;
  input.FwlogRingAddress = address; address += 0x8000ULL;
  input.Unknown1b8Address = address; address += 0x8000ULL;
  input.Unknown1c0Address = address; address += 0x8000ULL;
  input.Unknown1c8Address = address; address += 0x8000ULL;
  input.BufferManagerGpuAddress = J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_VA;
  input.BufferManagerCpuAddress = address;
  return input;
}

static void test_encodes_exact_g13_v13_5_pointer_slots(void) {
  APPLE_AGX_REGIONB_INPUT input = valid_input();
  APPLE_AGX_REGIONB_MANIFEST manifest = {0};
  unsigned char destination[J313_AGX_G2_INITDATA_REGION_B_SIZE];
  memset(destination, 0, sizeof(destination));
  memset(destination, 0xa5, J313_AGX_G2_CHANNEL_INFO_SET_SIZE);

  assert(AppleAgxRegionBEncodePointersG13V13_5(
             &input, destination, sizeof(destination), &manifest) ==
         AppleAgxRegionBResultOk);
  assert(read_u64(destination + J313_AGX_G2_REGIONB_STATS_TA_OFFSET) ==
         input.StatsTaAddress);
  assert(read_u64(destination + J313_AGX_G2_REGIONB_STATS_3D_OFFSET) ==
         input.Stats3dAddress);
  assert(read_u64(destination + J313_AGX_G2_REGIONB_STATS_CP_OFFSET) ==
         input.StatsCpAddress);
  assert(read_u64(destination + J313_AGX_G2_REGIONB_HWDATA_A_OFFSET) ==
         input.HwdataAAddress);
  assert(read_u64(destination + J313_AGX_G2_REGIONB_FAULT_INFO_OFFSET) ==
         input.FaultInfoAddress);
  assert(read_u64(destination + J313_AGX_G2_REGIONB_TIMESTAMP_OFFSET) ==
         input.TimestampAddress);
  assert(read_u64(destination + J313_AGX_G2_REGIONB_HWDATA_B_OFFSET) ==
         input.HwdataBAddress);
  assert(read_u64(destination + J313_AGX_G2_REGIONB_HWDATA_B_REPEAT_OFFSET) ==
         input.HwdataBAddress);
  assert(read_u64(destination + J313_AGX_G2_REGIONB_FWLOG_RING_OFFSET) ==
         input.FwlogRingAddress);
  assert(read_u64(destination + J313_AGX_G2_REGIONB_UNKNOWN_1B8_OFFSET) ==
         input.Unknown1b8Address);
  assert(read_u64(destination + J313_AGX_G2_REGIONB_UNKNOWN_1C0_OFFSET) ==
         input.Unknown1c0Address);
  assert(read_u64(destination + J313_AGX_G2_REGIONB_UNKNOWN_1C8_OFFSET) ==
         input.Unknown1c8Address);
  assert(read_u64(destination + J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_OFFSET) ==
         input.BufferManagerGpuAddress);
  assert(read_u64(destination + J313_AGX_G2_REGIONB_BUFFER_MGR_CPU_OFFSET) ==
         input.BufferManagerCpuAddress);
  assert(destination[0] == 0xa5u && destination[0x10fu] == 0xa5u);
  assert(destination[0x110u] == 0u && destination[0x16fu] == 0u);
  assert(manifest.PointerCount == 14u);
  assert(manifest.FirstOffset == J313_AGX_G2_REGIONB_STATS_TA_OFFSET);
  assert(manifest.LastOffset == J313_AGX_G2_REGIONB_BUFFER_MGR_CPU_OFFSET);
}

static void test_rejection_preserves_region_and_manifest(void) {
  APPLE_AGX_REGIONB_INPUT input = valid_input();
  APPLE_AGX_REGIONB_MANIFEST manifest = {0x55u, 0x66u, 0x77u};
  unsigned char destination[J313_AGX_G2_INITDATA_REGION_B_SIZE];
  unsigned char before[J313_AGX_G2_INITDATA_REGION_B_SIZE];
  memset(destination, 0, sizeof(destination));
  destination[J313_AGX_G2_REGIONB_STATS_CP_OFFSET] = 1u;
  memcpy(before, destination, sizeof(before));
  assert(AppleAgxRegionBEncodePointersG13V13_5(
             &input, destination, sizeof(destination), &manifest) ==
         AppleAgxRegionBResultDestinationNotZero);
  assert(memcmp(destination, before, sizeof(before)) == 0);
  assert(manifest.PointerCount == 0x55u && manifest.FirstOffset == 0x66u &&
         manifest.LastOffset == 0x77u);

  memset(destination, 0, sizeof(destination));
  memcpy(before, destination, sizeof(before));
  input.HwdataBAddress = input.StatsTaAddress;
  assert(AppleAgxRegionBEncodePointersG13V13_5(
             &input, destination, sizeof(destination), &manifest) ==
         AppleAgxRegionBResultAddress);
  assert(memcmp(destination, before, sizeof(before)) == 0);
}

int main(void) {
  test_encodes_exact_g13_v13_5_pointer_slots();
  test_rejection_preserves_region_and_manifest();
  return 0;
}
