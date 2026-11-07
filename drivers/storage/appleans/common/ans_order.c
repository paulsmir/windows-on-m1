/* SPDX-License-Identifier: MIT */
#include "ans_order.h"
#if defined(_KERNEL_MODE)
#define order_zero(p,n) RtlZeroMemory((p),(n))
#else
#include <string.h>
#define order_zero(p,n) memset((p),0,(n))
#endif
static int valid_tag(unsigned tag)
{ return tag >= ANS_ADMIN_TAGS && tag < ANS_QUEUE_DEPTH; }
static int token_matches(const struct ans_order *o, struct ans_order_token token)
{
    if (!o || !o->epoch || !valid_tag(token.tag)) return ANS_ORDER_INVALID;
    if (o->epoch != token.epoch || !token.sequence ||
        o->slot[token.tag].sequence != token.sequence ||
        o->slot[token.tag].state == ANS_ORDER_FREE) return ANS_ORDER_STALE;
    return ANS_ORDER_OK;
}
static void set_token(const struct ans_order *o, unsigned tag, struct ans_order_token *token)
{
    token->epoch = o->epoch;
    token->sequence = o->slot[tag].sequence;
    token->tag = (ans_u16)tag;
}
int ans_order_init(struct ans_order *o, ans_u64 epoch)
{
    if (!o || !epoch) return ANS_ORDER_INVALID;
    order_zero(o, sizeof(*o)); o->epoch = epoch;
    return ANS_ORDER_OK;
}
int ans_order_admit(struct ans_order *o, unsigned tag, enum ans_order_attribute attribute,
                    struct ans_order_token *token)
{
    if (!o || !o->epoch || !token || !valid_tag(tag) ||
        attribute < ANS_ORDER_SIMPLE || attribute > ANS_ORDER_UNTAGGED)
        return ANS_ORDER_INVALID;
    if (o->faulted) return ANS_ORDER_FAULT;
    if (o->slot[tag].state != ANS_ORDER_FREE) return ANS_ORDER_BUSY;
    for (unsigned i = ANS_ADMIN_TAGS; i < ANS_QUEUE_DEPTH; ++i)
        if (o->slot[i].state != ANS_ORDER_FREE &&
            (attribute == ANS_ORDER_UNTAGGED || o->slot[i].attribute == ANS_ORDER_UNTAGGED))
            return ANS_ORDER_BUSY;
    if (o->sequence == ANS_U64_MAX) {
        o->faulted = 1;
        return ANS_ORDER_EXHAUSTED;
    }
    o->slot[tag] = (struct ans_order_slot){++o->sequence, attribute, ANS_ORDER_QUEUED};
    set_token(o, tag, token);
    return ANS_ORDER_OK;
}
int ans_order_pick(struct ans_order *o, struct ans_order_token *token)
{
    unsigned chosen = ANS_QUEUE_DEPTH;
    if (!o || !o->epoch || !token) return ANS_ORDER_INVALID;
    if (o->faulted) return ANS_ORDER_FAULT;
    for (unsigned i = ANS_ADMIN_TAGS; i < ANS_QUEUE_DEPTH; ++i) {
        const struct ans_order_slot *s = &o->slot[i];
        if (s->state == ANS_ORDER_SELECTED || s->state == ANS_ORDER_QUARANTINED)
            return ANS_ORDER_BUSY;
        /* Serialize already executing HEADs (no preemption). SIMPLE requests
         * are still concurrent. New queued HEADs use LIFO once the active HEAD ends. */
        if (s->state == ANS_ORDER_ACTIVE && s->attribute == ANS_ORDER_HEAD)
            return ANS_ORDER_EMPTY;
        if (s->state == ANS_ORDER_QUEUED && s->attribute == ANS_ORDER_HEAD &&
            (chosen == ANS_QUEUE_DEPTH || s->sequence > o->slot[chosen].sequence))
            chosen = i;
    }
    if (chosen == ANS_QUEUE_DEPTH) {
        for (unsigned i = ANS_ADMIN_TAGS; i < ANS_QUEUE_DEPTH; ++i) {
            const struct ans_order_slot *s = &o->slot[i];
            int blocked = 0;
            if (s->state != ANS_ORDER_QUEUED) continue;
            for (unsigned j = ANS_ADMIN_TAGS; j < ANS_QUEUE_DEPTH; ++j) {
                const struct ans_order_slot *prior = &o->slot[j];
                if (prior->state == ANS_ORDER_FREE || prior->sequence >= s->sequence) continue;
                if (s->attribute == ANS_ORDER_ORDERED || s->attribute == ANS_ORDER_UNTAGGED ||
                    prior->attribute == ANS_ORDER_ORDERED || prior->attribute == ANS_ORDER_UNTAGGED ||
                    prior->attribute == ANS_ORDER_HEAD) { blocked = 1; break; }
            }
            if (!blocked && (chosen == ANS_QUEUE_DEPTH || s->sequence < o->slot[chosen].sequence))
                chosen = i;
        }
    }
    if (chosen == ANS_QUEUE_DEPTH) return ANS_ORDER_EMPTY;
    o->slot[chosen].state = ANS_ORDER_SELECTED;
    set_token(o, chosen, token);
    return ANS_ORDER_OK;
}
int ans_order_mark_submitted(struct ans_order *o, struct ans_order_token token)
{
    int rc = token_matches(o, token);
    if (rc) return rc;
    if (o->faulted) return ANS_ORDER_FAULT;
    if (o->slot[token.tag].state != ANS_ORDER_SELECTED) return ANS_ORDER_INVALID;
    o->slot[token.tag].state = ANS_ORDER_ACTIVE;
    return ANS_ORDER_OK;
}
int ans_order_complete(struct ans_order *o, struct ans_order_token token)
{
    int rc = token_matches(o, token);
    if (rc) return rc;
    /* Quarantine may only retire after stop proof; no guessed completion. Other
     * authenticated active completions may drain while the whole LU is faulted. */
    if (o->slot[token.tag].state != ANS_ORDER_ACTIVE) return ANS_ORDER_INVALID;
    order_zero(&o->slot[token.tag], sizeof(o->slot[token.tag]));
    return ANS_ORDER_OK;
}
int ans_order_cancel_queued(struct ans_order *o, struct ans_order_token token)
{
    int rc = token_matches(o, token);
    if (rc) return rc;
    if (o->slot[token.tag].state != ANS_ORDER_QUEUED &&
        o->slot[token.tag].state != ANS_ORDER_SELECTED) return ANS_ORDER_INVALID;
    order_zero(&o->slot[token.tag], sizeof(o->slot[token.tag]));
    return ANS_ORDER_OK;
}
int ans_order_quarantine(struct ans_order *o, struct ans_order_token token)
{
    int rc = token_matches(o, token);
    if (rc) return rc;
    if (o->slot[token.tag].state != ANS_ORDER_ACTIVE &&
        o->slot[token.tag].state != ANS_ORDER_SELECTED) return ANS_ORDER_INVALID;
    o->slot[token.tag].state = ANS_ORDER_QUARANTINED;
    o->faulted = 1;
    return ANS_ORDER_OK;
}
void ans_order_fault(struct ans_order *o) { if (o) o->faulted = 1; }
int ans_order_reset(struct ans_order *o, int dma_stopped)
{
    if (!o || !o->epoch) return ANS_ORDER_INVALID;
    if (!dma_stopped) { o->faulted = 1; return ANS_ORDER_STOP_REQUIRED; }
    if (o->epoch == ANS_U64_MAX) { o->faulted = 1; return ANS_ORDER_EXHAUSTED; }
    return ans_order_init(o, o->epoch + 1);
}
