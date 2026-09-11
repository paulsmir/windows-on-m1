#include "apple_agx_config_snapshot.h"

#include <string.h>

static unsigned int read_u32(const unsigned char *source) {
  return (unsigned int)source[0] | ((unsigned int)source[1] << 8u) |
         ((unsigned int)source[2] << 16u) | ((unsigned int)source[3] << 24u);
}

static unsigned long long read_u64(const unsigned char *source) {
  return (unsigned long long)read_u32(source) |
         ((unsigned long long)read_u32(source + 4u) << 32u);
}

APPLE_AGX_CONFIG_RESULT AppleAgxConfigSnapshotDecodeJ313(
    const unsigned char *BrokerWindow, unsigned int BrokerWindowSize,
    APPLE_AGX_CONFIG_SNAPSHOT *Snapshot) {
  APPLE_AGX_CONFIG_SNAPSHOT candidate;
  const unsigned char *wire;
  unsigned int index;
  unsigned int previousFrequency = 0u;

  if (BrokerWindow == 0 || Snapshot == 0)
    return AppleAgxConfigResultInvalidArgument;
  if (BrokerWindowSize < APPLE_AGX_CONFIG_MMIO_OFFSET +
                             APPLE_AGX_CONFIG_WIRE_SIZE)
    return AppleAgxConfigResultWindowSize;
  wire = BrokerWindow + APPLE_AGX_CONFIG_MMIO_OFFSET;
  if (read_u32(wire + 0x00u) != APPLE_AGX_CONFIG_MAGIC ||
      read_u32(wire + 0x04u) != APPLE_AGX_CONFIG_ABI_VERSION ||
      read_u32(wire + 0x08u) != APPLE_AGX_CONFIG_WIRE_SIZE ||
      read_u32(wire + 0x0cu) != 1u || read_u32(wire + 0x24u) != 0u ||
      read_u32(wire + 0xb0u) != APPLE_AGX_CONFIG_SCALAR_COUNT ||
      read_u32(wire + 0xb4u) != 0u || read_u32(wire + 0x144u) != 0u)
    return AppleAgxConfigResultHeader;

  memset(&candidate, 0, sizeof(candidate));
  candidate.PerfStateCount = read_u32(wire + 0x10u);
  candidate.PerfStateTableCount = read_u32(wire + 0x14u);
  candidate.BasePstate = read_u32(wire + 0x18u);
  candidate.MaxPstate = read_u32(wire + 0x1cu);
  candidate.PowerSamplePeriodMs = read_u32(wire + 0x20u);
  candidate.GpuRegionBase = read_u64(wire + 0x28u);
  if (candidate.PerfStateCount != 7u || candidate.PerfStateTableCount != 1u ||
      candidate.BasePstate != 1u || candidate.MaxPstate != 6u ||
      candidate.PowerSamplePeriodMs != 8u ||
      candidate.GpuRegionBase != 0x9fffb8000ULL)
    return AppleAgxConfigResultGeometry;

  for (index = 0u; index < APPLE_AGX_CONFIG_MAX_PERF_STATES; ++index) {
    candidate.PerfStates[index].FrequencyHz =
        read_u32(wire + 0x30u + index * 8u);
    candidate.PerfStates[index].VoltageMv =
        read_u32(wire + 0x34u + index * 8u);
    if (index < candidate.PerfStateCount) {
      if (candidate.PerfStates[index].VoltageMv == 0u ||
          (index > 0u &&
           candidate.PerfStates[index].FrequencyHz <= previousFrequency))
        return AppleAgxConfigResultPerfState;
      previousFrequency = candidate.PerfStates[index].FrequencyHz;
    } else if (candidate.PerfStates[index].FrequencyHz != 0u ||
               candidate.PerfStates[index].VoltageMv != 0u) {
      return AppleAgxConfigResultPerfState;
    }
  }
  candidate.ScalarPresence = read_u64(wire + 0xb8u);
  if ((candidate.ScalarPresence >> APPLE_AGX_CONFIG_SCALAR_COUNT) != 0u)
    return AppleAgxConfigResultHeader;
  for (index = 0u; index < APPLE_AGX_CONFIG_SCALAR_COUNT; ++index) {
    candidate.ScalarBits[index] = read_u32(wire + 0xc0u + index * 4u);
    if ((candidate.ScalarPresence & (1ULL << index)) == 0u &&
        candidate.ScalarBits[index] != 0u)
      return AppleAgxConfigResultHeader;
  }
  *Snapshot = candidate;
  return AppleAgxConfigResultOk;
}
