static void r137_reservation_cases(void) {
  ADMISSION_CONTEXT a={0}; ADMISSION_G3_STATE state={0}; REPLAY_BROKER b={0};
  APPLE_AGX_GPUVA_V5_IO io={&b,ReplayWrite64,ReplayRead64,ReplayWrite32,ReplayBarrier, 0};
  assert(posix_memalign((void **)&local_cpu,0x4000,(size_t)local_bytes)==0);
  memset(local_cpu,0,(size_t)local_bytes);
  ReplayBrokerInit(&b); InitializeListHead(&state.Processes);
  assert(AppleAgxGpuvaV5ClientInit(&state.Client,&io));
  { AGX_GPUVA_V5_REQUEST r={0}; AGX_GPUVA_V5_RESPONSE reply={0};
    r.Command=AGX_GPUVA_V5_CREATE;
    assert(AppleAgxGpuvaV5ClientCall(&state.Client,&r,&reply)); }
  a.Started=TRUE; a.GpuvaG3State=&state; state.Adapter=&a; a.ObjectAdapter=&a;
  a.Interface=(DXGKRNL_INTERFACE){(HANDLE)0x1234,ReplayReserveVa};
  for(unsigned system=0;system<2;++system) {
    DXGKARG_CREATEPROCESS args={0};
    args.Flags.SystemProcess=system; args.hDxgkProcess=(HANDLE)(uintptr_t)(system+100);
    unsigned calls=reserve_calls;
    expect_ok("R137 reservation",AdmissionDdiCreateProcess(&a,&args));
    assert(reserve_calls==calls+1 && reserve_process==args.hDxgkProcess);
    ADMISSION_G3_PROCESS *p=args.hKmdProcess;
    assert(p->PrivateVa==reserve_base && p->DxgkProcess==args.hDxgkProcess);
    /* G2 HW2 receipt: the reservation and its VA */
    assert(replay_reserve_receipt[0]==1u && replay_reserve_receipt[1]==system+1u &&
           replay_reserve_receipt[3]==0u &&
           replay_reserve_receipt[4]==(ULONG)APPLE_AGX_G4_PRIVATE_VA_BASE &&
           replay_reserve_receipt[5]==(ULONG)(APPLE_AGX_G4_PRIVATE_VA_BASE>>32));
    /* G2 HW2b: a GPU context gives an idle user process its present arena:
     * an extent of the private range in the USC window, kernels installed,
     * pages published; not while a job runs; never for the paging process. */
    {
      ADMISSION_DEVICE device={0}; ADMISSION_RENDER_CONTEXT context={0};
      ADMISSION_BACKEND_MEMORY_VIEW view;
      unsigned receipts=replay_arena_receipts;
      device.Object.Magic=ADMISSION_OBJECT_DEVICE_MAGIC;
      device.Object.Adapter=&a.ObjectAdapter; device.GpuvaG3Process=p;
      context.Object.Magic=ADMISSION_OBJECT_CONTEXT_MAGIC;
      context.Object.Device=&device.Object;
      expect_ok("HW2b attach context",AdmissionGpuvaG3AttachContext(&context,&device));
      if(system) {
        assert(AdmissionGpuvaG3EnsurePresentArena(&context)==STATUS_DEVICE_BUSY);
        assert(!p->PresentArena.Generation && replay_arena_receipts==receipts+1u &&
               replay_arena_receipt[3]==10u && replay_arena_receipt[5]==0u);
      } else {
        p->Graph.JobInFlight=true;
        assert(AdmissionGpuvaG3EnsurePresentArena(&context)==STATUS_DEVICE_BUSY);
        p->Graph.JobInFlight=false;
        assert(!p->PresentArena.Generation && replay_arena_receipt[2]>=1u &&
               replay_arena_receipt[3]==10u);
        expect_ok("HW2b present arena",AdmissionGpuvaG3EnsurePresentArena(&context));
        ULONGLONG va=p->PrivateVa+p->PresentArena.VaOffset;
        assert(p->PresentArena.Generation &&
               p->PresentArena.Bytes==APPLE_AGX_PRESENT_ARENA_BYTES && !(va&0xffffULL) &&
               va>=APPLE_AGX_G4_USC_EXECUTION_BASE &&
               va+APPLE_AGX_PRESENT_ARENA_BYTES<=APPLE_AGX_G4_USC_EXECUTION_BASE+
                   APPLE_AGX_G4_USC_WINDOW_BYTES);
        assert(replay_arena_receipt[0]==1u && replay_arena_receipt[1]==1u &&
               replay_arena_receipt[3]==0u && replay_arena_receipt[4]==0u &&
               replay_arena_receipt[5]==(ULONG)va && replay_arena_receipt[6]==(ULONG)(va>>32));
        expect_ok("HW2b private view",AdmissionMemoryRuntimePrivateView(&a,&view));
        assert(!memcmp((unsigned char *)view.CpuAddress+p->PresentArena.Offset+
                           APPLE_AGX_PRESENT_COPY_OFFSET,
                       AppleAgxPresentCopy.Binary,AppleAgxPresentCopy.BinaryBytes));
        assert(!memcmp((unsigned char *)view.CpuAddress+p->PresentArena.Offset+
                           APPLE_AGX_PRESENT_FILL_OFFSET,
                       AppleAgxPresentFill.Binary,AppleAgxPresentFill.BinaryBytes));
        assert(AppleAgxGpuvaG3GraphContainsRangeAccess(&p->Graph,va,
                   APPLE_AGX_PRESENT_ARENA_BYTES,true));
        /* made once per process */
        receipts=replay_arena_receipts;
        APPLE_AGX_G3_PRIVATE_EXTENT kept=p->PresentArena;
        expect_ok("HW2b present arena again",AdmissionGpuvaG3EnsurePresentArena(&context));
        assert(replay_arena_receipts==receipts && !memcmp(&kept,&p->PresentArena,sizeof(kept)));
      }
      AdmissionGpuvaG3DetachContext(&context);
    }
    if(getenv("G3_REPLAY_R137_ESCAPE") && !system) {
      ADMISSION_DEVICE device={0}; ADMISSION_RENDER_CONTEXT context={0};
      device.Object.Magic=ADMISSION_OBJECT_DEVICE_MAGIC;
      device.Object.Adapter=&a.ObjectAdapter; device.GpuvaG3Process=p;
      context.Object.Magic=ADMISSION_OBJECT_CONTEXT_MAGIC;
      context.Object.Device=&device.Object; context.Win32Transport=TRUE;
      expect_ok("R137 attach escape context",AdmissionGpuvaG3AttachContext(&context,&device));
      APPLE_AGX_G3_PRIVATE_REQUEST q={0};
      q.Magic=APPLE_AGX_G3_PRIVATE_MAGIC;q.Version=1;q.Bytes=sizeof(q);
      q.Operation=APPLE_AGX_G3_PRIVATE_ACQUIRE;
      q.Width=2560;q.Height=1600;q.UtileWidth=q.UtileHeight=16;q.Layers=q.Samples=1;
      DXGKARG_ESCAPE e={0}; e.hDevice=&device;e.hContext=&context;
      e.hKmdProcessHandle=p;e.pPrivateDriverData=&q;e.PrivateDriverDataSize=sizeof(q);
      assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&e))); /* HardwareAccess is required. */
      e.Flags.Value=1u;
      expect_ok("R137 production acquire",AdmissionDdiEscape(&a,&e));
      assert(q.ManagerId && q.ManagerGeneration && q.SceneId && q.SceneGeneration);
      for(unsigned i=0;i<9;++i)
        assert(AppleAgxGpuvaG3GraphContainsRangeAccess(&p->Graph,q.Ranges[i].Va,q.Ranges[i].Bytes,true));
      APPLE_AGX_G3_PRIVATE_REQUEST saved=q, second;
      assert(!AdmissionG4GraphAccessTyped(p,q.Ranges[0].Va,16,1,AppleAgxG4AccessProcess,0));
      memset(q.Ranges,0,sizeof(q.Ranges));q.SceneId=q.SceneGeneration=0;
      q.Operation=APPLE_AGX_G3_PRIVATE_PREPARE;
      expect_ok("R137 second scene",AdmissionDdiEscape(&a,&e));second=q;
      assert(q.ManagerGeneration==saved.ManagerGeneration && q.SceneId!=saved.SceneId);
      for(unsigned i=3;i<9;++i) assert(q.Ranges[i].Va!=saved.Ranges[i].Va);
      memset(q.Ranges,0,sizeof(q.Ranges));q.SceneId=q.SceneGeneration=0;
      assert(AdmissionDdiEscape(&a,&e)==STATUS_INSUFFICIENT_RESOURCES);
      assert(!q.SceneId && !p->Graph.Uncertain);
      memset(&q,0,sizeof(q));q.Magic=APPLE_AGX_G3_PRIVATE_MAGIC;q.Version=1;q.Bytes=sizeof(q);
      q.Operation=APPLE_AGX_G3_PRIVATE_RELEASE;
      q.ManagerId=saved.ManagerId;q.ManagerGeneration=saved.ManagerGeneration;
      q.SceneId=saved.SceneId;q.SceneGeneration=saved.SceneGeneration;
      e.hContext=(HANDLE)0xdead;
      assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&e)));
      e.hContext=&context;e.hDevice=(HANDLE)0xbeef;
      assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&e)));
      e.hDevice=&device;e.hKmdProcessHandle=NULL;
      assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&e)));
      e.hKmdProcessHandle=p;q.SceneGeneration++;
      assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&e)));q.SceneGeneration--;
      q.Reserved[0]=1;assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&e)));q.Reserved[0]=0;
      --e.PrivateDriverDataSize;assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&e)));++e.PrivateDriverDataSize;
      expect_ok("R137 production release",AdmissionDdiEscape(&a,&e));
      assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&e))); /* Stale release. */
      assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,saved.Ranges[3].Va,1));
      q.SceneId=second.SceneId;q.SceneGeneration=second.SceneGeneration;
      expect_ok("R137 second release",AdmissionDdiEscape(&a,&e));
      /* A different supported geometry uses arithmetic capacities, not a trace whitelist. */
      memset(&q,0,sizeof(q));q.Magic=APPLE_AGX_G3_PRIVATE_MAGIC;q.Version=1;q.Bytes=sizeof(q);
      q.Operation=APPLE_AGX_G3_PRIVATE_PREPARE;q.ManagerId=saved.ManagerId;
      q.ManagerGeneration=saved.ManagerGeneration;
      q.Width=1919;q.Height=1079;q.UtileWidth=32;q.UtileHeight=16;q.Layers=q.Samples=1;
      expect_ok("R137 varied geometry",AdmissionDdiEscape(&a,&e));
      AdmissionGpuvaG3DetachContext(&context);
    }
    /* Hand-built private tables, for the process the KMD gave none (G2 HW2b:
     * a user process got them with its present arena), above the pool
     * extents the KMD allocated. */
    if(getenv("G3_REPLAY_R137_PRIVATE") && !p->PrivateMiddleIpa) {
      ULONGLONG middle=local_ipa+(72ULL<<20), leaf=middle+0x4000, data=middle+0x10000;
      memset(local_cpu+(72u<<20),0,0x8000);
      assert(AppleAgxGpuvaG3GraphRegisterTable(&p->Graph,middle,1));
      assert(AppleAgxGpuvaG3GraphRegisterTable(&p->Graph,leaf,2));
      assert(AppleAgxGpuvaG3GraphUpdateParent(&p->Graph,p->Graph.RootIpa,1,middle));
      assert(AppleAgxGpuvaG3GraphUpdateParent(&p->Graph,middle,0,leaf));
      assert(AppleAgxGpuvaG3GraphUpdateLeafBacking(&p->Graph,leaf,0,data,true,
          (APPLE_AGX_GPUVA_G3_BACKING_KIND)2));
      ULONGLONG new_root=middle+0x8000;
      assert(AppleAgxGpuvaG3GraphRegisterTable(&p->Graph,new_root,0));
      assert(AppleAgxGpuvaG3GraphBindRoot(&p->Graph,new_root));
      assert(!AppleAgxGpuvaG3GraphContainsRange(&p->Graph,1ULL<<36,0x4000));
      assert(AppleAgxGpuvaG3GraphAttachPrivate(&p->Graph,1ULL<<36,middle,leaf));
      assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,1ULL<<36,0x4000));
      assert(AppleAgxGpuvaG3GraphDetachPrivateRoot(&p->Graph,1ULL<<36,p->BootstrapIpa,leaf));
      assert(AppleAgxGpuvaG3GraphUpdateParent(&p->Graph,new_root,1,0));
      assert(AppleAgxGpuvaG3GraphAttachPrivate(&p->Graph,1ULL<<36,middle,leaf));
      unsigned found=0;
      for(unsigned i=0;i<HV_AGX_GPUVA_V5_BACKINGS;++i)
        if(gpuva_v5.backings[i].live && gpuva_v5.backings[i].ipa==data) {
          assert(!gpuva_v5.backings[i].shared); ++found;
        }
      assert(found==1);
      assert(!AppleAgxGpuvaG3GraphUpdateLeafBacking(&p->Graph,leaf,0,data,true,
          AppleAgxGpuvaG3LocalBacking));
      assert(!AppleAgxGpuvaG3GraphUpdateLeafBacking(&p->Graph,leaf,1,leaf,true,
          AppleAgxGpuvaG3PrivateBacking));
      assert(!p->Graph.Uncertain);
      ULONGLONG generation=p->Graph.Backings->Generation;
      assert(hv_agx_gpuva_v5_revoke_backing(&gpuva_v5,p->Graph.ProcessId,
          p->Graph.ProcessGeneration,generation,data)==HV_AGX_GPUVA_V5_BUSY);
      assert(hv_agx_gpuva_v5_create(&gpuva_v5,999,1,middle+0xc000,false)==HV_AGX_GPUVA_V5_OK);
      assert(hv_agx_gpuva_v5_register_shared_backing(&gpuva_v5,999,1,generation,data)==HV_AGX_GPUVA_V5_OWNERSHIP);
      assert(hv_agx_gpuva_v5_destroy(&gpuva_v5,999,1)==HV_AGX_GPUVA_V5_OK);
      p->Graph.NextGeneration=~0ULL;
      assert(!AppleAgxGpuvaG3GraphUpdateLeafBacking(&p->Graph,leaf,1,data+0x4000,true,
          AppleAgxGpuvaG3PrivateBacking));
      assert(AppleAgxGpuvaG3GraphContainsRange(&p->Graph,1ULL<<36,0x4000));
    }
    expect_ok("R137 cleanup",AdmissionDdiDestroyProcess(&a,args.hKmdProcess));
    assert(state.ProcessCount==0);
    for(unsigned i=0;i<APPLE_AGX_G3_PRIVATE_UNITS;++i) assert(!state.PrivatePool.Blocks[i].Owner);
  }
  DXGKARG_CREATEPROCESS fail={0};
  reserve_status=STATUS_INSUFFICIENT_RESOURCES;
  assert(AdmissionDdiCreateProcess(&a,&fail)==reserve_status);
  assert(!fail.hKmdProcess && state.ProcessCount==0);
  reserve_status=0;
  const ULONGLONG invalid_bases[]={0,1,0x01ffffffULL,0x02000001ULL,
      (1ULL<<39)-1,(1ULL<<39),~0ULL};
  for(unsigned i=0;i<sizeof(invalid_bases)/sizeof(invalid_bases[0]);++i) {
    reserve_base=invalid_bases[i];
    assert(AdmissionDdiCreateProcess(&a,&fail)==STATUS_INVALID_ADDRESS);
    assert(!fail.hKmdProcess && state.ProcessCount==0);
  }
  /* G2 HW2: a valid span other than the requested top of the USC window
   * is not the range the KMD asked for. */
  reserve_base=1ULL<<36;
  {
    ULONG refused=replay_reserve_receipt[2];
    assert(AdmissionDdiCreateProcess(&a,&fail)==STATUS_INVALID_ADDRESS);
    assert(!fail.hKmdProcess && state.ProcessCount==0);
    assert(replay_reserve_receipt[2]==refused+1u &&
           replay_reserve_receipt[3]==(ULONG)STATUS_INVALID_ADDRESS &&
           replay_reserve_receipt[4]==0u && replay_reserve_receipt[5]==0x10u);
  }
  reserve_base=(1ULL<<36)+1;
  assert(!NT_SUCCESS(AdmissionDdiCreateProcess(&a,&fail)));
  assert(!fail.hKmdProcess && state.ProcessCount==0);
  reserve_base=1ULL<<39;
  assert(!NT_SUCCESS(AdmissionDdiCreateProcess(&a,&fail)));
  a.Interface.DxgkCbReserveGpuVirtualAddressRange=NULL;
  assert(!NT_SUCCESS(AdmissionDdiCreateProcess(&a,&fail)));
  free(local_cpu);
  puts("R137 reservation: PASS");
}
