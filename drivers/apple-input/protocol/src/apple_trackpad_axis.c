#include "apple_trackpad.h"

static uint16_t ai_trackpad_get_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t ai_trackpad_get_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int32_t ai_trackpad_get_i16(const uint8_t *bytes)
{
    uint32_t value = ai_trackpad_get_le16(bytes);

    if (value & 0x8000u)
        value |= 0xffff0000u;
    return (int32_t)value;
}

enum ai_status ai_trackpad_dimensions_parse(
    const uint8_t *report, size_t length,
    struct ai_trackpad_dimensions *out)
{
    struct ai_trackpad_dimensions result;

    if (!out)
        return AI_ERR_ARGUMENT;
    AI_MEMSET(out, sizeof(*out));
    if (!report)
        return AI_ERR_ARGUMENT;
    if (length < 17u)
        return AI_ERR_LENGTH;
    if (report[0] != 0xd9u)
        return AI_ERR_PROTOCOL;

    AI_MEMSET(&result, sizeof(result));
    result.width_hundredths_mm = ai_trackpad_get_le32(report + 1);
    result.height_hundredths_mm = ai_trackpad_get_le32(report + 5);
    result.min_x = ai_trackpad_get_i16(report + 9);
    result.min_y = ai_trackpad_get_i16(report + 11);
    result.max_x = ai_trackpad_get_i16(report + 13);
    result.max_y = ai_trackpad_get_i16(report + 15);
    if (!result.width_hundredths_mm || !result.height_hundredths_mm ||
        result.min_x >= result.max_x || result.min_y >= result.max_y)
        return AI_ERR_PROTOCOL;
    result.valid = true;
    *out = result;
    return AI_OK;
}

static bool ai_trackpad_hundredths_mm_to_hundredths_inch(
    uint32_t hundredths_mm, int32_t *out)
{
    uint64_t scaled;

    if (!out || !hundredths_mm)
        return false;
    scaled = (uint64_t)hundredths_mm * 5u + 63u;
    scaled /= 127u;
    if (!scaled || scaled > 0x7fffffffu)
        return false;
    *out = (int32_t)scaled;
    return true;
}

enum ai_status ai_trackpad_axis_contract_from_dimensions(
    const struct ai_trackpad_dimensions *dimensions,
    struct ai_trackpad_axis_contract *out)
{
    struct ai_trackpad_axis_contract result;

    if (!out)
        return AI_ERR_ARGUMENT;
    AI_MEMSET(out, sizeof(*out));
    if (!dimensions)
        return AI_ERR_ARGUMENT;
    if (!dimensions->valid ||
        dimensions->min_x >= dimensions->max_x ||
        dimensions->min_y >= dimensions->max_y)
        return AI_ERR_PROTOCOL;

    AI_MEMSET(&result, sizeof(result));
    result.x.logical_min = dimensions->min_x;
    result.x.logical_max = dimensions->max_x;
    result.y.logical_min = dimensions->min_y;
    result.y.logical_max = dimensions->max_y;
    if (!ai_trackpad_hundredths_mm_to_hundredths_inch(
            dimensions->width_hundredths_mm, &result.x.physical_max) ||
        !ai_trackpad_hundredths_mm_to_hundredths_inch(
            dimensions->height_hundredths_mm, &result.y.physical_max))
        return AI_ERR_PROTOCOL;
    result.x.unit = 0x13u;
    result.y.unit = 0x13u;
    result.x.unit_exponent = -2;
    result.y.unit_exponent = -2;
    result.x.valid = true;
    result.y.valid = true;
    result.valid = true;
    *out = result;
    return AI_OK;
}
