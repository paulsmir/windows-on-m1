/* Inserted into real R145 setup after local publication, before any QUERY.
 * Catch missing v2 snapshot, root selection hidden by fixtures, tail loss,
 * overwritten first evidence, and per-process/context count confusion. */
  /* Diagnostic counterexample: explicitly park on bootstrap. Production
   * paging now selects its root; the R147 replay tests that real ordering. */
  assert(AppleAgxGpuvaG3GraphBindRoot(&p->Graph,p->BootstrapIpa));
  assert(p->Graph.RootIpa==p->BootstrapIpa);
  replay_query_claim_watch=&a.G3CopyQueryFailureClaim;
  UINT pools_before=replay_pool_calls;
  assert(AdmissionDdiEscape(&a,&escape)==STATUS_INVALID_PARAMETER);
  assert(replay_pool_calls==pools_before+1 && replay_query_claim_irql==1);
  assert(query_registry_receipt[0]==2 && query_registry_receipt[1]==144);
#define SAVE_QUERY(name) do { \
    char path[1024]; \
    assert(snprintf(path,sizeof(path),"%s/%s.bin",getenv("G3_QUERY_OUTPUT"),name)>0); \
    FILE *out=fopen(path,"wb");assert(out); \
    assert(fwrite(query_registry_receipt,1,144,out)==144);assert(!fclose(out)); \
  } while(0)
  assert(query_registry_receipt[2]==53 && query_registry_receipt[3]==0xc000000d);
  assert((query_registry_receipt[4]&33)==33);
  assert(query_registry_receipt[6]==0 && query_registry_receipt[8]==1);
  SAVE_QUERY("bootstrap-root");
  ULONG first_receipt[36];memcpy(first_receipt,query_registry_receipt,sizeof(first_receipt));
  DXGKARG_SETROOTPAGETABLE r={0};r.hContext=&c;r.NumEntries=8;
  r.Address.SegmentId=ADMISSION_MEMORY_LOCAL_SEGMENT;r.Address.SegmentOffset=0x10000;
  AdmissionDdiSetRootPageTable(&a,&r);
  assert(!c.GpuvaG3Poisoned && c.GpuvaG3RootIpa==p->Graph.RootIpa);
  expect_ok("v2 root selected QUERY",AdmissionDdiEscape(&a,&escape));
  q->MappingGeneration=q->ProcessGeneration=0;q->GpuVa=0x20000;
  assert(AdmissionDdiEscape(&a,&escape)==STATUS_INVALID_PARAMETER);
  assert(query_registry_writes==1 && !memcmp(first_receipt,query_registry_receipt,sizeof(first_receipt)));
  a.G3CopyQueryFailureClaim=0;
  assert(AdmissionDdiEscape(&a,&escape)==STATUS_INVALID_PARAMETER);
  assert(!(query_registry_receipt[4]&32));
  assert(query_registry_receipt[6]==2 && query_registry_receipt[7]==8);
  assert(query_registry_receipt[8]==3 && query_registry_receipt[9]==3);
  assert(query_registry_receipt[10]==1 && query_registry_receipt[11]==1);
  SAVE_QUERY("absent-va");
  ADMISSION_RENDER_CONTEXT c2={0};c2.Object=c.Object;c2.GpuvaG3Process=p;
  r.hContext=&c2;AdmissionDdiSetRootPageTable(&a,&r);
  a.G3CopyQueryFailureClaim=0;q->GpuVa=0x10000;
  allocation.Object.Description.Size=0x10010;
  assert(AdmissionDdiEscape(&a,&escape)==STATUS_INVALID_PARAMETER);
  assert(query_registry_receipt[8]==5 && query_registry_receipt[9]==3);
  assert(query_registry_receipt[10]==2 && query_registry_receipt[11]==1);
  SAVE_QUERY("unpublished-tail");
  allocation.Object.Description.Size=0x10000;
  /* Preserve the exact component when the root's middle link is absent. */
  APPLE_AGX_GPUVA_G3_NODE *edge=p->Graph.Parents;
  while(edge && edge->Ipa!=p->Graph.RootIpa) edge=edge->Next;
  assert(edge);ULONGLONG middle=edge->AuxIpa;
  edge=p->Graph.Parents;
  while(edge && (edge->Ipa!=middle || edge->Index!=0)) edge=edge->Next;
  assert(edge);ULONGLONG leaf_table=edge->AuxIpa;
  assert(AppleAgxGpuvaG3GraphUpdateParent(&p->Graph,middle,0,0));
  a.G3CopyQueryFailureClaim=0;
  assert(AdmissionDdiEscape(&a,&escape)==STATUS_INVALID_PARAMETER);
  assert(query_registry_receipt[6]==1 && query_registry_receipt[8]==2);
  assert(AppleAgxGpuvaG3GraphUpdateParent(&p->Graph,middle,0,leaf_table));
  /* Real partial 4K unmap leaves three valid logical pages but no native
   * leaf. This must differ from an entirely absent logical VA. */
  DXGKARG_BUILDPAGINGBUFFER partial=x;DXGK_PTE invalid_pte={0};
  partial.UpdatePageTable.StartIndex=16;
  partial.UpdatePageTable.NumPageTableEntries=1;
  partial.UpdatePageTable.Flags.Use64KBPages=0;
  partial.UpdatePageTable.pPageTableEntries=&invalid_pte;
  expect_ok("v2 partial group",AdmissionGpuvaG3BuildPagingBuffer(&a,&partial));
  a.G3CopyQueryFailureClaim=0;
  assert(AdmissionDdiEscape(&a,&escape)==STATUS_INVALID_PARAMETER);
  assert(query_registry_receipt[6]==2 && query_registry_receipt[8]==4);
  SAVE_QUERY("leaf-not-published");
  expect_ok("v2 restore group",AdmissionGpuvaG3BuildPagingBuffer(&a,&x));
  /* Counts saturate; they never wrap into a misleading never-seen state. */
  p->SetRootCount=c2.GpuvaG3SetRootCount=MAXULONG;
  AdmissionDdiSetRootPageTable(&a,&r);
  assert(p->SetRootCount==MAXULONG && c2.GpuvaG3SetRootCount==MAXULONG);
  a.G3CopyQueryFailureClaim=0;a.G3CopyQueryFailurePredicate=0;
  replay_query_claim_watch=NULL;
  query_registry_writes=query_registry_flushes=0;
#undef SAVE_QUERY
