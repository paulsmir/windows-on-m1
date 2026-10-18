{
/* No fixture GraphBindRoot and no SetRoot: real paging must select the root.
 * The old paging body fails at this first QUERY with predicate53. */
  assert(!p->SetRootCount && !c.GpuvaG3SetRootCount && !c.GpuvaG3RootIpa);
  expect_ok("R147 paging before SetRoot QUERY",AdmissionDdiEscape(&a,&escape));
  ULONGLONG root_before=p->Graph.RootIpa, generation=q->MappingGeneration;
  assert(root_before!=p->BootstrapIpa);
  q->Operation=APPLE_AGX_G3_COPY_UPLOAD;q->TransferBytes=1;q->Data[0]=0x73;
  expect_ok("R147 upload before SetRoot",AdmissionDdiEscape(&a,&escape));
  assert(local_cpu[0x100000]==0x73);
  q->Operation=APPLE_AGX_G3_COPY_QUERY;q->TransferBytes=0;
  q->MappingGeneration=q->ProcessGeneration=0;
  /* Invalid new-root contents cannot select that root. */
  DXGK_PTE rp={0};rp.Flags=0x41;rp.PageTableAddress=0x14;rp.Reserved=1;
  assert(!NT_SUCCESS(sys_update(&a,p,local_cpu+0x20000,2,0,1,&rp,0,0)));
  assert(p->Graph.RootIpa==root_before);
  rp.Reserved=0;
  expect_ok("R147 moved root paging",sys_update(&a,p,local_cpu+0x20000,2,0,1,&rp,0,0));
  ULONGLONG moved=p->Graph.RootIpa;
  assert(moved!=root_before && p->Graph.MappingGeneration>generation);
  expect_ok("R147 moved root QUERY",AdmissionDdiEscape(&a,&escape));
  q->Operation=APPLE_AGX_G3_COPY_UPLOAD;q->TransferBytes=1;
  q->MappingGeneration=generation;
  /* R159: the range is re-resolved through the current (moved) root. */
  expect_ok("R159 upload after root move",AdmissionDdiEscape(&a,&escape));
  q->Operation=APPLE_AGX_G3_COPY_QUERY;q->TransferBytes=0;
  q->MappingGeneration=q->ProcessGeneration=0;
  /* Eviction of the old root must not select it again. */
  DXGKARG_BUILDPAGINGBUFFER ev={0};DXGK_PTE zero={0};
  ev.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;ev.UpdatePageTable.hProcess=p;
  ev.UpdatePageTable.PageTableAddress.CpuVirtual=local_cpu+0x10000;
  ev.UpdatePageTable.PageTableLevel=2;ev.UpdatePageTable.NumPageTableEntries=1;
  ev.UpdatePageTable.pPageTableEntries=&zero;ev.UpdatePageTable.Flags.NotifyEviction=1;
  expect_ok("R147 old root eviction",AdmissionGpuvaG3BuildPagingBuffer(&a,&ev));
  assert(p->Graph.RootIpa==moved);
  expect_ok("R147 QUERY after old eviction",AdmissionDdiEscape(&a,&escape));
  /* The later OS notification agrees and records the context normally. */
  DXGKARG_SETROOTPAGETABLE r={0};r.hContext=&c;r.NumEntries=8;
  r.Address.SegmentId=ADMISSION_MEMORY_LOCAL_SEGMENT;r.Address.SegmentOffset=0x20000;
  generation=p->Graph.MappingGeneration;AdmissionDdiSetRootPageTable(&a,&r);
  assert(!p->Poisoned && !c.GpuvaG3Poisoned && c.GpuvaG3RootIpa==moved);
  assert(p->SetRootCount==1 && c.GpuvaG3SetRootCount==1);
  assert(p->Graph.MappingGeneration==generation);
  /* SetRoot may also move to another populated root: restore the original. */
  expect_ok("R147 original populated",sys_update(&a,p,local_cpu+0x10000,2,0,1,&rp,0,0));
  r.Address.SegmentOffset=0x20000;AdmissionDdiSetRootPageTable(&a,&r);
  assert(p->Graph.RootIpa==moved && c.GpuvaG3RootIpa==moved);
  q->MappingGeneration=q->ProcessGeneration=0;
  expect_ok("R147 SetRoot relocation QUERY",AdmissionDdiEscape(&a,&escape));
  q->MappingGeneration=q->ProcessGeneration=0;
  /* Continue the entire existing R145 authorization/provenance/copy suite. */
  local_cpu[0x100000]=0;
  query_registry_writes=query_registry_flushes=0;
  puts("R147 no-SetRoot, paging relocation, old eviction, SetRoot relocation: PASS");
}
