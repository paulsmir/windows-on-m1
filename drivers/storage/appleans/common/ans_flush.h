/* SPDX-License-Identifier: MIT */
#ifndef ANS_FLUSH_H
#define ANS_FLUSH_H
#include "../include/apple_ans_broker_abi.h"
/* Exact flush elision. Windows sends every flush twice (SYNCHRONIZE CACHE, then SRB_FUNCTION_FLUSH)
 * and a controller flush is expensive on ANS. A flush may be completed without a command when
 * nothing can be in the volatile cache that an earlier, successfully completed flush did not
 * cover: that flush was submitted while no write-class command was outstanding, and no
 * write-class command has been submitted since. Durability is unchanged; this is not Asahi's
 * time-based limiter, which acknowledges flushes that have not happened yet.
 * Write-class = NVMe WRITE and Dataset Management. All calls under the adapter's interrupt lock. */
struct ans_flush_state {
    ans_u64 write_epoch;        /* write-class commands submitted so far */
    ans_u64 clean_epoch;        /* write_epoch covered by the last qualifying flush */
    ans_u32 writes_outstanding;
    ans_u32 clean_valid;
    ans_u64 elided, sent;
};
struct ans_flush_ticket { ans_u64 epoch; ans_u32 covers; };
void ans_flush_reset(struct ans_flush_state *s);               /* reset/stop: nothing is known clean */
void ans_flush_write_submitted(struct ans_flush_state *s);
void ans_flush_write_completed(struct ans_flush_state *s);
int ans_flush_can_elide(const struct ans_flush_state *s);
struct ans_flush_ticket ans_flush_submitted(struct ans_flush_state *s);
void ans_flush_completed(struct ans_flush_state *s, struct ans_flush_ticket t, int success);
void ans_flush_elided(struct ans_flush_state *s);
#endif
