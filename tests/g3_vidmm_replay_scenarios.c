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
  const int r79=getenv("G3_REPLAY_HISTORICAL")==NULL;
  /* EXP799 receipted the child IPA, not the reserve base.  This synthetic
   * base keeps that exact IPA inside the modelled 56 MiB local segment. */
  if (getenv("G3_REPLAY_EXP799")) local_ipa=0x9bc000000ULL;
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
    DXGKARG_CREATECONTEXT gdi={0}, paging={0}, render={0};
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
    render.Flags.Value=4; /* G4 UMD CreateContextVirtual render context. */
    render.hContext=(HANDLE)2;
    render.EngineAffinity=1;
    expect_ok("G4 render virtual context",AdmissionDdiCreateContext(&device,&render));
    assert(render.ContextInfo.DmaBufferSegmentSet==1u);
    assert(render.ContextInfo.DmaBufferPrivateDataSize==0x51000u);
    assert(render.ContextInfo.AllocationListSize==16u);
    assert(render.ContextInfo.PatchLocationListSize==0u);
    assert(render.ContextInfo.Caps.Value==1u); /* NoPatchingRequired. */
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
    expect_ok("destroy G4 render context",AdmissionDdiDestroyContext(render.hContext));
    expect_ok("destroy nonvirtual paging context",AdmissionDdiDestroyContext(paging.hContext));
  }
