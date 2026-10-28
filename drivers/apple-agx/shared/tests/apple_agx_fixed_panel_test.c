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
  APPLE_AGX_SCANOUT_U32 Format;
  APPLE_AGX_SCANOUT_U32 Width;
  APPLE_AGX_SCANOUT_U32 Height;
  APPLE_AGX_SCANOUT_U32 Stride;
  APPLE_AGX_SCANOUT_U64 SurfaceSize;
  APPLE_AGX_SCANOUT_U32 DestOrigin;
  APPLE_AGX_SCANOUT_U32 DestSize;
  APPLE_AGX_SCANOUT_U32 DestWrites;
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
  if (reg(Offset) == APPLE_AGX_SCANOUT_REG_FORMAT) {
    fake->Format = Value;
    return APPLE_AGX_SCANOUT_TRUE;
  }
  switch (reg(Offset)) {
  case APPLE_AGX_SCANOUT_REG_WIDTH:
    fake->Width = Value;
    return APPLE_AGX_SCANOUT_TRUE;
  case APPLE_AGX_SCANOUT_REG_HEIGHT:
    fake->Height = Value;
    return APPLE_AGX_SCANOUT_TRUE;
  case APPLE_AGX_SCANOUT_REG_STRIDE:
    fake->Stride = Value;
    return APPLE_AGX_SCANOUT_TRUE;
  case APPLE_AGX_SCANOUT_REG_DEST_ORIGIN:
  case APPLE_AGX_SCANOUT_REG_DEST_SIZE:
    /* m1n1 0d3a75b3 traps only 0x400..0x49f; these exist only with the
     * scaled-source capability. */
    if ((fake->Capabilities & APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE) == 0u)
      return APPLE_AGX_SCANOUT_FALSE;
    ++fake->DestWrites;
    if (reg(Offset) == APPLE_AGX_SCANOUT_REG_DEST_ORIGIN)
      fake->DestOrigin = Value;
    else
      fake->DestSize = Value;
    return APPLE_AGX_SCANOUT_TRUE;
  default:
    return APPLE_AGX_SCANOUT_FALSE;
  }
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
    fake->SurfaceSize = Value;
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
             &panel, &io, 0x80000000ULL, 0x3b800000ULL, 1ULL, 8u) ==
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
  /* EXP953: VidMm may place a primary anywhere in the local allocation
   * range, so the registered scanout pool must cover all of it. */
  assert(fake.PoolSize == 0x3b800000ULL);
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

/* M2-f2c: the pool is the receipt-sized segment 2; a smaller reserve
 * registers its own size and bounds every surface by it. */
