#include "apple_agx_asc_transport.h"

#define APPLE_AGX_ASC_NULL ((void *)0)

static APPLE_AGX_ASC_BOOL AppleAgxAscReadIoValid(
    const APPLE_AGX_ASC_IO *Io) {
  return (APPLE_AGX_ASC_BOOL)(Io != APPLE_AGX_ASC_NULL &&
                              Io->Read32 != APPLE_AGX_ASC_NULL);
}

static APPLE_AGX_ASC_BOOL AppleAgxAscSendIoValid(
    const APPLE_AGX_ASC_IO *Io) {
  return (APPLE_AGX_ASC_BOOL)(
      Io != APPLE_AGX_ASC_NULL && Io->NowMs != APPLE_AGX_ASC_NULL &&
      Io->Read32 != APPLE_AGX_ASC_NULL &&
      Io->Write64 != APPLE_AGX_ASC_NULL);
}

static APPLE_AGX_ASC_BOOL AppleAgxAscReceiveIoValid(
    const APPLE_AGX_ASC_IO *Io) {
  return (APPLE_AGX_ASC_BOOL)(
      Io != APPLE_AGX_ASC_NULL && Io->NowMs != APPLE_AGX_ASC_NULL &&
      Io->Read32 != APPLE_AGX_ASC_NULL && Io->Read64 != APPLE_AGX_ASC_NULL);
}

static APPLE_AGX_ASC_RESULT AppleAgxAscCheckDeadline(
    const APPLE_AGX_ASC_IO *Io, APPLE_AGX_ASC_U64 StartMs,
    APPLE_AGX_ASC_U64 DeadlineMs) {
  APPLE_AGX_ASC_U64 now = Io->NowMs(Io->Context);
  if (now < StartMs)
    return AppleAgxAscResultClockRegression;
  if (now > DeadlineMs)
    return AppleAgxAscResultTimeout;
  return AppleAgxAscResultOk;
}

APPLE_AGX_ASC_RESULT AppleAgxAscReadCpuStatus(const APPLE_AGX_ASC_IO *Io,
                                               APPLE_AGX_ASC_U32 *Status) {
  if (!AppleAgxAscReadIoValid(Io) || Status == APPLE_AGX_ASC_NULL)
    return AppleAgxAscResultInvalidArgument;
  if (!Io->Read32(Io->Context, J313_AGX_G2_ASC_CPU_STATUS_OFFSET, Status))
    return AppleAgxAscResultTransportFailed;
  return AppleAgxAscResultOk;
}

APPLE_AGX_ASC_RESULT AppleAgxAscSetRun(const APPLE_AGX_ASC_IO *Io,
                                       APPLE_AGX_ASC_BOOL Run) {
  APPLE_AGX_ASC_U32 control;

  if (!AppleAgxAscReadIoValid(Io) || Io->Write32 == APPLE_AGX_ASC_NULL ||
      Run > APPLE_AGX_ASC_TRUE)
    return AppleAgxAscResultInvalidArgument;
  if (!Io->Read32(Io->Context, J313_AGX_G2_ASC_CPU_CONTROL_OFFSET,
                  &control))
    return AppleAgxAscResultTransportFailed;
  if (Run != APPLE_AGX_ASC_FALSE)
    control |= APPLE_AGX_ASC_CPU_RUN;
  else
    control &= ~APPLE_AGX_ASC_CPU_RUN;
  if (!Io->Write32(Io->Context, J313_AGX_G2_ASC_CPU_CONTROL_OFFSET,
                   control))
    return AppleAgxAscResultTransportFailed;
  return AppleAgxAscResultOk;
}

APPLE_AGX_ASC_RESULT AppleAgxAscSend(const APPLE_AGX_ASC_IO *Io,
                                     APPLE_AGX_ASC_U64 Payload,
                                     APPLE_AGX_ASC_U32 Endpoint,
                                     APPLE_AGX_ASC_U64 DeadlineMs) {
  APPLE_AGX_ASC_U64 start_ms;
  APPLE_AGX_ASC_U32 control;
  APPLE_AGX_ASC_RESULT result;

  if (!AppleAgxAscSendIoValid(Io) || Endpoint > 0xffu)
    return AppleAgxAscResultInvalidArgument;
  start_ms = Io->NowMs(Io->Context);
  if (start_ms > DeadlineMs)
    return AppleAgxAscResultTimeout;
  for (;;) {
    if (!Io->Read32(Io->Context, J313_AGX_G2_ASC_INBOX_CTRL_OFFSET,
                    &control))
      return AppleAgxAscResultTransportFailed;
    if ((control & APPLE_AGX_ASC_MAILBOX_FULL) == 0u)
      break;
    if (Io->Pause != APPLE_AGX_ASC_NULL && !Io->Pause(Io->Context))
      return AppleAgxAscResultTransportFailed;
    result = AppleAgxAscCheckDeadline(Io, start_ms, DeadlineMs);
    if (result != AppleAgxAscResultOk)
      return result;
  }
  if (!Io->Write64(Io->Context, J313_AGX_G2_ASC_INBOX0_OFFSET, Payload) ||
      !Io->Write64(Io->Context, J313_AGX_G2_ASC_INBOX1_OFFSET,
                   (APPLE_AGX_ASC_U64)Endpoint))
    return AppleAgxAscResultTransportFailed;
  return AppleAgxAscResultOk;
}

APPLE_AGX_ASC_RESULT AppleAgxAscReceive(const APPLE_AGX_ASC_IO *Io,
                                        APPLE_AGX_ASC_MESSAGE *Message,
                                        APPLE_AGX_ASC_U64 DeadlineMs) {
  APPLE_AGX_ASC_U64 start_ms;
  APPLE_AGX_ASC_U64 selector;
  APPLE_AGX_ASC_U32 control;
  APPLE_AGX_ASC_RESULT result;

  if (!AppleAgxAscReceiveIoValid(Io) || Message == APPLE_AGX_ASC_NULL)
    return AppleAgxAscResultInvalidArgument;
  start_ms = Io->NowMs(Io->Context);
  if (start_ms > DeadlineMs)
    return AppleAgxAscResultTimeout;
  for (;;) {
    if (!Io->Read32(Io->Context, J313_AGX_G2_ASC_OUTBOX_CTRL_OFFSET,
                    &control))
      return AppleAgxAscResultTransportFailed;
    if ((control & APPLE_AGX_ASC_MAILBOX_EMPTY) == 0u)
      break;
    if (Io->Pause != APPLE_AGX_ASC_NULL && !Io->Pause(Io->Context))
      return AppleAgxAscResultTransportFailed;
    result = AppleAgxAscCheckDeadline(Io, start_ms, DeadlineMs);
    if (result != AppleAgxAscResultOk)
      return result;
  }
  if (!Io->Read64(Io->Context, J313_AGX_G2_ASC_OUTBOX0_OFFSET,
                  &Message->Payload) ||
      !Io->Read64(Io->Context, J313_AGX_G2_ASC_OUTBOX1_OFFSET, &selector))
    return AppleAgxAscResultTransportFailed;
  Message->Endpoint = (APPLE_AGX_ASC_U32)(selector & 0xffULL);
  return AppleAgxAscResultOk;
}
