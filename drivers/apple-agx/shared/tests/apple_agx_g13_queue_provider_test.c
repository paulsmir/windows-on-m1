#include "apple_agx_g13_queue_provider.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct _FIXTURE {
  APPLE_AGX_G13_QUEUE_PROVIDER Provider;
  APPLE_AGX_BACKEND_IO Io;
  APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG Config;
  APPLE_AGX_G13_QUEUE_RUNTIME_IO RuntimeIo;
  APPLE_AGX_G13_QUEUE_PROVIDER_IO ProviderIo;
  APPLE_AGX_BACKEND_JOB_IMAGE Job;
  APPLE_AGX_BACKEND_U64 TaRing[APPLE_AGX_G13_RING_CAPACITY];
  APPLE_AGX_BACKEND_U64 D3Ring[APPLE_AGX_G13_RING_CAPACITY];
  volatile APPLE_AGX_BACKEND_U32 TaWrite, D3Write, TaDone, D3Done;
  volatile APPLE_AGX_BACKEND_U32 TaStamp, D3Stamp;
  unsigned char TaSrc[32], D3Src[32], TaDst[32], D3Dst[32];
  unsigned int Flushes, Publishes, Sends, SendOrder[2], Quiesces, Reads;
  unsigned int FailQuiesce, FailReads;
} FIXTURE;

