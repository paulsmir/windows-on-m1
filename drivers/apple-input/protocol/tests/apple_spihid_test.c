#include "apple_spihid.h"

#include <assert.h>
#include <string.h>

static void put_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void seal_packet(uint8_t raw[AI_PACKET_SIZE])
{
    put_le16(raw + AI_PACKET_SIZE - 2, ai_crc16_usb(0, raw, AI_PACKET_SIZE - 2));
}

static size_t seal_message(uint8_t *raw, uint8_t type, uint8_t report,
                           uint8_t device, uint8_t id,
                           const uint8_t *payload, uint16_t payload_size)
{
    raw[0] = type;
    raw[1] = report;
    raw[2] = device;
    raw[3] = id;
    put_le16(raw + 4, payload_size);
    put_le16(raw + 6, payload_size);
    if (payload_size)
        memcpy(raw + 8, payload, payload_size);
    put_le16(raw + 8 + payload_size,
             ai_crc16_usb(0, raw, (size_t)8 + payload_size));
    return (size_t)10 + payload_size;
}

static void test_crc(void)
{
    static const char vector[] = "123456789";
    assert(ai_crc16_usb(0, vector, sizeof(vector) - 1) == 0xbb3d);
}

static void test_decode(void)
{
    uint8_t raw[AI_PACKET_SIZE] = {0};
    struct ai_packet_view packet;
    raw[0] = 0x20;
    raw[1] = 2;
    put_le16(raw + 2, 7);
    put_le16(raw + 4, 3);
    put_le16(raw + 6, 4);
    memcpy(raw + 8, "test", 4);
    seal_packet(raw);

    assert(ai_packet_decode(raw, &packet) == AI_OK);
    assert(packet.flags == 0x20 && packet.device == 2);
    assert(packet.offset == 7 && packet.remaining == 3 && packet.length == 4);
    assert(memcmp(packet.data, "test", 4) == 0);
    assert(ai_packet_decode(NULL, &packet) == AI_ERR_ARGUMENT);
    assert(ai_packet_decode(raw, NULL) == AI_ERR_ARGUMENT);
    raw[8] ^= 1;
    assert(ai_packet_decode(raw, &packet) == AI_ERR_CRC);
}

static void test_reassembly(void)
{
    struct ai_reassembler state = {0};
    struct ai_message_view message;
    static const uint8_t first[] = {'a', 'b', 'c'};
    static const uint8_t second[] = {'d', 'e'};
    struct ai_packet_view p1 = {0x20, 2, 0, 2, 3, first};
    struct ai_packet_view p2 = {0x20, 2, 3, 0, 2, second};

    assert(ai_reassembler_push(&state, &p1, &message) == AI_OK);
    assert(ai_reassembler_push(&state, &p2, &message) == AI_COMPLETE);
    assert(message.length == 5 && memcmp(message.data, "abcde", 5) == 0);

    ai_reassembler_reset(&state);
    assert(ai_reassembler_push(&state, &p1, &message) == AI_OK);
    p2.offset = 4;
    assert(ai_reassembler_push(&state, &p2, &message) == AI_ERR_SEQUENCE);

    struct ai_packet_view overflow = {0x20, 2, 0, 2048, 246, first};
    assert(ai_reassembler_push(&state, &overflow, &message) == AI_ERR_LENGTH);
}

static void test_message_decode(void)
{
    uint8_t raw[64] = {0};
    static const uint8_t payload[] = {0xaa, 0xbb, 0xcc};
    struct ai_protocol_message message;
    size_t size = seal_message(raw, 0x20, 0x02, 0x01, 7,
                               payload, sizeof(payload));

    assert(ai_message_decode(raw, size, &message) == AI_OK);
    assert(message.type == 0x20 && message.report == 0x02);
    assert(message.device == 0x01 && message.id == 7);
    assert(message.response_length == sizeof(payload));
    assert(message.payload_length == sizeof(payload));
    assert(memcmp(message.payload, payload, sizeof(payload)) == 0);
    assert(ai_message_decode(raw, size - 1, &message) == AI_ERR_LENGTH);
    raw[8] ^= 1;
    assert(ai_message_decode(raw, size, &message) == AI_ERR_CRC);
    assert(ai_message_decode(NULL, size, &message) == AI_ERR_ARGUMENT);
}

