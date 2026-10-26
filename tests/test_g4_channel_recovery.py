"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A firmware ChannelError must use RecoverChannel, not stop the retained RTKit
session. Receipt is not retirement: every active queue keeps DMA ownership until
its own GPU done pointer reaches the published endpoint.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'


def replay(program, sources):
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / 'replay.c'
        source.write_text(program)
        binary = Path(tmp) / 'replay'
        subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                        '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        '-I', str(SHARED / 'include'), '-I', str(SHARED / 'tests'),
                        str(source), *[str(SHARED / 'src' / s) for s in sources],
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


class ChannelRecovery(unittest.TestCase):
    def test_provider_retirement_and_ambiguous_failure(self):
        replay((SHARED / 'tests/apple_agx_channel_recovery_test.c').read_text(),
               ['apple_agx_g13_queue_runtime.c', 'apple_agx_g13_codec.c',
                'apple_agx_g13_queue_provider.c'])

    def test_missing_channel_handler_does_not_stop_retained_firmware(self):
        replay(r'''
#include "apple_agx_g13_queue_runtime_test.c"
#undef assert
#include <assert.h>
int main(void) {
  TEST_FIXTURE f;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION s;
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION c;
  unsigned char event[0x38] = {0};
  TestInitialize(&f);
  s = TestSubmission(&f, 101u);
  assert(AppleAgxG13QueueRuntimeSubmit(&f.Runtime, &s) == 0);
  PutU32(event, 8u); PutU32(event + 4, 1u);
  PutU32(event + 8, 0u); PutU32(event + 12, 7u);
  PutU32(event + 16, s.Ta.ExpectedStamp);
  (void)AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime, event, sizeof(event));
  assert(f.IoState.Quiesces == 0); /* old code stops RTKit here */
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime, &c));
  assert(f.Runtime.PendingFence == 101u);
  return 0;
}
''', ['apple_agx_g13_queue_runtime.c', 'apple_agx_g13_codec.c'])

    def test_channel_wire_encoding_and_device_control_receipt(self):
        replay(r'''
#define main existing_device_control_tests
#include "apple_agx_device_control_test.c"
#undef main
#include "apple_agx_g13_codec.h"
int main(void) {
  unsigned char event[0x38] = {8,0,0,0, 1,0,0,0, 2,0,0,0, 11,0,0,0,
                             0,0x34,0x12,0xab};
  /* Independently encoded from Asahi fw/channels.rs, G13/V13_5. */
  const unsigned char expected[0x30] = {15,0,0,0, 2,0,0,0,
      0,0,3,0,0x15,0,0,0, 0,0x34,0x12,0xab};
  unsigned char msg[0x30] = {0}, state[0x30] = {0}, ring[0x3000] = {0};
  APPLE_AGX_G13_EVENT decoded;
  APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
  APPLE_AGX_DEVICE_CONTROL_PUBLICATION publication;
  FAKE_TRANSPORT fake = {0};
  APPLE_AGX_PLATFORM_TRANSPORT_IO io;
  unsigned polls;
  assert(AppleAgxG13DecodeEvent(event, sizeof(event), &decoded));
  assert(decoded.ChannelErrorType == 1 && decoded.ChannelPipeType == 2 &&
         decoded.ChannelEventSlot == 11 && decoded.ChannelEventValue == 0xab123400u);
  assert(AppleAgxDeviceControlEncodeRecoverChannelG13V13_5(2, 0x1500030000ULL,
      0xab123400u, msg, sizeof(msg)) == AppleAgxDeviceControlResultOk);
  assert(!memcmp(msg, expected, sizeof(msg)));
  assert(AppleAgxDeviceControlEncodeRecoverChannelG13V13_5(2, 0x1500030000ULL,
      1, msg, sizeof(msg)) == AppleAgxDeviceControlResultDestinationNotZero);
  memset(msg, 0, sizeof(msg));
  assert(AppleAgxDeviceControlEncodeRecoverChannelG13V13_5(3, 0x1500030000ULL,
      1, msg, sizeof(msg)) != AppleAgxDeviceControlResultOk);
  assert(AppleAgxDeviceControlEncodeRecoverChannelG13V13_5(2, 0,
      1, msg, sizeof(msg)) != AppleAgxDeviceControlResultOk);
  assert(AppleAgxDeviceControlEncodeRecoverChannelG13V13_5(2, 0x1500030001ULL,
      1, msg, sizeof(msg)) != AppleAgxDeviceControlResultOk);
  prepare_owner(&owner, state, ring); fake.State=state; fake.Ring=ring; io=make_io(&fake);
  assert(AppleAgxDeviceControlPublishRecoverChannelG13V13_5(&owner, &io,
      2, 0x1500030000ULL, 0xab123400u, &publication) == AppleAgxDeviceControlResultOk);
  assert(!memcmp(ring, expected, sizeof(expected)));
  assert(fake.PublishCalls == 1 && fake.DoorbellCalls == 1 && fake.LastDoorbell == 0x11);
  assert(AppleAgxDeviceControlWaitForReceiptG13V13_5(&publication, &io, 2, &polls) ==
         AppleAgxDeviceControlResultReceiptTimedOut && polls == 2);
  state[0] = 1;
  assert(AppleAgxDeviceControlWaitForReceiptG13V13_5(&publication, &io, 1, &polls) == 0);
  /* Full queue and invalid firmware pointers do not publish or kick. */
  state[0] = 2;
  assert(AppleAgxDeviceControlPublishRecoverChannelG13V13_5(&owner, &io,
      2, 0x1500030000ULL, 0xab123400u, &publication) == AppleAgxDeviceControlResultRingFull);
  state[1] = 1;
  assert(AppleAgxDeviceControlPublishRecoverChannelG13V13_5(&owner, &io,
      2, 0x1500030000ULL, 0xab123400u, &publication) == AppleAgxDeviceControlResultPointer);
  fake.FailRead = 1;
  assert(AppleAgxDeviceControlPublishRecoverChannelG13V13_5(&owner, &io,
      2, 0x1500030000ULL, 0xab123400u, &publication) == AppleAgxDeviceControlResultTransportFailed);
  assert(fake.PublishCalls == 1 && fake.DoorbellCalls == 1);
  return 0;
}
''', ['apple_agx_device_control.c', 'apple_agx_rtkit.c', 'apple_agx_g13_codec.c'])

    def test_channel_recovery_retains_all_queues_and_survives_follow_on_work(self):
        replay(r'''
#include "apple_agx_g13_queue_runtime_test.c"
#undef assert
#include <assert.h>
static unsigned calls, fail, pipe_seen, value_seen;
static unsigned long long queue_seen;
static APPLE_AGX_BACKEND_BOOL recover_channel(void *ctx, unsigned fence,
    unsigned pipe, unsigned long long queue, unsigned value) {
  (void)ctx; assert(fence == 101); ++calls;
  pipe_seen=pipe; queue_seen=queue; value_seen=value;
  return !fail;
}
int main(void) {
  unsigned mode, after_timeout, bad;
  for (mode=0; mode<3; ++mode) for (after_timeout=0; after_timeout<2; ++after_timeout) {
    TEST_FIXTURE f;
    APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION s;
    APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION c;
    unsigned char event[0x38] = {0};
    unsigned pipe = mode == 0 ? 0 : 2;
    unsigned slot = mode == 0 ? 7 : 11;
    unsigned value;
    TestInitialize(&f); TestEnableCompute(&f); calls=fail=0;
    f.Runtime.Io.Recover=TestRecover; f.Runtime.Io.RecoverChannel=recover_channel;
    s=TestSubmission(&f,101);
    s.Compute.Source=f.ComputeSource; s.Compute.Destination=f.ComputeDestination;
    s.Compute.Bytes=sizeof(f.ComputeSource); s.Compute.GpuAddresses[0]=0x1500300000ULL;
    s.Compute.GpuAddressCount=1; s.Compute.ExpectedStamp=0xab123400;
    value=mode == 0 ? s.Ta.ExpectedStamp : s.Compute.ExpectedStamp;
    if (mode == 0) assert(AppleAgxG13QueueRuntimeSubmit(&f.Runtime,&s)==0);
    else if (mode == 1) {
      s.ComputeOnly=1; assert(AppleAgxG13QueueRuntimeSubmitCompute(&f.Runtime,&s)==0);
    } else {
      s.Ta.GpuAddresses[2]=s.Ta.GpuAddresses[1];
      s.Ta.GpuAddresses[1]=s.Ta.GpuAddresses[0]; s.Ta.GpuAddresses[0]=0x1500308800ULL;
      s.Ta.GpuAddressCount=3; s.TaLeadingBarrier=1;
      assert(AppleAgxG13QueueRuntimeSubmitRenderWithCompute(&f.Runtime,&s)==0);
    }
    if (after_timeout) {
      PutU32(event,4); PutU32(event+4,1);
      assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==0);
    }
    PutU32(event,8); PutU32(event+4,1); PutU32(event+8,pipe);
    PutU32(event+12,slot); PutU32(event+16,value);
    /* Unknown owner/value/pipe cannot be repaired by stopping RTKit. */
    for (bad=0; bad<3; ++bad) {
      unsigned offset=bad==0 ? 8 : bad==1 ? 12 : 16;
      unsigned original=bad==0 ? pipe : bad==1 ? slot : value;
      PutU32(event+offset, original+123);
      assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==
             AppleAgxG13QueueRuntimeResultResetFailed);
      assert(!calls && !f.IoState.Quiesces);
      assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&c));
      PutU32(event+offset,original);
    }
    {
      APPLE_AGX_G13_QUEUE_RUNTIME before_failure=f.Runtime;
      fail=1;
      assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==
             AppleAgxG13QueueRuntimeResultResetFailed);
      assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&c));
      fail=0;
      assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==
             AppleAgxG13QueueRuntimeResultResetFailed);
      assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime,9999)==
             AppleAgxG13QueueRuntimeResultResetFailed);
      assert(calls==1); /* ambiguous publication is never retried */
      f.Runtime=before_failure; /* independent successful transport scenario */
    }
    assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==0);
    assert(calls==2 && pipe_seen==pipe && value_seen==value &&
           queue_seen==(mode==0 ? 0x1500010000ULL : 0x1500030000ULL));
    assert(f.Runtime.RecoveryPending && !f.IoState.Quiesces);
    assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&c));
    assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==0 && calls==2);
    assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime,10000)==0);
    assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&c));
    if (mode!=1) { f.TaDone=f.TaWrite; f.D3Done=f.D3Write; }
    if (mode) {
      assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime,10001)==0);
      assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&c));
      f.ComputeDone=f.ComputeWrite;
    }
    assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime,10002)==0);
    assert(AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&c));
    assert(c.Fence==101 && c.Status==AppleAgxG13QueueCompletionRecovered);
    assert(!f.TaStamp && !f.D3Stamp && !f.ComputeStamp && !f.IoState.Quiesces);
    /* Ready only after retirement. A delayed identical event cannot abort
     * a successor with a different stamp. */
    s=TestSubmission(&f,102);
    if (mode!=1) s.Ta.GpuAddressCount=1;
    s.Ta.ExpectedStamp+=0x100; s.D3.ExpectedStamp+=0x100;
    assert(AppleAgxG13QueueRuntimeSubmit(&f.Runtime,&s)==0);
    assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==0);
    assert(!f.Runtime.RecoveryPending && calls==2 && !f.IoState.Quiesces);
    f.TaStamp=s.Ta.ExpectedStamp; f.TaDone=f.TaWrite;
    f.D3Stamp=s.D3.ExpectedStamp; f.D3Done=f.D3Write;
    TestEvent(event,7,9);
    assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==0);
    assert(AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&c));
    assert(c.Fence==102 && c.Status==AppleAgxG13QueueCompletionSuccess);
  }
  return 0;
}
''', ['apple_agx_g13_queue_runtime.c', 'apple_agx_g13_codec.c'])

    def test_provider_preserves_channel_recovery_callback(self):
        replay(r'''
#define main existing_platform_tests
#define AppleAgxG13QueueProviderInitialize FixtureQueueInitialize
#include "apple_agx_platform_provider_test.c"
#undef AppleAgxG13QueueProviderInitialize
#undef main
APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderInitialize(
    APPLE_AGX_G13_QUEUE_PROVIDER *p, const APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *c,
    const APPLE_AGX_G13_QUEUE_RUNTIME_IO *io, const APPLE_AGX_G13_QUEUE_PROVIDER_IO *pi) {
  APPLE_AGX_BACKEND_BOOL ok=FixtureQueueInitialize(p,c,io,pi);
  p->RuntimeIo=*io; /* actual initializer's copy at the dependency boundary */
  return ok;
}
static unsigned channel_calls;
static APPLE_AGX_BACKEND_BOOL channel(void *context, unsigned fence,
    unsigned pipe, unsigned long long queue, unsigned value) {
  assert(context==g_transport && fence==101 && pipe==2 &&
         queue==0x1500030000ULL && value==0xab123400u);
  ++channel_calls; return 1;
}
int main(void) {
  APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
  APPLE_AGX_PLATFORM_PROVIDER provider={0};
  APPLE_AGX_PLATFORM_PROVIDER_CONFIG config;
  APPLE_AGX_BACKEND_RUNTIME runtime={0};
  APPLE_AGX_BACKEND_IO io;
  FAKE_TRANSPORT transport={0}; FAKE_COMPONENTS components={0};
  unsigned char storage[35][0x4000];
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER render_shared;
  static unsigned char render_storage[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT][0x10000];
  g_components=&components; g_transport=&transport;
  prepare_channel_memory(&owner,storage); prepare_render_shared_memory(&render_shared,render_storage);
  config=provider_config(&owner,&transport,&runtime,&render_shared);
  config.QueueRuntimeIo.RecoverChannel=channel;
  assert(AppleAgxPlatformProviderInitialize(&provider,&config,&io));
  assert(provider.QueueProvider.RuntimeIo.RecoverChannel);
  assert(provider.QueueProvider.RuntimeIo.RecoverChannel(
      provider.QueueProvider.RuntimeIo.Context,101,2,0x1500030000ULL,0xab123400));
  assert(channel_calls==1);
  provider.BindingReady=0;
  assert(!provider.QueueProvider.RuntimeIo.RecoverChannel(
      provider.QueueProvider.RuntimeIo.Context,101,2,0x1500030000ULL,0xab123400));
  assert(channel_calls==1);
  return 0;
}
''', ['apple_agx_platform_provider.c', 'apple_agx_event_allocator.c',
       'apple_agx_render_shared_memory.c', 'apple_agx_g13_compute_work.c',
       'apple_agx_relocation.c', 'apple_agx_render_template.generated.c',
       'apple_agx_memory.c'])

    def test_platform_channel_command_is_bounded_and_preserves_fault_cause(self):
        import re
        source = (ROOT / 'drivers/apple-agx/render-admission/src/backend_platform_windows.c').read_text()
        match = re.search(r'static APPLE_AGX_BACKEND_BOOL AdmissionTransportRecoverChannel\(.*?^}', source, re.S | re.M)
        self.assertIsNotNone(match, 'production channel recovery transport is missing')
        replay(PLATFORM_SHIM + match[0] + PLATFORM_CASES,
               ['apple_agx_device_control.c', 'apple_agx_rtkit.c'])


