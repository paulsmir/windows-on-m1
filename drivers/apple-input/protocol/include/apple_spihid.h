#ifndef APPLE_SPIHID_H
#define APPLE_SPIHID_H

#ifdef AI_KERNEL_MODE
#include <ntddk.h>
#ifndef AI_KERNEL_FIXED_WIDTH_TYPES
#define AI_KERNEL_FIXED_WIDTH_TYPES
typedef UCHAR uint8_t;
typedef USHORT uint16_t;
typedef ULONG uint32_t;
typedef ULONGLONG uint64_t;
#endif
#ifndef AI_KERNEL_BOOL_TYPE
#define AI_KERNEL_BOOL_TYPE
typedef unsigned char bool;
#define true 1
#define false 0
#endif
#define AI_MEMCPY RtlCopyMemory
#define AI_MEMSET RtlZeroMemory
#define AI_UINT16_MAX MAXUSHORT
#define AI_UINT64_MAX MAXULONGLONG
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define AI_MEMCPY memcpy
#define AI_MEMSET(ptr, size) memset((ptr), 0, (size))
#define AI_UINT16_MAX UINT16_MAX
#define AI_UINT64_MAX UINT64_MAX
#endif

#define AI_PACKET_SIZE 256u
#define AI_PACKET_DATA_SIZE 246u
#define AI_MAX_MESSAGE_SIZE 2048u
#define AI_DESCRIPTOR_MAX 512u
#define AI_HID_REPORT_ID_CAPACITY 256u
#define AI_PACKET_READ 0x20u
#define AI_PACKET_WRITE 0x40u

enum ai_status {
    AI_OK = 0,
    AI_COMPLETE = 1,
    AI_ERR_ARGUMENT = -1,
    AI_ERR_CRC = -2,
    AI_ERR_LENGTH = -3,
    AI_ERR_SEQUENCE = -4,
    AI_ERR_TIMEOUT = -5,
    AI_ERR_PROTOCOL = -6,
};

enum ai_discovery_phase {
    AI_DISCOVERY_IDLE,
    AI_DISCOVERY_WAIT_BOOT,
    AI_DISCOVERY_IDENTITY,
    AI_DISCOVERY_INTERFACE_MANAGEMENT,
    AI_DISCOVERY_INTERFACE_KEYBOARD,
    AI_DISCOVERY_INTERFACE_TRACKPAD,
    AI_DISCOVERY_KEYBOARD_DESCRIPTOR,
    AI_DISCOVERY_TRACKPAD_DESCRIPTOR,
    AI_DISCOVERY_READY,
    AI_DISCOVERY_OFFLINE,
};

enum ai_trackpad_init_phase {
    AI_TRACKPAD_INIT_IDLE,
    AI_TRACKPAD_INIT_DIMENSIONS,
    AI_TRACKPAD_INIT_MULTITOUCH,
    AI_TRACKPAD_INIT_READY,
    AI_TRACKPAD_INIT_OFFLINE,
};

struct ai_trackpad_dimensions {
    uint32_t width_hundredths_mm;
    uint32_t height_hundredths_mm;
    int32_t min_x;
    int32_t min_y;
    int32_t max_x;
    int32_t max_y;
    bool valid;
};

struct ai_trackpad_init {
    enum ai_trackpad_init_phase phase;
    uint8_t retry_count;
    uint8_t retry_limit;
    uint8_t message_id;
    uint64_t deadline_us;
    struct ai_trackpad_dimensions dimensions;
};

struct ai_discovery {
    enum ai_discovery_phase phase;
    uint8_t retry_count;
    uint8_t retry_limit;
    uint32_t request_id;
    uint64_t deadline_us;
};

struct ai_discovery_request {
    uint8_t target;
    uint8_t type;
    uint8_t report;
    uint8_t device;
    uint16_t response_length;
};

struct ai_packet_view {
    uint8_t flags;
    uint8_t device;
    uint16_t offset;
    uint16_t remaining;
    uint16_t length;
    const uint8_t *data;
};

struct ai_message_view {
    uint8_t flags;
    uint8_t device;
    const uint8_t *data;
    size_t length;
};

struct ai_protocol_message {
    uint8_t type;
    uint8_t report;
    uint8_t device;
    uint8_t id;
    uint16_t response_length;
    const uint8_t *payload;
    uint16_t payload_length;
};

struct ai_reassembler {
    bool active;
    uint8_t flags;
    uint8_t device;
    size_t used;
    size_t total;
    uint8_t data[AI_MAX_MESSAGE_SIZE];
};

struct ai_descriptor_slot {
    bool valid;
    uint8_t device;
    uint16_t length;
    uint8_t bytes[AI_DESCRIPTOR_MAX];
};

struct ai_descriptor_store {
    struct ai_descriptor_slot keyboard;
    struct ai_descriptor_slot trackpad;
};

struct ai_hid_input_contract {
    bool valid;
    bool uses_report_ids;
    uint16_t bytes_by_id[AI_HID_REPORT_ID_CAPACITY];
};

