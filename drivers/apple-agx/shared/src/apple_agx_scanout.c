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
  Client->Qualified = APPLE_AGX_SCANOUT_FALSE;
  Client->PresentPending = APPLE_AGX_SCANOUT_FALSE;
  return AppleAgxScanoutOk;
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutQualify(
    APPLE_AGX_SCANOUT_CLIENT *Client) {
  APPLE_AGX_SCANOUT_U32 magic;
  APPLE_AGX_SCANOUT_U32 version;
  APPLE_AGX_SCANOUT_U32 capabilities;

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
  Client->AbiVersion = version;
  Client->Capabilities = capabilities;
  Client->Qualified = APPLE_AGX_SCANOUT_TRUE;
  return AppleAgxScanoutOk;
}

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutRegisterPool(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U64 PoolIpa,
    APPLE_AGX_SCANOUT_U64 DeadlineMs) {
  APPLE_AGX_SCANOUT_U64 sequence;
  APPLE_AGX_SCANOUT_RESULT result;
  if (Client == APPLE_AGX_SCANOUT_NULL || PoolIpa == 0ULL ||
      (PoolIpa & (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL)
    return AppleAgxScanoutInvalidArgument;
  if (!Client->Qualified)
    return AppleAgxScanoutNotQualified;
  result = AppleAgxScanoutTakeSequence(Client, &sequence);
  if (result != AppleAgxScanoutOk)
    return result;
  if (!AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_POOL_IPA,
                              PoolIpa) ||
      !AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_POOL_SIZE,
                              APPLE_AGX_SCANOUT_J313_POOL_SIZE) ||
      !AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE,
                              sequence) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_COMMAND,
                              APPLE_AGX_SCANOUT_CMD_REGISTER_POOL))
    return AppleAgxScanoutTransportFailed;
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
      SurfaceOffset > APPLE_AGX_SCANOUT_J313_POOL_SIZE -
                          APPLE_AGX_SCANOUT_J313_SURFACE_SIZE)
    return AppleAgxScanoutInvalidArgument;
  *SwapId = 0u;
  if (!Client->Qualified)
    return AppleAgxScanoutNotQualified;
  result = AppleAgxScanoutTakeSequence(Client, &sequence);
  if (result != AppleAgxScanoutOk)
    return result;
  if (!AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_SURFACE_OFFSET,
                              SurfaceOffset) ||
      !AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_SURFACE_SIZE,
                              APPLE_AGX_SCANOUT_J313_SURFACE_SIZE) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_WIDTH,
                              APPLE_AGX_SCANOUT_J313_WIDTH) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_HEIGHT,
                              APPLE_AGX_SCANOUT_J313_HEIGHT) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_STRIDE,
                              APPLE_AGX_SCANOUT_J313_STRIDE) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_FORMAT,
                              APPLE_AGX_SCANOUT_FORMAT_BGRA8888) ||
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
      SurfaceOffset > APPLE_AGX_SCANOUT_J313_POOL_SIZE -
                          APPLE_AGX_SCANOUT_J313_SURFACE_SIZE)
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
  if (!AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_SURFACE_OFFSET,
                              SurfaceOffset) ||
      !AppleAgxScanoutWrite64(Client, APPLE_AGX_SCANOUT_REG_SURFACE_SIZE,
                              APPLE_AGX_SCANOUT_J313_SURFACE_SIZE) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_WIDTH,
                              APPLE_AGX_SCANOUT_J313_WIDTH) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_HEIGHT,
                              APPLE_AGX_SCANOUT_J313_HEIGHT) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_STRIDE,
                              APPLE_AGX_SCANOUT_J313_STRIDE) ||
      !AppleAgxScanoutWrite32(Client, APPLE_AGX_SCANOUT_REG_FORMAT,
                              APPLE_AGX_SCANOUT_FORMAT_BGRA8888) ||
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
    if (!Client->PresentPending || latched == 0ULL ||
        latched != Client->PendingPresentSequence)
      return AppleAgxScanoutInconsistentReceipt;
    Client->PresentPending = APPLE_AGX_SCANOUT_FALSE;
    Client->PendingPresentSequence = 0ULL;
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
  return AppleAgxScanoutWaitForReceipt(Client, sequence,
                                       AppleAgxScanoutWaitRelease, 0ULL,
                                       DeadlineMs, APPLE_AGX_SCANOUT_NULL);
}
