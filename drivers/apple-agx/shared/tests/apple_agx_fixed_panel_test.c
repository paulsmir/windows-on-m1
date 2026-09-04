#include "apple_agx_fixed_panel.h"

#include <assert.h>
#include <string.h>

typedef enum _FAKE_RESPONSE {
  FakeRegister,
  FakePresent,
  FakeRelease,
  FakeReleaseNotQuiesced,
  FakeNoReceipt,
} FAKE_RESPONSE;

typedef struct _FAKE_BROKER {
  APPLE_AGX_SCANOUT_U32 Version;
  APPLE_AGX_SCANOUT_U32 Capabilities;
  APPLE_AGX_SCANOUT_U32 State;
  APPLE_AGX_SCANOUT_U32 Result;
  APPLE_AGX_SCANOUT_U32 Command;
  APPLE_AGX_SCANOUT_U32 SwapId;
  APPLE_AGX_SCANOUT_U64 Receipt;
  APPLE_AGX_SCANOUT_U64 Applied;
  APPLE_AGX_SCANOUT_U64 Request;
  APPLE_AGX_SCANOUT_U64 PoolIpa;
  APPLE_AGX_SCANOUT_U64 PoolSize;
  APPLE_AGX_SCANOUT_U64 SurfaceOffset;
  APPLE_AGX_SCANOUT_U64 ActiveOffset;
  APPLE_AGX_SCANOUT_U64 NowMs;
  APPLE_AGX_SCANOUT_U32 CommandWrites;
  FAKE_RESPONSE Response;
} FAKE_BROKER;

static APPLE_AGX_SCANOUT_U32 reg(APPLE_AGX_SCANOUT_U32 Offset) {
  assert(Offset >= APPLE_AGX_SCANOUT_MMIO_OFFSET);
  return Offset - APPLE_AGX_SCANOUT_MMIO_OFFSET;
}

