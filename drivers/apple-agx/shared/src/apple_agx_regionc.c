#include "apple_agx_regionc.h"

#include <string.h>

#define APPLE_AGX_REGIONC_J313_SCALAR_PRESENCE 0x1ff5b8bfULL
#define APPLE_AGX_REGIONC_J313_ORACLE_FNV1A64 0xc3bc91a9acf61290ULL

typedef struct _APPLE_AGX_REGIONC_WORD {
  unsigned int Offset;
  unsigned int Value;
} APPLE_AGX_REGIONC_WORD;

/*
 * Exact non-zero words emitted by the pinned m1n1 G13/V13_5 RegionC builder
 * for the physically captured J313 ADT contract.  Keeping the zero-filled
 * object sparse makes every firmware-visible difference reviewable.
 */
static const APPLE_AGX_REGIONC_WORD AppleAgxRegionCJ313Words[] = {
    {0x00024u, 0x00000bb8u}, {0x00034u, 0x00000001u},
    {0x0003cu, 0x00000001u}, {0x00044u, 0x00000078u},
    {0x00064u, 0x0028ffffu}, {0x00068u, 0x0000ffffu},
    {0x0006cu, 0x00010000u}, {0x00080u, 0x00010000u},
    {0x08970u, 0x00000001u}, {0x08978u, 0x00004c5fu},
    {0x0897cu, 0x00000258u}, {0x08980u, 0x00000258u},
    {0x0898cu, 0x00000258u}, {0x08994u, 0x00000001u},
    {0x08998u, 0x0000007du}, {0x0899cu, 0x3d75c28fu},
    {0x089a0u, 0x40800000u}, {0x089a4u, 0x00000028u},
    {0x089a8u, 0x0000007du}, {0x089acu, 0x00007530u},
    {0x089b0u, 0x000074ccu}, {0x089b4u, 0x00001adbu},
    {0x08a68u, 0x00002698u}, {0x08a6cu, 0x00001f40u},
    {0x08a70u, 0xffffff24u}, {0x08a78u, 0x40a00000u},
    {0x08a7cu, 0x3fcccccdu}, {0x08a8cu, 0x00000001u},
    {0x08a90u, 0x00004c5fu}, {0x08a94u, 0x40dccccdu},
    {0x08a98u, 0x3f3b645au}, {0x08aa8u, 0x00000001u},
    {0x08b0cu, 0xffffffffu}, {0x08b10u, 0x00007282u},
    {0x08b14u, 0x000050eau}, {0x08b18u, 0x0000370au},
    {0x08b1cu, 0x000025beu}, {0x08b20u, 0x00001c1fu},
    {0x08b24u, 0x000016fbu}, {0x08b28u, 0xffffffffu},
    {0x08b2cu, 0xffffffffu}, {0x08b30u, 0xffffffffu},
    {0x08b34u, 0xffffffffu}, {0x08b38u, 0xffffffffu},
    {0x08b3cu, 0xffffffffu}, {0x08b40u, 0xffffffffu},
    {0x08b44u, 0xffffffffu}, {0x08b48u, 0xffffffffu},
    {0x08bacu, 0x0000ffffu}, {0x08bb4u, 0x00000800u},
    {0x08bb8u, 0x00001555u}, {0x08bbcu, 0xffffffffu},
    {0x08bc0u, 0xffffffffu}, {0x08bc4u, 0xffffffffu},
    {0x08bc8u, 0xffffffffu}, {0x08bccu, 0xffffffffu},
    {0x08bd0u, 0xffffffffu}, {0x08bdcu, 0xffffffffu},
    {0x08be0u, 0xffffffffu}, {0x08be4u, 0xffffffffu},
    {0x08be8u, 0xffffffffu}, {0x090bcu, 0x00c00007u},
    {0x090f0u, 0x00000001u}, {0x090f4u, 0x000001f4u},
    {0x0913cu, 0x00000001u}, {0x09148u, 0x00000001u},
    {0x0914cu, 0x00000001u}, {0x09190u, 0x00000001u},
    {0x10fdcu, 0x00000001u}, {0x11178u, 0x00000028u},
    {0x1117cu, 0x0000000au}, {0x11180u, 0x000000fau},
    {0x11184u, 0x00000001u}, {0x11188u, 0x00000001u},
    {0x1118cu, 0x00000064u}, {0x11190u, 0x00000001u},
    {0x1119cu, 0x00000002u}, {0x111a0u, 0x00000028u},
    {0x111a4u, 0x00000005u}, {0x11af0u, 0x00000028u},
    {0x11af4u, 0x00000032u},
};

