#include "apple_agx_scanout.h"

#define APPLE_AGX_SCANOUT_NULL ((void *)0)
#define APPLE_AGX_SCANOUT_U64_MAX (~0ULL)

static APPLE_AGX_SCANOUT_U32 AppleAgxScanoutAddress(
    APPLE_AGX_SCANOUT_U32 Register) {
  return APPLE_AGX_SCANOUT_MMIO_OFFSET + Register;
}

static APPLE_AGX_SCANOUT_BOOL AppleAgxScanoutRead32(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U32 Register,
    APPLE_AGX_SCANOUT_U32 *Value) {
  return Client->Io.Read32(Client->Io.Context,
                           AppleAgxScanoutAddress(Register), Value);
}

static APPLE_AGX_SCANOUT_BOOL AppleAgxScanoutRead64(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U32 Register,
    APPLE_AGX_SCANOUT_U64 *Value) {
  return Client->Io.Read64(Client->Io.Context,
                           AppleAgxScanoutAddress(Register), Value);
}

static APPLE_AGX_SCANOUT_BOOL AppleAgxScanoutWrite32(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U32 Register,
    APPLE_AGX_SCANOUT_U32 Value) {
  return Client->Io.Write32(Client->Io.Context,
                            AppleAgxScanoutAddress(Register), Value);
}

static APPLE_AGX_SCANOUT_BOOL AppleAgxScanoutWrite64(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U32 Register,
    APPLE_AGX_SCANOUT_U64 Value) {
  return Client->Io.Write64(Client->Io.Context,
                            AppleAgxScanoutAddress(Register), Value);
}

static APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutTakeSequence(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U64 *Sequence) {
  if (Client->NextSequence == 0ULL)
    return AppleAgxScanoutSequenceExhausted;
  *Sequence = Client->NextSequence;
  if (Client->NextSequence == APPLE_AGX_SCANOUT_U64_MAX)
    Client->NextSequence = 0ULL;
  else
    ++Client->NextSequence;
  return AppleAgxScanoutOk;
}

static APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutMapBrokerResult(
    APPLE_AGX_SCANOUT_U32 Result) {
  if (Result == APPLE_AGX_SCANOUT_BROKER_STALE_SEQUENCE)
    return AppleAgxScanoutStaleSequence;
  if (Result == APPLE_AGX_SCANOUT_BROKER_NOT_QUIESCED)
    return AppleAgxScanoutNotQuiesced;
  return AppleAgxScanoutBrokerRejected;
}

typedef enum _APPLE_AGX_SCANOUT_WAIT_KIND {
  AppleAgxScanoutWaitRegister,
  AppleAgxScanoutWaitPresent,
  AppleAgxScanoutWaitRelease,
} APPLE_AGX_SCANOUT_WAIT_KIND;

static APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutWaitForReceipt(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U64 Sequence,
    APPLE_AGX_SCANOUT_WAIT_KIND Kind, APPLE_AGX_SCANOUT_U64 SurfaceOffset,
    APPLE_AGX_SCANOUT_U64 DeadlineMs, APPLE_AGX_SCANOUT_U32 *SwapId) {
  APPLE_AGX_SCANOUT_U64 start_ms = Client->Io.NowMs(Client->Io.Context);
  APPLE_AGX_SCANOUT_U32 polls;

  if (start_ms > DeadlineMs)
    return AppleAgxScanoutTimeout;
  for (polls = 0u; polls < Client->MaxPolls; ++polls) {
    APPLE_AGX_SCANOUT_U64 receipt;
    APPLE_AGX_SCANOUT_U64 applied;
    APPLE_AGX_SCANOUT_U32 result;
    APPLE_AGX_SCANOUT_U32 state;
    APPLE_AGX_SCANOUT_U64 now;

    if (!AppleAgxScanoutRead64(Client, APPLE_AGX_SCANOUT_REG_RECEIPT_SEQUENCE,
                               &receipt) ||
        !AppleAgxScanoutRead32(Client, APPLE_AGX_SCANOUT_REG_RESULT, &result) ||
        !AppleAgxScanoutRead32(Client, APPLE_AGX_SCANOUT_REG_STATE, &state) ||
        !AppleAgxScanoutRead64(Client, APPLE_AGX_SCANOUT_REG_APPLIED_SEQUENCE,
                               &applied))
      return AppleAgxScanoutTransportFailed;
    now = Client->Io.NowMs(Client->Io.Context);
    if (now < start_ms)
      return AppleAgxScanoutClockRegression;
    if (now > DeadlineMs)
      return AppleAgxScanoutTimeout;
    Client->LastBrokerResult = result;
    if (receipt > Sequence)
      return AppleAgxScanoutInconsistentReceipt;
    if (receipt == Sequence) {
      if (result != APPLE_AGX_SCANOUT_BROKER_OK)
        return AppleAgxScanoutMapBrokerResult(result);
      if (applied != Sequence)
        return AppleAgxScanoutInconsistentReceipt;
      if (Kind == AppleAgxScanoutWaitRegister) {
        return state == APPLE_AGX_SCANOUT_STATE_READY
                   ? AppleAgxScanoutOk
                   : AppleAgxScanoutInconsistentReceipt;
      }
      if (Kind == AppleAgxScanoutWaitRelease) {
        return state == APPLE_AGX_SCANOUT_STATE_UNREGISTERED
                   ? AppleAgxScanoutOk
                   : AppleAgxScanoutInconsistentReceipt;
      }
      if (state == APPLE_AGX_SCANOUT_STATE_ACTIVE) {
        APPLE_AGX_SCANOUT_U64 active_offset;
        APPLE_AGX_SCANOUT_U32 swap_id;
        if (!AppleAgxScanoutRead64(Client,
                                   APPLE_AGX_SCANOUT_REG_ACTIVE_OFFSET,
                                   &active_offset) ||
            !AppleAgxScanoutRead32(Client, APPLE_AGX_SCANOUT_REG_SWAP_ID,
                                   &swap_id))
          return AppleAgxScanoutTransportFailed;
        now = Client->Io.NowMs(Client->Io.Context);
        if (now < start_ms)
          return AppleAgxScanoutClockRegression;
        if (now > DeadlineMs)
          return AppleAgxScanoutTimeout;
        if (active_offset != SurfaceOffset || swap_id == 0u)
          return AppleAgxScanoutInconsistentReceipt;
        *SwapId = swap_id;
        return AppleAgxScanoutOk;
      }
      return AppleAgxScanoutInconsistentReceipt;
    }
    if (result == APPLE_AGX_SCANOUT_BROKER_STALE_SEQUENCE ||
        result == APPLE_AGX_SCANOUT_BROKER_BUSY)
      return AppleAgxScanoutMapBrokerResult(result);
    if (polls + 1u == Client->MaxPolls)
      break;
    if (Client->Io.Pause != APPLE_AGX_SCANOUT_NULL &&
        !Client->Io.Pause(Client->Io.Context))
      return AppleAgxScanoutTransportFailed;
  }
  return AppleAgxScanoutPollLimit;
}

/* The surface registers of one PRESENT.  Without CAP_SCALED_SOURCE the
 * traffic is exactly the fixed J313 sequence m1n1 0d3a75b3 validates. */
