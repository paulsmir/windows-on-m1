#include "gpuva_semantic_model.h"
#include <string.h>

static unsigned char allocate(void *opaque, APPLE_AGX_UAT_PAGE *page) {
  gm_process *p=opaque;
  if(p->allocations>=GM_TABLES) return 0;
  unsigned i=p->allocations++;
  memset(p->storage[i],0,sizeof(p->storage[i]));
  page->PhysicalAddress=p->allocator_base+i*GM_PAGE;
  page->Entries=p->storage[i];
  return 1;
}
static void release(void *opaque, const APPLE_AGX_UAT_PAGE *page) {
  (void)opaque; (void)page; /* Monotonic bounded fixture storage; no real pages. */
}
static int live(gm_model *m,unsigned id) { return id<GM_PROCESSES && m->process[id].live; }
static int busy(gm_model *m,unsigned id) { return m->busy && m->slot_owner==(int)id; }
static int binding(gm_model *m,gm_binding b) {
  if(!live(m,b.process)) return 0;
  gm_process *p=&m->process[b.process];
  return p->reserved && p->generation==b.process_gen &&
         p->allocation_gen==b.allocation_gen && p->va==b.va && b.size==GM_PAGE;
}
static int user_range(uint64_t va) {
  return va>=GM_PAGE && va<(1ULL<<39) && !(va&(GM_PAGE-1)) && GM_PAGE<=(1ULL<<39)-va;
}
static int owned_data_page(unsigned process,uint64_t pa) {
  /* Exact synthetic backing registrations for this finite fixture, not a
   * permission to map arbitrary RAM/pool pages. Tables and firmware excluded. */
  const uint64_t registered[GM_PROCESSES][3]={{0x20000000,0x21000000,0x22000000},
                     {0x30000000,0x31000000,0x32000000}, {0x40000000,0x41000000,0x42000000}};
  for(unsigned i=0;i<3;++i)
    if(pa>=registered[process][i] && pa-registered[process][i]<GM_PAGE) return 1;
  return 0;
}
void gm_initialize(gm_model *m) {
  memset(m,0,sizeof(*m)); m->slot_owner=-1;
  m->caps=(gm_caps){GM_SEGMENT_64K,GM_LOCAL_SEGMENT_ID};
  m->firmware_identity=0x90000000ULL;
  m->firmware_prefix[0]=0x12340003; m->firmware_prefix[1]=0x56780003;
}
enum gm_result gm_choose_caps(gm_model *m, uint64_t page) {
  if(!m || (page!=GM_SEGMENT_16K && page!=GM_SEGMENT_64K)) return GM_RANGE;
  m->caps.segment_page=page; m->caps.local_segment_id=GM_LOCAL_SEGMENT_ID;
  return GM_OK;
}
enum gm_result gm_table_levels(const gm_model *m, gm_level_desc out[3]) {
  if(!m || !out || m->caps.local_segment_id==0) return GM_RANGE;
  /* Native 39-bit VA = 3 + 11 + 11 + 14. All actual UAT tables are 16 KiB
   * in a VidMm-owned local segment; system segment 0 cannot hold them. */
  const unsigned bits[3]={3,11,11};
  for(unsigned i=0;i<3;++i)
    out[i]=(gm_level_desc){bits[i],m->caps.local_segment_id,
                           m->caps.local_segment_id,(unsigned)GM_PAGE,(unsigned)GM_PAGE};
  return GM_OK;
}
enum gm_result gm_segment_ptes(const gm_model *m, uint64_t base,
                               uint64_t offset, unsigned writable, gm_pte4 out[4]) {
  if(!m || !out || writable>1) return GM_RANGE;
  uint64_t size=m->caps.segment_page;
  if((size!=GM_SEGMENT_16K && size!=GM_SEGMENT_64K) ||
     (base&(size-1)) || (offset&(GM_PAGE-1)) || offset>size-GM_PAGE ||
     base>=(1ULL<<40) || size>(1ULL<<40)-base) return GM_RANGE;
  for(unsigned i=0;i<4;++i)
    out[i]=(gm_pte4){base+offset+i*GM_LOGICAL_PAGE,1,writable};
  return GM_OK;
}
enum gm_result gm_update_span(const gm_pte4 before[8], unsigned first,
                              unsigned count, const gm_pte4 *updates, gm_pte4 after[8]) {
  if(!before || !updates || !after || !count || first>=8 || count>8-first)
    return GM_RANGE;
  gm_pte4 candidate[8]; uint64_t pa; unsigned write;
  memcpy(candidate,before,sizeof(candidate));
  memcpy(candidate+first,updates,count*sizeof(*updates));
  for(unsigned group=0;group<2;++group) {
    enum gm_result r=gm_coarsen(candidate+group*4,&pa,&write);
    if(r!=GM_OK && r!=GM_UNMAP) return r;
  }
  memcpy(after,candidate,sizeof(candidate)); return GM_OK;
}
enum gm_result gm_create(gm_model *m,unsigned id) {
  if(id>=GM_PROCESSES || m->process[id].live) return GM_STALE;
  gm_process *p=&m->process[id]; memset(p,0,sizeof(*p));
  p->live=1; p->id=id; p->generation=++m->serial; p->root_gen=++m->serial;
  p->allocator_base=0x10000000ULL+id*0x01000000ULL;
  p->allocator=(APPLE_AGX_UAT_ALLOCATOR){p,allocate,release};
  p->inventory=(APPLE_AGX_UAT_INVENTORY){p->pages,GM_TABLES,0,p->mappings,4,0};
  return GM_OK;
}
enum gm_result gm_bootstrap(gm_model *m,unsigned id) {
  if(!live(m,id)) return GM_STALE;
  gm_process *p=&m->process[id];
  if(p->bootstrap_done) return GM_OK;
  if(id!=GM_SYSTEM && (!live(m,GM_SYSTEM) || !m->process[GM_SYSTEM].bootstrap_done))
    return GM_DEPENDENCY;
  if(AppleAgxUatCreateAddressSpace(id+1,&p->allocator,&p->inventory,&p->roots)!=AppleAgxUatResultOk)
    return GM_TABLE_ERROR;
  p->bootstrap_done=1; return GM_OK;
}
enum gm_result gm_reserve(gm_model *m,unsigned id,uint64_t va,gm_binding *out) {
  if(!live(m,id)) return GM_STALE;
  if(!out || !user_range(va)) return GM_RANGE;
  gm_process *p=&m->process[id]; if(p->reserved) return GM_BUSY;
  p->va=va; p->reserved=1; p->allocation_gen=++m->serial;
  *out=(gm_binding){id,p->generation,p->allocation_gen,va,GM_PAGE}; return GM_OK;
}
enum gm_result gm_coarsen(const gm_pte4 entries[4],uint64_t *pa,unsigned *write) {
  if(!entries || !pa || !write) return GM_RANGE;
  unsigned valid=0;
  for(unsigned i=0;i<4;++i) valid|=entries[i].valid;
  if(!valid) { *pa=0; *write=0; return GM_UNMAP; }
  /* One actual UAT leaf has one aligned base and one protection/validity.
   * No invented aliases, rounding over holes, bounce copies or hidden heap. */
  if(!entries[0].valid || (entries[0].pa&(GM_PAGE-1)) ||
     entries[0].pa>=(1ULL<<40) || GM_PAGE>(1ULL<<40)-entries[0].pa)
    return GM_UNREPRESENTABLE;
  for(unsigned i=0;i<4;++i)
    if(entries[i].valid!=1 || entries[i].writable>1 ||
       entries[i].writable!=entries[0].writable || entries[i].pa!=entries[0].pa+i*0x1000ULL)
      return GM_UNREPRESENTABLE;
  *pa=entries[0].pa; *write=entries[0].writable; return GM_OK;
}
enum gm_result gm_merge4(const gm_pte4 before[4],unsigned first,unsigned count,
                        const gm_pte4 *updates,gm_pte4 after[4]) {
  if(!before || !updates || !after || !count || first>=4 || count>4-first) return GM_RANGE;
  gm_pte4 candidate[4]; uint64_t pa; unsigned write;
  memcpy(candidate,before,sizeof(candidate));
  memcpy(candidate+first,updates,count*sizeof(*updates));
  enum gm_result r=gm_coarsen(candidate,&pa,&write);
  if(r==GM_OK || r==GM_UNMAP) memcpy(after,candidate,sizeof(candidate));
  return r;
}
static void paging_issue(gm_process *p,gm_fence *f) {
  ++p->map_gen; ++p->paging_issued;
  *f=(gm_fence){GM_PAGING,p->id,p->generation,p->paging_issued};
}
enum gm_result gm_map(gm_model *m,gm_binding b,const gm_pte4 entries[4],gm_fence *f) {
  if(!binding(m,b)) return GM_STALE;
  if(!f) return GM_RANGE;
  gm_process *p=&m->process[b.process]; uint64_t pa; unsigned write;
  if(!entries) return GM_RANGE;
  for(unsigned i=0;i<4;++i)
    if(entries[i].valid && !owned_data_page(b.process,entries[i].pa)) return GM_RANGE;
  enum gm_result r=gm_coarsen(entries,&pa,&write); if(r!=GM_OK) return r;
  if(busy(m,b.process) || p->mapped) return GM_BUSY;
  if(!p->bootstrap_done || p->paging_issued!=p->paging_done) return GM_DEPENDENCY;
  if(AppleAgxUatMap(b.process+1,&p->roots,b.va,pa,GM_PAGE,
      write?AppleAgxUatGpuSharedReadWrite:AppleAgxUatGpuSharedReadOnly,
      &p->allocator,&p->inventory)!=AppleAgxUatResultOk) return GM_TABLE_ERROR;
  p->mapped=1; paging_issue(p,f); return GM_OK;
}
enum gm_result gm_flush(gm_model *m,unsigned id) {
  if(!live(m,id)) return GM_STALE;
  if(busy(m,id)) return GM_BUSY;
  gm_process *p=&m->process[id];
  p->flushed_map_gen=p->map_gen; return GM_OK;
}
enum gm_result gm_paging_complete(gm_model *m,gm_fence f) {
  if(f.domain!=GM_PAGING || !live(m,f.process)) return GM_STALE;
  gm_process *p=&m->process[f.process];
  if(f.process_gen!=p->generation || f.sequence!=p->paging_issued || f.sequence<=p->paging_done)
    return GM_STALE;
  if(p->flushed_map_gen!=p->map_gen) return GM_DEPENDENCY;
  p->paging_done=f.sequence; return GM_OK;
}
enum gm_result gm_resident(gm_model *m,gm_binding b) {
  if(!binding(m,b)) return GM_STALE;
  m->process[b.process].resident=1; /* Abstract external VidMm residency grant. */
  return GM_OK;
}
enum gm_result gm_evict(gm_model *m,gm_binding b,gm_fence *f) {
  if(!binding(m,b)) return GM_STALE;
  if(!f) return GM_RANGE;
  gm_process *p=&m->process[b.process];
  if(busy(m,b.process)) return GM_BUSY;
  if(p->paging_issued!=p->paging_done) return GM_DEPENDENCY;
  if(p->mapped && AppleAgxUatUnmap(b.process+1,&p->roots,b.va,GM_PAGE,
       &p->allocator,&p->inventory)!=AppleAgxUatResultOk) return GM_TABLE_ERROR;
  p->mapped=0; p->resident=0; paging_issue(p,f); return GM_OK;
}
enum gm_result gm_close(gm_model *m,gm_binding b,gm_fence *f) {
  enum gm_result r=gm_evict(m,b,f);
  if(r==GM_OK) {m->process[b.process].reserved=0; ++m->process[b.process].allocation_gen;}
  return r;
}
enum gm_result gm_relocate_root(gm_model *m,unsigned id) {
  if(!live(m,id)) return GM_STALE;
  if(busy(m,id)) return GM_BUSY;
  gm_process *p=&m->process[id]; APPLE_AGX_UAT_PAGE fresh={0}, *old=0;
  if(!p->bootstrap_done || p->paging_issued!=p->paging_done) return GM_DEPENDENCY;
  for(unsigned i=0;i<p->inventory.PageCount;++i)
    if(p->pages[i].PhysicalAddress==p->roots.Ttbr0PhysicalAddress) old=&p->pages[i];
  if(!old || p->inventory.PageCount>=GM_TABLES || !allocate(p,&fresh)) return GM_TABLE_ERROR;
  memcpy(fresh.Entries,old->Entries,(size_t)GM_PAGE);
  fresh.Level=0; p->pages[p->inventory.PageCount++]=fresh;
  p->roots.Ttbr0PhysicalAddress=fresh.PhysicalAddress; p->root_gen=++m->serial;
  /* Keep retired root backing for this finite fixture; never premature free. */
  return GM_OK;
}
enum gm_result gm_bind_slot(gm_model *m,unsigned id,gm_lease *out) {
  if(!live(m,id)) return GM_STALE;
  if(!out) return GM_RANGE;
  if(m->busy) return GM_BUSY;
  if(m->slot_owner!=-1 && !m->invalidated) return GM_DEPENDENCY;
  gm_process *p=&m->process[id]; if(!p->bootstrap_done) return GM_DEPENDENCY;
  m->slot_owner=(int)id; m->invalidated=0; ++m->slot_gen;
  m->current=(gm_lease){id,p->generation,p->root_gen,p->map_gen,m->slot_gen};
  *out=m->current; return GM_OK;
}
enum gm_result gm_invalidate_slot(gm_model *m) {
  if(m->busy) return GM_BUSY;
  m->invalidated=1; return GM_OK; /* Explicit simulated invalidation acknowledgement. */
}
enum gm_result gm_submit(gm_model *m,gm_binding b,gm_lease lease,gm_fence *out) {
  if(!binding(m,b)) return GM_STALE;
  gm_process *p=&m->process[b.process];
  if(lease.process!=b.process || lease.process_gen!=p->generation ||
     lease.root_gen!=p->root_gen || lease.map_gen!=p->map_gen ||
     lease.slot_gen!=m->slot_gen || m->slot_owner!=(int)b.process || m->invalidated)
    return GM_STALE;
  if(!p->mapped || p->paging_issued!=p->paging_done) return GM_DEPENDENCY;
  if(!p->resident) return GM_NOT_RESIDENT;
  if(m->busy) return GM_BUSY;
  if(!out) return GM_RANGE;
  m->busy=1; m->active_render=(gm_fence){GM_RENDER,b.process,p->generation,++m->render_issued};
  *out=m->active_render; return GM_OK;
}
enum gm_result gm_render_complete(gm_model *m,gm_fence f) {
  if(f.domain!=GM_RENDER || !m->busy || f.process!=m->active_render.process ||
     f.process_gen!=m->active_render.process_gen || f.sequence!=m->active_render.sequence)
    return GM_STALE;
  m->busy=0; m->render_done=f.sequence; return GM_OK;
}
enum gm_result gm_translate(gm_model *m,unsigned id,uint64_t va,uint64_t *pa) {
  if(!live(m,id)) return GM_STALE;
  if(!pa || va>=1ULL<<39) return GM_RANGE;
  gm_process *p=&m->process[id]; unsigned long long address,descriptor;
  if(AppleAgxUatResolvePage(id+1,&p->roots,va&~(GM_PAGE-1),&p->inventory,&address,&descriptor)!=AppleAgxUatResultOk)
    return GM_RANGE;
  *pa=address+(va&(GM_PAGE-1)); return GM_OK;
}
enum gm_result gm_destroy(gm_model *m,unsigned id) {
  if(!live(m,id)) return GM_STALE;
  if(id==GM_SYSTEM && (live(m,0) || live(m,1))) return GM_BUSY;
  gm_process *p=&m->process[id];
  if(busy(m,id) || p->reserved || p->paging_issued!=p->paging_done) return GM_BUSY;
  if(m->slot_owner==(int)id) {
    if(!m->invalidated) return GM_DEPENDENCY;
    m->slot_owner=-1;
  }
  p->live=0; ++p->generation; return GM_OK;
}
