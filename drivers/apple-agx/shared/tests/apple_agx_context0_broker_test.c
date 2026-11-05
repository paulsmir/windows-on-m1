#define main graph_fixture_reference_main
#include "apple_agx_initdata_memory_test.c"
#undef main
#define main retained_fixture_reference_main
#include "../../../../tests/agx_retained_root_test.c"
#undef main
#include "apple_agx_context0_broker.h"
#include "../../../../m1n1_windows/src/hv_agx_retained_mmio.h"

static FAKE_MEMORY *windows_memory;
struct peer {
  struct fixture native;
  struct hv_agx_retained_mmio wire;
  unsigned map_calls, fail_map, fail_unmap, fail_absent, fail_query;
};
static unsigned long long guest_translate(void *context,unsigned long long ipa) {
  unsigned i; (void)context;
  for(i=0;i<windows_memory->AllocateCount;++i) {
    FAKE_ALLOCATION *a=&windows_memory->Allocations[i];
    unsigned long long base=a->DeviceBase+0x400000000ULL;
    if(a->Active && ipa>=base && ipa-base<=a->Length-0x4000)
      return a->DeviceBase+ipa-base;
  }
  return 0;
}
static unsigned char resolve_ipa(void *context,const APPLE_AGX_MEMORY_OBJECT *o,
    unsigned long long offset,unsigned long long *ipa) {
  (void)context;
  if(!o || offset>o->Length || 0x4000>o->Length-offset) return 0;
  *ipa=o->DeviceAddress+0x400000000ULL+offset; return 1;
}
static void peer_execute(void *opaque,const AGX_RR_REQUEST *q,AGX_RR_RESPONSE *r) {
  struct peer *p=opaque; struct hv_agx_retained_root *c=&p->native.core;
  switch(q->Command) {
  case AGX_RR_MAP:
    r->Status=++p->map_calls==p->fail_map ? HV_AGX_RETAINED_ALLOCATION :
      hv_agx_retained_map(c,q->Epoch,q->Va,q->Ipa,q->Length,&r->Handle); break;
  case AGX_RR_QUERY:
    r->Status=hv_agx_retained_query(c,q->Epoch,q->Handle,q->Va,q->Ipa,q->Length,&r->Pa);
    if(p->fail_query) r->Pa+=0x4000;
    break;
  case AGX_RR_UNMAP:
    r->Status=p->fail_unmap ? HV_AGX_RETAINED_STATE :
      hv_agx_retained_unmap(c,q->Epoch,q->Handle,q->Va,q->Ipa,q->Length); break;
  case AGX_RR_VERIFY_ABSENT:
    r->Status=p->fail_absent ? HV_AGX_RETAINED_STATE :
      hv_agx_retained_verify_absent(c,q->Epoch,q->Handle,q->Va,q->Ipa,q->Length); break;
  default: r->Status=99; break;
  }
  r->Epoch=c->Epoch; r->Root=c->Roots.Ttbr1PhysicalAddress;
  r->Count=c->MappingCount; r->Flags=7;
}
static unsigned long long peer_read(void *ctx,unsigned offset) {
  struct peer *p=ctx; unsigned long long value=0;
  assert(hv_agx_retained_mmio(&p->wire,offset-AGX_RR_OFFSET,&value,0,3,peer_execute,p)); return value;
}
static void peer_write64(void *ctx,unsigned offset,unsigned long long value) {
  struct peer *p=ctx;
  assert(hv_agx_retained_mmio(&p->wire,offset-AGX_RR_OFFSET,&value,1,3,peer_execute,p));
}
static void peer_write32(void *ctx,unsigned offset,unsigned value) {
  struct peer *p=ctx; unsigned long long data=value;
  assert(hv_agx_retained_mmio(&p->wire,offset-AGX_RR_OFFSET,&data,1,2,peer_execute,p));
}
int main(void) {
  unsigned fail;
  for(fail=0;fail<=201;++fail) {
    struct peer p={0}; FAKE_MEMORY memory; APPLE_AGX_MEMORY_IO memory_io;
    APPLE_AGX_INITDATA_MEMORY_GRAPH graph;
    APPLE_AGX_CONFIG_SNAPSHOT snapshot=physical_snapshot();
    APPLE_AGX_CONTEXT0_BROKER journal={0};
    AGX_RR_IO io={&p,peer_read,peer_write64,peer_write32};
    int result;
    init_fixture(&memory,&memory_io,&graph); windows_memory=&memory;
    assert(AppleAgxInitdataMemoryPrepareBroker(&graph,&memory_io,&snapshot)==0);
    start(&p.native); p.native.core.Ops.TranslateGuest=guest_translate;
    p.fail_map=fail<=200?fail:0; p.fail_query=fail==201;
    result=AppleAgxContext0BrokerMap(&journal,&graph,&io,1,ROOT_PA,resolve_ipa,0);
    if(fail==201) assert(result==AppleAgxContext0QueryFailed && journal.Count==1);
    else if(fail) assert(result!=0 && journal.Count==fail-1);
    else {
      assert(result==0 && journal.Count==200 && graph.MappingsReady);
      assert(AppleAgxContext0BrokerVerify(&journal)==0);
      p.fail_unmap=1;
      assert(AppleAgxContext0BrokerRetire(&journal)!=0);
      assert(AppleAgxInitdataMemoryDestroy(&graph)!=0 && memory.FreeCount==0);
      p.fail_unmap=0;
      p.fail_absent=1;
      assert(AppleAgxContext0BrokerRetire(&journal)!=0);
      assert(journal.Count==200 && p.native.core.MappingCount==199);
      assert(AppleAgxInitdataMemoryDestroy(&graph)!=0 && memory.FreeCount==0);
      p.fail_absent=0;
    }
    assert(AppleAgxContext0BrokerRetire(&journal)==0);
    assert(!journal.Count && !graph.BrokerOutstanding && !p.native.core.MappingCount);
    check_prefix(&p.native);
    assert(p.native.core.SystemBytes==0x4000);
    assert(hv_agx_retained_close(&p.native.core,1,1)==0 && !p.native.live);
    assert(AppleAgxInitdataMemoryDestroy(&graph)==0 && memory.FreeCount==89);
    assert(!graph.Roots.Ttbr0PhysicalAddress && !graph.Roots.Ttbr1PhysicalAddress);
    if(!fail) {
      memset(&memory,0,sizeof(memory));
      assert(AppleAgxInitdataMemoryPrepareBroker(&graph,&memory_io,&snapshot)==0);
      assert(prepare(&p.native,2)==0); firmware_boot(&p.native);
      p.native.core.Ops.TranslateGuest=guest_translate;
      assert(hv_agx_retained_activate(&p.native.core)==0);
      assert(AppleAgxContext0BrokerMap(&journal,&graph,&io,2,ROOT_PA,resolve_ipa,0)==0);
      assert(AppleAgxContext0BrokerVerify(&journal)==0);
      assert(AppleAgxContext0BrokerRetire(&journal)==0);
      check_prefix(&p.native);
      assert(hv_agx_retained_close(&p.native.core,2,1)==0 && !p.native.live);
      assert(AppleAgxInitdataMemoryDestroy(&graph)==0 && memory.FreeCount==89);
    }
  }
  return 0;
}
