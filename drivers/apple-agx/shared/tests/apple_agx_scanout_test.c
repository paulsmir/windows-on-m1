#include "apple_agx_scanout.h"

#include <assert.h>
#include <string.h>

typedef enum _FAKE_RESPONSE {
  FakeResponseNone = 0,
  FakeResponseRegisterGood,
  FakeResponseRegisterWrongState,
  FakeResponseRegisterWrongApplied,
  FakeResponsePresentGood,
  FakeResponsePresentWrongState,
  FakeResponsePresentWrongApplied,
  FakeResponsePresentWrongOffset,
  FakeResponsePresentZeroSwap,
  FakeResponseReleaseGood,
  FakeResponseNotQuiesced,
  FakeResponseStale,
  FakeResponseWrongReceipt,
} FAKE_RESPONSE;

typedef struct _FAKE_MMIO {
  APPLE_AGX_SCANOUT_U32 Magic;
  APPLE_AGX_SCANOUT_U32 Version;
  APPLE_AGX_SCANOUT_U32 Capabilities;
  APPLE_AGX_SCANOUT_U32 State;
  APPLE_AGX_SCANOUT_U32 Result;
  APPLE_AGX_SCANOUT_U32 SwapId;
  APPLE_AGX_SCANOUT_U32 Width;
  APPLE_AGX_SCANOUT_U32 Height;
  APPLE_AGX_SCANOUT_U32 Stride;
  APPLE_AGX_SCANOUT_U32 Format;
  APPLE_AGX_SCANOUT_U32 Command;
  APPLE_AGX_SCANOUT_U32 IrqStatus;
  APPLE_AGX_SCANOUT_U32 IrqEnable;
  APPLE_AGX_SCANOUT_U64 ReceiptSequence;
  APPLE_AGX_SCANOUT_U64 AppliedSequence;
  APPLE_AGX_SCANOUT_U64 LatchedSequence;
  APPLE_AGX_SCANOUT_U64 ActiveOffset;
  APPLE_AGX_SCANOUT_U64 PoolIpa;
  APPLE_AGX_SCANOUT_U64 PoolSize;
  APPLE_AGX_SCANOUT_U64 SurfaceOffset;
  APPLE_AGX_SCANOUT_U64 SurfaceSize;
  APPLE_AGX_SCANOUT_U64 RequestSequence;
  APPLE_AGX_SCANOUT_U64 NowMs;
  APPLE_AGX_SCANOUT_U64 PauseNowMs;
  APPLE_AGX_SCANOUT_U32 Polls;
  APPLE_AGX_SCANOUT_U32 Writes;
  APPLE_AGX_SCANOUT_BOOL OverrideNowOnPause;
  APPLE_AGX_SCANOUT_BOOL OverrideNowOnSwapRead;
  FAKE_RESPONSE Response;
} FAKE_MMIO;

static APPLE_AGX_SCANOUT_U32 reg_offset(APPLE_AGX_SCANOUT_U32 Offset) {
  assert(Offset >= APPLE_AGX_SCANOUT_MMIO_OFFSET);
  assert(Offset < APPLE_AGX_SCANOUT_MMIO_OFFSET + APPLE_AGX_SCANOUT_MMIO_SIZE);
  return Offset - APPLE_AGX_SCANOUT_MMIO_OFFSET;
}

