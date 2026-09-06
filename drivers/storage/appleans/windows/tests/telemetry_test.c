/* SPDX-License-Identifier: MIT */
#include "../telemetry.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void request(ans_u8 *p)
{
    memset(p,0,ANS_IOCTL_TOTAL_BYTES);
    struct ans_io_control h={.header_length=28,.timeout=10,.control_code=ANS_IOCTL_QUERY,
        .length=ANS_IOCTL_PAYLOAD_BYTES};
    memcpy(h.signature,ANS_IOCTL_SIGNATURE,8);memcpy(p,&h,sizeof(h));
    struct ans_telemetry_query q={ANS_IOCTL_VERSION,ANS_IOCTL_PAYLOAD_BYTES,3,0};
    memcpy(p+28,&q,sizeof(q));
}
int main(void)
{
    struct ans_telemetry t,peer,out;ans_stats_init(&t);
    assert(t.source==1 && !(t.valid_fields&ANS_TELEM_PHYSICAL_IRQS));
    ans_stats_submit(&t,0,1); /* Create SQ is NOT a WRITE. */
    ans_stats_submit(&t,1,2);ans_stats_submit(&t,1,2);
    assert(t.submitted==3 && t.writes==0 && t.reads==2 && t.max_outstanding==3);
    ans_stats_complete(&t,0,1,4096,1);
    ans_stats_complete(&t,1,2,131072,1);
    ans_stats_complete(&t,1,2,4096,0);
    assert(t.current_outstanding==0 && t.completed==3 && t.bytes_read==131072 && !t.bytes_written);
    ans_stats_submit(&t,1,0);assert(t.flushes==1 && !t.writes);
    ans_stats_uncertain(&t,1,2);
    assert(!(t.valid_fields&ANS_TELEM_CURRENT_OUTSTANDING) && !(t.valid_fields&ANS_TELEM_READS));
    ans_stats_stopped(&t,8);assert(t.generation==8 && t.current_outstanding==0);
    assert(t.valid_fields&ANS_TELEM_CURRENT_OUTSTANDING);
    assert(!(t.valid_fields&ANS_TELEM_MAX_OUTSTANDING));
    t.timeouts=ANS_U64_MAX;ans_stats_add(&t,ANS_TELEM_TIMEOUTS,1);
    assert(!(t.valid_fields&ANS_TELEM_TIMEOUTS));
    struct ans_telemetry bad;ans_stats_init(&bad);
    ans_stats_complete(&bad,1,2,4096,1);
    assert(!(bad.valid_fields&ANS_TELEM_CURRENT_OUTSTANDING));
    assert(!(bad.valid_fields&ANS_TELEM_COMPLETED) && bad.bytes_read==0);
    /* Exact SRB_IO_CONTROL framing, deliberately unaligned input/output. */
    ans_u8 raw[ANS_IOCTL_TOTAL_BYTES+2],before[ANS_IOCTL_TOTAL_BYTES];ans_u8 *p=raw+1;
    raw[0]=0xa5;raw[sizeof(raw)-1]=0x5a;request(p);
    ans_u32 wanted;assert(ans_stats_ioctl_validate(p,ANS_IOCTL_TOTAL_BYTES,&wanted) && wanted==3);
    peer=t;peer.source=2;peer.generation=9;
    assert(ans_stats_ioctl_reply(p,ANS_IOCTL_TOTAL_BYTES,&t,&peer));
    memcpy(&out,p+28+16+232,232);assert(out.source==2 && out.generation==9);
    assert(raw[0]==0xa5 && raw[sizeof(raw)-1]==0x5a);
    request(p);assert(ans_stats_ioctl_reply(p,ANS_IOCTL_TOTAL_BYTES,&t,NULL));
    struct ans_telemetry_query q;memcpy(&q,p+28,16);assert(q.sources==ANS_IOCTL_WINDOWS);
    for(unsigned i=0;i<232;i++)assert(p[28+16+232+i]==0);
    request(p);p[4]^=1;memcpy(before,p,sizeof(before));
    assert(!ans_stats_ioctl_reply(p,ANS_IOCTL_TOTAL_BYTES,&t,&peer));
    assert(!memcmp(before,p,sizeof(before)));
    request(p);p[28]=2;assert(!ans_stats_ioctl_validate(p,ANS_IOCTL_TOTAL_BYTES,&wanted));
    request(p);p[24]=0xff;assert(!ans_stats_ioctl_validate(p,ANS_IOCTL_TOTAL_BYTES,&wanted));
    request(p);p[0]=0xff;assert(!ans_stats_ioctl_validate(p,ANS_IOCTL_TOTAL_BYTES,&wanted));
    request(p);p[16]^=1;assert(!ans_stats_ioctl_validate(p,ANS_IOCTL_TOTAL_BYTES,&wanted));
    request(p);p[40]=1;assert(!ans_stats_ioctl_validate(p,ANS_IOCTL_TOTAL_BYTES,&wanted));
    request(p);assert(!ans_stats_ioctl_validate(p,ANS_IOCTL_TOTAL_BYTES-1,&wanted));
    assert(!ans_stats_ioctl_validate(p,ANS_IOCTL_TOTAL_BYTES+1,&wanted));
    puts("telemetry_test: ok");
}