static void test_discovery(void)
{
    struct ai_discovery state;
    ai_discovery_start(&state, 100, 50, 2);
    assert(state.phase == AI_DISCOVERY_WAIT_BOOT);
    assert(state.request_id == 0 && state.deadline_us == 150);
    assert(ai_discovery_accept_boot(&state,
                                    (const uint8_t[]){0xa0, 0x80, 0x00, 0x00},
                                    4, 105, 50) == AI_OK);
    assert(state.phase == AI_DISCOVERY_IDENTITY);
    assert(state.request_id == 0 && state.deadline_us == 155);
    assert(ai_discovery_accept(&state, 99, true, 110, 50) == AI_ERR_SEQUENCE);
    assert(ai_discovery_accept(&state, 0, true, 110, 50) == AI_OK);
    assert(state.phase == AI_DISCOVERY_INTERFACE_MANAGEMENT && state.request_id == 1);
    assert(ai_discovery_accept(&state, 1, true, 120, 50) == AI_OK);
    assert(state.phase == AI_DISCOVERY_INTERFACE_KEYBOARD);
    assert(ai_discovery_accept(&state, 2, true, 130, 50) == AI_OK);
    assert(state.phase == AI_DISCOVERY_INTERFACE_TRACKPAD);
    assert(ai_discovery_accept(&state, 3, true, 140, 50) == AI_OK);
    assert(state.phase == AI_DISCOVERY_KEYBOARD_DESCRIPTOR);
    assert(ai_discovery_accept(&state, 4, true, 150, 50) == AI_OK);
    assert(state.phase == AI_DISCOVERY_TRACKPAD_DESCRIPTOR);
    assert(ai_discovery_accept(&state, 5, true, 160, 50) == AI_COMPLETE);
    assert(state.phase == AI_DISCOVERY_READY);

    ai_discovery_start(&state, 0, 10, 1);
    assert(ai_discovery_poll(&state, 9, 10) == AI_OK);
    assert(ai_discovery_poll(&state, 10, 10) == AI_ERR_TIMEOUT);
    assert(state.retry_count == 1 && state.request_id == 0);
    assert(ai_discovery_poll(&state, 20, 10) == AI_ERR_TIMEOUT);
    assert(state.phase == AI_DISCOVERY_OFFLINE);

    ai_discovery_start(&state, 0, 10, 0);
    assert(ai_discovery_accept_boot(&state,
                                    (const uint8_t[]){0xa0, 0x80, 0x00, 0x00},
                                    4, 1, 10) == AI_OK);
    assert(ai_discovery_accept(&state, 0, false, 2, 10) == AI_ERR_PROTOCOL);
    assert(state.phase == AI_DISCOVERY_OFFLINE);
}

static void test_boot_and_write_status_contract(void)
{
    struct ai_discovery state;
    static const uint8_t booted[] = {0xa0, 0x80, 0x00, 0x00};
    static const uint8_t not_booted[] = {0xa0, 0x80, 0x00, 0x01};
    static const uint8_t status_ok[] = {0xac, 0x27, 0x68, 0xd5};
    static const uint8_t status_bad[] = {0xac, 0x27, 0x68, 0x00};

    ai_discovery_start(&state, 10, 100, 2);
    assert(ai_discovery_accept_boot(&state, not_booted, sizeof(not_booted),
                                    20, 100) == AI_ERR_PROTOCOL);
    assert(state.phase == AI_DISCOVERY_WAIT_BOOT);
    assert(ai_discovery_accept_boot(&state, booted, sizeof(booted) - 1,
                                    20, 100) == AI_ERR_LENGTH);
    assert(state.phase == AI_DISCOVERY_WAIT_BOOT);
    assert(ai_discovery_accept_boot(&state, booted, sizeof(booted),
                                    20, 100) == AI_OK);
    assert(state.phase == AI_DISCOVERY_IDENTITY);
    assert(ai_discovery_accept_boot(&state, booted, sizeof(booted),
                                    21, 100) == AI_ERR_SEQUENCE);

    assert(ai_write_status_valid(status_ok, sizeof(status_ok)));
    assert(!ai_write_status_valid(status_bad, sizeof(status_bad)));
    assert(!ai_write_status_valid(status_ok, sizeof(status_ok) - 1));
    assert(!ai_write_status_valid(NULL, sizeof(status_ok)));
}

