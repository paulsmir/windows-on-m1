/* EXP852 dump PTE group plus its saved firmware exclusion. No access to Air. */
static void r133_publication_cases(void) {
  ADMISSION_CONTEXT a={0}; ADMISSION_G3_STATE state={0}; REPLAY_BROKER b={0};
  APPLE_AGX_GPUVA_V5_IO io={&b,ReplayWrite64,ReplayRead64,ReplayWrite32,ReplayBarrier, 0};
  local_ipa=0x8e0000000ULL; replay_identity_ram=true;
  assert(posix_memalign((void **)&local_cpu,0x4000,(size_t)local_bytes)==0);
  memset(local_cpu,0,(size_t)local_bytes);ReplayBrokerInit(&b);
  /* EXP852 contract.bin regions: guest RAM and protected firmware. */
  b.memory.boot.ram_base=0x850000000ULL;b.memory.boot.ram_size=0x18f708000ULL;
  b.memory.regions[0].base=0x850000000ULL;b.memory.regions[0].size=0x18f708000ULL;
  b.memory.regions[1].kind=HV_CONTRACT_REGION_FIRMWARE;
  b.memory.regions[1].base=0x8510b4000ULL;b.memory.regions[1].size=0x1d88000ULL;
  InitializeListHead(&state.Processes);assert(AppleAgxGpuvaV5ClientInit(&state.Client,&io));
  {AGX_GPUVA_V5_REQUEST r={0};AGX_GPUVA_V5_RESPONSE reply={0};r.Command=AGX_GPUVA_V5_CREATE;
   assert(AppleAgxGpuvaV5ClientCall(&state.Client,&r,&reply));assert(reply.Epoch==7);}
  a.Started=TRUE;a.GpuvaG3State=&state;state.Adapter=&a;
  /* Publication needs a GPU-visible process; the paging process is CPU-only. */
  ADMISSION_G3_PROCESS *p=sys_process(&a,0x4000,0);
  DXGK_PTE ptes[1024]={0};
  for(UINT i=0;i<1024;++i) {
    ptes[i].Flags=1;
    /* Captured last preceding group: 0x8ef41c..1f. Earlier PFNs modelled. */
    ptes[i].PageAddress=i<512 ? 0x8ef220+i : 0x851420+i-512;
  }
  assert(ReplayTranslate(&b,0x8ef41c000ULL)==0x8ef41c000ULL);
  assert(ReplayTranslate(&b,0x851420000ULL)==0);
  DXGKARG_BUILDPAGINGBUFFER x={0};x.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;
  x.UpdatePageTable.hProcess=p;x.UpdatePageTable.hAllocation=(HANDLE)1;
  x.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentId=2;
  x.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentOffset=0xc000;
  x.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_GPU_PHYSICAL;
  x.UpdatePageTable.StartIndex=0x430;x.UpdatePageTable.NumPageTableEntries=0x400;
  x.UpdatePageTable.FirstPteVirtualAddress=0x2430000;x.UpdatePageTable.pPageTableEntries=ptes;
  NTSTATUS status=AdmissionGpuvaG3BuildPagingBuffer(&a,&x);
  if(!NT_SUCCESS(status)) fprintf(stderr,"EXP852 RED status=%08x branch=%u index=%x last=%u uncertain=%u\n",
      (UINT)status,last_paging_failure.Branch,last_paging_failure.Index,
      last_paging_failure.GraphLastStatus,last_paging_failure.GraphUncertain);
  expect_ok("EXP852 protected group must remain unpublished",status);
  assert(!p->Graph.Uncertain && !p->Poisoned);
  assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x62c000,0x4000));
  assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x630000,0x4000));
  ADMISSION_G3_TABLE_SHADOW *s=p->TableShadows;
  while(s->OriginalIpa!=local_ipa+0xc000) s=s->Next;
  assert(s->LogicalPtes[0x630].GuestIpa==0x851420000ULL);
  assert(s->ResidentPtes[0x633].GuestIpa==0x851423000ULL);
  assert(sys_frame(&state,0x851420000ULL)->Mappings==4);
  assert(sys_frame(&state,0x851420000ULL)->Grants==0);
  assert(((ULONGLONG *)s->Memory.CpuAddress)[0x18c]==0);
  assert(state.UnpublishedGroups[0]==128);
  /* A replacement that loses grant eligibility must remove the old leaf,
   * not leave a stale mapping. Eligibility is tested via the real broker. */
  x.UpdatePageTable.StartIndex=0x630;x.UpdatePageTable.NumPageTableEntries=4;
  x.UpdatePageTable.FirstPteVirtualAddress=0x2630000;
  x.UpdatePageTable.pPageTableEntries=ptes;
  expect_ok("R133 replacement grant",AdmissionGpuvaG3BuildPagingBuffer(&a,&x));
  assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x630000,0x4000));
  x.UpdatePageTable.pPageTableEntries=ptes+512;
  expect_ok("R133 protected replacement",AdmissionGpuvaG3BuildPagingBuffer(&a,&x));
  assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x630000,1));
  assert(((ULONGLONG *)s->Memory.CpuAddress)[0x18c]==0);
  assert(!sys_frame(&state,0x851420000ULL)->Grants);
  /* A stale process identity is an inconsistency, never optional backing. */
  p->Graph.ProcessGeneration++;
  x.UpdatePageTable.pPageTableEntries=ptes+4;
  assert(!NT_SUCCESS(AdmissionGpuvaG3BuildPagingBuffer(&a,&x)));
  assert(last_paging_failure.GraphLastStatus==HV_AGX_GPUVA_V5_STALE);
  assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x630000,1));
  p->Graph.ProcessGeneration--;
  /* A corrupt existing native descriptor refuses UPDATE_LEAF after a new
   * grant. Successful compensating REVOKE must retain that first error. */
  DXGK_PTE fresh[4]={0};
  for(UINT i=0;i<4;++i){fresh[i].Flags=1;fresh[i].PageAddress=0x8f4200+i;}
  x.UpdatePageTable.StartIndex=0x900;x.UpdatePageTable.FirstPteVirtualAddress=0x2900000;
  x.UpdatePageTable.pPageTableEntries=fresh;
  ((ULONGLONG *)s->Memory.CpuAddress)[0x240]=1;
  assert(!NT_SUCCESS(AdmissionGpuvaG3BuildPagingBuffer(&a,&x)));
  fprintf(stderr,"R133 first broker error=%u (want OWNERSHIP=4)\n",last_paging_failure.GraphLastStatus);
  assert(last_paging_failure.GraphLastStatus==HV_AGX_GPUVA_V5_OWNERSHIP);
  assert(!sys_frame(&state,0x8f4200000ULL));
  assert(((ULONGLONG *)s->Memory.CpuAddress)[0x240]==1);
  ((ULONGLONG *)s->Memory.CpuAddress)[0x240]=0; /* repair test fault only */
  expect_ok("R133 cleanup",AdmissionDdiDestroyProcess(&a,p));
  assert(!state.Registry.Frames);free(local_cpu);local_cpu=NULL;
  puts("R133 protected publication: PASS");
}
