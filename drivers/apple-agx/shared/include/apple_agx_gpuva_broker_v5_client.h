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
} APPLE_AGX_GPUVA_V5_IO;

typedef struct _APPLE_AGX_GPUVA_V5_CLIENT {
    APPLE_AGX_GPUVA_V5_IO Io;
    unsigned long long Sequence;
    unsigned long long Epoch;
    /* CPU view of the attached 16-KiB mailbox page, or NULL (MMIO window). */
    volatile unsigned char *Mailbox;
} APPLE_AGX_GPUVA_V5_CLIENT;

bool AppleAgxGpuvaV5ClientInit(APPLE_AGX_GPUVA_V5_CLIENT *,
                               const APPLE_AGX_GPUVA_V5_IO *);
bool AppleAgxGpuvaV5ClientCall(APPLE_AGX_GPUVA_V5_CLIENT *,
                               const AGX_GPUVA_V5_REQUEST *,
                               AGX_GPUVA_V5_RESPONSE *);
/* Attach after the epoch is known.  False leaves the window path in use. */
bool AppleAgxGpuvaV5ClientAttachMailbox(APPLE_AGX_GPUVA_V5_CLIENT *,
                                        volatile void *page,
                                        unsigned long long page_ipa);
/* True only when the broker released the page; otherwise keep it allocated. */
bool AppleAgxGpuvaV5ClientDetachMailbox(APPLE_AGX_GPUVA_V5_CLIENT *);
#endif
