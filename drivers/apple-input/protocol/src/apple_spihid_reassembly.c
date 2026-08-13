#include "apple_spihid.h"

void ai_reassembler_reset(struct ai_reassembler *state)
{
    if (state)
        AI_MEMSET(state, sizeof(*state));
}

enum ai_status ai_reassembler_push(struct ai_reassembler *state,
                                   const struct ai_packet_view *packet,
                                   struct ai_message_view *message)
{
    if (!state || !packet || !message || (packet->length && !packet->data))
        return AI_ERR_ARGUMENT;

    const size_t end = (size_t)packet->offset + packet->length;
    const size_t total = end + packet->remaining;
    if (end < packet->offset || total < end || total > AI_MAX_MESSAGE_SIZE ||
        packet->length > AI_PACKET_DATA_SIZE) {
        ai_reassembler_reset(state);
        return AI_ERR_LENGTH;
    }

    if (packet->offset == 0) {
        state->active = true;
        state->flags = packet->flags;
        state->device = packet->device;
        state->used = 0;
        state->total = total;
    } else if (!state->active || packet->flags != state->flags ||
               packet->device != state->device || packet->offset != state->used ||
               total != state->total) {
        ai_reassembler_reset(state);
        return AI_ERR_SEQUENCE;
    }

    if (packet->length)
        AI_MEMCPY(state->data + packet->offset, packet->data, packet->length);
    state->used = end;
    if (packet->remaining)
        return AI_OK;
    if (state->used != state->total) {
        ai_reassembler_reset(state);
        return AI_ERR_SEQUENCE;
    }

    message->flags = state->flags;
    message->device = state->device;
    message->data = state->data;
    message->length = state->total;
    state->active = false;
    return AI_COMPLETE;
}
