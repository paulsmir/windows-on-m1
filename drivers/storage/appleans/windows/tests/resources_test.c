/* SPDX-License-Identifier: MIT */
#include "../resources.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    struct ans_resources r={.base=ANS_BROKER_BASE,.length=ANS_BROKER_SIZE,
        .range_count=1,.vector=0x51,.memory=1,.level_sensitive=1};
    assert(ans_resources_valid(&r)); /* vector-only, deliberately not raw GSIV */
    r.vector=0;r.level=5;assert(ans_resources_valid(&r)); /* level-only */
    r.vector=0xffffffff;assert(ans_resources_valid(&r));
    r.level=0;assert(!ans_resources_valid(&r));
    r.vector=0;assert(!ans_resources_valid(&r));
    r.level=5;r.level2=6;assert(!ans_resources_valid(&r));
    r.level2=0;r.vector2=0x60;assert(!ans_resources_valid(&r));
    r.vector2=0;r.level_sensitive=0;assert(!ans_resources_valid(&r));
    r.level_sensitive=1;r.range_count=2;assert(!ans_resources_valid(&r));
    r.range_count=1;r.memory=0;assert(!ans_resources_valid(&r));
    r.memory=1;r.base+=4096;assert(!ans_resources_valid(&r));
    r.base=ANS_BROKER_BASE;r.length--;assert(!ans_resources_valid(&r));
    r.length=ANS_BROKER_SIZE;r.virtual_device=1;assert(!ans_resources_valid(&r));
    assert(!ans_resources_valid(NULL));
    puts("resources_test: ok");
}
