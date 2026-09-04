#include "apple_agx_g13_queue_runtime.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct _TEST_IO {
  unsigned int Sequence;
  unsigned int WorkFlushes;
  unsigned int RingFlushes;
  unsigned int Barriers;
  unsigned int PointerWrites;
  unsigned int Sends;
  unsigned int Quiesces;
  unsigned int SendOrder[8];
  unsigned int Published[2];
  unsigned int FailSendAt;
  unsigned int FailQuiesce;
  unsigned int FailReads;
} TEST_IO;

static APPLE_AGX_BACKEND_BOOL TestFlush(void *Context, const void *Address,
                                        APPLE_AGX_BACKEND_U32 Bytes) {
  TEST_IO *io = (TEST_IO *)Context;
  assert(Address != NULL);
  assert(Bytes != 0u);
  ++io->Sequence;
  if (Bytes == APPLE_AGX_G13_RING_SLOT_SIZE)
    ++io->RingFlushes;
  else
    ++io->WorkFlushes;
  return APPLE_AGX_BACKEND_TRUE;
}

static void TestBarrier(void *Context) {
  TEST_IO *io = (TEST_IO *)Context;
  ++io->Sequence;
  ++io->Barriers;
}

static APPLE_AGX_BACKEND_BOOL
TestPublishU32(void *Context, volatile APPLE_AGX_BACKEND_U32 *Address,
               APPLE_AGX_BACKEND_U32 Value) {
  TEST_IO *io = (TEST_IO *)Context;
  ++io->Sequence;
  ++io->PointerWrites;
  *Address = Value;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL TestReadU32(
    void *Context, const volatile APPLE_AGX_BACKEND_U32 *Address,
    APPLE_AGX_BACKEND_U32 *Value) {
  if (Address == NULL || Value == NULL || ((TEST_IO *)Context)->FailReads)
    return APPLE_AGX_BACKEND_FALSE;
  *Value = *Address;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL TestSend(
    void *Context, APPLE_AGX_BACKEND_U32 QueueType,
    const unsigned char Message[APPLE_AGX_G13_RUN_MESSAGE_SIZE]) {
  TEST_IO *io = (TEST_IO *)Context;
  ++io->Sequence;
  ++io->Sends;
  io->SendOrder[io->Sends - 1u] = QueueType;
  assert(Message != NULL);
  if (io->FailSendAt == io->Sends)
    return APPLE_AGX_BACKEND_FALSE;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL TestQuiesce(void *Context,
                                          APPLE_AGX_BACKEND_U32 Fence) {
  TEST_IO *io = (TEST_IO *)Context;
  assert(Fence != 0u);
  ++io->Quiesces;
  return io->FailQuiesce ? APPLE_AGX_BACKEND_FALSE
                         : APPLE_AGX_BACKEND_TRUE;
}

typedef struct _TEST_FIXTURE {
  TEST_IO IoState;
  APPLE_AGX_G13_QUEUE_RUNTIME Runtime;
  unsigned long long TaRing[APPLE_AGX_G13_RING_CAPACITY];
  unsigned long long D3Ring[APPLE_AGX_G13_RING_CAPACITY];
  volatile APPLE_AGX_BACKEND_U32 TaWrite;
  volatile APPLE_AGX_BACKEND_U32 D3Write;
  volatile APPLE_AGX_BACKEND_U32 TaDone;
  volatile APPLE_AGX_BACKEND_U32 D3Done;
  volatile APPLE_AGX_BACKEND_U32 TaStamp;
  volatile APPLE_AGX_BACKEND_U32 D3Stamp;
  unsigned char TaDestination[64];
  unsigned char D3Destination[64];
  unsigned char TaSource[64];
  unsigned char D3Source[64];
} TEST_FIXTURE;

static void TestInitialize(TEST_FIXTURE *Fixture) {
  APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG config;
  APPLE_AGX_G13_QUEUE_RUNTIME_IO io;

  memset(Fixture, 0, sizeof(*Fixture));
  memset(&config, 0, sizeof(config));
  memset(&io, 0, sizeof(io));
  memset(Fixture->TaSource, 0xa5, sizeof(Fixture->TaSource));
  memset(Fixture->D3Source, 0x5a, sizeof(Fixture->D3Source));

  config.Ta.QueueType = (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueTa;
  config.Ta.QueueInfoGpuAddress = 0x1500010000ULL;
  config.Ta.RingCpuAddress = Fixture->TaRing;
  config.Ta.RingCapacity = APPLE_AGX_G13_RING_CAPACITY;
  config.Ta.CpuWritePointer = &Fixture->TaWrite;
  config.Ta.GpuDonePointer = &Fixture->TaDone;
  config.Ta.Stamp = &Fixture->TaStamp;
  config.Ta.EventNumber = 7u;
  config.D3.QueueType = (APPLE_AGX_BACKEND_U32)AppleAgxG13Queue3d;
  config.D3.QueueInfoGpuAddress = 0x1500020000ULL;
  config.D3.RingCpuAddress = Fixture->D3Ring;
  config.D3.RingCapacity = APPLE_AGX_G13_RING_CAPACITY;
  config.D3.CpuWritePointer = &Fixture->D3Write;
  config.D3.GpuDonePointer = &Fixture->D3Done;
  config.D3.Stamp = &Fixture->D3Stamp;
  config.D3.EventNumber = 9u;
  config.TimeoutTicks = 100u;

  io.Context = &Fixture->IoState;
  io.FlushForDevice = TestFlush;
  io.MemoryBarrier = TestBarrier;
  io.PublishU32 = TestPublishU32;
  io.ReadU32 = TestReadU32;
  io.SendRunMessage = TestSend;
  io.Quiesce = TestQuiesce;
  assert(AppleAgxG13QueueRuntimeInitialize(&Fixture->Runtime, &config, &io) ==
         AppleAgxG13QueueRuntimeResultOk);
}

static APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION
TestSubmission(TEST_FIXTURE *Fixture, APPLE_AGX_BACKEND_U32 Fence) {
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION submission;
  memset(&submission, 0, sizeof(submission));
  submission.Fence = Fence;
  submission.Timestamp = 0x1122334455667788ULL;
  submission.NowTicks = 1000ULL;
  submission.Ta.Source = Fixture->TaSource;
  submission.Ta.Destination = Fixture->TaDestination;
  submission.Ta.Bytes = sizeof(Fixture->TaSource);
  submission.Ta.GpuAddresses[0] = 0x1500100000ULL;
  submission.Ta.GpuAddresses[1] = 0x1500108000ULL;
  submission.Ta.GpuAddressCount = 2u;
  submission.Ta.ExpectedStamp = 0x200u;
  submission.D3.Source = Fixture->D3Source;
  submission.D3.Destination = Fixture->D3Destination;
  submission.D3.Bytes = sizeof(Fixture->D3Source);
  submission.D3.GpuAddresses[0] = 0x1500200000ULL;
  submission.D3.GpuAddresses[1] = 0x1500208000ULL;
  submission.D3.GpuAddressCount = 2u;
  submission.D3.ExpectedStamp = 0x300u;
  return submission;
}

static void PutU32(unsigned char *Destination, unsigned int Value) {
  Destination[0] = (unsigned char)(Value & 0xffu);
  Destination[1] = (unsigned char)((Value >> 8) & 0xffu);
  Destination[2] = (unsigned char)((Value >> 16) & 0xffu);
  Destination[3] = (unsigned char)((Value >> 24) & 0xffu);
}

static void PutU64(unsigned char *Destination, unsigned long long Value) {
  PutU32(Destination, (unsigned int)Value);
  PutU32(Destination + 4, (unsigned int)(Value >> 32));
}

static void TestEvent(unsigned char Message[APPLE_AGX_G13_EVENT_MESSAGE_SIZE],
                      unsigned int FirstEvent,
                      unsigned int SecondEvent) {
  unsigned long long firing[2] = {0ULL, 0ULL};
  memset(Message, 0, APPLE_AGX_G13_EVENT_MESSAGE_SIZE);
  PutU32(Message, (unsigned int)AppleAgxG13EventFlag);
  firing[FirstEvent / 64u] |= 1ULL << (FirstEvent % 64u);
  if (SecondEvent < APPLE_AGX_G13_EVENT_COUNT)
    firing[SecondEvent / 64u] |= 1ULL << (SecondEvent % 64u);
  PutU64(Message + 4, firing[0]);
  PutU64(Message + 12, firing[1]);
}

static void TestCompleteSubmission(
    TEST_FIXTURE *Fixture,
    const APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION *Submission) {
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION completion;
  unsigned char event[APPLE_AGX_G13_EVENT_MESSAGE_SIZE];

  Fixture->D3Stamp = Submission->D3.ExpectedStamp;
  Fixture->D3Done = Fixture->D3Write;
  Fixture->TaStamp = Submission->Ta.ExpectedStamp;
  Fixture->TaDone = Fixture->TaWrite;
  TestEvent(event, 7u, 9u);
  assert(AppleAgxG13QueueRuntimeHandleEvent(
             &Fixture->Runtime, event, sizeof(event)) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(AppleAgxG13QueueRuntimeTakeCompletion(&Fixture->Runtime,
                                               &completion));
  assert(completion.Fence == Submission->Fence);
  assert(completion.Status == AppleAgxG13QueueCompletionSuccess);
}

static void TestJoinedPublishAndExactCompletion(void) {
  TEST_FIXTURE fixture;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION submission;
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION completion;
  unsigned char event[APPLE_AGX_G13_EVENT_MESSAGE_SIZE];

  TestInitialize(&fixture);
  submission = TestSubmission(&fixture, 42u);
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(memcmp(fixture.TaDestination, fixture.TaSource,
                sizeof(fixture.TaSource)) == 0);
  assert(memcmp(fixture.D3Destination, fixture.D3Source,
                sizeof(fixture.D3Source)) == 0);
  assert(fixture.D3Ring[0] == submission.D3.GpuAddresses[0]);
  assert(fixture.D3Ring[1] == submission.D3.GpuAddresses[1]);
  assert(fixture.TaRing[0] == submission.Ta.GpuAddresses[0]);
  assert(fixture.TaRing[1] == submission.Ta.GpuAddresses[1]);
  assert(fixture.D3Write == 2u && fixture.TaWrite == 2u);
  assert(fixture.IoState.WorkFlushes == 2u);
  assert(fixture.IoState.RingFlushes == 4u);
  assert(fixture.IoState.PointerWrites == 2u);
  assert(fixture.IoState.Sends == 2u);
  assert(fixture.IoState.SendOrder[0] ==
         (unsigned int)AppleAgxG13Queue3d);
  assert(fixture.IoState.SendOrder[1] ==
         (unsigned int)AppleAgxG13QueueTa);
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&fixture.Runtime,
                                                &completion));

  fixture.D3Stamp = submission.D3.ExpectedStamp;
  fixture.D3Done = 2u;
  TestEvent(event, 9u, APPLE_AGX_G13_EVENT_COUNT);
  assert(AppleAgxG13QueueRuntimeHandleEvent(
             &fixture.Runtime, event, sizeof(event)) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&fixture.Runtime,
                                                &completion));

  fixture.TaStamp = submission.Ta.ExpectedStamp;
  fixture.TaDone = 2u;
  TestEvent(event, 7u, APPLE_AGX_G13_EVENT_COUNT);
  assert(AppleAgxG13QueueRuntimeHandleEvent(
             &fixture.Runtime, event, sizeof(event)) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(AppleAgxG13QueueRuntimeTakeCompletion(&fixture.Runtime,
                                               &completion));
  assert(completion.Fence == 42u);
  assert(completion.Status == AppleAgxG13QueueCompletionSuccess);
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&fixture.Runtime,
                                                &completion));
}

static void TestPreparedRangesNeedNoCopy(void) {
  TEST_FIXTURE fixture;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION submission;
  TestInitialize(&fixture);
  submission = TestSubmission(&fixture, 47u);
  submission.Ta.Source = NULL;
  submission.Ta.Destination = NULL;
  submission.Ta.Bytes = 0u;
  submission.Ta.PreparedRanges[0].Address = fixture.TaSource;
  submission.Ta.PreparedRanges[0].Bytes = 16u;
  submission.Ta.PreparedRanges[1].Address = fixture.TaSource + 16u;
  submission.Ta.PreparedRanges[1].Bytes = 16u;
  submission.Ta.PreparedRangeCount = 2u;
  submission.D3.Source = NULL;
  submission.D3.Destination = NULL;
  submission.D3.Bytes = 0u;
  submission.D3.PreparedRanges[0].Address = fixture.D3Source;
  submission.D3.PreparedRanges[0].Bytes = 16u;
  submission.D3.PreparedRanges[1].Address = fixture.D3Source + 16u;
  submission.D3.PreparedRanges[1].Bytes = 16u;
  submission.D3.PreparedRangeCount = 2u;
  memset(fixture.TaDestination, 0x11, sizeof(fixture.TaDestination));
  memset(fixture.D3Destination, 0x22, sizeof(fixture.D3Destination));
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(fixture.TaDestination[0] == 0x11u);
  assert(fixture.D3Destination[0] == 0x22u);
  assert(fixture.IoState.WorkFlushes == 4u);
}

static void TestFirstAndLaterTaPublicationContract(void) {
  TEST_FIXTURE fixture;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION first;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION later;

  TestInitialize(&fixture);
  first = TestSubmission(&fixture, 50u);
  assert(!fixture.Runtime.BufferManagerInitialized);
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &first) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(fixture.TaRing[0] == first.Ta.GpuAddresses[0]);
  assert(fixture.TaRing[1] == first.Ta.GpuAddresses[1]);
  assert(fixture.D3Ring[0] == first.D3.GpuAddresses[0]);
  assert(fixture.D3Ring[1] == first.D3.GpuAddresses[1]);
  assert(fixture.TaWrite == 2u && fixture.D3Write == 2u);
  assert(fixture.Runtime.BufferManagerInitialized);
  TestCompleteSubmission(&fixture, &first);
  assert(fixture.Runtime.BufferManagerInitialized);

  memset(&fixture.IoState, 0, sizeof(fixture.IoState));
  later = TestSubmission(&fixture, 51u);
  later.NowTicks = 1200ULL;
  later.Ta.GpuAddresses[0] = first.Ta.GpuAddresses[1];
  later.Ta.GpuAddresses[1] = 0ULL;
  later.Ta.GpuAddressCount = 1u;
  later.Ta.ExpectedStamp = 0x400u;
  later.D3.GpuAddresses[0] = 0x1500210000ULL;
  later.D3.GpuAddresses[1] = 0x1500218000ULL;
  later.D3.ExpectedStamp = 0x500u;
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &later) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(fixture.TaRing[2] == first.Ta.GpuAddresses[1]);
  assert(fixture.TaRing[3] == 0ULL);
  assert(fixture.D3Ring[2] == later.D3.GpuAddresses[0]);
  assert(fixture.D3Ring[3] == later.D3.GpuAddresses[1]);
  assert(fixture.TaWrite == 3u && fixture.D3Write == 4u);
  assert(fixture.Runtime.TaPending.ExpectedDonePointer == 3u);
  assert(fixture.Runtime.D3Pending.ExpectedDonePointer == 4u);
  assert(fixture.IoState.RingFlushes == 3u);
}

