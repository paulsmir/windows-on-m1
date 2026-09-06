/* SPDX-License-Identifier: MIT */
#ifndef APPLE_ANS_RESOURCES_H
#define APPLE_ANS_RESOURCES_H
#include "../include/apple_ans_broker_abi.h"
struct ans_resources {
    ans_u64 base;
    ans_u32 length, range_count;
    ans_u32 vector, level, vector2, level2;
    int memory, level_sensitive, virtual_device;
};
/* Storport may describe a bus IRQ by vector or level. Neither translated value
 * is the raw ACPI GSIV. Zero/SP_UNINITIALIZED_VALUE mean absent/uninitialized. */
int ans_resources_valid(const struct ans_resources *r);
#endif
