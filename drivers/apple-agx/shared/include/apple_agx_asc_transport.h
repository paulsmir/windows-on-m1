#ifndef APPLE_AGX_ASC_TRANSPORT_H
#define APPLE_AGX_ASC_TRANSPORT_H

#include "j313_agx_g2.generated.h"

/* Freestanding types keep the protocol transport usable by WDK and host tests. */
typedef unsigned char APPLE_AGX_ASC_BOOL;
typedef unsigned int APPLE_AGX_ASC_U32;
typedef unsigned long long APPLE_AGX_ASC_U64;

#define APPLE_AGX_ASC_FALSE ((APPLE_AGX_ASC_BOOL)0u)
#define APPLE_AGX_ASC_TRUE ((APPLE_AGX_ASC_BOOL)1u)
#define APPLE_AGX_ASC_CPU_RUN (1u << 4u)
#define APPLE_AGX_ASC_CPU_RUNNING (1u << 0u)
#define APPLE_AGX_ASC_CPU_STOPPED (1u << 1u)
#define APPLE_AGX_ASC_MAILBOX_FULL (1u << 16u)
#define APPLE_AGX_ASC_MAILBOX_EMPTY (1u << 17u)

typedef enum _APPLE_AGX_ASC_RESULT {
  AppleAgxAscResultOk = 0,
  AppleAgxAscResultInvalidArgument,
  AppleAgxAscResultTimeout,
  AppleAgxAscResultClockRegression,
  AppleAgxAscResultTransportFailed,
} APPLE_AGX_ASC_RESULT;

typedef struct _APPLE_AGX_ASC_MESSAGE {
  APPLE_AGX_ASC_U64 Payload;
  APPLE_AGX_ASC_U32 Endpoint;
} APPLE_AGX_ASC_MESSAGE;

typedef struct _APPLE_AGX_ASC_IO {
  void *Context;
  APPLE_AGX_ASC_U64 (*NowMs)(void *Context);
  APPLE_AGX_ASC_BOOL (*Read32)(void *Context, APPLE_AGX_ASC_U32 Offset,
                               APPLE_AGX_ASC_U32 *Value);
  APPLE_AGX_ASC_BOOL (*Read64)(void *Context, APPLE_AGX_ASC_U32 Offset,
                               APPLE_AGX_ASC_U64 *Value);
  APPLE_AGX_ASC_BOOL (*Write32)(void *Context, APPLE_AGX_ASC_U32 Offset,
                                APPLE_AGX_ASC_U32 Value);
  APPLE_AGX_ASC_BOOL (*Write64)(void *Context, APPLE_AGX_ASC_U32 Offset,
                                APPLE_AGX_ASC_U64 Value);
  APPLE_AGX_ASC_BOOL (*Pause)(void *Context);
} APPLE_AGX_ASC_IO;

APPLE_AGX_ASC_RESULT AppleAgxAscReadCpuStatus(const APPLE_AGX_ASC_IO *Io,
                                               APPLE_AGX_ASC_U32 *Status);
APPLE_AGX_ASC_RESULT AppleAgxAscReadInboxControl(
    const APPLE_AGX_ASC_IO *Io, APPLE_AGX_ASC_U32 *Control);
APPLE_AGX_ASC_RESULT AppleAgxAscReadOutboxControl(
    const APPLE_AGX_ASC_IO *Io, APPLE_AGX_ASC_U32 *Control);
APPLE_AGX_ASC_RESULT AppleAgxAscSetRun(const APPLE_AGX_ASC_IO *Io,
                                       APPLE_AGX_ASC_BOOL Run);
APPLE_AGX_ASC_RESULT AppleAgxAscWaitRunning(const APPLE_AGX_ASC_IO *Io,
                                            APPLE_AGX_ASC_U64 DeadlineMs,
                                            APPLE_AGX_ASC_U32 *Status);
APPLE_AGX_ASC_RESULT AppleAgxAscSend(const APPLE_AGX_ASC_IO *Io,
                                     APPLE_AGX_ASC_U64 Payload,
                                     APPLE_AGX_ASC_U32 Endpoint,
                                     APPLE_AGX_ASC_U64 DeadlineMs);
APPLE_AGX_ASC_RESULT AppleAgxAscReceive(const APPLE_AGX_ASC_IO *Io,
                                        APPLE_AGX_ASC_MESSAGE *Message,
                                        APPLE_AGX_ASC_U64 DeadlineMs);

#endif /* APPLE_AGX_ASC_TRANSPORT_H */