static void test_pool_follows_the_reserve_size(void) {
  FAKE_BROKER fake;
  APPLE_AGX_SCANOUT_IO io;
  APPLE_AGX_FIXED_PANEL panel;
  APPLE_AGX_SCANOUT_U32 swap = 0u;
  memset(&fake, 0, sizeof(fake));
  memset(&io, 0, sizeof(io));
  io.Context = &fake;
  io.NowMs = now_ms;
  io.Read32 = read32;
  io.Read64 = read64;
  io.Write32 = write32;
  io.Write64 = write64;
  io.Pause = pause_broker;
  /* Above the J313 maximum, unaligned or below one surface: refused. */
  assert(AppleAgxFixedPanelInitialize(&panel, &io, 0x80000000ULL,
                                      0x3b810000ULL, 1ULL, 8u) ==
         AppleAgxFixedPanelInvalidArgument);
  assert(AppleAgxFixedPanelInitialize(&panel, &io, 0x80000000ULL,
                                      0x13804000ULL, 1ULL, 8u) ==
         AppleAgxFixedPanelInvalidArgument);
  assert(AppleAgxFixedPanelInitialize(&panel, &io, 0x80000000ULL,
                                      0x800000ULL, 1ULL, 8u) ==
         AppleAgxFixedPanelInvalidArgument);
  /* A 384 MiB reserve: segment 2 is 312 MiB. */
  assert(AppleAgxFixedPanelInitialize(&panel, &io, 0x80000000ULL,
                                      0x13800000ULL, 1ULL, 8u) ==
         AppleAgxFixedPanelOk);
  fake.Response = FakeRegister;
  assert(AppleAgxFixedPanelStart(&panel, 20ULL) == AppleAgxFixedPanelOk);
  assert(fake.PoolSize == 0x13800000ULL);
  assert(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u,
                                  10240u, 1u) == AppleAgxFixedPanelOk);
  fake.Response = FakePresent;
  /* 0x12860000 + one surface ends inside the pool; the next aligned
   * offset would end past it. */
  assert(AppleAgxFixedPanelPresent(&panel, 2u, 0x12870000ULL, 40ULL, &swap) ==
         AppleAgxFixedPanelInvalidSurface);
  assert(AppleAgxFixedPanelPresent(&panel, 2u, 0x12860000ULL, 40ULL, &swap) ==
         AppleAgxFixedPanelOk);
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
  assert(AppleAgxFixedPanelPresent(&panel, 2u, 0x3a870000ULL,
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

static APPLE_AGX_FIXED_PANEL make_v2_panel(FAKE_BROKER *fake) {
  APPLE_AGX_FIXED_PANEL panel;
  memset(fake, 0, sizeof(*fake));
  fake->Version = APPLE_AGX_SCANOUT_ABI_VERSION_V2;
  fake->Capabilities = APPLE_AGX_SCANOUT_REQUIRED_CAPABILITIES |
                       APPLE_AGX_SCANOUT_CAP_REPEATED_PRESENT |
                       APPLE_AGX_SCANOUT_CAP_LATCHED_RECEIPT |
                       APPLE_AGX_SCANOUT_CAP_LATCHED_IRQ;
  panel = make_panel(fake);
  fake->Response = FakeRegister;
  assert(AppleAgxFixedPanelStart(&panel, 20ULL) == AppleAgxFixedPanelOk);
  assert(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u,
                                  10240u, 1u) == AppleAgxFixedPanelOk);
  return panel;
}

/* EXP953: the legacy DWM primary was placed at local offset 0x3670000,
 * beyond the former 56 MiB pool, and must be scanout-addressable. */
static void test_queue_present_accepts_primary_beyond_former_window(void) {
  FAKE_BROKER fake;
  APPLE_AGX_FIXED_PANEL panel = make_v2_panel(&fake);
  APPLE_AGX_SCANOUT_U64 sequence = 0ULL;
  assert(AppleAgxFixedPanelQueuePresent(&panel, 2u, 0x3670000ULL,
                                        &sequence) == AppleAgxFixedPanelOk);
  assert(sequence != 0ULL);
  assert(fake.SurfaceOffset == 0x3670000ULL);
}

/* WDDM mode set: SetVidPnSourceVisibility(FALSE), SetVidPnSourceAddress,
 * SetVidPnSourceVisibility(TRUE).  The address is accepted while hidden and
 * handed back exactly once when the source becomes visible. */
static void test_queue_present_while_hidden_is_deferred_until_visible(void) {
  FAKE_BROKER fake;
  APPLE_AGX_FIXED_PANEL panel = make_v2_panel(&fake);
  APPLE_AGX_SCANOUT_U64 sequence = 7ULL;
  APPLE_AGX_SCANOUT_U64 offset = 0ULL;
  APPLE_AGX_SCANOUT_U32 writes;

  assert(AppleAgxFixedPanelSetVisible(&panel, 0u, APPLE_AGX_SCANOUT_FALSE) ==
         AppleAgxFixedPanelOk);
  writes = fake.CommandWrites;
  assert(AppleAgxFixedPanelQueuePresent(&panel, 2u, 0x3a860000ULL + 0x10000ULL,
                                        &sequence) ==
         AppleAgxFixedPanelInvalidSurface);
  assert(AppleAgxFixedPanelQueuePresent(&panel, 2u, 0x3670000ULL,
                                        &sequence) ==
         AppleAgxFixedPanelDeferred);
  assert(sequence == 0ULL);
  assert(fake.CommandWrites == writes);
  assert(AppleAgxFixedPanelTakeDeferred(&panel, &offset) ==
         APPLE_AGX_SCANOUT_FALSE);
  assert(AppleAgxFixedPanelSetVisible(&panel, 0u, APPLE_AGX_SCANOUT_TRUE) ==
         AppleAgxFixedPanelOk);
  assert(AppleAgxFixedPanelTakeDeferred(&panel, &offset) ==
         APPLE_AGX_SCANOUT_TRUE);
  assert(offset == 0x3670000ULL);
  assert(AppleAgxFixedPanelTakeDeferred(&panel, &offset) ==
         APPLE_AGX_SCANOUT_FALSE);
  assert(AppleAgxFixedPanelQueuePresent(&panel, 2u, offset, &sequence) ==
         AppleAgxFixedPanelOk);
  assert(fake.SurfaceOffset == 0x3670000ULL);
}

/* D3D-FS1: an RGBA8888 mode needs the broker's RGBA capability, and each
 * present then carries the committed format; BGRA stays the default. */
static void test_rgba_commit_needs_capability_and_sets_present_format(void) {
  FAKE_BROKER fake;
  APPLE_AGX_FIXED_PANEL panel;
  APPLE_AGX_SCANOUT_U32 swap = 0u;
  memset(&fake, 0, sizeof(fake));
  panel = make_panel(&fake);
  fake.Response = FakeRegister;
  assert(AppleAgxFixedPanelStart(&panel, 20ULL) == AppleAgxFixedPanelOk);
  assert(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u, 10240u,
                                  APPLE_AGX_SCANOUT_FORMAT_RGBA8888) ==
         AppleAgxFixedPanelInvalidMode);
  assert(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u, 10240u,
                                  3u) == AppleAgxFixedPanelInvalidMode);

  memset(&fake, 0, sizeof(fake));
  fake.Capabilities = APPLE_AGX_SCANOUT_REQUIRED_CAPABILITIES |
                      APPLE_AGX_SCANOUT_CAP_RGBA8888;
  panel = make_panel(&fake);
  fake.Response = FakeRegister;
  assert(AppleAgxFixedPanelStart(&panel, 20ULL) == AppleAgxFixedPanelOk);
  assert(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u, 10240u,
                                  APPLE_AGX_SCANOUT_FORMAT_RGBA8888) ==
         AppleAgxFixedPanelOk);
  fake.Response = FakePresent;
  assert(AppleAgxFixedPanelPresent(&panel, 2u, 0x1000000ULL,
                                   40ULL, &swap) == AppleAgxFixedPanelOk);
  assert(fake.Format == APPLE_AGX_SCANOUT_FORMAT_RGBA8888);
  assert(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u, 10240u,
                                  APPLE_AGX_SCANOUT_FORMAT_BGRA8888) ==
         AppleAgxFixedPanelOk);
  assert(panel.Scanout.PresentFormat == APPLE_AGX_SCANOUT_FORMAT_BGRA8888);
}

