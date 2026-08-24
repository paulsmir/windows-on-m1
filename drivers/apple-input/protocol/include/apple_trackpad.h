#pragma once

#ifdef AI_KERNEL_MODE
#include <ntddk.h>
#ifndef AI_KERNEL_FIXED_WIDTH_TYPES
#define AI_KERNEL_FIXED_WIDTH_TYPES
typedef UCHAR uint8_t;
typedef USHORT uint16_t;
#endif
#ifndef AI_KERNEL_BOOL_TYPE
#define AI_KERNEL_BOOL_TYPE
typedef unsigned char bool;
#define true 1
#define false 0
#endif
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#define AI_APPLE_TRACKPAD_HEADER_SIZE 48u
#define AI_APPLE_TRACKPAD_CONTACT_STRIDE 30u
#define AI_APPLE_TRACKPAD_MAX_CONTACTS 11u
#define AI_APPLE_TRACKPAD_CONTACT_COUNT_OFFSET 30u
#define AI_APPLE_TRACKPAD_TOUCH_MAJOR_OFFSET 16u

bool ai_apple_trackpad_release_candidate(const uint8_t *report, size_t length);
