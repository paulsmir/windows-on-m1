static UINT r168_live_backings(void) {
  UINT count=0;
  for(UINT index=0;index<HV_AGX_GPUVA_V5_BACKINGS;++index)
    if(gpuva_v5.backings[index].live) ++count;
  for(UINT owner=0;owner<HV_AGX_GPUVA_V5_PROCESSES;++owner)
    for(UINT word=0;word<HV_AGX_GPUVA_V5_LOCAL_PAGES/64u;++word)
      count+=(UINT)__builtin_popcountll(gpuva_v5.local_grants[owner][word]);
  return count;
}

static void r168_capacity_cases(void) {
  ADMISSION_CONTEXT adapter={0}; ADMISSION_G3_STATE state={0};
  REPLAY_BROKER broker={0}; DXGKARG_CREATEPROCESS create={0};
  APPLE_AGX_GPUVA_V5_IO io={&broker,ReplayWrite64,ReplayRead64,ReplayWrite32,ReplayBarrier, 0};
  DXGK_PTE entries[4]={0}; DXGKARG_BUILDPAGINGBUFFER args={0};
  local_ipa=0x8e0000000ULL; local_bytes=0x40000000ULL;
  vidmm_local_bytes=0x3b800000ULL;
  assert(posix_memalign((void **)&local_cpu,0x4000,(size_t)local_bytes)==0);
  memset(local_cpu,0,(size_t)local_bytes);
  ReplayBrokerInit(&broker); InitializeListHead(&state.Processes);
  if(!getenv("G3_REPLAY_R168_BROKER_REFUSAL"))
    assert(hv_agx_gpuva_v5_configure_local(&gpuva_v5,local_ipa,
        vidmm_local_bytes,UINT64_C(0x4000000))==HV_AGX_GPUVA_V5_OK);
  assert(AppleAgxGpuvaV5ClientInit(&state.Client,&io));
  {AGX_GPUVA_V5_REQUEST request={0}; AGX_GPUVA_V5_RESPONSE response={0};
    request.Command=AGX_GPUVA_V5_CREATE;
    assert(AppleAgxGpuvaV5ClientCall(&state.Client,&request,&response));}
  adapter.Started=TRUE; adapter.GpuvaG3State=&state; state.Adapter=&adapter;
  adapter.ObjectAdapter=&adapter;
  adapter.Interface=(DXGKRNL_INTERFACE){(HANDLE)0x1234,ReplayReserveVa};
  expect_ok("R168 create",AdmissionDdiCreateProcess(&adapter,&create));
  ADMISSION_G3_PROCESS *process=create.hKmdProcess;
  args.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;
  args.UpdatePageTable.hProcess=process;
  args.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_GPU_PHYSICAL;
  args.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentId=2;
  args.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentOffset=0x3b700000ULL;
  args.UpdatePageTable.NumPageTableEntries=4;
  args.UpdatePageTable.pPageTableEntries=entries;
  expect_ok("R168 empty leaf",AdmissionGpuvaG3BuildPagingBuffer(&adapter,&args));
  if(getenv("G3_REPLAY_R168_LIFETIME")) {
    /* EXP1125: an unmapped shared local grant stays registered (a broker
     * bitmap bit, no capacity) so VidMm's next surface on the same pages
     * skips REGISTER/REVOKE; at most APPLE_AGX_GPUVA_G3_RETAINED_LOCAL are
     * kept per process and process destruction revokes all of them. */
    const UINT cycles=APPLE_AGX_GPUVA_G3_RETAINED_LOCAL+8u;
    UINT calls;
    for(UINT cycle=0;cycle<cycles;++cycle) {
      UINT retained=cycle<APPLE_AGX_GPUVA_G3_RETAINED_LOCAL ?
          cycle : APPLE_AGX_GPUVA_G3_RETAINED_LOCAL;
      for(UINT page=0;page<4;++page) {
        entries[page].Flags=0x41;
        entries[page].PageAddress=0x1000u+cycle*4u+page;
      }
      expect_ok("R168 local map",AdmissionGpuvaG3BuildPagingBuffer(&adapter,&args));
      assert(r168_live_backings()==retained+1u);
      RtlZeroMemory(entries,sizeof(entries));
      args.UpdatePageTable.Flags.NotifyEviction=cycle&1u;
      expect_ok("R168 local unmap",AdmissionGpuvaG3BuildPagingBuffer(&adapter,&args));
      assert(r168_live_backings()==(cycle<APPLE_AGX_GPUVA_G3_RETAINED_LOCAL ?
          cycle+1u : APPLE_AGX_GPUVA_G3_RETAINED_LOCAL));
      assert(process->Graph.RetainedLocal==r168_live_backings());
      args.UpdatePageTable.Flags.NotifyEviction=0u;
    }
    /* Remapping retained pages publishes the leaf with one broker call. */
    for(UINT page=0;page<4;++page) {
      entries[page].Flags=0x41; entries[page].PageAddress=0x1000u+page;
    }
    calls=broker.commands;
    expect_ok("R168 retained remap",AdmissionGpuvaG3BuildPagingBuffer(&adapter,&args));
    assert(broker.commands==calls+1u);
    assert(process->Graph.RetainedLocal==APPLE_AGX_GPUVA_G3_RETAINED_LOCAL-1u);
    RtlZeroMemory(entries,sizeof(entries));
    calls=broker.commands;
    expect_ok("R168 retained unmap",AdmissionGpuvaG3BuildPagingBuffer(&adapter,&args));
    assert(broker.commands==calls+1u);
    assert(r168_live_backings()==APPLE_AGX_GPUVA_G3_RETAINED_LOCAL);
    expect_ok("R168 destroy",AdmissionDdiDestroyProcess(&adapter,process));
    assert(r168_live_backings()==0u);
    puts("R168 local map/unmap/eviction lifetime: PASS (bounded retained local grants; zero retained grants after destroy)");
  } else {
    UINT capacity=HV_AGX_GPUVA_V5_BACKINGS+
        (getenv("G3_REPLAY_R168_BROKER_REFUSAL")?0u:1u);
    for(UINT page=0;page<capacity;++page)
      assert(hv_agx_gpuva_v5_register_shared_backing(&gpuva_v5,
          process->Graph.ProcessId,process->Graph.ProcessGeneration,process->Graph.SharedBackingGeneration,
          local_ipa+0x01000000ULL+(ULONGLONG)page*0x4000ULL)==HV_AGX_GPUVA_V5_OK);
    assert(r168_live_backings()==capacity);
    for(UINT page=0;page<4;++page) {
      entries[page].Flags=0x41; entries[page].PageAddress=0x25000u+page;
    }
    NTSTATUS status=AdmissionGpuvaG3BuildPagingBuffer(&adapter,&args);
    fprintf(stderr,"R168 local capacity: status=%08x branch=%u broker=%u uncertain=%u live=%u\n",
        (unsigned)status,last_paging_failure.Branch,last_paging_failure.GraphLastStatus,
        last_paging_failure.GraphUncertain,r168_live_backings());
    if(getenv("G3_REPLAY_R168_BROKER_REFUSAL")) {
      assert(status==STATUS_GRAPHICS_ALLOCATION_BUSY);
      assert(last_paging_failure.Branch==7u);
      assert(last_paging_failure.GraphLastStatus==HV_AGX_GPUVA_V5_CAPACITY);
      assert(gpuva_v5.tables[0].entries[0]==0u);
      puts("R168 broker refusal returns supported busy without publication: PASS");
      free(local_cpu);local_cpu=NULL;return;
    }
    expect_ok("R168 advertised local residency beyond old128MiB grant bound",status);
    puts("R168 local capacity: PASS");
  }
  free(local_cpu); local_cpu=NULL;
}
