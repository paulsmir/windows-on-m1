/* SPDX-License-Identifier: MIT */
#include "resources.h"
static int present(ans_u32 value)
{
    return value && value != (ans_u32)~(ans_u32)0;
}
int ans_resources_valid(const struct ans_resources *r)
{
    return r && r->range_count==1 && r->memory && !r->virtual_device &&
        r->base==ANS_BROKER_BASE && r->length==ANS_BROKER_SIZE &&
        r->level_sensitive && (present(r->vector) || present(r->level)) &&
        !present(r->vector2) && !present(r->level2);
}