static void TestCountsAreBoundedAndQueueSpecific(void) {
  TEST_FIXTURE fixture;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION submission;

  TestInitialize(&fixture);
  submission = TestSubmission(&fixture, 52u);
  submission.Ta.GpuAddressCount = 1u;
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultInvalidArgument);
  submission = TestSubmission(&fixture, 52u);
  submission.D3.GpuAddressCount = 1u;
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultInvalidArgument);
  submission = TestSubmission(&fixture, 52u);
  submission.Ta.GpuAddressCount = 0u;
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultInvalidArgument);

  submission = TestSubmission(&fixture, 52u);
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultOk);
  TestCompleteSubmission(&fixture, &submission);
  submission = TestSubmission(&fixture, 53u);
  submission.NowTicks = 1200ULL;
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultInvalidArgument);
}

static void TestLaterSubmitWrapUsesActualCounts(void) {
  TEST_FIXTURE fixture;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION submission;

  TestInitialize(&fixture);
  fixture.Runtime.BufferManagerInitialized = APPLE_AGX_BACKEND_TRUE;
  fixture.Runtime.TaFirstRun = APPLE_AGX_BACKEND_FALSE;
  fixture.Runtime.D3FirstRun = APPLE_AGX_BACKEND_FALSE;
  fixture.Runtime.Config.Ta.RingCapacity = 4u;
  fixture.Runtime.Config.D3.RingCapacity = 4u;
  fixture.TaWrite = 3u;
  fixture.D3Write = 3u;
  fixture.TaDone = 2u;
  fixture.D3Done = 2u;
  submission = TestSubmission(&fixture, 54u);
  submission.Ta.GpuAddresses[0] = submission.Ta.GpuAddresses[1];
  submission.Ta.GpuAddresses[1] = 0ULL;
  submission.Ta.GpuAddressCount = 1u;
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(fixture.TaRing[3] == submission.Ta.GpuAddresses[0]);
  assert(fixture.TaRing[0] == 0ULL);
  assert(fixture.D3Ring[3] == submission.D3.GpuAddresses[0]);
  assert(fixture.D3Ring[0] == submission.D3.GpuAddresses[1]);
  assert(fixture.TaWrite == 0u && fixture.D3Write == 1u);

  TestInitialize(&fixture);
  fixture.Runtime.BufferManagerInitialized = APPLE_AGX_BACKEND_TRUE;
  fixture.Runtime.TaFirstRun = APPLE_AGX_BACKEND_FALSE;
  fixture.Runtime.D3FirstRun = APPLE_AGX_BACKEND_FALSE;
  fixture.Runtime.Config.Ta.RingCapacity = 4u;
  fixture.Runtime.Config.D3.RingCapacity = 4u;
  fixture.TaWrite = 3u;
  fixture.D3Write = 3u;
  fixture.TaDone = 1u;
  fixture.D3Done = 1u;
  submission = TestSubmission(&fixture, 55u);
  submission.Ta.GpuAddresses[0] = submission.Ta.GpuAddresses[1];
  submission.Ta.GpuAddresses[1] = 0ULL;
  submission.Ta.GpuAddressCount = 1u;
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultBusy);
  assert(fixture.TaRing[3] == 0ULL && fixture.D3Ring[3] == 0ULL);
  assert(fixture.IoState.RingFlushes == 0u);
}

