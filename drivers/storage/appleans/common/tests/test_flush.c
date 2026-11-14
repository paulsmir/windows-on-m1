/* SPDX-License-Identifier: MIT */
/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * A flush acknowledged without a controller flush while data may still be only in the volatile
 * cache is silent data loss on power failure. Elision is allowed only when a completed, successful
 * flush provably covers every write: none outstanding when it was submitted, none submitted since. */
#include "../ans_flush.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void)
{
    struct ans_flush_state s = {0};
    struct ans_flush_ticket f1, f2;
    assert(!ans_flush_can_elide(&s) && !ans_flush_can_elide(NULL)); /* nothing known clean at start */
    f1 = ans_flush_submitted(&s); assert(f1.covers && !ans_flush_can_elide(&s)); /* in flight: not yet */
    ans_flush_completed(&s, f1, 1); assert(ans_flush_can_elide(&s));           /* the Windows pair */
    ans_flush_elided(&s); assert(s.elided == 1 && s.sent == 1 && ans_flush_can_elide(&s));
    ans_flush_write_submitted(&s); assert(!ans_flush_can_elide(&s));           /* new data */
    f1 = ans_flush_submitted(&s); assert(!f1.covers);                          /* write still in flight */
    ans_flush_write_completed(&s); ans_flush_completed(&s, f1, 1);
    assert(!ans_flush_can_elide(&s));       /* that flush may have run before the write reached the cache */
    f2 = ans_flush_submitted(&s); assert(f2.covers); ans_flush_completed(&s, f2, 0);
    assert(!ans_flush_can_elide(&s));       /* failed flush proves nothing */
    f2 = ans_flush_submitted(&s); ans_flush_write_submitted(&s); ans_flush_write_completed(&s);
    ans_flush_completed(&s, f2, 1); assert(!ans_flush_can_elide(&s)); /* write raced the flush */
    f2 = ans_flush_submitted(&s); ans_flush_completed(&s, f2, 1); assert(ans_flush_can_elide(&s));
    f1 = ans_flush_submitted(&s); f2 = ans_flush_submitted(&s);               /* two flushes, out of order */
    ans_flush_completed(&s, f2, 1); ans_flush_completed(&s, f1, 1); assert(ans_flush_can_elide(&s));
    ans_flush_reset(&s); assert(!ans_flush_can_elide(&s));                    /* reset: unknown again */
    f1 = ans_flush_submitted(&s); ans_flush_completed(&s, f1, 1); assert(ans_flush_can_elide(&s));
    /* Model check: elision is offered only if every write completed before the submission of a
     * successful flush and nothing was written since. */
    srand(3);
    for (int round = 0; round < 20000; round++) {
        struct ans_flush_state m = {0}; struct ans_flush_ticket t[4]; int live[4] = {0};
        unsigned outstanding = 0; int dirty = 1; /* reference: 1 = cache may hold unflushed data */
        int covers[4] = {0};
        for (int step = 0; step < 40; step++) {
            int op = rand() % 6, i = rand() % 4;
            if (op == 0) { ans_flush_write_submitted(&m); outstanding++; dirty = 1; for (int k = 0; k < 4; k++) covers[k] = 0; }
            else if (op == 1 && outstanding) { ans_flush_write_completed(&m); outstanding--; }
            else if (op == 2 && !live[i]) { t[i] = ans_flush_submitted(&m); live[i] = 1; covers[i] = outstanding == 0; }
            else if (op == 3 && live[i]) { int ok = rand() % 4 != 0; ans_flush_completed(&m, t[i], ok); live[i] = 0; if (ok && covers[i]) dirty = 0; }
            else if (op == 4 && rand() % 16 == 0) { ans_flush_reset(&m); outstanding = 0; dirty = 1; for (int k = 0; k < 4; k++) { live[k] = 0; covers[k] = 0; } }
            if (ans_flush_can_elide(&m)) assert(!dirty && !outstanding);
        }
    }
    puts("ANS flush elision tests PASS");
    return 0;
}
