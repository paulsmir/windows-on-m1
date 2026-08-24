#include "apple_spihid.h"

static void arm_request(struct ai_discovery *state, uint64_t now_us, uint64_t timeout_us)
{
    state->request_id++;
    state->deadline_us = now_us + timeout_us;
    if (state->deadline_us < now_us)
        state->deadline_us = AI_UINT64_MAX;
}

static void arm_deadline(struct ai_discovery *state, uint64_t now_us,
                         uint64_t timeout_us)
{
    state->deadline_us = now_us + timeout_us;
    if (state->deadline_us < now_us)
        state->deadline_us = AI_UINT64_MAX;
}

void ai_discovery_start(struct ai_discovery *state, uint64_t now_us,
                        uint64_t timeout_us, uint8_t retry_limit)
{
    if (!state)
        return;
    *state = (struct ai_discovery){
        .phase = AI_DISCOVERY_WAIT_BOOT,
        .retry_limit = retry_limit,
    };
    arm_deadline(state, now_us, timeout_us);
}

enum ai_status ai_discovery_accept_boot(struct ai_discovery *state,
                                        const uint8_t *marker, size_t size,
                                        uint64_t now_us, uint64_t timeout_us)
{
    static const uint8_t expected[] = {0xa0, 0x80, 0x00, 0x00};

    if (!state || !marker)
        return AI_ERR_ARGUMENT;
    if (state->phase != AI_DISCOVERY_WAIT_BOOT)
        return AI_ERR_SEQUENCE;
    if (size != sizeof(expected))
        return AI_ERR_LENGTH;
    for (size_t index = 0; index < sizeof(expected); index++) {
        if (marker[index] != expected[index])
            return AI_ERR_PROTOCOL;
    }

    state->retry_count = 0;
    state->phase = AI_DISCOVERY_IDENTITY;
    /* Linux sends the first request with message ID zero. */
    arm_deadline(state, now_us, timeout_us);
    return AI_OK;
}

enum ai_status ai_discovery_accept(struct ai_discovery *state, uint32_t request_id,
                                   bool response_valid, uint64_t now_us,
                                   uint64_t timeout_us)
{
    if (!state)
        return AI_ERR_ARGUMENT;
    if (state->phase < AI_DISCOVERY_IDENTITY ||
        state->phase > AI_DISCOVERY_TRACKPAD_DESCRIPTOR)
        return AI_ERR_SEQUENCE;
    if (request_id != state->request_id)
        return AI_ERR_SEQUENCE;
    if (!response_valid) {
        state->phase = AI_DISCOVERY_OFFLINE;
        return AI_ERR_PROTOCOL;
    }

    state->retry_count = 0;
    state->phase = (enum ai_discovery_phase)(state->phase + 1);
    if (state->phase == AI_DISCOVERY_READY)
        return AI_COMPLETE;
    arm_request(state, now_us, timeout_us);
    return AI_OK;
}

enum ai_status ai_discovery_poll(struct ai_discovery *state, uint64_t now_us,
                                 uint64_t timeout_us)
{
    if (!state)
        return AI_ERR_ARGUMENT;
    if (state->phase < AI_DISCOVERY_WAIT_BOOT ||
        state->phase > AI_DISCOVERY_TRACKPAD_DESCRIPTOR)
        return state->phase == AI_DISCOVERY_READY ? AI_COMPLETE : AI_ERR_SEQUENCE;
    if (now_us < state->deadline_us)
        return AI_OK;

    if (state->retry_count >= state->retry_limit) {
        state->phase = AI_DISCOVERY_OFFLINE;
        return AI_ERR_TIMEOUT;
    }
    state->retry_count++;
    if (state->phase == AI_DISCOVERY_WAIT_BOOT)
        arm_deadline(state, now_us, timeout_us);
    else
        arm_request(state, now_us, timeout_us);
    return AI_ERR_TIMEOUT;
}

enum ai_status ai_discovery_request_for_phase(enum ai_discovery_phase phase,
                                              struct ai_discovery_request *out)
{
    if (!out)
        return AI_ERR_ARGUMENT;

    *out = (struct ai_discovery_request){
        .target = 0xd0,
        .type = 0x20,
        .response_length = AI_DESCRIPTOR_MAX,
    };
    switch (phase) {
    case AI_DISCOVERY_IDENTITY:
        out->report = 0x01;
        out->device = 0xd0;
        out->response_length = 0;
        return AI_OK;
    case AI_DISCOVERY_INTERFACE_MANAGEMENT:
    case AI_DISCOVERY_INTERFACE_KEYBOARD:
    case AI_DISCOVERY_INTERFACE_TRACKPAD:
        out->report = 0x02;
        out->device = (uint8_t)(phase - AI_DISCOVERY_INTERFACE_MANAGEMENT);
        return AI_OK;
    case AI_DISCOVERY_KEYBOARD_DESCRIPTOR:
    case AI_DISCOVERY_TRACKPAD_DESCRIPTOR:
        out->report = 0x10;
        out->device = (uint8_t)(phase - AI_DISCOVERY_KEYBOARD_DESCRIPTOR + 1);
        return AI_OK;
    default:
        return AI_ERR_SEQUENCE;
    }
}

bool ai_discovery_response_matches(enum ai_discovery_phase phase,
                                   const struct ai_message_view *wire,
                                   const struct ai_protocol_message *message)
{
    struct ai_discovery_request expected;

    if (!wire || !message || !message->payload_length ||
        wire->flags != AI_PACKET_WRITE || wire->device != 0xd0 ||
        message->type != 0x20 ||
        ai_discovery_request_for_phase(phase, &expected) != AI_OK)
        return false;

    return message->report == expected.report &&
           message->device == expected.device;
}
