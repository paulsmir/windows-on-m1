/* Generated; edit config/j313-ans.json and apple-ans-broker-abi.json. */
/* Source SHA-256: 97981550cd21af85a13c7400c82c0e876ab8b6d99fbb1d5535280db07b44a31c */
#ifndef APPLE_ANS_BROKER_ABI_H
#define APPLE_ANS_BROKER_ABI_H
#if defined(ANS_BROKER_UEFI)
#include <Uefi.h>
typedef UINT8 ans_u8; typedef UINT16 ans_u16; typedef UINT32 ans_u32; typedef UINT64 ans_u64;
#elif defined(_KERNEL_MODE)
#include <ntddk.h>
typedef UCHAR ans_u8; typedef USHORT ans_u16; typedef ULONG ans_u32; typedef ULONGLONG ans_u64;
#else
#include <stdint.h>
typedef uint8_t ans_u8; typedef uint16_t ans_u16; typedef uint32_t ans_u32; typedef uint64_t ans_u64;
#endif
#define ANS_U64_MAX (~(ans_u64)0)
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error ANS ABI requires little endian
#endif
#define ANS_MAGIC 0x31534e41
#define ANS_ABI_MAJOR 1
#define ANS_ABI_MINOR 0
#define ANS_QUEUE_DEPTH 64
#define ANS_ADMIN_TAGS 2
#define ANS_MAX_TRANSFER 131072
#define ANS_PRP_PAGE_SIZE 4096
#define ANS_MAX_DATA_PAGES 33
#define ANS_BROKER_BASE 0x300010000ULL
#define ANS_BROKER_SIZE 16384
#define ANS_GUEST_GSIV 960
#define ANS_FEATURE_READ_ONLY 1
#define ANS_TELEMETRY_OFFSET 2304
#define ANS_TELEMETRY_MAGIC 0x31445441
#define ANS_FEATURE_TELEMETRY 2
#define ANS_FEATURE_WRITE_WINDOW 4
#define ANS_REQUEST_OFFSET 256
#define ANS_RESPONSE_OFFSET 512
#define ANS_DOORBELL_OFFSET 768
#define ANS_ABORT_OFFSET 776
#define ANS_PENDING_OFFSET 2048
#define ANS_COMPLETION_OFFSET 4096
#define ANS_HANDOFF_REQUEST_OFFSET 15872
#define ANS_HANDOFF_RECEIPT_OFFSET 16128
#define ANS_HANDOFF_REQUIRED_PROOFS 127
#define ANS_DMA_NONE 0
#define ANS_DMA_FROM_DEVICE 1
#define ANS_DMA_TO_DEVICE 2
#define ANS_EXPECTED_CONTROLLER_BASE 0x27bcc0000ULL
#define ANS_EXPECTED_CONTROLLER_SIZE 0x40000
#define ANS_EXPECTED_COPROCESSOR_BASE 0x277400000ULL
#define ANS_EXPECTED_COPROCESSOR_SIZE 0x4000
#define ANS_EXPECTED_SART_BASE 0x27bc50000ULL
#define ANS_EXPECTED_SART_SIZE 0x10000
#define ANS_EXPECTED_MAILBOX_BASE 0x277408000ULL
#define ANS_EXPECTED_MAILBOX_SIZE 0x4000
#define ANS_EXPECTED_PHYSICAL_IRQ 590
#define ANS_TELEM_STARTS (1ULL << 0)
#define ANS_TELEM_STOPS (1ULL << 1)
#define ANS_TELEM_RESETS (1ULL << 2)
#define ANS_TELEM_SUBMITTED (1ULL << 3)
#define ANS_TELEM_COMPLETED (1ULL << 4)
#define ANS_TELEM_READS (1ULL << 5)
#define ANS_TELEM_WRITES (1ULL << 6)
#define ANS_TELEM_FLUSHES (1ULL << 7)
#define ANS_TELEM_BYTES_READ (1ULL << 8)
#define ANS_TELEM_BYTES_WRITTEN (1ULL << 9)
#define ANS_TELEM_MAX_OUTSTANDING (1ULL << 10)
#define ANS_TELEM_CURRENT_OUTSTANDING (1ULL << 11)
#define ANS_TELEM_PHYSICAL_IRQS (1ULL << 12)
#define ANS_TELEM_GUEST_IRQ_INJECTIONS (1ULL << 13)
#define ANS_TELEM_ISR_ENTRIES (1ULL << 14)
#define ANS_TELEM_CQES (1ULL << 15)
#define ANS_TELEM_TIMEOUTS (1ULL << 16)
#define ANS_TELEM_POLL_RECOVERED_COMPLETIONS (1ULL << 17)
#define ANS_TELEM_INVALID_CQE (1ULL << 18)
#define ANS_TELEM_INVALID_TAG (1ULL << 19)
#define ANS_TELEM_BROKER_REJECTIONS (1ULL << 20)
#define ANS_TELEM_PRP_TRANSLATION_FAILURES (1ULL << 21)
#define ANS_TELEM_TCB_INVAL_FAILURES (1ULL << 22)
#define ANS_TELEM_SPURIOUS_IRQS (1ULL << 23)
enum ans_state {
    ANS_STATE_OFF = 0,
    ANS_STATE_BOOTING = 1,
    ANS_STATE_READY = 2,
    ANS_STATE_CLAIMED = 3,
    ANS_STATE_QUIESCING = 4,
    ANS_STATE_FAULTED = 5,
};
enum ans_op {
    ANS_OP_INVALID = 0,
    ANS_OP_QUERY = 1,
    ANS_OP_CLAIM = 2,
    ANS_OP_REGISTER_QUEUES = 3,
    ANS_OP_AUTHORIZE_SUBMIT = 4,
    ANS_OP_REAP_COMPLETION = 5,
    ANS_OP_QUIESCE = 6,
    ANS_OP_RESET = 7,
    ANS_OP_RELEASE = 8,
};
enum ans_tag_state {
    ANS_TAG_STATE_FREE = 0,
    ANS_TAG_STATE_PREPARED = 1,
    ANS_TAG_STATE_IN_FLIGHT = 2,
    ANS_TAG_STATE_COMPLETED = 3,
};
enum ans_result {
    ANS_RESULT_OK = 0,
    ANS_RESULT_BAD_ABI = 1,
    ANS_RESULT_STALE = 2,
    ANS_RESULT_BAD_SEQUENCE = 3,
    ANS_RESULT_BAD_STATE = 4,
    ANS_RESULT_BAD_TAG = 5,
    ANS_RESULT_BAD_QUEUE = 6,
    ANS_RESULT_BAD_COMMAND = 7,
    ANS_RESULT_BAD_PRP = 8,
    ANS_RESULT_DENIED = 9,
    ANS_RESULT_BUSY = 10,
    ANS_RESULT_IO_ERROR = 11,
};
struct ans_broker_header {
    ans_u32 magic;
    ans_u16 abi_major;
    ans_u16 abi_minor;
    ans_u32 size;
    ans_u32 features;
    ans_u64 generation;
    ans_u64 sequence;
    ans_u32 state;
    ans_u32 last_error;
    ans_u32 controller_generation;
    ans_u32 reserved;
};
typedef char ans_broker_header_size_check[(sizeof(struct ans_broker_header) == 48) ? 1 : -1];
struct ans_broker_request {
    ans_u16 abi_major;
    ans_u16 abi_minor;
    ans_u32 size;
    ans_u64 generation;
    ans_u64 sequence;
    ans_u32 operation;
    ans_u16 queue;
    ans_u16 tag;
    ans_u32 transfer_length;
    ans_u32 direction;
    ans_u64 reserved;
    ans_u8 sqe[64];
};
typedef char ans_broker_request_size_check[(sizeof(struct ans_broker_request) == 112) ? 1 : -1];
struct ans_broker_response {
    ans_u64 generation;
    ans_u64 sequence;
    ans_u32 result;
    ans_u32 state;
    ans_u16 queue;
    ans_u16 tag;
    ans_u32 reserved;
    ans_u8 cqe[16];
};
typedef char ans_broker_response_size_check[(sizeof(struct ans_broker_response) == 48) ? 1 : -1];
struct ans_handoff_receipt {
    ans_u32 magic;
    ans_u16 abi_major;
    ans_u16 abi_minor;
    ans_u32 size;
    ans_u32 result;
    ans_u64 sequence;
    ans_u64 previous_generation;
    ans_u64 generation;
    ans_u32 proof_flags;
    ans_u32 state;
    ans_u64 reserved[2];
};
typedef char ans_handoff_receipt_size_check[(sizeof(struct ans_handoff_receipt) == 64) ? 1 : -1];
struct ans_telemetry {
    ans_u32 magic;
    ans_u32 size;
    ans_u32 source;
    ans_u32 reserved;
    ans_u64 generation;
    ans_u64 valid_fields;
    ans_u64 starts;
    ans_u64 stops;
    ans_u64 resets;
    ans_u64 submitted;
    ans_u64 completed;
    ans_u64 reads;
    ans_u64 writes;
    ans_u64 flushes;
    ans_u64 bytes_read;
    ans_u64 bytes_written;
    ans_u64 max_outstanding;
    ans_u64 current_outstanding;
    ans_u64 physical_irqs;
    ans_u64 guest_irq_injections;
    ans_u64 isr_entries;
    ans_u64 cqes;
    ans_u64 timeouts;
    ans_u64 poll_recovered_completions;
    ans_u64 invalid_cqe;
    ans_u64 invalid_tag;
    ans_u64 broker_rejections;
    ans_u64 prp_translation_failures;
    ans_u64 tcb_inval_failures;
    ans_u64 spurious_irqs;
    ans_u32 last_stage;
    ans_u32 last_error;
};
typedef char ans_telemetry_size_check[(sizeof(struct ans_telemetry) == 232) ? 1 : -1];
static inline int ans_handoff_receipt_valid(const struct ans_handoff_receipt *r,
    ans_u64 sequence, ans_u64 previous_generation)
{
    return r && sequence && previous_generation != ANS_U64_MAX &&
        r->magic == ANS_MAGIC && r->abi_major == ANS_ABI_MAJOR &&
        r->abi_minor == ANS_ABI_MINOR && r->size == sizeof(*r) &&
        r->result == ANS_RESULT_OK && r->sequence == sequence &&
        r->previous_generation == previous_generation &&
        r->generation == previous_generation + 1 &&
        r->proof_flags == ANS_HANDOFF_REQUIRED_PROOFS &&
        r->state == ANS_STATE_READY && !r->reserved[0] && !r->reserved[1];
}
#endif