static APPLE_AGX_SCANOUT_BOOL AppleAgxScanoutWriteSurface(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U64 SurfaceOffset) {
  if (!AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_SURFACE_OFFSET,
                              SurfaceOffset) ||
      !AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_SURFACE_SIZE,
                              Client->PresentSurfaceBytes) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_WIDTH,
                              Client->PresentWidth) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_HEIGHT,
                              Client->PresentHeight) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_STRIDE,
                              Client->PresentStride) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_FORMAT,
                              Client->PresentFormat))
    return APPLE_AGX_SCANOUT_FALSE;
  if ((Client->Capabilities & APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE) == 0u)
    return APPLE_AGX_SCANOUT_TRUE;
  return AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_DEST_ORIGIN,
                                Client->PresentDestOrigin) &&
         AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_DEST_SIZE,
                                Client->PresentDestSize);
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutInitialize(
    APPLE_AGX_SCANOUT_CLIENT *Client, const APPLE_AGX_SCANOUT_IO *Io,
    APPLE_AGX_SCANOUT_U64 FirstSequence, APPLE_AGX_SCANOUT_U32 MaxPolls) {
  if (Client == APPLE_AGX_SCANOUT_NULL || Io == APPLE_AGX_SCANOUT_NULL ||
      Io->NowMs == APPLE_AGX_SCANOUT_NULL ||
      Io->Read32 == APPLE_AGX_SCANOUT_NULL ||
      Io->Read64 == APPLE_AGX_SCANOUT_NULL ||
      Io->Write32 == APPLE_AGX_SCANOUT_NULL ||
      Io->Write64 == APPLE_AGX_SCANOUT_NULL || FirstSequence == 0ULL ||
      MaxPolls == 0u)
    return AppleAgxScanoutInvalidArgument;
  Client->Io = *Io;
  Client->NextSequence = FirstSequence;
  Client->MaxPolls = MaxPolls;
  Client->LastBrokerResult = APPLE_AGX_SCANOUT_BROKER_OK;
  Client->AbiVersion = 0u;
  Client->Capabilities = 0u;
  Client->PendingPresentSequence = 0ULL;
  Client->PoolBytes = 0ULL;
  Client->Qualified = APPLE_AGX_SCANOUT_FALSE;
  Client->PresentPending = APPLE_AGX_SCANOUT_FALSE;
  Client->PresentFormat = APPLE_AGX_SCANOUT_FORMAT_BGRA8888;
  Client->PresentWidth = APPLE_AGX_SCANOUT_J313_WIDTH;
  Client->PresentHeight = APPLE_AGX_SCANOUT_J313_HEIGHT;
  Client->PresentStride = APPLE_AGX_SCANOUT_J313_STRIDE;
  Client->PresentSurfaceBytes = APPLE_AGX_SCANOUT_J313_SURFACE_SIZE;
  Client->PresentDestOrigin = 0u;
  Client->PresentDestSize = APPLE_AGX_SCANOUT_J313_WIDTH |
                            (APPLE_AGX_SCANOUT_J313_HEIGHT << 16u);
  Client->TwoLayers = APPLE_AGX_SCANOUT_FALSE;
  return AppleAgxScanoutOk;
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutQualify(
    APPLE_AGX_SCANOUT_CLIENT *Client) {
  APPLE_AGX_SCANOUT_U32 magic;
  APPLE_AGX_SCANOUT_U32 version;
  APPLE_AGX_SCANOUT_U32 capabilities;
  APPLE_AGX_SCANOUT_U64 receipt;

  if (Client == APPLE_AGX_SCANOUT_NULL)
    return AppleAgxScanoutInvalidArgument;
  Client->Qualified = APPLE_AGX_SCANOUT_FALSE;
  if (!AppleAgxScanoutRead32(Client, APPLE_AGX_SCANOUT_REG_MAGIC, &magic) ||
      !AppleAgxScanoutRead32(Client, APPLE_AGX_SCANOUT_REG_ABI_VERSION,
                             &version) ||
      !AppleAgxScanoutRead32(Client, APPLE_AGX_SCANOUT_REG_CAPABILITIES,
                             &capabilities))
    return AppleAgxScanoutTransportFailed;
  if (magic != APPLE_AGX_SCANOUT_MAGIC)
    return AppleAgxScanoutIncompatibleMagic;
  if (version != APPLE_AGX_SCANOUT_ABI_VERSION_V1 &&
      version != APPLE_AGX_SCANOUT_ABI_VERSION_V2)
    return AppleAgxScanoutIncompatibleVersion;
  if ((capabilities & APPLE_AGX_SCANOUT_REQUIRED_CAPABILITIES) !=
      APPLE_AGX_SCANOUT_REQUIRED_CAPABILITIES)
    return AppleAgxScanoutMissingCapabilities;
  /* The broker's freshness floor lives as long as the hypervisor, not as long
   * as this client: a StartDevice after StopDevice in the same boot must
   * resume above the last terminal receipt, never at its own first sequence.
   * Sequences this client already issued are above every receipt, so the
   * adjustment only ever moves forward. */
  if (!AppleAgxScanoutRead64(Client, APPLE_AGX_SCANOUT_REG_RECEIPT_SEQUENCE,
                             &receipt))
    return AppleAgxScanoutTransportFailed;
  if (Client->NextSequence != 0ULL && receipt >= Client->NextSequence)
    Client->NextSequence =
        receipt == APPLE_AGX_SCANOUT_U64_MAX ? 0ULL : receipt + 1ULL;
  Client->AbiVersion = version;
  Client->Capabilities = capabilities;
  Client->Qualified = APPLE_AGX_SCANOUT_TRUE;
  return AppleAgxScanoutOk;
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutRegisterPool(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U64 PoolIpa,
    APPLE_AGX_SCANOUT_U64 PoolBytes, APPLE_AGX_SCANOUT_U64 DeadlineMs) {
  APPLE_AGX_SCANOUT_U64 sequence;
  APPLE_AGX_SCANOUT_RESULT result;
  if (Client == APPLE_AGX_SCANOUT_NULL || PoolIpa == 0ULL ||
      (PoolIpa & (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL ||
      PoolBytes < APPLE_AGX_SCANOUT_J313_SURFACE_SIZE ||
      PoolBytes > APPLE_AGX_SCANOUT_J313_POOL_MAX_SIZE ||
      (PoolBytes & (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL)
    return AppleAgxScanoutInvalidArgument;
  if (!Client->Qualified)
    return AppleAgxScanoutNotQualified;
  result = AppleAgxScanoutTakeSequence(Client, &sequence);
  if (result != AppleAgxScanoutOk)
    return result;
  if (!AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_POOL_IPA,
                              PoolIpa) ||
      !AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_POOL_SIZE,
                              PoolBytes) ||
      !AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE,
                              sequence) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_COMMAND,
                              APPLE_AGX_SCANOUT_CMD_REGISTER_POOL))
    return AppleAgxScanoutTransportFailed;
  Client->PoolBytes = PoolBytes;
  return AppleAgxScanoutWaitForReceipt(Client, sequence,
                                       AppleAgxScanoutWaitRegister, 0ULL,
                                       DeadlineMs, APPLE_AGX_SCANOUT_NULL);
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutPresentApplied(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U64 SurfaceOffset,
    APPLE_AGX_SCANOUT_U64 DeadlineMs, APPLE_AGX_SCANOUT_U32 *SwapId) {
  APPLE_AGX_SCANOUT_U64 sequence;
  APPLE_AGX_SCANOUT_RESULT result;
  if (Client == APPLE_AGX_SCANOUT_NULL || SwapId == APPLE_AGX_SCANOUT_NULL ||
      (SurfaceOffset & (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL ||
      Client->PresentSurfaceBytes == 0ULL ||
      Client->PoolBytes < Client->PresentSurfaceBytes ||
      SurfaceOffset > Client->PoolBytes - Client->PresentSurfaceBytes)
    return AppleAgxScanoutInvalidArgument;
  *SwapId = 0u;
  if (!Client->Qualified)
    return AppleAgxScanoutNotQualified;
  result = AppleAgxScanoutTakeSequence(Client, &sequence);
  if (result != AppleAgxScanoutOk)
    return result;
  if (!AppleAgxScanoutWriteSurface(Client, SurfaceOffset) ||
      !AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE,
                              sequence) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_COMMAND,
                              APPLE_AGX_SCANOUT_CMD_PRESENT))
    return AppleAgxScanoutTransportFailed;
  return AppleAgxScanoutWaitForReceipt(Client, sequence,
                                       AppleAgxScanoutWaitPresent,
                                       SurfaceOffset, DeadlineMs, SwapId);
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutQueuePresent(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U64 SurfaceOffset,
    APPLE_AGX_SCANOUT_U64 *Sequence) {
  APPLE_AGX_SCANOUT_U64 sequence;
  APPLE_AGX_SCANOUT_RESULT result;

  if (Client == APPLE_AGX_SCANOUT_NULL || Sequence == APPLE_AGX_SCANOUT_NULL ||
      (SurfaceOffset & (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL ||
      Client->PresentSurfaceBytes == 0ULL ||
      Client->PoolBytes < Client->PresentSurfaceBytes ||
      SurfaceOffset > Client->PoolBytes - Client->PresentSurfaceBytes)
    return AppleAgxScanoutInvalidArgument;
  *Sequence = 0ULL;
  if (!Client->Qualified)
    return AppleAgxScanoutNotQualified;
  if (Client->AbiVersion != APPLE_AGX_SCANOUT_ABI_VERSION_V2 ||
      (Client->Capabilities & APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES) !=
          APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES)
    return AppleAgxScanoutMissingCapabilities;
  if (Client->PresentPending)
    return AppleAgxScanoutBusy;

  result = AppleAgxScanoutTakeSequence(Client, &sequence);
  if (result != AppleAgxScanoutOk)
    return result;
  if (!AppleAgxScanoutWriteSurface(Client, SurfaceOffset) ||
      !AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE,
                              sequence) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_COMMAND,
                              APPLE_AGX_SCANOUT_CMD_PRESENT))
    return AppleAgxScanoutTransportFailed;

  Client->PendingPresentSequence = sequence;
  Client->PresentPending = APPLE_AGX_SCANOUT_TRUE;
  *Sequence = sequence;
  return AppleAgxScanoutOk;
}

static APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutRequireV2Interrupts(
    APPLE_AGX_SCANOUT_CLIENT *Client) {
  if (Client == APPLE_AGX_SCANOUT_NULL)
    return AppleAgxScanoutInvalidArgument;
  if (!Client->Qualified)
    return AppleAgxScanoutNotQualified;
  if (Client->AbiVersion != APPLE_AGX_SCANOUT_ABI_VERSION_V2 ||
      (Client->Capabilities & APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES) !=
          APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES)
    return AppleAgxScanoutMissingCapabilities;
  return AppleAgxScanoutOk;
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutEnableInterrupts(
    APPLE_AGX_SCANOUT_CLIENT *Client) {
  APPLE_AGX_SCANOUT_RESULT result =
      AppleAgxScanoutRequireV2Interrupts(Client);
  if (result != AppleAgxScanoutOk)
    return result;
  return AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_IRQ_ENABLE,
                                APPLE_AGX_SCANOUT_IRQ_MASK)
             ? AppleAgxScanoutOk
             : AppleAgxScanoutTransportFailed;
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutDisableInterrupts(
    APPLE_AGX_SCANOUT_CLIENT *Client) {
  if (Client == APPLE_AGX_SCANOUT_NULL || !Client->Qualified)
    return AppleAgxScanoutInvalidArgument;
  return AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_IRQ_ENABLE, 0u)
             ? AppleAgxScanoutOk
             : AppleAgxScanoutTransportFailed;
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutConsumeInterrupt(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U32 *IrqStatus,
    APPLE_AGX_SCANOUT_U64 *LatchedSequence) {
  APPLE_AGX_SCANOUT_RESULT result;
  APPLE_AGX_SCANOUT_U32 status;
  APPLE_AGX_SCANOUT_U64 latched = 0ULL;

  if (IrqStatus == APPLE_AGX_SCANOUT_NULL ||
      LatchedSequence == APPLE_AGX_SCANOUT_NULL)
    return AppleAgxScanoutInvalidArgument;
  *IrqStatus = 0u;
  *LatchedSequence = 0ULL;
  result = AppleAgxScanoutRequireV2Interrupts(Client);
  if (result != AppleAgxScanoutOk)
    return result;
  if (!AppleAgxScanoutRead32(Client, APPLE_AGX_SCANOUT_REG_IRQ_STATUS,
                             &status))
    return AppleAgxScanoutTransportFailed;
  status &= APPLE_AGX_SCANOUT_IRQ_MASK;
  if (status == 0u)
    return AppleAgxScanoutNoInterrupt;
  if ((status & APPLE_AGX_SCANOUT_IRQ_LATCHED) != 0u &&
      !AppleAgxScanoutRead64(Client,
                             APPLE_AGX_SCANOUT_REG_LATCHED_SEQUENCE,
                             &latched))
    return AppleAgxScanoutTransportFailed;
  if (!AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_IRQ_STATUS,
                              status))
    return AppleAgxScanoutTransportFailed;
  *IrqStatus = status;
  *LatchedSequence = latched;
  if ((status & APPLE_AGX_SCANOUT_IRQ_LATCHED) != 0u) {
    if (latched == 0ULL)
      return AppleAgxScanoutInconsistentReceipt;
    if (Client->PresentPending && latched != 0ULL &&
        latched == Client->PendingPresentSequence) {
      Client->PresentPending = APPLE_AGX_SCANOUT_FALSE;
      Client->PendingPresentSequence = 0ULL;
    }
  }
  return AppleAgxScanoutOk;
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutRelease(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U64 DeadlineMs) {
  APPLE_AGX_SCANOUT_U64 sequence;
  APPLE_AGX_SCANOUT_RESULT result;
  if (Client == APPLE_AGX_SCANOUT_NULL)
    return AppleAgxScanoutInvalidArgument;
  if (!Client->Qualified)
    return AppleAgxScanoutNotQualified;
  result = AppleAgxScanoutTakeSequence(Client, &sequence);
  if (result != AppleAgxScanoutOk)
    return result;
  if (!AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE,
                              sequence) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_COMMAND,
                              APPLE_AGX_SCANOUT_CMD_RELEASE))
    return AppleAgxScanoutTransportFailed;
  result = AppleAgxScanoutWaitForReceipt(Client, sequence,
                                         AppleAgxScanoutWaitRelease, 0ULL,
                                         DeadlineMs, APPLE_AGX_SCANOUT_NULL);
  /* V3: a successful RELEASE clears REG_NEGOTIATED; the next owner
   * negotiates again before its REGISTER_POOL. */
  if (result == AppleAgxScanoutOk)
    Client->TwoLayers = APPLE_AGX_SCANOUT_FALSE;
  return result;
}

/* ---- S2 ABI V3 (two display layers) ---- */

APPLE_AGX_SCANOUT_BOOL AppleAgxScanoutTwoLayersAdvertised(
    const APPLE_AGX_SCANOUT_CLIENT *Client) {
  return Client != APPLE_AGX_SCANOUT_NULL && Client->Qualified &&
                 Client->AbiVersion == APPLE_AGX_SCANOUT_ABI_VERSION_V2 &&
                 (Client->Capabilities &
                  APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES) ==
                     APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES &&
                 (Client->Capabilities & APPLE_AGX_SCANOUT_CAP_TWO_LAYERS) !=
                     0u
             ? APPLE_AGX_SCANOUT_TRUE
             : APPLE_AGX_SCANOUT_FALSE;
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutNegotiateTwoLayers(
    APPLE_AGX_SCANOUT_CLIENT *Client) {
  APPLE_AGX_SCANOUT_U32 negotiated = 0u;

  if (Client == APPLE_AGX_SCANOUT_NULL)
    return AppleAgxScanoutInvalidArgument;
  Client->TwoLayers = APPLE_AGX_SCANOUT_FALSE;
  if (!Client->Qualified)
    return AppleAgxScanoutNotQualified;
  /* Without the bit every offset from 0xa8 up is refused: no access. */
  if (!AppleAgxScanoutTwoLayersAdvertised(Client))
    return AppleAgxScanoutMissingCapabilities;
  if (!AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_NEGOTIATED,
                              APPLE_AGX_SCANOUT_CAP_TWO_LAYERS) ||
      !AppleAgxScanoutRead32(Client, APPLE_AGX_SCANOUT_REG_NEGOTIATED,
                             &negotiated))
    return AppleAgxScanoutTransportFailed;
  /* The broker ignores the write outside UNREGISTERED: only the read-back
   * proves the negotiation. */
  if (negotiated != APPLE_AGX_SCANOUT_CAP_TWO_LAYERS)
    return AppleAgxScanoutNotNegotiated;
  Client->TwoLayers = APPLE_AGX_SCANOUT_TRUE;
  return AppleAgxScanoutOk;
}

/* One record at Base, in the broker report's order (steps 4/5). */
static APPLE_AGX_SCANOUT_BOOL AppleAgxScanoutWriteLayer(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U32 Base,
    const APPLE_AGX_SCANOUT_LAYER *Layer) {
  return AppleAgxScanoutWrite64(
             Client, Base + APPLE_AGX_SCANOUT_LAYER_SURFACE_OFFSET,
             Layer->SurfaceOffset) &&
         AppleAgxScanoutWrite64(
             Client, Base + APPLE_AGX_SCANOUT_LAYER_SURFACE_SIZE,
             Layer->SurfaceSize) &&
         AppleAgxScanoutWrite32(Client, Base + APPLE_AGX_SCANOUT_LAYER_WIDTH,
                                Layer->Width) &&
         AppleAgxScanoutWrite32(Client, Base + APPLE_AGX_SCANOUT_LAYER_HEIGHT,
                                Layer->Height) &&
         AppleAgxScanoutWrite32(Client, Base + APPLE_AGX_SCANOUT_LAYER_STRIDE,
                                Layer->Stride) &&
         AppleAgxScanoutWrite32(Client, Base + APPLE_AGX_SCANOUT_LAYER_FORMAT,
                                Layer->Format) &&
         AppleAgxScanoutWrite32(
             Client, Base + APPLE_AGX_SCANOUT_LAYER_SRC_ORIGIN,
             Layer->SrcOrigin) &&
         AppleAgxScanoutWrite32(Client,
                                Base + APPLE_AGX_SCANOUT_LAYER_SRC_SIZE,
                                Layer->SrcSize) &&
         AppleAgxScanoutWrite32(
             Client, Base + APPLE_AGX_SCANOUT_LAYER_DST_ORIGIN,
             Layer->DstOrigin) &&
         AppleAgxScanoutWrite32(Client,
                                Base + APPLE_AGX_SCANOUT_LAYER_DST_SIZE,
                                Layer->DstSize) &&
         AppleAgxScanoutWrite32(Client, Base + APPLE_AGX_SCANOUT_LAYER_FLAGS,
                                Layer->Flags) &&
         AppleAgxScanoutWrite32(
             Client, Base + APPLE_AGX_SCANOUT_LAYER_RESERVED0, 0u) &&
         AppleAgxScanoutWrite64(
             Client, Base + APPLE_AGX_SCANOUT_LAYER_RESERVED1, 0ULL);
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutQueueCommitLayers(
    APPLE_AGX_SCANOUT_CLIENT *Client, const APPLE_AGX_SCANOUT_LAYER *Layers,
    APPLE_AGX_SCANOUT_U32 Count, APPLE_AGX_SCANOUT_U64 *Sequence) {
  APPLE_AGX_SCANOUT_U64 sequence;
  APPLE_AGX_SCANOUT_U64 receipt = 0ULL;
  APPLE_AGX_SCANOUT_U32 broker_result = 0u;
  APPLE_AGX_SCANOUT_RESULT result;

  if (Client == APPLE_AGX_SCANOUT_NULL || Sequence == APPLE_AGX_SCANOUT_NULL ||
      Layers == APPLE_AGX_SCANOUT_NULL || Count == 0u ||
      Count > APPLE_AGX_SCANOUT_V3_LAYERS)
    return AppleAgxScanoutInvalidArgument;
  *Sequence = 0ULL;
  if (!Client->Qualified)
    return AppleAgxScanoutNotQualified;
  if (!Client->TwoLayers)
    return AppleAgxScanoutNotNegotiated;
  if (Client->PresentPending)
    return AppleAgxScanoutBusy;
  result = AppleAgxScanoutTakeSequence(Client, &sequence);
  if (result != AppleAgxScanoutOk)
    return result;
  if (!AppleAgxScanoutWriteLayer(Client, APPLE_AGX_SCANOUT_REG_LAYER0,
                                 &Layers[0]) ||
      (Count == 2u &&
       !AppleAgxScanoutWriteLayer(Client, APPLE_AGX_SCANOUT_REG_LAYER1,
                                  &Layers[1])) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_LAYER_COUNT,
                              Count) ||
      !AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE,
                              sequence) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_COMMAND,
                              APPLE_AGX_SCANOUT_CMD_COMMIT_LAYERS))
    return AppleAgxScanoutTransportFailed;
  /* The broker validates the records when COMMAND is written: a refusal is
   * terminal (RECEIPT_SEQUENCE == this sequence, RESULT = its code), BUSY
   * leaves the receipt alone.  RESULT OK means pending until the latch. */
  if (!AppleAgxScanoutRead32(Client, APPLE_AGX_SCANOUT_REG_RESULT,
                             &broker_result) ||
      !AppleAgxScanoutRead64(Client, APPLE_AGX_SCANOUT_REG_RECEIPT_SEQUENCE,
                             &receipt))
    return AppleAgxScanoutTransportFailed;
  Client->LastBrokerResult = broker_result;
  if (broker_result != APPLE_AGX_SCANOUT_BROKER_OK) {
    if (receipt == sequence)
      return AppleAgxScanoutBrokerRejected;
    if (broker_result == APPLE_AGX_SCANOUT_BROKER_BUSY)
      return AppleAgxScanoutBusy;
    return AppleAgxScanoutMapBrokerResult(broker_result);
  }
  Client->PendingPresentSequence = sequence;
  Client->PresentPending = APPLE_AGX_SCANOUT_TRUE;
  *Sequence = sequence;
  return AppleAgxScanoutOk;
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutTakePresentFailure(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U64 *Sequence,
    APPLE_AGX_SCANOUT_U32 *BrokerResult) {
  APPLE_AGX_SCANOUT_U32 result = 0u;
  APPLE_AGX_SCANOUT_U64 receipt = 0ULL;
  if (Client == APPLE_AGX_SCANOUT_NULL || Sequence == APPLE_AGX_SCANOUT_NULL ||
      BrokerResult == APPLE_AGX_SCANOUT_NULL)
    return AppleAgxScanoutInvalidArgument;
  *Sequence = 0ULL;
  *BrokerResult = 0u;
  if (!Client->PresentPending || Client->PendingPresentSequence == 0ULL)
    return AppleAgxScanoutNoInterrupt;
  if (!AppleAgxScanoutRead32(Client, APPLE_AGX_SCANOUT_REG_RESULT, &result) ||
      !AppleAgxScanoutRead64(Client, APPLE_AGX_SCANOUT_REG_RECEIPT_SEQUENCE,
                             &receipt))
    return AppleAgxScanoutTransportFailed;
  if (receipt != Client->PendingPresentSequence ||
      result == APPLE_AGX_SCANOUT_BROKER_OK)
    return AppleAgxScanoutNoInterrupt;
  Client->LastBrokerResult = result;
  *Sequence = receipt;
  *BrokerResult = result;
  Client->PresentPending = APPLE_AGX_SCANOUT_FALSE;
  Client->PendingPresentSequence = 0ULL;
  return AppleAgxScanoutOk;
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutReadActiveLayers(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U32 *ActiveLayers) {
  if (Client == APPLE_AGX_SCANOUT_NULL ||
      ActiveLayers == APPLE_AGX_SCANOUT_NULL)
    return AppleAgxScanoutInvalidArgument;
  *ActiveLayers = 0u;
  if (!Client->Qualified)
    return AppleAgxScanoutNotQualified;
  if (!Client->TwoLayers)
    return AppleAgxScanoutNotNegotiated;
  return AppleAgxScanoutRead32(Client, APPLE_AGX_SCANOUT_REG_ACTIVE_LAYERS,
                               ActiveLayers)
             ? AppleAgxScanoutOk
             : AppleAgxScanoutTransportFailed;
}
