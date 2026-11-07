/* SPDX-License-Identifier: MIT */
#include "telemetry.h"
#if defined(_KERNEL_MODE)
#define COPY(d,s,n) RtlCopyMemory(d,s,n)
#define ZERO(d,n) RtlZeroMemory(d,n)
#else
#include <string.h>
#define COPY(d,s,n) memcpy(d,s,n)
#define ZERO(d,n) memset(d,0,n)
#endif
typedef char ans_io_control_size_check[(sizeof(struct ans_io_control)==28)?1:-1];
typedef char ans_telemetry_query_size_check[(sizeof(struct ans_telemetry_query)==16)?1:-1];
#define ALL_FIELDS ((1ULL<<24)-1)
#define WINDOWS_FIELDS (ALL_FIELDS & ~(ANS_TELEM_PHYSICAL_IRQS | ANS_TELEM_GUEST_IRQ_INJECTIONS | \
    ANS_TELEM_TCB_INVAL_FAILURES | ANS_TELEM_POLL_RECOVERED_COMPLETIONS))
void ans_stats_init(struct ans_telemetry *t)
{
    ZERO(t,sizeof(*t));t->magic=ANS_TELEMETRY_MAGIC;t->size=sizeof(*t);
    t->source=1;t->valid_fields=WINDOWS_FIELDS;
}
void ans_stats_add(struct ans_telemetry *t, ans_u64 field, ans_u64 value)
{
    ans_u64 *counter;
    if (!t || !(t->valid_fields & field)) return;
#define COUNTER(bit,name) case bit: counter=&t->name;break
    switch(field) {
    COUNTER(ANS_TELEM_STARTS,starts);COUNTER(ANS_TELEM_STOPS,stops);
    COUNTER(ANS_TELEM_RESETS,resets);COUNTER(ANS_TELEM_SUBMITTED,submitted);
    COUNTER(ANS_TELEM_COMPLETED,completed);COUNTER(ANS_TELEM_READS,reads);
    COUNTER(ANS_TELEM_WRITES,writes);COUNTER(ANS_TELEM_FLUSHES,flushes);
    COUNTER(ANS_TELEM_BYTES_READ,bytes_read);COUNTER(ANS_TELEM_BYTES_WRITTEN,bytes_written);
    COUNTER(ANS_TELEM_MAX_OUTSTANDING,max_outstanding);COUNTER(ANS_TELEM_CURRENT_OUTSTANDING,current_outstanding);
    COUNTER(ANS_TELEM_ISR_ENTRIES,isr_entries);COUNTER(ANS_TELEM_CQES,cqes);
    COUNTER(ANS_TELEM_TIMEOUTS,timeouts);COUNTER(ANS_TELEM_INVALID_CQE,invalid_cqe);
    COUNTER(ANS_TELEM_INVALID_TAG,invalid_tag);COUNTER(ANS_TELEM_BROKER_REJECTIONS,broker_rejections);
    COUNTER(ANS_TELEM_PRP_TRANSLATION_FAILURES,prp_translation_failures);
    COUNTER(ANS_TELEM_SPURIOUS_IRQS,spurious_irqs);
    default:return;
    }
#undef COUNTER
    if (*counter>ANS_U64_MAX-value) { t->valid_fields&=~field;*counter=0; }
    else *counter+=value;
}
void ans_stats_submit(struct ans_telemetry *t, unsigned queue, unsigned opcode)
{
    ans_stats_add(t,ANS_TELEM_SUBMITTED,1);
    ans_stats_add(t,ANS_TELEM_CURRENT_OUTSTANDING,1);
    if ((t->valid_fields & (ANS_TELEM_CURRENT_OUTSTANDING|ANS_TELEM_MAX_OUTSTANDING))==
        (ANS_TELEM_CURRENT_OUTSTANDING|ANS_TELEM_MAX_OUTSTANDING) &&
        t->current_outstanding>t->max_outstanding) t->max_outstanding=t->current_outstanding;
    if (queue==1) {
        if (opcode==2) ans_stats_add(t,ANS_TELEM_READS,1);
        if (opcode==1) ans_stats_add(t,ANS_TELEM_WRITES,1);
        if (opcode==0) ans_stats_add(t,ANS_TELEM_FLUSHES,1);
    }
}
void ans_stats_complete(struct ans_telemetry *t, unsigned queue, unsigned opcode,
                        ans_u32 bytes, int success)
{
    ans_stats_add(t,ANS_TELEM_COMPLETED,1);
    if (t->valid_fields & ANS_TELEM_CURRENT_OUTSTANDING) {
        if (t->current_outstanding) t->current_outstanding--;
        else {
            t->valid_fields&=~(ANS_TELEM_CURRENT_OUTSTANDING|ANS_TELEM_COMPLETED);
            t->completed=0;return;
        }
    }
    if (success && queue==1) {
        if (opcode==2) ans_stats_add(t,ANS_TELEM_BYTES_READ,bytes);
        if (opcode==1) ans_stats_add(t,ANS_TELEM_BYTES_WRITTEN,bytes);
    }
}
void ans_stats_uncertain(struct ans_telemetry *t, unsigned queue, unsigned opcode)
{
    ans_u64 invalid=ANS_TELEM_SUBMITTED|ANS_TELEM_COMPLETED|
        ANS_TELEM_CURRENT_OUTSTANDING|ANS_TELEM_MAX_OUTSTANDING;
    if (queue==1) {
        if (opcode==2) invalid|=ANS_TELEM_READS;
        if (opcode==1) invalid|=ANS_TELEM_WRITES;
        if (opcode==0) invalid|=ANS_TELEM_FLUSHES;
    }
    t->valid_fields&=~invalid;
    t->current_outstanding=0;t->max_outstanding=0;
}
void ans_stats_stopped(struct ans_telemetry *t, ans_u64 generation)
{
    t->generation=generation;t->current_outstanding=0;
    t->valid_fields|=ANS_TELEM_CURRENT_OUTSTANDING;
}
static int equal(const ans_u8 *a, const ans_u8 *b, unsigned count)
{
    for(unsigned i=0;i<count;i++)if(a[i]!=b[i])return 0;
    return 1;
}
int ans_stats_ioctl_validate(const void *buffer, ans_u32 bytes, ans_u32 *sources)
{
    struct ans_io_control h;struct ans_telemetry_query q;
    if(!buffer || bytes!=ANS_IOCTL_TOTAL_BYTES || !sources)return 0;
    COPY(&h,buffer,sizeof(h));
    COPY(&q,(const ans_u8 *)buffer+sizeof(h),sizeof(q));
    if(h.header_length!=sizeof(h) || h.length!=ANS_IOCTL_PAYLOAD_BYTES ||
        h.control_code!=ANS_IOCTL_QUERY || !equal(h.signature,(const ans_u8 *)ANS_IOCTL_SIGNATURE,8) ||
        !h.timeout || h.timeout>60 || q.version!=ANS_IOCTL_VERSION ||
        q.size!=ANS_IOCTL_PAYLOAD_BYTES || q.reserved ||
        (q.sources!=ANS_IOCTL_WINDOWS && q.sources!=(ANS_IOCTL_WINDOWS|ANS_IOCTL_BROKER)))return 0;
    *sources=q.sources;return 1;
}
static int valid(const struct ans_telemetry *t,unsigned source)
{
    return t && t->magic==ANS_TELEMETRY_MAGIC && t->size==sizeof(*t) &&
        t->source==source && !t->reserved && !(t->valid_fields&~ALL_FIELDS);
}
int ans_stats_ioctl_reply(void *buffer, ans_u32 bytes, const struct ans_telemetry *windows,
                          const struct ans_telemetry *broker)
{
    ans_u32 wanted;struct ans_io_control h;
    if(!ans_stats_ioctl_validate(buffer,bytes,&wanted) || !valid(windows,1))return 0;
    struct ans_telemetry_query q={ANS_IOCTL_VERSION,ANS_IOCTL_PAYLOAD_BYTES,ANS_IOCTL_WINDOWS,0};
    if((wanted&ANS_IOCTL_BROKER) && valid(broker,2))q.sources|=ANS_IOCTL_BROKER;
    COPY(&h,buffer,sizeof(h));h.return_code=0;
    COPY(buffer,&h,sizeof(h));COPY((ans_u8 *)buffer+sizeof(h),&q,sizeof(q));
    COPY((ans_u8 *)buffer+sizeof(h)+sizeof(q),windows,sizeof(*windows));
    if(q.sources&ANS_IOCTL_BROKER)
        COPY((ans_u8 *)buffer+sizeof(h)+sizeof(q)+sizeof(*windows),broker,sizeof(*broker));
    else ZERO((ans_u8 *)buffer+sizeof(h)+sizeof(q)+sizeof(*windows),sizeof(*windows));
    return 1;
}
