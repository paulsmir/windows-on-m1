#include "apple_agx_rtkit_session.h"

#include <assert.h>
#include <string.h>

typedef struct _FAKE_SESSION_ASC {
  unsigned long long Now;
  unsigned int Control;
  unsigned long long ReceivePayload[8];
  unsigned int ReceiveEndpoint[8];
  unsigned int ReceiveCount;
  unsigned int ReceiveIndex;
  unsigned long long SendPayload[16];
  unsigned int SendEndpoint[16];
  unsigned int SendCount;
  unsigned long long PendingPayload;
  unsigned char FailWrite;
  unsigned int CpuStatus[8];
  unsigned int CpuStatusCount;
  unsigned int CpuStatusIndex;
  unsigned int InboxControl[8];
  unsigned int InboxControlCount;
  unsigned int InboxControlIndex;
} FAKE_SESSION_ASC;

static unsigned long long FakeNow(void *Context) {
  return ((FAKE_SESSION_ASC *)Context)->Now++;
}

static unsigned char FakeRead32(void *Context, unsigned int Offset,
                                unsigned int *Value) {
  FAKE_SESSION_ASC *fake = Context;
  if (Offset == J313_AGX_G2_ASC_CPU_CONTROL_OFFSET)
    *Value = fake->Control;
  else if (Offset == J313_AGX_G2_ASC_CPU_STATUS_OFFSET) {
    if (fake->CpuStatusIndex < fake->CpuStatusCount)
      *Value = fake->CpuStatus[fake->CpuStatusIndex++];
    else
      *Value = APPLE_AGX_ASC_CPU_RUNNING;
  }
  else if (Offset == J313_AGX_G2_ASC_INBOX_CTRL_OFFSET) {
    if (fake->InboxControlIndex < fake->InboxControlCount)
      *Value = fake->InboxControl[fake->InboxControlIndex++];
    else
      *Value = 0u;
  }
  else if (Offset == J313_AGX_G2_ASC_OUTBOX_CTRL_OFFSET)
    *Value = fake->ReceiveIndex < fake->ReceiveCount
                 ? 0u
                 : APPLE_AGX_ASC_MAILBOX_EMPTY;
  else
    return 0u;
  return 1u;
}

static unsigned char FakeRead64(void *Context, unsigned int Offset,
                                unsigned long long *Value) {
  FAKE_SESSION_ASC *fake = Context;
  if (fake->ReceiveIndex >= fake->ReceiveCount)
    return 0u;
  if (Offset == J313_AGX_G2_ASC_OUTBOX0_OFFSET) {
    *Value = fake->ReceivePayload[fake->ReceiveIndex];
    return 1u;
  }
  if (Offset == J313_AGX_G2_ASC_OUTBOX1_OFFSET) {
    *Value = fake->ReceiveEndpoint[fake->ReceiveIndex++];
    return 1u;
  }
  return 0u;
}

static unsigned char FakeWrite32(void *Context, unsigned int Offset,
                                 unsigned int Value) {
  FAKE_SESSION_ASC *fake = Context;
  if (fake->FailWrite || Offset != J313_AGX_G2_ASC_CPU_CONTROL_OFFSET)
    return 0u;
  fake->Control = Value;
  return 1u;
}

static unsigned char FakeWrite64(void *Context, unsigned int Offset,
                                 unsigned long long Value) {
  FAKE_SESSION_ASC *fake = Context;
  if (fake->FailWrite)
    return 0u;
  if (Offset == J313_AGX_G2_ASC_INBOX0_OFFSET) {
    fake->PendingPayload = Value;
    return 1u;
  }
  if (Offset == J313_AGX_G2_ASC_INBOX1_OFFSET && fake->SendCount < 16u) {
    fake->SendPayload[fake->SendCount] = fake->PendingPayload;
    fake->SendEndpoint[fake->SendCount] = (unsigned int)Value;
    fake->SendCount++;
    return 1u;
  }
  return 0u;
}

static unsigned char FakePause(void *Context) {
  (void)Context;
  return 1u;
}

static APPLE_AGX_ASC_IO MakeIo(FAKE_SESSION_ASC *Fake) {
  APPLE_AGX_ASC_IO io;
  memset(&io, 0, sizeof(io));
  io.Context = Fake;
  io.NowMs = FakeNow;
  io.Read32 = FakeRead32;
  io.Read64 = FakeRead64;
  io.Write32 = FakeWrite32;
  io.Write64 = FakeWrite64;
  io.Pause = FakePause;
  return io;
}

static void Queue(FAKE_SESSION_ASC *Fake, unsigned long long Payload) {
  Fake->ReceivePayload[Fake->ReceiveCount] = Payload;
  Fake->ReceiveEndpoint[Fake->ReceiveCount] = 0u;
  Fake->ReceiveCount++;
}