static APPLE_AGX_FIXED_PANEL make_scaled_panel(FAKE_BROKER *fake) {
  APPLE_AGX_FIXED_PANEL panel;
  memset(fake, 0, sizeof(*fake));
  fake->Version = APPLE_AGX_SCANOUT_ABI_VERSION_V2;
  fake->Capabilities = APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES |
                       APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE;
  panel = make_panel(fake);
  fake->Response = FakeRegister;
  assert(AppleAgxFixedPanelStart(&panel, 20ULL) == AppleAgxFixedPanelOk);
  return panel;
}

/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?  m1n1 0d3a75b3 rejects
 * every PRESENT whose geometry is not 2560x1600/10240 (surface_valid) and
 * traps no register past 0x49f.  Without CAP_SCALED_SOURCE the guest must
 * offer only the native mode, refuse a smaller commit, and keep the exact
 * native register traffic; otherwise a mode change would black the panel. */
static void test_scaled_modes_need_the_scaled_source_capability(void) {
  FAKE_BROKER fake;
  APPLE_AGX_FIXED_PANEL panel = make_v2_panel(&fake);
  APPLE_AGX_FIXED_PANEL_SOURCE_MODE mode;
  APPLE_AGX_SCANOUT_U64 sequence = 0ULL;

  assert(AppleAgxFixedPanelScaledSourceSupported(&panel) ==
         APPLE_AGX_SCANOUT_FALSE);
  assert(AppleAgxFixedPanelSourceModeCount(APPLE_AGX_SCANOUT_FALSE) == 1u);
  assert(AppleAgxFixedPanelSourceModeAt(0u, APPLE_AGX_SCANOUT_FALSE, &mode));
  assert(mode.Width == 2560u && mode.Height == 1600u &&
         mode.Stride == 10240u &&
         mode.SurfaceBytes == APPLE_AGX_SCANOUT_J313_SURFACE_SIZE);
  assert(!AppleAgxFixedPanelSourceModeAt(1u, APPLE_AGX_SCANOUT_FALSE, &mode));
  assert(AppleAgxFixedPanelScalingSupport(1920u, 1080u,
                                          APPLE_AGX_SCANOUT_FALSE) == 0u);
  assert(AppleAgxFixedPanelCommitMode(
             &panel, 0u, 0u, 1920u, 1080u, 7680u,
             APPLE_AGX_SCANOUT_FORMAT_BGRA8888,
             AppleAgxFixedPanelScalingAspectRatioCenteredMax) ==
         AppleAgxFixedPanelInvalidMode);
  assert(AppleAgxFixedPanelQueuePresent(&panel, 2u, 0x1000000ULL,
                                        &sequence) == AppleAgxFixedPanelOk);
  assert(fake.Width == 2560u && fake.Height == 1600u &&
         fake.Stride == 10240u &&
         fake.SurfaceSize == APPLE_AGX_SCANOUT_J313_SURFACE_SIZE);
  assert(fake.DestWrites == 0u);
}