static void test_discovery_request_contract(void)
{
    struct ai_discovery_request request;

    assert(ai_discovery_request_for_phase(AI_DISCOVERY_IDENTITY, &request) == AI_OK);
    assert(request.target == 0xd0 && request.type == 0x20);
    assert(request.report == 0x01 && request.device == 0xd0);
    assert(request.response_length == 0);

    assert(ai_discovery_request_for_phase(AI_DISCOVERY_INTERFACE_MANAGEMENT,
                                          &request) == AI_OK);
    assert(request.target == 0xd0 && request.type == 0x20);
    assert(request.report == 0x02 && request.device == 0x00);
    assert(request.response_length == AI_DESCRIPTOR_MAX);

    assert(ai_discovery_request_for_phase(AI_DISCOVERY_INTERFACE_KEYBOARD,
                                          &request) == AI_OK);
    assert(request.report == 0x02 && request.device == 0x01);
    assert(request.response_length == AI_DESCRIPTOR_MAX);

    assert(ai_discovery_request_for_phase(AI_DISCOVERY_INTERFACE_TRACKPAD,
                                          &request) == AI_OK);
    assert(request.report == 0x02 && request.device == 0x02);
    assert(request.response_length == AI_DESCRIPTOR_MAX);

    assert(ai_discovery_request_for_phase(AI_DISCOVERY_KEYBOARD_DESCRIPTOR,
                                          &request) == AI_OK);
    assert(request.report == 0x10 && request.device == 0x01);
    assert(request.response_length == AI_DESCRIPTOR_MAX);

    assert(ai_discovery_request_for_phase(AI_DISCOVERY_TRACKPAD_DESCRIPTOR,
                                          &request) == AI_OK);
    assert(request.report == 0x10 && request.device == 0x02);
    assert(request.response_length == AI_DESCRIPTOR_MAX);

    assert(ai_discovery_request_for_phase(AI_DISCOVERY_READY, &request) ==
           AI_ERR_SEQUENCE);
    assert(ai_discovery_request_for_phase(AI_DISCOVERY_IDENTITY, NULL) ==
           AI_ERR_ARGUMENT);
}

static void test_discovery_request_encoding(void)
{
    struct ai_discovery_request request;
    struct ai_packet_view packet;
    struct ai_protocol_message message;
    uint8_t raw[AI_PACKET_SIZE];

    assert(ai_discovery_request_for_phase(AI_DISCOVERY_INTERFACE_KEYBOARD,
                                          &request) == AI_OK);
    assert(ai_discovery_request_encode(&request, 0x5a, raw) == AI_OK);
    assert(ai_packet_decode(raw, &packet) == AI_OK);
    assert(packet.flags == AI_PACKET_WRITE && packet.device == 0xd0);
    assert(packet.offset == 0 && packet.remaining == 0 && packet.length == 10);
    assert(ai_message_decode(packet.data, packet.length, &message) == AI_OK);
    assert(message.type == 0x20 && message.report == 0x02);
    assert(message.device == 0x01 && message.id == 0x5a);
    assert(message.response_length == AI_DESCRIPTOR_MAX);
    assert(message.payload_length == 0);

    assert(ai_discovery_request_encode(NULL, 0, raw) == AI_ERR_ARGUMENT);
    assert(ai_discovery_request_encode(&request, 0, NULL) == AI_ERR_ARGUMENT);
}

