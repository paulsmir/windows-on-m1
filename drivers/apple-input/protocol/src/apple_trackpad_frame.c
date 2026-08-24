#include "apple_trackpad.h"

static uint16_t get_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

bool ai_apple_trackpad_release_candidate(const uint8_t *report, size_t length)
{
    uint8_t contact_count;
    size_t expected_length;
    size_t index;

    if (!report || length < AI_APPLE_TRACKPAD_HEADER_SIZE - 2u)
        return false;

    contact_count = report[AI_APPLE_TRACKPAD_CONTACT_COUNT_OFFSET];
    if (contact_count > AI_APPLE_TRACKPAD_MAX_CONTACTS)
        return false;

    expected_length = AI_APPLE_TRACKPAD_HEADER_SIZE - 2u +
        (size_t)contact_count * AI_APPLE_TRACKPAD_CONTACT_STRIDE;
    if (length != expected_length)
        return false;
    if (contact_count == 0u)
        return true;

    for (index = 0; index < contact_count; ++index) {
        size_t touch_major = AI_APPLE_TRACKPAD_HEADER_SIZE +
            index * AI_APPLE_TRACKPAD_CONTACT_STRIDE +
            AI_APPLE_TRACKPAD_TOUCH_MAJOR_OFFSET;

        if (get_le16(report + touch_major) == 0u)
            return true;
    }
    return false;
}
