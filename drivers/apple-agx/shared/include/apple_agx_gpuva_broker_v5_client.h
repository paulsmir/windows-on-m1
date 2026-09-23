#ifndef APPLE_AGX_GPUVA_BROKER_V5_CLIENT_H
#define APPLE_AGX_GPUVA_BROKER_V5_CLIENT_H
#include <stdbool.h>
#include <stdint.h>
#include "apple_agx_gpuva_broker_v5.h"

typedef struct _APPLE_AGX_GPUVA_V5_IO {
    void *Context;
    bool (*Write64)(void *, unsigned, uint64_t);
    bool (*Read64)(void *, unsigned, uint64_t *);
    bool (*Write32)(void *, unsigned, uint32_t);
    void (*Barrier)(void *);
} APPLE_AGX_GPUVA_V5_IO;

typedef struct _APPLE_AGX_GPUVA_V5_CLIENT {
    APPLE_AGX_GPUVA_V5_IO Io;
    uint64_t Sequence;
    uint64_t Epoch;
} APPLE_AGX_GPUVA_V5_CLIENT;

bool AppleAgxGpuvaV5ClientInit(APPLE_AGX_GPUVA_V5_CLIENT *,
                               const APPLE_AGX_GPUVA_V5_IO *);
bool AppleAgxGpuvaV5ClientCall(APPLE_AGX_GPUVA_V5_CLIENT *,
                               const AGX_GPUVA_V5_REQUEST *,
                               AGX_GPUVA_V5_RESPONSE *);
#endif