static void TestResetRequiresBufferManagerInitializationAgain(void) {
  TEST_FIXTURE fixture;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION submission;

  TestInitialize(&fixture);
  submission = TestSubmission(&fixture, 56u);
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultOk);
  TestCompleteSubmission(&fixture, &submission);
  assert(fixture.Runtime.BufferManagerInitialized);
  assert(AppleAgxG13QueueRuntimeReset(&fixture.Runtime) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(!fixture.Runtime.BufferManagerInitialized);
  assert(fixture.Runtime.TaFirstRun && fixture.Runtime.D3FirstRun);
  submission = TestSubmission(&fixture, 57u);
  submission.NowTicks = 1400ULL;
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(fixture.Runtime.TaPending.ExpectedDonePointer == 4u);
  assert(fixture.Runtime.D3Pending.ExpectedDonePointer == 4u);
}

static void TestTwoEntryWrapAndFreeSpace(void) {
  TEST_FIXTURE fixture;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION submission;

  TestInitialize(&fixture);
  fixture.Runtime.Config.Ta.RingCapacity = 4u;
  fixture.Runtime.Config.D3.RingCapacity = 4u;
  fixture.TaWrite = 3u;
  fixture.D3Write = 3u;
  fixture.TaDone = 2u;
  fixture.D3Done = 2u;
  submission = TestSubmission(&fixture, 48u);
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(fixture.TaRing[3] == submission.Ta.GpuAddresses[0]);
  assert(fixture.TaRing[0] == submission.Ta.GpuAddresses[1]);
  assert(fixture.D3Ring[3] == submission.D3.GpuAddresses[0]);
  assert(fixture.D3Ring[0] == submission.D3.GpuAddresses[1]);
  assert(fixture.TaWrite == 1u && fixture.D3Write == 1u);

  TestInitialize(&fixture);
  fixture.Runtime.Config.Ta.RingCapacity = 4u;
  fixture.Runtime.Config.D3.RingCapacity = 4u;
  fixture.TaWrite = 3u;
  fixture.D3Write = 3u;
  fixture.TaDone = 1u;
  fixture.D3Done = 1u;
  submission = TestSubmission(&fixture, 49u);
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultBusy);
  assert(fixture.TaWrite == 3u && fixture.D3Write == 3u);
  assert(fixture.TaRing[3] == 0ULL && fixture.TaRing[0] == 0ULL);
  assert(fixture.D3Ring[3] == 0ULL && fixture.D3Ring[0] == 0ULL);
  assert(fixture.IoState.WorkFlushes == 0u);
  assert(fixture.IoState.RingFlushes == 0u);
  assert(fixture.IoState.PointerWrites == 0u);
  assert(fixture.IoState.Sends == 0u);
}

