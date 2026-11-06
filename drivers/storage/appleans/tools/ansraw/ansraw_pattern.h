/* SPDX-License-Identifier: MIT */
/* ANS008 raw-write pattern. Every 4 KiB block depends on its absolute LBA, so a
 * misplaced, duplicated or stale block changes the range hash. Shared by
 * ansraw.exe (WinPE) and tools/ans_raw_pattern.py (host/recovery reference). */
#ifndef ANSRAW_PATTERN_H
#define ANSRAW_PATTERN_H
#include <stdint.h>
#define ANSRAW_BLOCK 4096u
#define ANSRAW_SEED 0x414E533030380000ULL /* "ANS008" */
static inline void ansraw_fill_block(uint64_t lba, uint8_t out[ANSRAW_BLOCK])
{
    for (uint32_t j = 0; j < ANSRAW_BLOCK / 8; j++) {
        uint64_t v = ANSRAW_SEED ^ (lba << 12) ^ j;
        for (unsigned b = 0; b < 8; b++) out[j * 8 + b] = (uint8_t)(v >> (8 * b));
    }
}
#endif
