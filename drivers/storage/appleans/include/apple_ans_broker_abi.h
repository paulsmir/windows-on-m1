/* Generated; edit config/j313-ans.json and apple-ans-broker-abi.json. */
/* Source SHA-256: d2d3f1018aa37c5db03a5f860dd19f56a4adc860b153ad73cc86cee8cab2f651 */
#ifndef APPLE_ANS_BROKER_ABI_H
#define APPLE_ANS_BROKER_ABI_H
#include <stdint.h>
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error ANS ABI requires little endian
#endif
#define ANS_MAGIC 0x31534e41
#define ANS_ABI_MAJOR 1
#define ANS_ABI_MINOR 0
#define ANS_QUEUE_DEPTH 64
#define ANS_ADMIN_TAGS 1
#define ANS_MAX_TRANSFER 131072
#define ANS_PRP_PAGE_SIZE 4096
#define ANS_MAX_DATA_PAGES 33
#define ANS_BROKER_BASE 0x300010000ULL
#define ANS_BROKER_SIZE 16384
#define ANS_GUEST_GSIV 889
#define ANS_FEATURE_READ_ONLY 1
#define ANS_DMA_NONE 0
#define ANS_DMA_FROM_DEVICE 1
#define ANS_DMA_TO_DEVICE 2
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
    uint32_t magic;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t size;
    uint32_t features;
    uint64_t generation;
    uint64_t sequence;
    uint32_t state;
    uint32_t last_error;
    uint32_t controller_generation;
    uint32_t reserved;
};
typedef char ans_broker_header_size_check[(sizeof(struct ans_broker_header) == 48) ? 1 : -1];
struct ans_broker_request {
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t size;
    uint64_t generation;
    uint64_t sequence;
    uint32_t operation;
    uint16_t queue;
    uint16_t tag;
    uint32_t transfer_length;
    uint32_t direction;
    uint64_t reserved;
    uint8_t sqe[64];
};
typedef char ans_broker_request_size_check[(sizeof(struct ans_broker_request) == 112) ? 1 : -1];
struct ans_broker_response {
    uint64_t generation;
    uint64_t sequence;
    uint32_t result;
    uint32_t state;
    uint16_t queue;
    uint16_t tag;
    uint32_t reserved;
    uint8_t cqe[16];
};
typedef char ans_broker_response_size_check[(sizeof(struct ans_broker_response) == 48) ? 1 : -1];
#endif