PLATFORM_SHIM = r'''
#define main existing_device_control_tests
#include "apple_agx_device_control_test.c"
#undef main
#define PASSIVE_LEVEL 0
#define TRUE 1
#define AppleAgxG13EventChannelError 8
#define ADMISSION_PLATFORM_DEVICE_CONTROL_STALL_US 50
/* Minimal WDK/platform boundary; real device-control publisher/receipt run. */
typedef unsigned long long ULONGLONG;
typedef struct {
  struct { struct { unsigned PendingFence; } QueueProvider; } Provider;
  struct { APPLE_AGX_CHANNEL_MEMORY_OWNER ChannelMemory; } Initdata;
  APPLE_AGX_PLATFORM_TRANSPORT_IO TransportIo;
  unsigned FirmwareRecoveries, LastFirmwareRecoveryKind, LastFirmwareRecoveryFence;
  unsigned LastFirmwareRecoveryMs, MaxFirmwareRecoveryMs, FirmwareRecoveryUnpublished;
  ULONGLONG JobSubmitMs;
} ADMISSION_PLATFORM_RUNTIME;
static unsigned platform_irql, platform_now;
static unsigned KeGetCurrentIrql(void) { return platform_irql; }
static ULONGLONG AdmissionPlatformNowMs(void) { return ++platform_now; }
static void KeStallExecutionProcessor(unsigned us) { assert(us==50); }
static void InterlockedIncrement(unsigned *p) { ++*p; }
'''
PLATFORM_CASES = r'''
int main(void) {
  ADMISSION_PLATFORM_RUNTIME r = {0};
  FAKE_TRANSPORT f = {0};
  unsigned char state[0x30]={0}, ring[0x3000]={0};
  prepare_owner(&r.Initdata.ChannelMemory,state,ring);
  f.State=state; f.Ring=ring; r.TransportIo=make_io(&f);
  r.Provider.QueueProvider.PendingFence=101;
  platform_irql=2;
  assert(!AdmissionTransportRecoverChannel(&r,101,2,0x1500030000ULL,0xab123400));
  platform_irql=0;
  assert(!AdmissionTransportRecoverChannel(&r,102,2,0x1500030000ULL,0xab123400));
  assert(!f.PublishCalls);
  /* The command is published once. Receipt timeout neither re-publishes nor
   * invents a recovery. No successful firmware execution is modeled. */
  assert(!AdmissionTransportRecoverChannel(&r,101,2,0x1500030000ULL,0xab123400));
  assert(f.PublishCalls==1 && f.DoorbellCalls==1 && platform_now<=503);
  assert(!r.FirmwareRecoveries);
  /* Next independent invocation models a new runtime/command receipt. */
  memset(state,0,sizeof(state)); f.FlushCpuCalls=0;
  f.ReceiptAfterFlushes=2; f.ReceiptValue=1; platform_now=0;
  r.LastFirmwareRecoveryFence=101; r.LastFirmwareRecoveryKind=4;
  assert(AdmissionTransportRecoverChannel(&r,101,2,0x1500030000ULL,0xab123400));
  assert(r.FirmwareRecoveries==1 && r.FirmwareRecoveryUnpublished);
  assert(r.LastFirmwareRecoveryKind==4); /* preserve triggering Timeout */
  memset(state,0,sizeof(state)); f.FlushCpuCalls=0;
  r.LastFirmwareRecoveryFence=99;
  assert(AdmissionTransportRecoverChannel(&r,101,2,0x1500030000ULL,0xab123400));
  assert(r.LastFirmwareRecoveryFence==101 && r.LastFirmwareRecoveryKind==8);
  return 0;
}
'''
