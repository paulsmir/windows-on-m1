#include "apple_agx_asc_transport.h"

#include <assert.h>
#include <string.h>

typedef struct _FAKE_ASC {
  unsigned long long Now;
  unsigned int Read32Values[9];
  unsigned long long Read64Values[4];
  unsigned int Read32Count;
  unsigned int Read64Count;
  unsigned int Write32Count;
  unsigned int Write64Count;
  unsigned int LastOffset;
  unsigned long long LastValue;
  unsigned char FailRead;
  unsigned char FailWrite;
} FAKE_ASC;

static unsigned long long FakeNow(void *Context) {
  return ((FAKE_ASC *)Context)->Now++;
}

static unsigned char FakeRead32(void *Context, unsigned int Offset,
                                unsigned int *Value) {
  FAKE_ASC *fake = (FAKE_ASC *)Context;
  unsigned int index = fake->Read32Count++;
  fake->LastOffset = Offset;
  if (fake->FailRead || Value == 0)
    return 0;
  *Value = fake->Read32Values[index];
  return 1;
}

static unsigned char FakeRead64(void *Context, unsigned int Offset,
                                unsigned long long *Value) {
  FAKE_ASC *fake = (FAKE_ASC *)Context;
  unsigned int index = fake->Read64Count++;
  fake->LastOffset = Offset;
  if (fake->FailRead || Value == 0)
    return 0;
  *Value = fake->Read64Values[index];
  return 1;
}

static unsigned char FakeWrite32(void *Context, unsigned int Offset,
                                 unsigned int Value) {
  FAKE_ASC *fake = (FAKE_ASC *)Context;
  fake->Write32Count++;
  fake->LastOffset = Offset;
  fake->LastValue = Value;
  return fake->FailWrite ? 0 : 1;
}

static unsigned char FakeWrite64(void *Context, unsigned int Offset,
                                 unsigned long long Value) {
  FAKE_ASC *fake = (FAKE_ASC *)Context;
  fake->Write64Count++;
  fake->LastOffset = Offset;
  fake->LastValue = Value;
  return fake->FailWrite ? 0 : 1;
}

static APPLE_AGX_ASC_IO MakeIo(FAKE_ASC *Fake) {
  APPLE_AGX_ASC_IO io;
  memset(&io, 0, sizeof(io));
  io.Context = Fake;
  io.NowMs = FakeNow;
  io.Read32 = FakeRead32;
  io.Read64 = FakeRead64;
  io.Write32 = FakeWrite32;
  io.Write64 = FakeWrite64;
  return io;
}

static void TestReadCpuStatusUsesTypedOffset(void) {
  FAKE_ASC fake;
  APPLE_AGX_ASC_IO io;
  unsigned int status = 0;

  memset(&fake, 0, sizeof(fake));
  fake.Read32Values[0] = 0x21u;
  io = MakeIo(&fake);
  assert(AppleAgxAscReadCpuStatus(&io, &status) == AppleAgxAscResultOk);
  assert(status == 0x21u);
  assert(fake.LastOffset == J313_AGX_G2_ASC_CPU_STATUS_OFFSET);
  assert(fake.Read32Count == 1u);
  assert(fake.Read64Count == 0u);
}

static void TestSetRunPreservesOtherControlBits(void) {
  FAKE_ASC fake;
  APPLE_AGX_ASC_IO io;

  memset(&fake, 0, sizeof(fake));
  fake.Read32Values[0] = 0x101u;
  io = MakeIo(&fake);
  assert(AppleAgxAscSetRun(&io, 1u) == AppleAgxAscResultOk);
  assert(fake.Write32Count == 1u);
  assert(fake.LastOffset == J313_AGX_G2_ASC_CPU_CONTROL_OFFSET);
  assert(fake.LastValue == 0x111u);

  memset(&fake, 0, sizeof(fake));
  fake.Read32Values[0] = 0x111u;
  io = MakeIo(&fake);
  assert(AppleAgxAscSetRun(&io, 0u) == AppleAgxAscResultOk);
  assert(fake.LastValue == 0x101u);
}

