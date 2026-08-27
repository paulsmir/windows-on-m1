#include "apple_agx_firmware_status.h"

static void AppleAgxFirmwareStatusWriteU64(unsigned char *Destination,
                                            unsigned long long Value) {
  unsigned int index;
  for (index = 0u; index < 8u; ++index)
    Destination[index] =
        (unsigned char)((Value >> (index * 8u)) & 0xffULL);
}

static unsigned char AppleAgxFirmwareStatusDestinationIsZero(
    const unsigned char *Destination, unsigned int Size) {
  unsigned int index;
  for (index = 0u; index < Size; ++index) {
    if (Destination[index] != 0u)
      return 0u;
  }
  return 1u;
}

static unsigned char AppleAgxFirmwareStatusAddressIsValid(
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

APPLE_AGX_FIRMWARE_STATUS_RESULT AppleAgxFirmwareStatusEncodeG13V13_5(
    const APPLE_AGX_FIRMWARE_STATUS_INPUT *Input,
    unsigned char *Destination, unsigned int DestinationSize,
    APPLE_AGX_FIRMWARE_STATUS_MANIFEST *Manifest) {
  unsigned char encoded[J313_AGX_G2_INITDATA_FW_STATUS_SIZE] = {0};
  unsigned int index;

  if (Input == 0 || Destination == 0 || Manifest == 0)
    return AppleAgxFirmwareStatusResultInvalidArgument;
  if (DestinationSize != J313_AGX_G2_INITDATA_FW_STATUS_SIZE)
    return AppleAgxFirmwareStatusResultDestinationSize;
  if (AppleAgxFirmwareStatusDestinationIsZero(Destination, DestinationSize) ==
      0u)
    return AppleAgxFirmwareStatusResultDestinationNotZero;
  if (J313_AGX_G2_INITDATA_FW_STATUS_SIZE != 0x80u ||
      J313_AGX_G2_FWCTL_STATE_SIZE != 0x30u ||
      J313_AGX_G2_FWCTL_MESSAGE_SIZE != 0x14u ||
      J313_AGX_G2_FWCTL_RING_ENTRY_COUNT != 0x100u ||
      J313_AGX_G2_FWCTL_RING_SIZE != 0x1400u)
    return AppleAgxFirmwareStatusResultUnsupportedVersion;
  if (AppleAgxFirmwareStatusAddressIsValid(Input->StateAddress) == 0u ||
      AppleAgxFirmwareStatusAddressIsValid(Input->RingAddress) == 0u ||
      Input->StateAddress == Input->RingAddress)
    return AppleAgxFirmwareStatusResultAddress;

  AppleAgxFirmwareStatusWriteU64(encoded + 0x00u, Input->StateAddress);
  AppleAgxFirmwareStatusWriteU64(encoded + 0x08u, Input->RingAddress);
  for (index = 0u; index < DestinationSize; ++index)
    Destination[index] = encoded[index];
  Manifest->EncodedSize = DestinationSize;
  Manifest->StateAddress = Input->StateAddress;
  Manifest->RingAddress = Input->RingAddress;
  return AppleAgxFirmwareStatusResultOk;
}
