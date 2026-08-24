#include "apple_trackpad.h"

static void arm_deadline(struct ai_trackpad_init *state, uint64_t now_us,
                         uint64_t timeout_us)
{
    state->deadline_us = now_us + timeout_us;
    if (state->deadline_us < now_us)
        state->deadline_us = AI_UINT64_MAX;
}

void ai_trackpad_init_start(struct ai_trackpad_init *state,
                            uint8_t message_id, uint64_t now_us,
                            uint64_t timeout_us, uint8_t retry_limit)
{
    if (!state)
        return;
    *state = (struct ai_trackpad_init){
        .phase = AI_TRACKPAD_INIT_DIMENSIONS,
        .retry_limit = retry_limit,
        .message_id = message_id,
    };
    arm_deadline(state, now_us, timeout_us);
}

bool ai_trackpad_init_response_matches(
    const struct ai_trackpad_init *state,
    const struct ai_message_view *wire,
    const struct ai_protocol_message *message)
{
    if (!state || !wire || !message || wire->flags != AI_PACKET_WRITE ||
        message->id != state->message_id)
        return false;
    if (state->phase == AI_TRACKPAD_INIT_DIMENSIONS)
        return wire->device == 0x02 && message->type == 0x32 &&
               message->report == 0xd9 && message->device == 0x00 &&
               message->payload_length >= 17u;
    if (state->phase == AI_TRACKPAD_INIT_MULTITOUCH)
        return wire->device == 0x02 && message->type == 0x52 &&
               message->report == 0x02;
    return false;
}

enum ai_status ai_trackpad_init_accept(
    struct ai_trackpad_init *state, const struct ai_message_view *wire,
    const struct ai_protocol_message *message, uint64_t now_us,
    uint64_t timeout_us)
{
    if (!state || !wire || !message)
        return AI_ERR_ARGUMENT;
    if (!ai_trackpad_init_response_matches(state, wire, message))
        return AI_ERR_SEQUENCE;

    if (state->phase == AI_TRACKPAD_INIT_DIMENSIONS) {
        enum ai_status status = ai_trackpad_dimensions_parse(
            message->payload, message->payload_length, &state->dimensions);

        if (status != AI_OK)
            return status;
    }

    state->retry_count = 0;
    state->message_id++;
    if (state->phase == AI_TRACKPAD_INIT_DIMENSIONS) {
        state->phase = AI_TRACKPAD_INIT_MULTITOUCH;
        arm_deadline(state, now_us, timeout_us);
        return AI_OK;
    }
    state->phase = AI_TRACKPAD_INIT_READY;
    return AI_COMPLETE;
}

enum ai_status ai_trackpad_init_poll(struct ai_trackpad_init *state,
                                     uint64_t now_us,
                                     uint64_t timeout_us)
{
    if (!state)
        return AI_ERR_ARGUMENT;
    if (state->phase != AI_TRACKPAD_INIT_DIMENSIONS &&
        state->phase != AI_TRACKPAD_INIT_MULTITOUCH)
        return state->phase == AI_TRACKPAD_INIT_READY ? AI_COMPLETE
                                                      : AI_ERR_SEQUENCE;
    if (now_us < state->deadline_us)
        return AI_OK;
    if (state->retry_count >= state->retry_limit) {
        state->phase = AI_TRACKPAD_INIT_OFFLINE;
        return AI_ERR_TIMEOUT;
    }
    state->retry_count++;
    state->message_id++;
    arm_deadline(state, now_us, timeout_us);
    return AI_ERR_TIMEOUT;
}
