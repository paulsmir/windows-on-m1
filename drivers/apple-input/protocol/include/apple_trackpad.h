#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AI_APPLE_TRACKPAD_HEADER_SIZE 48u
#define AI_APPLE_TRACKPAD_CONTACT_STRIDE 30u
#define AI_APPLE_TRACKPAD_MAX_CONTACTS 11u
#define AI_APPLE_TRACKPAD_CONTACT_COUNT_OFFSET 30u
#define AI_APPLE_TRACKPAD_TOUCH_MAJOR_OFFSET 16u

bool ai_apple_trackpad_release_candidate(const uint8_t *report, size_t length);
