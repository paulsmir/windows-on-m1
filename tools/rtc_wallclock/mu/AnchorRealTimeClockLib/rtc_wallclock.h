/* SPDX-License-Identifier: MIT */
/*
 * Apple RTC (SMC counter + PMU offset) -> wall clock, and wall clock -> EFI-style calendar time.
 * Reference implementation for the Mu wall-clock design (investigation/MU_RTC_WALLCLOCK_DESIGN.md); header-only,
 * freestanding (stdint only), shared by the m1n1 reader and the Mu runtime GetTime once implemented.
 *
 * RTC model (behaviour from Asahi rtc-macsmc / apple,smc-rtc binding; no code copied): the SMC key CLKM holds a
 * 48-bit counter in 32768 Hz ticks; the PMU NVMEM cell rtc_offset holds a 48-bit offset in the same ticks. Wall time
 * = sign_extend_48(ctr + off) / 32768 seconds since 1970-01-01 UTC. Writers store off = (t << 15) - ctr truncated to
 * 48 bits, so the sum is taken modulo 2^48 before the sign extension.
 */
#ifndef RTC_WALLCLOCK_H
#define RTC_WALLCLOCK_H

#include <stdint.h>

#define RTC_WC_BITS 48
#define RTC_WC_MASK ((1ULL << RTC_WC_BITS) - 1)
#define RTC_WC_HZ_SHIFT 15 /* 32768 Hz */

/* 48-bit two's complement value in the low bits of v -> int64 */
static inline int64_t rtc_wc_sext48(uint64_t v)
{
    v &= RTC_WC_MASK;
    return (v & (1ULL << (RTC_WC_BITS - 1))) ? (int64_t)(v | ~RTC_WC_MASK) : (int64_t)v;
}

/* floor division for a possibly negative numerator and a positive divisor */
static inline int64_t rtc_wc_floordiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

/* counter + offset (both 48-bit ticks) -> Unix milliseconds (floor) */
static inline int64_t rtc_wc_unix_ms(uint64_t ctr, uint64_t off)
{
    int64_t ticks = rtc_wc_sext48(ctr + off);
    int64_t secs = rtc_wc_floordiv(ticks, 1LL << RTC_WC_HZ_SHIFT);
    int64_t frac = ticks - secs * (1LL << RTC_WC_HZ_SHIFT); /* 0 .. 32767 */
    return secs * 1000 + (frac * 1000) / (1LL << RTC_WC_HZ_SHIFT);
}

/* the offset a writer stores so that the current second is t (stage 2 only; shown for the round-trip test) */
static inline uint64_t rtc_wc_offset_for(int64_t unix_s, uint64_t ctr)
{
    return ((uint64_t)unix_s << RTC_WC_HZ_SHIFT) - (ctr & RTC_WC_MASK);
}

/* anchor + generic-timer elapsed time; the counter difference is modular, so a 64-bit wrap is harmless */
static inline int64_t rtc_wc_now_ms(int64_t anchor_ms, uint64_t anchor_cnt, uint64_t cnt, uint64_t freq)
{
    uint64_t d = cnt - anchor_cnt;
    return anchor_ms + (int64_t)(d / freq) * 1000 + (int64_t)(((d % freq) * 1000) / freq);
}

struct rtc_wc_time {
    uint16_t year;  /* 1900 .. 9999 (EFI_TIME range) */
    uint8_t month;  /* 1 .. 12 */
    uint8_t day;    /* 1 .. 31 */
    uint8_t hour, minute, second;
    uint32_t nanosecond;
};

/* Unix ms -> civil UTC (proleptic Gregorian). Returns 0 on success, -1 outside the EFI_TIME year range. */
static inline int rtc_wc_to_civil(int64_t unix_ms, struct rtc_wc_time *t)
{
    int64_t secs = rtc_wc_floordiv(unix_ms, 1000);
    int64_t ms = unix_ms - secs * 1000;
    int64_t days = rtc_wc_floordiv(secs, 86400);
    int64_t sod = secs - days * 86400;
    /* days since 1970-01-01 -> y/m/d, era-based (400-year cycles of 146097 days) */
    int64_t z = days + 719468;
    int64_t era = rtc_wc_floordiv(z, 146097);
    int64_t doe = z - era * 146097;                                    /* 0 .. 146096 */
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* 0 .. 399 */
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);             /* 0 .. 365, year starts 1 March */
    int64_t mp = (5 * doy + 2) / 153;                                  /* 0 .. 11, March-based */
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2)
        y += 1;
    if (y < 1900 || y > 9999)
        return -1;
    t->year = (uint16_t)y;
    t->month = (uint8_t)m;
    t->day = (uint8_t)d;
    t->hour = (uint8_t)(sod / 3600);
    t->minute = (uint8_t)((sod % 3600) / 60);
    t->second = (uint8_t)(sod % 60);
    t->nanosecond = (uint32_t)(ms * 1000000);
    return 0;
}

#endif
