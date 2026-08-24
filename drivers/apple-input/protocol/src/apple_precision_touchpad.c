#include "apple_trackpad.h"

static const uint8_t ptp_default_certification[256] = {
    0xfc, 0x28, 0xfe, 0x84, 0x40, 0xcb, 0x9a, 0x87, 0x0d, 0xbe, 0x57, 0x3c,
    0xb6, 0x70, 0x09, 0x88, 0x07, 0x97, 0x2d, 0x2b, 0xe3, 0x38, 0x34, 0xb6,
    0x6c, 0xed, 0xb0, 0xf7, 0xe5, 0x9c, 0xf6, 0xc2, 0x2e, 0x84, 0x1b, 0xe8,
    0xb4, 0x51, 0x78, 0x43, 0x1f, 0x28, 0x4b, 0x7c, 0x2d, 0x53, 0xaf, 0xfc,
    0x47, 0x70, 0x1b, 0x59, 0x6f, 0x74, 0x43, 0xc4, 0xf3, 0x47, 0x18, 0x53,
    0x1a, 0xa2, 0xa1, 0x71, 0xc7, 0x95, 0x0e, 0x31, 0x55, 0x21, 0xd3, 0xb5,
    0x1e, 0xe9, 0x0c, 0xba, 0xec, 0xb8, 0x89, 0x19, 0x3e, 0xb3, 0xaf, 0x75,
    0x81, 0x9d, 0x53, 0xb9, 0x41, 0x57, 0xf4, 0x6d, 0x39, 0x25, 0x29, 0x7c,
    0x87, 0xd9, 0xb4, 0x98, 0x45, 0x7d, 0xa7, 0x26, 0x9c, 0x65, 0x3b, 0x85,
    0x68, 0x89, 0xd7, 0x3b, 0xbd, 0xff, 0x14, 0x67, 0xf2, 0x2b, 0xf0, 0x2a,
    0x41, 0x54, 0xf0, 0xfd, 0x2c, 0x66, 0x7c, 0xf8, 0xc0, 0x8f, 0x33, 0x13,
    0x03, 0xf1, 0xd3, 0xc1, 0x0b, 0x89, 0xd9, 0x1b, 0x62, 0xcd, 0x51, 0xb7,
    0x80, 0xb8, 0xaf, 0x3a, 0x10, 0xc1, 0x8a, 0x5b, 0xe8, 0x8a, 0x56, 0xf0,
    0x8c, 0xaa, 0xfa, 0x35, 0xe9, 0x42, 0xc4, 0xd8, 0x55, 0xc3, 0x38, 0xcc,
    0x2b, 0x53, 0x5c, 0x69, 0x52, 0xd5, 0xc8, 0x73, 0x02, 0x38, 0x7c, 0x73,
    0xb6, 0x41, 0xe7, 0xff, 0x05, 0xd8, 0x2b, 0x79, 0x9a, 0xe2, 0x34, 0x60,
    0x8f, 0xa3, 0x32, 0x1f, 0x09, 0x78, 0x62, 0xbc, 0x80, 0xe3, 0x0f, 0xbd,
    0x65, 0x20, 0x08, 0x13, 0xc1, 0xe2, 0xee, 0x53, 0x2d, 0x86, 0x7e, 0xa7,
    0x5a, 0xc5, 0xd3, 0x7d, 0x98, 0xbe, 0x31, 0x48, 0x1f, 0xfb, 0xda, 0xaf,
    0xa2, 0xa8, 0x6a, 0x89, 0xd6, 0xbf, 0xf2, 0xd3, 0x32, 0x2a, 0x9a, 0xe4,
    0xcf, 0x17, 0xb7, 0xb8, 0xf4, 0xe1, 0x33, 0x08, 0x24, 0x8b, 0xc4, 0x43,
    0xa5, 0xe5, 0x24, 0xc2,
};

