/* EXP1121: every 2.4 MiB VidMm fill/transfer took 17-19 BuildPagingBuffer
 * passes because each record covered one 4 KiB page.  Physically contiguous
 * local-segment pages form one record (the CPU view of the local segment is
 * linear); system pages, discontiguous local pages and pattern phase stay
 * exact. */
static UINT coalesce_build(ADMISSION_CONTEXT *a, DXGKARG_BUILDPAGINGBUFFER *x,
    unsigned char *dma, unsigned char *private_data) {
  x->pDmaBuffer=dma; x->DmaSize=4096;
  x->pDmaBufferPrivateData=private_data; x->DmaBufferPrivateDataSize=4096;
  expect_ok("coalesce build",AdmissionGpuvaG3BuildPagingBuffer(a,x));
  return (UINT)(((unsigned char *)x->pDmaBuffer-dma)/sizeof(ADMISSION_PAGING_MARKER));
}

static void coalesce_run(ADMISSION_CONTEXT *a, unsigned char *private_data, UINT count) {
  ADMISSION_PAGING_RECORD *records=(ADMISSION_PAGING_RECORD *)private_data;
  assert(AdmissionPagingRecordsValid(records,count,64,
      count*sizeof(ADMISSION_PAGING_MARKER)));
  for (UINT i=0;i<count;++i)
    expect_ok("coalesce execute",AdmissionG3ExecuteVirtualPaging(a,&records[i]));
}

