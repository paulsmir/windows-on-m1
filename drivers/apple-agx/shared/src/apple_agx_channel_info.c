#include "apple_agx_channel_info.h"

static void AppleAgxChannelInfoWriteU64(unsigned char *Destination,
                                         unsigned long long Value) {
  unsigned int index;
  for (index = 0u; index < 8u; ++index)
    Destination[index] =
        (unsigned char)((Value >> (index * 8u)) & 0xffULL);
}

static unsigned char AppleAgxChannelInfoDestinationIsZero(
    const unsigned char *Destination, unsigned int Size) {
  unsigned int index;
  for (index = 0u; index < Size; ++index) {
    if (Destination[index] != 0u)
      return 0u;
  }
  return 1u;
}

static unsigned char AppleAgxChannelInfoAddressIsValid(
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

static unsigned char AppleAgxChannelInfoAddressesAreUnique(
    const APPLE_AGX_CHANNEL_INFO_INPUT *Input) {
  unsigned int left;
  unsigned int right;

  for (left = 0u; left < J313_AGX_G2_CHANNEL_INFO_COUNT; ++left) {
    if (Input->Entries[left].StateAddress ==
        Input->Entries[left].RingAddress)
      return 0u;
    for (right = left + 1u;
         right < J313_AGX_G2_CHANNEL_INFO_COUNT; ++right) {
      if (Input->Entries[left].StateAddress ==
              Input->Entries[right].StateAddress ||
          Input->Entries[left].StateAddress ==
              Input->Entries[right].RingAddress ||
          Input->Entries[left].RingAddress ==
              Input->Entries[right].StateAddress ||
          Input->Entries[left].RingAddress ==
              Input->Entries[right].RingAddress)
        return 0u;
    }
  }
  return 1u;
}

APPLE_AGX_CHANNEL_INFO_RESULT AppleAgxChannelInfoEncodeG13V13_5(
    const APPLE_AGX_CHANNEL_INFO_INPUT *Input,
    unsigned char *Destination, unsigned int DestinationSize,
    APPLE_AGX_CHANNEL_INFO_MANIFEST *Manifest) {
  unsigned char encoded[J313_AGX_G2_CHANNEL_INFO_SET_SIZE] = {0};
  unsigned int index;

  if (Input == 0 || Destination == 0 || Manifest == 0)
    return AppleAgxChannelInfoResultInvalidArgument;
  if (DestinationSize != J313_AGX_G2_CHANNEL_INFO_SET_SIZE)
    return AppleAgxChannelInfoResultDestinationSize;
  if (AppleAgxChannelInfoDestinationIsZero(Destination, DestinationSize) ==
      0u)
    return AppleAgxChannelInfoResultDestinationNotZero;
  if (J313_AGX_G2_CHANNEL_INFO_SIZE != 0x10u ||
      J313_AGX_G2_CHANNEL_INFO_COUNT != 0x11u ||
      J313_AGX_G2_CHANNEL_INFO_SET_SIZE != 0x110u ||
      J313_AGX_G2_CMD_QUEUE_CHANNEL_COUNT != 0xcu ||
      J313_AGX_G2_CHANNEL_STATE_STRIDE != 0x30u ||
      J313_AGX_G2_FWLOG_RING_COUNT != 0x6u ||
      J313_AGX_G2_FWLOG_STATE_SIZE != 0x120u ||
      J313_AGX_G2_FWLOG_DUMMY_RING_SIZE != 0x150000u)
    return AppleAgxChannelInfoResultUnsupportedVersion;
  for (index = 0u; index < J313_AGX_G2_CHANNEL_INFO_COUNT; ++index) {
    if (AppleAgxChannelInfoAddressIsValid(
            Input->Entries[index].StateAddress) == 0u ||
        AppleAgxChannelInfoAddressIsValid(
            Input->Entries[index].RingAddress) == 0u)
      return AppleAgxChannelInfoResultAddress;
  }
  if (AppleAgxChannelInfoAddressesAreUnique(Input) == 0u)
    return AppleAgxChannelInfoResultAddress;

  for (index = 0u; index < J313_AGX_G2_CHANNEL_INFO_COUNT; ++index) {
    unsigned int offset = index * J313_AGX_G2_CHANNEL_INFO_SIZE;
    AppleAgxChannelInfoWriteU64(
        encoded + offset, Input->Entries[index].StateAddress);
    AppleAgxChannelInfoWriteU64(
        encoded + offset + 8u, Input->Entries[index].RingAddress);
  }
  for (index = 0u; index < DestinationSize; ++index)
    Destination[index] = encoded[index];
  Manifest->EncodedSize = DestinationSize;
  Manifest->ChannelCount = J313_AGX_G2_CHANNEL_INFO_COUNT;
  return AppleAgxChannelInfoResultOk;
}
