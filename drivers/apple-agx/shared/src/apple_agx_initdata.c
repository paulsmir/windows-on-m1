#include "apple_agx_initdata.h"

#define APPLE_AGX_INITDATA_LEVEL_SIZE 0x20u
#define APPLE_AGX_INITDATA_PHYSICAL_MASK 0x000000ffffffc000ULL

static void AppleAgxInitdataWriteU16(unsigned char *Destination,
                                     unsigned short Value) {
  Destination[0] = (unsigned char)(Value & 0xffu);
  Destination[1] = (unsigned char)((Value >> 8) & 0xffu);
}

static void AppleAgxInitdataWriteU32(unsigned char *Destination,
                                     unsigned int Value) {
  unsigned int index;
  for (index = 0; index < 4u; ++index) {
    Destination[index] = (unsigned char)((Value >> (index * 8u)) & 0xffu);
  }
}

static void AppleAgxInitdataWriteU64(unsigned char *Destination,
                                     unsigned long long Value) {
  unsigned int index;
  for (index = 0; index < 8u; ++index) {
    Destination[index] =
        (unsigned char)((Value >> (index * 8u)) & 0xffULL);
  }
}

static unsigned char AppleAgxInitdataDestinationIsZero(
    const unsigned char *Destination, unsigned int Size) {
  unsigned int index;
  for (index = 0; index < Size; ++index) {
    if (Destination[index] != 0u) {
      return 0u;
    }
  }
  return 1u;
}

static APPLE_AGX_INITDATA_RESULT AppleAgxInitdataValidateAddress(
    unsigned long long Address) {
  APPLE_AGX_UAT_HALF half;
  APPLE_AGX_UAT_RESULT result = AppleAgxUatValidateRange(
      J313_AGX_G2_UAT_FIRMWARE_CONTEXT, Address, 0ULL,
      J313_AGX_G2_PAGE_SIZE, AppleAgxUatFirmwarePrivateReadWrite, &half);
  if (result != AppleAgxUatResultOk || half != AppleAgxUatTtbr1) {
    return AppleAgxInitdataResultAddress;
  }
  return AppleAgxInitdataResultOk;
}

static void AppleAgxInitdataWriteLevel(unsigned char *Destination,
                                       unsigned int Shift,
                                       unsigned int Entries) {
  Destination[0] = 8u;
  Destination[1] = J313_AGX_G2_UAT_PAGE_BITS;
  Destination[2] = J313_AGX_G2_UAT_PAGE_BITS;
  Destination[3] = (unsigned char)Shift;
  AppleAgxInitdataWriteU16(Destination + 4u, (unsigned short)Entries);
  AppleAgxInitdataWriteU16(Destination + 6u,
                           (unsigned short)J313_AGX_G2_PAGE_SIZE);
  AppleAgxInitdataWriteU64(Destination + 8u, 1ULL);
  AppleAgxInitdataWriteU64(Destination + 16u,
                           APPLE_AGX_INITDATA_PHYSICAL_MASK);
  AppleAgxInitdataWriteU64(
      Destination + 24u,
      ((unsigned long long)(Entries - 1u) << Shift));
}

