#include "apple_trackpad.h"

#define AI_HID_GLOBAL_STACK_CAPACITY 4u
#define AI_HID_COLLECTION_STACK_CAPACITY 8u
#define AI_HID_PAGE_GENERIC_DESKTOP 0x01u
#define AI_HID_PAGE_DIGITIZERS 0x0du
#define AI_HID_USAGE_X 0x30u
#define AI_HID_USAGE_Y 0x31u
#define AI_HID_USAGE_TOUCH_PAD 0x05u
#define AI_HID_USAGE_FINGER 0x22u
#define AI_HID_COLLECTION_APPLICATION 0x01u
#define AI_HID_COLLECTION_LOGICAL 0x02u

struct ai_trackpad_hid_globals {
    uint32_t usage_page;
    int32_t logical_min;
    int32_t logical_max;
    int32_t physical_min;
    int32_t physical_max;
    uint32_t unit;
    int8_t unit_exponent;
    bool logical_min_set;
    bool logical_max_set;
    bool physical_min_set;
    bool physical_max_set;
    bool unit_set;
    bool unit_exponent_set;
};

struct ai_trackpad_hid_local {
    uint32_t usage_page;
    uint32_t usage;
    bool usage_set;
};

struct ai_trackpad_collection {
    bool touch_pad;
    bool finger;
};

static uint32_t ai_hid_unsigned_value(const uint8_t *bytes, size_t size)
{
    uint32_t value = 0;
    size_t index;

    for (index = 0; index < size; ++index)
        value |= (uint32_t)bytes[index] << (index * 8u);
    return value;
}

static int32_t ai_hid_signed_value(const uint8_t *bytes, size_t size)
{
    uint32_t value = ai_hid_unsigned_value(bytes, size);

    if (size == 1 && (value & 0x80u))
        value |= 0xffffff00u;
    else if (size == 2 && (value & 0x8000u))
        value |= 0xffff0000u;
    return (int32_t)value;
}

static bool ai_hid_max_value(const uint8_t *bytes, size_t size,
                             int32_t minimum, int32_t *out)
{
    uint32_t value;

    if (minimum < 0) {
        *out = ai_hid_signed_value(bytes, size);
        return true;
    }
    value = ai_hid_unsigned_value(bytes, size);
    if (value > 0x7fffffffu)
        return false;
    *out = (int32_t)value;
    return true;
}

static bool ai_trackpad_axis_equal(const struct ai_trackpad_axis *left,
                                   const struct ai_trackpad_axis *right)
{
    return left->logical_min == right->logical_min &&
        left->logical_max == right->logical_max &&
        left->physical_min == right->physical_min &&
        left->physical_max == right->physical_max &&
        left->unit == right->unit &&
        left->unit_exponent == right->unit_exponent;
}

static enum ai_status ai_trackpad_axis_record(
    const struct ai_trackpad_hid_globals *globals,
    struct ai_trackpad_axis *axis)
{
    struct ai_trackpad_axis candidate;

    if (!globals->logical_min_set || !globals->logical_max_set ||
        !globals->physical_min_set || !globals->physical_max_set ||
        !globals->unit_set || !globals->unit_exponent_set ||
        globals->logical_min >= globals->logical_max ||
        globals->physical_min >= globals->physical_max)
        return AI_ERR_PROTOCOL;

    AI_MEMSET(&candidate, sizeof(candidate));
    candidate.logical_min = globals->logical_min;
    candidate.logical_max = globals->logical_max;
    candidate.physical_min = globals->physical_min;
    candidate.physical_max = globals->physical_max;
    candidate.unit = globals->unit;
    candidate.unit_exponent = globals->unit_exponent;
    candidate.valid = true;

    if (axis->valid)
        return ai_trackpad_axis_equal(axis, &candidate) ? AI_OK :
            AI_ERR_PROTOCOL;
    *axis = candidate;
    return AI_OK;
}

enum ai_status ai_trackpad_axis_contract_parse(
    const uint8_t *descriptor, size_t length,
    struct ai_trackpad_axis_contract *out)
{
    struct ai_trackpad_hid_globals globals;
    struct ai_trackpad_hid_globals global_stack[
        AI_HID_GLOBAL_STACK_CAPACITY];
    struct ai_trackpad_hid_local local;
    struct ai_trackpad_collection collections[
        AI_HID_COLLECTION_STACK_CAPACITY];
    struct ai_trackpad_axis_contract result;
    size_t global_depth = 0;
    size_t collection_depth = 0;
    size_t offset = 0;

    if (!out)
        return AI_ERR_ARGUMENT;
    AI_MEMSET(out, sizeof(*out));
    if (!descriptor || length == 0)
        return AI_ERR_ARGUMENT;
    AI_MEMSET(&globals, sizeof(globals));
    AI_MEMSET(global_stack, sizeof(global_stack));
    AI_MEMSET(&local, sizeof(local));
    AI_MEMSET(collections, sizeof(collections));
    AI_MEMSET(&result, sizeof(result));