/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?  With the capability a
 * 1920x1080 commit must reach the broker as its own geometry plus the
 * aspect-preserving 2560x1440 rectangle at y=80; sending native geometry
 * would make DCP read 16 MB from a 8 MB primary. */
static void test_scaled_commit_carries_source_geometry_and_destination(void) {
  FAKE_BROKER fake;
  APPLE_AGX_FIXED_PANEL panel = make_scaled_panel(&fake);
  APPLE_AGX_SCANOUT_U64 sequence = 0ULL;

  assert(AppleAgxFixedPanelScaledSourceSupported(&panel));
  assert(AppleAgxFixedPanelCommitMode(
             &panel, 0u, 0u, 1920u, 1080u, 7680u,
             APPLE_AGX_SCANOUT_FORMAT_BGRA8888,
             AppleAgxFixedPanelScalingIdentity) ==
         AppleAgxFixedPanelInvalidMode);
  assert(AppleAgxFixedPanelCommitMode(
             &panel, 0u, 0u, 1920u, 1080u, 7936u,
             APPLE_AGX_SCANOUT_FORMAT_BGRA8888,
             AppleAgxFixedPanelScalingStretched) ==
         AppleAgxFixedPanelInvalidMode);
  assert(AppleAgxFixedPanelCommitMode(
             &panel, 0u, 0u, 1366u, 768u, 5464u,
             APPLE_AGX_SCANOUT_FORMAT_BGRA8888,
             AppleAgxFixedPanelScalingStretched) ==
         AppleAgxFixedPanelInvalidMode);
  assert(AppleAgxFixedPanelCommitMode(
             &panel, 0u, 0u, 1920u, 1080u, 7680u,
             APPLE_AGX_SCANOUT_FORMAT_BGRA8888,
             AppleAgxFixedPanelScalingAspectRatioCenteredMax) ==
         AppleAgxFixedPanelOk);
  assert(AppleAgxFixedPanelQueuePresent(&panel, 2u, 0x1000000ULL,
                                        &sequence) == AppleAgxFixedPanelOk);
  assert(fake.Width == 1920u && fake.Height == 1080u &&
         fake.Stride == 7680u && fake.SurfaceSize == 7680ULL * 1080u);
  assert(fake.DestWrites == 2u);
  assert(fake.DestOrigin == (0u | (80u << 16u)));
  assert(fake.DestSize == (2560u | (1440u << 16u)));

  /* Out-of-range scaling values (D3DKMDT unpinned/custom) are refused. */
  assert(AppleAgxFixedPanelCommitMode(
             &panel, 0u, 0u, 1920u, 1080u, 7680u,
             APPLE_AGX_SCANOUT_FORMAT_BGRA8888,
             (APPLE_AGX_FIXED_PANEL_SCALING)254) ==
         AppleAgxFixedPanelInvalidMode);
  assert(AppleAgxFixedPanelCommitMode(
             &panel, 0u, 0u, 1920u, 1080u, 7680u,
             APPLE_AGX_SCANOUT_FORMAT_BGRA8888,
             (APPLE_AGX_FIXED_PANEL_SCALING)5) ==
         AppleAgxFixedPanelInvalidMode);
  /* Native identity on the same broker: full-panel rectangle. */
  assert(AppleAgxFixedPanelCommitMode(
             &panel, 0u, 0u, 2560u, 1600u, 10240u,
             APPLE_AGX_SCANOUT_FORMAT_BGRA8888,
             AppleAgxFixedPanelScalingStretched) ==
         AppleAgxFixedPanelInvalidMode);
  assert(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u, 10240u,
                                  APPLE_AGX_SCANOUT_FORMAT_BGRA8888) ==
         AppleAgxFixedPanelOk);
  assert(panel.Scanout.PresentSurfaceBytes ==
         APPLE_AGX_SCANOUT_J313_SURFACE_SIZE);
  assert(panel.Scanout.PresentDestOrigin == 0u);
  assert(panel.Scanout.PresentDestSize == (2560u | (1600u << 16u)));
}