#endif
  root.hContext=cc.hContext;
  root.Address.SegmentId=ADMISSION_MEMORY_LOCAL_SEGMENT;
  root.Address.SegmentOffset=0;
  root.NumEntries=8;
  if (r79) {
    /* VidMm may own a table before setting it as the hardware root. */
    flags.Repeat=1; flags.InitialUpdate=1;
    update(&adapter,sys.hKmdProcess,2,local_cpu,8,0,flags,&empty,
           "R79 prepare root before SetRootPageTable");
    flush.Operation=DXGK_OPERATION_FLUSH_TLB;
    flush.FlushTlb.hProcess=sys.hKmdProcess;
    flush.FlushTlb.RootPageTableAddress=root.Address;
    /* EXP796 receipts the actual pre-root VidMm interval. */
    flush.FlushTlb.StartVirtualAddress=0x2030000;
    flush.FlushTlb.EndVirtualAddress=0x25b0000;
    UINT flushes=broker.flush_commands;
    expect_ok("R79 flush before SetRootPageTable",
        AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush));
    assert(broker.flush_commands==flushes);
    assert(last_flush_receipt.Branch==5);
    assert(last_flush_receipt.RootOffset==0 &&
           last_flush_receipt.InputStart==0x2030000 &&
           last_flush_receipt.InputEnd==0x25b0000);
  }
  AdmissionDdiSetRootPageTable(&adapter,&root);
  assert(!((ADMISSION_RENDER_CONTEXT *)cc.hContext)->GpuvaG3Poisoned);
  if (r79) {
    UINT flushes=broker.flush_commands;
    expect_ok("R79 current root without slot",
        AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush));
    assert(broker.flush_commands==flushes);
    assert(last_flush_receipt.Branch==5);
  }
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
  if (getenv("G3_REPLAY_EXP799")) {
    ADMISSION_G3_PROCESS *first=(ADMISSION_G3_PROCESS *)sys.hKmdProcess;
    ADMISSION_G3_PROCESS *second;
    DXGK_PTE ptes[4]={0};
    DXGKARG_BUILDPAGINGBUFFER observed={0};
    ULONGLONG target=0x9bcb20000ULL;
    int first_owner=-1, table_collision=0;
    assert(target>=local_ipa && target-local_ipa<0x3800000ULL);
    assert(ReplayTranslate(&broker,target)==target);
    for (unsigned i=0;i<HV_AGX_GPUVA_V5_TABLES;i++)
      if (gpuva_v5.tables[i].live && gpuva_v5.tables[i].pa==target)
        table_collision=1;
    assert(!table_collision);
    for (UINT i=0;i<4;i++) {
      ptes[i].Flags=0x41;
      ptes[i].PageAddress=(target-local_ipa)/0x1000ULL+i;
    }
    observed.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;
    observed.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
    observed.UpdatePageTable.PageTableLevel=0;
    observed.UpdatePageTable.StartIndex=0xbb0u;
    observed.UpdatePageTable.NumPageTableEntries=4;
    observed.UpdatePageTable.FirstPteVirtualAddress=0x2bb0000ULL;
    observed.UpdatePageTable.pPageTableEntries=ptes;
    observed.UpdatePageTable.PageTableAddress.CpuVirtual=local_cpu+0xc000;
    observed.UpdatePageTable.hProcess=sys.hKmdProcess;
    expect_ok("EXP799 first process local leaf",
        AdmissionGpuvaG3BuildPagingBuffer(&adapter,&observed));
    for (unsigned i=0;i<HV_AGX_GPUVA_V5_BACKINGS;i++)
      if (gpuva_v5.backings[i].live && gpuva_v5.backings[i].pa==target)
        first_owner=(int)gpuva_v5.backings[i].owner;
    assert(first_owner>=0);
    expect_ok("EXP799 second process create",
        AdmissionDdiCreateProcess(&adapter,&user));
    second=(ADMISSION_G3_PROCESS *)user.hKmdProcess;
    assert(second);
    assert(first->Graph.ProcessId!=second->Graph.ProcessId);
    assert(first->Graph.SharedBackingGeneration==
        second->Graph.SharedBackingGeneration);
    assert(hv_agx_gpuva_v5_register_shared_backing(&gpuva_v5,
        second->Graph.ProcessId,second->Graph.ProcessGeneration,
        second->Graph.SharedBackingGeneration+1u,target)==
        HV_AGX_GPUVA_V5_OWNERSHIP); /* existing backing, wrong generation */
    assert(hv_agx_gpuva_v5_register_shared_backing(&gpuva_v5,
        second->Graph.ProcessId,second->Graph.ProcessGeneration,
        second->Graph.SharedBackingGeneration,first->Graph.RootIpa)==
        HV_AGX_GPUVA_V5_OWNERSHIP); /* table page is not backing */
    broker.blocked_ipa=target;
    assert(hv_agx_gpuva_v5_register_shared_backing(&gpuva_v5,
        second->Graph.ProcessId,second->Graph.ProcessGeneration,
        second->Graph.SharedBackingGeneration,target)==
        HV_AGX_GPUVA_V5_OWNERSHIP); /* translation failure */
    broker.blocked_ipa=0;
    observed.UpdatePageTable.PageTableAddress.CpuVirtual=local_cpu+0x20000;
    observed.UpdatePageTable.hProcess=user.hKmdProcess;
    expect_ok("EXP799 second process index/IPA local leaf",
        AdmissionGpuvaG3BuildPagingBuffer(&adapter,&observed));
    puts("EXP799 local leaf: translation/alignment and table isolation pass; shared reserve backing passes");
  }
  if (r79 && getenv("G3_REPLAY_SELF_TABLE_BACKING")==NULL) {
  {
    DXGK_PTE root_link={0};
    root_link.Flags=0x41;
    root_link.PageTableAddress=0x8;
    update(&adapter,sys.hKmdProcess,2,local_cpu,1,0,flags,&root_link,
           "R80 paging root links the scratch-area table");
  }
  {
    unsigned char dma[256]={0}, private_data[512]={0};
    DXGKARG_BUILDPAGINGBUFFER fill={0};
    fill.Operation=DXGK_OPERATION_VIRTUAL_FILL;
    fill.hSystemContext=cc.hContext;
    fill.pDmaBuffer=dma;
    fill.DmaSize=sizeof(dma);
    fill.pDmaBufferPrivateData=private_data;
    fill.DmaBufferPrivateDataSize=sizeof(private_data);
    fill.FillVirtual.DestinationVirtualAddress=0x2000000;
    fill.FillVirtual.FillSizeInBytes=0x1000;
    fill.FillVirtual.FillPattern=0x7f81a2c3;
    expect_ok("R80 FillVirtual builds a paging packet",
              AdmissionGpuvaG3BuildPagingBuffer(&adapter,&fill));
    assert(fill.pDmaBuffer>=(void *)dma+16);
    ADMISSION_PAGING_RECORD *record=(ADMISSION_PAGING_RECORD *)private_data;
    assert(record->Kind==AdmissionPagingVirtualFill);
    assert(AdmissionPagingRecordsValid(record,1,64,
                                      sizeof(ADMISSION_PAGING_MARKER)));
    expect_ok("R80 FillVirtual CPU execution",
              AdmissionG3ExecuteVirtualPaging(&adapter,record));
    for (UINT i=0;i<0x1000;i++)
      assert(local_cpu[0x100000+i]==((unsigned char *)&record->FillPattern)[i&3u]);
    memset(dma,0,sizeof(dma));
    memset(private_data,0,sizeof(private_data));
    DXGKARG_BUILDPAGINGBUFFER fence={0};
    fence.Operation=DXGK_OPERATION_SIGNAL_MONITORED_FENCE;
    fence.hSystemContext=cc.hContext;
    fence.pDmaBuffer=dma;
    fence.DmaSize=sizeof(dma);
    fence.pDmaBufferPrivateData=private_data;
    fence.DmaBufferPrivateDataSize=sizeof(private_data);
    fence.SignalMonitoredFence.MonitoredFenceGpuVa=0x2000010;
    fence.SignalMonitoredFence.MonitoredFenceValue=0x718293a4b5c6d7e8ULL;
    expect_ok("R80 monitored fence build",
              AdmissionGpuvaG3BuildPagingBuffer(&adapter,&fence));
    assert(AdmissionPagingRecordsValid(
        (ADMISSION_PAGING_RECORD *)private_data,1,64,
        sizeof(ADMISSION_PAGING_MARKER)));
    {
      ADMISSION_PAGING_RECORD malformed=
          *(ADMISSION_PAGING_RECORD *)private_data;
      malformed.PatternOffset=MAXULONG;
      assert(!AdmissionPagingRecordsValid(&malformed,1,64,
          sizeof(ADMISSION_PAGING_MARKER)));
    }
    expect_ok("R80 monitored fence CPU execution",
              AdmissionG3ExecuteVirtualPaging(&adapter,
                  (ADMISSION_PAGING_RECORD *)private_data));
    assert(*(ULONGLONG *)(local_cpu+0x100010)==
           0x718293a4b5c6d7e8ULL);
    memset(dma,0,sizeof(dma));
    memset(private_data,0,sizeof(private_data));
    fence.pDmaBuffer=dma;
    fence.DmaSize=sizeof(dma);
    fence.pDmaBufferPrivateData=private_data;
    fence.DmaBufferPrivateDataSize=sizeof(private_data);
    fence.MultipassOffset=0;
    fence.SignalMonitoredFence.MonitoredFenceGpuVa=0x2000ffc;
    expect_ok("R80 monitored fence split across logical PTEs",
              AdmissionGpuvaG3BuildPagingBuffer(&adapter,&fence));
    assert(AdmissionPagingRecordsValid(
        (ADMISSION_PAGING_RECORD *)private_data,2,64,
        2*sizeof(ADMISSION_PAGING_MARKER)));
    for (UINT i=0;i<2;i++)
      expect_ok("R80 split fence CPU execution",
          AdmissionG3ExecuteVirtualPaging(&adapter,
              &((ADMISSION_PAGING_RECORD *)private_data)[i]));
    assert(*(ULONGLONG *)(local_cpu+0x100ffc)==
           0x718293a4b5c6d7e8ULL);
  }
  {
    DXGK_PTE system_page={0};
    DXGKARG_BUILDPAGINGBUFFER system_update={0};
    unsigned char dma[256]={0}, private_data[512]={0};
    DXGKARG_BUILDPAGINGBUFFER transfer={0};
    system_page.Flags=0x1;
    system_page.PageAddress=system_ipa>>12;
    system_update.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;
    system_update.UpdatePageTable.hProcess=sys.hKmdProcess;
    system_update.UpdatePageTable.PageTableAddress.CpuVirtual=local_cpu+0xc000;
    system_update.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
    system_update.UpdatePageTable.PageTableLevel=0;
    system_update.UpdatePageTable.StartIndex=4;
    system_update.UpdatePageTable.NumPageTableEntries=1;
    system_update.UpdatePageTable.FirstPteVirtualAddress=0x2004000;
    system_update.UpdatePageTable.pPageTableEntries=&system_page;
    expect_ok("R80 system destination logical PTE",
              AdmissionGpuvaG3BuildPagingBuffer(&adapter,&system_update));
    transfer.Operation=DXGK_OPERATION_VIRTUAL_TRANSFER;
    transfer.hSystemContext=cc.hContext;
    transfer.pDmaBuffer=dma;
    transfer.DmaSize=sizeof(dma);
    transfer.pDmaBufferPrivateData=private_data;
    transfer.DmaBufferPrivateDataSize=sizeof(private_data);
    transfer.TransferVirtual.SourceVirtualAddress=0x2000000;
    transfer.TransferVirtual.DestinationVirtualAddress=0x2004000;
    transfer.TransferVirtual.TransferSizeInBytes=0x1000;
    transfer.TransferVirtual.TransferDirection=
        DXGK_MEMORY_TRANSFER_LOCAL_TO_SYSTEM;
    expect_ok("R80 TransferVirtual local to system build",
              AdmissionGpuvaG3BuildPagingBuffer(&adapter,&transfer));
    assert(AdmissionPagingRecordsValid(
        (ADMISSION_PAGING_RECORD *)private_data,1,64,
        sizeof(ADMISSION_PAGING_MARKER)));
    expect_ok("R80 TransferVirtual local to system CPU execution",
              AdmissionG3ExecuteVirtualPaging(&adapter,
                  (ADMISSION_PAGING_RECORD *)private_data));
    assert(memcmp(system_cpu,local_cpu+0x100000,0x1000)==0);
    {
      DXGKARG_BUILDPAGINGBUFFER wrong=transfer;
      wrong.TransferVirtual.TransferDirection=
          DXGK_MEMORY_TRANSFER_SYSTEM_TO_LOCAL;
      wrong.MultipassOffset=0;
      wrong.pDmaBuffer=dma;
      wrong.DmaSize=sizeof(dma);
      wrong.pDmaBufferPrivateData=private_data;
      wrong.DmaBufferPrivateDataSize=sizeof(private_data);
      assert(AdmissionGpuvaG3BuildPagingBuffer(&adapter,&wrong)==
             STATUS_INVALID_PARAMETER);
    }
    memset(local_cpu+0x108000,0,0x1000);
    memset(dma,0,sizeof(dma));
    memset(private_data,0,sizeof(private_data));
    transfer.pDmaBuffer=dma;
    transfer.DmaSize=sizeof(dma);
    transfer.pDmaBufferPrivateData=private_data;
    transfer.DmaBufferPrivateDataSize=sizeof(private_data);
    transfer.MultipassOffset=0;
    transfer.TransferVirtual.SourceVirtualAddress=0x2004000;
    transfer.TransferVirtual.DestinationVirtualAddress=0x2008000;
    transfer.TransferVirtual.TransferDirection=
        DXGK_MEMORY_TRANSFER_SYSTEM_TO_LOCAL;
    expect_ok("R80 TransferVirtual system to local build",
              AdmissionGpuvaG3BuildPagingBuffer(&adapter,&transfer));
    expect_ok("R80 TransferVirtual system to local CPU execution",
              AdmissionG3ExecuteVirtualPaging(&adapter,
                  (ADMISSION_PAGING_RECORD *)private_data));
    assert(memcmp(local_cpu+0x108000,system_cpu,0x1000)==0);
    {
      DXGK_PTE aperture_page={0};
      DXGKARG_BUILDPAGINGBUFFER aperture_update=system_update;
      aperture_page.Flags=0x21; /* valid writable segment1 */
      aperture_page.PageAddress=0x1; /* aperture offset 0x1000 */
      aperture_update.UpdatePageTable.StartIndex=5;
      aperture_update.UpdatePageTable.FirstPteVirtualAddress=0x2005000;
      aperture_update.UpdatePageTable.pPageTableEntries=&aperture_page;
      expect_ok("R80 aperture destination logical PTE",
          AdmissionGpuvaG3BuildPagingBuffer(&adapter,&aperture_update));
      memset(dma,0,sizeof(dma));
      memset(private_data,0,sizeof(private_data));
      transfer.pDmaBuffer=dma;
      transfer.DmaSize=sizeof(dma);
      transfer.pDmaBufferPrivateData=private_data;
      transfer.DmaBufferPrivateDataSize=sizeof(private_data);
      transfer.MultipassOffset=0;
      transfer.TransferVirtual.SourceVirtualAddress=0x2000000;
      transfer.TransferVirtual.DestinationVirtualAddress=0x2005000;
      transfer.TransferVirtual.TransferDirection=
          DXGK_MEMORY_TRANSFER_LOCAL_TO_SYSTEM;
      expect_ok("R80 aperture transfer build",
          AdmissionGpuvaG3BuildPagingBuffer(&adapter,&transfer));
      expect_ok("R80 aperture transfer CPU execution",
          AdmissionG3ExecuteVirtualPaging(&adapter,
              (ADMISSION_PAGING_RECORD *)private_data));
      assert(memcmp(system_cpu+0x1000,local_cpu+0x100000,0x1000)==0);
    }
  }
  }
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
  if (r79) ((ADMISSION_G3_PROCESS *)sys.hKmdProcess)->Graph.Slot=1u;
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
  if (r79) {
    UINT flushes=broker.flush_commands;
    expect_ok("R79 owned noncurrent table",
        AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush));
    assert(broker.flush_commands==flushes);
    assert(last_flush_receipt.Branch==6);
  } else {
    assert(AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush)==STATUS_INVALID_PARAMETER);
    assert(last_flush_receipt.Branch==4);
  }
  if (r79) {
    flush.FlushTlb.RootPageTableAddress.SegmentOffset=0x30000;
    assert(AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush)==STATUS_INVALID_PARAMETER);
    assert(last_flush_receipt.Branch==4);
  }
  flush.FlushTlb.RootPageTableAddress=root.Address;
  if (r79) {
    DXGKARG_SETROOTPAGETABLE relocated=root;
    UINT flushes=broker.flush_commands;
    ADMISSION_G3_TABLE_SHADOW *shadow=
        ((ADMISSION_G3_PROCESS *)sys.hKmdProcess)->TableShadows;
    while (shadow && shadow->OriginalIpa!=local_ipa) shadow=shadow->Next;
    assert(shadow);
    relocated.Address.SegmentOffset=0x20000;
    AdmissionDdiSetRootPageTable(&adapter,&relocated);
    expect_ok("R79 flush old owned root after relocate",
        AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush));
    assert(broker.flush_commands==flushes);
    assert(last_flush_receipt.Branch==6);
    flush.FlushTlb.RootPageTableAddress.SegmentOffset=
        shadow->BrokerIpa-local_ipa;
    expect_ok("R79 flush old shadow root after relocate",
        AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush));
    assert(broker.flush_commands==flushes);
    assert(last_flush_receipt.Branch==6);
    flush.FlushTlb.RootPageTableAddress=relocated.Address;
    ((ADMISSION_G3_PROCESS *)sys.hKmdProcess)->Graph.Created=0;
    expect_ok("R79 graph not created",
        AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush));
    assert(broker.flush_commands==flushes);
    assert(last_flush_receipt.Branch==5);
    ((ADMISSION_G3_PROCESS *)sys.hKmdProcess)->Graph.Created=1;
    AdmissionDdiSetRootPageTable(&adapter,&root);
  }
  if (r79) {
    expect_ok("R79 foreign CreateProcess",AdmissionDdiCreateProcess(&adapter,&user));
    flush.FlushTlb.hProcess=user.hKmdProcess;
    flush.FlushTlb.RootPageTableAddress=root.Address;
    assert(AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush)==STATUS_INVALID_PARAMETER);
    assert(last_flush_receipt.Branch==4);
    flush.FlushTlb.hProcess=(HANDLE)0x1234;
    assert(AdmissionGpuvaG3BuildPagingBuffer(&adapter,&flush)==STATUS_INVALID_PARAMETER);
    assert(last_flush_receipt.Branch==4);
    expect_ok("R79 foreign DestroyProcess",AdmissionDdiDestroyProcess(&adapter,user.hKmdProcess));
  }
  if (r79 && getenv("G3_REPLAY_SELF_TABLE_BACKING")==NULL) {
  {
    /* EXP796 FlushTlb interval is a projection, not the uncaptured Fill input. */
    const UINT page_count=0x580000u/0x1000u;
    DXGK_PTE *span=calloc(page_count,sizeof(*span));
    DXGKARG_BUILDPAGINGBUFFER mapping={0}, fill={0};
    UINT passes=0;
    assert(span);
    for (UINT i=0;i<page_count;i++) {
      span[i].Flags=0x41;
      span[i].PageAddress=0x200+i;
    }
    mapping.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;
    mapping.UpdatePageTable.hProcess=sys.hKmdProcess;
    mapping.UpdatePageTable.PageTableAddress.CpuVirtual=local_cpu+0xc000;
    mapping.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
    mapping.UpdatePageTable.PageTableLevel=0;
    mapping.UpdatePageTable.StartIndex=0x30;
    mapping.UpdatePageTable.NumPageTableEntries=page_count;
    mapping.UpdatePageTable.FirstPteVirtualAddress=0x2030000;
    mapping.UpdatePageTable.pPageTableEntries=span;
    expect_ok("R80 projected EXP796 scratch range",
              AdmissionGpuvaG3BuildPagingBuffer(&adapter,&mapping));
    free(span);
    fill.Operation=DXGK_OPERATION_VIRTUAL_FILL;
    fill.hSystemContext=cc.hContext;
    fill.FillVirtual.DestinationVirtualAddress=0x2030000;
    fill.FillVirtual.FillSizeInBytes=0x580000;
    fill.FillVirtual.FillPattern=0x3198b746;
    do {
      unsigned char dma[0x1000]={0};
      unsigned char private_data[0x1000]={0};
      NTSTATUS status;
      UINT records;
      fill.pDmaBuffer=dma;
      fill.DmaSize=sizeof(dma);
      fill.pDmaBufferPrivateData=private_data;
      fill.DmaBufferPrivateDataSize=sizeof(private_data);
      fill.DmaBufferWriteOffset=0;
      status=AdmissionGpuvaG3BuildPagingBuffer(&adapter,&fill);
      assert(status==STATUS_SUCCESS ||
             status==STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);
      records=((unsigned char *)fill.pDmaBufferPrivateData-private_data)/
          sizeof(ADMISSION_PAGING_RECORD);
      assert(records>0 && records<=64);
      assert(AdmissionPagingRecordsValid(
          (ADMISSION_PAGING_RECORD *)private_data,records,64,
          records*sizeof(ADMISSION_PAGING_MARKER)));
      if (passes==0) {
        DXGKARG_SUBMITCOMMANDVIRTUAL submission={0};
        submission.hContext=cc.hContext;
        submission.DmaBufferVirtualAddress=0x2021000;
        submission.DmaBufferSize=records*sizeof(ADMISSION_PAGING_MARKER);
        submission.pDmaBufferPrivateData=private_data;
        submission.DmaBufferPrivateDataSize=
            records*sizeof(ADMISSION_PAGING_RECORD);
        submission.SubmissionFenceId=1;
        submission.Flags.Paging=1;
        assert(records==34 && submission.DmaBufferSize==0x220 &&
               submission.DmaBufferPrivateDataSize==0xff0);
        expect_ok("EXP797 virtual paging Submit packet",
            AdmissionGpuvaG3SubmitVirtualPaging(&adapter,
                (ADMISSION_RENDER_CONTEXT *)cc.hContext,&submission));
        assert(replay_paging_submits==1 &&
               replay_paging_submit_bytes==0xff0 &&
               replay_paging_submit_fence==1);
        submission.Flags.Value=0;
        assert(AdmissionGpuvaG3SubmitVirtualPaging(&adapter,
            (ADMISSION_RENDER_CONTEXT *)cc.hContext,&submission)==
            STATUS_INVALID_PARAMETER);
        submission.Flags.Paging=1;
        --submission.DmaBufferPrivateDataSize;
        assert(AdmissionGpuvaG3SubmitVirtualPaging(&adapter,
            (ADMISSION_RENDER_CONTEXT *)cc.hContext,&submission)==
            STATUS_INVALID_PARAMETER);
      }
      for (UINT i=0;i<records;i++)
        expect_ok("R80 projected EXP796 Fill CPU execution",
            AdmissionG3ExecuteVirtualPaging(&adapter,
                &((ADMISSION_PAGING_RECORD *)private_data)[i]));
      ++passes;
      if (status==STATUS_SUCCESS) break;
      assert(passes<64);
    } while (1);
    assert(passes>1 && fill.MultipassOffset==0x580000u);
    for (UINT i=0;i<0x580000u;i++)
      assert(local_cpu[0x200000+i]==
          ((unsigned char *)&fill.FillVirtual.FillPattern)[i&3u]);
  }
  }
  expect_ok("DestroyContext",AdmissionDdiDestroyContext(cc.hContext));
  expect_ok("DestroyProcess",AdmissionDdiDestroyProcess(&adapter,sys.hKmdProcess));
  expect_ok("Learn ordinary CreateProcess",AdmissionDdiCreateProcess(&adapter,&user));
  expect_ok("ordinary DestroyProcess",AdmissionDdiDestroyProcess(&adapter,user.hKmdProcess));
  free(local_cpu);
  puts("G3 VidMm host replay: all recorded and projected inputs passed via real m1n1 broker dispatch");
  return 0;
}
