#ifndef APPLE_AGX_CONFIG_SNAPSHOT_H
#define APPLE_AGX_CONFIG_SNAPSHOT_H

#define APPLE_AGX_CONFIG_MAGIC 0x43475841u
#define APPLE_AGX_CONFIG_ABI_VERSION 2u
#define APPLE_AGX_CONFIG_MMIO_OFFSET 0x100u
#define APPLE_AGX_CONFIG_WIRE_SIZE 0x148u
#define APPLE_AGX_CONFIG_MAX_PERF_STATES 16u
#define APPLE_AGX_CONFIG_SCALAR_COUNT 33u

typedef enum _APPLE_AGX_CONFIG_SCALAR {
  AppleAgxConfigScalarAvgPowerFilterTcMs = 0,
  AppleAgxConfigScalarAvgPowerKiOnly,
  AppleAgxConfigScalarAvgPowerKp,
  AppleAgxConfigScalarAvgPowerMinDutyCycle,
  AppleAgxConfigScalarAvgPowerTargetFilterTc,
  AppleAgxConfigScalarFastDie0IntegralGain,
  AppleAgxConfigScalarFastDie0PropTargetDelta,
  AppleAgxConfigScalarFastDie0ProportionalGain,
  AppleAgxConfigScalarFastDie0ReleaseTemp,
  AppleAgxConfigScalarPerfBoostCeStep,
  AppleAgxConfigScalarPerfBoostMinUtil,
  AppleAgxConfigScalarPerfFilterDropThreshold,
  AppleAgxConfigScalarPerfFilterTimeConstant,
  AppleAgxConfigScalarPerfFilterTimeConstant2,
  AppleAgxConfigScalarPerfIntegralGain,
  AppleAgxConfigScalarPerfIntegralGain2,
  AppleAgxConfigScalarPerfIntegralMinClamp,
  AppleAgxConfigScalarPerfProportionalGain,
  AppleAgxConfigScalarPerfProportionalGain2,
  AppleAgxConfigScalarPerfResetIterations,
  AppleAgxConfigScalarPerfTargetUtilization,
  AppleAgxConfigScalarPpmFilterTimeConstantMs,
  AppleAgxConfigScalarPpmKi,
  AppleAgxConfigScalarPpmKp,
  AppleAgxConfigScalarPowerFilterTimeConstant,
  AppleAgxConfigScalarPowerIntegralGain,
  AppleAgxConfigScalarPowerIntegralMinClamp,
  AppleAgxConfigScalarPowerMinDutyCycle,
  AppleAgxConfigScalarPowerProportionalGain,
  AppleAgxConfigScalarPowerSamplePeriodAicClocks,
  AppleAgxConfigScalarIdleOffDelayMs,
  AppleAgxConfigScalarFenderIdleOffDelayMs,
  AppleAgxConfigScalarFirmwareEarlyWakeTimeoutMs,
} APPLE_AGX_CONFIG_SCALAR;

typedef struct _APPLE_AGX_CONFIG_PERF_STATE {
  unsigned int FrequencyHz;
  unsigned int VoltageMv;
} APPLE_AGX_CONFIG_PERF_STATE;

typedef struct _APPLE_AGX_CONFIG_SNAPSHOT {
  unsigned int PerfStateCount;
  unsigned int PerfStateTableCount;
  unsigned int BasePstate;
  unsigned int MaxPstate;
  unsigned int PowerSamplePeriodMs;
  unsigned long long GpuRegionBase;
  APPLE_AGX_CONFIG_PERF_STATE PerfStates[APPLE_AGX_CONFIG_MAX_PERF_STATES];
  unsigned long long ScalarPresence;
  unsigned int ScalarBits[APPLE_AGX_CONFIG_SCALAR_COUNT];
} APPLE_AGX_CONFIG_SNAPSHOT;

typedef enum _APPLE_AGX_CONFIG_RESULT {
  AppleAgxConfigResultOk = 0,
  AppleAgxConfigResultInvalidArgument,
  AppleAgxConfigResultWindowSize,
  AppleAgxConfigResultHeader,
  AppleAgxConfigResultGeometry,
  AppleAgxConfigResultPerfState,
} APPLE_AGX_CONFIG_RESULT;

APPLE_AGX_CONFIG_RESULT AppleAgxConfigSnapshotDecodeJ313(
    const unsigned char *BrokerWindow, unsigned int BrokerWindowSize,
    APPLE_AGX_CONFIG_SNAPSHOT *Snapshot);

#endif