static void TestSendWaitsForSpaceThenPublishesPayloadBeforeEndpoint(void) {
  FAKE_ASC fake;
  APPLE_AGX_ASC_IO io;

  memset(&fake, 0, sizeof(fake));
  fake.Read32Values[0] = APPLE_AGX_ASC_MAILBOX_FULL;
  fake.Read32Values[1] = 0u;
  io = MakeIo(&fake);
  assert(AppleAgxAscSend(&io, 0x1122334455667788ULL, 0x20u, 10u) ==
         AppleAgxAscResultOk);
  assert(fake.Read32Count == 2u);
  assert(fake.Write64Count == 2u);
  assert(fake.LastOffset == J313_AGX_G2_ASC_INBOX1_OFFSET);
  assert(fake.LastValue == 0x20u);
}

static void TestReceiveWaitsForMessageAndExtractsEndpoint(void) {
  FAKE_ASC fake;
  APPLE_AGX_ASC_IO io;
  APPLE_AGX_ASC_MESSAGE message;

  memset(&fake, 0, sizeof(fake));
  fake.Read32Values[0] = APPLE_AGX_ASC_MAILBOX_EMPTY;
  fake.Read32Values[1] = 0u;
  fake.Read64Values[0] = 0xaabbccddeeff0011ULL;
  fake.Read64Values[1] = 0x1234000000000021ULL;
  io = MakeIo(&fake);
  assert(AppleAgxAscReceive(&io, &message, 10u) == AppleAgxAscResultOk);
  assert(message.Payload == 0xaabbccddeeff0011ULL);
  assert(message.Endpoint == 0x21u);
  assert(fake.Read64Count == 2u);
}

static void TestBoundedWaitAndTransportFailures(void) {
  FAKE_ASC fake;
  APPLE_AGX_ASC_IO io;
  APPLE_AGX_ASC_MESSAGE message;

  memset(&fake, 0, sizeof(fake));
  fake.Read32Values[0] = APPLE_AGX_ASC_MAILBOX_FULL;
  fake.Read32Values[1] = APPLE_AGX_ASC_MAILBOX_FULL;
  fake.Read32Values[2] = APPLE_AGX_ASC_MAILBOX_FULL;
  io = MakeIo(&fake);
  assert(AppleAgxAscSend(&io, 1u, 0u, 1u) == AppleAgxAscResultTimeout);
  assert(fake.Write64Count == 0u);

  memset(&fake, 0, sizeof(fake));
  fake.FailRead = 1u;
  io = MakeIo(&fake);
  assert(AppleAgxAscReadCpuStatus(&io, &fake.Read32Values[0]) ==
         AppleAgxAscResultTransportFailed);

  memset(&fake, 0, sizeof(fake));
  fake.Read32Values[0] = 0u;
  fake.FailWrite = 1u;
  io = MakeIo(&fake);
  assert(AppleAgxAscSend(&io, 1u, 0u, 10u) ==
         AppleAgxAscResultTransportFailed);

  memset(&fake, 0, sizeof(fake));
  io = MakeIo(&fake);
  assert(AppleAgxAscSend(&io, 1u, 0x100u, 10u) ==
         AppleAgxAscResultInvalidArgument);
  assert(AppleAgxAscReceive(&io, 0, 10u) ==
         AppleAgxAscResultInvalidArgument);
  assert(AppleAgxAscReceive(0, &message, 10u) ==
         AppleAgxAscResultInvalidArgument);
}

static void TestSendAndReceiveRequireOnlyTheirOwnCallbacks(void) {
  FAKE_ASC fake;
  APPLE_AGX_ASC_IO io;
  APPLE_AGX_ASC_MESSAGE message;

  memset(&fake, 0, sizeof(fake));
  io = MakeIo(&fake);
  io.Read64 = 0;
  assert(AppleAgxAscSend(&io, 0x55u, 0x20u, 10u) == AppleAgxAscResultOk);

  memset(&fake, 0, sizeof(fake));
  fake.Read64Values[0] = 0x66u;
  fake.Read64Values[1] = 0x21u;
  io = MakeIo(&fake);
  io.Write64 = 0;
  assert(AppleAgxAscReceive(&io, &message, 10u) == AppleAgxAscResultOk);
  assert(message.Payload == 0x66u);
  assert(message.Endpoint == 0x21u);
}

int main(void) {
  TestReadCpuStatusUsesTypedOffset();
  TestSetRunPreservesOtherControlBits();
  TestSendWaitsForSpaceThenPublishesPayloadBeforeEndpoint();
  TestReceiveWaitsForMessageAndExtractsEndpoint();
  TestBoundedWaitAndTransportFailures();
  TestSendAndReceiveRequireOnlyTheirOwnCallbacks();
  return 0;
}