/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?  The surface bound is the
 * committed surface, not a native one: a 1280x720 primary VidMm placed in
 * the last 4 MiB of the pool is valid, a native primary there is not. */
static void test_surface_bound_follows_the_committed_mode(void) {
  FAKE_BROKER fake;
  APPLE_AGX_FIXED_PANEL panel = make_scaled_panel(&fake);
  APPLE_AGX_SCANOUT_U64 sequence = 0ULL;
  const APPLE_AGX_SCANOUT_U64 tail = 0x3b800000ULL - 0x400000ULL;

  assert(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u, 10240u,
                                  APPLE_AGX_SCANOUT_FORMAT_BGRA8888) ==
         AppleAgxFixedPanelOk);
  assert(AppleAgxFixedPanelQueuePresent(&panel, 2u, tail, &sequence) ==
         AppleAgxFixedPanelInvalidSurface);
  assert(AppleAgxFixedPanelCommitMode(
             &panel, 0u, 0u, 1280u, 720u, 5120u,
             APPLE_AGX_SCANOUT_FORMAT_BGRA8888,
             AppleAgxFixedPanelScalingCentered) == AppleAgxFixedPanelOk);
  assert(AppleAgxFixedPanelQueuePresent(&panel, 2u, tail, &sequence) ==
         AppleAgxFixedPanelOk);
  assert(fake.SurfaceOffset == tail && fake.SurfaceSize == 5120ULL * 720u);
  assert(fake.DestOrigin == (640u | (440u << 16u)));
  assert(fake.DestSize == (1280u | (720u << 16u)));
}

/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?  Every offered source
 * mode must be scanout-legal (DCP stride & 63 == 0, inside the panel,
 * unique) and the D3DKMDT scaling rules must hold: identity only at equal
 * size, centered/stretched/aspect only below it. */