static void TestEventAloneCannotComplete(void) {
  TEST_FIXTURE fixture;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION submission;
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION completion;
  unsigned char event[APPLE_AGX_G13_EVENT_MESSAGE_SIZE];

  TestInitialize(&fixture);
  submission = TestSubmission(&fixture, 43u);
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultOk);
  TestEvent(event, 7u, 9u);
  assert(AppleAgxG13QueueRuntimeHandleEvent(
             &fixture.Runtime, event, sizeof(event)) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&fixture.Runtime,
                                                &completion));
  fixture.TaStamp = submission.Ta.ExpectedStamp;
  fixture.D3Stamp = submission.D3.ExpectedStamp;
  fixture.TaDone = 2u;
  fixture.D3Done = 2u;
  TestEvent(event, 7u, 9u);
  assert(AppleAgxG13QueueRuntimeHandleEvent(
             &fixture.Runtime, event, sizeof(event)) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(AppleAgxG13QueueRuntimeTakeCompletion(&fixture.Runtime,
                                               &completion));
}

static void TestTimeoutAndCancelRequireQuiescence(void) {
  TEST_FIXTURE fixture;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION submission;
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION completion;

  TestInitialize(&fixture);
  submission = TestSubmission(&fixture, 44u);
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(AppleAgxG13QueueRuntimeCheckTimeout(&fixture.Runtime, 1099ULL) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&fixture.Runtime,
                                                &completion));
  assert(AppleAgxG13QueueRuntimeCheckTimeout(&fixture.Runtime, 1100ULL) ==
         AppleAgxG13QueueRuntimeResultTimedOut);
  assert(fixture.IoState.Quiesces == 1u);
  assert(AppleAgxG13QueueRuntimeReset(&fixture.Runtime) ==
         AppleAgxG13QueueRuntimeResultInvalidState);
  assert(AppleAgxG13QueueRuntimeTakeCompletion(&fixture.Runtime,
                                               &completion));
  assert(completion.Fence == 44u);
  assert(completion.Status == AppleAgxG13QueueCompletionTimedOut);

  assert(AppleAgxG13QueueRuntimeReset(&fixture.Runtime) ==
         AppleAgxG13QueueRuntimeResultOk);
  submission = TestSubmission(&fixture, 45u);
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultOk);
  assert(AppleAgxG13QueueRuntimeCancel(&fixture.Runtime, 45u) ==
         AppleAgxG13QueueRuntimeResultCancelled);
  assert(AppleAgxG13QueueRuntimeTakeCompletion(&fixture.Runtime,
                                               &completion));
  assert(completion.Status == AppleAgxG13QueueCompletionCancelled);
}

