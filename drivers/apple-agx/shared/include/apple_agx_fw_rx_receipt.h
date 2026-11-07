/* SPDX-License-Identifier: MIT
 *
 * Read-only receipt of the firmware->host channels the KMD does not consume
 * (EXP2215 follow-up, fork 7). Asahi polls fw_log, ktrace, stats and event on
 * every firmware interrupt (drm/asahi gpu.rs) and advances each read_ptr; the
 * KMD drains only the event channel, so the stats (16), ktrace (15) and fwlog
 * (14, six sub-channels) read pointers never move. Whether the firmware then
 * drops or waits is unknown. This receipt samples the pointers and the stats
 * messages without writing anything: no read_ptr update, no firmware command.
 *
 * Layouts (drm/asahi fw/channels.rs, m1n1 fw/agx/channels.py, V13_5):
 * ChannelState = read_ptr @0x00, write_ptr @0x20, 0x30 bytes; the fwlog state
 * is six of them. A stats message is 0x40 bytes: u32 tag, then the payload at
 * 4 with 4-byte aligned u64s. Tags: 4 Utilization {ts, util1..4}, 0xb
 * PowerState {ts, last_busy_ts, active, poweroff, unk1, pstate}, 0xc FwBusy
 * {ts, busy}, 0xd PState {pad 8, ps_min, unk1, ps_max}; the maximum is 0xe.
 */
#ifndef APPLE_AGX_FW_RX_RECEIPT_H
#define APPLE_AGX_FW_RX_RECEIPT_H

#define APPLE_AGX_FW_RX_RECEIPT_VERSION 1u
#define APPLE_AGX_FW_RX_STATE_BYTES 0x30u
#define APPLE_AGX_FW_RX_READ_OFFSET 0x00u
#define APPLE_AGX_FW_RX_WRITE_OFFSET 0x20u
#define APPLE_AGX_FW_RX_FWLOG_SUBCHANNELS 6u
#define APPLE_AGX_FW_RX_STATS_ENTRIES 0x100u
#define APPLE_AGX_FW_RX_STATS_ENTRY_BYTES 0x40u
#define APPLE_AGX_FW_RX_STATS_TAGS 16u
#define APPLE_AGX_FW_RX_STATS_MAX_TAG 0xeu
/* Asahi channel.rs ring sizes: ktrace 0x200, stats and each fwlog ring 0x100 */
#define APPLE_AGX_FW_RX_KTRACE_ENTRIES 0x200u
#define APPLE_AGX_FW_RX_FWLOG_ENTRIES 0x100u

typedef unsigned int (*APPLE_AGX_FW_RX_READ_U32)(void *Context,
    const volatile unsigned int *Address, unsigned int *Value);

typedef unsigned int (*APPLE_AGX_FW_RX_PUBLISH_U32)(void *Context,
    volatile unsigned int *Address, unsigned int Value);

/* The drain's writable view: only the read pointers are written. */
typedef struct _APPLE_AGX_FW_RX_DRAIN_VIEW {
  volatile unsigned char *StatsState;
  volatile unsigned char *KtraceState;
  volatile unsigned char *FwlogState;
  APPLE_AGX_FW_RX_READ_U32 ReadU32;
  APPLE_AGX_FW_RX_PUBLISH_U32 PublishU32;
  void *Context;
} APPLE_AGX_FW_RX_DRAIN_VIEW;

typedef struct _APPLE_AGX_FW_RX_VIEW {
  const volatile unsigned char *StatsState;  /* 0x30 bytes */
  const volatile unsigned char *StatsRing;   /* entries x 0x40 */
  const volatile unsigned char *KtraceState; /* 0x30 bytes */
  const volatile unsigned char *FwlogState;  /* 6 x 0x30 bytes */
  APPLE_AGX_FW_RX_READ_U32 ReadU32;
  void *Context;
} APPLE_AGX_FW_RX_VIEW;

/* 304 bytes. Pointer values are the last sample; *Changes count samples in
 * which the write pointer differed from the previous sample. The stats
 * histogram and the Last* values cover the ring entries the firmware wrote:
 * [0, StatsWrite) while it has not wrapped (read_ptr 0 never moves), all
 * entries once StatsWrapped (write pointer seen lower than before). */
typedef struct _APPLE_AGX_FW_RX_RECEIPT {
  unsigned int Version, Bytes, Samples, Failures;
  unsigned int StatsRead, StatsWrite, StatsWriteFirst, StatsWriteChanges;
  unsigned int StatsWrapped, StatsFull, StatsUnknownTags, StatsScanned;
  unsigned int KtraceRead, KtraceWrite, KtraceWriteChanges, DrainFailures;
  unsigned int FwlogRead[APPLE_AGX_FW_RX_FWLOG_SUBCHANNELS];
  unsigned int FwlogWrite[APPLE_AGX_FW_RX_FWLOG_SUBCHANNELS];
  unsigned int StatsTags[APPLE_AGX_FW_RX_STATS_TAGS];
  unsigned long long FirstQpc, LastQpc;
  unsigned long long UtilTimestamp;
  unsigned int Util[4];
  unsigned long long FwBusyTimestamp;
  unsigned int FwBusy, PowerStateActive;
  unsigned long long PowerStateTimestamp, PowerStateLastBusy;
  unsigned int PowerStateOff, PowerStatePstate, PStateMin, PStateMax;
  /* AppleAgxFwRxDrain (Wom1FwRxDrain): calls and the messages it consumed */
  unsigned long long DrainCalls, DrainedStats, DrainedKtrace, DrainedFwlog;
  unsigned long long Reserved1;
} APPLE_AGX_FW_RX_RECEIPT;

typedef char apple_agx_fw_rx_receipt_size[
    sizeof(APPLE_AGX_FW_RX_RECEIPT) == 304u ? 1 : -1];

#ifdef __cplusplus
extern "C" {
#endif
void AppleAgxFwRxReceiptInit(APPLE_AGX_FW_RX_RECEIPT *Receipt);
/* One read-only sample. Returns 1 when every pointer was read. */
unsigned int AppleAgxFwRxSample(APPLE_AGX_FW_RX_RECEIPT *Receipt,
    const APPLE_AGX_FW_RX_VIEW *View, unsigned long long Qpc);
/* Asahi's consumer contract (drm/asahi gpu.rs: fw_log, ktrace and stats
 * poll() on every firmware interrupt; channel.rs RxChannel::get advances
 * read_ptr past each message): sets each undrained channel's read pointer
 * to its write pointer. The messages are not interpreted (Asahi only logs
 * them); counts go to the receipt. A pointer out of range is left alone and
 * counted in DrainFailures. Returns 1 when every channel was handled. */
unsigned int AppleAgxFwRxDrain(APPLE_AGX_FW_RX_RECEIPT *Receipt,
    const APPLE_AGX_FW_RX_DRAIN_VIEW *View);
#ifdef __cplusplus
}
#endif

#endif
