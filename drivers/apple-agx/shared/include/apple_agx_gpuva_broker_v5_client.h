#ifndef APPLE_AGX_GPUVA_BROKER_V5_CLIENT_H
#define APPLE_AGX_GPUVA_BROKER_V5_CLIENT_H
#include <stdbool.h>
#include "apple_agx_gpuva_broker_v5.h"

typedef struct _APPLE_AGX_GPUVA_V5_IO {
    void *Context;
    bool (*Write64)(void *, unsigned, unsigned long long);
    bool (*Read64)(void *, unsigned, unsigned long long *);
    bool (*Write32)(void *, unsigned, unsigned int);
    void (*Barrier)(void *);
    /* EXP1044 diagnostic: optional monotonic tick source for call timing. */
    unsigned long long (*Now)(void *);
} APPLE_AGX_GPUVA_V5_IO;

#define APPLE_AGX_GPUVA_V5_TIMED_COMMANDS 32u

typedef struct _APPLE_AGX_GPUVA_V5_CLIENT {
    APPLE_AGX_GPUVA_V5_IO Io;
    unsigned long long Sequence;
    unsigned long long Epoch;
    /* EXP1044 diagnostic: per-command call count and ticks (Io.Now). */
    unsigned long long Calls[APPLE_AGX_GPUVA_V5_TIMED_COMMANDS];
    unsigned long long Ticks[APPLE_AGX_GPUVA_V5_TIMED_COMMANDS];
    unsigned long long MaxTicks[APPLE_AGX_GPUVA_V5_TIMED_COMMANDS];
} APPLE_AGX_GPUVA_V5_CLIENT;

bool AppleAgxGpuvaV5ClientInit(APPLE_AGX_GPUVA_V5_CLIENT *,
                               const APPLE_AGX_GPUVA_V5_IO *);
bool AppleAgxGpuvaV5ClientCall(APPLE_AGX_GPUVA_V5_CLIENT *,
                               const AGX_GPUVA_V5_REQUEST *,
                               AGX_GPUVA_V5_RESPONSE *);
#endif