static void test_discovery_response_matching(void)
{
    static const uint8_t payload[] = {1};
    struct ai_message_view wire = {AI_PACKET_WRITE, 0xd0, payload, 11};
    struct ai_protocol_message message = {
        .type = 0x20,
        .report = 0x01,
        .device = 0xd0,
        .payload = payload,
        .payload_length = 1,
    };

    assert(ai_discovery_response_matches(AI_DISCOVERY_IDENTITY, &wire, &message));
    message.report = 0x02;
    message.device = 0;
    assert(ai_discovery_response_matches(AI_DISCOVERY_INTERFACE_MANAGEMENT,
                                         &wire, &message));
    message.device = 1;
    assert(ai_discovery_response_matches(AI_DISCOVERY_INTERFACE_KEYBOARD,
                                         &wire, &message));
    message.device = 2;
    assert(ai_discovery_response_matches(AI_DISCOVERY_INTERFACE_TRACKPAD,
                                         &wire, &message));
    message.report = 0x10;
    message.device = 1;
    assert(ai_discovery_response_matches(AI_DISCOVERY_KEYBOARD_DESCRIPTOR,
                                         &wire, &message));
    message.device = 2;
    assert(ai_discovery_response_matches(AI_DISCOVERY_TRACKPAD_DESCRIPTOR,
                                         &wire, &message));

    wire.flags = AI_PACKET_READ;
    assert(!ai_discovery_response_matches(AI_DISCOVERY_TRACKPAD_DESCRIPTOR,
                                          &wire, &message));
    wire.flags = AI_PACKET_WRITE;
    wire.device = 2;
    assert(!ai_discovery_response_matches(AI_DISCOVERY_TRACKPAD_DESCRIPTOR,
                                          &wire, &message));
    wire.device = 0xd0;
    message.type = 0x10;
    assert(!ai_discovery_response_matches(AI_DISCOVERY_TRACKPAD_DESCRIPTOR,
                                          &wire, &message));
    message.type = 0x20;
    message.payload_length = 0;
    assert(!ai_discovery_response_matches(AI_DISCOVERY_TRACKPAD_DESCRIPTOR,
                                          &wire, &message));
    assert(!ai_discovery_response_matches(AI_DISCOVERY_READY, &wire, &message));
    assert(!ai_discovery_response_matches(AI_DISCOVERY_IDENTITY, NULL, &message));
    assert(!ai_discovery_response_matches(AI_DISCOVERY_IDENTITY, &wire, NULL));
}

static void test_spi_transfer_plan(void)
{
    struct ai_spi_transfer_plan plan;

    assert(ai_spi_plan_transfer(120000000, 8000000, 8, AI_PACKET_SIZE,
                                &plan) == AI_OK);
    assert(plan.clock_divider == 15);
    assert(plan.words == AI_PACKET_SIZE && plan.bytes_per_word == 1);
    assert(!plan.poll);

    assert(ai_spi_plan_transfer(120000000, 32000000, 16, 256, &plan) == AI_OK);
    assert(plan.clock_divider == 4 && plan.words == 128);
    assert(plan.bytes_per_word == 2 && plan.poll);

    assert(ai_spi_plan_transfer(120000000, 1, 32, 256, &plan) == AI_OK);
    assert(plan.clock_divider == 0x7ff && plan.words == 64);
    assert(plan.bytes_per_word == 4 && !plan.poll);

    assert(ai_spi_plan_transfer(0, 8000000, 8, 256, &plan) == AI_ERR_ARGUMENT);
    assert(ai_spi_plan_transfer(120000000, 0, 8, 256, &plan) == AI_ERR_ARGUMENT);
    assert(ai_spi_plan_transfer(120000000, 8000000, 0, 256, &plan) == AI_ERR_ARGUMENT);
    assert(ai_spi_plan_transfer(120000000, 8000000, 33, 256, &plan) == AI_ERR_ARGUMENT);
    assert(ai_spi_plan_transfer(120000000, 8000000, 16, 255, &plan) == AI_ERR_LENGTH);
    assert(ai_spi_plan_transfer(120000000, 8000000, 8, 256, NULL) == AI_ERR_ARGUMENT);
}

