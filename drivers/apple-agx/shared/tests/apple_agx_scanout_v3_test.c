#include "apple_agx_fixed_panel.h"
#include "apple_agx_scanout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* S2 ABI V3 client test against a fake broker page.  Catches: a V3 access
 * (offset >= 0xa8) when the switch is off or the capability is absent (the
 * broker refuses those); negotiation after REGISTER_POOL; a commit whose
 * MMIO order differs from the broker report's steps 4-8; a refusal code
 * that is not surfaced; a second commit before the first latched. */

static unsigned int Failures;

#define CHECK(Condition)                                                      \
  do {                                                                        \
    if (!(Condition)) {                                                       \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,       \
              #Condition);                                                    \
      ++Failures;                                                             \
    }                                                                         \
  } while (0)

#define BASE APPLE_AGX_SCANOUT_MMIO_OFFSET
#define V2_CAPS                                                               \
  (APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES | APPLE_AGX_SCANOUT_CAP_RGBA8888)
#define POOL_IPA 0x10000000ULL
#define POOL_BYTES 0x3b800000ULL
#define MAX_TRACE 256u

typedef struct _ACCESS {
  unsigned int Write;
  unsigned int Width;
  unsigned int Offset; /* broker-relative */
  unsigned long long Value;
} ACCESS;

typedef struct _FAKE {
  unsigned int Caps;
  unsigned int State;
  unsigned int Result;
  unsigned int Negotiated;
  unsigned int AcceptNegotiation; /* broker takes the write */
  unsigned int ActiveLayers;
  unsigned int RefuseCode;        /* next COMMIT_LAYERS refusal, 0 = accept */
  unsigned int SwapId;
  unsigned int IrqStatus;
  unsigned long long Receipt;
  unsigned long long Applied;
  unsigned long long Latched;
  unsigned long long RequestSequence;
  unsigned long long ActiveOffset;
  unsigned long long Now;
  unsigned int Violations;        /* V3 access without the bit */
  unsigned int TraceCount;
  ACCESS Trace[MAX_TRACE];
} FAKE;

static void Log(FAKE *Fake, unsigned int Write, unsigned int Width,
                unsigned int Offset, unsigned long long Value) {
  if (Offset >= APPLE_AGX_SCANOUT_REG_NEGOTIATED &&
      (Fake->Caps & APPLE_AGX_SCANOUT_CAP_TWO_LAYERS) == 0u)
    ++Fake->Violations;
  if (Fake->TraceCount < MAX_TRACE) {
    Fake->Trace[Fake->TraceCount].Write = Write;
    Fake->Trace[Fake->TraceCount].Width = Width;
    Fake->Trace[Fake->TraceCount].Offset = Offset;
    Fake->Trace[Fake->TraceCount].Value = Value;
  }
  ++Fake->TraceCount;
}

static APPLE_AGX_SCANOUT_U64 FakeNow(void *Context) {
  return ((FAKE *)Context)->Now;
}

