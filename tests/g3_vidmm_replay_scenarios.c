/* Receipts EXP776–783 followed by documented leaf/root/flush and a user process. */
static void expect_ok(const char *step, NTSTATUS status) {
  if (!NT_SUCCESS(status)) {
    fprintf(stderr, "%s: status %08x\n", step, (unsigned)status);
    exit(1);
  }
}
static void update(ADMISSION_CONTEXT *adapter, HANDLE process, UINT level,
                   void *table, UINT count, ULONGLONG first_va,
                   DXGK_UPDATEPAGETABLEFLAGS flags,
                   DXGK_PTE *entries, const char *name) {
  unsigned char dma[4096]={0}, private_data[4096]={0};
  DXGKARG_BUILDPAGINGBUFFER args={0};
  args.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;
  args.pDmaBuffer=dma;
  args.pDmaBufferPrivateData=private_data;
  args.UpdatePageTable.hProcess=process;
  args.UpdatePageTable.PageTableAddress.CpuVirtual=table;
  args.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
  args.UpdatePageTable.PageTableLevel=level;
  args.UpdatePageTable.NumPageTableEntries=count;
  args.UpdatePageTable.FirstPteVirtualAddress=first_va;
  args.UpdatePageTable.Flags=flags;
  args.UpdatePageTable.pPageTableEntries=entries;
  expect_ok(name,AdmissionGpuvaG3BuildPagingBuffer(adapter,&args));
  assert(args.pDmaBuffer==dma && args.pDmaBufferPrivateData==private_data);
}
int main(void) {
  assert(DXGK_PAGETABLEUPDATE_CPU_VIRTUAL==0);
  assert(DXGK_PAGETABLEUPDATE_GPU_PHYSICAL==2);
  ADMISSION_CONTEXT adapter={0};
  ADMISSION_G3_STATE state={0};
  REPLAY_BROKER broker={0};
  APPLE_AGX_GPUVA_V5_IO io={&broker,ReplayWrite64,ReplayRead64,
                             ReplayWrite32,ReplayBarrier};
  ADMISSION_DEVICE device={0};
  DXGKARG_CREATEPROCESS sys={0}, user={0};
  DXGKARG_CREATECONTEXT cc={0};
  DXGKARG_SETROOTPAGETABLE root={0};
  DXGKARG_BUILDPAGINGBUFFER flush={0};
  DXGK_UPDATEPAGETABLEFLAGS flags={0};
  DXGK_PTE empty={0},parents[2048]={0},leaf={0}, map32[32]={0};
  DXGK_PTE *zeros=NULL;
  assert(posix_memalign((void **)&local_cpu,0x4000,(size_t)local_bytes)==0);
  memset(local_cpu,0,(size_t)local_bytes);
  ReplayBrokerInit(&broker);
  InitializeListHead(&state.Processes);
  assert(AppleAgxGpuvaV5ClientInit(&state.Client,&io));
  {
    AGX_GPUVA_V5_REQUEST probe={0};
    AGX_GPUVA_V5_RESPONSE response={0};
    probe.Command=AGX_GPUVA_V5_CREATE;
    assert(AppleAgxGpuvaV5ClientCall(&state.Client,&probe,&response));
    assert(response.Status==HV_AGX_GPUVA_V5_STALE && response.Epoch==7);
  }
  adapter.Started=TRUE;
  adapter.GpuvaG3State=&state;
  adapter.ObjectAdapter=&adapter;
  state.Adapter=&adapter;
  sys.Flags.SystemProcess=1;
  sys.NumPasid=1;
  expect_ok("EXP776 CreateProcess PASID1",AdmissionDdiCreateProcess(&adapter,&sys));
  assert(sys.hKmdProcess);
  device.Object.Magic=ADMISSION_OBJECT_DEVICE_MAGIC;
  device.Object.Adapter=&adapter.ObjectAdapter;
  device.GpuvaG3Process=sys.hKmdProcess;
  cc.Flags.Value=5; /* SystemContext | VirtualAddressing, EXP778. */
  cc.EngineAffinity=1;
  expect_ok("EXP778 CreateContext flags5",AdmissionDdiCreateContext(&device,&cc));
  assert(cc.hContext);
#ifdef G3_REPLAY_CONTEXT_SEGMENT_CHECK
  assert(cc.ContextInfo.DmaBufferSegmentSet==1u);
  assert(cc.ContextInfo.DmaBufferSize==0x50000u);
  assert(cc.ContextInfo.DmaBufferPrivateDataSize==0x51000u);
  assert(cc.ContextInfo.AllocationListSize==64u);
  assert(cc.ContextInfo.PatchLocationListSize==64u);
  assert(cc.ContextInfo.Caps.Value==0u);
  assert(cc.ContextInfo.PagingCompanionNodeId==0u);
  {
    DXGKARG_CREATECONTEXT gdi={0}, paging={0};
    gdi.Flags.Value=6; /* GdiContext | VirtualAddressing, EXP792. */
    gdi.hContext=(HANDLE)1;
    gdi.EngineAffinity=1;
    expect_ok("EXP792 GDI virtual context",AdmissionDdiCreateContext(&device,&gdi));
    assert(gdi.ContextInfo.DmaBufferSegmentSet==1u);
    assert(gdi.ContextInfo.DmaBufferSize==0x50000u);
    assert(gdi.ContextInfo.DmaBufferPrivateDataSize==0x51000u);
    assert(gdi.ContextInfo.AllocationListSize==256u);
    assert(gdi.ContextInfo.PatchLocationListSize==256u);
    assert(gdi.ContextInfo.Caps.Value==0u);
    assert(gdi.ContextInfo.PagingCompanionNodeId==0u);
    paging.Flags.Value=1; /* Nonvirtual paging SystemContext. */
    paging.EngineAffinity=1;
    expect_ok("nonvirtual paging context",AdmissionDdiCreateContext(&device,&paging));
    assert(paging.ContextInfo.DmaBufferSegmentSet==0u);
    assert(paging.ContextInfo.DmaBufferSize==0x50000u);
    assert(paging.ContextInfo.DmaBufferPrivateDataSize==0x51000u);
    assert(paging.ContextInfo.AllocationListSize==64u);
    assert(paging.ContextInfo.PatchLocationListSize==64u);
    assert(paging.ContextInfo.Caps.Value==0u);
    assert(paging.ContextInfo.PagingCompanionNodeId==0u);
    expect_ok("destroy GDI virtual context",AdmissionDdiDestroyContext(gdi.hContext));
    expect_ok("destroy nonvirtual paging context",AdmissionDdiDestroyContext(paging.hContext));
  }
#endif
  root.hContext=cc.hContext;
  root.Address.SegmentId=ADMISSION_MEMORY_LOCAL_SEGMENT;
  root.Address.SegmentOffset=0;
  root.NumEntries=8;
  AdmissionDdiSetRootPageTable(&adapter,&root);
  assert(!((ADMISSION_RENDER_CONTEXT *)cc.hContext)->GpuvaG3Poisoned);
  flags.Repeat=1; flags.InitialUpdate=1;
  if (getenv("G3_REPLAY_DMA_ONLY")) {
    zeros=calloc(8192,sizeof(*zeros));
    assert(zeros);
    flags.Repeat=0;
  }
  /* EXP785: the allocator may return a dirty page before InitialUpdate. */
  if (getenv("G3_REPLAY_DIRTY_TABLE"))
    ((uint64_t *)(local_cpu+0x4000))[19]=0xfeed123456789abcULL;
  update(&adapter,sys.hKmdProcess,0,local_cpu+0x4000,8192,0,flags,
         zeros ? zeros : &empty,
         "EXP780 level0 Repeat InitialUpdate DMA pointers");
  if (getenv("G3_REPLAY_DIRTY_TABLE"))
  {
    assert(((uint64_t *)(local_cpu+0x4000))[19]==0);
    assert(last_paging_failure.Branch==8);
    assert(last_paging_failure.TableAddBranch==1);
    assert(last_paging_failure.TableFirstNonzeroIndex==19);
    assert(last_paging_failure.TableFirstNonzeroWord==0xfeed123456789abcULL);
  }
  {
    /* Model a freed graph node whose broker registration was not revoked.
     * The page is empty, so table_add must reject duplicate ownership (d). */
    ADMISSION_G3_PROCESS *process=(ADMISSION_G3_PROCESS *)sys.hKmdProcess;
    APPLE_AGX_GPUVA_G3_NODE **link=&process->Graph.Tables;
    APPLE_AGX_GPUVA_G3_NODE *held;
    ULONGLONG registered_ipa=0;
    if (AdmissionGpuvaG3BrokerTable(process,local_ipa+0x4000,
                                    FALSE,&registered_ipa)!=STATUS_SUCCESS)
      registered_ipa=local_ipa+0x4000;
    while (*link && (*link)->Ipa!=registered_ipa) link=&(*link)->Next;
    assert(*link);
    held=*link;
    *link=held->Next;
    assert(!AppleAgxGpuvaG3GraphRegisterTable(&process->Graph,
                                                registered_ipa,2));
    assert(process->Graph.LastStatus==HV_AGX_GPUVA_V5_OWNERSHIP);
    held->Next=*link;
    *link=held;
  }
  free(zeros);
  parents[1].Flags=0x41; parents[1].PageTableAddress=0xC;
  parents[2].Flags=0x20041; parents[2].PageTableAddress=0x2C;
  flags.Value=0;
  update(&adapter,sys.hKmdProcess,1,local_cpu+0x8000,2048,0,flags,parents,
         "EXP783 level1 mixed 4K/64K parent PTEs");
  /* EXP784C records this metadata; the exact 32 PTE values were not receipted.
   * A contiguous local projection exercises the real leaf graph path. */
  for (UINT i=0;i<32;i++) {
    map32[i].Flags=0x41;
    map32[i].PageAddress=0x100+i;
  }
  if (getenv("G3_REPLAY_SELF_TABLE_BACKING")) {
    /* EXP786 receipted the first PTE exactly; adjacent PTEs remain a
     * contiguous projection because their values were not captured. */
    for (UINT i=0;i<4;i++) map32[i].PageAddress=0xc+i;
  }
  update(&adapter,sys.hKmdProcess,0,local_cpu+0xc000,32,0x2000000,
         flags,map32,"EXP784C level0 Count32 Flags0 projection");
  if (getenv("G3_REPLAY_SELF_TABLE_BACKING")) {
    ADMISSION_G3_PROCESS *process=(ADMISSION_G3_PROCESS *)sys.hKmdProcess;
    ADMISSION_G3_TABLE_SHADOW *shadow=process->TableShadows;
    APPLE_AGX_GPUVA_G3_NODE *backing=process->Graph.Backings;
    while (shadow && shadow->OriginalIpa!=local_ipa+0xc000)
      shadow=shadow->Next;
    while (backing && backing->Ipa!=local_ipa+0xc000)
      backing=backing->Next;
    assert(shadow && backing);
    assert(shadow->BrokerIpa!=local_ipa+0xc000);
    assert(shadow->BrokerIpa>=local_ipa+0x3800000ULL);
    assert(memcmp(local_cpu+0xc000,shadow->Memory.CpuAddress,0x4000)==0);
  }
  if (getenv("G3_REPLAY_SINGLE_PTE")) {
    /* EXP788 exact valid RO system PTE; it must remain unpublished alone. */
    DXGK_PTE pte={0};
    DXGKARG_BUILDPAGINGBUFFER one={0};
    one.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;
    one.UpdatePageTable.hProcess=sys.hKmdProcess;
    one.UpdatePageTable.PageTableAddress.CpuVirtual=local_cpu+0xc000;
    one.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
    one.UpdatePageTable.PageTableLevel=0;
    one.UpdatePageTable.StartIndex=64;
    one.UpdatePageTable.NumPageTableEntries=1;
    one.UpdatePageTable.FirstPteVirtualAddress=0x2040000;
    one.UpdatePageTable.pPageTableEntries=&pte;
    pte.Flags=0x9;
    pte.PageAddress=0x9916a0;
    expect_ok("EXP788 Count1 valid RO system PTE",
              AdmissionGpuvaG3BuildPagingBuffer(&adapter,&one));
    {
      ADMISSION_G3_PROCESS *process=(ADMISSION_G3_PROCESS *)sys.hKmdProcess;
      APPLE_AGX_GPUVA_G3_NODE *node=process->Graph.Leaves;
      while (node && !(node->Index==16 &&
              node->Ipa==local_ipa+0x3808000)) node=node->Next;
      assert(!node);
    }
    pte.Flags=0;
    pte.PageAddress=0;
    expect_ok("Count1 unmap",AdmissionGpuvaG3BuildPagingBuffer(&adapter,&one));
    {
      DXGK_PTE group[4]={0};
      for (UINT i=0;i<4;i++) {
        group[i].Flags=0x9;
        group[i].PageAddress=0x10180+i;
      }
      one.UpdatePageTable.NumPageTableEntries=3;
      one.UpdatePageTable.pPageTableEntries=group;
      expect_ok("partial three PTEs",AdmissionGpuvaG3BuildPagingBuffer(&adapter,&one));
      one.UpdatePageTable.StartIndex=67;
      one.UpdatePageTable.NumPageTableEntries=1;
      one.UpdatePageTable.FirstPteVirtualAddress=0x2043000;
      one.UpdatePageTable.pPageTableEntries=&group[3];
      expect_ok("complete logical system group",AdmissionGpuvaG3BuildPagingBuffer(&adapter,&one));
      {
        ADMISSION_G3_PROCESS *process=(ADMISSION_G3_PROCESS *)sys.hKmdProcess;
        APPLE_AGX_GPUVA_G3_NODE *node=process->Graph.Leaves;
        while (node && !(node->Index==16 &&
                node->AuxIpa==local_ipa+0x180000)) node=node->Next;
        assert(!node); /* No system-memory backing grant for this process. */
      }
      pte.Flags=0;
      one.UpdatePageTable.StartIndex=66;
      one.UpdatePageTable.FirstPteVirtualAddress=0x2042000;
      one.UpdatePageTable.pPageTableEntries=&pte;
      expect_ok("partial invalidate",AdmissionGpuvaG3BuildPagingBuffer(&adapter,&one));
      {
        ADMISSION_G3_PROCESS *process=(ADMISSION_G3_PROCESS *)sys.hKmdProcess;
        APPLE_AGX_GPUVA_G3_NODE *node=process->Graph.Leaves;
        while (node && node->Index!=16) node=node->Next;
        assert(!node);
      }
    }
  }
  {
    DXGK_PTE ptes[4]={0};
    DXGKARG_BUILDPAGINGBUFFER failed={0};
    for (UINT i=0;i<4;i++) {
      ptes[i].Flags=0x41;
      ptes[i].PageAddress=0x140+i;
    }
    failed.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;
    failed.UpdatePageTable.hProcess=sys.hKmdProcess;
    failed.UpdatePageTable.PageTableAddress.CpuVirtual=local_cpu+0xc000;
    failed.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
    failed.UpdatePageTable.PageTableLevel=0;
    failed.UpdatePageTable.NumPageTableEntries=4;
    failed.UpdatePageTable.FirstPteVirtualAddress=0x2008000;
    failed.UpdatePageTable.pPageTableEntries=ptes;
    broker.blocked_ipa=local_ipa+0x140000;
    assert(AdmissionGpuvaG3BuildPagingBuffer(&adapter,&failed)==
           STATUS_DEVICE_HARDWARE_ERROR);
    assert(last_paging_failure.Branch==7);
    assert(last_paging_failure.Level==0);
    assert(last_paging_failure.Index==0);
    assert(last_paging_failure.PageAddress==0x140);
    assert(last_paging_failure.ChildIpa==local_ipa+0x140000);
    assert(last_paging_failure.GraphLastStatus==4);
    broker.blocked_ipa=0;
  }
  {
    /* EXP793: four valid 4K system pages can occupy one logical group
     * without forming an AGX 16K backing.  Keep the shadow, publish none. */
    DXGK_PTE scattered[4]={0};
    DXGKARG_BUILDPAGINGBUFFER system_update={0};
    UINT calls=broker.commands;
    ULONGLONG unpublished_before=state.UnpublishedGroups[0];
    for (UINT i=0;i<4;i++) {
      scattered[i].Flags=0x9;
      scattered[i].PageAddress=0x851000+i*0x20;
    }
    system_update.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;
    system_update.UpdatePageTable.hProcess=sys.hKmdProcess;
    system_update.UpdatePageTable.PageTableAddress.CpuVirtual=local_cpu+0xc000;
    system_update.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
    system_update.UpdatePageTable.PageTableLevel=0;
    system_update.UpdatePageTable.StartIndex=0x5c;
    system_update.UpdatePageTable.NumPageTableEntries=4;
    system_update.UpdatePageTable.FirstPteVirtualAddress=0x205c000;
    system_update.UpdatePageTable.pPageTableEntries=scattered;
    expect_ok("EXP793 scattered system 4K group",
        AdmissionGpuvaG3BuildPagingBuffer(&adapter,&system_update));
    assert(broker.commands==calls);
    assert(state.UnpublishedGroups[0]==unpublished_before+1u);
    for (UINT i=0;i<4;i++) scattered[i].PageAddress=0x851000+i;
    calls=broker.commands;
    expect_ok("EXP793 unregistered system 16K group",
        AdmissionGpuvaG3BuildPagingBuffer(&adapter,&system_update));
    assert(broker.commands==calls);
    assert(state.UnpublishedGroups[0]==unpublished_before+2u);
    {
      ADMISSION_G3_PROCESS *process=(ADMISSION_G3_PROCESS *)sys.hKmdProcess;
      ADMISSION_G3_TABLE_SHADOW *shadow=process->TableShadows;
      while (shadow && shadow->OriginalIpa!=local_ipa+0xc000)
        shadow=shadow->Next;
      assert(shadow && shadow->LogicalPtes[0x5c].GuestIpa==
             0x851000000ULL);
    }
  }
  leaf.Flags=0x41; leaf.PageAddress=0x10;
  flags.Use64KBPages=1; flags.InitialUpdate=1; flags.Repeat=1;
  update(&adapter,sys.hKmdProcess,0,local_cpu+0x2c000,512,0,flags,&leaf,
         "Learn leaf 64K PTE");
  flush.Operation=DXGK_OPERATION_FLUSH_TLB;
  flush.FlushTlb.hProcess=sys.hKmdProcess;
  flush.FlushTlb.RootPageTableAddress=root.Address;
  flush.FlushTlb.StartVirtualAddress=0;
  flush.FlushTlb.EndVirtualAddress=0x10000;
  expect_ok("Learn FlushTlb",AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush));
  flush.FlushTlb.StartVirtualAddress=0x12345;
  flush.FlushTlb.EndVirtualAddress=0x1ffff;
  expect_ok("R78 inclusive unaligned FlushTlb",AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush));
  assert(broker.last_flush_start==0x10000 && broker.last_flush_end==0x20000);
  assert(last_flush_receipt.Branch==1 &&
         last_flush_receipt.InputStart==0x12345 &&
         last_flush_receipt.InputEnd==0x1ffff &&
         last_flush_receipt.GraphRootIpa==
             ((ADMISSION_G3_PROCESS *)sys.hKmdProcess)->Graph.RootIpa &&
         last_flush_receipt.RootOffset==root.Address.SegmentOffset);
  flush.FlushTlb.StartVirtualAddress=0;
  flush.FlushTlb.EndVirtualAddress=0;
  expect_ok("R78 full FlushTlb",AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush));
  assert(broker.last_flush_start==0 && broker.last_flush_end==0);
  assert(last_flush_receipt.Branch==2);
  flush.FlushTlb.StartVirtualAddress=0x30000;
  flush.FlushTlb.EndVirtualAddress=0x20000;
  expect_ok("R78 reversed FlushTlb fallback",AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush));
  assert(broker.last_flush_start==0 && broker.last_flush_end==0);
  assert(last_flush_receipt.Branch==3);
  flush.FlushTlb.StartVirtualAddress=(1ULL<<39)-0x4000;
  flush.FlushTlb.EndVirtualAddress=(1ULL<<39)+0x1000;
  expect_ok("R78 upper bound clamp",AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush));
  assert(broker.last_flush_start==(1ULL<<39)-0x4000 &&
         broker.last_flush_end==(1ULL<<39));
  flush.FlushTlb.StartVirtualAddress=0x1000;
  flush.FlushTlb.EndVirtualAddress=0x1ffff;
  flush.FlushTlb.RootPageTableAddress.SegmentOffset=0x4000;
  assert(AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush)==STATUS_INVALID_PARAMETER);
  assert(last_flush_receipt.Branch==4);
  flush.FlushTlb.RootPageTableAddress=root.Address;
  expect_ok("DestroyContext",AdmissionDdiDestroyContext(cc.hContext));
  expect_ok("DestroyProcess",AdmissionDdiDestroyProcess(&adapter,sys.hKmdProcess));
  expect_ok("Learn ordinary CreateProcess",AdmissionDdiCreateProcess(&adapter,&user));
  expect_ok("ordinary DestroyProcess",AdmissionDdiDestroyProcess(&adapter,user.hKmdProcess));
  free(local_cpu);
  puts("G3 VidMm host replay: all recorded and projected inputs passed via real m1n1 broker dispatch");
  return 0;
}
