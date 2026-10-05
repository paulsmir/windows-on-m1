#include "gpuva_semantic_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VA 0x20000ULL
#define PA_P 0x20000000ULL
#define PA_Q 0x30000000ULL
static gm_model *m;
static unsigned checks;
#define CHECK(x) do { assert(x); ++checks; } while (0)
static void reset(void) {
  gm_initialize(m); CHECK(gm_create(m,GM_SYSTEM)==GM_OK);
  CHECK(gm_bootstrap(m,GM_SYSTEM)==GM_OK);
  CHECK(gm_create(m,0)==GM_OK); CHECK(gm_create(m,1)==GM_OK);
}
static void ptes(gm_pte4 out[4], uint64_t base) {
  for(unsigned i=0;i<4;++i) out[i]=(gm_pte4){base+i*0x1000ULL,1,1};
}
static gm_binding ready(unsigned p, uint64_t pa) {
  gm_binding b; gm_fence f; gm_pte4 entries[4]; ptes(entries,pa);
  CHECK(gm_bootstrap(m,p)==GM_OK);
  CHECK(gm_reserve(m,p,VA,&b)==GM_OK);
  CHECK(gm_map(m,b,entries,&f)==GM_OK);
  CHECK(gm_paging_complete(m,f)==GM_DEPENDENCY);
  CHECK(gm_flush(m,p)==GM_OK); CHECK(gm_paging_complete(m,f)==GM_OK);
  return b;
}
static void isolation_root_and_private(void) {
  reset(); gm_binding p=ready(0,PA_P), q=ready(1,PA_Q); uint64_t pa;
  CHECK(p.va==q.va);
  CHECK(m->process[0].roots.Ttbr0PhysicalAddress!=m->process[1].roots.Ttbr0PhysicalAddress);
  CHECK(m->process[0].generation!=m->process[1].generation);
  CHECK(gm_translate(m,0,VA+0x1234,&pa)==GM_OK && pa==PA_P+0x1234);
  CHECK(gm_translate(m,1,VA+0x1234,&pa)==GM_OK && pa==PA_Q+0x1234);
  uint64_t qroot=m->process[1].roots.Ttbr0PhysicalAddress, qgen=m->process[1].root_gen;
  uint64_t old=m->process[0].roots.Ttbr0PhysicalAddress;
  CHECK(gm_relocate_root(m,0)==GM_OK);
  CHECK(m->process[0].roots.Ttbr0PhysicalAddress!=old);
  CHECK(qroot==m->process[1].roots.Ttbr0PhysicalAddress && qgen==m->process[1].root_gen);
  CHECK(m->process[0].va==VA);
  CHECK(gm_translate(m,0,VA+0x3fff,&pa)==GM_OK && pa==PA_P+0x3fff);
  CHECK(gm_translate(m,0,0xffffffa041000000ULL,&pa)==GM_RANGE);
  gm_binding ignored;
  CHECK(gm_reserve(m,0,0xffffffa041000000ULL,&ignored)==GM_RANGE);
  CHECK(gm_reserve(m,1,(1ULL<<39)-0x1000,&ignored)==GM_RANGE);
  CHECK(gm_translate(m,0,VA+GM_PAGE,&pa)!=GM_OK);
  CHECK(m->firmware_identity==0x90000000ULL);
  CHECK(m->firmware_prefix[0]==0x12340003 && m->firmware_prefix[1]==0x56780003);
  unsigned long long rawpa,descriptor;
  CHECK(AppleAgxUatResolvePage(1,&m->process[0].roots,0xffffffa041000000ULL,
        &m->process[0].inventory,&rawpa,&descriptor)==AppleAgxUatResultNotMapped);
}
static void physical_backing_ownership(void) {
  reset(); CHECK(gm_bootstrap(m,0)==GM_OK);
  gm_binding b; CHECK(gm_reserve(m,0,VA,&b)==GM_OK);
  gm_pte4 forbidden[4]; gm_fence not_issued;
  ptes(forbidden,m->firmware_identity);
  CHECK(gm_map(m,b,forbidden,&not_issued)==GM_RANGE);
  ptes(forbidden,PA_Q);
  CHECK(gm_map(m,b,forbidden,&not_issued)==GM_RANGE);
  ptes(forbidden,m->process[0].roots.Ttbr0PhysicalAddress);
  CHECK(gm_map(m,b,forbidden,&not_issued)==GM_RANGE);
  CHECK(!m->process[0].mapped && !m->process[0].paging_issued);
}
static void lifetime_and_fence_domains(void) {
  reset(); gm_binding p=ready(0,PA_P), q=ready(1,PA_Q);
  gm_lease lp,lq; gm_fence render,paging; gm_pte4 entries[4]; uint64_t pa;
  CHECK(gm_bind_slot(m,0,&lp)==GM_OK);
  CHECK(gm_submit(m,p,lp,&render)==GM_NOT_RESIDENT);
  CHECK(gm_resident(m,p)==GM_OK); CHECK(gm_submit(m,p,lp,&render)==GM_OK);
  CHECK(render.sequence==m->process[0].paging_done); /* Equal number, different domains. */
  CHECK(gm_paging_complete(m,render)==GM_STALE);
  CHECK(gm_evict(m,p,&paging)==GM_BUSY);
  CHECK(gm_relocate_root(m,0)==GM_BUSY);
  CHECK(gm_bind_slot(m,1,&lq)==GM_BUSY);
  CHECK(gm_invalidate_slot(m)==GM_BUSY);
  CHECK(gm_render_complete(m,render)==GM_OK);
  CHECK(gm_render_complete(m,render)==GM_STALE);
  CHECK(gm_bind_slot(m,1,&lq)==GM_DEPENDENCY);
  CHECK(gm_invalidate_slot(m)==GM_OK); CHECK(gm_bind_slot(m,1,&lq)==GM_OK);
  CHECK(gm_resident(m,q)==GM_OK);
  CHECK(gm_submit(m,p,lp,&render)==GM_STALE);
  gm_lease stale=lq; --stale.slot_gen; CHECK(gm_submit(m,q,stale,&render)==GM_STALE);
  stale=lq; --stale.root_gen; CHECK(gm_submit(m,q,stale,&render)==GM_STALE);
  stale=lq; --stale.process_gen; CHECK(gm_submit(m,q,stale,&render)==GM_STALE);
  gm_binding wrong=q; wrong.process=0; CHECK(gm_submit(m,wrong,lq,&render)==GM_STALE);
  CHECK(gm_evict(m,p,&paging)==GM_OK);
  CHECK(m->process[0].reserved && m->process[0].va==VA && !m->process[0].resident);
  CHECK(gm_render_complete(m,paging)==GM_STALE);
  CHECK(gm_flush(m,0)==GM_OK); CHECK(gm_paging_complete(m,paging)==GM_OK);
  CHECK(gm_translate(m,0,VA,&pa)!=GM_OK);
  ptes(entries,0x22000000ULL); CHECK(gm_map(m,p,entries,&paging)==GM_OK);
  CHECK(gm_resident(m,p)==GM_OK);
  CHECK(gm_invalidate_slot(m)==GM_OK); CHECK(gm_bind_slot(m,0,&lp)==GM_OK);
  CHECK(gm_submit(m,p,lp,&render)==GM_DEPENDENCY);
  CHECK(gm_flush(m,0)==GM_OK); CHECK(gm_paging_complete(m,paging)==GM_OK);
  CHECK(gm_translate(m,0,VA,&pa)==GM_OK && pa==0x22000000ULL);
  CHECK(p.va==VA); CHECK(gm_submit(m,p,lp,&render)==GM_OK);
  CHECK(gm_render_complete(m,render)==GM_OK);
  CHECK(gm_close(m,p,&paging)==GM_OK);
  CHECK(gm_submit(m,p,lp,&render)==GM_STALE);
  CHECK(gm_flush(m,0)==GM_OK); CHECK(gm_paging_complete(m,paging)==GM_OK);
  gm_binding recreated;
  CHECK(gm_reserve(m,0,VA,&recreated)==GM_OK);
  CHECK(recreated.process_gen==p.process_gen && recreated.allocation_gen!=p.allocation_gen);
  CHECK(gm_resident(m,p)==GM_STALE);
  CHECK(gm_close(m,recreated,&paging)==GM_OK);
  CHECK(gm_flush(m,0)==GM_OK); CHECK(gm_paging_complete(m,paging)==GM_OK);
  CHECK(gm_invalidate_slot(m)==GM_OK); CHECK(gm_destroy(m,0)==GM_OK);
  CHECK(gm_create(m,0)==GM_OK); (void)ready(0,PA_P);
  CHECK(gm_resident(m,p)==GM_STALE);
  CHECK(gm_paging_complete(m,paging)==GM_STALE);
}
static void bootstrap_without_render(void) {
  gm_initialize(m); CHECK(gm_create(m,0)==GM_OK);
  CHECK(gm_bootstrap(m,0)==GM_DEPENDENCY);
  reset(); CHECK(m->slot_owner==-1 && m->render_issued==0);
  (void)ready(0,PA_P);
  CHECK(m->slot_owner==-1 && m->render_issued==0 && m->process[0].paging_done==1);
  CHECK(m->process[GM_SYSTEM].roots.Ttbr0PhysicalAddress!=m->process[0].roots.Ttbr0PhysicalAddress);
  CHECK(gm_destroy(m,GM_SYSTEM)==GM_BUSY);
}
static void relocated_root_rejects_actual_old_lease(void) {
  reset(); gm_binding b=ready(0,PA_P); gm_lease old,fresh; gm_fence f;
  CHECK(gm_resident(m,b)==GM_OK); CHECK(gm_bind_slot(m,0,&old)==GM_OK);
  CHECK(gm_relocate_root(m,0)==GM_OK);
  CHECK(gm_submit(m,b,old,&f)==GM_STALE);
  CHECK(gm_bind_slot(m,0,&fresh)==GM_DEPENDENCY);
  CHECK(gm_invalidate_slot(m)==GM_OK); CHECK(gm_bind_slot(m,0,&fresh)==GM_OK);
  CHECK(fresh.root_gen!=old.root_gen && fresh.slot_gen!=old.slot_gen);
  CHECK(gm_submit(m,b,fresh,&f)==GM_OK); CHECK(gm_render_complete(m,f)==GM_OK);
}
static void translation_domain(void) {
  gm_pte4 entries[4]; uint64_t pa=0; unsigned write=0;
  ptes(entries,PA_P); CHECK(gm_coarsen(entries,&pa,&write)==GM_OK && pa==PA_P && write==1);
  for(unsigned i=0;i<4;++i) entries[i].writable=0;
  CHECK(gm_coarsen(entries,&pa,&write)==GM_OK && write==0);
  /* All nontrivial4K validity/protection partitions are not one16K PTE. */
  for(unsigned mask=1;mask<15;++mask) {
    ptes(entries,PA_P);
    for(unsigned i=0;i<4;++i) entries[i].valid=(mask>>i)&1;
    CHECK(gm_coarsen(entries,&pa,&write)==GM_UNREPRESENTABLE);
    ptes(entries,PA_P);
    for(unsigned i=0;i<4;++i) entries[i].writable=(mask>>i)&1;
    CHECK(gm_coarsen(entries,&pa,&write)==GM_UNREPRESENTABLE);
  }
  ptes(entries,PA_P); entries[1].pa=0x21001000;
  CHECK(gm_coarsen(entries,&pa,&write)==GM_UNREPRESENTABLE);
  ptes(entries,PA_P+0x1000);
  CHECK(gm_coarsen(entries,&pa,&write)==GM_UNREPRESENTABLE);
  reset(); gm_binding b=ready(0,PA_P); gm_fence f; uint64_t before;
  CHECK(gm_translate(m,0,VA,&before)==GM_OK && before==PA_P);
  CHECK(gm_translate(m,0,VA+0x1000,&pa)==GM_OK && pa==PA_P+0x1000);
  CHECK(pa!=0x21001000ULL); /* Actual shared UAT walk cannot supply scatter target. */
  CHECK(gm_map(m,b,entries,&f)!=GM_OK);
  CHECK(gm_translate(m,0,VA,&pa)==GM_OK && pa==before);
  /* Counterexample is independent of translator: no single aligned base can
   * simultaneously equal PA_P at offset0 and0x21001000 at offset4096. */
  CHECK((PA_P+0x1000ULL)!=0x21001000ULL);
  reset(); CHECK(gm_bootstrap(m,0)==GM_OK); CHECK(gm_reserve(m,0,VA,&b)==GM_OK);
  ptes(entries,PA_P); entries[1].pa=0x21001000ULL;
  CHECK(gm_map(m,b,entries,&f)==GM_UNREPRESENTABLE);
  CHECK(!m->process[0].mapped && m->process[0].paging_issued==0);
  CHECK(gm_translate(m,0,VA,&pa)!=GM_OK);
  memset(entries,0,sizeof(entries));
  CHECK(gm_coarsen(entries,&pa,&write)==GM_UNMAP && pa==0);
}
static void readonly_projection(void) {
  reset(); CHECK(gm_bootstrap(m,0)==GM_OK);
  gm_binding b; gm_fence f; gm_pte4 entries[4];
  CHECK(gm_reserve(m,0,VA,&b)==GM_OK); ptes(entries,PA_P);
  for(unsigned i=0;i<4;++i) entries[i].writable=0;
  CHECK(gm_map(m,b,entries,&f)==GM_OK);
  unsigned long long pa,descriptor;
  CHECK(AppleAgxUatResolvePage(1,&m->process[0].roots,VA,&m->process[0].inventory,
                              &pa,&descriptor)==AppleAgxUatResultOk);
  CHECK((descriptor&(1ULL<<54))==0); /* Pinned shared readonly encoding, not hardware test. */
  CHECK(gm_flush(m,0)==GM_OK); CHECK(gm_paging_complete(m,f)==GM_OK);
  CHECK(gm_evict(m,b,&f)==GM_OK); CHECK(gm_flush(m,0)==GM_OK);
  CHECK(gm_paging_complete(m,f)==GM_OK); ptes(entries,PA_P);
  CHECK(gm_map(m,b,entries,&f)==GM_OK);
  CHECK(AppleAgxUatResolvePage(1,&m->process[0].roots,VA,&m->process[0].inventory,
                              &pa,&descriptor)==AppleAgxUatResultOk);
  CHECK((descriptor&(1ULL<<54))!=0);
}
static void partial_logical_updates(void) {
  gm_pte4 before[4], after[4], replacement[4]; ptes(before,PA_P);
  memcpy(after,before,sizeof(after));
  CHECK(gm_merge4(before,1,1,&before[1],after)==GM_OK);
  CHECK(memcmp(before,after,sizeof(before))==0);
  ptes(replacement,PA_Q);
  CHECK(gm_merge4(before,1,1,&replacement[1],after)==GM_UNREPRESENTABLE);
  CHECK(memcmp(before,after,sizeof(before))==0);
  CHECK(gm_merge4(before,0,4,replacement,after)==GM_OK);
  CHECK(after[0].pa==PA_Q && after[3].pa==PA_Q+0x3000);
  memset(replacement,0,sizeof(replacement));
  CHECK(gm_merge4(before,0,4,replacement,after)==GM_UNMAP);
  CHECK(!after[0].valid && !after[3].valid);
  CHECK(gm_merge4(before,4,1,replacement,after)==GM_RANGE);
}
static void segment_geometry_and_atomic_boundaries(void) {
  for(unsigned profile=0;profile<2;++profile) {
    reset();
    uint64_t size=profile?GM_SEGMENT_16K:GM_SEGMENT_64K;
    CHECK(gm_choose_caps(m,size)==GM_OK);
    CHECK(m->caps.segment_page==size);
    gm_level_desc desc[3]; CHECK(gm_table_levels(m,desc)==GM_OK);
    CHECK(desc[0].index_bits==3 && desc[1].index_bits==11 && desc[2].index_bits==13);
    for(unsigned l=0;l<3;++l) {
      CHECK(desc[l].segment_id==GM_LOCAL_SEGMENT_ID);
      CHECK(desc[l].paging_segment_id==GM_LOCAL_SEGMENT_ID);
      CHECK(desc[l].table_bytes==GM_PAGE && desc[l].alignment==GM_PAGE);
    }
    unsigned leaf_count=(unsigned)(size/GM_PAGE);
    for(unsigned leaf=0;leaf<leaf_count;++leaf) {
      gm_pte4 entries[4]; uint64_t pa; unsigned write;
      CHECK(gm_segment_ptes(m,PA_P,leaf*GM_PAGE,1,entries)==GM_OK);
      CHECK(gm_coarsen(entries,&pa,&write)==GM_OK);
      CHECK(pa==PA_P+leaf*GM_PAGE && write==1);
      for(unsigned logical=0;logical<4;++logical)
        CHECK(entries[logical].pa==pa+logical*GM_LOGICAL_PAGE);
    }
    gm_pte4 invalid[4];
    CHECK(gm_segment_ptes(m,PA_P,leaf_count*GM_PAGE,1,invalid)==GM_RANGE);
    CHECK(gm_segment_ptes(m,PA_P+GM_LOGICAL_PAGE,0,1,invalid)==GM_RANGE);
    CHECK(gm_segment_ptes(m,PA_P,GM_LOGICAL_PAGE,1,invalid)==GM_RANGE);
    CHECK(gm_segment_ptes(m,PA_P,0,2,invalid)==GM_RANGE);
    /* The owner may register another segment page, but one generated leaf
     * cannot source a 4 KiB PFN from it. */
    gm_pte4 actual[4]; uint64_t pa; unsigned write;
    CHECK(gm_segment_ptes(m,PA_P,0,1,actual)==GM_OK);
    actual[1].pa=PA_Q+GM_LOGICAL_PAGE;
    CHECK(gm_coarsen(actual,&pa,&write)==GM_UNREPRESENTABLE);
  }
  reset();
  gm_pte4 before[8], after[8], updates[8];
  ptes(before,PA_P); ptes(before+4,PA_P+GM_PAGE);
  memcpy(after,before,sizeof(before));
  /* StartIndex 3, count 2 crosses a native 16 KiB boundary. A no-op partial
   * update is representable and preserves both leaves. */
  updates[0]=before[3]; updates[1]=before[4];
  CHECK(gm_update_span(before,3,2,updates,after)==GM_OK);
  CHECK(memcmp(before,after,sizeof(before))==0);
  updates[0].pa=PA_Q+3*GM_LOGICAL_PAGE;
  CHECK(gm_update_span(before,3,2,updates,after)==GM_UNREPRESENTABLE);
  CHECK(memcmp(before,after,sizeof(before))==0);
  ptes(updates,PA_Q); ptes(updates+4,PA_Q+GM_PAGE);
  CHECK(gm_update_span(before,0,8,updates,after)==GM_OK);
  CHECK(after[0].pa==PA_Q && after[7].pa==PA_Q+7*GM_LOGICAL_PAGE);
  CHECK(gm_choose_caps(m,GM_LOGICAL_PAGE)==GM_RANGE);
}
int main(void) {
  m=calloc(1,sizeof(*m)); assert(m);
  isolation_root_and_private(); physical_backing_ownership(); lifetime_and_fence_domains();
  bootstrap_without_render(); relocated_root_rejects_actual_old_lease();
  translation_domain(); partial_logical_updates(); readonly_projection();
  segment_geometry_and_atomic_boundaries();
  printf("{\"harness\":\"PASS\",\"checks\":%u,\"shared_uat\":true,"
         "\"representable_16k_groups\":\"PASS\",\"arbitrary_4k_groups\":\"COUNTEREXAMPLE\","
         "\"selected_segment_scatter\":\"UNREACHABLE_IN_GENERATOR\","
         "\"vidmm_segment_granularity_assumed\":true,"
         "\"wddm_input_domain_proven\":false,"
         "\"hardware\":false}\n",checks);
  free(m); return 0;
}