static const unsigned int AppleAgxRegionCJ313Frequencies[7] = {
    0u,         396000000u, 528000000u, 720000000u,
    924000000u, 1128000000u, 1278000000u};
static const unsigned int AppleAgxRegionCJ313Voltages[7] = {
    400u, 612u, 650u, 687u, 778u, 871u, 943u};
static const unsigned int
    AppleAgxRegionCJ313Scalars[APPLE_AGX_CONFIG_SCALAR_COUNT] = {
        0x000003e8u, 0x40f00000u, 0x40800000u, 0x00000028u,
        0x0000007du, 0x43480000u, 0x00000000u, 0x40a00000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000005u, 0x00000032u, 0x00000000u, 0x3e4a2121u,
        0x00000000u, 0x00000000u, 0x40db53d0u, 0x00000000u,
        0x00000055u, 0x00000064u, 0x42b70000u, 0x40dccccdu,
        0x00000139u, 0x3ca59586u, 0x00000000u, 0x00000028u,
        0x40a90fdbu, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u};

static unsigned char AppleAgxRegionCIsZero(const unsigned char *data,
                                           unsigned int size) {
  unsigned int index;
  for (index = 0u; index < size; ++index) {
    if (data[index] != 0u)
      return 0u;
  }
  return 1u;
}

static unsigned char AppleAgxRegionCMatchesJ313(
    const APPLE_AGX_CONFIG_SNAPSHOT *snapshot) {
  unsigned int index;
  if (snapshot->PerfStateCount != 7u ||
      snapshot->PerfStateTableCount != 1u || snapshot->BasePstate != 1u ||
      snapshot->MaxPstate != 6u || snapshot->PowerSamplePeriodMs != 8u ||
      snapshot->GpuRegionBase != 0x9fffb8000ULL ||
      snapshot->ScalarPresence != APPLE_AGX_REGIONC_J313_SCALAR_PRESENCE)
    return 0u;
  for (index = 0u; index < 7u; ++index) {
    if (snapshot->PerfStates[index].FrequencyHz !=
            AppleAgxRegionCJ313Frequencies[index] ||
        snapshot->PerfStates[index].VoltageMv !=
            AppleAgxRegionCJ313Voltages[index])
      return 0u;
  }
  for (; index < APPLE_AGX_CONFIG_MAX_PERF_STATES; ++index) {
    if (snapshot->PerfStates[index].FrequencyHz != 0u ||
        snapshot->PerfStates[index].VoltageMv != 0u)
      return 0u;
  }
  return (unsigned char)(memcmp(snapshot->ScalarBits,
                                AppleAgxRegionCJ313Scalars,
                                sizeof(AppleAgxRegionCJ313Scalars)) == 0);
}

static void AppleAgxRegionCWriteU32(unsigned char *destination,
                                    unsigned int value) {
  destination[0] = (unsigned char)value;
  destination[1] = (unsigned char)(value >> 8u);
  destination[2] = (unsigned char)(value >> 16u);
  destination[3] = (unsigned char)(value >> 24u);
}

APPLE_AGX_REGIONC_RESULT AppleAgxRegionCEncodeJ313G13V13_5(
    const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot, unsigned char *Destination,
    unsigned int DestinationSize, APPLE_AGX_REGIONC_MANIFEST *Manifest) {
  unsigned int index;
  if (Snapshot == 0 || Destination == 0 || Manifest == 0)
    return AppleAgxRegionCResultInvalidArgument;
  if (DestinationSize != J313_AGX_G2_INITDATA_REGION_C_SIZE)
    return AppleAgxRegionCResultDestinationSize;
  if (AppleAgxRegionCIsZero(Destination, DestinationSize) == 0u)
    return AppleAgxRegionCResultDestinationNotZero;
  if (AppleAgxRegionCMatchesJ313(Snapshot) == 0u)
    return AppleAgxRegionCResultSnapshotMismatch;

  for (index = 0u;
       index < sizeof(AppleAgxRegionCJ313Words) /
                   sizeof(AppleAgxRegionCJ313Words[0]);
       ++index) {
    AppleAgxRegionCWriteU32(
        Destination + AppleAgxRegionCJ313Words[index].Offset,
        AppleAgxRegionCJ313Words[index].Value);
  }
  Manifest->EncodedSize = DestinationSize;
  Manifest->NonzeroWordCount =
      (unsigned int)(sizeof(AppleAgxRegionCJ313Words) /
                     sizeof(AppleAgxRegionCJ313Words[0]));
  Manifest->OracleFnv1a64 = APPLE_AGX_REGIONC_J313_ORACLE_FNV1A64;
  return AppleAgxRegionCResultOk;
}
