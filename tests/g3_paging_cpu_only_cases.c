/* VidMm's system paging process: only paging operations use its VAs, and this
 * driver executes them on the CPU through the logical shadow.  Its leaf
 * updates must make no broker call and take no grant or mapping reference
 * (EXP1121: 592-PTE map/unmap pairs cost ~4.6 ms of broker calls each).
 * A user process mapping the same pages still publishes them. */
static void paging_execute(ADMISSION_CONTEXT *a, DXGKARG_BUILDPAGINGBUFFER *x,
    unsigned char *dma, unsigned char *private_data, NTSTATUS expected) {
  ADMISSION_PAGING_RECORD *records=(ADMISSION_PAGING_RECORD *)private_data;
  UINT count, i;
  x->pDmaBuffer=dma; x->DmaSize=4096;
  x->pDmaBufferPrivateData=private_data; x->DmaBufferPrivateDataSize=4096;
  assert(AdmissionGpuvaG3BuildPagingBuffer(a,x)==expected);
  if (!NT_SUCCESS(expected)) return;
  count=(UINT)(((unsigned char *)x->pDmaBuffer-dma)/sizeof(ADMISSION_PAGING_MARKER));
  assert(count && AdmissionPagingRecordsValid(records,count,64,
      count*sizeof(ADMISSION_PAGING_MARKER)));
  for (i=0;i<count;++i)
    expect_ok("paging record",AdmissionG3ExecuteVirtualPaging(a,&records[i]));
}

