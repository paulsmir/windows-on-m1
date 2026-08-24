#include "apple_spihid.h"

#define AI_HID_GLOBAL_STACK_CAPACITY 4u

struct ai_hid_global_state {
    uint32_t report_size;
    uint32_t report_count;
    uint8_t report_id;
};

static uint32_t ai_hid_unsigned_value(const uint8_t *bytes, size_t size)
{
    uint32_t value = 0;
    size_t index;

    for (index = 0; index < size; ++index)
        value |= (uint32_t)bytes[index] << (index * 8u);
    return value;
}

enum ai_status ai_hid_input_contract_parse(
    const uint8_t *descriptor, size_t length,
    struct ai_hid_input_contract *out)
{
    struct ai_hid_global_state globals = {0};
    struct ai_hid_global_state stack[AI_HID_GLOBAL_STACK_CAPACITY];
    struct ai_hid_input_contract result;
    uint32_t bits_by_id[AI_HID_REPORT_ID_CAPACITY];
    size_t stack_depth = 0;
    size_t offset = 0;
    size_t report_count = 0;

    if (!descriptor || length == 0 || !out)
        return AI_ERR_ARGUMENT;
    AI_MEMSET(out, sizeof(*out));
    AI_MEMSET(&result, sizeof(result));
    AI_MEMSET(bits_by_id, sizeof(bits_by_id));
    AI_MEMSET(stack, sizeof(stack));

    while (offset < length) {
        uint8_t prefix = descriptor[offset++];
        size_t item_size;
        uint8_t item_type;
        uint8_t item_tag;
        uint32_t value;

        if (prefix == 0xfe)
            return AI_ERR_PROTOCOL;
        item_size = prefix & 0x03u;
        if (item_size == 3)
            item_size = 4;
        if (item_size > length - offset)
            return AI_ERR_LENGTH;
        item_type = (prefix >> 2) & 0x03u;
        item_tag = prefix >> 4;
        value = ai_hid_unsigned_value(descriptor + offset, item_size);
        offset += item_size;

        if (item_type == 1) {
            switch (item_tag) {
            case 7:
                globals.report_size = value;
                break;
            case 8:
                if (value == 0 || value >= AI_HID_REPORT_ID_CAPACITY)
                    return AI_ERR_PROTOCOL;
                globals.report_id = (uint8_t)value;
                result.uses_report_ids = true;
                break;
            case 9:
                globals.report_count = value;
                break;
            case 10:
                if (stack_depth == AI_HID_GLOBAL_STACK_CAPACITY)
                    return AI_ERR_PROTOCOL;
                stack[stack_depth++] = globals;
                break;
            case 11:
                if (stack_depth == 0)
                    return AI_ERR_PROTOCOL;
                globals = stack[--stack_depth];
                break;
            default:
                break;
            }
        } else if (item_type == 0 && item_tag == 8) {
            uint64_t contribution;
            uint64_t total;
            uint64_t maximum_bits = (uint64_t)AI_UINT16_MAX * 8u;

            if (globals.report_size == 0 || globals.report_count == 0)
                return AI_ERR_PROTOCOL;
            contribution = (uint64_t)globals.report_size *
                           globals.report_count;
            total = (uint64_t)bits_by_id[globals.report_id] + contribution;
            if (contribution > maximum_bits || total > maximum_bits)
                return AI_ERR_LENGTH;
            bits_by_id[globals.report_id] = (uint32_t)total;
            ++report_count;
        }
    }

    if (stack_depth != 0 || report_count == 0)
        return AI_ERR_PROTOCOL;
    if (result.uses_report_ids && bits_by_id[0] != 0)
        return AI_ERR_PROTOCOL;

    for (offset = 0; offset < AI_HID_REPORT_ID_CAPACITY; ++offset) {
        uint32_t bytes;

        if (bits_by_id[offset] == 0)
            continue;
        bytes = (bits_by_id[offset] + 7u) / 8u;
        if (result.uses_report_ids)
            ++bytes;
        if (bytes > AI_UINT16_MAX)
            return AI_ERR_LENGTH;
        result.bytes_by_id[offset] = (uint16_t)bytes;
    }

    *out = result;
    out->valid = true;
    return AI_OK;
}

bool ai_hid_input_report_valid(const struct ai_hid_input_contract *contract,
                               const uint8_t *report, size_t length,
                               uint8_t *report_id)
{
    uint8_t id;

    if (!contract || !contract->valid || !report || length == 0)
        return false;
    id = contract->uses_report_ids ? report[0] : 0;
    if (contract->bytes_by_id[id] == 0 ||
        contract->bytes_by_id[id] != length)
        return false;
    if (report_id)
        *report_id = id;
    return true;
}