static void paging_coalesce_cases(void) {
  ADMISSION_CONTEXT a={0}; ADMISSION_G3_STATE state={0}; REPLAY_BROKER b={0};
  APPLE_AGX_GPUVA_V5_IO io={&b,ReplayWrite64,ReplayRead64,ReplayWrite32,ReplayBarrier, 0};
  ADMISSION_DEVICE device={0};
  DXGKARG_CREATECONTEXT cc={0};
  DXGKARG_SETROOTPAGETABLE root={0};
  DXGKARG_BUILDPAGINGBUFFER x={0};
  DXGK_PTE ptes[68]={0};
  static unsigned char dma[4096], private_data[4096];
  ADMISSION_PAGING_RECORD *records=(ADMISSION_PAGING_RECORD *)private_data;
  const unsigned char *pattern;
  UINT count, i;
  assert(posix_memalign((void **)&local_cpu,0x4000,(size_t)local_bytes)==0);
  memset(local_cpu,0,(size_t)local_bytes);
  ReplayBrokerInit(&b); InitializeListHead(&state.Processes);
  assert(AppleAgxGpuvaV5ClientInit(&state.Client,&io));
  { AGX_GPUVA_V5_REQUEST r={0}; AGX_GPUVA_V5_RESPONSE reply={0};
    r.Command=AGX_GPUVA_V5_CREATE;
    assert(AppleAgxGpuvaV5ClientCall(&state.Client,&r,&reply)); }
  a.Started=TRUE;a.GpuvaG3State=&state;state.Adapter=&a;a.ObjectAdapter=&a;
  ADMISSION_G3_PROCESS *paging=sys_process(&a,0x10000,1);
  device.Object.Magic=ADMISSION_OBJECT_DEVICE_MAGIC;
  device.Object.Adapter=&a.ObjectAdapter;
  device.GpuvaG3Process=paging;
  cc.Flags.Value=5; cc.EngineAffinity=1;
  expect_ok("coalesce context",AdmissionDdiCreateContext(&device,&cc));
  root.hContext=cc.hContext;
  root.Address.SegmentId=ADMISSION_MEMORY_LOCAL_SEGMENT;
  root.Address.SegmentOffset=0x10000;
  root.NumEntries=8;
  AdmissionDdiSetRootPageTable(&a,&root);

  /* VA 0x10000: 32 contiguous local pages (0x100..0x11f);
   * VA 0x30000: 32 local pages with one break after 16 (0x200.., 0x300..);
   * VA 0x50000: one 16K system group. */
  for (i=0;i<32;++i) {ptes[i].Flags=0x41;ptes[i].PageAddress=0x100+i;}
  expect_ok("contiguous map",sys_update(&a,paging,local_cpu+0x18000,0,16,32,ptes,0,0));
  for (i=0;i<32;++i) {ptes[i].Flags=0x41;ptes[i].PageAddress=(i<16?0x200:0x300)+i;}
  expect_ok("split map",sys_update(&a,paging,local_cpu+0x18000,0,48,32,ptes,0,0));
  for (i=0;i<4;++i) {ptes[i].Flags=1;ptes[i].PageAddress=(system_ipa>>12)+i;}
  expect_ok("system map",sys_update(&a,paging,local_cpu+0x18000,0,80,4,ptes,0,0));

  /* An unaligned contiguous fill is one record with the exact phase. */
  memset(local_cpu+0x100000,0xee,0x20000);
  x.Operation=DXGK_OPERATION_VIRTUAL_FILL;
  x.hSystemContext=cc.hContext;
  x.FillVirtual.DestinationVirtualAddress=0x10003;
  x.FillVirtual.FillSizeInBytes=0x1fff9;
  x.FillVirtual.FillPattern=0x7f81a2c3;
  count=coalesce_build(&a,&x,dma,private_data);
  assert(count==1);
  assert(records[0].Bytes==0x1fff9 && records[0].DestinationIpa==local_ipa+0x100003);
  coalesce_run(&a,private_data,count);
  pattern=(const unsigned char *)&x.FillVirtual.FillPattern;
  assert(local_cpu[0x100002]==0xee && local_cpu[0x11fffc]==0xee);
  for (i=0;i<0x1fff9;++i) assert(local_cpu[0x100003+i]==pattern[i&3u]);

  /* A discontiguous run splits exactly at the break. */
  memset(&x,0,sizeof(x));
  x.Operation=DXGK_OPERATION_VIRTUAL_FILL;
  x.hSystemContext=cc.hContext;
  x.FillVirtual.DestinationVirtualAddress=0x30000;
  x.FillVirtual.FillSizeInBytes=0x20000;
  x.FillVirtual.FillPattern=0x01020304;
  count=coalesce_build(&a,&x,dma,private_data);
  assert(count==2);
  assert(records[0].Bytes==0x10000 && records[0].DestinationIpa==local_ipa+0x200000);
  assert(records[1].Bytes==0x10000 && records[1].DestinationIpa==local_ipa+0x310000);
  assert(records[1].PatternOffset==0u);
  coalesce_run(&a,private_data,count);
  assert(*(unsigned int *)(local_cpu+0x20fffc)==0x01020304u);
  assert(*(unsigned int *)(local_cpu+0x310000)==0x01020304u);
  assert(local_cpu[0x210000]==0 && local_cpu[0x30ffff]==0);

  /* Local to local transfer between contiguous runs: one record. */
  for (i=0;i<0x10000;++i) local_cpu[0x100000+i]=(unsigned char)(i*13u+5u);
  memset(&x,0,sizeof(x));
  x.Operation=DXGK_OPERATION_VIRTUAL_TRANSFER;
  x.hSystemContext=cc.hContext;
  x.TransferVirtual.SourceVirtualAddress=0x10000;
  x.TransferVirtual.DestinationVirtualAddress=0x30000;
  x.TransferVirtual.TransferSizeInBytes=0x10000;
  x.TransferVirtual.TransferDirection=DXGK_MEMORY_TRANSFER_LOCAL_TO_LOCAL;
  count=coalesce_build(&a,&x,dma,private_data);
  assert(count==1 && records[0].Bytes==0x10000);
  coalesce_run(&a,private_data,count);
  assert(memcmp(local_cpu+0x200000,local_cpu+0x100000,0x10000)==0);

  /* Local to system: the system side stays one record per 4 KiB page. */
  memset(system_cpu,0,sizeof(system_cpu));
  memset(&x,0,sizeof(x));
  x.Operation=DXGK_OPERATION_VIRTUAL_TRANSFER;
  x.hSystemContext=cc.hContext;
  x.TransferVirtual.SourceVirtualAddress=0x10000;
  x.TransferVirtual.DestinationVirtualAddress=0x50000;
  x.TransferVirtual.TransferSizeInBytes=0x4000;
  x.TransferVirtual.TransferDirection=DXGK_MEMORY_TRANSFER_LOCAL_TO_SYSTEM;
  count=coalesce_build(&a,&x,dma,private_data);
  assert(count==4);
  for (i=0;i<4;++i) assert(records[i].Bytes==0x1000u);
  coalesce_run(&a,private_data,count);
  assert(memcmp(system_cpu,local_cpu+0x100000,0x4000)==0);

  /* The validator admits multi-page records only inside the local segment. */
  {
    ADMISSION_PAGING_RECORD bad=records[0];
    bad.Bytes=0x2000u;
    assert(!AdmissionPagingRecordsValid(&bad,1,64,sizeof(ADMISSION_PAGING_MARKER)));
    bad=records[0];
    bad.Kind=AdmissionPagingVirtualFill;
    bad.DestinationSegment=ADMISSION_MEMORY_LOCAL_SEGMENT;
    bad.DestinationIpa=local_ipa+0x100000;
    bad.Bytes=0x2000u;
    assert(AdmissionPagingRecordsValid(&bad,1,64,sizeof(ADMISSION_PAGING_MARKER)));
    assert(NT_SUCCESS(AdmissionG3ExecuteVirtualPaging(&a,&bad)));
    bad.DestinationSegment=0u;
    assert(!AdmissionPagingRecordsValid(&bad,1,64,sizeof(ADMISSION_PAGING_MARKER)));
    assert(!NT_SUCCESS(AdmissionG3ExecuteVirtualPaging(&a,&bad)));
  }
  puts("paging coalesce: PASS");
}