static void TestPartialTransportFailureFaultsWithoutFence(void) {
  TEST_FIXTURE fixture;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION submission;
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION completion;

  TestInitialize(&fixture);
  fixture.IoState.FailSendAt = 2u;
  submission = TestSubmission(&fixture, 46u);
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultTransportFailed);
  assert(fixture.IoState.Quiesces == 1u);
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&fixture.Runtime,
                                                &completion));
  assert(AppleAgxG13QueueRuntimeReset(&fixture.Runtime) ==
         AppleAgxG13QueueRuntimeResultOk);

  TestInitialize(&fixture);
  fixture.IoState.FailSendAt = 2u;
  fixture.IoState.FailQuiesce = 1u;
  submission = TestSubmission(&fixture, 47u);
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultResetFailed);
  assert(fixture.Runtime.Phase == AppleAgxG13QueueRuntimeFaulted);
  assert(fixture.Runtime.PendingFence == 47u);
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&fixture.Runtime,
                                                &completion));
}

static void TestReadFailureCannotMasqueradeAsZeroPointer(void) {
  TEST_FIXTURE fixture;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION submission;

  TestInitialize(&fixture);
  fixture.IoState.FailReads = 1u;
  submission = TestSubmission(&fixture, 48u);
  assert(AppleAgxG13QueueRuntimeSubmit(&fixture.Runtime, &submission) ==
         AppleAgxG13QueueRuntimeResultBusy);
  assert(fixture.IoState.PointerWrites == 0u);
  assert(fixture.IoState.Sends == 0u);
}

int main(void) {
  TestJoinedPublishAndExactCompletion();
  TestPreparedRangesNeedNoCopy();
  TestFirstAndLaterTaPublicationContract();
  TestCountsAreBoundedAndQueueSpecific();
  TestLaterSubmitWrapUsesActualCounts();
  TestResetRequiresBufferManagerInitializationAgain();
  TestTwoEntryWrapAndFreeSpace();
  TestEventAloneCannotComplete();
  TestTimeoutAndCancelRequireQuiescence();
  TestPartialTransportFailureFaultsWithoutFence();
  TestReadFailureCannotMasqueradeAsZeroPointer();
  puts("apple_agx_g13_queue_runtime_test: ok");
  return 0;
}
