#include "apple_agx_context0_broker.h"

static unsigned char exchange(APPLE_AGX_CONTEXT0_BROKER *j,unsigned op,
                              const APPLE_AGX_CONTEXT0_LEAF *leaf) {
  AGX_RR_REQUEST q={0};
  j->LastOperation=op;
  j->LastResponse=(AGX_RR_RESPONSE){0};
  q.Command=op; q.Epoch=j->Epoch; q.Va=leaf->Va; q.Ipa=leaf->Ipa; q.Length=0x4000;
  q.Handle=op==AGX_RR_MAP?0:leaf->Handle;
  return AgxRrExchange(&j->Io,&q,&j->LastResponse) &&
      j->LastResponse.Epoch==j->Epoch && j->LastResponse.Root==j->Root &&
      (j->LastResponse.Flags&(AGX_RR_FLAG_ACTIVE|AGX_RR_FLAG_PREFIX_UNCHANGED))==
          (AGX_RR_FLAG_ACTIVE|AGX_RR_FLAG_PREFIX_UNCHANGED);
}

static unsigned char graph_valid(const APPLE_AGX_INITDATA_MEMORY_GRAPH *g) {
  unsigned i,k,total=0;
  if(!g || !g->Initialized || !g->Built || !g->BrokerOnly || g->MappingsReady ||
      g->BrokerOutstanding || g->Roots.Ttbr0PhysicalAddress || g->Roots.Ttbr1PhysicalAddress ||
      g->TtbrPair.Ttbr0 || g->TtbrPair.Ttbr1 || g->Inventory.PageCount ||
      g->Inventory.MappingCount!=90) return 0;
  for(i=0;i<g->Inventory.MappingCount;++i) {
    const APPLE_AGX_UAT_MAPPING *m=&g->UatMappings[i];
    const APPLE_AGX_MEMORY_OBJECT *o=g->MappingObjects[i];
    if(!o || o->State==AppleAgxMemoryEmpty || !o->CpuAddress || o->DevicePages ||
        o->DevicePageCount || m->Context || !m->Length || (m->Length&0x3fff) ||
        (m->VirtualAddress&0x3fff) || m->PhysicalAddress!=o->DeviceAddress ||
        m->Length!=o->Length || m->Protection!=AppleAgxUatFirmwareSharedReadWrite ||
        m->VirtualAddress>~0ULL-m->Length || m->PhysicalAddress>~0ULL-m->Length ||
        m->Length/0x4000 > APPLE_AGX_CONTEXT0_MAX_LEAVES-total) return 0;
    total+=(unsigned)(m->Length/0x4000);
    for(k=0;k<i;++k) {
      const APPLE_AGX_UAT_MAPPING *p=&g->UatMappings[k];
      if(m->VirtualAddress<p->VirtualAddress+p->Length &&
         p->VirtualAddress<m->VirtualAddress+m->Length) return 0;
      if(o!=g->MappingObjects[k] && m->PhysicalAddress<p->PhysicalAddress+p->Length &&
         p->PhysicalAddress<m->PhysicalAddress+m->Length) return 0;
    }
  }
  return total==200;
}

