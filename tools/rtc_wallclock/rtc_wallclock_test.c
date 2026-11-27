/* SPDX-License-Identifier: MIT */
/* Deterministic unit test of rtc_wallclock.h: epoch, leap years (2000 yes, 2100 no, 2024 yes), the 48-bit counter
 * wrap of ctr + off, negative sums (before 1970), the writer/reader round trip, the generic-timer anchor including a
 * 64-bit counter wrap, and the EFI_TIME year range. Exit 0 = all pass. */
#include <stdio.h>
#include <string.h>
#include "rtc_wallclock.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void civil(int64_t ms, int y, int mo, int d, int h, int mi, int s, int ns)
{
    struct rtc_wc_time t;
    int r = rtc_wc_to_civil(ms, &t);
    CHECK(r == 0 && t.year == y && t.month == mo && t.day == d && t.hour == h && t.minute == mi && t.second == s &&
          t.nanosecond == (uint32_t)ns,
          "civil(%lld) = r%d %04u-%02u-%02u %02u:%02u:%02u.%09u, want %04d-%02d-%02d %02d:%02d:%02d.%09d",
          (long long)ms, r, t.year, t.month, t.day, t.hour, t.minute, t.second, t.nanosecond, y, mo, d, h, mi, s, ns);
}

static uint64_t lcg = 0x2545F4914F6CDD1DULL;
static uint64_t rnd(void) { lcg = lcg * 6364136223846793005ULL + 1442695040888963407ULL; return lcg; }

int main(void)
{
    /* epoch and calendar edges */
    civil(0, 1970, 1, 1, 0, 0, 0, 0);
    civil(951782400LL * 1000, 2000, 2, 29, 0, 0, 0, 0);         /* 2000 is a leap year */
    civil(951868800LL * 1000, 2000, 3, 1, 0, 0, 0, 0);
    civil(4107542399LL * 1000, 2100, 2, 28, 23, 59, 59, 0);     /* 2100 is not */
    civil(4107542400LL * 1000, 2100, 3, 1, 0, 0, 0, 0);
    civil(1709164800LL * 1000 + 999, 2024, 2, 29, 0, 0, 0, 999000000);
    civil(1795788732LL * 1000 + 250, 2026, 11, 27, 14, 12, 12, 250000000);
    civil(-2208988800LL * 1000, 1900, 1, 1, 0, 0, 0, 0);         /* EFI_TIME lower bound */
    civil(-1, 1969, 12, 31, 23, 59, 59, 999000000);              /* floor, not truncation */
    {
        struct rtc_wc_time t;
        CHECK(rtc_wc_to_civil(-2208988800LL * 1000 - 1, &t) == -1, "1899 must be refused");
        CHECK(rtc_wc_to_civil(253402300800LL * 1000, &t) == -1, "10000 must be refused");
    }

    /* counter + offset: 32768 Hz, floor to ms */
    CHECK(rtc_wc_unix_ms(0, 0) == 0, "zero");
    CHECK(rtc_wc_unix_ms(32768, 0) == 1000, "one second");
    CHECK(rtc_wc_unix_ms(16384, 16384) == 1000, "split");
    CHECK(rtc_wc_unix_ms(1, 0) == 0 && rtc_wc_unix_ms(33, 0) == 1, "sub-ms floor");
    {
        uint64_t t0 = 1795788732ULL;                             /* a 2026 time */
        uint64_t ctr = 0xFFFFFFFFFFFFULL - 32768 * 5;            /* counter 5 s before its 48-bit wrap */
        uint64_t off = rtc_wc_offset_for((int64_t)t0, ctr);
        off &= RTC_WC_MASK;                                       /* the writer stores 48 bits */
        CHECK(rtc_wc_unix_ms(ctr, off) == (int64_t)t0 * 1000, "before wrap: %lld", (long long)rtc_wc_unix_ms(ctr, off));
        uint64_t ctr2 = (ctr + 32768 * 10) & RTC_WC_MASK;        /* 10 s later, counter wrapped */
        CHECK(rtc_wc_unix_ms(ctr2, off) == ((int64_t)t0 + 10) * 1000, "after wrap: %lld",
              (long long)rtc_wc_unix_ms(ctr2, off));
    }
    /* negative sum (bit 47 set) -> before 1970 */
    CHECK(rtc_wc_unix_ms(RTC_WC_MASK, 0) == -1, "sum -1 tick -> -1 ms (floor): %lld", (long long)rtc_wc_unix_ms(RTC_WC_MASK, 0));
    CHECK(rtc_wc_unix_ms(0, (uint64_t)(-(int64_t)32768 * 86400) & RTC_WC_MASK) == -86400000LL, "one day before epoch");

    /* writer/reader round trip, random counters, times 1900 .. 2106-02-07 (the format holds +-2^32 s around 1970:
     * 48-bit ticks at 2^15 Hz; Asahi rtc-macsmc range_min/range_max state the same span) */
    for (int i = 0; i < 100000; i++) {
        uint64_t ctr = rnd() & RTC_WC_MASK;
        int64_t t = -2208988800LL + (int64_t)(rnd() % (4294967295ULL + 2208988800ULL));
        uint64_t off = rtc_wc_offset_for(t, ctr) & RTC_WC_MASK;
        uint64_t sub = rnd() % 32768;                             /* inside the set second */
        if (rtc_wc_floordiv(rtc_wc_unix_ms((ctr + sub) & RTC_WC_MASK, off), 1000) != t) {
            CHECK(0, "round trip t=%lld ctr=%#llx", (long long)t, (unsigned long long)ctr); break;
        }
    }
    /* beyond the span the stored value wraps: 2^32 s after 1970 reads back as -2^32 s (documented limit) */
    CHECK(rtc_wc_unix_ms(rtc_wc_offset_for(4294967296LL, 0) & RTC_WC_MASK, 0) == -4294967296000LL, "format wraps after 2106");

    /* anchor + generic timer (24 MHz), including a 64-bit counter wrap */
    CHECK(rtc_wc_now_ms(1000, 0, 24000000, 24000000) == 2000, "anchor +1 s");
    CHECK(rtc_wc_now_ms(1000, 0xFFFFFFFFFFFFFFFFULL - 11999999, 12000000, 24000000) == 2000, "anchor across 64-bit wrap");
    CHECK(rtc_wc_now_ms(0, 0, 23999, 24000000) == 0 && rtc_wc_now_ms(0, 0, 24000, 24000000) == 1, "anchor ms floor");

    printf("RTC_WALLCLOCK_TEST %s fails=%d\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