APPLE_AGX_INITDATA_RESULT AppleAgxInitdataEncodeG13V13_5(
    const APPLE_AGX_INITDATA_INPUT *Input, unsigned char *Destination,
    unsigned int DestinationSize, APPLE_AGX_INITDATA_MANIFEST *Manifest) {
  unsigned char encoded[J313_AGX_G2_INITDATA_SIZE] = {0};
  unsigned long long addresses[4];
  unsigned int index;
  unsigned int other;

  if (Input == 0 || Destination == 0 || Manifest == 0) {
    return AppleAgxInitdataResultInvalidArgument;
  }
  if (DestinationSize != J313_AGX_G2_INITDATA_SIZE) {
    return AppleAgxInitdataResultDestinationSize;
  }
  if (AppleAgxInitdataDestinationIsZero(Destination, DestinationSize) == 0u) {
    return AppleAgxInitdataResultDestinationNotZero;
  }
  if (J313_AGX_G2_INITDATA_SIZE != 0xbcu ||
      J313_AGX_G2_INITDATA_VERSION_WORD0 != 0x6ba0u ||
      J313_AGX_G2_INITDATA_VERSION_WORD1 != 0x1f28u ||
      J313_AGX_G2_INITDATA_VERSION_WORD2 != 0x0601u ||
      J313_AGX_G2_INITDATA_VERSION_WORD3 != 0x00b0u) {
    return AppleAgxInitdataResultUnsupportedVersion;
  }

  addresses[0] = Input->TaggedBufferAddress;
  addresses[1] = Input->RuntimePointersAddress;
  addresses[2] = Input->GlobalsAddress;
  addresses[3] = Input->FirmwareStatusAddress;
  for (index = 0; index < 4u; ++index) {
    if (AppleAgxInitdataValidateAddress(addresses[index]) !=
        AppleAgxInitdataResultOk) {
      return AppleAgxInitdataResultAddress;
    }
    for (other = 0; other < index; ++other) {
      if (addresses[index] == addresses[other]) {
        return AppleAgxInitdataResultOverlap;
      }
    }
  }

  AppleAgxInitdataWriteU16(encoded + 0x00u,
                           J313_AGX_G2_INITDATA_VERSION_WORD0);
  AppleAgxInitdataWriteU16(encoded + 0x02u,
                           J313_AGX_G2_INITDATA_VERSION_WORD1);
  AppleAgxInitdataWriteU16(encoded + 0x04u,
                           J313_AGX_G2_INITDATA_VERSION_WORD2);
  AppleAgxInitdataWriteU16(encoded + 0x06u,
                           J313_AGX_G2_INITDATA_VERSION_WORD3);
  AppleAgxInitdataWriteU64(encoded + 0x08u, addresses[0]);
  AppleAgxInitdataWriteU32(encoded + 0x10u, 0u);
  AppleAgxInitdataWriteU32(encoded + 0x14u, 0u);
  AppleAgxInitdataWriteU64(encoded + 0x18u, addresses[1]);
  AppleAgxInitdataWriteU64(encoded + 0x20u, addresses[2]);
  AppleAgxInitdataWriteU64(encoded + 0x28u, addresses[3]);
  AppleAgxInitdataWriteU16(encoded + 0x30u,
                           (unsigned short)J313_AGX_G2_PAGE_SIZE);
  encoded[0x32u] = J313_AGX_G2_UAT_PAGE_BITS;
  encoded[0x33u] = J313_AGX_G2_UAT_LEVEL_COUNT;
  AppleAgxInitdataWriteLevel(encoded + 0x34u,
                             J313_AGX_G2_UAT_LEVEL0_SHIFT,
                             J313_AGX_G2_UAT_LEVEL0_ENTRIES);
  AppleAgxInitdataWriteLevel(encoded + 0x54u,
                             J313_AGX_G2_UAT_LEVEL1_SHIFT,
                             J313_AGX_G2_UAT_LEVEL1_ENTRIES);
  AppleAgxInitdataWriteLevel(encoded + 0x74u,
                             J313_AGX_G2_UAT_LEVEL2_SHIFT,
                             J313_AGX_G2_UAT_LEVEL2_ENTRIES);
  AppleAgxInitdataWriteU32(encoded + 0xa8u, 1u);

  for (index = 0; index < DestinationSize; ++index) {
    Destination[index] = encoded[index];
  }
  Manifest->EncodedSize = DestinationSize;
  Manifest->VersionWords[0] = J313_AGX_G2_INITDATA_VERSION_WORD0;
  Manifest->VersionWords[1] = J313_AGX_G2_INITDATA_VERSION_WORD1;
  Manifest->VersionWords[2] = J313_AGX_G2_INITDATA_VERSION_WORD2;
  Manifest->VersionWords[3] = J313_AGX_G2_INITDATA_VERSION_WORD3;
  for (index = 0; index < 4u; ++index) {
    Manifest->ReferencedAddresses[index] = addresses[index];
  }
  return AppleAgxInitdataResultOk;
}
