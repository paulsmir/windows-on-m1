#ifndef APPLE_AGX_SCANOUT_H
#define APPLE_AGX_SCANOUT_H

/* ABI v1 mirrors m1n1 src/hv_agx_scanout_broker.h at MMIO +0x400. */
typedef unsigned char APPLE_AGX_SCANOUT_BOOL;
typedef unsigned int APPLE_AGX_SCANOUT_U32;
typedef unsigned long long APPLE_AGX_SCANOUT_U64;

#define APPLE_AGX_SCANOUT_FALSE ((APPLE_AGX_SCANOUT_BOOL)0u)
#define APPLE_AGX_SCANOUT_TRUE ((APPLE_AGX_SCANOUT_BOOL)1u)

#define APPLE_AGX_SCANOUT_MMIO_OFFSET 0x400u
/* rc106: the window ends after REG_DEST_SIZE (m1n1 codex/m1n1-scaled). */
#define APPLE_AGX_SCANOUT_MMIO_SIZE 0xa8u
#define APPLE_AGX_SCANOUT_MAGIC 0x53584741u
#define APPLE_AGX_SCANOUT_ABI_VERSION_V1 1u
#define APPLE_AGX_SCANOUT_ABI_VERSION_V2 2u
#define APPLE_AGX_SCANOUT_ABI_VERSION APPLE_AGX_SCANOUT_ABI_VERSION_V1

#define APPLE_AGX_SCANOUT_CAP_FIXED_J313 (1u << 0u)
#define APPLE_AGX_SCANOUT_CAP_BGRA8888 (1u << 1u)
#define APPLE_AGX_SCANOUT_CAP_REGISTERED_POOL (1u << 2u)
#define APPLE_AGX_SCANOUT_CAP_APPLIED_RECEIPT (1u << 3u)
#define APPLE_AGX_SCANOUT_CAP_REPEATED_PRESENT (1u << 4u)
#define APPLE_AGX_SCANOUT_CAP_LATCHED_RECEIPT (1u << 5u)
#define APPLE_AGX_SCANOUT_CAP_LATCHED_IRQ (1u << 6u)
/* D3D-FS1: the broker scans out RGBA8888 (DCP 'RGBA') as well as BGRA8888 */
#define APPLE_AGX_SCANOUT_CAP_RGBA8888 (1u << 7u)
/* rc106 (m1n1 codex/m1n1-scaled, absent from 0d3a75b3): the broker accepts
 * a source smaller than the panel and DCP scales it into the destination
 * rectangle (iomfb swap destination[0]).  The guest writes
 * DEST_ORIGIN/DEST_SIZE and a non-native WIDTH/HEIGHT/STRIDE/SURFACE_SIZE
 * only when this bit is set. */
#define APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE (1u << 8u)
#define APPLE_AGX_SCANOUT_REQUIRED_CAPABILITIES                              \
  (APPLE_AGX_SCANOUT_CAP_FIXED_J313 | APPLE_AGX_SCANOUT_CAP_BGRA8888 |       \
   APPLE_AGX_SCANOUT_CAP_REGISTERED_POOL |                                   \
   APPLE_AGX_SCANOUT_CAP_APPLIED_RECEIPT)
#define APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES                            \
  (APPLE_AGX_SCANOUT_REQUIRED_CAPABILITIES |                                 \
   APPLE_AGX_SCANOUT_CAP_REPEATED_PRESENT |                                  \
   APPLE_AGX_SCANOUT_CAP_LATCHED_RECEIPT | APPLE_AGX_SCANOUT_CAP_LATCHED_IRQ)
/* S2 ABI V3 (m1n1 gpu/s2-broker-v3, src/hv_agx_scanout_broker.h; values
 * only).  REG_ABI_VERSION stays 2: V3 is this optional capability.  Every
 * register at or above 0xa8 exists only on a broker that advertises it; a
 * broker without the bit refuses those accesses, so this client touches
 * them only after the bit was read set (AppleAgxScanoutTwoLayersAdvertised)
 * and, past REG_NEGOTIATED, only after the negotiation read back. */