static APPLE_AGX_SCANOUT_BOOL FakeRead32(void *Context,
                                         APPLE_AGX_SCANOUT_U32 Offset,
                                         APPLE_AGX_SCANOUT_U32 *Value) {
  FAKE *fake = Context;
  unsigned int reg = Offset - BASE;
  switch (reg) {
  case APPLE_AGX_SCANOUT_REG_MAGIC: *Value = APPLE_AGX_SCANOUT_MAGIC; break;
  case APPLE_AGX_SCANOUT_REG_ABI_VERSION:
    *Value = APPLE_AGX_SCANOUT_ABI_VERSION_V2; break;
  case APPLE_AGX_SCANOUT_REG_CAPABILITIES: *Value = fake->Caps; break;
  case APPLE_AGX_SCANOUT_REG_STATE: *Value = fake->State; break;
  case APPLE_AGX_SCANOUT_REG_RESULT: *Value = fake->Result; break;
  case APPLE_AGX_SCANOUT_REG_SWAP_ID: *Value = fake->SwapId; break;
  case APPLE_AGX_SCANOUT_REG_IRQ_STATUS: *Value = fake->IrqStatus; break;
  case APPLE_AGX_SCANOUT_REG_NEGOTIATED: *Value = fake->Negotiated; break;
  case APPLE_AGX_SCANOUT_REG_ACTIVE_LAYERS: *Value = fake->ActiveLayers; break;
  default: *Value = 0u; break;
  }
  Log(fake, 0u, 32u, reg, *Value);
  return (fake->Caps & APPLE_AGX_SCANOUT_CAP_TWO_LAYERS) == 0u &&
                 reg >= APPLE_AGX_SCANOUT_REG_NEGOTIATED
             ? APPLE_AGX_SCANOUT_FALSE
             : APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_SCANOUT_BOOL FakeRead64(void *Context,
                                         APPLE_AGX_SCANOUT_U32 Offset,
                                         APPLE_AGX_SCANOUT_U64 *Value) {
  FAKE *fake = Context;
  unsigned int reg = Offset - BASE;
  switch (reg) {
  case APPLE_AGX_SCANOUT_REG_RECEIPT_SEQUENCE: *Value = fake->Receipt; break;
  case APPLE_AGX_SCANOUT_REG_APPLIED_SEQUENCE: *Value = fake->Applied; break;
  case APPLE_AGX_SCANOUT_REG_LATCHED_SEQUENCE: *Value = fake->Latched; break;
  case APPLE_AGX_SCANOUT_REG_ACTIVE_OFFSET: *Value = fake->ActiveOffset; break;
  default: *Value = 0ULL; break;
  }
  Log(fake, 0u, 64u, reg, *Value);
  return APPLE_AGX_SCANOUT_TRUE;
}

static void Command(FAKE *Fake, unsigned int Command) {
  unsigned long long seq = Fake->RequestSequence;
  if (Command == APPLE_AGX_SCANOUT_CMD_REGISTER_POOL) {
    Fake->Receipt = Fake->Applied = seq;
    Fake->Result = 0u;
    Fake->State = APPLE_AGX_SCANOUT_STATE_READY;
  } else if (Command == APPLE_AGX_SCANOUT_CMD_RELEASE) {
    Fake->Receipt = Fake->Applied = seq;
    Fake->Result = 0u;
    Fake->State = APPLE_AGX_SCANOUT_STATE_UNREGISTERED;
    Fake->Negotiated = 0u;
  } else if (Command == APPLE_AGX_SCANOUT_CMD_COMMIT_LAYERS) {
    if (Fake->RefuseCode == APPLE_AGX_SCANOUT_BROKER_BUSY) {
      Fake->Result = APPLE_AGX_SCANOUT_BROKER_BUSY; /* receipt untouched */
    } else if (Fake->RefuseCode != 0u) {
      Fake->Receipt = seq;
      Fake->Result = Fake->RefuseCode;
    } else {
      Fake->Result = 0u;
      Fake->State = APPLE_AGX_SCANOUT_STATE_PENDING;
    }
  }
}

static APPLE_AGX_SCANOUT_BOOL FakeWrite32(void *Context,
                                          APPLE_AGX_SCANOUT_U32 Offset,
                                          APPLE_AGX_SCANOUT_U32 Value) {
  FAKE *fake = Context;
  unsigned int reg = Offset - BASE;
  Log(fake, 1u, 32u, reg, Value);
  if ((fake->Caps & APPLE_AGX_SCANOUT_CAP_TWO_LAYERS) == 0u &&
      reg >= APPLE_AGX_SCANOUT_REG_NEGOTIATED)
    return APPLE_AGX_SCANOUT_FALSE;
  if (reg == APPLE_AGX_SCANOUT_REG_NEGOTIATED &&
      fake->State == APPLE_AGX_SCANOUT_STATE_UNREGISTERED &&
      fake->AcceptNegotiation && Value == APPLE_AGX_SCANOUT_CAP_TWO_LAYERS)
    fake->Negotiated = Value;
  if (reg == APPLE_AGX_SCANOUT_REG_IRQ_STATUS)
    fake->IrqStatus &= ~Value;
  if (reg == APPLE_AGX_SCANOUT_REG_COMMAND)
    Command(fake, Value);
  return APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_SCANOUT_BOOL FakeWrite64(void *Context,
                                          APPLE_AGX_SCANOUT_U32 Offset,
                                          APPLE_AGX_SCANOUT_U64 Value) {
  FAKE *fake = Context;
  unsigned int reg = Offset - BASE;
  Log(fake, 1u, 64u, reg, Value);
  if (reg == APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE)
    fake->RequestSequence = Value;
  return APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_SCANOUT_BOOL FakePause(void *Context) {
  (void)Context;
  return APPLE_AGX_SCANOUT_TRUE;
}

static void FakeInit(FAKE *Fake, unsigned int Caps) {
  memset(Fake, 0, sizeof(*Fake));
  Fake->Caps = Caps;
  Fake->AcceptNegotiation = 1u;
  Fake->SwapId = 7u;
  Fake->Now = 100u;
}

static APPLE_AGX_FIXED_PANEL_RESULT StartPanel(APPLE_AGX_FIXED_PANEL *Panel,
                                               FAKE *Fake,
                                               unsigned int Request) {
  APPLE_AGX_SCANOUT_IO io;
  memset(&io, 0, sizeof(io));
  io.Context = Fake;
  io.NowMs = FakeNow;
  io.Read32 = FakeRead32;
  io.Read64 = FakeRead64;
  io.Write32 = FakeWrite32;
  io.Write64 = FakeWrite64;
  io.Pause = FakePause;
  if (AppleAgxFixedPanelInitialize(Panel, &io, POOL_IPA, POOL_BYTES, 1ULL,
                                   16u) != AppleAgxFixedPanelOk)
    return AppleAgxFixedPanelInvalidArgument;
  Panel->RequestTwoLayers = Request ? APPLE_AGX_SCANOUT_TRUE
                                    : APPLE_AGX_SCANOUT_FALSE;
  return AppleAgxFixedPanelStart(Panel, 10000ULL);
}

static unsigned int FindWrite(const FAKE *Fake, unsigned int Offset) {
  unsigned int i;
  for (i = 0u; i < Fake->TraceCount && i < MAX_TRACE; ++i)
    if (Fake->Trace[i].Write && Fake->Trace[i].Offset == Offset)
      return i;
  return 0xffffffffu;
}

static unsigned int MaxOffset(const FAKE *Fake) {
  unsigned int i, max = 0u;
  for (i = 0u; i < Fake->TraceCount && i < MAX_TRACE; ++i)
    if (Fake->Trace[i].Offset > max)
      max = Fake->Trace[i].Offset;
  return max;
}

static APPLE_AGX_SCANOUT_LAYER Layer(unsigned long long Offset,
                                     unsigned int Width, unsigned int Height,
                                     unsigned int Flags) {
  APPLE_AGX_SCANOUT_LAYER layer;
  memset(&layer, 0, sizeof(layer));
  layer.SurfaceOffset = Offset;
  layer.Width = Width;
  layer.Height = Height;
  layer.Stride = Width * 4u;
  layer.SurfaceSize = (unsigned long long)layer.Stride * Height;
  layer.Format = APPLE_AGX_SCANOUT_FORMAT_BGRA8888;
  layer.SrcOrigin = 0u;
  layer.SrcSize = APPLE_AGX_SCANOUT_PACK(Width, Height);
  layer.DstOrigin = APPLE_AGX_SCANOUT_PACK(100u, 200u);
  layer.DstSize = APPLE_AGX_SCANOUT_PACK(Width, Height);
  layer.Flags = Flags;
  return layer;
}

/* Switch off, bit absent / bit present: today's traffic only. */
static void TestNoV3AccessWhenNotEffective(void) {
  APPLE_AGX_FIXED_PANEL panel;
  FAKE fake;
  APPLE_AGX_SCANOUT_LAYER layers[2];
  APPLE_AGX_SCANOUT_U64 sequence = 0ULL;
  APPLE_AGX_SCANOUT_U32 active = 0u;
  unsigned int request;
  unsigned int caps;

  layers[0] = Layer(0x10000ULL, 640u, 480u, 1u);
  layers[1] = Layer(0x1000000ULL, 2560u, 1600u, 1u);
  for (request = 0u; request < 2u; ++request) {
    for (caps = 0u; caps < 2u; ++caps) {
      if (request == 1u && caps == 1u)
        continue;
      FakeInit(&fake, V2_CAPS | (caps ? APPLE_AGX_SCANOUT_CAP_TWO_LAYERS : 0u));
      CHECK(StartPanel(&panel, &fake, request) == AppleAgxFixedPanelOk);
      CHECK(!panel.Scanout.TwoLayers);
      CHECK(!AppleAgxFixedPanelTwoLayers(&panel));
      CHECK(MaxOffset(&fake) < APPLE_AGX_SCANOUT_REG_NEGOTIATED);
      CHECK(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u, 10240u,
                                     APPLE_AGX_SCANOUT_FORMAT_BGRA8888) ==
            AppleAgxFixedPanelOk);
      CHECK(AppleAgxFixedPanelQueuePresent(&panel, 2u, 0ULL, &sequence) ==
            AppleAgxFixedPanelOk);
      fake.TraceCount = 0u;
      CHECK(AppleAgxScanoutQueueCommitLayers(&panel.Scanout, layers, 2u,
                                             &sequence) ==
            AppleAgxScanoutNotNegotiated);
      CHECK(AppleAgxScanoutReadActiveLayers(&panel.Scanout, &active) ==
            AppleAgxScanoutNotNegotiated);
      CHECK(fake.TraceCount == 0u);
      CHECK(fake.Violations == 0u);
      CHECK(MaxOffset(&fake) < APPLE_AGX_SCANOUT_REG_NEGOTIATED);
    }
  }
  /* Bit absent: an explicit negotiation call does no MMIO at all. */
  FakeInit(&fake, V2_CAPS);
  CHECK(StartPanel(&panel, &fake, 1u) == AppleAgxFixedPanelOk);
  fake.TraceCount = 0u;
  CHECK(AppleAgxScanoutNegotiateTwoLayers(&panel.Scanout) ==
        AppleAgxScanoutMissingCapabilities);
  CHECK(fake.TraceCount == 0u && fake.Violations == 0u);
}

/* Negotiation: after Qualify, before REGISTER_POOL, write then read back. */
static void TestNegotiationOrder(void) {
  APPLE_AGX_FIXED_PANEL panel;
  FAKE fake;
  unsigned int write_neg;
  unsigned int pool;

  FakeInit(&fake, V2_CAPS | APPLE_AGX_SCANOUT_CAP_TWO_LAYERS);
  CHECK(StartPanel(&panel, &fake, 1u) == AppleAgxFixedPanelOk);
  CHECK(panel.Scanout.TwoLayers && AppleAgxFixedPanelTwoLayers(&panel));
  write_neg = FindWrite(&fake, APPLE_AGX_SCANOUT_REG_NEGOTIATED);
  pool = FindWrite(&fake, APPLE_AGX_SCANOUT_REG_POOL_IPA);
  CHECK(write_neg != 0xffffffffu && pool != 0xffffffffu && write_neg < pool);
  /* Exact pair: write32 0xa8 = 0x200, then read32 0xa8. */
  CHECK(fake.Trace[write_neg].Width == 32u &&
        fake.Trace[write_neg].Value == APPLE_AGX_SCANOUT_CAP_TWO_LAYERS);
  CHECK(!fake.Trace[write_neg + 1u].Write &&
        fake.Trace[write_neg + 1u].Width == 32u &&
        fake.Trace[write_neg + 1u].Offset == APPLE_AGX_SCANOUT_REG_NEGOTIATED);
  /* Qualify's reads precede it (magic, version, caps, receipt). */
  CHECK(write_neg >= 4u && fake.Trace[write_neg - 1u].Offset ==
                               APPLE_AGX_SCANOUT_REG_RECEIPT_SEQUENCE);

  /* Ignored write (broker not UNREGISTERED / not taken): stays V2. */
  FakeInit(&fake, V2_CAPS | APPLE_AGX_SCANOUT_CAP_TWO_LAYERS);
  fake.AcceptNegotiation = 0u;
  CHECK(StartPanel(&panel, &fake, 1u) == AppleAgxFixedPanelOk);
  CHECK(!panel.Scanout.TwoLayers);

  /* Release clears the negotiated state; the next owner negotiates. */
  FakeInit(&fake, V2_CAPS | APPLE_AGX_SCANOUT_CAP_TWO_LAYERS);
  CHECK(StartPanel(&panel, &fake, 1u) == AppleAgxFixedPanelOk);
  CHECK(AppleAgxFixedPanelStop(&panel, 10000ULL) == AppleAgxFixedPanelOk);
  CHECK(!panel.Scanout.TwoLayers && fake.Negotiated == 0u);
}

static void ExpectRecord(const FAKE *Fake, unsigned int *Index,
                         unsigned int Base, const APPLE_AGX_SCANOUT_LAYER *L) {
  static const struct {
    unsigned int Field;
    unsigned int Width;
  } order[] = {
      {APPLE_AGX_SCANOUT_LAYER_SURFACE_OFFSET, 64u},
      {APPLE_AGX_SCANOUT_LAYER_SURFACE_SIZE, 64u},
      {APPLE_AGX_SCANOUT_LAYER_WIDTH, 32u},
      {APPLE_AGX_SCANOUT_LAYER_HEIGHT, 32u},
      {APPLE_AGX_SCANOUT_LAYER_STRIDE, 32u},
      {APPLE_AGX_SCANOUT_LAYER_FORMAT, 32u},
      {APPLE_AGX_SCANOUT_LAYER_SRC_ORIGIN, 32u},
      {APPLE_AGX_SCANOUT_LAYER_SRC_SIZE, 32u},
      {APPLE_AGX_SCANOUT_LAYER_DST_ORIGIN, 32u},
      {APPLE_AGX_SCANOUT_LAYER_DST_SIZE, 32u},
      {APPLE_AGX_SCANOUT_LAYER_FLAGS, 32u},
      {APPLE_AGX_SCANOUT_LAYER_RESERVED0, 32u},
      {APPLE_AGX_SCANOUT_LAYER_RESERVED1, 64u},
  };
  unsigned long long values[13];
  unsigned int i;
  values[0] = L->SurfaceOffset; values[1] = L->SurfaceSize;
  values[2] = L->Width; values[3] = L->Height; values[4] = L->Stride;
  values[5] = L->Format; values[6] = L->SrcOrigin; values[7] = L->SrcSize;
  values[8] = L->DstOrigin; values[9] = L->DstSize; values[10] = L->Flags;
  values[11] = 0ULL; values[12] = 0ULL;
  for (i = 0u; i < 13u; ++i, ++*Index) {
    const ACCESS *a = &Fake->Trace[*Index];
    if (!a->Write || a->Offset != Base + order[i].Field ||
        a->Width != order[i].Width || a->Value != values[i]) {
      fprintf(stderr, "record field %u at trace %u: w%u off 0x%x width %u "
              "value 0x%llx\n", i, *Index, a->Write, a->Offset, a->Width,
              a->Value);
      ++Failures;
    }
  }
}

static void ExpectAccess(const FAKE *Fake, unsigned int *Index,
                         unsigned int Write, unsigned int Width,
                         unsigned int Offset, unsigned long long Value,
                         int CheckValue, int Line) {
  const ACCESS *a = &Fake->Trace[*Index];
  if (a->Write != Write || a->Width != Width || a->Offset != Offset ||
      (CheckValue && a->Value != Value)) {
    fprintf(stderr, "line %d: trace %u: w%u off 0x%x width %u value 0x%llx\n",
            Line, *Index, a->Write, a->Offset, a->Width, a->Value);
    ++Failures;
  }
  ++*Index;
}

#define EXPECT(W, Width, Off, Val) \
  ExpectAccess(&fake, &index, (W), (Width), (Off), (Val), 1, __LINE__)
#define EXPECT_READ(Width, Off) \
  ExpectAccess(&fake, &index, 0u, (Width), (Off), 0ULL, 0, __LINE__)

static void TestCommitSequences(void) {
  APPLE_AGX_FIXED_PANEL panel;
  FAKE fake;
  APPLE_AGX_SCANOUT_LAYER layers[2];
  APPLE_AGX_SCANOUT_U64 sequence = 0ULL, second = 0ULL;
  APPLE_AGX_SCANOUT_U32 status = 0u, active = 0u;
  APPLE_AGX_SCANOUT_U64 latched = 0ULL;
  unsigned int index;

  FakeInit(&fake, V2_CAPS | APPLE_AGX_SCANOUT_CAP_TWO_LAYERS);
  CHECK(StartPanel(&panel, &fake, 1u) == AppleAgxFixedPanelOk);
  CHECK(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u, 10240u,
                                 APPLE_AGX_SCANOUT_FORMAT_BGRA8888) ==
        AppleAgxFixedPanelOk);
  layers[0] = Layer(0x1000000ULL, 640u, 480u,
                    APPLE_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED);
  layers[1] = Layer(0x10000ULL, 2560u, 1600u,
                    APPLE_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED);
  layers[1].DstOrigin = 0u;
  fake.TraceCount = 0u;
  CHECK(AppleAgxFixedPanelQueueCommitLayers(&panel, layers, 2u, &sequence) ==
        AppleAgxFixedPanelOk);
  CHECK(sequence == 2ULL); /* REGISTER_POOL took 1 */
  CHECK(fake.TraceCount == 13u + 13u + 3u + 2u);
  index = 0u;
  ExpectRecord(&fake, &index, APPLE_AGX_SCANOUT_REG_LAYER0, &layers[0]);
  ExpectRecord(&fake, &index, APPLE_AGX_SCANOUT_REG_LAYER1, &layers[1]);
  EXPECT(1u, 32u, APPLE_AGX_SCANOUT_REG_LAYER_COUNT, 2ULL);
  EXPECT(1u, 64u, APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE, sequence);
  EXPECT(1u, 32u, APPLE_AGX_SCANOUT_REG_COMMAND,
         APPLE_AGX_SCANOUT_CMD_COMMIT_LAYERS);
  EXPECT_READ(32u, APPLE_AGX_SCANOUT_REG_RESULT);
  EXPECT_READ(64u, APPLE_AGX_SCANOUT_REG_RECEIPT_SEQUENCE);
  CHECK(panel.Scanout.PresentPending &&
        panel.Scanout.PendingPresentSequence == sequence);

  /* One outstanding commit: BUSY before its latch, without MMIO. */
  fake.TraceCount = 0u;
  CHECK(AppleAgxFixedPanelQueueCommitLayers(&panel, layers, 1u, &second) ==
        AppleAgxFixedPanelPresentPending);
  CHECK(AppleAgxFixedPanelQueuePresent(&panel, 2u, 0ULL, &second) ==
        AppleAgxFixedPanelPresentPending);
  CHECK(fake.TraceCount == 0u);

  /* Its latch releases it through the V2 interrupt path. */
  fake.Latched = sequence;
  fake.IrqStatus = APPLE_AGX_SCANOUT_IRQ_LATCHED;
  CHECK(AppleAgxScanoutConsumeInterrupt(&panel.Scanout, &status, &latched) ==
        AppleAgxScanoutOk);
  CHECK(latched == sequence && !panel.Scanout.PresentPending);

  /* One-layer commit (the removal swap): record 0 only, count 1. */
  fake.TraceCount = 0u;
  layers[1].Flags = 0u;
  CHECK(AppleAgxFixedPanelQueueCommitLayers(&panel, &layers[1], 1u,
                                            &second) == AppleAgxFixedPanelOk);
  CHECK(fake.TraceCount == 13u + 3u + 2u);
  index = 0u;
  ExpectRecord(&fake, &index, APPLE_AGX_SCANOUT_REG_LAYER0, &layers[1]);
  EXPECT(1u, 32u, APPLE_AGX_SCANOUT_REG_LAYER_COUNT, 1ULL);
  EXPECT(1u, 64u, APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE, second);
  EXPECT(1u, 32u, APPLE_AGX_SCANOUT_REG_COMMAND,
         APPLE_AGX_SCANOUT_CMD_COMMIT_LAYERS);
  fake.Latched = second;
  fake.IrqStatus = APPLE_AGX_SCANOUT_IRQ_LATCHED;
  CHECK(AppleAgxScanoutConsumeInterrupt(&panel.Scanout, &status, &latched) ==
        AppleAgxScanoutOk);
  CHECK(!panel.Scanout.PresentPending);

  fake.ActiveLayers = APPLE_AGX_SCANOUT_ACTIVE_SLOT0 |
                      APPLE_AGX_SCANOUT_ACTIVE_SLOT1;
  fake.TraceCount = 0u;
  CHECK(AppleAgxScanoutReadActiveLayers(&panel.Scanout, &active) ==
        AppleAgxScanoutOk);
  CHECK(active == 3u && fake.TraceCount == 1u &&
        fake.Trace[0].Offset == APPLE_AGX_SCANOUT_REG_ACTIVE_LAYERS &&
        fake.Trace[0].Width == 32u && !fake.Trace[0].Write);
}

/* RQ-1: a pending commit that fails after acceptance (synchronous failure
 * or the RQ-2 latch deadline): IRQ_ERROR, RESULT != OK, RECEIPT = its
 * sequence.  Only that matching failure releases the client; a RELEASE or
 * stale error, or RESULT OK, does not. */
static void TestPendingFailureReleasesClient(void) {
  APPLE_AGX_FIXED_PANEL panel;
  FAKE fake;
  APPLE_AGX_SCANOUT_LAYER layers[2];
  APPLE_AGX_SCANOUT_U64 sequence = 0ULL, failed = 0ULL, next = 0ULL;
  APPLE_AGX_SCANOUT_U32 result = 0u, status = 0u;
  APPLE_AGX_SCANOUT_U64 latched = 0ULL;

  FakeInit(&fake, V2_CAPS | APPLE_AGX_SCANOUT_CAP_TWO_LAYERS);
  CHECK(StartPanel(&panel, &fake, 1u) == AppleAgxFixedPanelOk);
  CHECK(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u, 10240u,
                                 APPLE_AGX_SCANOUT_FORMAT_BGRA8888) ==
        AppleAgxFixedPanelOk);
  layers[0] = Layer(0x1000000ULL, 640u, 480u,
                    APPLE_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED);
  layers[1] = Layer(0x10000ULL, 2560u, 1600u,
                    APPLE_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED);
  layers[1].DstOrigin = 0u;
  CHECK(AppleAgxFixedPanelQueueCommitLayers(&panel, layers, 2u, &sequence) ==
        AppleAgxFixedPanelOk);
  /* Nothing failed yet: no release, nothing reported. */
  CHECK(AppleAgxScanoutTakePresentFailure(&panel.Scanout, &failed, &result) ==
        AppleAgxScanoutNoInterrupt);
  CHECK(panel.Scanout.PresentPending && failed == 0ULL);
  /* An error about another sequence (a RELEASE, a stale receipt). */
  fake.Receipt = sequence - 1ULL;
  fake.Result = APPLE_AGX_SCANOUT_BROKER_PRESENT_FAILED;
  CHECK(AppleAgxScanoutTakePresentFailure(&panel.Scanout, &failed, &result) ==
        AppleAgxScanoutNoInterrupt);
  CHECK(panel.Scanout.PresentPending);
  /* The receipt of this commit with RESULT OK is its acceptance, not a failure. */
  fake.Receipt = sequence;
  fake.Result = APPLE_AGX_SCANOUT_BROKER_OK;
  CHECK(AppleAgxScanoutTakePresentFailure(&panel.Scanout, &failed, &result) ==
        AppleAgxScanoutNoInterrupt);
  CHECK(panel.Scanout.PresentPending);
  /* Its failure: IRQ_ERROR consumed, then the failure releases the client. */
  fake.Result = APPLE_AGX_SCANOUT_BROKER_PRESENT_FAILED;
  fake.IrqStatus = APPLE_AGX_SCANOUT_IRQ_ERROR;
  CHECK(AppleAgxScanoutConsumeInterrupt(&panel.Scanout, &status, &latched) ==
        AppleAgxScanoutOk);
  CHECK(status == APPLE_AGX_SCANOUT_IRQ_ERROR && panel.Scanout.PresentPending);
  CHECK(AppleAgxScanoutTakePresentFailure(&panel.Scanout, &failed, &result) ==
        AppleAgxScanoutOk);
  CHECK(failed == sequence && result == APPLE_AGX_SCANOUT_BROKER_PRESENT_FAILED);
  CHECK(!panel.Scanout.PresentPending && panel.Scanout.PendingPresentSequence == 0ULL);
  CHECK(panel.Scanout.LastBrokerResult == APPLE_AGX_SCANOUT_BROKER_PRESENT_FAILED);
  /* The same records go again with a new sequence. */
  fake.Result = 0u;
  CHECK(AppleAgxFixedPanelQueueCommitLayers(&panel, layers, 2u, &next) ==
        AppleAgxFixedPanelOk);
  CHECK(next == sequence + 1ULL && panel.Scanout.PresentPending);
  /* No client: invalid argument. */
  CHECK(AppleAgxScanoutTakePresentFailure(NULL, &failed, &result) ==
        AppleAgxScanoutInvalidArgument);
}