static APPLE_AGX_SCANOUT_BOOL fake_read32(void *Context,
                                          APPLE_AGX_SCANOUT_U32 Offset,
                                          APPLE_AGX_SCANOUT_U32 *Value) {
  FAKE_MMIO *fake = (FAKE_MMIO *)Context;
  switch (reg_offset(Offset)) {
  case APPLE_AGX_SCANOUT_REG_MAGIC: *Value = fake->Magic; break;
  case APPLE_AGX_SCANOUT_REG_ABI_VERSION: *Value = fake->Version; break;
  case APPLE_AGX_SCANOUT_REG_CAPABILITIES: *Value = fake->Capabilities; break;
  case APPLE_AGX_SCANOUT_REG_STATE: *Value = fake->State; break;
  case APPLE_AGX_SCANOUT_REG_RESULT: *Value = fake->Result; break;
  case APPLE_AGX_SCANOUT_REG_IRQ_STATUS: *Value = fake->IrqStatus; break;
  case APPLE_AGX_SCANOUT_REG_IRQ_ENABLE: *Value = fake->IrqEnable; break;
  case APPLE_AGX_SCANOUT_REG_SWAP_ID:
    *Value = fake->SwapId;
    if (fake->OverrideNowOnSwapRead)
      fake->NowMs = fake->PauseNowMs;
    break;
  default: return APPLE_AGX_SCANOUT_FALSE;
  }
  return APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_SCANOUT_BOOL fake_read64(void *Context,
                                          APPLE_AGX_SCANOUT_U32 Offset,
                                          APPLE_AGX_SCANOUT_U64 *Value) {
  FAKE_MMIO *fake = (FAKE_MMIO *)Context;
  switch (reg_offset(Offset)) {
  case APPLE_AGX_SCANOUT_REG_RECEIPT_SEQUENCE: *Value = fake->ReceiptSequence; break;
  case APPLE_AGX_SCANOUT_REG_APPLIED_SEQUENCE: *Value = fake->AppliedSequence; break;
  case APPLE_AGX_SCANOUT_REG_LATCHED_SEQUENCE: *Value = fake->LatchedSequence; break;
  case APPLE_AGX_SCANOUT_REG_ACTIVE_OFFSET: *Value = fake->ActiveOffset; break;
  default: return APPLE_AGX_SCANOUT_FALSE;
  }
  return APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_SCANOUT_BOOL fake_write32(void *Context,
                                           APPLE_AGX_SCANOUT_U32 Offset,
                                           APPLE_AGX_SCANOUT_U32 Value) {
  FAKE_MMIO *fake = (FAKE_MMIO *)Context;
  ++fake->Writes;
  switch (reg_offset(Offset)) {
  case APPLE_AGX_SCANOUT_REG_IRQ_STATUS:
    fake->IrqStatus &= ~(Value & APPLE_AGX_SCANOUT_IRQ_MASK);
    break;
  case APPLE_AGX_SCANOUT_REG_IRQ_ENABLE:
    fake->IrqEnable = Value & APPLE_AGX_SCANOUT_IRQ_MASK;
    break;
  case APPLE_AGX_SCANOUT_REG_WIDTH: fake->Width = Value; break;
  case APPLE_AGX_SCANOUT_REG_HEIGHT: fake->Height = Value; break;
  case APPLE_AGX_SCANOUT_REG_STRIDE: fake->Stride = Value; break;
  case APPLE_AGX_SCANOUT_REG_FORMAT: fake->Format = Value; break;
  case APPLE_AGX_SCANOUT_REG_COMMAND:
    fake->Command = Value;
    if (fake->Response == FakeResponseStale) {
      fake->Result = APPLE_AGX_SCANOUT_BROKER_STALE_SEQUENCE;
    } else {
      fake->Result = APPLE_AGX_SCANOUT_BROKER_OK;
      fake->State = Value == APPLE_AGX_SCANOUT_CMD_RELEASE
                        ? APPLE_AGX_SCANOUT_STATE_QUIESCING
                        : APPLE_AGX_SCANOUT_STATE_PENDING;
    }
    break;
  default: return APPLE_AGX_SCANOUT_FALSE;
  }
  return APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_SCANOUT_BOOL fake_write64(void *Context,
                                           APPLE_AGX_SCANOUT_U32 Offset,
                                           APPLE_AGX_SCANOUT_U64 Value) {
  FAKE_MMIO *fake = (FAKE_MMIO *)Context;
  ++fake->Writes;
  switch (reg_offset(Offset)) {
  case APPLE_AGX_SCANOUT_REG_POOL_IPA: fake->PoolIpa = Value; break;
  case APPLE_AGX_SCANOUT_REG_POOL_SIZE: fake->PoolSize = Value; break;
  case APPLE_AGX_SCANOUT_REG_SURFACE_OFFSET: fake->SurfaceOffset = Value; break;
  case APPLE_AGX_SCANOUT_REG_SURFACE_SIZE: fake->SurfaceSize = Value; break;
  case APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE: fake->RequestSequence = Value; break;
  default: return APPLE_AGX_SCANOUT_FALSE;
  }
  return APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_SCANOUT_U64 fake_now_ms(void *Context) {
  return ((FAKE_MMIO *)Context)->NowMs;
}

static APPLE_AGX_SCANOUT_BOOL fake_pause(void *Context) {
  FAKE_MMIO *fake = (FAKE_MMIO *)Context;
  ++fake->Polls;
  if (fake->OverrideNowOnPause)
    fake->NowMs = fake->PauseNowMs;
  else
    ++fake->NowMs;
  if (fake->Polls != 1u)
    return APPLE_AGX_SCANOUT_TRUE;
  switch (fake->Response) {
  case FakeResponseRegisterGood:
    fake->ReceiptSequence = fake->RequestSequence;
    fake->AppliedSequence = fake->RequestSequence;
    fake->State = APPLE_AGX_SCANOUT_STATE_READY;
    break;
  case FakeResponseRegisterWrongState:
    fake->ReceiptSequence = fake->RequestSequence;
    fake->AppliedSequence = fake->RequestSequence;
    fake->State = APPLE_AGX_SCANOUT_STATE_ACTIVE;
    break;
  case FakeResponseRegisterWrongApplied:
    fake->ReceiptSequence = fake->RequestSequence;
    fake->State = APPLE_AGX_SCANOUT_STATE_READY;
    break;
  case FakeResponsePresentGood:
    fake->ReceiptSequence = fake->RequestSequence;
    fake->AppliedSequence = fake->RequestSequence;
    fake->ActiveOffset = fake->SurfaceOffset;
    fake->SwapId = 42u;
    fake->State = APPLE_AGX_SCANOUT_STATE_ACTIVE;
    break;
  case FakeResponsePresentWrongState:
    fake->ReceiptSequence = fake->RequestSequence;
    fake->AppliedSequence = fake->RequestSequence;
    fake->ActiveOffset = fake->SurfaceOffset;
    fake->SwapId = 42u;
    fake->State = APPLE_AGX_SCANOUT_STATE_READY;
    break;
  case FakeResponsePresentWrongApplied:
    fake->ReceiptSequence = fake->RequestSequence;
    fake->ActiveOffset = fake->SurfaceOffset;
    fake->SwapId = 42u;
    fake->State = APPLE_AGX_SCANOUT_STATE_ACTIVE;
    break;
  case FakeResponsePresentWrongOffset:
    fake->ReceiptSequence = fake->RequestSequence;
    fake->AppliedSequence = fake->RequestSequence;
    fake->ActiveOffset = fake->SurfaceOffset + APPLE_AGX_SCANOUT_ALIGNMENT;
    fake->SwapId = 42u;
    fake->State = APPLE_AGX_SCANOUT_STATE_ACTIVE;
    break;
  case FakeResponsePresentZeroSwap:
    fake->ReceiptSequence = fake->RequestSequence;
    fake->AppliedSequence = fake->RequestSequence;
    fake->ActiveOffset = fake->SurfaceOffset;
    fake->SwapId = 0u;
    fake->State = APPLE_AGX_SCANOUT_STATE_ACTIVE;
    break;
  case FakeResponseReleaseGood:
    fake->ReceiptSequence = fake->RequestSequence;
    fake->AppliedSequence = fake->RequestSequence;
    fake->State = APPLE_AGX_SCANOUT_STATE_UNREGISTERED;
    break;
  case FakeResponseNotQuiesced:
    fake->ReceiptSequence = fake->RequestSequence;
    fake->Result = APPLE_AGX_SCANOUT_BROKER_NOT_QUIESCED;
    fake->State = APPLE_AGX_SCANOUT_STATE_ACTIVE;
    break;
  case FakeResponseWrongReceipt:
    fake->ReceiptSequence = fake->RequestSequence + 1ULL;
    break;
  default:
    break;
  }
  return APPLE_AGX_SCANOUT_TRUE;
}

static void fake_init(FAKE_MMIO *Fake) {
  memset(Fake, 0, sizeof(*Fake));
  Fake->Magic = APPLE_AGX_SCANOUT_MAGIC;
  Fake->Version = APPLE_AGX_SCANOUT_ABI_VERSION;
  Fake->Capabilities = APPLE_AGX_SCANOUT_REQUIRED_CAPABILITIES;
  Fake->State = APPLE_AGX_SCANOUT_STATE_UNREGISTERED;
}

static APPLE_AGX_SCANOUT_CLIENT make_client(FAKE_MMIO *Fake,
                                             APPLE_AGX_SCANOUT_U64 FirstSequence,
                                             APPLE_AGX_SCANOUT_U32 MaxPolls) {
  APPLE_AGX_SCANOUT_CLIENT client;
  APPLE_AGX_SCANOUT_IO io;
  io.Context = Fake;
  io.NowMs = fake_now_ms;
  io.Read32 = fake_read32;
  io.Read64 = fake_read64;
  io.Write32 = fake_write32;
  io.Write64 = fake_write64;
  io.Pause = fake_pause;
  assert(AppleAgxScanoutInitialize(&client, &io, FirstSequence, MaxPolls) ==
         AppleAgxScanoutOk);
  return client;
}

static void qualify(APPLE_AGX_SCANOUT_CLIENT *Client) {
  assert(AppleAgxScanoutQualify(Client) == AppleAgxScanoutOk);
}

static void test_abi_matches_m1n1_v1_header(void) {
  assert(APPLE_AGX_SCANOUT_MMIO_OFFSET == 0x400ULL);
  assert(APPLE_AGX_SCANOUT_MMIO_SIZE == 0xa0ULL);
  assert(APPLE_AGX_SCANOUT_MAGIC == 0x53584741u);
  assert(APPLE_AGX_SCANOUT_ABI_VERSION == 1u);
  assert(APPLE_AGX_SCANOUT_REG_RECEIPT_SEQUENCE == 0x20u);
  assert(APPLE_AGX_SCANOUT_REG_APPLIED_SEQUENCE == 0x28u);
  assert(APPLE_AGX_SCANOUT_REG_LATCHED_SEQUENCE == 0x30u);
  assert(APPLE_AGX_SCANOUT_REG_ACTIVE_OFFSET == 0x38u);
  assert(APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE == 0x90u);
  assert(APPLE_AGX_SCANOUT_REG_COMMAND == 0x98u);
  assert(APPLE_AGX_SCANOUT_J313_POOL_SIZE == 0x3800000ULL);
  assert(APPLE_AGX_SCANOUT_J313_SURFACE_SIZE == 0xfa0000ULL);
  assert(APPLE_AGX_SCANOUT_IRQ_MASK == 3u);
}

static void test_qualification_rejects_each_incompatible_identity(void) {
  FAKE_MMIO fake;
  APPLE_AGX_SCANOUT_CLIENT client;
  fake_init(&fake);
  client = make_client(&fake, 1ULL, 4u);
  fake.Magic = 0u;
  assert(AppleAgxScanoutQualify(&client) == AppleAgxScanoutIncompatibleMagic);
  fake.Magic = APPLE_AGX_SCANOUT_MAGIC;
  fake.Version = 3u;
  assert(AppleAgxScanoutQualify(&client) == AppleAgxScanoutIncompatibleVersion);
  fake.Version = APPLE_AGX_SCANOUT_ABI_VERSION;
  fake.Capabilities &= ~APPLE_AGX_SCANOUT_CAP_APPLIED_RECEIPT;
  assert(AppleAgxScanoutQualify(&client) == AppleAgxScanoutMissingCapabilities);
}

static void test_register_requires_terminal_receipt_ready_and_applied(void) {
  FAKE_MMIO fake;
  APPLE_AGX_SCANOUT_CLIENT client;
  fake_init(&fake);
  fake.Response = FakeResponseRegisterGood;
  client = make_client(&fake, 7ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutRegisterPool(&client, 0x81000000ULL, 100ULL) ==
         AppleAgxScanoutOk);
  assert(fake.PoolIpa == 0x81000000ULL);
  assert(fake.PoolSize == APPLE_AGX_SCANOUT_J313_POOL_SIZE);
  assert(fake.RequestSequence == 7ULL);
  assert(fake.Command == APPLE_AGX_SCANOUT_CMD_REGISTER_POOL);

  fake_init(&fake);
  fake.Response = FakeResponseRegisterWrongState;
  client = make_client(&fake, 8ULL, 2u);
  qualify(&client);
  assert(AppleAgxScanoutRegisterPool(&client, 0x82000000ULL, 100ULL) ==
         AppleAgxScanoutInconsistentReceipt);

  fake_init(&fake);
  fake.Response = FakeResponseRegisterWrongApplied;
  client = make_client(&fake, 9ULL, 2u);
  qualify(&client);
  assert(AppleAgxScanoutRegisterPool(&client, 0x83000000ULL, 100ULL) ==
         AppleAgxScanoutInconsistentReceipt);
}

static void test_present_requires_applied_active_offset_and_nonzero_swap(void) {
  FAKE_MMIO fake;
  APPLE_AGX_SCANOUT_CLIENT client;
  APPLE_AGX_SCANOUT_U32 swap_id = 0u;
  fake_init(&fake);
  fake.Response = FakeResponsePresentGood;
  client = make_client(&fake, 20ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutPresentApplied(&client, 0x10000ULL, 100ULL, &swap_id) ==
         AppleAgxScanoutOk);
  assert(swap_id == 42u);
  assert(fake.Width == 2560u && fake.Height == 1600u);
  assert(fake.Stride == 10240u && fake.Format == 1u);
  assert(fake.LatchedSequence == 0ULL);

  fake_init(&fake);
  fake.Response = FakeResponsePresentWrongState;
  client = make_client(&fake, 21ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutPresentApplied(&client, 0x20000ULL, 100ULL, &swap_id) ==
         AppleAgxScanoutInconsistentReceipt);

  fake_init(&fake);
  fake.Response = FakeResponsePresentWrongApplied;
  client = make_client(&fake, 22ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutPresentApplied(&client, 0x30000ULL, 100ULL, &swap_id) ==
         AppleAgxScanoutInconsistentReceipt);

  fake_init(&fake);
  fake.Response = FakeResponsePresentWrongOffset;
  client = make_client(&fake, 23ULL, 4u);
  qualify(&client);
  swap_id = 99u;
  assert(AppleAgxScanoutPresentApplied(&client, 0x40000ULL, 100ULL, &swap_id) ==
         AppleAgxScanoutInconsistentReceipt);
  assert(swap_id == 0u);

  fake_init(&fake);
  fake.Response = FakeResponsePresentZeroSwap;
  client = make_client(&fake, 24ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutPresentApplied(&client, 0x50000ULL, 100ULL, &swap_id) ==
         AppleAgxScanoutInconsistentReceipt);
}

static void test_release_fails_closed_on_not_quiesced(void) {
  FAKE_MMIO fake;
  APPLE_AGX_SCANOUT_CLIENT client;
  fake_init(&fake);
  fake.Response = FakeResponseNotQuiesced;
  client = make_client(&fake, 30ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutRelease(&client, 100ULL) == AppleAgxScanoutNotQuiesced);
  assert(fake.State == APPLE_AGX_SCANOUT_STATE_ACTIVE);

  fake_init(&fake);
  fake.Response = FakeResponseReleaseGood;
  client = make_client(&fake, 31ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutRelease(&client, 100ULL) == AppleAgxScanoutOk);
}

static void test_stale_receipt_timeout_and_sequence_are_fail_closed(void) {
  FAKE_MMIO fake;
  APPLE_AGX_SCANOUT_CLIENT client;
  APPLE_AGX_SCANOUT_U32 swap_id = 0u;
  fake_init(&fake);
  fake.Response = FakeResponseStale;
  client = make_client(&fake, 40ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutRegisterPool(&client, 0x84000000ULL, 100ULL) ==
         AppleAgxScanoutStaleSequence);

  fake.Response = FakeResponseNone;
  fake.Result = APPLE_AGX_SCANOUT_BROKER_OK;
  assert(AppleAgxScanoutPresentApplied(&client, 0ULL, 100ULL, &swap_id) ==
         AppleAgxScanoutPollLimit);
  assert(fake.RequestSequence == 41ULL);
  assert(fake.LatchedSequence == 0ULL);

  fake_init(&fake);
  fake.Response = FakeResponseWrongReceipt;
  client = make_client(&fake, 50ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutRelease(&client, 100ULL) ==
         AppleAgxScanoutInconsistentReceipt);

  fake_init(&fake);
  client = make_client(&fake, 60ULL, 100u);
  qualify(&client);
  assert(AppleAgxScanoutRelease(&client, 1ULL) == AppleAgxScanoutTimeout);
}

static void test_late_or_clock_regressed_terminal_receipt_is_rejected(void) {
  FAKE_MMIO fake;
  APPLE_AGX_SCANOUT_CLIENT client;

  fake_init(&fake);
  fake.Response = FakeResponseRegisterGood;
  fake.NowMs = 10ULL;
  fake.OverrideNowOnPause = APPLE_AGX_SCANOUT_TRUE;
  fake.PauseNowMs = 101ULL;
  client = make_client(&fake, 70ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutRegisterPool(&client, 0x85000000ULL, 100ULL) ==
         AppleAgxScanoutTimeout);

  fake_init(&fake);
  fake.Response = FakeResponseRegisterGood;
  fake.NowMs = 10ULL;
  fake.OverrideNowOnPause = APPLE_AGX_SCANOUT_TRUE;
  fake.PauseNowMs = 9ULL;
  client = make_client(&fake, 71ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutRegisterPool(&client, 0x86000000ULL, 100ULL) ==
         AppleAgxScanoutClockRegression);
}

static void test_present_specific_snapshot_must_also_meet_deadline(void) {
  FAKE_MMIO fake;
  APPLE_AGX_SCANOUT_CLIENT client;
  APPLE_AGX_SCANOUT_U32 swap_id = 0u;

  fake_init(&fake);
  fake.Response = FakeResponsePresentGood;
  fake.OverrideNowOnSwapRead = APPLE_AGX_SCANOUT_TRUE;
  fake.PauseNowMs = 101ULL;
  client = make_client(&fake, 80ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutPresentApplied(&client, 0x10000ULL, 100ULL, &swap_id) ==
         AppleAgxScanoutTimeout);
  assert(swap_id == 0u);

  fake_init(&fake);
  fake.Response = FakeResponsePresentGood;
  fake.NowMs = 10ULL;
  fake.OverrideNowOnSwapRead = APPLE_AGX_SCANOUT_TRUE;
  fake.PauseNowMs = 9ULL;
  client = make_client(&fake, 81ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutPresentApplied(&client, 0x20000ULL, 100ULL, &swap_id) ==
         AppleAgxScanoutClockRegression);
  assert(swap_id == 0u);
}

static void test_v2_queue_present_is_nonblocking_and_fail_closed(void) {
  FAKE_MMIO fake;
  APPLE_AGX_SCANOUT_CLIENT client;
  APPLE_AGX_SCANOUT_U64 sequence = 0ULL;
  APPLE_AGX_SCANOUT_U32 writes;

  fake_init(&fake);
  fake.Version = APPLE_AGX_SCANOUT_ABI_VERSION_V2;
  fake.Capabilities = APPLE_AGX_SCANOUT_REQUIRED_CAPABILITIES |
                      APPLE_AGX_SCANOUT_CAP_REPEATED_PRESENT |
                      APPLE_AGX_SCANOUT_CAP_LATCHED_RECEIPT |
                      APPLE_AGX_SCANOUT_CAP_LATCHED_IRQ;
  client = make_client(&fake, 90ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutQueuePresent(&client, 0x10000ULL, &sequence) ==
         AppleAgxScanoutOk);
  assert(sequence == 90ULL);
  assert(fake.RequestSequence == 90ULL);
  assert(fake.Command == APPLE_AGX_SCANOUT_CMD_PRESENT);
  assert(fake.Polls == 0u);

  writes = fake.Writes;
  sequence = 0ULL;
  assert(AppleAgxScanoutQueuePresent(&client, 0x20000ULL, &sequence) ==
         AppleAgxScanoutBusy);
  assert(sequence == 0ULL);
  assert(fake.Writes == writes);

  fake_init(&fake);
  client = make_client(&fake, 91ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutQueuePresent(&client, 0x10000ULL, &sequence) ==
         AppleAgxScanoutMissingCapabilities);
  assert(fake.Writes == 0u);
}

static void test_v2_latched_irq_is_consumed_exactly_once(void) {
  FAKE_MMIO fake;
  APPLE_AGX_SCANOUT_CLIENT client;
  APPLE_AGX_SCANOUT_U64 queued = 0ULL;
  APPLE_AGX_SCANOUT_U64 latched = 0ULL;
  APPLE_AGX_SCANOUT_U32 irq = 0u;

  fake_init(&fake);
  fake.Version = APPLE_AGX_SCANOUT_ABI_VERSION_V2;
  fake.Capabilities = APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES;
  client = make_client(&fake, 100ULL, 4u);
  qualify(&client);
  assert(AppleAgxScanoutEnableInterrupts(&client) == AppleAgxScanoutOk);
  assert(fake.IrqEnable == APPLE_AGX_SCANOUT_IRQ_MASK);
  assert(AppleAgxScanoutQueuePresent(&client, 0x10000ULL, &queued) ==
         AppleAgxScanoutOk);
  fake.LatchedSequence = queued;
  fake.IrqStatus = APPLE_AGX_SCANOUT_IRQ_LATCHED;
  assert(AppleAgxScanoutConsumeInterrupt(&client, &irq, &latched) ==
         AppleAgxScanoutOk);
  assert(irq == APPLE_AGX_SCANOUT_IRQ_LATCHED);
  assert(latched == queued);
  assert(fake.IrqStatus == 0u);
  assert(client.PresentPending == APPLE_AGX_SCANOUT_FALSE);
  assert(AppleAgxScanoutConsumeInterrupt(&client, &irq, &latched) ==
         AppleAgxScanoutNoInterrupt);
}

int main(void) {
  test_abi_matches_m1n1_v1_header();
  test_qualification_rejects_each_incompatible_identity();
  test_register_requires_terminal_receipt_ready_and_applied();
  test_present_requires_applied_active_offset_and_nonzero_swap();
  test_release_fails_closed_on_not_quiesced();
  test_stale_receipt_timeout_and_sequence_are_fail_closed();
  test_late_or_clock_regressed_terminal_receipt_is_rejected();
  test_present_specific_snapshot_must_also_meet_deadline();
  test_v2_queue_present_is_nonblocking_and_fail_closed();
  test_v2_latched_irq_is_consumed_exactly_once();
  return 0;
}