static void put_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static bool axis_contract_valid(const struct ai_trackpad_axis_contract *axes)
{
    return axes && axes->valid && axes->x.valid && axes->y.valid &&
        axes->x.logical_min < axes->x.logical_max &&
        axes->y.logical_min < axes->y.logical_max &&
        axes->x.physical_min < axes->x.physical_max &&
        axes->y.physical_min < axes->y.physical_max &&
        axes->x.unit == axes->y.unit &&
        axes->x.unit_exponent == axes->y.unit_exponent;
}

static uint16_t normalize_axis(int32_t value, int32_t minimum,
                               int32_t maximum)
{
    signed long long clamped = value;
    unsigned long long delta;
    unsigned long long range;

    if (clamped < minimum)
        clamped = minimum;
    if (clamped > maximum)
        clamped = maximum;
    delta = (unsigned long long)(clamped - (signed long long)minimum);
    range = (unsigned long long)((signed long long)maximum - minimum);
    return (uint16_t)((delta * 4095ULL + range / 2ULL) / range);
}

void ai_ptp_feature_init(struct ai_ptp_feature_state *state)
{
    if (!state)
        return;
    AI_MEMSET(state, sizeof(*state));
    state->surface_enabled = true;
    state->button_enabled = true;
}

enum ai_status ai_ptp_encode_neutral(
    uint16_t scan_time_100us,
    uint8_t *report, size_t capacity, size_t *length)
{
    if (!report || !length)
        return AI_ERR_ARGUMENT;
    *length = 0;
    if (capacity < AI_PTP_INPUT_REPORT_SIZE)
        return AI_ERR_LENGTH;
    AI_MEMSET(report, AI_PTP_INPUT_REPORT_SIZE);
    report[0] = AI_PTP_REPORT_INPUT;
    put_le16(report + 31u, scan_time_100us);
    *length = AI_PTP_INPUT_REPORT_SIZE;
    return AI_OK;
}

enum ai_status ai_ptp_encode_input(
    const struct ai_trackpad_axis_contract *axes,
    const struct ai_trackpad_output_frame *frame,
    uint16_t scan_time_100us,
    const struct ai_ptp_feature_state *features,
    uint8_t *report, size_t capacity, size_t *length)
{
    uint8_t index;
    uint8_t emitted = 0;

    if (!axes || !frame || !features || !report || !length)
        return AI_ERR_ARGUMENT;
    *length = 0;
    if (frame->count > AI_PTP_MAX_CONTACTS)
        return AI_ERR_LENGTH;
    if (!axis_contract_valid(axes))
        return AI_ERR_PROTOCOL;
    if (features->input_mode != 3u || features->mode_change_pending)
        return AI_OK;
    if (capacity < AI_PTP_INPUT_REPORT_SIZE)
        return AI_ERR_LENGTH;

    AI_MEMSET(report, AI_PTP_INPUT_REPORT_SIZE);
    report[0] = AI_PTP_REPORT_INPUT;
    if (features->surface_enabled) {
        for (index = 0; index < frame->count; ++index) {
            const struct ai_trackpad_output_contact *source =
                &frame->contacts[index];
            uint8_t *destination = report + 1u + (size_t)emitted * 6u;
            uint16_t x;
            uint16_t y;

            if (source->id >= AI_PTP_MAX_CONTACTS)
                return AI_ERR_PROTOCOL;
            x = normalize_axis(source->x, axes->x.logical_min,
                               axes->x.logical_max);
            y = normalize_axis(source->y, axes->y.logical_min,
                               axes->y.logical_max);
            destination[0] = (uint8_t)(1u | (source->tip ? 2u : 0u));
            destination[1] = source->id;
            put_le16(destination + 2u, x);
            put_le16(destination + 4u, (uint16_t)(4095u - y));
            ++emitted;
        }
    }
    put_le16(report + 31u, scan_time_100us);
    report[33] = emitted;
    report[34] = features->button_enabled && frame->button ? 1u : 0u;
    *length = AI_PTP_INPUT_REPORT_SIZE;
    return AI_OK;
}

