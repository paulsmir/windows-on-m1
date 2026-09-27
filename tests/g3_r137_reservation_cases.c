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
