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
#define AI_PTP_MAX_CONTACTS 5u

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

struct ai_apple_trackpad_contact {
    int32_t x;
    int32_t y;
};

struct ai_apple_trackpad_frame {
    bool button;
    uint8_t count;
    struct ai_apple_trackpad_contact contacts[AI_APPLE_TRACKPAD_MAX_CONTACTS];
};

struct ai_trackpad_physical_slot {
    bool active;
    bool admitted;
    uint8_t windows_id;
    int32_t x;
    int32_t y;
};

struct ai_trackpad_tracker {
    struct ai_trackpad_physical_slot slots[AI_APPLE_TRACKPAD_MAX_CONTACTS];
};

struct ai_trackpad_output_contact {
    bool tip;
    uint8_t id;
    int32_t x;
    int32_t y;
};

struct ai_trackpad_output_frame {
    bool button;
    uint8_t count;
    uint8_t active_count;
    uint8_t suppressed_count;
    struct ai_trackpad_output_contact contacts[AI_PTP_MAX_CONTACTS];
};

bool ai_apple_trackpad_release_candidate(const uint8_t *report, size_t length);
enum ai_status ai_trackpad_axis_contract_parse(
    const uint8_t *descriptor, size_t length,
    struct ai_trackpad_axis_contract *out);
enum ai_status ai_apple_trackpad_decode(
    const uint8_t *report, size_t length,
    struct ai_apple_trackpad_frame *out);
enum ai_status ai_trackpad_tracker_update(
    struct ai_trackpad_tracker *tracker,
    const struct ai_apple_trackpad_frame *frame,
    struct ai_trackpad_output_frame *out);