    while (offset < length) {
        uint8_t prefix = descriptor[offset++];
        size_t item_size;
        uint8_t item_type;
        uint8_t item_tag;
        const uint8_t *data;
        uint32_t value;

        if (prefix == 0xfe)
            return AI_ERR_PROTOCOL;
        item_size = prefix & 0x03u;
        if (item_size == 3u)
            item_size = 4u;
        if (item_size > length - offset)
            return AI_ERR_LENGTH;
        item_type = (prefix >> 2) & 0x03u;
        item_tag = prefix >> 4;
        data = descriptor + offset;
        value = ai_hid_unsigned_value(data, item_size);
        offset += item_size;

        if (item_type == 1u) {
            switch (item_tag) {
            case 0: /* Usage Page */
                if (value > 0xffffu)
                    return AI_ERR_PROTOCOL;
                globals.usage_page = value;
                break;
            case 1: /* Logical Minimum */
                globals.logical_min = ai_hid_signed_value(data, item_size);
                globals.logical_min_set = true;
                break;
            case 2: /* Logical Maximum */
                if (!globals.logical_min_set ||
                    !ai_hid_max_value(data, item_size, globals.logical_min,
                                      &globals.logical_max))
                    return AI_ERR_PROTOCOL;
                globals.logical_max_set = true;
                break;
            case 3: /* Physical Minimum */
                globals.physical_min = ai_hid_signed_value(data, item_size);
                globals.physical_min_set = true;
                break;
            case 4: /* Physical Maximum */
                if (!globals.physical_min_set ||
                    !ai_hid_max_value(data, item_size, globals.physical_min,
                                      &globals.physical_max))
                    return AI_ERR_PROTOCOL;
                globals.physical_max_set = true;
                break;
            case 5: /* Unit Exponent */
                if (value > 0x0fu)
                    return AI_ERR_PROTOCOL;
                globals.unit_exponent = (int8_t)(value & 0x0fu);
                if (globals.unit_exponent & 0x08)
                    globals.unit_exponent = (int8_t)(
                        globals.unit_exponent - 16);
                globals.unit_exponent_set = true;
                break;
            case 6: /* Unit */
                globals.unit = value;
                globals.unit_set = true;
                break;
            case 10: /* Push */
                if (item_size != 0u ||
                    global_depth == AI_HID_GLOBAL_STACK_CAPACITY)
                    return AI_ERR_PROTOCOL;
                global_stack[global_depth++] = globals;
                break;
            case 11: /* Pop */
                if (item_size != 0u || global_depth == 0u)
                    return AI_ERR_PROTOCOL;
                globals = global_stack[--global_depth];
                break;
            default:
                break;
            }
        } else if (item_type == 2u && item_tag == 0u) {
            /* Usage */
            if (item_size == 4u && (value >> 16) != 0u) {
                local.usage_page = value >> 16;
                local.usage = value & 0xffffu;
            } else {
                local.usage_page = globals.usage_page;
                local.usage = value;
            }
            local.usage_set = true;
        } else if (item_type == 0u) {
            if (item_tag == 10u) { /* Collection */
                struct ai_trackpad_collection parent = {0};
                struct ai_trackpad_collection next;

                if (collection_depth == AI_HID_COLLECTION_STACK_CAPACITY ||
                    !local.usage_set)
                    return AI_ERR_PROTOCOL;
                if (collection_depth != 0u)
                    parent = collections[collection_depth - 1u];
                next = parent;
                if (local.usage_page == AI_HID_PAGE_DIGITIZERS &&
                    local.usage == AI_HID_USAGE_TOUCH_PAD &&
                    value == AI_HID_COLLECTION_APPLICATION)
                    next.touch_pad = true;
                if (parent.touch_pad &&
                    local.usage_page == AI_HID_PAGE_DIGITIZERS &&
                    local.usage == AI_HID_USAGE_FINGER &&
                    value == AI_HID_COLLECTION_LOGICAL)
                    next.finger = true;
                collections[collection_depth++] = next;
            } else if (item_tag == 12u) { /* End Collection */
                if (item_size != 0u || collection_depth == 0u)
                    return AI_ERR_PROTOCOL;
                --collection_depth;
            } else if (item_tag == 8u && collection_depth != 0u &&
                       collections[collection_depth - 1u].finger &&
                       local.usage_set &&
                       local.usage_page == AI_HID_PAGE_GENERIC_DESKTOP) {
                enum ai_status status = AI_OK;

                if (local.usage == AI_HID_USAGE_X)
                    status = ai_trackpad_axis_record(&globals, &result.x);
                else if (local.usage == AI_HID_USAGE_Y)
                    status = ai_trackpad_axis_record(&globals, &result.y);
                if (status != AI_OK)
                    return status;
            }
            AI_MEMSET(&local, sizeof(local));
        }
    }

    if (global_depth != 0u || collection_depth != 0u ||
        !result.x.valid || !result.y.valid ||
        result.x.unit != result.y.unit ||
        result.x.unit_exponent != result.y.unit_exponent)
        return AI_ERR_PROTOCOL;
    result.valid = true;
    *out = result;
    return AI_OK;
}