static APPLE_AGX_SCANOUT_BOOL read32(void *Context,
                                      APPLE_AGX_SCANOUT_U32 Offset,
                                      APPLE_AGX_SCANOUT_U32 *Value) {
  FAKE_BROKER *fake = Context;
  switch (reg(Offset)) {
  case APPLE_AGX_SCANOUT_REG_MAGIC:
    *Value = APPLE_AGX_SCANOUT_MAGIC;
    break;
  case APPLE_AGX_SCANOUT_REG_ABI_VERSION:
    *Value = fake->Version == 0u ? APPLE_AGX_SCANOUT_ABI_VERSION
                                : fake->Version;
    break;
  case APPLE_AGX_SCANOUT_REG_CAPABILITIES:
    *Value = fake->Capabilities == 0u
                 ? APPLE_AGX_SCANOUT_REQUIRED_CAPABILITIES
                 : fake->Capabilities;
    break;
  case APPLE_AGX_SCANOUT_REG_STATE:
    *Value = fake->State;
    break;
  case APPLE_AGX_SCANOUT_REG_RESULT:
    *Value = fake->Result;
    break;
  case APPLE_AGX_SCANOUT_REG_SWAP_ID:
    *Value = fake->SwapId;
    break;
  default:
    return APPLE_AGX_SCANOUT_FALSE;
  }
  return APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_SCANOUT_BOOL read64(void *Context,
                                      APPLE_AGX_SCANOUT_U32 Offset,
                                      APPLE_AGX_SCANOUT_U64 *Value) {
  FAKE_BROKER *fake = Context;
  switch (reg(Offset)) {
  case APPLE_AGX_SCANOUT_REG_RECEIPT_SEQUENCE:
    *Value = fake->Receipt;
    break;
  case APPLE_AGX_SCANOUT_REG_APPLIED_SEQUENCE:
    *Value = fake->Applied;
    break;
  case APPLE_AGX_SCANOUT_REG_ACTIVE_OFFSET:
    *Value = fake->ActiveOffset;
    break;
  default:
    return APPLE_AGX_SCANOUT_FALSE;
  }
  return APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_SCANOUT_BOOL write32(void *Context,
                                       APPLE_AGX_SCANOUT_U32 Offset,
                                       APPLE_AGX_SCANOUT_U32 Value) {
  FAKE_BROKER *fake = Context;
  if (reg(Offset) == APPLE_AGX_SCANOUT_REG_COMMAND) {
    fake->Command = Value;
    ++fake->CommandWrites;
    fake->State = Value == APPLE_AGX_SCANOUT_CMD_RELEASE
                      ? APPLE_AGX_SCANOUT_STATE_QUIESCING
                      : APPLE_AGX_SCANOUT_STATE_PENDING;
    return APPLE_AGX_SCANOUT_TRUE;
  }
  if (reg(Offset) == APPLE_AGX_SCANOUT_REG_WIDTH ||
      reg(Offset) == APPLE_AGX_SCANOUT_REG_HEIGHT ||
      reg(Offset) == APPLE_AGX_SCANOUT_REG_STRIDE ||
      reg(Offset) == APPLE_AGX_SCANOUT_REG_FORMAT)
    return APPLE_AGX_SCANOUT_TRUE;
  return APPLE_AGX_SCANOUT_FALSE;
}

static APPLE_AGX_SCANOUT_BOOL write64(void *Context,
                                       APPLE_AGX_SCANOUT_U32 Offset,
                                       APPLE_AGX_SCANOUT_U64 Value) {
  FAKE_BROKER *fake = Context;
  switch (reg(Offset)) {
  case APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE:
    fake->Request = Value;
    break;
  case APPLE_AGX_SCANOUT_REG_POOL_IPA:
    fake->PoolIpa = Value;
    break;
  case APPLE_AGX_SCANOUT_REG_POOL_SIZE:
    fake->PoolSize = Value;
    break;
  case APPLE_AGX_SCANOUT_REG_SURFACE_OFFSET:
    fake->SurfaceOffset = Value;
    break;
  case APPLE_AGX_SCANOUT_REG_SURFACE_SIZE:
    break;
  default:
    return APPLE_AGX_SCANOUT_FALSE;
  }
  return APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_SCANOUT_U64 now_ms(void *Context) {
  return ((FAKE_BROKER *)Context)->NowMs;
}

static APPLE_AGX_SCANOUT_BOOL pause_broker(void *Context) {
  FAKE_BROKER *fake = Context;
  ++fake->NowMs;
  if (fake->Response == FakeNoReceipt)
    return APPLE_AGX_SCANOUT_TRUE;
  fake->Receipt = fake->Request;
  fake->Applied = fake->Request;
  fake->Result = APPLE_AGX_SCANOUT_BROKER_OK;
  if (fake->Response == FakeRegister) {
    fake->State = APPLE_AGX_SCANOUT_STATE_READY;
  } else if (fake->Response == FakePresent) {
    fake->State = APPLE_AGX_SCANOUT_STATE_ACTIVE;
    fake->ActiveOffset = fake->SurfaceOffset;
    fake->SwapId = 7u;
  } else if (fake->Response == FakeRelease) {
    fake->State = APPLE_AGX_SCANOUT_STATE_UNREGISTERED;
  } else {
    fake->Result = APPLE_AGX_SCANOUT_BROKER_NOT_QUIESCED;
  }
  return APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_FIXED_PANEL make_panel(FAKE_BROKER *Fake) {
  APPLE_AGX_SCANOUT_IO io;
  APPLE_AGX_FIXED_PANEL panel;
  memset(&io, 0, sizeof(io));
  memset(&panel, 0, sizeof(panel));
  io.Context = Fake;
  io.NowMs = now_ms;
  io.Read32 = read32;
  io.Read64 = read64;
  io.Write32 = write32;
  io.Write64 = write64;
  io.Pause = pause_broker;
  assert(AppleAgxFixedPanelInitialize(
             &panel, &io, 0x80000000ULL, 1ULL, 8u) ==
         AppleAgxFixedPanelOk);
  return panel;
}

static void test_start_registers_exact_pool(void) {
  FAKE_BROKER fake;
  APPLE_AGX_FIXED_PANEL panel;
  memset(&fake, 0, sizeof(fake));
  panel = make_panel(&fake);
  fake.Response = FakeRegister;
  assert(AppleAgxFixedPanelStart(&panel, 20ULL) == AppleAgxFixedPanelOk);
  assert(fake.PoolIpa == 0x80000000ULL);
  assert(fake.PoolSize == 0x3800000ULL);
  assert(panel.Started == APPLE_AGX_SCANOUT_TRUE);
}

static void test_commit_accepts_only_fixed_mode_and_topology(void) {
  FAKE_BROKER fake;
  APPLE_AGX_FIXED_PANEL panel;
  memset(&fake, 0, sizeof(fake));
  panel = make_panel(&fake);
  fake.Response = FakeRegister;
  assert(AppleAgxFixedPanelStart(&panel, 20ULL) == AppleAgxFixedPanelOk);
  assert(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u,
                                  10240u, 1u) == AppleAgxFixedPanelOk);
  assert(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 1920u, 1080u,
                                  7680u, 1u) ==
         AppleAgxFixedPanelInvalidMode);
  assert(AppleAgxFixedPanelCommit(&panel, 0u, 1u, 2560u, 1600u,
                                  10240u, 1u) ==
         AppleAgxFixedPanelInvalidTopology);
}

static void test_present_translates_segment_address_without_copy(void) {
  FAKE_BROKER fake;
  APPLE_AGX_FIXED_PANEL panel;
  APPLE_AGX_SCANOUT_U32 swap = 0u;
  memset(&fake, 0, sizeof(fake));
  panel = make_panel(&fake);
  fake.Response = FakeRegister;
  assert(AppleAgxFixedPanelStart(&panel, 20ULL) == AppleAgxFixedPanelOk);
  assert(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u,
                                  10240u, 1u) == AppleAgxFixedPanelOk);
  fake.Response = FakePresent;
  assert(AppleAgxFixedPanelPresent(&panel, 2u, 0x1000000ULL,
                                   40ULL, &swap) == AppleAgxFixedPanelOk);
  assert(fake.SurfaceOffset == 0x1000000ULL);
  assert(fake.Command == APPLE_AGX_SCANOUT_CMD_PRESENT);
  assert(swap == 7u);
  assert(AppleAgxFixedPanelPresent(&panel, 2u, 0x2000000ULL,
                                   60ULL, &swap) ==
         AppleAgxFixedPanelSinglePresentOnly);
  assert(fake.SurfaceOffset == 0x1000000ULL);
}

static void test_register_timeout_retains_uncertain_ownership(void) {
  FAKE_BROKER fake;
  APPLE_AGX_FIXED_PANEL panel;
  memset(&fake, 0, sizeof(fake));
  panel = make_panel(&fake);
  fake.Response = FakeNoReceipt;
  assert(AppleAgxFixedPanelStart(&panel, 20ULL) ==
         AppleAgxFixedPanelTimeout);
  assert(panel.Ownership == AppleAgxFixedPanelOwnershipUnknown);
}

static void test_present_rejects_wrong_segment_and_tail(void) {
  FAKE_BROKER fake;
  APPLE_AGX_FIXED_PANEL panel;
  APPLE_AGX_SCANOUT_U32 swap = 0u;
  APPLE_AGX_SCANOUT_U32 writes;
  memset(&fake, 0, sizeof(fake));
  panel = make_panel(&fake);
  fake.Response = FakeRegister;
  assert(AppleAgxFixedPanelStart(&panel, 20ULL) == AppleAgxFixedPanelOk);
  assert(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u,
                                  10240u, 1u) == AppleAgxFixedPanelOk);
  writes = fake.CommandWrites;
  assert(AppleAgxFixedPanelPresent(&panel, 1u, 0ULL,
                                   40ULL, &swap) ==
         AppleAgxFixedPanelInvalidSegment);
  assert(AppleAgxFixedPanelPresent(&panel, 2u, 0x3000000ULL,
                                   40ULL, &swap) ==
         AppleAgxFixedPanelInvalidSurface);
  assert(fake.CommandWrites == writes);
}

