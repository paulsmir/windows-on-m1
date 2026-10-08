/* EXP854B: measured empty current root reused as a level-1 table.
 * The earlier 4K/64K transitions are coverage, not a claimed captured trace. */
static APPLE_AGX_GPUVA_G3_GRAPH *r155_graph;
static void r155_end_job(void) {
  if(r155_graph && r155_graph->JobInFlight) assert(AppleAgxGpuvaG3GraphEndJob(r155_graph));
}
static void r135_root_reuse_cases(void) {
  ADMISSION_CONTEXT a={0}; ADMISSION_G3_STATE state={0}; REPLAY_BROKER b={0};
  APPLE_AGX_GPUVA_V5_IO io={&b,ReplayWrite64,ReplayRead64,ReplayWrite32,ReplayBarrier,NULL};
  ADMISSION_DEVICE device={0}; DXGKARG_CREATECONTEXT cc={0};
  DXGKARG_SETROOTPAGETABLE setroot={0}; DXGKARG_BUILDPAGINGBUFFER reuse={0};
  DXGK_PTE pte={0}, zero={0}, parents[2048]={0};
  APPLE_AGX_GPUVA_G3_NODE *node;
  assert(posix_memalign((void **)&local_cpu,0x4000,(size_t)local_bytes)==0);
  memset(local_cpu,0,(size_t)local_bytes);
  ReplayBrokerInit(&b); InitializeListHead(&state.Processes);
  assert(AppleAgxGpuvaV5ClientInit(&state.Client,&io));
  { AGX_GPUVA_V5_REQUEST r={0}; AGX_GPUVA_V5_RESPONSE reply={0};
    r.Command=AGX_GPUVA_V5_CREATE;
    assert(AppleAgxGpuvaV5ClientCall(&state.Client,&r,&reply)); }
  a.Started=TRUE; a.GpuvaG3State=&state; state.Adapter=&a;
  a.ObjectAdapter=&a;
  ADMISSION_G3_PROCESS *p=sys_process(&a,0x10000,0);
  ULONGLONG former_root=p->Graph.RootIpa;
  device.Object.Magic=ADMISSION_OBJECT_DEVICE_MAGIC;
  device.Object.Adapter=&a.ObjectAdapter; device.GpuvaG3Process=p;
  cc.Flags.Value=4; cc.EngineAffinity=1; cc.hContext=(HANDLE)1;
  expect_ok("R135 context",AdmissionDdiCreateContext(&device,&cc));
  setroot.hContext=cc.hContext; setroot.NumEntries=8;
  setroot.Address.SegmentId=ADMISSION_MEMORY_LOCAL_SEGMENT;
  setroot.Address.SegmentOffset=0x10000;
  AdmissionDdiSetRootPageTable(&a,&setroot);
  assert(!((ADMISSION_RENDER_CONTEXT *)cc.hContext)->GpuvaG3Poisoned);
  if(getenv("G3_REPLAY_R137_PRIVATE")) {
    p->PrivateMiddleIpa=local_ipa+(40ULL<<20);
    p->PrivateLeafIpa=p->PrivateMiddleIpa+0x4000;
    assert(AppleAgxGpuvaG3GraphRegisterTable(&p->Graph,p->PrivateMiddleIpa,1));
    assert(AppleAgxGpuvaG3GraphRegisterTable(&p->Graph,p->PrivateLeafIpa,2));
    assert(AppleAgxGpuvaG3GraphUpdateLeafBacking(&p->Graph,p->PrivateLeafIpa,0,
        p->PrivateMiddleIpa+0x10000,true,AppleAgxGpuvaG3PrivateBacking));
    assert(AppleAgxGpuvaG3GraphAttachPrivate(&p->Graph,p->PrivateVa,
        p->PrivateMiddleIpa,p->PrivateLeafIpa));
  }
  /* Different leaf allocations, same 32-MiB VA span, in both directions. */
  for(UINT wide=0;wide<3;++wide) {
    UINT use64=wide==1, leaf_offset=use64?0x40000:0x18000;
    pte.Flags=1; pte.PageAddress=system_ipa>>12;
    expect_ok("R135 leaf format",sys_update(&a,p,local_cpu+leaf_offset,
        0,use64?1:16,use64?1:16,&pte,use64,1));
    pte.Flags=0x41 | (use64?0x20000:0);
    pte.PageTableAddress=leaf_offset>>12;
    expect_ok("R135 select leaf format",sys_update(&a,p,local_cpu+0x14000,
        1,0,1,&pte,0,0));
  }
  reuse.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;
  reuse.UpdatePageTable.hProcess=p;
  reuse.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_GPU_PHYSICAL;
  reuse.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentId=2;
  reuse.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentOffset=0x10000;
  reuse.UpdatePageTable.PageTableLevel=1;
  reuse.UpdatePageTable.NumPageTableEntries=2048;
  reuse.UpdatePageTable.Flags.InitialUpdate=1;
  reuse.UpdatePageTable.pPageTableEntries=parents;
  /* A real live root remains protected even under InitialUpdate. */
  assert(!NT_SUCCESS(AdmissionGpuvaG3BuildPagingBuffer(&a,&reuse)));
  assert(p->Graph.RootIpa==former_root && !p->Graph.Uncertain);
  expect_ok("R135 clear middle",sys_update(&a,p,local_cpu+0x14000,1,0,1,&zero,0,0));
  expect_ok("R135 clear root",sys_update(&a,p,local_cpu+0x10000,2,0,1,&zero,0,0));
  if(p->PrivateLeafIpa)
    assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,p->PrivateVa,0x4000));
  else assert(!p->Graph.Parents && !p->Graph.Leaves);
  assert(!p->Graph.JobInFlight);
  /* Ordinary updates must not reinterpret a still-current empty root. */
  reuse.UpdatePageTable.Flags.InitialUpdate=0;
  assert(!NT_SUCCESS(AdmissionGpuvaG3BuildPagingBuffer(&a,&reuse)));
  assert(p->Graph.RootIpa==former_root);
  reuse.UpdatePageTable.Flags.InitialUpdate=1;
  assert(AppleAgxGpuvaG3GraphBeginJob(&p->Graph,1));
  /* R155: a job that never completes still bounds the wait (TDR owns hangs). */
  replay_delay_calls=0;
  assert(AdmissionGpuvaG3BuildPagingBuffer(&a,&reuse)==STATUS_DEVICE_BUSY);
  assert(replay_delay_calls>=1);
  assert(p->Graph.RootIpa==former_root);
  assert(AppleAgxGpuvaG3GraphEndJob(&p->Graph));
  /* R155 (EXP868 0x10E/0xB): VidMm updates a leaf of a process whose native
   * job is in flight. Paging executes only after that job's fence, so the
   * build waits (lock released) for joined completion instead of returning
   * STATUS_DEVICE_BUSY, which DxgkDdiBuildPagingBuffer may never return. */
  r155_graph=&p->Graph; replay_delay_hook=r155_end_job; replay_delay_calls=0;
  assert(AppleAgxGpuvaG3GraphBeginJob(&p->Graph,1));
  { DXGK_PTE leaf={0}; leaf.Flags=1; leaf.PageAddress=system_ipa>>12;
    expect_ok("R155 update during in-flight job",
        sys_update(&a,p,local_cpu+0x18000,0,0,16,&leaf,0,1)); }
  assert(replay_delay_calls>=1 && !p->Graph.JobInFlight && !p->Graph.LeaseToken);
  replay_delay_hook=NULL; r155_graph=NULL;
  /* Broker-owned slot exclusion remains effective even if local slot metadata
   * is stale. Failure is returned, not hidden as a successful paging update. */
  { uint64_t token=0;
    assert(hv_agx_gpuva_v5_lease(&gpuva_v5,p->Graph.ProcessId,
        p->Graph.ProcessGeneration,1,&token)==HV_AGX_GPUVA_V5_OK);
    assert(!NT_SUCCESS(AdmissionGpuvaG3BuildPagingBuffer(&a,&reuse)));
    assert(p->Graph.RootIpa==former_root && !p->Graph.Uncertain);
    assert(last_paging_failure.GraphLastStatus==HV_AGX_GPUVA_V5_BUSY);
    if(p->PrivateLeafIpa)
      assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,p->PrivateVa,0x4000));
    assert(hv_agx_gpuva_v5_release(&gpuva_v5,1,token)==HV_AGX_GPUVA_V5_OK);
  }
  /* Populating the parking root is an ownership conflict, not permission to
   * redirect the process onto that unrelated graph. */
  { ULONGLONG middle=0;
    UINT ordinary_root_index=(p->PrivateVa>>36)==0 ? 1u : 0u;
    expect_ok("R135 middle identity",AdmissionGpuvaG3BrokerTable(p,
        local_ipa+0x14000,FALSE,&middle));
    assert(AppleAgxGpuvaG3GraphUpdateParent(&p->Graph,p->BootstrapIpa,ordinary_root_index,middle));
    ULONGLONG before_generation=p->Graph.MappingGeneration;
    assert(!NT_SUCCESS(AdmissionGpuvaG3BuildPagingBuffer(&a,&reuse)));
    assert(p->Graph.RootIpa==former_root);
    if(p->PrivateLeafIpa) {
      assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,p->PrivateVa,0x4000));
      assert(p->Graph.MappingGeneration==before_generation);
    }
    assert(AppleAgxGpuvaG3GraphUpdateParent(&p->Graph,p->BootstrapIpa,ordinary_root_index,0));
  }
  if(getenv("G3_REPLAY_R135_ALLOC")) {
    void *(*allocate)(void *,unsigned long long)=p->Graph.Allocate;
    p->Graph.Allocate=sys_no_nodes;
    assert(!NT_SUCCESS(AdmissionGpuvaG3BuildPagingBuffer(&a,&reuse)));
    assert(p->Graph.RootIpa==p->BootstrapIpa && !p->Graph.Uncertain);
    for(node=p->Graph.Tables;node && node->Ipa!=former_root;node=node->Next) {}
    assert(node && node->Level==0); /* Metadata reserved before retirement. */
    p->Graph.Allocate=allocate;
  }
  /* Source-backed descendant sequence; the saved failure itself is InitialUpdate
   * of level1, count2048, first PTE Flags41 (4K child), not Use64KBPages. */
  parents[0].Flags=0x41; parents[0].PageTableAddress=0x18;
  NTSTATUS status=AdmissionGpuvaG3BuildPagingBuffer(&a,&reuse);
  if(!NT_SUCCESS(status)) {
    assert(status==STATUS_INVALID_ADDRESS);
    assert(last_paging_failure.Branch==2 && last_paging_failure.Level==1);
    assert(last_paging_failure.TableAddBranch==2 && last_paging_failure.GraphLastStatus==0);
  }
  expect_ok("EXP854B empty current root reused as level1",status);
  assert(p->Graph.RootIpa==p->BootstrapIpa && p->Graph.RootIpa!=former_root);
  for(node=p->Graph.Tables;node && node->Ipa!=former_root;node=node->Next) {}
  assert(node && node->Level==1);
  assert(((ADMISSION_RENDER_CONTEXT *)cc.hContext)->GpuvaG3RootIpa==former_root);
  assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,0x10000,1));
  /* Bind a fresh root through the actual DDI and prove its translated child. */
  pte.Flags=0x41; pte.PageTableAddress=0x10;
  expect_ok("R135 new root",sys_update(&a,p,local_cpu+0x50000,2,0,1,&pte,0,0));
  setroot.Address.SegmentOffset=0x50000;
  AdmissionDdiSetRootPageTable(&a,&setroot);
  assert(!((ADMISSION_RENDER_CONTEXT *)cc.hContext)->GpuvaG3Poisoned);
  pte.Flags=1;pte.PageAddress=system_ipa>>12;
  expect_ok("R135 map after rebind",sys_update(&a,p,local_cpu+0x18000,0,1,1,&pte,1,0));
  assert(AppleAgxGpuvaG3GraphContainsRangeAccess(&p->Graph,0x10000,0x10000,true));
  if(p->PrivateLeafIpa)
    assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,p->PrivateVa,0x4000));
  /* Otherwise-admissible job: only its root identity differs on rejection.
   * The established replay shim models the output view; real DMA translation,
   * root gate, broker lease/job and completion are exercised here. */
  ADMISSION_RENDER_CONTEXT *context=cc.hContext;
  context->GpuvaG3DmaBufferVa=0x10000;
  context->GpuvaG3DmaBufferBytes=0x1000;
  ULONGLONG rebound_root=context->GpuvaG3RootIpa;
  expect_ok("R135 rebound job",AdmissionGpuvaG3BeginJob(&a,context,91));
  assert(AdmissionGpuvaG3CompleteJob(&a,91));
  context->GpuvaG3RootIpa=former_root;
  assert(!NT_SUCCESS(AdmissionGpuvaG3BeginJob(&a,context,92)));
  assert(!state.ActiveProcess && !p->Graph.JobInFlight && !p->Graph.LeaseToken);
  context->GpuvaG3RootIpa=rebound_root;
  expect_ok("R135 restored root job",AdmissionGpuvaG3BeginJob(&a,context,92));
  assert(AdmissionGpuvaG3CompleteJob(&a,92));
  assert(!AppleAgxGpuvaG3GraphRegisterTable(&p->Graph,p->Graph.RootIpa,1));
  expect_ok("R135 destroy context",AdmissionDdiDestroyContext(cc.hContext));
  expect_ok("R135 destroy process",AdmissionDdiDestroyProcess(&a,p));
  free(local_cpu);local_cpu=NULL;
  puts("R135 empty root reuse: PASS");
}