static void test_spi_init_plan(void)
{
    struct ai_spi_register_op ops[9];

    assert(ai_spi_init_plan(NULL, 0) == 9);
    assert(ai_spi_init_plan(ops, 8) == 9);
    assert(ai_spi_init_plan(ops, 9) == 9);

    assert(ops[0].kind == AI_SPI_REGISTER_WRITE && ops[0].offset == 0x00c);
    assert(ops[0].clear_mask == 0 && ops[0].set_value == 0x2);
    assert(ops[1].kind == AI_SPI_REGISTER_MASK && ops[1].offset == 0x150);
    assert(ops[1].clear_mask == (1u << 24) && ops[1].set_value == 0);
    assert(ops[2].kind == AI_SPI_REGISTER_MASK && ops[2].offset == 0x154);
    assert(ops[2].clear_mask == (1u << 9) && ops[2].set_value == (1u << 1));
    assert(ops[3].kind == AI_SPI_REGISTER_WRITE && ops[3].offset == 0x000);
    assert(ops[3].set_value == ((1u << 2) | (1u << 3)));
    assert(ops[4].kind == AI_SPI_REGISTER_WRITE && ops[4].offset == 0x004);
    assert(ops[4].set_value == (1u << 5));
    assert(ops[5].offset == 0x138 && ops[5].set_value == 0);
    assert(ops[6].offset == 0x130 && ops[6].set_value == 0);
    assert(ops[7].offset == 0x160 && ops[7].set_value == 0);
    assert(ops[8].offset == 0x168 && ops[8].set_value == 0);
}

static void test_interrupt_worker_queue(void)
{
    struct ai_transport_queue queue;

    ai_transport_queue_reset(&queue);
    assert(ai_transport_irq(&queue));
    assert(!ai_transport_irq(&queue));
    assert(queue.coalesced_irqs == 1);

    assert(ai_transport_worker_begin(&queue));
    assert(!ai_transport_worker_begin(&queue));
    assert(!ai_transport_irq(&queue));
    assert(queue.coalesced_irqs == 2);

    assert(ai_transport_worker_complete(&queue, false));
    assert(ai_transport_worker_begin(&queue));
    assert(!ai_transport_worker_complete(&queue, false));

    assert(ai_transport_irq(&queue));
    assert(ai_transport_worker_begin(&queue));
    assert(ai_transport_worker_complete(&queue, true));
    assert(ai_transport_worker_begin(&queue));
    assert(!ai_transport_worker_complete(&queue, false));
}

static void test_descriptor_store_owns_bytes(void)
{
    struct ai_descriptor_store store;
    uint8_t keyboard[] = {0x05, 0x01, 0x09, 0x06};
    uint8_t trackpad[] = {0x05, 0x0d, 0x09, 0x05};
    uint8_t oversized[AI_DESCRIPTOR_MAX + 1] = {0};
    const struct ai_descriptor_slot *slot;

    ai_descriptor_store_reset(&store);
    assert(ai_descriptor_store_get(&store, 1) == NULL);
    assert(ai_descriptor_store_get(&store, 2) == NULL);

    assert(ai_descriptor_store_put(&store, 1, keyboard,
                                   sizeof(keyboard)) == AI_OK);
    keyboard[0] = 0xff;
    slot = ai_descriptor_store_get(&store, 1);
    assert(slot && slot->valid && slot->device == 1);
    assert(slot->length == sizeof(keyboard));
    assert(slot->bytes[0] == 0x05);

    assert(ai_descriptor_store_put(&store, 2, trackpad,
                                   sizeof(trackpad)) == AI_OK);
    trackpad[0] = 0xff;
    slot = ai_descriptor_store_get(&store, 2);
    assert(slot && slot->valid && slot->device == 2);
    assert(slot->length == sizeof(trackpad));
    assert(slot->bytes[0] == 0x05);

    assert(ai_descriptor_store_put(NULL, 1, keyboard,
                                   sizeof(keyboard)) == AI_ERR_ARGUMENT);
    assert(ai_descriptor_store_put(&store, 1, NULL,
                                   sizeof(keyboard)) == AI_ERR_ARGUMENT);
    assert(ai_descriptor_store_put(&store, 1, keyboard, 0) == AI_ERR_ARGUMENT);
    assert(ai_descriptor_store_put(&store, 1, oversized,
                                   sizeof(oversized)) == AI_ERR_LENGTH);
    assert(ai_descriptor_store_put(&store, 0, keyboard,
                                   sizeof(keyboard)) == AI_ERR_PROTOCOL);
    assert(ai_descriptor_store_put(&store, 3, keyboard,
                                   sizeof(keyboard)) == AI_ERR_PROTOCOL);
    assert(ai_descriptor_store_get(NULL, 1) == NULL);
    assert(ai_descriptor_store_get(&store, 0) == NULL);
    assert(ai_descriptor_store_get(&store, 3) == NULL);

    ai_descriptor_store_reset(&store);
    assert(ai_descriptor_store_get(&store, 1) == NULL);
    assert(ai_descriptor_store_get(&store, 2) == NULL);
}