int AppleAgxContext0BrokerMap(APPLE_AGX_CONTEXT0_BROKER *j,
    APPLE_AGX_INITDATA_MEMORY_GRAPH *g,const AGX_RR_IO *io,
    unsigned long long epoch,unsigned long long root,APPLE_AGX_CONTEXT0_IPA resolve,void *ctx) {
  unsigned i; unsigned long long offset;
  if(!j || j->Count || j->Uncertain || !io || !resolve || !epoch || !root ||
      (root&0x3fff) || !graph_valid(g)) return AppleAgxContext0Invalid;
  j->Graph=g; j->Io=*io; j->Epoch=epoch; j->Root=root;
  j->Mapped=j->Verified=j->Retired=j->Absent=0;
  for(i=0;i<g->Inventory.MappingCount;++i) {
    const APPLE_AGX_UAT_MAPPING *m=&g->UatMappings[i];
    j->FailedRange=i;
    for(offset=0;offset<m->Length;offset+=0x4000) {
      APPLE_AGX_CONTEXT0_LEAF leaf={m->VirtualAddress+offset,0,m->PhysicalAddress+offset,0,i,1};
      if(!resolve(ctx,g->MappingObjects[i],offset,&leaf.Ipa) || (leaf.Ipa&0x3fff))
        return AppleAgxContext0Invalid;
      if(!exchange(j,AGX_RR_MAP,&leaf)) {
        /* A success-looking reply that cannot be authenticated may represent
         * a committed mapping. Hold backing, never unmap a guessed handle. */
        if(j->LastResponse.Status==0 || j->LastResponse.Handle) {
          j->Uncertain=1; ++g->BrokerOutstanding;
        }
        return AppleAgxContext0MapFailed;
      }
      leaf.Handle=j->LastResponse.Handle;
      if(!leaf.Handle) {j->Uncertain=1; ++g->BrokerOutstanding;return AppleAgxContext0MapFailed;}
      j->Leaves[j->Count++]=leaf; ++g->BrokerOutstanding; ++j->Mapped;
      if(j->LastResponse.Count!=j->Count || !exchange(j,AGX_RR_QUERY,&leaf) ||
          j->LastResponse.Pa!=leaf.Pa || j->LastResponse.Count!=j->Count)
        return AppleAgxContext0QueryFailed;
      ++j->Verified;
    }
  }
  for(i=0;i<APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT;++i)
    if(AppleAgxMemoryMarkGpuMapped(&g->ChannelMemory.Objects[i],0,g->ChannelMemory.VirtualAddresses[i])!=0)
      return AppleAgxContext0Invalid;
  for(i=0;i<APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;++i)
    if(AppleAgxMemoryMarkGpuMapped(&g->RenderSharedMemory.Objects[i],0,g->RenderSharedMemory.VirtualAddresses[i])!=0)
      return AppleAgxContext0Invalid;
  g->MappingsReady=1;
  j->FailedRange=~0u;
  return AppleAgxContext0Ok;
}

int AppleAgxContext0BrokerVerify(APPLE_AGX_CONTEXT0_BROKER *j) {
  unsigned i;
  if(!j || !j->Graph || !j->Graph->MappingsReady || j->Uncertain || j->Count!=200)
    return AppleAgxContext0Invalid;
  for(i=0;i<j->Count;++i)
    if(j->Leaves[i].State!=1 || !exchange(j,AGX_RR_QUERY,&j->Leaves[i]) ||
       j->LastResponse.Pa!=j->Leaves[i].Pa || j->LastResponse.Count!=j->Count)
      return AppleAgxContext0QueryFailed;
  return AppleAgxContext0Ok;
}

int AppleAgxContext0BrokerRetire(APPLE_AGX_CONTEXT0_BROKER *j) {
  unsigned i;
  if(!j || j->Uncertain) return AppleAgxContext0RetireFailed;
  if(!j->Graph) return j->Count?AppleAgxContext0RetireFailed:AppleAgxContext0Ok;
  while(j->Count) {
    APPLE_AGX_CONTEXT0_LEAF *leaf=&j->Leaves[j->Count-1];
    if(leaf->State==1) {
      if(!exchange(j,AGX_RR_UNMAP,leaf) || j->LastResponse.Count!=j->Count-1)
        return AppleAgxContext0RetireFailed;
      leaf->State=2; ++j->Retired;
    }
    if(!exchange(j,AGX_RR_VERIFY_ABSENT,leaf) || j->LastResponse.Count!=j->Count-1)
      return AppleAgxContext0RetireFailed;
    ++j->Absent; --j->Count; --j->Graph->BrokerOutstanding;
  }
  for(i=0;i<j->Graph->Inventory.MappingCount;++i) {
    APPLE_AGX_MEMORY_OBJECT *o=j->Graph->MappingObjects[i];
    if(o->State==AppleAgxMemoryGpuMapped || o->State==AppleAgxMemoryCompleted)
      if(AppleAgxMemoryMarkGpuUnmapped(o)!=0) return AppleAgxContext0RetireFailed;
  }
  j->Graph->MappingsReady=0;
  return AppleAgxContext0Ok;
}