static void test_release_retains_ownership_without_quiesce(void) {
  FAKE_BROKER fake;
  APPLE_AGX_FIXED_PANEL panel;
  memset(&fake, 0, sizeof(fake));
  panel = make_panel(&fake);
  fake.Response = FakeRegister;
  assert(AppleAgxFixedPanelStart(&panel, 20ULL) == AppleAgxFixedPanelOk);
  fake.Response = FakeReleaseNotQuiesced;
  assert(AppleAgxFixedPanelStop(&panel, 40ULL) ==
         AppleAgxFixedPanelNotQuiesced);
  assert(panel.Started == APPLE_AGX_SCANOUT_TRUE);
  fake.Response = FakeRelease;
  assert(AppleAgxFixedPanelStop(&panel, 60ULL) == AppleAgxFixedPanelOk);
  assert(panel.Started == APPLE_AGX_SCANOUT_FALSE);
}

static void test_v2_queue_present_does_not_poll_or_consume_v1_handoff(void) {
  FAKE_BROKER fake;
  APPLE_AGX_FIXED_PANEL panel;
  APPLE_AGX_SCANOUT_U64 sequence = 0ULL;
  APPLE_AGX_SCANOUT_U64 before_ms;

  memset(&fake, 0, sizeof(fake));
  fake.Version = APPLE_AGX_SCANOUT_ABI_VERSION_V2;
  fake.Capabilities = APPLE_AGX_SCANOUT_REQUIRED_CAPABILITIES |
                      APPLE_AGX_SCANOUT_CAP_REPEATED_PRESENT |
                      APPLE_AGX_SCANOUT_CAP_LATCHED_RECEIPT |
                      APPLE_AGX_SCANOUT_CAP_LATCHED_IRQ;
  panel = make_panel(&fake);
  fake.Response = FakeRegister;
  assert(AppleAgxFixedPanelStart(&panel, 20ULL) == AppleAgxFixedPanelOk);
  assert(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u,
                                  10240u, 1u) == AppleAgxFixedPanelOk);
  before_ms = fake.NowMs;
  assert(AppleAgxFixedPanelQueuePresent(&panel, 2u, 0x1000000ULL,
                                        &sequence) == AppleAgxFixedPanelOk);
  assert(sequence != 0ULL);
  assert(fake.NowMs == before_ms);
  assert(panel.PresentConsumed == APPLE_AGX_SCANOUT_FALSE);
  assert(AppleAgxFixedPanelQueuePresent(&panel, 2u, 0x2000000ULL,
                                        &sequence) ==
         AppleAgxFixedPanelPresentPending);
}

int main(void) {
  test_start_registers_exact_pool();
  test_commit_accepts_only_fixed_mode_and_topology();
  test_present_translates_segment_address_without_copy();
  test_register_timeout_retains_uncertain_ownership();
  test_present_rejects_wrong_segment_and_tail();
  test_release_retains_ownership_without_quiesce();
  test_v2_queue_present_does_not_poll_or_consume_v1_handoff();
  return 0;
}