/* Every terminal refusal code reaches the caller; BUSY is not terminal. */
static void TestRefusalsSurface(void) {
  APPLE_AGX_FIXED_PANEL panel;
  FAKE fake;
  APPLE_AGX_SCANOUT_LAYER layers[2];
  APPLE_AGX_SCANOUT_U64 sequence = 0ULL;
  unsigned int code;

  FakeInit(&fake, V2_CAPS | APPLE_AGX_SCANOUT_CAP_TWO_LAYERS);
  CHECK(StartPanel(&panel, &fake, 1u) == AppleAgxFixedPanelOk);
  CHECK(AppleAgxFixedPanelCommit(&panel, 0u, 0u, 2560u, 1600u, 10240u,
                                 APPLE_AGX_SCANOUT_FORMAT_BGRA8888) ==
        AppleAgxFixedPanelOk);
  layers[0] = Layer(0x1000000ULL, 640u, 480u, 1u);
  layers[1] = Layer(0x10000ULL, 2560u, 1600u, 1u);
  for (code = APPLE_AGX_SCANOUT_BROKER_LAYERS_NOT_NEGOTIATED;
       code <= APPLE_AGX_SCANOUT_BROKER_SLOT1_LATCHED; ++code) {
    fake.RefuseCode = code;
    CHECK(AppleAgxScanoutQueueCommitLayers(&panel.Scanout, layers, 2u,
                                           &sequence) ==
          AppleAgxScanoutBrokerRejected);
    CHECK(panel.Scanout.LastBrokerResult == code);
    CHECK(sequence == 0ULL && !panel.Scanout.PresentPending);
  }
  fake.RefuseCode = APPLE_AGX_SCANOUT_BROKER_INVALID_SURFACE;
  CHECK(AppleAgxFixedPanelQueueCommitLayers(&panel, layers, 2u, &sequence) ==
        AppleAgxFixedPanelBrokerRejected);
  fake.RefuseCode = APPLE_AGX_SCANOUT_BROKER_BUSY;
  CHECK(AppleAgxScanoutQueueCommitLayers(&panel.Scanout, layers, 2u,
                                         &sequence) == AppleAgxScanoutBusy);
  CHECK(!panel.Scanout.PresentPending);
  fake.RefuseCode = 0u;
  CHECK(AppleAgxScanoutQueueCommitLayers(&panel.Scanout, layers, 0u,
                                         &sequence) ==
        AppleAgxScanoutInvalidArgument);
  CHECK(AppleAgxScanoutQueueCommitLayers(&panel.Scanout, layers, 3u,
                                         &sequence) ==
        AppleAgxScanoutInvalidArgument);
  CHECK(fake.Violations == 0u);
}

