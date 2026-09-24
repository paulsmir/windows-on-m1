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
  update(&adapter,sys.hKmdProcess,0,local_cpu+0x4000,8192,0,flags,
         zeros ? zeros : &empty,
         "EXP780 level0 Repeat InitialUpdate DMA pointers");
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
  update(&adapter,sys.hKmdProcess,0,local_cpu+0xc000,32,0x2000000,
         flags,map32,"EXP784C level0 Count32 Flags0 projection");
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
  expect_ok("DestroyContext",AdmissionDdiDestroyContext(cc.hContext));
  expect_ok("DestroyProcess",AdmissionDdiDestroyProcess(&adapter,sys.hKmdProcess));
  expect_ok("Learn ordinary CreateProcess",AdmissionDdiCreateProcess(&adapter,&user));
  expect_ok("ordinary DestroyProcess",AdmissionDdiDestroyProcess(&adapter,user.hKmdProcess));
  free(local_cpu);
  puts("G3 VidMm host replay: all recorded and projected inputs passed via real m1n1 broker dispatch");
  return 0;
}