static APPLE_AGX_BACKEND_BOOL Flush(void *Context, const void *Address,
                                    APPLE_AGX_BACKEND_U32 Bytes) {
  FIXTURE *f = Context;
  assert(Address != NULL && Bytes != 0u);
  ++f->Flushes;
  return APPLE_AGX_BACKEND_TRUE;
}
static void Barrier(void *Context) { (void)Context; }
static APPLE_AGX_BACKEND_BOOL Publish(
    void *Context, volatile APPLE_AGX_BACKEND_U32 *Address,
    APPLE_AGX_BACKEND_U32 Value) {
  FIXTURE *f = Context;
  ++f->Publishes;
  *Address = Value;
  return APPLE_AGX_BACKEND_TRUE;
}
static APPLE_AGX_BACKEND_BOOL Read(
    void *Context, const volatile APPLE_AGX_BACKEND_U32 *Address,
    APPLE_AGX_BACKEND_U32 *Value) {
  FIXTURE *f = Context;
  ++f->Reads;
  if (Address == NULL || Value == NULL)
    return APPLE_AGX_BACKEND_FALSE;
  if (f->FailReads)
    return APPLE_AGX_BACKEND_FALSE;
  *Value = *Address;
  return APPLE_AGX_BACKEND_TRUE;
}
static APPLE_AGX_BACKEND_BOOL Send(
    void *Context, APPLE_AGX_BACKEND_U32 QueueType,
    const unsigned char Message[APPLE_AGX_G13_RUN_MESSAGE_SIZE]) {
  FIXTURE *f = Context;
  assert(Message != NULL && f->Sends < 2u);
  f->SendOrder[f->Sends++] = QueueType;
  return APPLE_AGX_BACKEND_TRUE;
}
static APPLE_AGX_BACKEND_BOOL Quiesce(void *Context,
                                      APPLE_AGX_BACKEND_U32 Fence) {
  FIXTURE *f = Context;
  assert(Fence == 41u);
  ++f->Quiesces;
  return f->FailQuiesce ? APPLE_AGX_BACKEND_FALSE : APPLE_AGX_BACKEND_TRUE;
}
static APPLE_AGX_BACKEND_BOOL Build(
    void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
    APPLE_AGX_BACKEND_U32 Fence,
    APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION *Submission) {
  FIXTURE *f = Context;
  memset(Submission, 0, sizeof(*Submission));
  Submission->Fence = Fence;
  Submission->Timestamp = 99ULL;
  Submission->NowTicks = 1000ULL;
  Submission->Ta.PreparedRanges[0].Address = f->TaSrc;
  Submission->Ta.PreparedRanges[0].Bytes = sizeof(f->TaSrc);
  Submission->Ta.PreparedRanges[1].Address = f->TaDst;
  Submission->Ta.PreparedRanges[1].Bytes = sizeof(f->TaDst);
  Submission->Ta.PreparedRangeCount = 2u;
  Submission->Ta.GpuAddresses[0] = Job->TaWorkAddresses[0];
  Submission->Ta.GpuAddresses[1] = Job->TaWorkAddresses[1];
  Submission->Ta.GpuAddressCount = Job->TaWorkAddressCount;
  Submission->Ta.ExpectedStamp = Job->TaExpectedStamp;
  Submission->D3.PreparedRanges[0].Address = f->D3Src;
  Submission->D3.PreparedRanges[0].Bytes = sizeof(f->D3Src);
  Submission->D3.PreparedRanges[1].Address = f->D3Dst;
  Submission->D3.PreparedRanges[1].Bytes = sizeof(f->D3Dst);
  Submission->D3.PreparedRangeCount = 2u;
  Submission->D3.GpuAddresses[0] = Job->D3WorkAddresses[0];
  Submission->D3.GpuAddresses[1] = Job->D3WorkAddresses[1];
  Submission->D3.GpuAddressCount = Job->D3WorkAddressCount;
  Submission->D3.ExpectedStamp = Job->D3ExpectedStamp;
  return APPLE_AGX_BACKEND_TRUE;
}
static void PutU32(unsigned char *p, unsigned int v) {
  p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8);
  p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24);
}
static void PutU64(unsigned char *p, unsigned long long v) {
  PutU32(p, (unsigned int)v); PutU32(p+4, (unsigned int)(v>>32));
}
static void Event(unsigned char *message, unsigned int number) {
  unsigned long long firing[2] = {0ULL, 0ULL};
  memset(message, 0, APPLE_AGX_G13_EVENT_MESSAGE_SIZE);
  PutU32(message, (unsigned int)AppleAgxG13EventFlag);
  firing[number/64u] |= 1ULL << (number%64u);
  PutU64(message+4, firing[0]); PutU64(message+12, firing[1]);
}
static void Init(FIXTURE *f) {
  memset(f, 0, sizeof(*f));
  f->Config.Ta.QueueType = (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueTa;
  f->Config.Ta.QueueInfoGpuAddress = 0x1500010000ULL;
  f->Config.Ta.RingCpuAddress=f->TaRing;
  f->Config.Ta.RingCapacity=APPLE_AGX_G13_RING_CAPACITY;
  f->Config.Ta.CpuWritePointer=&f->TaWrite;
  f->Config.Ta.GpuDonePointer=&f->TaDone;
  f->Config.Ta.Stamp=&f->TaStamp; f->Config.Ta.EventNumber=7u;
  f->Config.D3.QueueType = (APPLE_AGX_BACKEND_U32)AppleAgxG13Queue3d;
  f->Config.D3.QueueInfoGpuAddress = 0x1500020000ULL;
  f->Config.D3.RingCpuAddress=f->D3Ring;
  f->Config.D3.RingCapacity=APPLE_AGX_G13_RING_CAPACITY;
  f->Config.D3.CpuWritePointer=&f->D3Write;
  f->Config.D3.GpuDonePointer=&f->D3Done;
  f->Config.D3.Stamp=&f->D3Stamp; f->Config.D3.EventNumber=9u;
  f->Config.TimeoutTicks=100u;
  f->RuntimeIo.Context=f; f->RuntimeIo.FlushForDevice=Flush;
  f->RuntimeIo.MemoryBarrier=Barrier; f->RuntimeIo.PublishU32=Publish;
  f->RuntimeIo.ReadU32=Read; f->RuntimeIo.SendRunMessage=Send;
  f->RuntimeIo.Quiesce=Quiesce;
  f->ProviderIo.Context=f; f->ProviderIo.BuildSubmission=Build;
  f->Job.TaWorkAddresses[0]=0x1500100000ULL;
  f->Job.TaWorkAddresses[1]=0x1500108000ULL;
  f->Job.TaWorkAddressCount=2u;
  f->Job.D3WorkAddresses[0]=0x1500200000ULL;
  f->Job.D3WorkAddresses[1]=0x1500208000ULL;
  f->Job.D3WorkAddressCount=2u;
  f->Job.TaEvent=7u; f->Job.D3Event=9u;
  f->Job.TaExpectedStamp=0x200u; f->Job.D3ExpectedStamp=0x300u;
  f->Job.TaExpectedDonePointer=2u; f->Job.D3ExpectedDonePointer=2u;
  assert(AppleAgxG13QueueProviderInitialize(&f->Provider, &f->Config,
                                             &f->RuntimeIo, &f->ProviderIo));
  memset(&f->Io, 0, sizeof(f->Io)); f->Io.Context=&f->Provider;
  assert(AppleAgxG13QueueProviderInstall(&f->Io));
}
static void Submit(FIXTURE *f) {
  assert(f->Io.Queues.Create(f->Io.Context));
  assert(f->Io.Queues.Run3d(f->Io.Context, &f->Job, 41u));
  assert(f->Io.Queues.RunTa(f->Io.Context, &f->Job, 41u));
}
static void Complete(FIXTURE *f) {
  APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH batch;
  unsigned char event[APPLE_AGX_G13_EVENT_MESSAGE_SIZE];
  f->D3Stamp=f->Job.D3ExpectedStamp; f->D3Done=f->D3Write;
  Event(event,9u);
  assert(AppleAgxG13QueueProviderIngestEvent(
      &f->Provider,event,sizeof(event),&batch));
  assert(batch.CompletedFence==0u);
  f->TaStamp=f->Job.TaExpectedStamp; f->TaDone=f->TaWrite;
  Event(event,7u);
  assert(AppleAgxG13QueueProviderIngestEvent(
      &f->Provider,event,sizeof(event),&batch));
  assert(batch.CompletedFence==41u);
  assert(f->Provider.Phase==AppleAgxG13QueueProviderCreated);
}
static void TestReadOnlyJobPlanTracksQueueLifetime(void) {
  FIXTURE f;
  APPLE_AGX_G13_QUEUE_JOB_PLAN plan;
  Init(&f);
  memset(&plan, 0xa5, sizeof(plan));
  assert(!AppleAgxG13QueueProviderPlanJob(&f.Provider, &plan));
  assert(f.Io.Queues.Create(f.Io.Context));
  assert(AppleAgxG13QueueProviderPlanJob(&f.Provider, &plan));
  assert(plan.IncludeInitBm);
  assert(plan.TaExpectedDonePointer == 2u);
  assert(plan.D3ExpectedDonePointer == 2u);
  assert(f.TaWrite == 0u && f.D3Write == 0u);
  assert(f.Io.Queues.Run3d(f.Io.Context, &f.Job, 41u));
  assert(!AppleAgxG13QueueProviderPlanJob(&f.Provider, &plan));
  assert(f.Io.Queues.RunTa(f.Io.Context, &f.Job, 41u));
  Complete(&f);
  assert(AppleAgxG13QueueProviderPlanJob(&f.Provider, &plan));
  assert(!plan.IncludeInitBm);
  assert(plan.TaExpectedDonePointer == 3u);
  assert(plan.D3ExpectedDonePointer == 4u);
  assert(f.TaWrite == 2u && f.D3Write == 2u);
}
static void TestAtomicStagingAndOrder(void) {
  FIXTURE f; APPLE_AGX_BACKEND_JOB_IMAGE mismatch;
  Init(&f); assert(f.Io.Queues.Create(f.Io.Context));
  assert(f.Io.Queues.Run3d(f.Io.Context, &f.Job, 41u));
  assert(f.Flushes==0u && f.Publishes==0u && f.Sends==0u);
  mismatch=f.Job; mismatch.D3WorkAddresses[1]+=0x20ULL;
  assert(!f.Io.Queues.RunTa(f.Io.Context, &mismatch, 41u));
  assert(f.Publishes==0u && f.Sends==0u);
  assert(f.Io.Queues.RunTa(f.Io.Context, &f.Job, 41u));
  assert(f.Publishes==2u && f.Sends==2u);
  assert(f.SendOrder[0]==(unsigned int)AppleAgxG13Queue3d);
  assert(f.SendOrder[1]==(unsigned int)AppleAgxG13QueueTa);
}
static void TestExactCompletion(void) {
  FIXTURE f; APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH batch;
  unsigned char event[APPLE_AGX_G13_EVENT_MESSAGE_SIZE];
  Init(&f); Submit(&f); f.D3Done=2u; f.D3Stamp=0x2ffu; Event(event,9u);
  assert(AppleAgxG13QueueProviderIngestEvent(&f.Provider,event,sizeof(event),&batch));
  assert(batch.ObservationCount==0u && batch.CompletedFence==0u);
  f.D3Stamp=0x301u;
  assert(AppleAgxG13QueueProviderIngestEvent(&f.Provider,event,sizeof(event),&batch));
  assert(batch.ObservationCount==1u && batch.Observations[0].Queue==AppleAgxBackendQueue3d);
  assert(batch.Observations[0].Stamp==0x301u);
  assert(batch.CompletedFence==0u);
  f.TaStamp=0x200u; f.TaDone=0u; Event(event,7u);
  assert(AppleAgxG13QueueProviderIngestEvent(&f.Provider,event,sizeof(event),&batch));
  assert(batch.ObservationCount==0u && batch.CompletedFence==0u);
  f.TaDone=2u;
  assert(AppleAgxG13QueueProviderIngestEvent(&f.Provider,event,sizeof(event),&batch));
  assert(batch.ObservationCount==1u && batch.Observations[0].Queue==AppleAgxBackendQueueTa);
  assert(batch.CompletedFence==41u);
}
static void TestIngestFailureNamesDecoderOwner(void) {
  FIXTURE f;
  APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH batch;
  unsigned char event[APPLE_AGX_G13_EVENT_MESSAGE_SIZE] = {0};
  Init(&f);
  Submit(&f);
  event[0] = 2u;
  assert(!AppleAgxG13QueueProviderIngestEvent(
      &f.Provider, event, sizeof(event), &batch));
  assert(f.Provider.LastIngestGuard ==
         AppleAgxG13QueueProviderIngestGuardRuntime);
  assert(f.Provider.LastIngestRuntimeResult ==
         AppleAgxG13QueueRuntimeResultInvalidArgument);
}
static void TestSecondSubmitPublishesOnlyWorkTa(void) {
  FIXTURE f;
  Init(&f); Submit(&f);
  assert(f.TaRing[0]==f.Job.TaWorkAddresses[0]);
  assert(f.TaRing[1]==f.Job.TaWorkAddresses[1]);
  assert(f.D3Ring[0]==f.Job.D3WorkAddresses[0]);
  assert(f.D3Ring[1]==f.Job.D3WorkAddresses[1]);
  assert(f.Provider.Runtime.BufferManagerInitialized);
  Complete(&f);
  assert(f.Provider.Runtime.BufferManagerInitialized);

  f.Flushes=0u; f.Publishes=0u; f.Sends=0u;
  f.Job.TaExpectedStamp=0x400u; f.Job.D3ExpectedStamp=0x500u;
  f.Job.TaExpectedDonePointer=3u;
  f.Job.D3ExpectedDonePointer=4u;
  assert(f.Io.Queues.Run3d(f.Io.Context,&f.Job,41u));
  assert(f.Io.Queues.RunTa(f.Io.Context,&f.Job,41u));
  assert(f.TaRing[2]==f.Job.TaWorkAddresses[1]);
  assert(f.TaRing[3]==0ULL);
  assert(f.D3Ring[2]==f.Job.D3WorkAddresses[0]);
  assert(f.D3Ring[3]==f.Job.D3WorkAddresses[1]);
  assert(f.TaWrite==3u && f.D3Write==4u);
  assert(f.Provider.Runtime.TaPending.ExpectedDonePointer==3u);
  assert(f.Provider.Runtime.D3Pending.ExpectedDonePointer==4u);
  assert(f.Flushes==6u);
}
static void TestFailClosedQuiesce(void) {
  FIXTURE f; Init(&f); f.Provider.RuntimeIo.Quiesce=NULL;
  assert(!f.Io.Queues.Create(f.Io.Context));
  Init(&f); Submit(&f); f.FailQuiesce=1u;
  assert(!f.Io.Queues.Stop(f.Io.Context,41u)); assert(f.Quiesces==1u);
  assert(!f.Io.Queues.Stop(f.Io.Context,0u));
  f.FailQuiesce=0u;
  assert(f.Io.Queues.Stop(f.Io.Context,41u));
  assert(f.Quiesces==2u);
  assert(f.Provider.Phase==AppleAgxG13QueueProviderCreated);
  assert(f.Io.Queues.Destroy(f.Io.Context));

  Init(&f); Submit(&f); f.FailQuiesce=1u;
  assert(!f.Io.Queues.Reset(f.Io.Context,41u)); assert(f.Quiesces==1u);
  f.FailQuiesce=0u;
  assert(f.Io.Queues.Reset(f.Io.Context,41u));
  assert(f.Quiesces==2u);
  assert(f.Provider.Phase==AppleAgxG13QueueProviderCreated);
}
static void TestSuccessfulStopAndResetAreSynchronous(void) {
  FIXTURE f;
  Init(&f); Submit(&f);
  assert(f.Io.Queues.Stop(f.Io.Context,41u));
  assert(f.Quiesces==1u);
  assert(f.Provider.Phase==AppleAgxG13QueueProviderCreated);
  assert(f.Io.Queues.Destroy(f.Io.Context));
  Init(&f); Submit(&f);
  assert(f.Io.Queues.Reset(f.Io.Context,41u));
  assert(f.Quiesces==1u);
  assert(f.Provider.Phase==AppleAgxG13QueueProviderCreated);
}
static void TestReadyStopWithoutPendingFenceIsNoOp(void) {
  FIXTURE f;
  Init(&f);
  assert(f.Io.Queues.Create(f.Io.Context));
  assert(f.Provider.Phase==AppleAgxG13QueueProviderCreated);
  assert(f.Provider.PendingFence==0u);
  assert(f.Io.Queues.Stop(f.Io.Context,0u));
  assert(f.Quiesces==0u);
  assert(f.Provider.Phase==AppleAgxG13QueueProviderCreated);
  assert(f.Io.Queues.Destroy(f.Io.Context));
  Init(&f);
  assert(f.Io.Queues.Create(f.Io.Context));
  assert(f.Io.Queues.Run3d(f.Io.Context,&f.Job,41u));
  assert(!f.Io.Queues.Stop(f.Io.Context,0u));
  assert(f.Io.Queues.Stop(f.Io.Context,41u));
  Init(&f);
  Submit(&f);
  assert(!f.Io.Queues.Stop(f.Io.Context,0u));
  assert(f.Io.Queues.Stop(f.Io.Context,41u));
}
static void TestTimeoutRequiresTruthfulQuiesce(void) {
  FIXTURE f;
  APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH batch;
  Init(&f); Submit(&f);
  assert(AppleAgxG13QueueProviderCheckTimeout(&f.Provider, 1099ULL, &batch));
  assert(batch.ObservationCount == 0u && batch.CompletedFence == 0u);
  assert(f.Quiesces == 0u);
  assert(AppleAgxG13QueueProviderCheckTimeout(&f.Provider, 1100ULL, &batch));
  assert(f.Quiesces == 1u);
  assert(batch.ObservationCount == 1u);
  assert(batch.Observations[0].Status == AppleAgxBackendObservationTimeout);
  assert(batch.CompletedFence == 41u);
  assert(f.Provider.Phase == AppleAgxG13QueueProviderFaulted);

  Init(&f); Submit(&f); f.FailQuiesce = 1u;
  assert(!AppleAgxG13QueueProviderCheckTimeout(&f.Provider, 1100ULL, &batch));
  assert(f.Quiesces == 1u);
  assert(f.Provider.Phase == AppleAgxG13QueueProviderFaulted);
  assert(!f.Provider.FailureQuiesced);
}

static void TestProgressReceiptReportsOnlyTheExactPendingFence(void) {
  FIXTURE f;
  APPLE_AGX_G13_QUEUE_PROGRESS progress;
  APPLE_AGX_G13_QUEUE_PROVIDER before;
  unsigned int flushes, publishes, sends, quiesces, reads;

  Init(&f);
  Submit(&f);
  f.TaDone = 1u;
  f.TaStamp = 0x200u;
  f.D3Done = 2u;
  f.D3Stamp = 0x301u;
  before = f.Provider;
  flushes = f.Flushes;
  publishes = f.Publishes;
  sends = f.Sends;
  quiesces = f.Quiesces;
  reads = f.Reads;
  assert(AppleAgxG13QueueProviderQueryProgress(&f.Provider, &progress));
  assert(progress.Fence == 41u);
  assert(progress.ProviderPhase == AppleAgxG13QueueProviderSubmitted);
  assert(progress.RuntimePhase == AppleAgxG13QueueRuntimeSubmitted);
  assert(progress.TaDonePointer == 1u && progress.TaStamp == 0x200u);
  assert(progress.D3DonePointer == 2u && progress.D3Stamp == 0x301u);
  assert(!progress.TaEventSeen && !progress.TaComplete);
  assert(!progress.D3EventSeen && !progress.D3Complete);
  assert(f.Reads == reads + 4u);
  assert(f.Flushes == flushes && f.Publishes == publishes &&
         f.Sends == sends && f.Quiesces == quiesces);
  assert(memcmp(&f.Provider, &before, sizeof(before)) == 0);
}

static void TestProgressReceiptDetectsAdvancingSample(void) {
  FIXTURE f;
  APPLE_AGX_G13_QUEUE_PROGRESS before, after;

  Init(&f);
  Submit(&f);
  assert(AppleAgxG13QueueProviderQueryProgress(&f.Provider, &before));
  f.D3Done = 1u;
  f.D3Stamp = 0x301u;
  assert(AppleAgxG13QueueProviderQueryProgress(&f.Provider, &after));
  assert(AppleAgxG13QueueProgressHasAdvanced(&before, &after));
}

static void TestProgressReceiptRejectsUnchangedSampleAsNotAdvanced(void) {
  FIXTURE f;
  APPLE_AGX_G13_QUEUE_PROGRESS before, after;

  Init(&f);
  Submit(&f);
  assert(AppleAgxG13QueueProviderQueryProgress(&f.Provider, &before));
  assert(AppleAgxG13QueueProviderQueryProgress(&f.Provider, &after));
  assert(!AppleAgxG13QueueProgressHasAdvanced(&before, &after));
}

static void TestProgressReceiptFailsClosedForInvalidStateAndRead(void) {
  FIXTURE f;
  APPLE_AGX_G13_QUEUE_PROGRESS progress;
  APPLE_AGX_G13_QUEUE_PROVIDER before;

  Init(&f);
  assert(!AppleAgxG13QueueProviderQueryProgress(&f.Provider, &progress));
  assert(f.Reads == 0u);
  Submit(&f);
  before = f.Provider;
  f.D3Done = APPLE_AGX_G13_RING_CAPACITY;
  assert(!AppleAgxG13QueueProviderQueryProgress(&f.Provider, &progress));
  assert(memcmp(&f.Provider, &before, sizeof(before)) == 0);
  f.D3Done = 0u;
  f.FailReads = 1u;
  assert(!AppleAgxG13QueueProviderQueryProgress(&f.Provider, &progress));
  assert(memcmp(&f.Provider, &before, sizeof(before)) == 0);
}
int main(void) {
  TestAtomicStagingAndOrder(); TestExactCompletion(); TestFailClosedQuiesce();
  TestIngestFailureNamesDecoderOwner();
  TestReadOnlyJobPlanTracksQueueLifetime();
  TestSecondSubmitPublishesOnlyWorkTa();
  TestSuccessfulStopAndResetAreSynchronous();
  TestReadyStopWithoutPendingFenceIsNoOp();
  TestTimeoutRequiresTruthfulQuiesce();
  TestProgressReceiptReportsOnlyTheExactPendingFence();
  TestProgressReceiptDetectsAdvancingSample();
  TestProgressReceiptRejectsUnchangedSampleAsNotAdvanced();
  TestProgressReceiptFailsClosedForInvalidStateAndRead();
  puts("apple_agx_g13_queue_provider_test: ok"); return 0;
}
