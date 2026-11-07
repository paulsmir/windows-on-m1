/* Combined-channel recovery through the real provider and queue runtime.
 * WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * Every participating channel needs its own recovery; partial retirement
 * cannot release the combined submission. Ambiguous command failure must
 * retain ownership and forbid publication retries through the real provider.
 * Compile with shared/include and shared/tests on the include path, linking
 * apple_agx_g13_queue_{runtime,provider}.c and apple_agx_g13_codec.c.
 */
#define main existing_provider_main
#include "apple_agx_g13_queue_provider_test.c"
#undef main

static unsigned calls, mask, fail;

static APPLE_AGX_BACKEND_BOOL RecoverChannel(
    void *context, unsigned fence, unsigned pipe,
    unsigned long long queue, unsigned value) {
  FIXTURE *f = context;
  assert(fence == 41u && pipe < 3u);
  assert(queue == (pipe == 0u ? f->Provider.Config.Ta.QueueInfoGpuAddress :
      pipe == 1u ? f->Provider.Config.D3.QueueInfoGpuAddress :
                   f->Provider.Config.Compute.QueueInfoGpuAddress));
  assert(value == (pipe == 0u ? f->Job.TaExpectedStamp :
      pipe == 1u ? f->Job.D3ExpectedStamp : f->Job.ComputeExpectedStamp));
  ++calls;
  mask |= 1u << pipe;
  return !fail;
}

int main(void) {
  unsigned pipe;
  FIXTURE f;
  APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH batch;
  unsigned char event[0x38] = {0};

  InitMode(&f, 1);
  f.Provider.RuntimeIo.RecoverChannel = RecoverChannel;
  Submit(&f);
  for (pipe = 0u; pipe < 3u; ++pipe) {
    PutU32(event, 8u);
    PutU32(event + 4u, pipe);
    PutU32(event + 8u, pipe);
    PutU32(event + 12u, pipe == 0u ? 7u : pipe == 1u ? 9u : 11u);
    PutU32(event + 16u, pipe == 0u ? f.Job.TaExpectedStamp :
        pipe == 1u ? f.Job.D3ExpectedStamp : f.Job.ComputeExpectedStamp);
    assert(AppleAgxG13QueueProviderIngestEvent(
        &f.Provider, event, sizeof(event), &batch));
    assert(!batch.CompletedFence && f.Provider.PendingFence == 41u);
  }
  assert(calls == 3u && mask == 7u && !f.Quiesces);
  f.TaDone = f.TaWrite;
  f.D3Done = f.D3Write;
  assert(AppleAgxG13QueueProviderCheckTimeout(&f.Provider, 10000u, &batch));
  assert(!batch.CompletedFence);
  f.ComputeDone = f.ComputeWrite;
  assert(AppleAgxG13QueueProviderCheckTimeout(&f.Provider, 10001u, &batch));
  assert(batch.CompletedFence == 41u);
  assert(batch.Observations[0].Status == AppleAgxBackendObservationRecovered);

  InitMode(&f, 1);
  f.Provider.RuntimeIo.RecoverChannel = RecoverChannel;
  Submit(&f);
  fail = 1u;
  assert(!AppleAgxG13QueueProviderIngestEvent(
      &f.Provider, event, sizeof(event), &batch));
  assert(f.Provider.Phase == AppleAgxG13QueueProviderFaulted);
  assert(f.Provider.PendingFence == 41u && !f.Quiesces);
  fail = 0u;
  f.TaDone = f.TaWrite;
  f.D3Done = f.D3Write;
  f.ComputeDone = f.ComputeWrite;
  assert(!AppleAgxG13QueueProviderCheckTimeout(&f.Provider, 10002u, &batch));
  assert(!AppleAgxG13QueueProviderIngestEvent(
      &f.Provider, event, sizeof(event), &batch));
  assert(calls == 4u && f.Provider.PendingFence == 41u && !f.Quiesces);
  puts("provider_three_channels_and_failure_latch: ok");
  return 0;
}
