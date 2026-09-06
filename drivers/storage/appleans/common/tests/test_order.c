/* SPDX-License-Identifier: MIT */
#include "../ans_order.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* WHAT REAL BUG OR INVARIANT? Arrival tags are recyclable, so they cannot order
 * requests. An ORDERED task must not publish before ALL older tasks finish;
 * queued HEADs must displace older queued tasks (including older HEADs). A stale
 * completion must never release a newly reused slot or a quarantined request. */
static struct ans_order o;
static struct ans_order_token token[ANS_QUEUE_DEPTH];
static unsigned id(unsigned n) { return ANS_ADMIN_TAGS + n; }
static void reset(void) { assert(ans_order_init(&o, 7) == ANS_ORDER_OK); }
static void admit(unsigned n, enum ans_order_attribute a)
{ assert(ans_order_admit(&o, id(n), a, &token[id(n)]) == ANS_ORDER_OK); }
static void publish(unsigned n)
{
    struct ans_order_token picked = {0};
    assert(ans_order_pick(&o, &picked) == ANS_ORDER_OK);
    assert(picked.tag == id(n) && picked.sequence == token[id(n)].sequence);
    assert(ans_order_mark_submitted(&o, picked) == ANS_ORDER_OK);
}
static void finish(unsigned n) { assert(ans_order_complete(&o, token[id(n)]) == ANS_ORDER_OK); }
static void empty(void)
{ struct ans_order_token picked; assert(ans_order_pick(&o, &picked) == ANS_ORDER_EMPTY); }
static void simple_ordered(void)
{
    reset();
    admit(8, ANS_ORDER_SIMPLE); admit(2, ANS_ORDER_SIMPLE);
    admit(0, ANS_ORDER_ORDERED); admit(1, ANS_ORDER_SIMPLE);
    publish(8); publish(2); empty();
    finish(2); empty(); /* One earlier task still active, despite tag/arrival reuse. */
    finish(8); publish(0); empty(); finish(0); publish(1); finish(1); empty();
    reset();
    for (unsigned i = 0; i < 20; ++i) admit(i, ANS_ORDER_SIMPLE);
    admit(20, ANS_ORDER_ORDERED); admit(21, ANS_ORDER_ORDERED);
    for (unsigned i = 0; i < 20; ++i) publish(i);
    for (unsigned i = 20; i > 1; --i) { finish(i - 1); empty(); }
    finish(0); publish(20); empty(); finish(20); publish(21); finish(21);
}
static void heads(void)
{
    reset();
    admit(0, ANS_ORDER_SIMPLE); publish(0);
    admit(1, ANS_ORDER_ORDERED); admit(2, ANS_ORDER_SIMPLE);
    admit(3, ANS_ORDER_HEAD); admit(4, ANS_ORDER_HEAD);
    publish(4); empty(); finish(4); publish(3); empty(); finish(3);
    empty(); /* HEAD completion does not discharge the older SIMPLE prerequisite. */
    finish(0); publish(1);
    admit(5, ANS_ORDER_HEAD); publish(5); /* Later HEAD bypasses active ORDERED too. */
    empty(); finish(5); empty(); finish(1); publish(2); finish(2);
    reset();
    admit(0, ANS_ORDER_HEAD); publish(0);
    admit(1, ANS_ORDER_HEAD); admit(2, ANS_ORDER_HEAD);
    empty(); /* Executing HEAD is not preempted. Queued successors still LIFO. */
    finish(0); publish(2); finish(2); publish(1); finish(1);
}
static void tokens_and_faults(void)
{
    struct ans_order_token picked, old, invalid;
    reset(); admit(0, ANS_ORDER_SIMPLE); old = token[id(0)];
    assert(ans_order_mark_submitted(&o, old) == ANS_ORDER_INVALID);
    assert(ans_order_complete(&o, old) == ANS_ORDER_INVALID);
    assert(ans_order_pick(&o, &picked) == ANS_ORDER_OK);
    assert(ans_order_pick(&o, &picked) == ANS_ORDER_BUSY);
    assert(ans_order_mark_submitted(&o, old) == ANS_ORDER_OK);
    assert(ans_order_cancel_queued(&o, old) == ANS_ORDER_INVALID);
    finish(0); admit(0, ANS_ORDER_ORDERED);
    assert(ans_order_complete(&o, old) == ANS_ORDER_STALE);
    invalid = token[id(0)]; invalid.epoch++;
    assert(ans_order_complete(&o, invalid) == ANS_ORDER_STALE);
    invalid = token[id(0)]; invalid.tag = 0;
    assert(ans_order_complete(&o, invalid) == ANS_ORDER_INVALID);
    publish(0); admit(1, ANS_ORDER_SIMPLE);
    assert(ans_order_quarantine(&o, token[id(0)]) == ANS_ORDER_OK);
    assert(ans_order_pick(&o, &picked) == ANS_ORDER_FAULT);
    assert(ans_order_complete(&o, token[id(0)]) == ANS_ORDER_INVALID);
    assert(ans_order_admit(&o, id(2), ANS_ORDER_HEAD, &picked) == ANS_ORDER_FAULT);
    assert(ans_order_reset(&o, 0) == ANS_ORDER_STOP_REQUIRED);
    assert(o.slot[id(0)].state == ANS_ORDER_QUARANTINED && o.slot[id(1)].state == ANS_ORDER_QUEUED);
    old = token[id(0)];
    assert(ans_order_reset(&o, 1) == ANS_ORDER_OK && o.epoch == 8);
    admit(0, ANS_ORDER_SIMPLE); publish(0);
    assert(ans_order_complete(&o, old) == ANS_ORDER_STALE);
    finish(0);
    reset(); admit(0, ANS_ORDER_SIMPLE); publish(0); admit(1, ANS_ORDER_ORDERED);
    ans_order_fault(&o);
    finish(0); /* A verified unrelated completion may drain; dispatch stays frozen. */
    assert(ans_order_pick(&o, &picked) == ANS_ORDER_FAULT);
    assert(ans_order_cancel_queued(&o, token[id(1)]) == ANS_ORDER_OK);
}
static void admission_and_exhaustion(void)
{
    struct ans_order_token t;
    reset();
    assert(ans_order_admit(&o, 0, ANS_ORDER_SIMPLE, &t) == ANS_ORDER_INVALID);
    assert(ans_order_admit(&o, ANS_QUEUE_DEPTH, ANS_ORDER_SIMPLE, &t) == ANS_ORDER_INVALID);
    assert(ans_order_admit(&o, id(0), (enum ans_order_attribute)99, &t) == ANS_ORDER_INVALID);
    admit(0, ANS_ORDER_UNTAGGED);
    assert(ans_order_admit(&o, id(1), ANS_ORDER_HEAD, &t) == ANS_ORDER_BUSY);
    publish(0); finish(0);
    admit(0, ANS_ORDER_SIMPLE);
    assert(ans_order_admit(&o, id(0), ANS_ORDER_SIMPLE, &t) == ANS_ORDER_BUSY);
    assert(ans_order_admit(&o, id(1), ANS_ORDER_UNTAGGED, &t) == ANS_ORDER_BUSY);
    assert(ans_order_cancel_queued(&o, token[id(0)]) == ANS_ORDER_OK);
    admit(1, ANS_ORDER_UNTAGGED); publish(1); finish(1);
    reset();
    for (unsigned i = 0; i < ANS_QUEUE_DEPTH - ANS_ADMIN_TAGS; ++i) admit(i, ANS_ORDER_SIMPLE);
    for (unsigned i = 0; i < ANS_QUEUE_DEPTH - ANS_ADMIN_TAGS; ++i) publish(i);
    for (unsigned i = ANS_QUEUE_DEPTH - ANS_ADMIN_TAGS; i; --i) finish(i - 1);
    reset(); o.sequence = ANS_U64_MAX - 1;
    admit(0, ANS_ORDER_SIMPLE); assert(token[id(0)].sequence == ANS_U64_MAX);
    publish(0); finish(0);
    assert(ans_order_admit(&o, id(1), ANS_ORDER_SIMPLE, &t) == ANS_ORDER_EXHAUSTED && o.faulted);
    assert(ans_order_reset(&o, 0) == ANS_ORDER_STOP_REQUIRED);
    assert(ans_order_reset(&o, 1) == ANS_ORDER_OK && !o.sequence);
    assert(ans_order_init(&o, ANS_U64_MAX) == ANS_ORDER_OK);
    admit(0, ANS_ORDER_SIMPLE); publish(0);
    assert(ans_order_reset(&o, 1) == ANS_ORDER_EXHAUSTED && o.slot[id(0)].state == ANS_ORDER_ACTIVE);
}
int main(void)
{
    simple_ordered(); heads(); tokens_and_faults(); admission_and_exhaustion();
    puts("ANS SIMPLE/ORDERED/HEAD task ordering and lifetime tests PASS");
    return 0;
}
