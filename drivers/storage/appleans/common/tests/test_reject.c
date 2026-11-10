/* SPDX-License-Identifier: MIT */
/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * The reject receipt decides whether a bus-type change makes Windows send requests the miniport
 * refuses. It must count every reject, keep distinct codes apart, never lose a count when the
 * table is full, and serialise at fixed offsets. */
#include "../ans_reject.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint64_t get_le(const uint8_t *p, size_t n) { uint64_t v = 0; while (n--) v = (v << 8) | p[n]; return v; }
int main(void)
{
    struct ans_rejects r = {0};
    ans_reject_note(NULL, 1, 1); ans_reject_note(&r, 0, 5); ans_reject_note(&r, 4, 5);
    assert(!r.total[1] && !r.total[2] && !r.total[3] && !r.entry[0].kind);
    ans_reject_note(&r, ANS_REJECT_CDB, 0x1b); ans_reject_note(&r, ANS_REJECT_CDB, 0x1b);
    ans_reject_note(&r, ANS_REJECT_IO_CONTROL, 0x1b); /* same code, other kind: own slot */
    ans_reject_note(&r, ANS_REJECT_SRB_FUNCTION, 0x28);
    assert(r.total[3] == 2 && r.total[2] == 1 && r.total[1] == 1 && !r.overflow);
    assert(r.entry[0].kind == 3 && r.entry[0].code == 0x1b && r.entry[0].count == 2);
    assert(r.entry[1].kind == 2 && r.entry[1].count == 1 && r.entry[2].kind == 1 && r.entry[2].code == 0x28);
    for (uint32_t c = 0; c < 20; c++) ans_reject_note(&r, ANS_REJECT_CDB, 0x100 + c);
    assert(r.total[3] == 22 && r.overflow == 7 && r.entry[15].code == 0x100 + 12);
    ans_reject_note(&r, ANS_REJECT_CDB, 0x1b); assert(r.entry[0].count == 3 && r.overflow == 7);

    uint8_t raw[ANS_REJECT_IOCTL_TOTAL + 2], *b = raw + 1;
    memset(raw, 0xee, sizeof raw); memset(b, 0, ANS_REJECT_IOCTL_TOTAL);
    assert(ans_reject_io_control_code(b, 19) == 0xffffffffu && ans_reject_io_control_code(NULL, 64) == 0xffffffffu);
    b[0] = 28; memcpy(b + 4, "APANS001", 8); b[12] = 10; b[16] = 0x08; b[17] = 0x60; b[18] = 0x1b;
    b[24] = (uint8_t)ANS_REJECT_IOCTL_PAYLOAD; b[25] = (uint8_t)(ANS_REJECT_IOCTL_PAYLOAD >> 8);
    b[28] = 1; b[32] = b[24]; b[33] = b[25];
    assert(ANS_REJECT_IOCTL_TOTAL == 324 && ans_reject_io_control_code(b, ANS_REJECT_IOCTL_TOTAL) == 0x001b6008u);
    assert(ans_reject_ioctl_validate(b, ANS_REJECT_IOCTL_TOTAL) && !ans_reject_ioctl_validate(b, 323));
    b[16] = 4; assert(!ans_reject_ioctl_validate(b, ANS_REJECT_IOCTL_TOTAL)); b[16] = 8;
    b[4] = 'X'; assert(!ans_reject_ioctl_reply(b, ANS_REJECT_IOCTL_TOTAL, &r)); b[4] = 'A';
    b[20] = 7; assert(ans_reject_ioctl_reply(b, ANS_REJECT_IOCTL_TOTAL, &r));
    assert(raw[0] == 0xee && raw[sizeof raw - 1] == 0xee && get_le(b + 20, 4) == 0);
    const uint8_t *p = b + 28;
    assert(get_le(p + 8, 8) == 1 && get_le(p + 16, 8) == 1 && get_le(p + 24, 8) == 23 && get_le(p + 32, 8) == 7);
    assert(get_le(p + 40, 4) == 3 && get_le(p + 44, 4) == 0x1b && get_le(p + 48, 8) == 3);
    assert(get_le(p + 40 + 15 * 16 + 4, 4) == 0x100 + 12);
    puts("ANS reject receipt tests PASS");
    return 0;
}
