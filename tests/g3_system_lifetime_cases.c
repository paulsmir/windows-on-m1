/* Real KMD DDI + graph + wire + broker; only OS memory/CPU and stage-2 I/O
 * are host models. Expectations are independent descriptor/ref-count facts. */
static void *sys_no_nodes(void *p,unsigned long long n) {(void)p;(void)n;return NULL;}
static NTSTATUS sys_update(ADMISSION_CONTEXT *a, ADMISSION_G3_PROCESS *p,
    void *table, UINT level, UINT start, UINT count, DXGK_PTE *ptes,
    UINT wide, UINT repeat) {
  DXGKARG_BUILDPAGINGBUFFER x={0};
  x.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;
  x.UpdatePageTable.hProcess=p;
  x.UpdatePageTable.PageTableAddress.CpuVirtual=table;
  x.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
  x.UpdatePageTable.PageTableLevel=level;
  x.UpdatePageTable.StartIndex=start;
  x.UpdatePageTable.NumPageTableEntries=count;
  x.UpdatePageTable.FirstPteVirtualAddress=(ULONGLONG)start*(wide?0x10000:0x1000);
  x.UpdatePageTable.pPageTableEntries=ptes;
  x.UpdatePageTable.Flags.Use64KBPages=wide;
  x.UpdatePageTable.Flags.Repeat=repeat;
  return AdmissionGpuvaG3BuildPagingBuffer(a,&x);
}
static ADMISSION_G3_PROCESS *sys_process(ADMISSION_CONTEXT *a, UINT offset,
                                        UINT paging) {
  DXGKARG_CREATEPROCESS create={0}; DXGK_PTE pte={0};
  a->Interface=(DXGKRNL_INTERFACE){(HANDLE)0x1234,ReplayReserveVa};
  create.Flags.SystemProcess=paging;
  expect_ok("R132 create",AdmissionDdiCreateProcess(a,&create));
  ADMISSION_G3_PROCESS *p=create.hKmdProcess;
  expect_ok("R132 empty leaf",sys_update(a,p,local_cpu+offset+0x8000,0,0,1,&pte,0,0));
  pte.Flags=0x41; pte.PageTableAddress=(offset+0x8000)>>12;
  expect_ok("R132 middle",sys_update(a,p,local_cpu+offset+0x4000,1,0,1,&pte,0,0));
  pte.PageTableAddress=(offset+0x4000)>>12;
  expect_ok("R132 root",sys_update(a,p,local_cpu+offset,2,0,1,&pte,0,0));
  ADMISSION_G3_TABLE_SHADOW *s=p->TableShadows;
  while(s->OriginalIpa!=local_ipa+offset) s=s->Next;
  assert(AppleAgxGpuvaG3GraphBindRoot(&p->Graph,s->BrokerIpa));
  return p;
}
static APPLE_AGX_GPUVA_G3_FRAME *sys_frame(ADMISSION_G3_STATE *s,ULONGLONG ipa) {
  APPLE_AGX_GPUVA_G3_FRAME *f=s->Registry.Frames;
  while(f && f->Ipa!=ipa) f=f->Next;
  return f;
}
static void r134_system_64k_cases(void) {
  ADMISSION_CONTEXT a={0}; ADMISSION_G3_STATE state={0}; REPLAY_BROKER b={0};
  APPLE_AGX_GPUVA_V5_IO io={&b,ReplayWrite64,ReplayRead64,ReplayWrite32,ReplayBarrier, 0};
  DXGK_PTE pte={0}, four[4]={0}, zero={0};
  assert(ADMISSION_GPUVA_G1B_PAGE_PROFILE==16);
  assert(posix_memalign((void **)&local_cpu,0x4000,(size_t)local_bytes)==0);
  memset(local_cpu,0,(size_t)local_bytes);
  ReplayBrokerInit(&b); InitializeListHead(&state.Processes);
  assert(AppleAgxGpuvaV5ClientInit(&state.Client,&io));
  { AGX_GPUVA_V5_REQUEST r={0}; AGX_GPUVA_V5_RESPONSE reply={0};
    r.Command=AGX_GPUVA_V5_CREATE;
    assert(AppleAgxGpuvaV5ClientCall(&state.Client,&r,&reply)); }
  a.Started=TRUE;a.GpuvaG3State=&state;state.Adapter=&a;
  ADMISSION_G3_PROCESS *p=sys_process(&a,0x10000,0);
  void *leaf=local_cpu+0x18000;
  pte.Flags=1; pte.PageAddress=system_ipa>>12;
  expect_ok("R134 system 64K",sys_update(&a,p,leaf,0,1,1,&pte,1,0));
  for(UINT i=0;i<4;++i) {
    ULONGLONG va=0x10000ULL+(ULONGLONG)i*0x4000ULL;
    assert(AppleAgxGpuvaG3GraphContainsRangeAccess(&p->Graph,va,0x4000,true));
    assert(sys_frame(&state,system_ipa+(ULONGLONG)i*0x4000ULL));
  }
  /* A malformed backing base stays logical but never becomes a GPU leaf. */
  pte.PageAddress=(system_ipa+0x1000ULL)>>12;
  expect_ok("R134 misaligned system base",sys_update(&a,p,leaf,0,1,1,&pte,1,0));
  assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x10000,0x10000));
  pte.PageAddress=system_ipa>>12;
  b.bad_subpage=system_ipa+0x4000ULL;
  expect_ok("R134 protected system frame",sys_update(&a,p,leaf,0,1,1,&pte,1,0));
  assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x14000,0x4000));
  b.bad_subpage=0;
  for(UINT i=0;i<4;++i) {four[i].Flags=1;four[i].PageAddress=(system_ipa>>12)+i;}
  four[2].ReadOnly=1;
  expect_ok("R134 mixed 4K rights",sys_update(&a,p,leaf,0,16,4,four,0,0));
  assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x10000,0x4000));
  four[2].ReadOnly=0;
  expect_ok("R134 4K after 64K",sys_update(&a,p,leaf,0,16,4,four,0,0));
  assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x10000,0x4000));
  expect_ok("R134 64K after 4K",sys_update(&a,p,leaf,0,1,1,&pte,1,0));
  assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x10000,0x10000));
  expect_ok("R134 unmap",sys_update(&a,p,leaf,0,1,1,&zero,1,0));
  assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x10000,0x10000));
  expect_ok("R134 destroy",AdmissionDdiDestroyProcess(&a,p));
  free(local_cpu);local_cpu=NULL;
  puts("R134 system 64K outer DDI: PASS");
}
static void system_lifetime_cases(void) {
  ADMISSION_CONTEXT a={0}; ADMISSION_G3_STATE state={0}; REPLAY_BROKER b={0};
  APPLE_AGX_GPUVA_V5_IO io={&b,ReplayWrite64,ReplayRead64,ReplayWrite32,ReplayBarrier, 0};
  DXGK_PTE pte[32]={0},zero={0};
  assert(posix_memalign((void **)&local_cpu,0x4000,(size_t)local_bytes)==0);
  memset(local_cpu,0,(size_t)local_bytes);
  ReplayBrokerInit(&b); InitializeListHead(&state.Processes);
  assert(AppleAgxGpuvaV5ClientInit(&state.Client,&io));
  { AGX_GPUVA_V5_REQUEST r={0}; AGX_GPUVA_V5_RESPONSE reply={0};
    r.Command=AGX_GPUVA_V5_CREATE;
    assert(AppleAgxGpuvaV5ClientCall(&state.Client,&r,&reply));
    assert(reply.Epoch==7); }
  a.Started=TRUE;a.GpuvaG3State=&state;state.Adapter=&a;
  ADMISSION_G3_PROCESS *p=sys_process(&a,0x10000,0);
  ADMISSION_G3_PROCESS *q=sys_process(&a,0x20000,0);
  ADMISSION_G3_PROCESS *paging=sys_process(&a,0x30000,1);
  void *leaf=local_cpu+0x18000;
  for(UINT i=0;i<32;++i) {pte[i].Flags=1;pte[i].PageAddress=(system_ipa>>12)+i;}
  /* Every partial-arrival permutation. Incomplete groups hold residency but
   * never publish a leaf. Null hAllocation is the normal fixture. */
  for(UINT i=0;i<4;++i) for(UINT j=0;j<4;++j) if(j!=i)
    for(UINT k=0;k<4;++k) if(k!=i && k!=j) {
      UINT order[4]={i,j,k,6-i-j-k};
      for(UINT n=0;n<4;++n) {
        UINT x=order[n];
        expect_ok("R132 partial",sys_update(&a,p,leaf,0,4+x,1,&pte[x],0,0));
        assert(AppleAgxGpuvaG3GraphContainsRangeAccess(&p->Graph,0x4000,0x4000,true)==(n==3));
      }
      assert(sys_frame(&state,system_ipa)->Mappings==4);
      assert(sys_frame(&state,system_ipa)->Grants==1);
      expect_ok("R132 clear",sys_update(&a,p,leaf,0,4,4,&zero,0,1));
      assert(!sys_frame(&state,system_ipa));
    }
  expect_ok("R132 map",sys_update(&a,p,leaf,0,4,4,pte,0,0));
  ULONGLONG generation=sys_frame(&state,system_ipa)->Generation;
  { APPLE_AGX_GPUVA_G3_NODE *native=p->Graph.Leaves;assert(native);
    UINT t=0;for(;t<HV_AGX_GPUVA_V5_TABLES;++t)
      if(gpuva_v5.tables[t].live && gpuva_v5.tables[t].ipa==native->Ipa) break;
    assert(t<HV_AGX_GPUVA_V5_TABLES);
    assert((gpuva_v5.tables[t].entries[native->Index]&0xffffffc000ULL)==system_ipa+0x10000000ULL);
  }
  expect_ok("R132 same-owner alias",sys_update(&a,p,leaf,0,12,4,pte,0,0));
  expect_ok("R132 second-root",sys_update(&a,q,local_cpu+0x28000,0,4,4,pte,0,0));
  expect_ok("R132 paging alias",sys_update(&a,paging,local_cpu+0x38000,0,4,4,pte,0,0));
  assert(sys_frame(&state,system_ipa)->Mappings==16 && sys_frame(&state,system_ipa)->Grants==3);
  assert(sys_frame(&state,system_ipa)->Generation==generation);
  assert(AppleAgxGpuvaG3GraphBeginJob(&p->Graph,1));
  assert(sys_update(&a,p,leaf,0,4,4,&zero,0,1)==STATUS_DEVICE_BUSY);
  assert(AdmissionDdiDestroyProcess(&a,p)==STATUS_DEVICE_BUSY);
  assert(sys_frame(&state,system_ipa)->Mappings==16);
  assert(AppleAgxGpuvaG3GraphEndJob(&p->Graph));
  expect_ok("R132 reverse destroy paging",AdmissionDdiDestroyProcess(&a,paging));
  expect_ok("R132 reverse destroy q",AdmissionDdiDestroyProcess(&a,q));
  assert(sys_frame(&state,system_ipa)->Mappings==8 && sys_frame(&state,system_ipa)->Grants==1);
  expect_ok("R132 invalidate one alias",sys_update(&a,p,leaf,0,5,1,&zero,0,0));
  assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x4000,1));
  assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0xc000,1));
  expect_ok("R132 clear both",sys_update(&a,p,leaf,0,4,12,&zero,0,1));
  assert(!sys_frame(&state,system_ipa));
  expect_ok("R132 PFN reuse",sys_update(&a,p,leaf,0,4,4,pte,0,0));
  assert(sys_frame(&state,system_ipa)->Generation>generation);
  /* Mixed permissions and repeated 4K PFNs cannot become a wider RW leaf. */
  pte[2].ReadOnly=1;
  expect_ok("R132 mixed rights",sys_update(&a,p,leaf,0,4,4,pte,0,0));
  assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x4000,1));
  pte[2].ReadOnly=0;
  expect_ok("R132 Repeat PFN",sys_update(&a,p,leaf,0,4,4,pte,0,1));
  assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x4000,1));
  for(UINT i=0;i<4;++i) pte[i].ReadOnly=1;
  expect_ok("R132 RO",sys_update(&a,p,leaf,0,4,4,pte,0,0));
  assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x4000,0x4000));
  assert(!AppleAgxGpuvaG3GraphContainsRangeAccess(&p->Graph,0x4000,1,true));
  for(UINT i=0;i<4;++i) pte[i].ReadOnly=0;
  /* 64K replacement removes R131's CPU envelope but has 16 resident subpages. */
  expect_ok("R132 64K",sys_update(&a,p,leaf,0,0,1,pte,1,0));
  assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0,0x10000));
  ADMISSION_G3_TABLE_SHADOW *s=p->TableShadows;
  while(s->OriginalIpa!=local_ipa+0x18000) s=s->Next;
  assert(!s->LogicalPtes[4].Flags);
  expect_ok("R132 64K to 4K",sys_update(&a,p,leaf,0,5,1,&zero,0,0));
  assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x4000,1));
  assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x8000,1));
  UINT commands=b.commands;
  pte[0].PageAddress=~0ULL;
  assert(!NT_SUCCESS(sys_update(&a,p,leaf,0,0,1,pte,0,0)));
  assert(b.commands==commands);
  assert(!NT_SUCCESS(sys_update(&a,p,leaf,0,8191,2,pte,0,0)));
  pte[0].PageAddress=system_ipa>>12;
  /* Reject attributes and generation exhaustion before touching the graph. */
  for (UINT bit=1;bit<64;++bit) {
    if(bit==3 || (bit>=5 && bit<=9)) continue;
    pte[0].Flags=1ULL|(1ULL<<bit); commands=b.commands;
    assert(!NT_SUCCESS(sys_update(&a,p,leaf,0,4,1,pte,0,0)));
    assert(b.commands==commands);
  }
  pte[0].Flags=1;
  ULONGLONG saved_generation=p->Graph.MappingGeneration;
  p->Graph.MappingGeneration=~0ULL-1;
  commands=b.commands;
  assert(!NT_SUCCESS(sys_update(&a,p,leaf,0,4,4,pte,0,0)));
  assert(p->Graph.MappingGeneration==~0ULL-1 && b.commands==commands);
  p->Graph.MappingGeneration=saved_generation;
  /* Actual production translator checks every subpage, including endpoints. */
  assert(ReplayTranslate(&b,system_ipa)==system_ipa+0x10000000ULL);
  for(UINT sub=0;sub<4;++sub) {
    b.bad_subpage=system_ipa+sub*0x1000ULL;
    /* This frame already has a grant: changed stage-2 translation during
     * its lifetime is a genuine inconsistency, not an optional grant refusal. */
    assert(!NT_SUCCESS(sys_update(&a,p,leaf,0,32,4,pte,0,0)));
    assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x20000,1));
    b.bad_subpage=0;
  }
  assert(!ReplayTranslate(&b,0x300000000ULL)); /* MMIO/no stage-2 RAM */
  root_base=system_ipa+0x10000000ULL;root_length=0x4000;
  assert(!ReplayTranslate(&b,system_ipa));root_base=root_length=0;
  J313_AUTONOMOUS_LAYOUT.ramdisk_base=system_ipa+0x10000000ULL;
  J313_AUTONOMOUS_LAYOUT.ramdisk_max_size=0x4000;
  assert(!ReplayTranslate(&b,system_ipa));
  J313_AUTONOMOUS_LAYOUT.ramdisk_base=J313_AUTONOMOUS_LAYOUT.ramdisk_max_size=0;
  b.memory.region_count=3;
  b.memory.regions[2].kind=HV_CONTRACT_REGION_DART_TABLES;
  b.memory.regions[2].base=system_ipa+0x10000000ULL;
  b.memory.regions[2].size=0x4000;
  assert(!ReplayTranslate(&b,system_ipa));b.memory.region_count=2;
  /* A table cannot be registered as backing; this reaches real ownership checks. */
  DXGK_PTE conflict[4]={0};
  for(UINT i=0;i<4;++i){conflict[i].Flags=1;conflict[i].PageAddress=(s->BrokerIpa>>12)+i;}
  expect_ok("R133 table backing stays unpublished",sys_update(&a,p,leaf,0,32,4,conflict,0,0));
  assert(sys_frame(&state,s->BrokerIpa)->Mappings==4);
  assert(!sys_frame(&state,s->BrokerIpa)->Grants);
  assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x20000,1));
  expect_ok("R133 ungrantable retirement",sys_update(&a,p,leaf,0,32,4,&zero,0,1));
  assert(!sys_frame(&state,s->BrokerIpa));
  /* Moving a child link within one parent update keeps its live backing. */
  DXGK_PTE moved[2]={0};moved[1].Flags=0x41;moved[1].PageAddress=0x18;
  expect_ok("R132 move parent link",sys_update(&a,p,local_cpu+0x14000,1,0,2,moved,0,0));
  assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x2000000,1));
  moved[0]=moved[1];moved[1]=zero;
  expect_ok("R132 restore parent link",sys_update(&a,p,local_cpu+0x14000,1,0,2,moved,0,0));
  /* Diamond table alias: the first detach preserves the other path; after
   * both detach, stale descendant edges must not retain ownership. */
  expect_ok("R132 diamond middle",sys_update(&a,p,local_cpu+0x1c000,1,0,1,moved,0,0));
  moved[0].PageAddress=0x1c;
  expect_ok("R132 diamond root",sys_update(&a,p,local_cpu+0x10000,2,1,1,moved,0,0));
  expect_ok("R132 first top detach",sys_update(&a,p,local_cpu+0x10000,2,0,1,&zero,0,0));
  assert(state.Registry.Frames);
  assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,1ULL<<36,1));
  DXGK_PTE failed_parent[2]={0};failed_parent[1].Flags=0x41;failed_parent[1].PageAddress=0x10;
  assert(!NT_SUCCESS(sys_update(&a,p,local_cpu+0x14000,1,0,2,failed_parent,0,0)));
  assert(!p->Graph.Uncertain); /* Clean rollback must preserve retired aliases. */
  expect_ok("R132 final top detach",sys_update(&a,p,local_cpu+0x10000,2,1,1,&zero,0,0));
  assert(!state.Registry.Frames);
  assert(!s->ResidentPtes || !s->ResidentPtes[0].Flags);
  /* Table level reuse must discard shadow provenance, including partial PTEs. */
  expect_ok("R132 leaf becomes middle",sys_update(&a,p,leaf,1,0,1,&zero,0,0));
  expect_ok("R132 middle becomes leaf partial",sys_update(&a,p,leaf,0,1,1,&pte[1],0,0));
  assert(!p->Graph.Leaves);
  assert(sys_frame(&state,system_ipa)->Mappings==1);
  expect_ok("R132 clear partial",sys_update(&a,p,leaf,0,1,1,&zero,0,0));
  assert(!state.Registry.Frames);
  /* Explicit detach also retires a populated subtree that was never rooted. */
  DXGK_PTE parent={0};parent.Flags=0x41;parent.PageAddress=0x18;
  expect_ok("R132 unrooted parent",sys_update(&a,p,local_cpu+0x1c000,1,0,1,&parent,0,0));
  expect_ok("R132 unrooted map",sys_update(&a,p,leaf,0,4,4,pte,0,0));
  expect_ok("R132 unrooted middle reuse",sys_update(&a,p,local_cpu+0x1c000,0,0,1,&zero,0,0));
  assert(!state.Registry.Frames);
  expect_ok("R132 unrooted parent again",sys_update(&a,p,local_cpu+0x1c000,1,0,1,&parent,0,0));
  expect_ok("R132 unrooted map again",sys_update(&a,p,leaf,0,4,4,pte,0,0));
  expect_ok("R132 unrooted detach",sys_update(&a,p,local_cpu+0x1c000,1,0,1,&zero,0,0));
  assert(!state.Registry.Frames);
  expect_ok("R132 destroy",AdmissionDdiDestroyProcess(&a,p));
  assert(!state.Registry.Frames && !state.ProcessCount);
  /* Real BeginJob reparses the current GPU ranges and pins the graph
   * under the same lock. Queue admission itself is covered by the production
   * SubmitCommandVirtual replay; here no graph/lease operation is mocked. */
  p=sys_process(&a,0x80000,0);leaf=local_cpu+0x88000;
  DXGK_PTE *large=calloc(4096,sizeof(*large));assert(large);
  for(UINT i=0;i<4096;++i){large[i].Flags=1;large[i].PageAddress=(system_ipa>>12)+i;}
  expect_ok("R132 BeginJob mappings",sys_update(&a,p,leaf,0,16,4096,large,0,0));
  struct {APPLE_AGX_G4_NATIVE_HEADER ah;APPLE_AGX_G4_ATTACHMENT attachment;
    APPLE_AGX_G4_NATIVE_HEADER rh;APPLE_AGX_G4_NATIVE_RENDER render;} command={0};
  command.ah.Type=APPLE_AGX_G4_FRAGMENT_ATTACHMENTS;command.ah.Size=sizeof(command.attachment);
  command.ah.VdmBarrier=command.ah.CdmBarrier=0xffff;
  command.attachment.Pointer=0x14000;command.attachment.Size=4096;
  command.rh.Type=APPLE_AGX_G4_RENDER;command.rh.Size=sizeof(command.render);
  command.render.VdmCtrlStreamBase=0x18000;command.render.WidthPx=32;
  command.render.HeightPx=32;command.render.Layers=1;
  command.render.UtileWidthPx=command.render.UtileHeightPx=32;
  command.render.Samples=1;command.render.SampleSizeBytes=8;command.render.Flags=1u<<2;
  a.BackendImage.G4Native=1;a.BackendImage.BoundFence=91;
  a.BackendImage.G4CommandBytes=sizeof(command);
  APPLE_AGX_G4_PRIVATE_HEADER_V2 *h=&a.BackendImage.G4Header;
  h->Base.Magic=APPLE_AGX_G4_PRIVATE_MAGIC;h->Base.Version=2;
  h->Base.Reserved=APPLE_AGX_G4_COLOR_BGRA8;h->Base.HeaderBytes=sizeof(*h);h->Base.CommandBytes=sizeof(command);h->Base.CommandVa=0x10000;
  UINT needed[9];assert(AppleAgxG4ProcessRequiredBytes(&command.render,needed));
  ULONGLONG cursor=0x20000;
  for(UINT i=0;i<9;++i){h->Process[i].Va=cursor;h->Process[i].Bytes=(needed[i]+0xffffu)&~0xffffu;
    cursor+=(needed[i]+0xffffULL)&~0xffffULL;}
  assert(cursor<0x1010000);
  memcpy(a.BackendImage.Commands,&command,sizeof(command));
  ADMISSION_RENDER_CONTEXT context={0};context.GpuvaG3Process=p;
  context.GpuvaG3RootIpa=p->Graph.RootIpa;context.GpuvaG3DmaBufferVa=0x10000;
  context.GpuvaG3DmaBufferBytes=sizeof(command);
  context.GpuvaG3MappingGeneration=p->Graph.MappingGeneration;
  assert(cursor < 4000ULL * 4096ULL);
  expect_ok("R154 unrelated queued unmap",sys_update(&a,p,leaf,0,4000,1,&zero,0,0));
  assert(context.GpuvaG3MappingGeneration < p->Graph.MappingGeneration);
  expect_ok("R154 revalidate after unrelated unmap",AdmissionGpuvaG3BeginJob(&a,&context,91));
  assert(context.GpuvaG3MappingGeneration == p->Graph.MappingGeneration);
  assert(state.ActiveProcess==p && p->Graph.JobInFlight);
  assert(AdmissionDdiDestroyProcess(&a,p)==STATUS_DEVICE_BUSY);
  assert(sys_update(&a,p,leaf,0,24,1,&zero,0,0)==STATUS_DEVICE_BUSY);
  assert(AdmissionGpuvaG3CompleteJob(&a,91));
  expect_ok("R132 queued invalidate",sys_update(&a,p,leaf,0,24,1,&zero,0,0));
  assert(!NT_SUCCESS(AdmissionGpuvaG3BeginJob(&a,&context,91)));
  expect_ok("R132 queued same-PFN remap",sys_update(&a,p,leaf,0,24,1,&large[8],0,0));
  expect_ok("R154 restored mapping revalidated",AdmissionGpuvaG3BeginJob(&a,&context,91));
  assert(context.GpuvaG3MappingGeneration == p->Graph.MappingGeneration);
  assert(AdmissionGpuvaG3CompleteJob(&a,91));
  ULONGLONG old_root=p->Graph.RootIpa;
  assert(AppleAgxGpuvaG3GraphBindRoot(&p->Graph,p->BootstrapIpa));
  assert(!NT_SUCCESS(AdmissionGpuvaG3BeginJob(&a,&context,91)));
  assert(AppleAgxGpuvaG3GraphBindRoot(&p->Graph,old_root));
  expect_ok("R154 restored root revalidated",AdmissionGpuvaG3BeginJob(&a,&context,91));
  assert(context.GpuvaG3MappingGeneration == p->Graph.MappingGeneration);
  assert(AdmissionGpuvaG3CompleteJob(&a,91));
  a.BackendImage.G4Native=0;
  expect_ok("R132 BeginJob cleanup",AdmissionDdiDestroyProcess(&a,p));free(large);
  assert(!state.Registry.Frames);
  /* Replacement metadata allocation must fail before destructive retirement;
   * a retry of the new level cannot inherit the old leaf shadow lifetime. */
  p=sys_process(&a,0x90000,0);leaf=local_cpu+0x98000;
  expect_ok("R132 prepare detached leaf",sys_update(&a,p,local_cpu+0x90000,2,0,1,&zero,0,0));
  expect_ok("R132 unrooted old mapping",sys_update(&a,p,leaf,0,4,4,pte,0,0));
  APPLE_AGX_GPUVA_G3_ALLOC saved_alloc=p->Graph.Allocate;
  p->Graph.Allocate=sys_no_nodes;
  assert(!NT_SUCCESS(sys_update(&a,p,leaf,1,0,1,&zero,0,0)));
  assert(!p->Graph.Uncertain && state.Registry.Frames);
  p->Graph.Allocate=saved_alloc;
  expect_ok("R132 allocation retry new level",sys_update(&a,p,leaf,1,0,1,&zero,0,0));
  assert(!state.Registry.Frames);
  expect_ok("R132 allocation retry cleanup",AdmissionDdiDestroyProcess(&a,p));
  /* Capacity refusal preserves preceding leaves and logical-only residency. */
  p=sys_process(&a,0x40000,0);leaf=local_cpu+0x48000;
  q=sys_process(&a,0x50000,1);
  for(UINT i=0;i<HV_AGX_GPUVA_V5_BACKINGS-1;++i)
    assert(hv_agx_gpuva_v5_register_shared_backing(&gpuva_v5,q->Graph.ProcessId,
        q->Graph.ProcessGeneration,777,system_ipa+0x100000+i*0x4000ULL)==HV_AGX_GPUVA_V5_OK);
  expect_ok("R133 capacity stays unpublished",sys_update(&a,p,leaf,0,4,8,pte,0,0));
  assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x4000,0x4000));
  assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x8000,1));
  assert(sys_frame(&state,system_ipa)->Grants==1);
  assert(sys_frame(&state,system_ipa+0x4000)->Mappings==4);
  assert(sys_frame(&state,system_ipa+0x4000)->Grants==0);
  assert(!p->Graph.Uncertain);
  for(UINT i=0;i<HV_AGX_GPUVA_V5_BACKINGS-1;++i)
    assert(hv_agx_gpuva_v5_revoke_backing(&gpuva_v5,q->Graph.ProcessId,
        q->Graph.ProcessGeneration,777,system_ipa+0x100000+i*0x4000ULL)==HV_AGX_GPUVA_V5_OK);
  expect_ok("R133 capacity retry",sys_update(&a,p,leaf,0,8,4,pte+4,0,0));
  assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x8000,0x4000));
  expect_ok("R133 capacity retirement",sys_update(&a,p,leaf,0,4,8,&zero,0,1));
  assert(!state.Registry.Frames);
  expect_ok("R132 capacity q cleanup",AdmissionDdiDestroyProcess(&a,q));
  /* Wire epoch/process/backing generations and shared/exclusive collisions. */
  expect_ok("R132 post-capacity map",sys_update(&a,p,leaf,0,4,4,pte,0,0));
  generation=sys_frame(&state,system_ipa)->Generation;
  assert(hv_agx_gpuva_v5_revoke_backing(&gpuva_v5,p->Graph.ProcessId,
      p->Graph.ProcessGeneration,generation,system_ipa)==HV_AGX_GPUVA_V5_BUSY);
  assert(hv_agx_gpuva_v5_revoke_backing(&gpuva_v5,p->Graph.ProcessId,
      p->Graph.ProcessGeneration,generation+1,system_ipa)==HV_AGX_GPUVA_V5_STALE);
  AGX_GPUVA_V5_REQUEST req={0};AGX_GPUVA_V5_RESPONSE reply={0};
  req.Command=AGX_GPUVA_V5_REGISTER_BACKING;req.ProcessId=p->Graph.ProcessId;
  req.ProcessGeneration=p->Graph.ProcessGeneration+1;
  req.AllocationGeneration=generation;req.AuxIpa=system_ipa;
  assert(AppleAgxGpuvaV5ClientCall(&state.Client,&req,&reply));assert(reply.Status==HV_AGX_GPUVA_V5_STALE);
  state.Client.Epoch=6;
  assert(!AppleAgxGpuvaV5ClientCall(&state.Client,&req,&reply));
  state.Client.Epoch=7;
  q=sys_process(&a,0x60000,0);
  assert(hv_agx_gpuva_v5_register_backing(&gpuva_v5,q->Graph.ProcessId,
      q->Graph.ProcessGeneration,generation,system_ipa)==HV_AGX_GPUVA_V5_OWNERSHIP);
  assert(hv_agx_gpuva_v5_register_shared_backing(&gpuva_v5,q->Graph.ProcessId,
      q->Graph.ProcessGeneration,generation+1,system_ipa)==HV_AGX_GPUVA_V5_OWNERSHIP);
  expect_ok("R132 wire q cleanup",AdmissionDdiDestroyProcess(&a,q));
  expect_ok("R132 final p cleanup",AdmissionDdiDestroyProcess(&a,p));
  assert(!state.Registry.Frames);
  /* A post-store synchronization failure taints the KMD even if the broker
   * restored the descriptor. No paging success, release, or BeginJob follows. */
  p=sys_process(&a,0x70000,0);leaf=local_cpu+0x78000;
  UINT fail_count=getenv("G3_REPLAY_R132_SYNC_ONCE") ? 1u : 2u;
  if(getenv("G3_REPLAY_R132_TLB")) {
    req=(AGX_GPUVA_V5_REQUEST){0};req.Command=AGX_GPUVA_V5_LEASE;
    req.ProcessId=p->Graph.ProcessId;req.ProcessGeneration=p->Graph.ProcessGeneration;req.Slot=1;
    assert(AppleAgxGpuvaV5ClientCall(&state.Client,&req,&reply));assert(reply.Status==0 && reply.Token);
    b.invalidate_failures=2;
  } else b.sync_failures=fail_count;
  assert(!NT_SUCCESS(sys_update(&a,p,leaf,0,4,4,pte,0,0)));
  assert(p->Graph.Uncertain && p->Poisoned);
  assert(gpuva_v5.tainted == (fail_count==2));
  assert(!AppleAgxGpuvaG3GraphBeginJob(&p->Graph,1));
  assert(AdmissionDdiDestroyProcess(&a,p)==STATUS_DEVICE_BUSY);
  assert(sys_frame(&state,system_ipa)->Mappings==4 && sys_frame(&state,system_ipa)->Grants==1);
  s=p->TableShadows;while(s->OriginalIpa!=local_ipa+0x78000) s=s->Next;
  assert(s->PendingPtes && !s->ResidentPtes[4].Flags);
  assert(((ULONGLONG *)s->Memory.CpuAddress)[1]==0); /* descriptor restored */
  /* Terminal/poisoned records intentionally live until reboot. Host fixture
   * frees them below without invoking production recovery or claiming teardown. */
  while(state.Processes.Flink != &state.Processes) {
    ADMISSION_G3_PROCESS *dead=CONTAINING_RECORD(state.Processes.Flink,ADMISSION_G3_PROCESS,Link);
    RemoveEntryList(&dead->Link);
    while(dead->TableShadows){ADMISSION_G3_TABLE_SHADOW *t=dead->TableShadows;dead->TableShadows=t->Next;
      free(t->LogicalPtes);free(t->ResidentPtes);free(t->PendingPtes);free(t);}
    APPLE_AGX_GPUVA_G3_NODE **heads[]={&dead->Graph.Tables,&dead->Graph.Parents,&dead->Graph.Leaves,&dead->Graph.Backings};
    for(UINT i=0;i<4;++i)while(*heads[i]){APPLE_AGX_GPUVA_G3_NODE *n=*heads[i];*heads[i]=n->Next;free(n);}
    free(dead);
  }
  while(state.Registry.Frames){APPLE_AGX_GPUVA_G3_FRAME *f=state.Registry.Frames;state.Registry.Frames=f->Next;free(f);}
  free(local_cpu);local_cpu=NULL;
  puts("R132 system lifetime: PASS");
}