struct ai_spi_transfer_plan {
    uint16_t clock_divider;
    uint16_t words;
    uint8_t bytes_per_word;
    bool poll;
};

enum ai_spi_register_op_kind {
    AI_SPI_REGISTER_WRITE,
    AI_SPI_REGISTER_MASK,
};

struct ai_spi_register_op {
    enum ai_spi_register_op_kind kind;
    uint16_t offset;
    uint32_t clear_mask;
    uint32_t set_value;
};

/*
 * Interrupt-to-worker handoff shared by the portable tests and the KMDF
 * transport.  The interrupt path never performs SPI: it only marks one unit
 * of work pending.  Additional edges are coalesced until the passive worker
 * has drained the controller and sampled the level-triggered GPIO again.
 */
struct ai_transport_queue {
    bool pending;
    bool worker_active;
    uint32_t coalesced_irqs;
};

uint16_t ai_crc16_usb(uint16_t seed, const void *data, size_t size);
enum ai_status ai_packet_decode(const uint8_t raw[AI_PACKET_SIZE], struct ai_packet_view *out);
enum ai_status ai_message_decode(const uint8_t *raw, size_t size,
                                 struct ai_protocol_message *out);
void ai_reassembler_reset(struct ai_reassembler *state);
enum ai_status ai_reassembler_push(struct ai_reassembler *state,
                                   const struct ai_packet_view *packet,
                                   struct ai_message_view *message);
void ai_discovery_start(struct ai_discovery *state, uint64_t now_us,
                        uint64_t timeout_us, uint8_t retry_limit);
enum ai_status ai_discovery_accept(struct ai_discovery *state, uint32_t request_id,
                                   bool response_valid, uint64_t now_us,
                                   uint64_t timeout_us);
enum ai_status ai_discovery_accept_boot(struct ai_discovery *state,
                                        const uint8_t *marker, size_t size,
                                        uint64_t now_us, uint64_t timeout_us);
enum ai_status ai_discovery_poll(struct ai_discovery *state, uint64_t now_us,
                                 uint64_t timeout_us);
enum ai_status ai_discovery_request_for_phase(enum ai_discovery_phase phase,
                                              struct ai_discovery_request *out);
bool ai_discovery_response_matches(enum ai_discovery_phase phase,
                                   const struct ai_message_view *wire,
                                   const struct ai_protocol_message *message);
enum ai_status ai_discovery_request_encode(const struct ai_discovery_request *request,
                                           uint8_t message_id,
                                           uint8_t raw[AI_PACKET_SIZE]);
enum ai_status ai_trackpad_init_request_encode(
    enum ai_trackpad_init_phase phase, uint8_t message_id,
    uint8_t raw[AI_PACKET_SIZE]);
enum ai_status ai_trackpad_dimensions_parse(
    const uint8_t *report, size_t length,
    struct ai_trackpad_dimensions *out);
void ai_trackpad_init_start(struct ai_trackpad_init *state,
                            uint8_t message_id, uint64_t now_us,
                            uint64_t timeout_us, uint8_t retry_limit);
bool ai_trackpad_init_response_matches(
    const struct ai_trackpad_init *state,
    const struct ai_message_view *wire,
    const struct ai_protocol_message *message);
enum ai_status ai_trackpad_init_accept(
    struct ai_trackpad_init *state, const struct ai_message_view *wire,
    const struct ai_protocol_message *message, uint64_t now_us,
    uint64_t timeout_us);
enum ai_status ai_trackpad_init_poll(struct ai_trackpad_init *state,
                                     uint64_t now_us,
                                     uint64_t timeout_us);
bool ai_write_status_valid(const uint8_t *status, size_t size);
enum ai_status ai_spi_plan_transfer(uint32_t reference_hz, uint32_t target_hz,
                                    uint8_t bits_per_word, size_t byte_length,
                                    struct ai_spi_transfer_plan *out);
size_t ai_spi_init_plan(struct ai_spi_register_op *out, size_t capacity);
void ai_transport_queue_reset(struct ai_transport_queue *queue);
bool ai_transport_irq(struct ai_transport_queue *queue);
bool ai_transport_worker_begin(struct ai_transport_queue *queue);
bool ai_transport_worker_complete(struct ai_transport_queue *queue,
                                  bool interrupt_asserted);
void ai_descriptor_store_reset(struct ai_descriptor_store *store);
enum ai_status ai_descriptor_store_put(struct ai_descriptor_store *store,
                                       uint8_t device,
                                       const uint8_t *bytes,
                                       size_t length);
const struct ai_descriptor_slot *ai_descriptor_store_get(
    const struct ai_descriptor_store *store, uint8_t device);
enum ai_status ai_hid_input_contract_parse(
    const uint8_t *descriptor, size_t length,
    struct ai_hid_input_contract *out);
bool ai_hid_input_report_valid(const struct ai_hid_input_contract *contract,
                               const uint8_t *report, size_t length,
                               uint8_t *report_id);

#endif
