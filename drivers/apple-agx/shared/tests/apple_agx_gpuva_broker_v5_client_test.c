#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../include/apple_agx_gpuva_broker_v5_client.h"
#include "../../../../m1n1_windows/src/hv_agx_gpuva_v5_mmio.h"

struct fixture { struct hv_agx_gpuva_v5_wire wire; unsigned calls, barriers; bool bad_receipt; };
static void execute(void *opaque, const AGX_GPUVA_V5_REQUEST *q,
                    AGX_GPUVA_V5_RESPONSE *r)
{
    struct fixture *f=opaque;
    ++f->calls;
    assert(q->Sequence == f->calls && q->Version == 5 && q->Bytes == 128);
    r->Epoch=9;
    r->Status=q->Epoch == 9 ? 0 : 2;
    if (f->bad_receipt) ++r->Receipt;
}
static bool w64(void *opaque, unsigned offset, uint64_t value)
{ struct fixture *f=opaque; return hv_agx_gpuva_v5_mmio(&f->wire,offset-AGX_GPUVA_V5_OFFSET,&value,true,3,execute,f); }
static bool r64(void *opaque, unsigned offset, uint64_t *value)
{ struct fixture *f=opaque; return hv_agx_gpuva_v5_mmio(&f->wire,offset-AGX_GPUVA_V5_OFFSET,value,false,3,execute,f); }
static bool w32(void *opaque, unsigned offset, uint32_t value)
{ struct fixture *f=opaque; uint64_t v=value; return hv_agx_gpuva_v5_mmio(&f->wire,offset-AGX_GPUVA_V5_OFFSET,&v,true,2,execute,f); }
static void barrier(void *opaque) { ++((struct fixture *)opaque)->barriers; }
int main(void)
{
    struct fixture f={0}; APPLE_AGX_GPUVA_V5_CLIENT c; AGX_GPUVA_V5_REQUEST q={0}; AGX_GPUVA_V5_RESPONSE r={0};
    APPLE_AGX_GPUVA_V5_IO io={&f,w64,r64,w32,barrier};
    assert(AppleAgxGpuvaV5ClientInit(&c,&io));
    q.Command=AGX_GPUVA_V5_CREATE; q.ProcessId=1; q.ProcessGeneration=1;
    assert(AppleAgxGpuvaV5ClientCall(&c,&q,&r));
    assert(c.Epoch==9 && r.Status==2 && c.Sequence==1);
    assert(AppleAgxGpuvaV5ClientCall(&c,&q,&r));
    assert(r.Status==0 && c.Sequence==2 && f.calls==2 && f.barriers==4);
    f.bad_receipt=true;
    assert(!AppleAgxGpuvaV5ClientCall(&c,&q,&r));
    return 0;
}
