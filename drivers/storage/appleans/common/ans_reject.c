/* SPDX-License-Identifier: MIT */
#include "ans_reject.h"
static ans_u64 get_le(const ans_u8 *p, unsigned n)
{
    ans_u64 v = 0;
    while (n--) v = (v << 8) | p[n];
    return v;
}
static void put_le(ans_u8 *p, ans_u64 v, unsigned n)
{
    while (n--) { *p++ = (ans_u8)v; v >>= 8; }
}
void ans_reject_note(struct ans_rejects *r, ans_u32 kind, ans_u32 code)
{
    ans_u32 i;
    if (!r || kind < ANS_REJECT_SRB_FUNCTION || kind > ANS_REJECT_MISALIGNED) return;
    r->total[kind]++;
    for (i = 0; i < ANS_REJECT_SLOTS; i++) {
        struct ans_reject_entry *e = &r->entry[i];
        if (!e->kind) { e->kind = kind; e->code = code; }
        if (e->kind == kind && e->code == code) { e->count++; return; }
    }
    r->overflow++;
}
ans_u32 ans_reject_io_control_code(const void *buffer, ans_u32 bytes)
{
    if (!buffer || bytes < 20) return 0xffffffffu;
    return (ans_u32)get_le((const ans_u8 *)buffer + 16, 4);
}
int ans_reject_ioctl_validate(const void *buffer, ans_u32 bytes)
{
    static const ans_u8 signature[8] = {'A','P','A','N','S','0','0','1'};
    const ans_u8 *b = buffer;
    ans_u32 timeout, i;
    if (!b || bytes != ANS_REJECT_IOCTL_TOTAL) return 0;
    for (i = 0; i < 8; i++) if (b[4 + i] != signature[i]) return 0;
    timeout = (ans_u32)get_le(b + 12, 4);
    return get_le(b, 4) == 28 && timeout && timeout <= 60 &&
        get_le(b + 16, 4) == ANS_REJECT_IOCTL_QUERY &&
        get_le(b + 24, 4) == ANS_REJECT_IOCTL_PAYLOAD &&
        get_le(b + 28, 4) == 1 && get_le(b + 32, 4) == ANS_REJECT_IOCTL_PAYLOAD;
}
int ans_reject_ioctl_reply(void *buffer, ans_u32 bytes, const struct ans_rejects *r)
{
    ans_u8 *b = buffer, *p;
    ans_u32 i;
    if (!r || !ans_reject_ioctl_validate(buffer, bytes)) return 0;
    put_le(b + 20, 0, 4);
    p = b + 28;
    for (i = 1; i <= 3; i++) put_le(p + i * 8, r->total[i], 8);
    put_le(p + 32, r->overflow, 8);
    for (i = 0; i < ANS_REJECT_SLOTS; i++) {
        put_le(p + 40 + i * 16, r->entry[i].kind, 4);
        put_le(p + 44 + i * 16, r->entry[i].code, 4);
        put_le(p + 48 + i * 16, r->entry[i].count, 8);
    }
    return 1;
}
