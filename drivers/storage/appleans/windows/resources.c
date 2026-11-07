/* SPDX-License-Identifier: MIT */
#include "resources.h"
#include "../common/ans_direct.h"
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
int ans_direct_resources_valid(const struct ans_resources *r)
{
    return r && r->range_count==2 && r->memory && r->memory2 && !r->virtual_device &&
        r->base==ANSD_CTRL_BASE && r->length==ANSD_CTRL_SIZE &&
        r->base2==ANSD_MBOX_BASE && r->length2==ANSD_MBOX_SIZE &&
        r->level_sensitive && (present(r->vector) || present(r->level)) &&
        !present(r->vector2) && !present(r->level2);
}
