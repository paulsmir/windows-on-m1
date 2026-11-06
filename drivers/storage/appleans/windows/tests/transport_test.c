/* SPDX-License-Identifier: MIT */
#include "../transport.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static unsigned char memory[ANS_BROKER_SIZE];
static unsigned barriers, writes, bells, response_reads;
static int corruption;
static ans_u32 read32(void *ctx, ans_u32 off)
{
    ans_u32 v;
    assert(ctx == memory && !(off & 3) && off <= sizeof(memory)-4);
    memcpy(&v,memory+off,4);
    if (off >= ANS_RESPONSE_OFFSET && off < ANS_RESPONSE_OFFSET+48) {
        response_reads++;
        if (corruption == 6 && response_reads == 13) v ^= 1; /* torn second snapshot */
    }
    return v;
}
static ans_u64 read64(void *ctx, ans_u32 off)
{
    ans_u64 v;
    assert(ctx==memory && off==ANS_PENDING_OFFSET);
    memcpy(&v,memory+off,8);
    return v;
}
static void barrier(void *ctx) { assert(ctx==memory); barriers++; }
static void write32(void *ctx, ans_u32 off, ans_u32 value)
{
    assert(ctx==memory);
    if (off != ANS_DOORBELL_OFFSET) {
        assert(off>=ANS_REQUEST_OFFSET && off<ANS_REQUEST_OFFSET+112);
        memcpy(memory+off,&value,4);writes++;return;
    }
    assert(writes==28 && barriers>=2);bells++;writes=0;response_reads=0;
    struct ans_broker_request r;
    struct ans_broker_header h;
    memcpy(&r,memory+ANS_REQUEST_OFFSET,sizeof(r));memcpy(&h,memory,sizeof(h));
    assert(value==(ans_u32)r.sequence && r.sequence && r.generation==h.generation);
    struct ans_broker_response s={.generation=r.generation,.sequence=r.sequence,
        .state=h.state,.queue=r.queue,.tag=r.tag};
    if (r.operation==ANS_OP_CLAIM) s.state=ANS_STATE_CLAIMED;
    if (r.operation==ANS_OP_QUIESCE) s.state=ANS_STATE_QUIESCING;
    if (r.operation==ANS_OP_RESET || r.operation==ANS_OP_RELEASE) {
        if ((h.state!=ANS_STATE_QUIESCING && h.state!=ANS_STATE_FAULTED) ||
            (r.operation==ANS_OP_RELEASE && h.state!=ANS_STATE_QUIESCING)) {
            s.result=ANS_RESULT_BAD_STATE;h.sequence=r.sequence;
        } else {
            s.generation++;s.state=r.operation==ANS_OP_RESET?ANS_STATE_READY:ANS_STATE_OFF;
            h.sequence=0;
        }
    } else h.sequence=r.sequence;
    h.generation=s.generation;h.state=s.state;
    if (corruption==1) s.sequence--;
    if (corruption==2) s.generation++;
    if (corruption==3) s.queue^=1;
    if (corruption==4) s.reserved=1;
    if (corruption==5) s.result=0xffffffff;
    if (corruption==7) s.result=ANS_RESULT_DENIED;
    if (corruption==8) { s.result=ANS_RESULT_IO_ERROR;s.state=ANS_STATE_FAULTED; }
    if (corruption==9) h.sequence=123;
    memcpy(memory,&h,sizeof(h));memcpy(memory+ANS_RESPONSE_OFFSET,&s,sizeof(s));
}
static const struct ans_wire_ops ops={read32,read64,write32,barrier};
static void setup(struct ans_wire *w, ans_u64 seq)
{
    memset(memory,0,sizeof(memory));corruption=0;barriers=writes=bells=response_reads=0;
    struct ans_broker_header h={.magic=ANS_MAGIC,.abi_major=ANS_ABI_MAJOR,
        .abi_minor=ANS_ABI_MINOR,.size=sizeof(h),.features=ANS_FEATURE_READ_ONLY,
        .generation=9,.sequence=seq,.state=ANS_STATE_READY};
    memcpy(memory,&h,sizeof(h));assert(ans_wire_open(w,&ops,memory)==ANS_WIRE_OK);
}
static int call(struct ans_wire *w, unsigned op)
{
    struct ans_broker_request r={.operation=op};struct ans_broker_response s;
    return ans_wire_call(w,&r,&s);
}
int main(void)
{
    struct ans_wire w;
    setup(&w,0);assert(call(&w,ANS_OP_QUERY)==0 && w.sequence==1);
    assert(call(&w,ANS_OP_CLAIM)==0 && w.state==ANS_STATE_CLAIMED);
    assert(call(&w,ANS_OP_REGISTER_QUEUES)==0);
    assert(call(&w,ANS_OP_QUIESCE)==0);
    assert(call(&w,ANS_OP_RELEASE)==0 && w.generation==10 && w.sequence==0);
    assert(call(&w,ANS_OP_CLAIM)==0 && w.sequence==1);
    assert(call(&w,ANS_OP_RESET)==ANS_WIRE_REJECTED);
    assert(call(&w,ANS_OP_QUIESCE)==0);
    assert(call(&w,ANS_OP_RESET)==0 && w.generation==11 && w.sequence==0);
    setup(&w,0xffffffff);assert(call(&w,ANS_OP_QUERY)==0 && w.sequence==0x100000000ULL);
    for (int fault=1;fault<=6;fault++) {
        setup(&w,0);corruption=fault;
        assert(call(&w,ANS_OP_QUERY)==ANS_WIRE_UNCERTAIN && w.faulted);
        unsigned old=bells;assert(call(&w,ANS_OP_CLAIM)==ANS_WIRE_UNCERTAIN && bells==old);
        corruption=0;
        /* Faulted service recovery preserves the consumed request sequence. */
        ((struct ans_broker_header *)memory)->state=ANS_STATE_FAULTED;
        assert(call(&w,ANS_OP_RESET)==0 && !w.faulted && w.generation==10);
    }
    setup(&w,0);corruption=7;
    assert(call(&w,ANS_OP_QUERY)==ANS_WIRE_REJECTED && !w.faulted);
    setup(&w,0);corruption=8;
    assert(call(&w,ANS_OP_AUTHORIZE_SUBMIT)==ANS_WIRE_UNCERTAIN && w.faulted);
    setup(&w,0);assert(call(&w,ANS_OP_CLAIM)==0);assert(call(&w,ANS_OP_QUIESCE)==0);corruption=9;
    assert(call(&w,ANS_OP_RESET)==ANS_WIRE_UNCERTAIN && w.faulted);
    setup(&w,0);
    ((struct ans_broker_header *)memory)->features=0;
    assert(ans_wire_open(&w,&ops,memory)==ANS_WIRE_INVALID && !w.bound);
    /* ANS008: exactly one of READ_ONLY / WRITE_WINDOW; TELEMETRY may accompany either. */
    setup(&w,0);
    ((struct ans_broker_header *)memory)->features=ANS_FEATURE_WRITE_WINDOW|ANS_FEATURE_TELEMETRY;
    assert(ans_wire_open(&w,&ops,memory)==ANS_WIRE_OK && w.bound && (w.features&ANS_FEATURE_WRITE_WINDOW));
    setup(&w,0);
    ((struct ans_broker_header *)memory)->features=ANS_FEATURE_WRITE_WINDOW|ANS_FEATURE_READ_ONLY;
    assert(ans_wire_open(&w,&ops,memory)==ANS_WIRE_INVALID && !w.bound);
    setup(&w,0);
    ((struct ans_broker_header *)memory)->features=ANS_FEATURE_TELEMETRY;
    assert(ans_wire_open(&w,&ops,memory)==ANS_WIRE_INVALID && !w.bound);
    setup(&w,0);
    w.sequence=ANS_U64_MAX;unsigned old=bells;
    assert(call(&w,ANS_OP_QUERY)==ANS_WIRE_INVALID && bells==old);
    setup(&w,0);
    ans_u64 bits=1ULL<<63,pending;memcpy(memory+ANS_PENDING_OFFSET,&bits,8);
    assert(ans_wire_pending(&w,&pending)==0 && pending==bits);
    ans_u8 cqe[16]={0},out[16];cqe[12]=63;cqe[8]=255;
    memcpy(memory+ANS_COMPLETION_OFFSET+63*16,cqe,16);
    assert(ans_wire_cqe(&w,63,out)==0 && out[8]==255);
    memory[ANS_COMPLETION_OFFSET+63*16+13]=1;
    assert(ans_wire_cqe(&w,63,out)==ANS_WIRE_UNCERTAIN && w.faulted);
    puts("transport_test: ok");
}