static void test_mode_table_and_destination_rules(void) {
  APPLE_AGX_FIXED_PANEL_SOURCE_MODE mode;
  APPLE_AGX_FIXED_PANEL_SOURCE_MODE other;
  APPLE_AGX_FIXED_PANEL_RECT rect;
  APPLE_AGX_SCANOUT_U32 count =
      AppleAgxFixedPanelSourceModeCount(APPLE_AGX_SCANOUT_TRUE);
  APPLE_AGX_SCANOUT_U32 i, j;
  const APPLE_AGX_SCANOUT_U32 scaled_bits =
      APPLE_AGX_FIXED_PANEL_SCALING_BIT(AppleAgxFixedPanelScalingCentered) |
      APPLE_AGX_FIXED_PANEL_SCALING_BIT(AppleAgxFixedPanelScalingStretched) |
      APPLE_AGX_FIXED_PANEL_SCALING_BIT(
          AppleAgxFixedPanelScalingAspectRatioCenteredMax);

  assert(count >= 8u);
  for (i = 0u; i < count; ++i) {
    assert(AppleAgxFixedPanelSourceModeAt(i, APPLE_AGX_SCANOUT_TRUE, &mode));
    assert(mode.Width <= 2560u && mode.Height <= 1600u);
    assert(mode.Stride == mode.Width * 4u && (mode.Stride & 63u) == 0u);
    assert(mode.SurfaceBytes == (APPLE_AGX_SCANOUT_U64)mode.Stride *
                                    mode.Height);
    assert(AppleAgxFixedPanelSourceModeFind(mode.Width, mode.Height,
                                            mode.Stride,
                                            APPLE_AGX_SCANOUT_TRUE, &other));
    for (j = 0u; j < i; ++j) {
      assert(AppleAgxFixedPanelSourceModeAt(j, APPLE_AGX_SCANOUT_TRUE,
                                            &other));
      assert(other.Width != mode.Width || other.Height != mode.Height);
    }
    if (i == 0u) {
      assert(mode.Width == 2560u && mode.Height == 1600u);
      assert(AppleAgxFixedPanelScalingSupport(mode.Width, mode.Height,
                                              APPLE_AGX_SCANOUT_TRUE) ==
             APPLE_AGX_FIXED_PANEL_SCALING_BIT(
                 AppleAgxFixedPanelScalingIdentity));
    } else {
      assert(AppleAgxFixedPanelScalingSupport(mode.Width, mode.Height,
                                              APPLE_AGX_SCANOUT_TRUE) ==
             scaled_bits);
    }
    assert(AppleAgxFixedPanelDestination(
        mode.Width, mode.Height,
        AppleAgxFixedPanelScalingAspectRatioCenteredMax, &rect));
    assert(rect.X + rect.Width <= 2560u && rect.Y + rect.Height <= 1600u);
    assert(rect.Width == 2560u || rect.Height == 1600u);
  }
  assert(AppleAgxFixedPanelSourceModeFind(1920u, 1080u, 7680u,
                                          APPLE_AGX_SCANOUT_TRUE, &mode));
  assert(!AppleAgxFixedPanelSourceModeFind(1920u, 1080u, 7680u,
                                           APPLE_AGX_SCANOUT_FALSE, &mode));
  /* 4:3 pillarbox: 1024x768 -> 2133x1600 at x=213. */
  assert(AppleAgxFixedPanelDestination(
      1024u, 768u, AppleAgxFixedPanelScalingAspectRatioCenteredMax, &rect));
  assert(rect.X == 213u && rect.Y == 0u && rect.Width == 2133u &&
         rect.Height == 1600u);
  /* 16:10 sources fill the panel. */
  assert(AppleAgxFixedPanelDestination(
      1680u, 1050u, AppleAgxFixedPanelScalingAspectRatioCenteredMax, &rect));
  assert(rect.X == 0u && rect.Y == 0u && rect.Width == 2560u &&
         rect.Height == 1600u);
  assert(AppleAgxFixedPanelDestination(
      800u, 600u, AppleAgxFixedPanelScalingStretched, &rect));
  assert(rect.X == 0u && rect.Y == 0u && rect.Width == 2560u &&
         rect.Height == 1600u);
  assert(!AppleAgxFixedPanelDestination(
      1920u, 1080u, AppleAgxFixedPanelScalingIdentity, &rect));
  assert(!AppleAgxFixedPanelDestination(
      2561u, 1600u, AppleAgxFixedPanelScalingCentered, &rect));
  assert(!AppleAgxFixedPanelDestination(
      1920u, 1080u, (APPLE_AGX_FIXED_PANEL_SCALING)5, &rect));
}