#define APPLE_AGX_SCANOUT_CAP_TWO_LAYERS (1u << 9u)
#define APPLE_AGX_SCANOUT_V3_MMIO_SIZE 0x140u
#define APPLE_AGX_SCANOUT_V3_LAYERS 2u
#define APPLE_AGX_SCANOUT_V3_LAYER_RECORD_SIZE 0x40u
/* DCP limits the V3 broker enforces: destination at least 32x32 on the
 * panel, source/destination within 2x either way. */
#define APPLE_AGX_SCANOUT_V3_MIN_EXTENT 32u
#define APPLE_AGX_SCANOUT_V3_MAX_SCALE 2u
/* Layer record FLAGS: bit 0 = premultiplied alpha; every other bit must
 * be zero. */
#define APPLE_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED (1u << 0u)
#define APPLE_AGX_SCANOUT_LAYER_FLAGS_VALID                                  \
  APPLE_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED
/* REG_ACTIVE_LAYERS bits. */
#define APPLE_AGX_SCANOUT_ACTIVE_SLOT0 (1u << 0u)
#define APPLE_AGX_SCANOUT_ACTIVE_SLOT1 (1u << 1u)
/* X | Y << 16 and Width | Height << 16, as the broker packs them. */
#define APPLE_AGX_SCANOUT_PACK(Low, High)                                    \
  (((APPLE_AGX_SCANOUT_U32)(Low) & 0xffffu) |                                \
   (((APPLE_AGX_SCANOUT_U32)(High) & 0xffffu) << 16u))

#define APPLE_AGX_SCANOUT_J313_WIDTH 2560u
#define APPLE_AGX_SCANOUT_J313_HEIGHT 1600u
#define APPLE_AGX_SCANOUT_J313_STRIDE 10240u
#define APPLE_AGX_SCANOUT_J313_SURFACE_SIZE 0xfa0000ULL
/* M2-f2c: the pool is segment 2 of the receipt-sized reserve; this is its
 * J313 maximum (a 1 GiB reserve less the private span and backend arena). */
#define APPLE_AGX_SCANOUT_J313_POOL_MAX_SIZE 0x3b800000ULL
#define APPLE_AGX_SCANOUT_ALIGNMENT 0x10000ULL
#define APPLE_AGX_SCANOUT_FORMAT_BGRA8888 1u
#define APPLE_AGX_SCANOUT_FORMAT_RGBA8888 2u
#define APPLE_AGX_SCANOUT_IRQ_LATCHED (1u << 0u)
#define APPLE_AGX_SCANOUT_IRQ_ERROR (1u << 1u)
#define APPLE_AGX_SCANOUT_IRQ_MASK                                             \
  (APPLE_AGX_SCANOUT_IRQ_LATCHED | APPLE_AGX_SCANOUT_IRQ_ERROR)

