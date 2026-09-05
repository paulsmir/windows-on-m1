#include "hv_agx_retained_mmio.h"
#include "../drivers/apple-agx/shared/include/apple_agx_retained_root_client.h"
#include <assert.h>
#include <string.h>
static unsigned calls;
static void execute(void *ctx, const AGX_RR_REQUEST *q, AGX_RR_RESPONSE *r) {
  (void)ctx; ++calls; r->Status = 0; r->Epoch = 17; r->Handle = q->Va;
}
static unsigned long long rd(void *ctx,unsigned offset) {
  unsigned long long v=0;
  assert(hv_agx_retained_mmio(ctx,offset-AGX_RR_OFFSET,&v,0,3,execute,0));
  return v;
}
static void wr64(void *ctx,unsigned offset,unsigned long long v) {
  assert(hv_agx_retained_mmio(ctx,offset-AGX_RR_OFFSET,&v,1,3,execute,0));
}
static void wr32(void *ctx,unsigned offset,unsigned v) {
  unsigned long long data=v;
  assert(hv_agx_retained_mmio(ctx,offset-AGX_RR_OFFSET,&data,1,2,execute,0));
}
int main(void) {
  assert(!AgxRrGpuRegionWritable(0,8));
  assert(!AgxRrGpuRegionWritable(16,8));
  assert(!AgxRrGpuRegionWritable(1000,8));
  assert(AgxRrGpuRegionWritable(1008,1));
  assert(AgxRrGpuRegionWritable(1016,8));
  assert(!AgxRrGpuRegionWritable(1024,8));
  struct hv_agx_retained_mmio state = {0};
  AGX_RR_REQUEST q = {2,64,AGX_RR_MAP,0,1,17,0xffffffa010000000ULL,0x200000,0x4000,0};
  unsigned long long value;
  unsigned i;
  for(i=0;i<sizeof(q);i+=8) {
    memcpy(&value,(char *)&q+i,8);
    assert(hv_agx_retained_mmio(&state,i,&value,1,3,execute,0));
  }
  value=1;
  assert(hv_agx_retained_mmio(&state,64,&value,1,2,execute,0));
  assert(calls==1 && state.Response.Status==0 && state.Response.Receipt==1);
  assert(hv_agx_retained_mmio(&state,64,&value,1,2,execute,0));
  assert(calls==1 && state.Response.Status!=0);
  state.Request.Sequence=2; state.Request.Version=1;
  assert(hv_agx_retained_mmio(&state,64,&value,1,2,execute,0));
  assert(calls==1 && state.Response.Status!=0);
  state.Request.Version=2; state.Request.Bytes=63;
  assert(hv_agx_retained_mmio(&state,64,&value,1,2,execute,0));
  assert(calls==1);
  assert(!hv_agx_retained_mmio(&state,128,&value,1,3,execute,0));
  assert(!hv_agx_retained_mmio(&state,1,&value,1,3,execute,0));
  assert(!hv_agx_retained_mmio(&state,248,&value,0,3,execute,0));
  state.Request.Bytes=64; state.Request.Reserved=1;
  assert(hv_agx_retained_mmio(&state,64,&value,1,2,execute,0));
  assert(calls==1);
  {
    struct hv_agx_retained_mmio other={0};
    AGX_RR_IO io={&other,rd,wr64,wr32};
    AGX_RR_REQUEST request={0}; AGX_RR_RESPONSE response={0};
    request.Command=AGX_RR_MAP; request.Va=0xffffffa010000000ULL;
    assert(AgxRrExchange(&io,&request,&response));
    assert(response.Epoch==17 && response.Handle==request.Va && response.Receipt==1);
    assert(AgxRrExchange(&io,&request,&response) && response.Receipt==2);
    other.Response.Receipt=~0ULL;
    assert(!AgxRrExchange(&io,&request,&response));
  }
  return 0;
}
