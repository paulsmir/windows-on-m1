/* SPDX-License-Identifier: MIT
 * Read-only firmware->host channel receipt (apple_agx_fw_rx_receipt.h). */
#include "apple_agx_fw_rx_receipt.h"

static void fw_rx_zero(void *p, unsigned int n) {
  unsigned char *b = (unsigned char *)p;
  while (n--) *b++ = 0u;
}

void AppleAgxFwRxReceiptInit(APPLE_AGX_FW_RX_RECEIPT *Receipt) {
  if (!Receipt) return;
  fw_rx_zero(Receipt, (unsigned int)sizeof(*Receipt));
  Receipt->Version = APPLE_AGX_FW_RX_RECEIPT_VERSION;
  Receipt->Bytes = (unsigned int)sizeof(*Receipt);
}

static unsigned int fw_rx_u32(const APPLE_AGX_FW_RX_VIEW *v,
    const volatile unsigned char *base, unsigned int offset, unsigned int *out) {
  return base && v->ReadU32 &&
         v->ReadU32(v->Context, (const volatile unsigned int *)(base + offset), out)
      ? 1u : 0u;
}

static unsigned int fw_rx_u64(const APPLE_AGX_FW_RX_VIEW *v,
    const volatile unsigned char *base, unsigned int offset, unsigned long long *out) {
  unsigned int lo = 0u, hi = 0u;
  if (!fw_rx_u32(v, base, offset, &lo) || !fw_rx_u32(v, base, offset + 4u, &hi))
    return 0u;
  *out = (unsigned long long)lo | (unsigned long long)hi << 32;
  return 1u;
}

/* The stats ring entries [0, count) in firmware order, oldest first when it
 * wrapped (the slot at the write pointer is the oldest). Last* values are
 * those of the newest entry of each tag. */
static unsigned int fw_rx_scan_stats(APPLE_AGX_FW_RX_RECEIPT *r,
    const APPLE_AGX_FW_RX_VIEW *v) {
  unsigned int i, count = r->StatsWrapped ? APPLE_AGX_FW_RX_STATS_ENTRIES : r->StatsWrite;
  unsigned int start = r->StatsWrapped ? r->StatsWrite : 0u;
  unsigned int ok = 1u;
  fw_rx_zero(r->StatsTags, (unsigned int)sizeof(r->StatsTags));
  r->StatsUnknownTags = 0u;
  r->StatsScanned = 0u;
  for (i = 0u; i < count; ++i) {
    const unsigned int slot = (start + i) % APPLE_AGX_FW_RX_STATS_ENTRIES;
    const unsigned int at = slot * APPLE_AGX_FW_RX_STATS_ENTRY_BYTES;
    unsigned int tag = 0u;
    if (!fw_rx_u32(v, v->StatsRing, at, &tag)) { ok = 0u; break; }
    ++r->StatsScanned;
    if (tag > APPLE_AGX_FW_RX_STATS_MAX_TAG) { ++r->StatsUnknownTags; continue; }
    ++r->StatsTags[tag];
    switch (tag) {
      case 0x4u:
        ok &= fw_rx_u64(v, v->StatsRing, at + 4u, &r->UtilTimestamp);
        ok &= fw_rx_u32(v, v->StatsRing, at + 12u, &r->Util[0]);
        ok &= fw_rx_u32(v, v->StatsRing, at + 16u, &r->Util[1]);
        ok &= fw_rx_u32(v, v->StatsRing, at + 20u, &r->Util[2]);
        ok &= fw_rx_u32(v, v->StatsRing, at + 24u, &r->Util[3]);
        break;
      case 0xbu:
        ok &= fw_rx_u64(v, v->StatsRing, at + 4u, &r->PowerStateTimestamp);
        ok &= fw_rx_u64(v, v->StatsRing, at + 12u, &r->PowerStateLastBusy);
        ok &= fw_rx_u32(v, v->StatsRing, at + 20u, &r->PowerStateActive);
        ok &= fw_rx_u32(v, v->StatsRing, at + 24u, &r->PowerStateOff);
        ok &= fw_rx_u32(v, v->StatsRing, at + 32u, &r->PowerStatePstate);
        break;
      case 0xcu:
        ok &= fw_rx_u64(v, v->StatsRing, at + 4u, &r->FwBusyTimestamp);
        ok &= fw_rx_u32(v, v->StatsRing, at + 12u, &r->FwBusy);
        break;
      case 0xdu:
        ok &= fw_rx_u32(v, v->StatsRing, at + 12u, &r->PStateMin);
        ok &= fw_rx_u32(v, v->StatsRing, at + 20u, &r->PStateMax);
        break;
      default:
        break;
    }
  }
  return ok;
}