static void paging_cpu_only_cases(void) {
  ADMISSION_CONTEXT a={0}; ADMISSION_G3_STATE state={0}; REPLAY_BROKER b={0};
  APPLE_AGX_GPUVA_V5_IO io={&b,ReplayWrite64,ReplayRead64,ReplayWrite32,ReplayBarrier, 0};
  ADMISSION_DEVICE device={0};
  DXGKARG_CREATECONTEXT cc={0};
  DXGKARG_SETROOTPAGETABLE root={0};
  DXGKARG_BUILDPAGINGBUFFER x={0};
  DXGK_PTE ptes[36]={0}, zero={0};
  static unsigned char dma[4096], private_data[4096];
  UINT calls, i;
  assert(posix_memalign((void **)&local_cpu,0x4000,(size_t)local_bytes)==0);
  memset(local_cpu,0,(size_t)local_bytes);
  ReplayBrokerInit(&b); InitializeListHead(&state.Processes);
  assert(AppleAgxGpuvaV5ClientInit(&state.Client,&io));
  { AGX_GPUVA_V5_REQUEST r={0}; AGX_GPUVA_V5_RESPONSE reply={0};
    r.Command=AGX_GPUVA_V5_CREATE;
    assert(AppleAgxGpuvaV5ClientCall(&state.Client,&r,&reply)); }
  a.Started=TRUE;a.GpuvaG3State=&state;state.Adapter=&a;a.ObjectAdapter=&a;
  ADMISSION_G3_PROCESS *paging=sys_process(&a,0x10000,1);
  ADMISSION_G3_PROCESS *user=sys_process(&a,0x20000,0);
  device.Object.Magic=ADMISSION_OBJECT_DEVICE_MAGIC;
  device.Object.Adapter=&a.ObjectAdapter;
  device.GpuvaG3Process=paging;
  cc.Flags.Value=5; /* SystemContext | VirtualAddressing (EXP778). */
  cc.EngineAffinity=1;
  expect_ok("paging context",AdmissionDdiCreateContext(&device,&cc));
  root.hContext=cc.hContext;
  root.Address.SegmentId=ADMISSION_MEMORY_LOCAL_SEGMENT;
  root.Address.SegmentOffset=0x10000;
  root.NumEntries=8;
  AdmissionDdiSetRootPageTable(&a,&root);
  assert(((ADMISSION_RENDER_CONTEXT *)cc.hContext)->GpuvaG3RootIpa==
         paging->Graph.RootIpa);

  /* 32 local pages at VA 0x10000 and one 16K system group at VA 0x30000. */
  for (i=0;i<32;++i) {ptes[i].Flags=0x41;ptes[i].PageAddress=0x100+i;}
  for (i=0;i<4;++i) {ptes[32+i].Flags=1;ptes[32+i].PageAddress=(system_ipa>>12)+i;}
  calls=b.commands;
  expect_ok("paging map",sys_update(&a,paging,local_cpu+0x18000,0,16,36,ptes,0,0));
  assert(b.commands==calls);
  assert(paging->Graph.Leaves==NULL && paging->Graph.Backings==NULL);
  assert(!sys_frame(&state,system_ipa));
  {
    ADMISSION_G3_TABLE_SHADOW *s=paging->TableShadows;
    while (s->OriginalIpa!=local_ipa+0x18000) s=s->Next;
    assert(s->ResidentPtes==NULL);
    assert(s->LogicalPtes[16].GuestIpa==local_ipa+0x100000);
    assert(s->LogicalPtes[48].GuestIpa==system_ipa &&
           s->LogicalPtes[48].SegmentId==0u);
  }

  /* The same PTEs in a user process are published to the GPU. */
  calls=b.commands;
  expect_ok("user map",sys_update(&a,user,local_cpu+0x28000,0,16,36,ptes,0,0));
  assert(b.commands>calls);
  assert(AppleAgxGpuvaG3GraphContainsRange(&user->Graph,0x10000,0x24000));
  assert(sys_frame(&state,system_ipa)->Mappings==4 &&
         sys_frame(&state,system_ipa)->Grants==1);

  /* The CPU executor still reaches both segments through the paging VAs. */
  for (i=0;i<0x4000;++i) local_cpu[0x100000+i]=(unsigned char)(i*7u+3u);
  memset(system_cpu,0,sizeof(system_cpu));
  x.Operation=DXGK_OPERATION_VIRTUAL_TRANSFER;
  x.hSystemContext=cc.hContext;
  x.TransferVirtual.SourceVirtualAddress=0x10000;
  x.TransferVirtual.DestinationVirtualAddress=0x30000;
  x.TransferVirtual.TransferSizeInBytes=0x4000;
  x.TransferVirtual.TransferDirection=DXGK_MEMORY_TRANSFER_LOCAL_TO_SYSTEM;
  paging_execute(&a,&x,dma,private_data,STATUS_SUCCESS);
  assert(memcmp(system_cpu,local_cpu+0x100000,0x4000)==0);
  memset(&x,0,sizeof(x));
  x.Operation=DXGK_OPERATION_VIRTUAL_FILL;
  x.hSystemContext=cc.hContext;
  x.FillVirtual.DestinationVirtualAddress=0x14000;
  x.FillVirtual.FillSizeInBytes=0x8000;
  x.FillVirtual.FillPattern=0x7f81a2c3;
  paging_execute(&a,&x,dma,private_data,STATUS_SUCCESS);
  for (i=0;i<0x8000;++i)
    assert(local_cpu[0x104000+i]==((const unsigned char *)&x.FillVirtual.FillPattern)[i&3u]);
  assert(local_cpu[0x103fff]==(unsigned char)(0x3fffu*7u+3u));

  /* The paging process never runs a GPU job. */
  assert(AdmissionGpuvaG3BeginJob(&a,(ADMISSION_RENDER_CONTEXT *)cc.hContext,1u)==
         STATUS_INVALID_DEVICE_STATE);

  /* Unmapping is broker-free and removes CPU reachability. */
  calls=b.commands;
  expect_ok("paging unmap",sys_update(&a,paging,local_cpu+0x18000,0,16,36,&zero,0,1));
  assert(b.commands==calls);
  memset(&x,0,sizeof(x));
  x.Operation=DXGK_OPERATION_VIRTUAL_FILL;
  x.hSystemContext=cc.hContext;
  x.FillVirtual.DestinationVirtualAddress=0x10000;
  x.FillVirtual.FillSizeInBytes=0x1000;
  paging_execute(&a,&x,dma,private_data,STATUS_INVALID_ADDRESS);
  assert(sys_frame(&state,system_ipa)->Mappings==4);

  expect_ok("destroy paging context",AdmissionDdiDestroyContext(cc.hContext));
  expect_ok("destroy paging process",AdmissionDdiDestroyProcess(&a,paging));
  assert(sys_frame(&state,system_ipa)->Mappings==4 &&
         sys_frame(&state,system_ipa)->Grants==1);
  puts("paging process CPU-only: PASS");
}