static void test_hid_input_contract(void)
{
    static const uint8_t keyboard_descriptor[] = {
        0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
        0x85, 0x01, 0x75, 0x08, 0x95, 0x08, 0x81, 0x02,
        0xc0,
    };
    static const uint8_t no_report_id[] = {
        0x75, 0x08, 0x95, 0x08, 0x81, 0x02,
    };
    static const uint8_t multiple_inputs[] = {
        0x85, 0x02, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02,
        0x95, 0x05, 0x81, 0x02,
    };
    static const uint8_t rounded_bits[] = {
        0x75, 0x01, 0x95, 0x09, 0x81, 0x02,
    };
    static const uint8_t push_pop[] = {
        0x75, 0x08, 0x95, 0x02, 0xa4,
        0x75, 0x01, 0x95, 0x03, 0x81, 0x02,
        0xb4, 0x81, 0x02,
    };
    static const uint8_t truncated[] = {0x75};
    static const uint8_t long_item[] = {0xfe, 0x00, 0x00};
    static const uint8_t report_id_zero[] = {
        0x85, 0x00, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
    };
    static const uint8_t zero_report_size[] = {
        0x75, 0x00, 0x95, 0x01, 0x81, 0x02,
    };
    static const uint8_t zero_report_count[] = {
        0x75, 0x08, 0x95, 0x00, 0x81, 0x02,
    };
    static const uint8_t stack_underflow[] = {0xb4};
    static const uint8_t stack_overflow[] = {0xa4, 0xa4, 0xa4, 0xa4, 0xa4};
    static const uint8_t unbalanced_stack[] = {
        0x75, 0x08, 0x95, 0x01, 0xa4, 0x81, 0x02,
    };
    static const uint8_t bit_count_overflow[] = {
        0x77, 0xff, 0xff, 0xff, 0xff,
        0x97, 0xff, 0xff, 0xff, 0xff,
        0x81, 0x02,
    };
    static const uint8_t no_input[] = {0x05, 0x01, 0x09, 0x06};
    struct ai_hid_input_contract contract;
    uint8_t keyboard_report[9] = {1};
    uint8_t no_id_report[8] = {0};
    uint8_t report_id = 0xff;

    assert(ai_hid_input_contract_parse(keyboard_descriptor,
                                       sizeof(keyboard_descriptor),
                                       &contract) == AI_OK);
    assert(contract.valid && contract.uses_report_ids);
    assert(contract.bytes_by_id[1] == 9);
    assert(ai_hid_input_report_valid(&contract, keyboard_report,
                                     sizeof(keyboard_report), &report_id));
    assert(report_id == 1);
    assert(!ai_hid_input_report_valid(&contract, keyboard_report, 8,
                                      &report_id));
    keyboard_report[0] = 2;
    assert(!ai_hid_input_report_valid(&contract, keyboard_report,
                                      sizeof(keyboard_report), &report_id));

    assert(ai_hid_input_contract_parse(no_report_id, sizeof(no_report_id),
                                       &contract) == AI_OK);
    assert(contract.valid && !contract.uses_report_ids);
    assert(contract.bytes_by_id[0] == sizeof(no_id_report));
    assert(ai_hid_input_report_valid(&contract, no_id_report,
                                     sizeof(no_id_report), &report_id));
    assert(report_id == 0);

    assert(ai_hid_input_contract_parse(multiple_inputs,
                                       sizeof(multiple_inputs),
                                       &contract) == AI_OK);
    assert(contract.uses_report_ids && contract.bytes_by_id[2] == 2);
    assert(ai_hid_input_contract_parse(rounded_bits, sizeof(rounded_bits),
                                       &contract) == AI_OK);
    assert(!contract.uses_report_ids && contract.bytes_by_id[0] == 2);
    assert(ai_hid_input_contract_parse(push_pop, sizeof(push_pop),
                                       &contract) == AI_OK);
    assert(contract.bytes_by_id[0] == 3);

    assert(ai_hid_input_contract_parse(NULL, sizeof(no_input), &contract) ==
           AI_ERR_ARGUMENT);
    assert(ai_hid_input_contract_parse(no_input, sizeof(no_input), NULL) ==
           AI_ERR_ARGUMENT);
    assert(ai_hid_input_contract_parse(no_input, 0, &contract) ==
           AI_ERR_ARGUMENT);
    assert(ai_hid_input_contract_parse(truncated, sizeof(truncated),
                                       &contract) == AI_ERR_LENGTH);
    assert(ai_hid_input_contract_parse(long_item, sizeof(long_item),
                                       &contract) == AI_ERR_PROTOCOL);
    assert(ai_hid_input_contract_parse(report_id_zero,
                                       sizeof(report_id_zero),
                                       &contract) == AI_ERR_PROTOCOL);
    assert(ai_hid_input_contract_parse(zero_report_size,
                                       sizeof(zero_report_size),
                                       &contract) == AI_ERR_PROTOCOL);
    assert(ai_hid_input_contract_parse(zero_report_count,
                                       sizeof(zero_report_count),
                                       &contract) == AI_ERR_PROTOCOL);
    assert(ai_hid_input_contract_parse(stack_underflow,
                                       sizeof(stack_underflow),
                                       &contract) == AI_ERR_PROTOCOL);
    assert(ai_hid_input_contract_parse(stack_overflow,
                                       sizeof(stack_overflow),
                                       &contract) == AI_ERR_PROTOCOL);
    assert(ai_hid_input_contract_parse(unbalanced_stack,
                                       sizeof(unbalanced_stack),
                                       &contract) == AI_ERR_PROTOCOL);
    assert(ai_hid_input_contract_parse(bit_count_overflow,
                                       sizeof(bit_count_overflow),
                                       &contract) == AI_ERR_LENGTH);
    assert(ai_hid_input_contract_parse(no_input, sizeof(no_input),
                                       &contract) == AI_ERR_PROTOCOL);
    assert(!ai_hid_input_report_valid(NULL, no_id_report,
                                      sizeof(no_id_report), &report_id));
    AI_MEMSET(&contract, sizeof(contract));
    assert(!ai_hid_input_report_valid(&contract, no_id_report,
                                      sizeof(no_id_report), &report_id));
    contract.valid = true;
    contract.bytes_by_id[0] = sizeof(no_id_report);
    assert(!ai_hid_input_report_valid(&contract, NULL,
                                      sizeof(no_id_report), &report_id));
    assert(!ai_hid_input_report_valid(&contract, no_id_report, 0,
                                      &report_id));
}

int main(void)
{
    test_crc();
    test_decode();
    test_reassembly();
    test_message_decode();
    test_discovery();
    test_boot_and_write_status_contract();
    test_discovery_request_contract();
    test_discovery_request_encoding();
    test_discovery_response_matching();
    test_spi_transfer_plan();
    test_spi_init_plan();
    test_interrupt_worker_queue();
    test_descriptor_store_owns_bytes();
    test_hid_input_contract();
    return 0;
}