typedef enum _APPLE_AGX_SCANOUT_REGISTER {
  APPLE_AGX_SCANOUT_REG_MAGIC = 0x00u,
  APPLE_AGX_SCANOUT_REG_ABI_VERSION = 0x04u,
  APPLE_AGX_SCANOUT_REG_CAPABILITIES = 0x08u,
  APPLE_AGX_SCANOUT_REG_STATE = 0x0cu,
  APPLE_AGX_SCANOUT_REG_RESULT = 0x10u,
  APPLE_AGX_SCANOUT_REG_IRQ_STATUS = 0x14u,
  APPLE_AGX_SCANOUT_REG_IRQ_ENABLE = 0x18u,
  APPLE_AGX_SCANOUT_REG_RECEIPT_SEQUENCE = 0x20u,
  APPLE_AGX_SCANOUT_REG_APPLIED_SEQUENCE = 0x28u,
  APPLE_AGX_SCANOUT_REG_LATCHED_SEQUENCE = 0x30u,
  APPLE_AGX_SCANOUT_REG_ACTIVE_OFFSET = 0x38u,
  APPLE_AGX_SCANOUT_REG_POOL_PA = 0x40u,
  APPLE_AGX_SCANOUT_REG_SWAP_ID = 0x48u,
  APPLE_AGX_SCANOUT_REG_ACCEPTED_REQUESTS = 0x50u,
  APPLE_AGX_SCANOUT_REG_REJECTED_REQUESTS = 0x58u,
  APPLE_AGX_SCANOUT_REG_POOL_IPA = 0x60u,
  APPLE_AGX_SCANOUT_REG_POOL_SIZE = 0x68u,
  APPLE_AGX_SCANOUT_REG_SURFACE_OFFSET = 0x70u,
  APPLE_AGX_SCANOUT_REG_SURFACE_SIZE = 0x78u,
  APPLE_AGX_SCANOUT_REG_WIDTH = 0x80u,
  APPLE_AGX_SCANOUT_REG_HEIGHT = 0x84u,
  APPLE_AGX_SCANOUT_REG_STRIDE = 0x88u,
  APPLE_AGX_SCANOUT_REG_FORMAT = 0x8cu,
  APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE = 0x90u,
  APPLE_AGX_SCANOUT_REG_COMMAND = 0x98u,
  /* CAP_SCALED_SOURCE only: X | Y << 16 and Width | Height << 16 of the
   * panel rectangle the source is scaled into (MMIO size 0xa8). */
  APPLE_AGX_SCANOUT_REG_DEST_ORIGIN = 0xa0u,
  APPLE_AGX_SCANOUT_REG_DEST_SIZE = 0xa4u,
  /* ---- V3: CAP_TWO_LAYERS only ---- */
  /* u32 rw: the owner writes CAP_TWO_LAYERS while UNREGISTERED and reads it
   * back; any other write is ignored.  A successful RELEASE clears it. */
  APPLE_AGX_SCANOUT_REG_NEGOTIATED = 0xa8u,
  /* u32 rw: records in the next COMMIT_LAYERS, 1 or 2. */
  APPLE_AGX_SCANOUT_REG_LAYER_COUNT = 0xacu,
  /* Record 0 = DCP slot 0 (bottom), record 1 = DCP slot 1 (top). */
  APPLE_AGX_SCANOUT_REG_LAYER0 = 0xb0u,
  APPLE_AGX_SCANOUT_REG_LAYER1 = 0xf0u,
  /* u32 ro: APPLE_AGX_SCANOUT_ACTIVE_SLOT* (bit 1: slot 1 may be shown). */
  APPLE_AGX_SCANOUT_REG_ACTIVE_LAYERS = 0x130u,
  /* u64 ro: slot-1 surface offset of the applied two-layer commit. */
  APPLE_AGX_SCANOUT_REG_ACTIVE_OFFSET_1 = 0x138u,
} APPLE_AGX_SCANOUT_REGISTER;

/* Field offsets inside one V3 layer record (little-endian). */
typedef enum _APPLE_AGX_SCANOUT_LAYER_FIELD {
  APPLE_AGX_SCANOUT_LAYER_SURFACE_OFFSET = 0x00u, /* u64, 64 KiB aligned */
  APPLE_AGX_SCANOUT_LAYER_SURFACE_SIZE = 0x08u,   /* u64, Stride * Height */
  APPLE_AGX_SCANOUT_LAYER_WIDTH = 0x10u,          /* u32 */
  APPLE_AGX_SCANOUT_LAYER_HEIGHT = 0x14u,         /* u32 */
  APPLE_AGX_SCANOUT_LAYER_STRIDE = 0x18u,         /* u32 */
  APPLE_AGX_SCANOUT_LAYER_FORMAT = 0x1cu,         /* u32 FORMAT_* */
  APPLE_AGX_SCANOUT_LAYER_SRC_ORIGIN = 0x20u,     /* u32 X | Y << 16 */
  APPLE_AGX_SCANOUT_LAYER_SRC_SIZE = 0x24u,       /* u32 W | H << 16 */
  APPLE_AGX_SCANOUT_LAYER_DST_ORIGIN = 0x28u,     /* u32 X | Y << 16 */
  APPLE_AGX_SCANOUT_LAYER_DST_SIZE = 0x2cu,       /* u32 W | H << 16 */
  APPLE_AGX_SCANOUT_LAYER_FLAGS = 0x30u,          /* u32 LAYER_FLAG_* */
  APPLE_AGX_SCANOUT_LAYER_RESERVED0 = 0x34u,      /* u32, zero */
  APPLE_AGX_SCANOUT_LAYER_RESERVED1 = 0x38u,      /* u64, zero */
} APPLE_AGX_SCANOUT_LAYER_FIELD;

