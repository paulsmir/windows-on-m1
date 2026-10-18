#ifndef APPLE_AGX_FIRMWARE_START_RECEIPT_H
#define APPLE_AGX_FIRMWARE_START_RECEIPT_H
#include "apple_agx_firmware.h"
typedef struct _APPLE_AGX_FIRMWARE_START_RECEIPT {
    unsigned int Captured,Result,CompletedMask;
} APPLE_AGX_FIRMWARE_START_RECEIPT;
static inline void AppleAgxFirmwareCaptureStartFailure(
    APPLE_AGX_FIRMWARE_START_RECEIPT *r,APPLE_AGX_FIRMWARE_PHASE phase,
    APPLE_AGX_FIRMWARE_RESULT result,unsigned int mask) {
    if(r && !r->Captured && phase==AppleAgxFirmwareFailed) {
        r->Captured=1;r->Result=result;r->CompletedMask=mask;
    }
}
#endif
