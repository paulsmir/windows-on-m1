#ifndef APPLE_AGX_GPUVA_B1_STATUS_H
#define APPLE_AGX_GPUVA_B1_STATUS_H

typedef struct _APPLE_AGX_GPUVA_B1_STATUS {
  unsigned int FirstFailure;
  unsigned int Terminal;
} APPLE_AGX_GPUVA_B1_STATUS;

static inline APPLE_AGX_GPUVA_B1_STATUS AppleAgxGpuvaB1FinalStatus(
    unsigned int Primary, unsigned int CleanupSucceeded,
    unsigned int BusyStatus) {
  APPLE_AGX_GPUVA_B1_STATUS result;
  result.FirstFailure = Primary != 0u ? Primary :
      CleanupSucceeded ? 0u : BusyStatus;
  result.Terminal = CleanupSucceeded ? Primary : BusyStatus;
  return result;
}

#endif
