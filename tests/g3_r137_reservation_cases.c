static void r137_reservation_cases(void) {
  ADMISSION_CONTEXT a={0}; ADMISSION_G3_STATE state={0}; REPLAY_BROKER b={0};
  APPLE_AGX_GPUVA_V5_IO io={&b,ReplayWrite64,ReplayRead64,ReplayWrite32,ReplayBarrier};
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
    if(getenv("G3_REPLAY_R137_PRIVATE")) {
      ULONGLONG middle=local_ipa+(40ULL<<20), leaf=middle+0x4000, data=middle+0x10000;
      memset(local_cpu+(40u<<20),0,0x8000);
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
  }
  DXGKARG_CREATEPROCESS fail={0};
  reserve_status=STATUS_INSUFFICIENT_RESOURCES;
  assert(AdmissionDdiCreateProcess(&a,&fail)==reserve_status);
  assert(!fail.hKmdProcess && state.ProcessCount==0);
  reserve_status=0; reserve_base=(1ULL<<36)+1;
  assert(!NT_SUCCESS(AdmissionDdiCreateProcess(&a,&fail)));
  assert(!fail.hKmdProcess && state.ProcessCount==0);
  reserve_base=1ULL<<39;
  assert(!NT_SUCCESS(AdmissionDdiCreateProcess(&a,&fail)));
  a.Interface.DxgkCbReserveGpuVirtualAddressRange=NULL;
  assert(!NT_SUCCESS(AdmissionDdiCreateProcess(&a,&fail)));
  free(local_cpu);
  puts("R137 reservation: PASS");
}
