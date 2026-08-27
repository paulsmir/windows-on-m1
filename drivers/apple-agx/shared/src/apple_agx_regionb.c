#include "apple_agx_regionb.h"

#define APPLE_AGX_REGIONB_POINTER_COUNT 14u

static void AppleAgxRegionBWriteU64(unsigned char *Destination,
                                    unsigned long long Value) {
  unsigned int index;
  for (index = 0u; index < 8u; ++index)
    Destination[index] =
        (unsigned char)((Value >> (index * 8u)) & 0xffULL);
}

static unsigned char AppleAgxRegionBSlotIsZero(
    const unsigned char *Destination, unsigned int Offset) {
  unsigned int index;
  for (index = 0u; index < 8u; ++index) {
    if (Destination[Offset + index] != 0u)
      return 0u;
  }
  return 1u;
}

static unsigned char AppleAgxRegionBHighAddressIsValid(
    unsigned long long Address) {
  APPLE_AGX_UAT_HALF half;
  APPLE_AGX_UAT_RESULT result;
  if ((Address & (J313_AGX_G2_PAGE_SIZE - 1ULL)) != 0ULL)
    return 0u;
  result = AppleAgxUatValidateRange(
      J313_AGX_G2_UAT_FIRMWARE_CONTEXT, Address, 0ULL,
      J313_AGX_G2_PAGE_SIZE, AppleAgxUatFirmwareSharedReadWrite, &half);
  return result == AppleAgxUatResultOk && half == AppleAgxUatTtbr1;
}

APPLE_AGX_REGIONB_RESULT AppleAgxRegionBEncodePointersG13V13_5(
    const APPLE_AGX_REGIONB_INPUT *Input, unsigned char *Destination,
    unsigned int DestinationSize, APPLE_AGX_REGIONB_MANIFEST *Manifest) {
  static const unsigned int offsets[APPLE_AGX_REGIONB_POINTER_COUNT] = {
      J313_AGX_G2_REGIONB_STATS_TA_OFFSET,
      J313_AGX_G2_REGIONB_STATS_3D_OFFSET,
      J313_AGX_G2_REGIONB_STATS_CP_OFFSET,
      J313_AGX_G2_REGIONB_HWDATA_A_OFFSET,
      J313_AGX_G2_REGIONB_FAULT_INFO_OFFSET,
      J313_AGX_G2_REGIONB_TIMESTAMP_OFFSET,
      J313_AGX_G2_REGIONB_HWDATA_B_OFFSET,
      J313_AGX_G2_REGIONB_HWDATA_B_REPEAT_OFFSET,
      J313_AGX_G2_REGIONB_FWLOG_RING_OFFSET,
      J313_AGX_G2_REGIONB_UNKNOWN_1B8_OFFSET,
      J313_AGX_G2_REGIONB_UNKNOWN_1C0_OFFSET,
      J313_AGX_G2_REGIONB_UNKNOWN_1C8_OFFSET,
      J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_OFFSET,
      J313_AGX_G2_REGIONB_BUFFER_MGR_CPU_OFFSET,
  };
  unsigned long long addresses[APPLE_AGX_REGIONB_POINTER_COUNT];
  unsigned int left;
  unsigned int right;

  if (Input == 0 || Destination == 0 || Manifest == 0)
    return AppleAgxRegionBResultInvalidArgument;
  if (DestinationSize != J313_AGX_G2_INITDATA_REGION_B_SIZE)
    return AppleAgxRegionBResultDestinationSize;
  if (J313_AGX_G2_INITDATA_REGION_B_SIZE != 0x6bc0u ||
      J313_AGX_G2_REGIONB_STATS_TA_OFFSET != 0x170u ||
      J313_AGX_G2_REGIONB_BUFFER_MGR_CPU_OFFSET != 0x21cu ||
      J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_VA != 0x420000000ULL)
    return AppleAgxRegionBResultUnsupportedVersion;

  addresses[0] = Input->StatsTaAddress;
  addresses[1] = Input->Stats3dAddress;
  addresses[2] = Input->StatsCpAddress;
  addresses[3] = Input->HwdataAAddress;
  addresses[4] = Input->FaultInfoAddress;
  addresses[5] = Input->TimestampAddress;
  addresses[6] = Input->HwdataBAddress;
  addresses[7] = Input->HwdataBAddress;
  addresses[8] = Input->FwlogRingAddress;
  addresses[9] = Input->Unknown1b8Address;
  addresses[10] = Input->Unknown1c0Address;
  addresses[11] = Input->Unknown1c8Address;
  addresses[12] = Input->BufferManagerGpuAddress;
  addresses[13] = Input->BufferManagerCpuAddress;

  for (left = 0u; left < APPLE_AGX_REGIONB_POINTER_COUNT; ++left) {
    if (AppleAgxRegionBSlotIsZero(Destination, offsets[left]) == 0u)
      return AppleAgxRegionBResultDestinationNotZero;
    if (left == 12u) {
      if (addresses[left] != J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_VA)
        return AppleAgxRegionBResultAddress;
    } else if (AppleAgxRegionBHighAddressIsValid(addresses[left]) == 0u) {
      return AppleAgxRegionBResultAddress;
    }
    for (right = 0u; right < left; ++right) {
      if (addresses[left] == addresses[right] &&
          !(left == 7u && right == 6u))
        return AppleAgxRegionBResultAddress;
    }
  }

  for (left = 0u; left < APPLE_AGX_REGIONB_POINTER_COUNT; ++left)
    AppleAgxRegionBWriteU64(Destination + offsets[left], addresses[left]);
  Manifest->PointerCount = APPLE_AGX_REGIONB_POINTER_COUNT;
  Manifest->FirstOffset = J313_AGX_G2_REGIONB_STATS_TA_OFFSET;
  Manifest->LastOffset = J313_AGX_G2_REGIONB_BUFFER_MGR_CPU_OFFSET;
  return AppleAgxRegionBResultOk;
}