typedef enum _APPLE_AGX_SCANOUT_COMMAND {
  APPLE_AGX_SCANOUT_CMD_QUERY = 0u,
  APPLE_AGX_SCANOUT_CMD_REGISTER_POOL = 1u,
  APPLE_AGX_SCANOUT_CMD_PRESENT = 2u,
  APPLE_AGX_SCANOUT_CMD_RELEASE = 3u,
  /* V3: one DCP swap with LAYER_COUNT records; completion is the V2
   * receipt and latch of the commit, once for the whole commit. */
  APPLE_AGX_SCANOUT_CMD_COMMIT_LAYERS = 4u,
} APPLE_AGX_SCANOUT_COMMAND;

typedef enum _APPLE_AGX_SCANOUT_STATE {
  APPLE_AGX_SCANOUT_STATE_UNREGISTERED = 0u,
  APPLE_AGX_SCANOUT_STATE_READY = 1u,
  APPLE_AGX_SCANOUT_STATE_PENDING = 2u,
  APPLE_AGX_SCANOUT_STATE_ACTIVE = 3u,
  APPLE_AGX_SCANOUT_STATE_QUIESCING = 4u,
  APPLE_AGX_SCANOUT_STATE_FAILED = 5u,
} APPLE_AGX_SCANOUT_STATE;

typedef enum _APPLE_AGX_SCANOUT_BROKER_RESULT {
  APPLE_AGX_SCANOUT_BROKER_OK = 0u,
  APPLE_AGX_SCANOUT_BROKER_INVALID_COMMAND = 1u,
  APPLE_AGX_SCANOUT_BROKER_STALE_SEQUENCE = 2u,
  APPLE_AGX_SCANOUT_BROKER_BUSY = 3u,
  APPLE_AGX_SCANOUT_BROKER_INVALID_POOL = 4u,
  APPLE_AGX_SCANOUT_BROKER_UNMAPPED = 5u,
  APPLE_AGX_SCANOUT_BROKER_NOT_RAM = 6u,
  APPLE_AGX_SCANOUT_BROKER_NONCONTIGUOUS = 7u,
  APPLE_AGX_SCANOUT_BROKER_INVALID_SURFACE = 8u,
  APPLE_AGX_SCANOUT_BROKER_MAP_FAILED = 9u,
  APPLE_AGX_SCANOUT_BROKER_PRESENT_FAILED = 10u,
  APPLE_AGX_SCANOUT_BROKER_NOT_QUIESCED = 11u,
  /* V3 refusals; all terminal (RECEIPT_SEQUENCE takes the commit). */
  APPLE_AGX_SCANOUT_BROKER_LAYERS_NOT_NEGOTIATED = 12u,
  APPLE_AGX_SCANOUT_BROKER_LAYER_COUNT = 13u,
  APPLE_AGX_SCANOUT_BROKER_LAYER_RESERVED = 14u,
  APPLE_AGX_SCANOUT_BROKER_LAYER_FORMAT = 15u,
  APPLE_AGX_SCANOUT_BROKER_LAYER_GEOMETRY = 16u,
  APPLE_AGX_SCANOUT_BROKER_LAYER_STRIDE = 17u,
  APPLE_AGX_SCANOUT_BROKER_LAYER_OUTSIDE_POOL = 18u,
  APPLE_AGX_SCANOUT_BROKER_LAYER_SOURCE_RECT = 19u,
  APPLE_AGX_SCANOUT_BROKER_LAYER_DEST_RECT = 20u,
  APPLE_AGX_SCANOUT_BROKER_LAYER_SCALE = 21u,
  APPLE_AGX_SCANOUT_BROKER_LAYER_SAME_SURFACE = 22u,
  APPLE_AGX_SCANOUT_BROKER_LAYER_OVERLAP = 23u,
  APPLE_AGX_SCANOUT_BROKER_SLOT1_LATCHED = 24u,
} APPLE_AGX_SCANOUT_BROKER_RESULT;

