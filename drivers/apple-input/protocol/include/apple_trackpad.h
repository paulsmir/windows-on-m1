#pragma once

#include "apple_spihid.h"

#ifdef AI_KERNEL_MODE
#ifndef AI_KERNEL_SIGNED_FIXED_WIDTH_TYPES
#define AI_KERNEL_SIGNED_FIXED_WIDTH_TYPES
typedef signed char int8_t;
typedef LONG int32_t;
#endif
#endif

#define AI_APPLE_TRACKPAD_HEADER_SIZE 48u
#define AI_APPLE_TRACKPAD_CONTACT_STRIDE 30u
#define AI_APPLE_TRACKPAD_MAX_CONTACTS 11u
#define AI_APPLE_TRACKPAD_CONTACT_COUNT_OFFSET 30u
#define AI_APPLE_TRACKPAD_TOUCH_MAJOR_OFFSET 16u

struct ai_trackpad_axis {
    int32_t logical_min;
    int32_t logical_max;
    int32_t physical_min;
    int32_t physical_max;
    uint32_t unit;
    int8_t unit_exponent;
    bool valid;
};

struct ai_trackpad_axis_contract {
    struct ai_trackpad_axis x;
    struct ai_trackpad_axis y;
    bool valid;
};

bool ai_apple_trackpad_release_candidate(const uint8_t *report, size_t length);
enum ai_status ai_trackpad_axis_contract_parse(
    const uint8_t *descriptor, size_t length,
    struct ai_trackpad_axis_contract *out);