static void QueueBoot(FAKE_SESSION_ASC *Fake) {
  Queue(Fake, 0x0010000000040001ULL);
  Queue(Fake, 0x0088000000000000ULL);
  Queue(Fake, 0x0070000000000020ULL);
  Queue(Fake, 0x00b0000000000020ULL);
}

static void TestBootAndStopAreExactAndBounded(void) {
  FAKE_SESSION_ASC fake;
  APPLE_AGX_ASC_IO io;
  APPLE_AGX_RTKIT_SESSION session;

  memset(&fake, 0, sizeof(fake));
  QueueBoot(&fake);
  io = MakeIo(&fake);
  AppleAgxRtkitSessionInitialize(&session);
  assert(AppleAgxRtkitSessionBoot(&session, &io, 100u) ==
         AppleAgxRtkitSessionResultOk);
  assert(session.Running);
  assert(session.CpuReady);
  assert((fake.Control & APPLE_AGX_ASC_CPU_RUN) != 0u);
  assert(fake.SendCount == 4u);
  assert(fake.SendPayload[0] == 0x0060000000000220ULL);
  assert(fake.SendPayload[1] == 0x0020000000040004ULL);
  assert(fake.SendPayload[2] == 0x0088000000000000ULL);
  assert(fake.SendPayload[3] == 0x00b0000000000020ULL);

  Queue(&fake, 0x00b0000000000010ULL);
  Queue(&fake, 0x0070000000000010ULL);
  assert(AppleAgxRtkitSessionStop(&session, &io, 200u) ==
         AppleAgxRtkitSessionResultOk);
  assert(!session.Running);
  assert((fake.Control & APPLE_AGX_ASC_CPU_RUN) == 0u);
  assert(fake.SendPayload[4] == 0x00b0000000000010ULL);
  assert(fake.SendPayload[5] == 0x0060000000000010ULL);
}

static void TestProtocolFailureClearsRun(void) {
  FAKE_SESSION_ASC fake;
  APPLE_AGX_ASC_IO io;
  APPLE_AGX_RTKIT_SESSION session;

  memset(&fake, 0, sizeof(fake));
  Queue(&fake, 0x0010000000040001ULL);
  fake.ReceiveEndpoint[0] = 1u;
  io = MakeIo(&fake);
  AppleAgxRtkitSessionInitialize(&session);
  assert(AppleAgxRtkitSessionBoot(&session, &io, 100u) ==
         AppleAgxRtkitSessionResultProtocolViolation);
  assert(!session.Running);
  assert(session.CpuReady);
  assert((fake.Control & APPLE_AGX_ASC_CPU_RUN) == 0u);
}

static void TestBootWaitsForCpuReadyBeforeSendingWake(void) {
  FAKE_SESSION_ASC fake;
  APPLE_AGX_ASC_IO io;
  APPLE_AGX_RTKIT_SESSION session;

  memset(&fake, 0, sizeof(fake));
  fake.CpuStatus[0] = APPLE_AGX_ASC_CPU_STOPPED;
  fake.CpuStatus[1] = APPLE_AGX_ASC_CPU_RUNNING;
  fake.CpuStatusCount = 2u;
  QueueBoot(&fake);
  io = MakeIo(&fake);
  AppleAgxRtkitSessionInitialize(&session);
  assert(AppleAgxRtkitSessionBoot(&session, &io, 100u) ==
         AppleAgxRtkitSessionResultOk);
  assert(fake.CpuStatusIndex == 2u);
  assert(session.CpuReady);
  assert(fake.SendCount == 4u);
}

static void TestHelloTimeoutPreservesMailboxSnapshots(void) {
  FAKE_SESSION_ASC fake;
  APPLE_AGX_ASC_IO io;
  APPLE_AGX_RTKIT_SESSION session;

  memset(&fake, 0, sizeof(fake));
  fake.InboxControl[0] = 0x00000101u;
  fake.InboxControl[1] = 0x00000101u;
  fake.InboxControl[2] = 0x00100201u;
  fake.InboxControl[3] = 0x00010201u;
  fake.InboxControlCount = 4u;
  io = MakeIo(&fake);
  AppleAgxRtkitSessionInitialize(&session);
  assert(AppleAgxRtkitSessionBoot(&session, &io, 8u) ==
         AppleAgxRtkitSessionResultTimeout);
  assert(session.InboxBeforeInitValid);
  assert(session.InboxAfterInitValid);
  assert(session.InboxAtFailureValid);
  assert(session.OutboxAtFailureValid);
  assert(session.InboxControlBeforeInit == 0x00000101u);
  assert(session.InboxControlAfterInit == 0x00100201u);
  assert(session.InboxControlAtFailure == 0x00010201u);
  assert(session.OutboxControlAtFailure == APPLE_AGX_ASC_MAILBOX_EMPTY);
}

int main(void) {
  TestBootAndStopAreExactAndBounded();
  TestProtocolFailureClearsRun();
  TestBootWaitsForCpuReadyBeforeSendingWake();
  TestHelloTimeoutPreservesMailboxSnapshots();
  return 0;
}