typedef enum _APPLE_AGX_SCANOUT_RESULT {
  AppleAgxScanoutOk = 0,
  AppleAgxScanoutInvalidArgument,
  AppleAgxScanoutNotQualified,
  AppleAgxScanoutIncompatibleMagic,
  AppleAgxScanoutIncompatibleVersion,
  AppleAgxScanoutMissingCapabilities,
  AppleAgxScanoutTransportFailed,
  AppleAgxScanoutTimeout,
  AppleAgxScanoutClockRegression,
  AppleAgxScanoutPollLimit,
  AppleAgxScanoutStaleSequence,
  AppleAgxScanoutBrokerRejected,
  AppleAgxScanoutNotQuiesced,
  AppleAgxScanoutInconsistentReceipt,
  AppleAgxScanoutSequenceExhausted,
  AppleAgxScanoutBusy,
  AppleAgxScanoutNoInterrupt,
  /* V3: CAP_TWO_LAYERS was not negotiated (no V3 register was touched by
   * the call that returned it, except the negotiation read-back). */
  AppleAgxScanoutNotNegotiated,
} APPLE_AGX_SCANOUT_RESULT;

/* One V3 layer record as the client writes it. */
typedef struct _APPLE_AGX_SCANOUT_LAYER {
  APPLE_AGX_SCANOUT_U64 SurfaceOffset;
  APPLE_AGX_SCANOUT_U64 SurfaceSize;
  APPLE_AGX_SCANOUT_U32 Width;
  APPLE_AGX_SCANOUT_U32 Height;
  APPLE_AGX_SCANOUT_U32 Stride;
  APPLE_AGX_SCANOUT_U32 Format;
  APPLE_AGX_SCANOUT_U32 SrcOrigin;
  APPLE_AGX_SCANOUT_U32 SrcSize;
  APPLE_AGX_SCANOUT_U32 DstOrigin;
  APPLE_AGX_SCANOUT_U32 DstSize;
  APPLE_AGX_SCANOUT_U32 Flags;
} APPLE_AGX_SCANOUT_LAYER;

typedef struct _APPLE_AGX_SCANOUT_IO {
  void *Context;
  APPLE_AGX_SCANOUT_U64 (*NowMs)(void *Context);
  APPLE_AGX_SCANOUT_BOOL (*Read32)(void *Context, APPLE_AGX_SCANOUT_U32 Offset,
                                   APPLE_AGX_SCANOUT_U32 *Value);
  APPLE_AGX_SCANOUT_BOOL (*Read64)(void *Context, APPLE_AGX_SCANOUT_U32 Offset,
                                   APPLE_AGX_SCANOUT_U64 *Value);
  APPLE_AGX_SCANOUT_BOOL (*Write32)(void *Context, APPLE_AGX_SCANOUT_U32 Offset,
                                    APPLE_AGX_SCANOUT_U32 Value);
  APPLE_AGX_SCANOUT_BOOL (*Write64)(void *Context, APPLE_AGX_SCANOUT_U32 Offset,
                                    APPLE_AGX_SCANOUT_U64 Value);
  APPLE_AGX_SCANOUT_BOOL (*Pause)(void *Context);
} APPLE_AGX_SCANOUT_IO;

typedef struct _APPLE_AGX_SCANOUT_CLIENT {
  APPLE_AGX_SCANOUT_IO Io;
  APPLE_AGX_SCANOUT_U64 NextSequence;
  APPLE_AGX_SCANOUT_U32 MaxPolls;
  APPLE_AGX_SCANOUT_U32 LastBrokerResult;
  APPLE_AGX_SCANOUT_U32 AbiVersion;
  APPLE_AGX_SCANOUT_U32 Capabilities;
  APPLE_AGX_SCANOUT_U64 PendingPresentSequence;
  /* Bytes of the registered pool; surfaces must lie inside it. */
  APPLE_AGX_SCANOUT_U64 PoolBytes;
  APPLE_AGX_SCANOUT_BOOL Qualified;
  APPLE_AGX_SCANOUT_BOOL PresentPending;
  /* D3D-FS1: format each present carries (the committed mode's) */
  APPLE_AGX_SCANOUT_U32 PresentFormat;
  /* rc106: geometry each present carries (the committed mode's); native
   * J313 unless CAP_SCALED_SOURCE admitted a smaller source. */
  APPLE_AGX_SCANOUT_U32 PresentWidth;
  APPLE_AGX_SCANOUT_U32 PresentHeight;
  APPLE_AGX_SCANOUT_U32 PresentStride;
  APPLE_AGX_SCANOUT_U64 PresentSurfaceBytes;
  APPLE_AGX_SCANOUT_U32 PresentDestOrigin;
  APPLE_AGX_SCANOUT_U32 PresentDestSize;
  /* V3: TRUE only after REG_NEGOTIATED read back CAP_TWO_LAYERS; cleared
   * by a successful RELEASE (the broker clears its register then). */
  APPLE_AGX_SCANOUT_BOOL TwoLayers;
} APPLE_AGX_SCANOUT_CLIENT;

APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutInitialize(
    APPLE_AGX_SCANOUT_CLIENT *Client, const APPLE_AGX_SCANOUT_IO *Io,
    APPLE_AGX_SCANOUT_U64 FirstSequence, APPLE_AGX_SCANOUT_U32 MaxPolls);
APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutQualify(
    APPLE_AGX_SCANOUT_CLIENT *Client);
APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutRegisterPool(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U64 PoolIpa,
    APPLE_AGX_SCANOUT_U64 PoolBytes, APPLE_AGX_SCANOUT_U64 DeadlineMs);
APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutPresentApplied(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U64 SurfaceOffset,
    APPLE_AGX_SCANOUT_U64 DeadlineMs, APPLE_AGX_SCANOUT_U32 *SwapId);
APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutQueuePresent(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U64 SurfaceOffset,
    APPLE_AGX_SCANOUT_U64 *Sequence);
APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutEnableInterrupts(
    APPLE_AGX_SCANOUT_CLIENT *Client);
APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutDisableInterrupts(
    APPLE_AGX_SCANOUT_CLIENT *Client);
APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutConsumeInterrupt(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U32 *IrqStatus,
    APPLE_AGX_SCANOUT_U64 *LatchedSequence);
/* V3.  Advertised: qualified V2 broker with the V2 present capabilities
 * and CAP_TWO_LAYERS in the capabilities Qualify read; no MMIO. */
APPLE_AGX_SCANOUT_BOOL AppleAgxScanoutTwoLayersAdvertised(
    const APPLE_AGX_SCANOUT_CLIENT *Client);
/* Write REG_NEGOTIATED = CAP_TWO_LAYERS and read it back.  Call after
 * Qualify and before RegisterPool (the broker takes it only while
 * UNREGISTERED).  MissingCapabilities without any MMIO when the bit is not
 * advertised; NotNegotiated when the read-back differs. */
APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutNegotiateTwoLayers(
    APPLE_AGX_SCANOUT_CLIENT *Client);
/* Stage Count (1 or 2) records, LAYER_COUNT, the sequence and command
 * COMMIT_LAYERS, then read RESULT and RECEIPT_SEQUENCE once.  A terminal
 * refusal returns BrokerRejected with LastBrokerResult = the refusal code;
 * BUSY returns Busy.  On Ok the commit is pending until its latch
 * (ConsumeInterrupt), exactly like QueuePresent; one commit or present is
 * outstanding at a time. */
APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutQueueCommitLayers(
    APPLE_AGX_SCANOUT_CLIENT *Client, const APPLE_AGX_SCANOUT_LAYER *Layers,
    APPLE_AGX_SCANOUT_U32 Count, APPLE_AGX_SCANOUT_U64 *Sequence);
/* RQ-1: after IRQ_ERROR, read RESULT and RECEIPT_SEQUENCE once.  When a
 * present or commit is pending, RECEIPT is its sequence and RESULT is not OK
 * (a synchronous failure after acceptance, or the broker's latch deadline),
 * the client is released (PresentPending FALSE) and Ok returns the failed
 * sequence and the result; any other combination returns NoInterrupt and
 * changes nothing (a RELEASE failure, a stale receipt, an acceptance). */
APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutTakePresentFailure(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U64 *Sequence,
    APPLE_AGX_SCANOUT_U32 *BrokerResult);
/* REG_ACTIVE_LAYERS; NotNegotiated without MMIO unless negotiated. */
APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutReadActiveLayers(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U32 *ActiveLayers);
APPLE_AGX_SCANOUT_RESULT AppleAgxScanoutRelease(
    APPLE_AGX_SCANOUT_CLIENT *Client, APPLE_AGX_SCANOUT_U64 DeadlineMs);

#endif /* APPLE_AGX_SCANOUT_H */
