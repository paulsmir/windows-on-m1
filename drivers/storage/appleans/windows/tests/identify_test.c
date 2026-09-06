/* SPDX-License-Identifier: MIT */
#include "../identify.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    ans_u8 c[4096]={0},n[4096]={0};struct ans_namespace ns;
    c[0]=0x6b;c[1]=0x10;c[516]=1;c[525]=1;
    memcpy(c+24,"APPLE SSD AP0256Q",16);memcpy(c+64,"1.23",4);
    n[1]=1;n[9]=1;n[130]=12;
    assert(ans_identify_namespace(c,n,&ns));
    assert(ns.blocks==256 && ns.read_only && !ns.ready && !ns.cache_known);
    assert(!memcmp(ns.product,"APPLE SSD AP0256Q",16));
    memcpy(c+64,"12345678",8);
    assert(ans_identify_namespace(c,n,&ns) && !memcmp(ns.revision,"5678",4));
    assert(!memcmp(ns.vendor,"NVMe    ",8));
    c[77]=4;assert(!ans_identify_namespace(c,n,&ns));c[77]=5;
    assert(ans_identify_namespace(c,n,&ns));
    n[128]=8;assert(!ans_identify_namespace(c,n,&ns) && !ns.blocks);n[128]=0;
    n[29]=1;assert(!ans_identify_namespace(c,n,&ns));n[29]=0;
    n[26]=16;assert(!ans_identify_namespace(c,n,&ns));n[26]=0;
    n[130]=9;assert(!ans_identify_namespace(c,n,&ns));n[130]=12;
    c[516]=0;assert(!ans_identify_namespace(c,n,&ns));c[516]=1;
    n[9]=2;assert(!ans_identify_namespace(c,n,&ns));
    puts("identify_test: ok");
}
