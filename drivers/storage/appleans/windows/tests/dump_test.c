/* SPDX-License-Identifier: MIT */
#include "../dump.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BYTES 0x5c000u
int main(void)
{
    struct ans_dump_geometry g={.common_va=0xffff800012340000ULL,.common_pa=0x8c0000000ULL,
        .blocks=1000,.common_bytes=BYTES,.block_size=4096,.nsid=1};
    struct ans_dump_context c;
    memset(&c,0xa5,sizeof(c));
    /* The dump instance dereferences this record at HIGH_LEVEL: every damaged form refuses. */
    assert(!ans_dump_context_valid(NULL,BYTES));
    assert(!ans_dump_context_valid(&c,BYTES)); /* never sealed */
    assert(ans_dump_context_seal(&c,&g) && ans_dump_context_valid(&c,BYTES));
    assert(!ans_dump_context_valid(&c,BYTES+4096)); /* other queue layout */
    assert(!ans_dump_context_valid((struct ans_dump_context *)((char *)&c+1),BYTES));
    for (unsigned i=0;i<sizeof(c)-16;i++) { /* any sealed byte, any single bit */
        for (unsigned bit=0;bit<8;bit++) {
            ((unsigned char *)&c)[i]^=(unsigned char)(1u<<bit);
            assert(!ans_dump_context_valid(&c,BYTES));
            ((unsigned char *)&c)[i]^=(unsigned char)(1u<<bit);
        }
    }
    /* The receipt is outside the seal: the dump instance may write it without re-sealing. */
    c.calls=c.flags=c.last_mode=c.range_count=0;
    ans_dump_context_receipt(&c,3,ANS_DUMP_SEEN_VALID|ANS_DUMP_SEEN_RANGES,2);
    ans_dump_context_receipt(&c,1,ANS_DUMP_SEEN_VALID,0);
    assert(ans_dump_context_valid(&c,BYTES));
    assert(c.calls==2 && c.range_count==0 && ans_dump_context_receipt_value(&c)==0x020103u);
    c.calls=1000;assert((ans_dump_context_receipt_value(&c)>>16)==255u);
    /* Unusable geometry never produces a sealed record, and unseals an old one. */
    struct ans_dump_geometry bad=g;
    bad.common_va=0;assert(!ans_dump_context_seal(&c,&bad) && !ans_dump_context_valid(&c,BYTES));
    bad=g;bad.common_pa=0;assert(!ans_dump_context_seal(&c,&bad));
    bad=g;bad.block_size=512;assert(!ans_dump_context_seal(&c,&bad));
    bad=g;bad.blocks=0;assert(!ans_dump_context_seal(&c,&bad));
    bad=g;bad.nsid=2;assert(!ans_dump_context_seal(&c,&bad));
    bad=g;bad.common_va=~0ULL-100;assert(!ans_dump_context_seal(&c,&bad)); /* wraps */
    assert(!ans_dump_context_seal(&c,NULL) && !ans_dump_context_seal(NULL,&g));
    puts("dump_test: ok");
}
