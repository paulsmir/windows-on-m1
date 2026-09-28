static ADMISSION_OPEN_ALLOCATION *r145_open;
static BOOLEAN r145_mismatch;
static PVOID r145_acquire(const DXGKARGCB_GETHANDLEDATA *q,HANDLE *ref) {
  assert(q->Type==DXGK_HANDLE_ALLOCATION && q->Flags.DeviceSpecific);
  if(!r145_open || q->hObject!=r145_open->RuntimeAllocation) return NULL;
  *ref=r145_open;++r145_references;
  if(r145_mismatch) ++r145_open->RuntimeAllocation;
  return r145_open;
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
  a.Started=TRUE;a.PhysicalDeviceObject=(PDEVICE_OBJECT)1;a.GpuvaG3State=&state;state.Adapter=&a;
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
  opened.Allocation=&allocation.Object;opened.RuntimeAllocation=0x81234071u;
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
  q->Allocation=0x81234071u;q->GpuVa=0x10000;
  DXGKARG_ESCAPE escape={0};escape.hDevice=&d;escape.hContext=&c;
  escape.hKmdProcessHandle=p;escape.Flags.Value=1;
  escape.pPrivateDriverData=q;escape.PrivateDriverDataSize=sizeof(*q);
  /* A runtime allocation token is not the pointer returned by KMD Create.
   * Missing QUERY failure attribution must fail even after a successful replay. */
  assert((ULONGLONG)opened.RuntimeAllocation!=(ULONGLONG)(ULONG_PTR)&allocation);
  escape.Flags.Value=0;
  assert(AdmissionDdiEscape(&a,&escape)==STATUS_INVALID_PARAMETER);
  assert(a.G3CopyQueryFailureClaim==2 && a.G3CopyQueryFailurePredicate==4 &&
         a.G3CopyQueryFailureStatus==(ULONG)STATUS_INVALID_PARAMETER);
  assert(query_registry_writes==1 && query_registry_flushes==1 &&
         query_registry_receipt[0]==3 && query_registry_receipt[1]==168 &&
         query_registry_receipt[2]==4 &&
         query_registry_receipt[3]==(ULONG)STATUS_INVALID_PARAMETER);
  /* Guard 4 precedes request copy and handle acquisition: v3 must not invent
   * a request identity or inspect allocation storage on this early path. */
  assert(!(a.G3CopyQueryFailure.Flags & AppleAgxG3QueryCanonicalAllocationAvailable));
  assert(!a.G3CopyQueryFailure.RequestAllocationHandle &&
         !a.G3CopyQueryFailure.CanonicalAllocationIdentity &&
         !a.G3CopyQueryFailure.CanonicalAllocationBytes);
  escape.Flags.Value=1;
  q->Allocation=0x81234072u;
  assert(AdmissionDdiEscape(&a,&escape)==STATUS_INVALID_HANDLE);
  assert(a.G3CopyQueryFailurePredicate==4 && query_registry_writes==1); /* first survives */
  q->Allocation=0x81234071u;
  a.G3CopyQueryFailureClaim=0;a.G3CopyQueryFailurePredicate=0;
  /* Each mutation reaches the real predicate; external registry IO alone is
   * shimmed. Returning a different status or losing first attribution fails. */
#define QUERY_REJECT(change, restore, id, code) do { \
    ULONG writes=query_registry_writes; \
    a.G3CopyQueryFailureClaim=0; a.G3CopyQueryFailurePredicate=0; \
    change; \
    NTSTATUS got=AdmissionDdiEscape(&a,&escape); \
    restore; \
    assert(got==(code) && a.G3CopyQueryFailureClaim==2 && \
        a.G3CopyQueryFailurePredicate==(id) && \
        a.G3CopyQueryFailureStatus==(ULONG)(code)); \
    assert(query_registry_writes==writes+1 && query_registry_receipt[2]==(id) && \
        query_registry_receipt[3]==(ULONG)(code) && !r145_references && \
        replay_irql==PASSIVE_LEVEL && local_cpu[0x100000]==0); \
  } while(0)
  QUERY_REJECT(a.Started=FALSE,a.Started=TRUE,1,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(escape.PrivateDriverDataSize--,escape.PrivateDriverDataSize++,5,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(a.Interface.DxgkCbAcquireHandleData=NULL,a.Interface.DxgkCbAcquireHandleData=r145_acquire,7,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(a.GpuvaG3State=NULL,a.GpuvaG3State=&state,8,STATUS_INVALID_DEVICE_STATE);
  /* Wrong magic never reaches COPY dispatch, so test the handler separately. */
  q->Magic=0;a.G3CopyQueryFailureClaim=0;
  assert(AdmissionGpuvaG3CopyEscape(&a,&escape)==STATUS_INVALID_PARAMETER && a.G3CopyQueryFailurePredicate==10);
  q->Magic=APPLE_AGX_G3_COPY_MAGIC;
  QUERY_REJECT(replay_pool_fail=TRUE,replay_pool_fail=FALSE,9,STATUS_INSUFFICIENT_RESOURCES);
  QUERY_REJECT(q->Version=2,q->Version=1,11,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(q->Bytes--,q->Bytes++,12,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(q->Reserved=1,q->Reserved=0,13,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(q->Allocation=0,q->Allocation=0x81234071u,15,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(q->GpuVa=0,q->GpuVa=0x10000,16,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(q->GpuVa++,q->GpuVa--,17,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(q->GpuVa=1ULL<<39,q->GpuVa=0x10000,18,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(q->TransferBytes=65537,q->TransferBytes=0,19,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(q->ProcessGeneration=1,q->ProcessGeneration=0,20,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(q->Allocation=0x81234072u,q->Allocation=0x81234071u,22,STATUS_INVALID_HANDLE);
  QUERY_REJECT(escape.hKmdProcessHandle=NULL,escape.hKmdProcessHandle=p,23,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(p->Poisoned=1,p->Poisoned=0,24,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(p->Graph.Uncertain=1,p->Graph.Uncertain=0,25,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(p->Graph.Created=0,p->Graph.Created=1,26,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(escape.hContext=NULL,escape.hContext=&c,27,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(c.Win32Transport=FALSE,c.Win32Transport=TRUE,28,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(c.GpuvaG3Closing=TRUE,c.GpuvaG3Closing=FALSE,29,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(c.GpuvaG3Poisoned=TRUE,c.GpuvaG3Poisoned=FALSE,30,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(c.Object.Device=NULL,c.Object.Device=&d.Object,31,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(escape.hDevice=NULL,escape.hDevice=&d,32,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(d.Object.Adapter=NULL,d.Object.Adapter=&a.ObjectAdapter,33,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(opened.Magic=0,opened.Magic=ADMISSION_OPEN_ALLOCATION_MAGIC,34,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(opened.Device=NULL,opened.Device=&d,35,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(opened.Allocation=NULL,opened.Allocation=&allocation.Object,36,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(r145_mismatch=TRUE,(r145_mismatch=FALSE,--opened.RuntimeAllocation),37,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(allocation.Object.Magic=0,allocation.Object.Magic=ADMISSION_ALLOCATION_OBJECT_MAGIC,38,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(allocation.Win32ClassId=0,allocation.Win32ClassId=1,39,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(allocation.Object.Description.CpuVisible=1,allocation.Object.Description.CpuVisible=0,40,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(allocation.Object.Description.Type=0,allocation.Object.Description.Type=ADMISSION_WIN32_ALLOCATION_GPU_LOCAL,41,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(state.ActiveProcess=p,state.ActiveProcess=NULL,42,STATUS_DEVICE_BUSY);
  QUERY_REJECT(p->Graph.JobInFlight=1,p->Graph.JobInFlight=0,43,STATUS_DEVICE_BUSY);
  QUERY_REJECT(p->Graph.LeaseToken=1,p->Graph.LeaseToken=0,44,STATUS_DEVICE_BUSY);
  QUERY_REJECT(allocation.Object.Description.Size=0,allocation.Object.Description.Size=0x10000,48,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(allocation.Object.Description.Size=1ULL<<32,allocation.Object.Description.Size=0x10000,49,STATUS_INVALID_PARAMETER);
  QUERY_REJECT((q->GpuVa=(1ULL<<39)-65536,allocation.Object.Description.Size=131072),(q->GpuVa=65536,allocation.Object.Description.Size=65536),52,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(q->GpuVa=0x4000000,q->GpuVa=0x10000,53,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(replay_local_view_status=STATUS_INVALID_DEVICE_STATE,replay_local_view_status=STATUS_SUCCESS,54,STATUS_INVALID_DEVICE_STATE);
  unsigned char *saved_local_cpu=local_cpu;
  /* local_cpu is also checked by the macro, after restore. */
  QUERY_REJECT(local_cpu=NULL,local_cpu=saved_local_cpu,55,STATUS_INVALID_PARAMETER);
  ADMISSION_G3_TABLE_SHADOW *receipt_shadow=p->TableShadows;
  while(receipt_shadow && receipt_shadow->OriginalIpa!=local_ipa+0x18000) receipt_shadow=receipt_shadow->Next;
  assert(receipt_shadow && receipt_shadow->ResidentPtes);
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *saved_resident=receipt_shadow->ResidentPtes;
  QUERY_REJECT(receipt_shadow->ResidentPtes=NULL,receipt_shadow->ResidentPtes=saved_resident,56,STATUS_INVALID_PARAMETER);
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *rp=&receipt_shadow->ResidentPtes[16];
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE original=*rp;
  QUERY_REJECT(rp->Flags=0,*rp=original,57,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(rp->SegmentId=0,*rp=original,58,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(rp->Allocation=(ULONGLONG)opened.RuntimeAllocation,*rp=original,59,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(rp->AllocationOffset=4096,*rp=original,60,STATUS_INVALID_PARAMETER);
  QUERY_REJECT(rp->GuestIpa=local_ipa+local_bytes,*rp=original,61,STATUS_INVALID_PARAMETER);
#undef QUERY_REJECT
  /* No complete Operation tag: reject without inventing QUERY attribution. */
  a.G3CopyQueryFailureClaim=0;
  for(UINT bytes=4;bytes<16;++bytes) {
    escape.PrivateDriverDataSize=bytes;
    assert(AdmissionDdiEscape(&a,&escape)==STATUS_INVALID_PARAMETER);
    assert(a.G3CopyQueryFailureClaim==0 && !r145_references);
  }
  escape.PrivateDriverDataSize=sizeof(*q);
  ULONG writes_before_success=query_registry_writes;
  a.G3CopyQueryFailureClaim=0;a.G3CopyQueryFailurePredicate=0;
  expect_ok("R145 copy query",AdmissionDdiEscape(&a,&escape));
  assert(q->MappingGeneration==p->Graph.MappingGeneration && q->ProcessGeneration);
  assert(a.G3CopyQueryFailureClaim==0 && query_registry_writes==writes_before_success);
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
  q->Allocation=0x81234072u;assert(!NT_SUCCESS(AdmissionDdiEscape(&a,&escape)));q->Allocation=0x81234071u;
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
  assert(a.G3CopyQueryFailureClaim==0 && query_registry_writes==writes_before_success);
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