static void TestConstantsMatchBrokerReport(void) {
  CHECK(APPLE_AGX_SCANOUT_CAP_TWO_LAYERS == 0x200u);
  CHECK(APPLE_AGX_SCANOUT_REG_NEGOTIATED == 0xa8u);
  CHECK(APPLE_AGX_SCANOUT_REG_LAYER_COUNT == 0xacu);
  CHECK(APPLE_AGX_SCANOUT_REG_LAYER0 == 0xb0u);
  CHECK(APPLE_AGX_SCANOUT_REG_LAYER1 == 0xf0u);
  CHECK(APPLE_AGX_SCANOUT_REG_LAYER1 - APPLE_AGX_SCANOUT_REG_LAYER0 ==
        APPLE_AGX_SCANOUT_V3_LAYER_RECORD_SIZE);
  CHECK(APPLE_AGX_SCANOUT_REG_ACTIVE_LAYERS == 0x130u);
  CHECK(APPLE_AGX_SCANOUT_REG_ACTIVE_OFFSET_1 == 0x138u);
  CHECK(APPLE_AGX_SCANOUT_V3_MMIO_SIZE == 0x140u);
  CHECK(APPLE_AGX_SCANOUT_CMD_COMMIT_LAYERS == 4u);
  CHECK(APPLE_AGX_SCANOUT_LAYER_RESERVED1 == 0x38u);
  CHECK(APPLE_AGX_SCANOUT_BROKER_LAYERS_NOT_NEGOTIATED == 12u);
  CHECK(APPLE_AGX_SCANOUT_BROKER_LAYER_OVERLAP == 23u);
  CHECK(APPLE_AGX_SCANOUT_BROKER_SLOT1_LATCHED == 24u);
  CHECK(APPLE_AGX_SCANOUT_PACK(100u, 200u) == (100u | (200u << 16u)));
}

int main(void) {
  TestConstantsMatchBrokerReport();
  TestNoV3AccessWhenNotEffective();
  TestNegotiationOrder();
  TestCommitSequences();
  TestPendingFailureReleasesClient();
  TestRefusalsSurface();
  if (Failures != 0u) {
    fprintf(stderr, "apple_agx_scanout_v3_test: %u failure(s)\n", Failures);
    return EXIT_FAILURE;
  }
  printf("apple_agx_scanout_v3_test: OK\n");
  return EXIT_SUCCESS;
}
