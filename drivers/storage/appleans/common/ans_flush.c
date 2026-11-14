/* SPDX-License-Identifier: MIT */
#include "ans_flush.h"
void ans_flush_reset(struct ans_flush_state *s)
{
    if (!s) return;
    s->clean_valid = 0;
    s->writes_outstanding = 0;
    s->write_epoch++; /* anything in flight at a reset may or may not have reached the cache */
}
void ans_flush_write_submitted(struct ans_flush_state *s)
{
    if (!s) return;
    s->write_epoch++;
    s->writes_outstanding++;
}
void ans_flush_write_completed(struct ans_flush_state *s)
{
    if (s && s->writes_outstanding) s->writes_outstanding--;
}
int ans_flush_can_elide(const struct ans_flush_state *s)
{
    return s && s->clean_valid && !s->writes_outstanding && s->clean_epoch == s->write_epoch;
}
struct ans_flush_ticket ans_flush_submitted(struct ans_flush_state *s)
{
    struct ans_flush_ticket t = {0, 0};
    if (!s) return t;
    t.epoch = s->write_epoch;
    t.covers = s->writes_outstanding == 0;
    s->sent++;
    return t;
}
void ans_flush_completed(struct ans_flush_state *s, struct ans_flush_ticket t, int success)
{
    if (!s || !success || !t.covers) return;
    /* Later flushes never move the mark backwards. */
    if (!s->clean_valid || t.epoch > s->clean_epoch) { s->clean_epoch = t.epoch; s->clean_valid = 1; }
}
void ans_flush_elided(struct ans_flush_state *s) { if (s) s->elided++; }