unsigned int AppleAgxFwRxSample(APPLE_AGX_FW_RX_RECEIPT *Receipt,
    const APPLE_AGX_FW_RX_VIEW *View, unsigned long long Qpc) {
  unsigned int read = 0u, write = 0u, ok = 1u, i;
  if (!Receipt || !View || Receipt->Version != APPLE_AGX_FW_RX_RECEIPT_VERSION)
    return 0u;
  if (!fw_rx_u32(View, View->StatsState, APPLE_AGX_FW_RX_READ_OFFSET, &read) ||
      !fw_rx_u32(View, View->StatsState, APPLE_AGX_FW_RX_WRITE_OFFSET, &write) ||
      read >= APPLE_AGX_FW_RX_STATS_ENTRIES || write >= APPLE_AGX_FW_RX_STATS_ENTRIES) {
    ok = 0u;
  } else {
    if (Receipt->Samples == 0u) {
      Receipt->StatsWriteFirst = write;
      Receipt->FirstQpc = Qpc;
    } else if (write != Receipt->StatsWrite) {
      ++Receipt->StatsWriteChanges;
      if (write < Receipt->StatsWrite) Receipt->StatsWrapped = 1u;
    }
    Receipt->StatsRead = read;
    Receipt->StatsWrite = write;
    /* full: the firmware's next write would reach the unread read pointer */
    Receipt->StatsFull = (write + 1u) % APPLE_AGX_FW_RX_STATS_ENTRIES == read ? 1u : 0u;
    if (View->StatsRing && !fw_rx_scan_stats(Receipt, View)) ok = 0u;
  }
  if (View->KtraceState) {
    unsigned int kr = 0u, kw = 0u;
    if (fw_rx_u32(View, View->KtraceState, APPLE_AGX_FW_RX_READ_OFFSET, &kr) &&
        fw_rx_u32(View, View->KtraceState, APPLE_AGX_FW_RX_WRITE_OFFSET, &kw)) {
      if (Receipt->Samples != 0u && kw != Receipt->KtraceWrite) ++Receipt->KtraceWriteChanges;
      Receipt->KtraceRead = kr;
      Receipt->KtraceWrite = kw;
    } else {
      ok = 0u;
    }
  }
  for (i = 0u; View->FwlogState && i < APPLE_AGX_FW_RX_FWLOG_SUBCHANNELS; ++i) {
    const unsigned int base = i * APPLE_AGX_FW_RX_STATE_BYTES;
    if (!fw_rx_u32(View, View->FwlogState, base + APPLE_AGX_FW_RX_READ_OFFSET, &Receipt->FwlogRead[i]) ||
        !fw_rx_u32(View, View->FwlogState, base + APPLE_AGX_FW_RX_WRITE_OFFSET, &Receipt->FwlogWrite[i]))
      ok = 0u;
  }
  ++Receipt->Samples;
  Receipt->LastQpc = Qpc;
  if (!ok) ++Receipt->Failures;
  return ok;
}

/* One channel: read_ptr := write_ptr when both are in range. */
static unsigned int fw_rx_drain_one(const APPLE_AGX_FW_RX_DRAIN_VIEW *v,
    volatile unsigned char *state, unsigned int entries, unsigned long long *drained) {
  unsigned int read = 0u, write = 0u;
  if (!state || !v->ReadU32(v->Context,
          (const volatile unsigned int *)(state + APPLE_AGX_FW_RX_READ_OFFSET), &read) ||
      !v->ReadU32(v->Context,
          (const volatile unsigned int *)(state + APPLE_AGX_FW_RX_WRITE_OFFSET), &write) ||
      read >= entries || write >= entries)
    return 0u;
  if (read == write) return 1u;
  if (!v->PublishU32(v->Context,
          (volatile unsigned int *)(state + APPLE_AGX_FW_RX_READ_OFFSET), write))
    return 0u;
  *drained += (write + entries - read) % entries;
  return 1u;
}

unsigned int AppleAgxFwRxDrain(APPLE_AGX_FW_RX_RECEIPT *Receipt,
    const APPLE_AGX_FW_RX_DRAIN_VIEW *View) {
  unsigned int ok = 1u, i;
  if (!Receipt || !View || !View->ReadU32 || !View->PublishU32 ||
      Receipt->Version != APPLE_AGX_FW_RX_RECEIPT_VERSION)
    return 0u;
  ++Receipt->DrainCalls;
  if (View->StatsState &&
      !fw_rx_drain_one(View, View->StatsState, APPLE_AGX_FW_RX_STATS_ENTRIES,
                       &Receipt->DrainedStats)) ok = 0u;
  if (View->KtraceState &&
      !fw_rx_drain_one(View, View->KtraceState, APPLE_AGX_FW_RX_KTRACE_ENTRIES,
                       &Receipt->DrainedKtrace)) ok = 0u;
  for (i = 0u; View->FwlogState && i < APPLE_AGX_FW_RX_FWLOG_SUBCHANNELS; ++i)
    if (!fw_rx_drain_one(View, View->FwlogState + i * APPLE_AGX_FW_RX_STATE_BYTES,
                         APPLE_AGX_FW_RX_FWLOG_ENTRIES, &Receipt->DrainedFwlog)) ok = 0u;
  if (!ok) ++Receipt->DrainFailures;
  return ok;
}
