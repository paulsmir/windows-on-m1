static ADMISSION_OPEN_ALLOCATION *r145_open;
static UINT r145_references;
static PVOID r145_acquire(const DXGKARGCB_GETHANDLEDATA *q,HANDLE *ref) {
  assert(q->Type==DXGK_HANDLE_ALLOCATION && q->Flags.DeviceSpecific);
  if(!r145_open || q->hObject!=r145_open->RuntimeAllocation) return NULL;
  *ref=r145_open;++r145_references;return r145_open;
}
static VOID r145_release(DXGKARGCB_RELEASEHANDLEDATA ref) {
  assert(ref.ReleaseHandle==r145_open && r145_references==1);--r145_references;
}
static void r145_copy_cases(void) {
  ADMISSION_CONTEXT a={0};ADMISSION_G3_STATE state={0};REPLAY_BROKER b={0};
  APPLE_AGX_GPUVA_V5_IO io={&b,ReplayWrite64,ReplayRead64,ReplayWrite32,ReplayBarrier};
  assert(posix_memalign((void **)&local_cpu,0x4000,(size_t)local_bytes)==0);
  memset(local_cpu,0,(size_t)local_bytes);
  ReplayBrokerInit(&b);InitializeListHead(&state.Processes);
  assert(AppleAgxGpuvaV5ClientInit(&state.Client,&io));
  { AGX_GPUVA_V5_REQUEST r={0};AGX_GPUVA_V5_RESPONSE reply={0};
    r.Command=AGX_GPUVA_V5_CREATE;assert(AppleAgxGpuvaV5ClientCall(&state.Client,&r,&reply)); }
  a.Started=TRUE;a.GpuvaG3State=&state;state.Adapter=&a;
  ADMISSION_G3_PROCESS *p=sys_process(&a,0x10000,0);
  ADMISSION_DEVICE d={0};ADMISSION_RENDER_CONTEXT c={0};
  d.Object.Adapter=&a.ObjectAdapter;d.Object.Magic=ADMISSION_OBJECT_DEVICE_MAGIC;
  c.Object.Device=&d.Object;c.Object.Magic=ADMISSION_OBJECT_CONTEXT_MAGIC;
  c.Win32Transport=TRUE;c.GpuvaG3Process=p;p->Contexts=&c;
  ADMISSION_ALLOCATION_HANDLE allocation={0};ADMISSION_OPEN_ALLOCATION opened={0};
  allocation.Object.Magic=ADMISSION_ALLOCATION_OBJECT_MAGIC;
  allocation.Object.Description.Size=0x10000;
  allocation.Object.Description.Type=ADMISSION_WIN32_ALLOCATION_GPU_LOCAL;
  allocation.Win32ClassId=1;allocation.Win32Flags=15;
  opened.Magic=ADMISSION_OPEN_ALLOCATION_MAGIC;opened.Device=&d;
  opened.Allocation=&allocation.Object;opened.RuntimeAllocation=71;
  opened.Win32ClassId=1;opened.Win32Flags=15;r145_open=&opened;
  a.Interface.DxgkCbAcquireHandleData=r145_acquire;
  a.Interface.DxgkCbReleaseHandleData=r145_release;
  DXGKARG_BUILDPAGINGBUFFER x={0};DXGK_PTE ptes[16]={0};
  for(UINT i=0;i<16;++i){ptes[i].Flags=0x41;ptes[i].PageAddress=0x100+i;}
  x.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;x.UpdatePageTable.hProcess=p;
  x.UpdatePageTable.PageTableAddress.CpuVirtual=local_cpu+0x18000;
  x.UpdatePageTable.hAllocation=&allocation;
  x.UpdatePageTable.StartIndex=ADMISSION_GPUVA_G1B_PAGE_PROFILE==64?1:16;
  x.UpdatePageTable.NumPageTableEntries=ADMISSION_GPUVA_G1B_PAGE_PROFILE==64?1:16;
  x.UpdatePageTable.Flags.Use64KBPages=ADMISSION_GPUVA_G1B_PAGE_PROFILE==64;
  x.UpdatePageTable.FirstPteVirtualAddress=0x10000;
  x.UpdatePageTable.pPageTableEntries=ptes;
  expect_ok("R145 local publication",AdmissionGpuvaG3BuildPagingBuffer(&a,&x));
  APPLE_AGX_G3_COPY_REQUEST *q=calloc(1,sizeof(*q));assert(q);
  q->Magic=APPLE_AGX_G3_COPY_MAGIC;q->Version=1;q->Bytes=sizeof(*q);
  q->Allocation=71;q->GpuVa=0x10000;
  DXGKARG_ESCAPE escape={0};escape.hDevice=&d;escape.hContext=&c;
  escape.hKmdProcessHandle=p;escape.Flags.Value=1;
  escape.pPrivateDriverData=q;escape.PrivateDriverDataSize=sizeof(*q);
  expect_ok("R145 copy query",AdmissionDdiEscape(&a,&escape));
  assert(q->MappingGeneration==p->Graph.MappingGeneration && q->ProcessGeneration);
  q->Operation=APPLE_AGX_G3_COPY_UPLOAD;q->Offset=0xff9;q->TransferBytes=0x4009;
  for(UINT i=0;i<q->TransferBytes;++i) q->Data[i]=(unsigned char)(i*37+9);
  expect_ok("R145 partial upload",AdmissionDdiEscape(&a,&escape));
  assert(!memcmp(local_cpu+0x100000+q->Offset,q->Data,q->TransferBytes));
  assert(local_cpu[0x100000+q->Offset-1]==0 && local_cpu[0x100000+q->Offset+q->TransferBytes]==0);
  q->Operation=APPLE_AGX_G3_COPY_DOWNLOAD;memset(q->Data,0,sizeof(q->Data));
  expect_ok("R145 readback",AdmissionDdiEscape(&a,&escape));
  for(UINT i=0;i<q->TransferBytes;++i) assert(q->Data[i]==(unsigned char)(i*37+9));
  q->Operation=APPLE_AGX_G3_COPY_UPLOAD;memset(q->Data,0xab,sizeof(q->Data));
  ++q->MappingGeneration;assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)));--q->MappingGeneration;
  ++q->ProcessGeneration;assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)));--q->ProcessGeneration;
  p->Graph.JobInFlight=1;assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)));p->Graph.JobInFlight=0;
  q->Allocation=72;assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)));q->Allocation=71;
  opened.Device=NULL;assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)));opened.Device=&d;
  opened.ReadOnly=TRUE;assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)));opened.ReadOnly=FALSE;
  q->Offset=0xffff;assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)));q->Offset=0xff9;
  assert(local_cpu[0x100ff9]==9 && !r145_references);
  /* Wrong tail provenance must be rejected before any earlier page is stored. */
  ADMISSION_G3_TABLE_SHADOW *shadow=p->TableShadows;
  while(shadow && shadow->OriginalIpa!=local_ipa+0x18000) shadow=shadow->Next;
  assert(shadow && shadow->ResidentPtes);
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE saved=shadow->ResidentPtes[20];
  shadow->ResidentPtes[20].Allocation=0;
  assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)) && local_cpu[0x100ff9]==9);
  shadow->ResidentPtes[20]=saved;shadow->ResidentPtes[20].AllocationOffset+=4096;
  assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)) && local_cpu[0x100ff9]==9);
  shadow->ResidentPtes[20]=saved;shadow->ResidentPtes[20].SegmentId=0;
  assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)) && local_cpu[0x100ff9]==9);
  shadow->ResidentPtes[20]=saved;
  escape.Flags.Value=0;assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)));escape.Flags.Value=1;
  c.GpuvaG3Closing=TRUE;assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)));c.GpuvaG3Closing=FALSE;
  state.ActiveProcess=p;assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)));state.ActiveProcess=NULL;
  /* A real eviction update invalidates the saved copy generation. */
  memset(ptes,0,sizeof(ptes));expect_ok("R145 eviction",AdmissionGpuvaG3BuildPagingBuffer(&a,&x));
  assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)) && !r145_references);
  /* Re-residency: same VA/owner, different local backing and generation. */
  memcpy(local_cpu+0x200000,local_cpu+0x100000,0x10000);
  for(UINT i=0;i<16;++i){ptes[i].Flags=0x41;ptes[i].PageAddress=0x200+i;}
  expect_ok("R145 re-residency",AdmissionGpuvaG3BuildPagingBuffer(&a,&x));
  assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)));
  q->Operation=APPLE_AGX_G3_COPY_QUERY;q->Offset=0;q->TransferBytes=0;
  q->MappingGeneration=q->ProcessGeneration=0;
  expect_ok("R145 new generation",AdmissionDdiEscape(&a,&escape));
  q->Operation=APPLE_AGX_G3_COPY_DOWNLOAD;q->Offset=0xff9;q->TransferBytes=0x4009;
  expect_ok("R145 migrated readback",AdmissionDdiEscape(&a,&escape));
  for(UINT i=0;i<q->TransferBytes;++i) assert(q->Data[i]==(unsigned char)(i*37+9));
  p->Contexts=NULL;expect_ok("R145 cleanup",AdmissionDdiDestroyProcess(&a,p));
  free(q);free(local_cpu);local_cpu=NULL;r145_open=NULL;
  puts("R145 local copy: PASS");
}
