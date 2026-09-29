/* EXP856: real UpdatePageTable + graph + broker, with separate VidMm/DCP
 * views. Only OS allocation, memory and stage-2 services are simulated. */
static void r144_local_bounds_cases(void) {
  ADMISSION_CONTEXT a={0}; ADMISSION_G3_STATE state={0}; REPLAY_BROKER b={0};
  APPLE_AGX_GPUVA_V5_IO io={&b,ReplayWrite64,ReplayRead64,ReplayWrite32,ReplayBarrier};
  DXGKARG_CREATEPROCESS create={0}; DXGKARG_BUILDPAGINGBUFFER args={0};
  DXGK_PTE zero={0}, ptes[16]={0}, parent={0};
  ADMISSION_SCANOUT_MEMORY_VIEW scanout={0};
  const ULONGLONG leaf=0x3d7d4000ULL, middle=0x18000000ULL, root=0x08000000ULL;
  const ULONGLONG payload=0x3d7f0000ULL;
  local_ipa=0x8e0000000ULL; local_bytes=0x40000000ULL;
  vidmm_local_bytes=0x3d800000ULL;
  assert(posix_memalign((void **)&local_cpu,0x4000,(size_t)local_bytes)==0);
  memset(local_cpu,0,(size_t)local_bytes);
  ReplayBrokerInit(&b); InitializeListHead(&state.Processes);
  assert(AppleAgxGpuvaV5ClientInit(&state.Client,&io));
  { AGX_GPUVA_V5_REQUEST request={0}; AGX_GPUVA_V5_RESPONSE reply={0};
    request.Command=AGX_GPUVA_V5_CREATE;
    assert(AppleAgxGpuvaV5ClientCall(&state.Client,&request,&reply)); }
  a.Started=TRUE; a.GpuvaG3State=&state; state.Adapter=&a;
  a.ObjectAdapter=&a;
  a.Interface=(DXGKRNL_INTERFACE){(HANDLE)0x1234,ReplayReserveVa};
  expect_ok("R144 create",AdmissionDdiCreateProcess(&a,&create));
  ADMISSION_G3_PROCESS *p=create.hKmdProcess;
  expect_ok("R144 DCP view",AdmissionMemoryRuntimeScanoutView(&a,&scanout));
  assert(scanout.Bytes==0x03800000ULL && scanout.PoolBytes==0x03800000ULL);

  /* Exact full-dump input: level0, segment2, offset3e7d4000, count16,
   * Repeat|InitialUpdate, one zero PTE. No child IPA exists in this receipt. */
  args.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;
  args.UpdatePageTable.hProcess=p;
  args.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_GPU_PHYSICAL;
  args.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentId=2;
  args.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentOffset=leaf;
  args.UpdatePageTable.NumPageTableEntries=16;
  args.UpdatePageTable.Flags.Repeat=1;
  args.UpdatePageTable.Flags.InitialUpdate=1;
  args.UpdatePageTable.pPageTableEntries=&zero;
  NTSTATUS status=AdmissionGpuvaG3BuildPagingBuffer(&a,&args);
  if (!NT_SUCCESS(status)) {
    fprintf(stderr,"EXP856 receipt: branch=%u level=%u mode=%u offset=%llx status=%08x\n",
        last_paging_failure.Branch,last_paging_failure.Level,
        last_paging_failure.UpdateMode,last_paging_failure.TableAddress,(unsigned)status);
    assert(status==STATUS_INVALID_ADDRESS && last_paging_failure.Branch==1);
    assert(last_paging_failure.TableAddress==leaf && !last_paging_failure.ChildIpa);
  }
  expect_ok("EXP856 full-local UpdatePageTable",status);
  assert(p->TableShadows && p->TableShadows->OriginalIpa==0x91d7d4000ULL);

  /* Neighbors and the final native table: no trace-address allowlist. */
  const ULONGLONG offsets[]={0x03800000ULL,0x04000000ULL,0x20000000ULL,
                            0x3d7d0000ULL,0x3d7d8000ULL,0x3d7fc000ULL};
  for (UINT i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
    args.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentOffset=offsets[i];
    expect_ok("R144 high GPU table",AdmissionGpuvaG3BuildPagingBuffer(&a,&args));
    expect_ok("R144 high CPU table",sys_update(&a,p,local_cpu+offsets[i],0,0,16,&zero,0,1));
  }
  /* Private/backend and out-of-reserve addresses must never reach broker I/O. */
  const ULONGLONG invalid[]={0x3d7ff000ULL,0x3d800000ULL,0x3f800000ULL,
                            0x40000000ULL,0xffffffffffffc000ULL};
  for (UINT i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
    UINT commands=b.commands;
    args.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentOffset=invalid[i];
    assert(AdmissionGpuvaG3BuildPagingBuffer(&a,&args)==STATUS_INVALID_ADDRESS);
    assert(b.commands==commands);
    if (invalid[i]<local_bytes)
      assert(sys_update(&a,p,local_cpu+invalid[i],0,0,16,&zero,0,1)==STATUS_INVALID_ADDRESS);
    assert(b.commands==commands);
  }
  /* High local backing in both logical4K and64K leaf formats. */
  for (UINT i=0;i<16;++i) {
    ptes[i].Flags=0x41;
    ptes[i].PageAddress=(payload>>12)+i;
  }
  expect_ok("R144 high local leaf",sys_update(&a,p,local_cpu+leaf,0,0,16,ptes,0,0));
  parent.Flags=0x41; parent.PageTableAddress=leaf>>12;
  expect_ok("R144 high child",sys_update(&a,p,local_cpu+middle,1,0,1,&parent,0,0));
  parent.PageTableAddress=middle>>12;
  expect_ok("R144 high root",sys_update(&a,p,local_cpu+root,2,0,1,&parent,0,0));
  ULONGLONG broker_root=0, mapped=0;
  expect_ok("R144 root shadow",AdmissionGpuvaG3BrokerTable(p,local_ipa+root,FALSE,&broker_root));
  assert(AppleAgxGpuvaG3GraphBindRoot(&p->Graph,broker_root));
  assert(AppleAgxGpuvaG3GraphTranslateVa(&p->Graph,0x1000,&mapped));
  assert(mapped==0x91d7f1000ULL);
  if (ADMISSION_GPUVA_G1B_PAGE_PROFILE==64) {
    expect_ok("R144 high64K leaf",sys_update(&a,p,local_cpu+leaf,0,0,1,ptes,1,0));
    assert(AppleAgxGpuvaG3GraphTranslateVa(&p->Graph,0xf000,&mapped));
    assert(mapped==0x91d7ff000ULL);
  }
  /* Local payload at the segment end is refused without losing old mappings. */
  ptes[0].PageAddress=0x3d800000ULL>>12;
  assert(!NT_SUCCESS(sys_update(&a,p,local_cpu+leaf,0,0,1,ptes,0,0)));
  assert(AppleAgxGpuvaG3GraphTranslateVa(&p->Graph,0x1000,&mapped));
  assert(mapped==0x91d7f1000ULL);

  DXGKARG_BUILDPAGINGBUFFER flush={0};
  flush.Operation=DXGK_OPERATION_FLUSH_TLB;
  flush.FlushTlb.hProcess=p;
  flush.FlushTlb.RootPageTableAddress.SegmentId=2;
  flush.FlushTlb.RootPageTableAddress.SegmentOffset=root;
  UINT flushes=b.flush_commands;
  expect_ok("R144 high root flush",AdmissionGpuvaG3BuildPagingBuffer(&a,&flush));
  /* No active slot: resolve the owned high root, then acknowledge no-op. */
  assert(b.flush_commands==flushes && last_flush_receipt.ResolveStatus==0);
  assert(last_flush_receipt.Branch==5);

  /* Same full-local ownership applies to CPU execution of paging records. */
  ADMISSION_PAGING_RECORD record={0};
  record.Kind=AdmissionPagingVirtualFill; record.Bytes=0x1000;
  record.DestinationSegment=2; record.DestinationIpa=local_ipa+payload;
  record.FillPattern=0x76543210;
  expect_ok("R144 high paging fill",AdmissionG3ExecuteVirtualPaging(&a,&record));
  assert(*(UINT *)(local_cpu+payload)==0x76543210);
  assert(*(UINT *)(local_cpu+payload+0xffc)==0x76543210);
  record.Kind=AdmissionPagingVirtualTransfer;
  record.SourceSegment=2; record.SourceIpa=record.DestinationIpa;
  record.DestinationIpa+=0x1000;
  expect_ok("R144 high paging copy",AdmissionG3ExecuteVirtualPaging(&a,&record));
  assert(memcmp(local_cpu+payload,local_cpu+payload+0x1000,0x1000)==0);
  record.DestinationIpa=local_ipa+0x3d800000ULL;
  assert(AdmissionG3ExecuteVirtualPaging(&a,&record)==STATUS_INVALID_ADDRESS);
  assert(*(UINT *)(local_cpu+0x3d800000ULL)==0);

  expect_ok("R144 cleanup",AdmissionDdiDestroyProcess(&a,p));
  assert(!state.Registry.Frames);
  free(local_cpu); local_cpu=NULL;
  puts("R144 full-local paging: PASS");
}