enum ai_status ai_ptp_get_feature(
    const struct ai_ptp_feature_state *state, uint8_t report_id,
    uint8_t *buffer, size_t capacity, size_t *length)
{
    size_t required;

    if (!state || !buffer || !length)
        return AI_ERR_ARGUMENT;
    *length = 0;
    switch (report_id) {
    case AI_PTP_REPORT_CAPABILITIES:
        required = AI_PTP_CAPABILITIES_REPORT_SIZE;
        break;
    case AI_PTP_REPORT_CERTIFICATION:
        required = AI_PTP_CERTIFICATION_REPORT_SIZE;
        break;
    case AI_PTP_REPORT_INPUT_MODE:
        required = AI_PTP_INPUT_MODE_REPORT_SIZE;
        break;
    case AI_PTP_REPORT_SELECTIVE:
        required = AI_PTP_SELECTIVE_REPORT_SIZE;
        break;
    default:
        return AI_ERR_PROTOCOL;
    }
    if (capacity < required)
        return AI_ERR_LENGTH;

    AI_MEMSET(buffer, required);
    buffer[0] = report_id;
    if (report_id == AI_PTP_REPORT_CAPABILITIES) {
        buffer[1] = AI_PTP_MAX_CONTACTS;
    } else if (report_id == AI_PTP_REPORT_CERTIFICATION) {
        AI_MEMCPY(buffer + 1u, ptp_default_certification,
                  sizeof(ptp_default_certification));
    } else if (report_id == AI_PTP_REPORT_INPUT_MODE) {
        buffer[1] = state->mode_change_pending ?
            state->pending_input_mode : state->input_mode;
    } else {
        buffer[1] = (uint8_t)((state->surface_enabled ? 1u : 0u) |
                              (state->button_enabled ? 2u : 0u));
    }
    *length = required;
    return AI_OK;
}

enum ai_status ai_ptp_set_feature(
    struct ai_ptp_feature_state *state, uint8_t report_id,
    const uint8_t *buffer, size_t length, bool contacts_active,
    bool *neutral_required)
{
    if (!state || !buffer || !neutral_required)
        return AI_ERR_ARGUMENT;
    *neutral_required = false;
    if (length != 2u)
        return AI_ERR_LENGTH;
    if (buffer[0] != report_id)
        return AI_ERR_PROTOCOL;

    if (report_id == AI_PTP_REPORT_INPUT_MODE) {
        uint8_t requested = buffer[1] == 3u ? 3u : 0u;

        if (state->mode_change_pending &&
            state->pending_input_mode == requested)
            return AI_OK;
        if (!state->mode_change_pending && state->input_mode == requested)
            return AI_OK;
        if (contacts_active) {
            state->pending_input_mode = requested;
            state->mode_change_pending = true;
            state->neutral_pending = true;
            *neutral_required = true;
        } else {
            state->input_mode = requested;
            state->pending_input_mode = requested;
            state->mode_change_pending = false;
            state->neutral_pending = false;
        }
        return AI_OK;
    }
    if (report_id == AI_PTP_REPORT_SELECTIVE) {
        if ((buffer[1] & 0xfcu) != 0u)
            return AI_ERR_PROTOCOL;
        state->surface_enabled = (buffer[1] & 1u) != 0u;
        state->button_enabled = (buffer[1] & 2u) != 0u;
        return AI_OK;
    }
    return AI_ERR_PROTOCOL;
}

bool ai_ptp_feature_take_neutral(struct ai_ptp_feature_state *state)
{
    bool pending;

    if (!state)
        return false;
    pending = state->neutral_pending;
    state->neutral_pending = false;
    return pending;
}

bool ai_ptp_feature_contacts_update(struct ai_ptp_feature_state *state,
                                    bool contacts_active)
{
    if (!state)
        return false;
    if (state->mode_change_pending) {
        if (contacts_active)
            return false;
        state->input_mode = state->pending_input_mode;
        state->mode_change_pending = false;
        state->neutral_pending = false;
    }
    return state->input_mode == 3u;
}
