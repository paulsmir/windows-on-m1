#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../include/apple_agx_gpuva_broker_v5_client.h"
#include "../../../../m1n1_windows/src/hv_agx_gpuva_v5_mmio.h"

struct fixture { struct hv_agx_gpuva_v5_wire wire; unsigned calls, barriers, window_writes; bool bad_receipt, refuse_attach; unsigned char box[AGX_GPUVA_V5_MAILBOX_BYTES]; };
static void execute(void *opaque, const AGX_GPUVA_V5_REQUEST *q,
                    AGX_GPUVA_V5_RESPONSE *r)
{
    struct fixture *f=opaque;
    ++f->calls;
    assert(q->Sequence == f->calls && q->Version == AGX_GPUVA_V5_VERSION &&
           q->Bytes == 128);
    if (q->Command == AGX_GPUVA_V5_ATTACH_MAILBOX) {
        r->Epoch=9;
        r->Status=f->refuse_attach ? 1 : 0;
        if (!f->refuse_attach) f->wire.mailbox=q->AuxIpa ? f->box : NULL;
        return;
    }
    r->Epoch=9;
    r->Status=q->Epoch == 9 ? 0 : 2;
    if (f->bad_receipt) ++r->Receipt;
}
static bool w64(void *opaque, unsigned offset, uint64_t value)
{ struct fixture *f=opaque; ++f->window_writes; return hv_agx_gpuva_v5_mmio(&f->wire,offset-AGX_GPUVA_V5_OFFSET,&value,true,3,execute,f); }
static bool r64(void *opaque, unsigned offset, uint64_t *value)
{ struct fixture *f=opaque; return hv_agx_gpuva_v5_mmio(&f->wire,offset-AGX_GPUVA_V5_OFFSET,value,false,3,execute,f); }
static bool w32(void *opaque, unsigned offset, uint32_t value)
{ struct fixture *f=opaque; uint64_t v=value; return hv_agx_gpuva_v5_mmio(&f->wire,offset-AGX_GPUVA_V5_OFFSET,&v,true,2,execute,f); }
static void barrier(void *opaque) { ++((struct fixture *)opaque)->barriers; }
int main(void)
{
    struct fixture f={0}; APPLE_AGX_GPUVA_V5_CLIENT c; AGX_GPUVA_V5_REQUEST q={0}; AGX_GPUVA_V5_RESPONSE r={0};
    APPLE_AGX_GPUVA_V5_IO io={&f,w64,r64,w32,barrier, 0};
    assert(AppleAgxGpuvaV5ClientInit(&c,&io));
    q.Command=AGX_GPUVA_V5_CREATE; q.ProcessId=1; q.ProcessGeneration=1;
    assert(AppleAgxGpuvaV5ClientCall(&c,&q,&r));
    assert(c.Epoch==9 && r.Status==2 && c.Sequence==1);
    assert(AppleAgxGpuvaV5ClientCall(&c,&q,&r));
    assert(r.Status==0 && c.Sequence==2 && f.calls==2 && f.barriers==4);
    f.bad_receipt=true;
    assert(!AppleAgxGpuvaV5ClientCall(&c,&q,&r));
    f.bad_receipt=false;
    /* A broker without mailbox support refuses the attach; the client keeps
     * the trapped window. */
    f.refuse_attach=true;
    assert(!AppleAgxGpuvaV5ClientAttachMailbox(&c,f.box,0x40000000ULL));
    assert(c.Mailbox==NULL);
    f.refuse_attach=false;
    assert(!AppleAgxGpuvaV5ClientAttachMailbox(&c,f.box,0x40001000ULL));
    assert(AppleAgxGpuvaV5ClientAttachMailbox(&c,f.box,0x40000000ULL));
    assert(c.Mailbox==f.box && f.wire.mailbox==f.box);
    /* Mailbox calls carry the request in memory: no window writes. */
    unsigned writes=f.window_writes, calls=f.calls;
    assert(AppleAgxGpuvaV5ClientCall(&c,&q,&r));
    assert(r.Status==0 && f.calls==calls+1 && f.window_writes==writes);
    f.bad_receipt=true;
    assert(!AppleAgxGpuvaV5ClientCall(&c,&q,&r));
    f.bad_receipt=false;
    assert(AppleAgxGpuvaV5ClientDetachMailbox(&c));
    assert(c.Mailbox==NULL && f.wire.mailbox==NULL);
    writes=f.window_writes;
    assert(AppleAgxGpuvaV5ClientCall(&c,&q,&r));
    assert(f.window_writes==writes+16);
    return 0;
}