/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?  A commit whose
 * scaling is unpinned or "not specified" must still get a legal transform
 * (identity at native size, aspect-preserving below it); a native commit
 * keeps accepting every geometric scaling as identity (pre-rc106 behaviour);
 * a pinned identity on a smaller source or a custom scaling is refused. */
static void test_resolve_scaling_defaults_and_refusals(void) {
  APPLE_AGX_FIXED_PANEL_SCALING scaling = AppleAgxFixedPanelScalingCentered;
  const APPLE_AGX_SCANOUT_BOOL on = APPLE_AGX_SCANOUT_TRUE;
  const APPLE_AGX_SCANOUT_U32 defaults[] = {0u, 254u, 255u};
  unsigned i;
  for (i = 0u; i < sizeof(defaults) / sizeof(defaults[0]); ++i) {
    assert(AppleAgxFixedPanelResolveScaling(2560u, 1600u, on, defaults[i],
                                            &scaling));
    assert(scaling == AppleAgxFixedPanelScalingIdentity);
    assert(AppleAgxFixedPanelResolveScaling(1920u, 1080u, on, defaults[i],
                                            &scaling));
    assert(scaling == AppleAgxFixedPanelScalingAspectRatioCenteredMax);
    assert(!AppleAgxFixedPanelResolveScaling(
        1920u, 1080u, APPLE_AGX_SCANOUT_FALSE, defaults[i], &scaling));
  }
  assert(AppleAgxFixedPanelResolveScaling(1920u, 1080u, on, 3u, &scaling));
  assert(scaling == AppleAgxFixedPanelScalingStretched);
  assert(!AppleAgxFixedPanelResolveScaling(1920u, 1080u, on, 1u, &scaling));
  for (i = 1u; i <= 4u; ++i) {
    scaling = AppleAgxFixedPanelScalingCentered;
    assert(AppleAgxFixedPanelResolveScaling(2560u, 1600u, on, i, &scaling));
    assert(scaling == AppleAgxFixedPanelScalingIdentity);
    scaling = AppleAgxFixedPanelScalingCentered;
    assert(AppleAgxFixedPanelResolveScaling(
        2560u, 1600u, APPLE_AGX_SCANOUT_FALSE, i, &scaling));
    assert(scaling == AppleAgxFixedPanelScalingIdentity);
  }
  assert(!AppleAgxFixedPanelResolveScaling(2560u, 1600u, on, 5u, &scaling));
  assert(!AppleAgxFixedPanelResolveScaling(1920u, 1080u, on, 5u, &scaling));
  assert(!AppleAgxFixedPanelResolveScaling(1366u, 768u, on, 255u, &scaling));
}

int main(void) {
  test_start_registers_exact_pool();
  test_commit_accepts_only_fixed_mode_and_topology();
  test_present_translates_segment_address_without_copy();
  test_register_timeout_retains_uncertain_ownership();
  test_pool_follows_the_reserve_size();
  test_present_rejects_wrong_segment_and_tail();
  test_release_retains_ownership_without_quiesce();
  test_v2_queue_present_does_not_poll_or_consume_v1_handoff();
  test_queue_present_accepts_primary_beyond_former_window();
  test_queue_present_while_hidden_is_deferred_until_visible();
  test_rgba_commit_needs_capability_and_sets_present_format();
  test_scaled_modes_need_the_scaled_source_capability();
  test_scaled_commit_carries_source_geometry_and_destination();
  test_surface_bound_follows_the_committed_mode();
  test_mode_table_and_destination_rules();
  test_resolve_scaling_defaults_and_refusals();
  return 0;
}
