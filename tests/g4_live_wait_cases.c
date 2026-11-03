/* Classifier inputs independent of EXP2116: no trace-shaped admission rules.
 * This runs real KMD preflight, memory resolvers and graph lookup. */
static ADMISSION_G3_PROCESS *diagnostic_process;
static void poison_during_wait(void) { diagnostic_process->Poisoned=TRUE; replay_interrupt_time+=30000000ULL; }
static void retire_diagnostic_job(void) { diagnostic_process->Graph.JobInFlight=false; replay_interrupt_time+=30000000ULL; }
static void check_live_reasons(ADMISSION_CONTEXT *a) {
  const ULONGLONG saved_base=local_ipa, saved_bytes=vidmm_local_bytes;
  for (unsigned want=0;want<36;++want) {
    ADMISSION_G3_PROCESS process={0}, *p=&process;
    ADMISSION_G3_TABLE_SHADOW shadow={0};
    APPLE_AGX_GPUVA_G3_NODE table={0}, edge={0}, *slots[2048]={0};
    APPLE_AGX_GPUVA_G3_LOGICAL_PTE logical[4]={{0}},resident[4]={{0}};
    ADMISSION_ALLOCATION_HANDLE owner={0};
    ADMISSION_G3_LIVE_WAIT_ENTRY detail={0};
    DXGK_PTE ptes[4]={{0}};
    DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE u={0};
    ULONGLONG original=local_ipa+0x8000;
    p->Graph.LiveAdditive=true;p->Graph.RootIpa=local_ipa+0x10000;
    shadow.OriginalIpa=original;shadow.BrokerIpa=local_ipa+0x18000;
    table.Ipa=shadow.BrokerIpa;table.Level=2;
    p->TableShadows=&shadow;p->Graph.Tables=&table;
    u.NumPageTableEntries=4;u.pPageTableEntries=ptes;
    for(unsigned i=0;i<4;++i){ptes[i].Flags=0x41;ptes[i].PageAddress=0x200+i;}
    switch(want) {
      case 0: break;
      case 1: p=NULL;break;
      case 2: p->Graph.LiveAdditive=false;break;
      case 3: p->Poisoned=true;break;
      case 4: p->Graph.Uncertain=true;break;
      case 5: p->CpuOnlyMappings=1;break;
      case 6: u.Flags.NotifyEviction=1;break;
      case 7: u.NumPageTableEntries=0;break;
      case 8: shadow.PendingPtes=logical;break;
      case 9: table.Level=1;break;
      case 10: replay_local_view_status=STATUS_INVALID_DEVICE_STATE;break;
      case 11: owner.Object.Magic=ADMISSION_ALLOCATION_OBJECT_MAGIC;
        owner.NativeStagingBinding=&owner;u.hAllocation=&owner;break;
      case 12: p->Graph.MappingGeneration=MAXULONGLONG;break;
      case 13: original=local_ipa-0x4000;break;
      case 14: u.FirstPteVirtualAddress=1;break;
      case 15: u.PageTableLevel=2;table.Level=0;break;
      case 16: u.PageTableLevel=1;table.Level=1;ptes[0].Valid=0;break;
      case 17: u.PageTableLevel=1;table.Level=1;ptes[0].PageTableAddress=1;break;
      case 18: u.PageTableLevel=1;table.Level=1;ptes[0].PageTableAddress=8;break;
      case 19: u.NumPageTableEntries=1;break;
      case 20: u.AllocationOffsetInBytes=MAXULONGLONG;break;
      case 21: logical[0].Flags=1;shadow.LogicalPtes=logical;break;
      case 22: resident[0].Flags=1;shadow.ResidentPtes=resident;break;
      case 23: ptes[0].Valid=0;break;
      case 24: ptes[0].NoExecute=1;break;
      case 25: ptes[0].PageAddress=1ULL<<52;break;
      case 26: ptes[0].Segment=0;break;
      case 27: ptes[0].PageAddress=vidmm_local_bytes>>12;break;
      case 28: replay_pool_ipa=MAXULONGLONG-0x3fff;replay_pool_bytes=0x4000;
        ptes[0].Segment=3;ptes[0].PageAddress=3;break;
      case 29: u.Flags.Use64KBPages=1;u.NumPageTableEntries=1;
        ptes[0].PageAddress=0x204;break;
      case 30: ptes[0].PageAddress=0x201;break;
      case 31: edge.Ipa=shadow.BrokerIpa;edge.Index=0;p->Graph.Leaves=&edge;table.Slots=slots;slots[0]=&edge;break;
      case 32: vidmm_local_bytes=0x202000;break;
      case 33: ptes[1].PageAddress=0x202;break;
      case 34: replay_pool_ipa=local_ipa;replay_pool_bytes=vidmm_local_bytes;
        ptes[1].Segment=3;break;
      case 35: ptes[1].ReadOnly=1;break;
    }
    ULONG got=AdmissionG3LiveUpdateReason(p,original,&u,a,&detail);
    if(got!=want){fprintf(stderr,"reason want=%u got=%u\n",want,got);abort();}
    /* Pre-q22d classifier provides an independently executed truth table. */
#ifdef Q22D_COMPARE_BASE
    assert(OldAdmissionG3LiveUpdate(p,original,&u,a)==(want==0));
#endif
    /* Run every reachable refusal through the real BuildPagingBuffer loop.
     * Simulated concurrent invalidation ends the wait before graph mutation. */
    if(want!=0 && want!=1 && want!=3 && want!=4 && want!=7 && want!=10 && want!=13) {
      ADMISSION_G3_STATE *state=calloc(1,sizeof(*state));assert(state);
      void *saved_state=a->GpuvaG3State;DXGKARG_BUILDPAGINGBUFFER args={0};
      InitializeListHead(&state->Processes);InsertTailList(&state->Processes,&p->Link);
      p->Magic=ADMISSION_G3_PROCESS_MAGIC;p->State=state;p->Graph.JobInFlight=true;
      p->Graph.ProcessId=42;state->ActiveProcess=p;state->ActiveFence=99;
      state->LastCompletedFence=98;state->Adapter=a;a->GpuvaG3State=state;
      args.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;args.UpdatePageTable=u;
      args.UpdatePageTable.hProcess=p;args.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_GPU_PHYSICAL;
      args.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentId=2;
      args.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentOffset=original-local_ipa;
      unsigned before=wait_captures, delays=replay_delay_calls;
      diagnostic_process=p;replay_delay_hook=poison_during_wait;
      assert(AdmissionGpuvaG3BuildPagingBuffer(a,&args)==STATUS_INVALID_DEVICE_STATE);
      assert(wait_captures==before+1 && replay_delay_calls==delays+1);
      assert(wait_capture.Reason==want && wait_capture.Iterations==1 && wait_capture.Exit==3);
      assert(wait_capture.ProcessId==42 && wait_capture.ActiveFence==99 && wait_capture.CompletedFence==98);
      assert(wait_capture.TableIpa==original && wait_capture.Level==u.PageTableLevel);
      replay_delay_hook=NULL;free(state);a->GpuvaG3State=saved_state;
    }
    if(want>=21&&want<=35) assert(detail.Index==(want>=33 ? 1 : 0));
    local_ipa=saved_base;vidmm_local_bytes=saved_bytes;
    replay_local_view_status=STATUS_SUCCESS;replay_pool_ipa=replay_pool_bytes=0;
  }
  wait_captures=0;
  puts("all 36 preflight reasons PASS; 29 real DDI wait reasons PASS");
}

static void check_wait_exit_paths(ADMISSION_CONTEXT *a) {
  ADMISSION_G3_STATE *s=calloc(1,sizeof(*s));ADMISSION_G3_PROCESS *p=calloc(1,sizeof(*p));
  void *saved=a->GpuvaG3State;assert(s&&p);a->GpuvaG3State=s;s->Adapter=a;
  InitializeListHead(&s->Processes);InsertTailList(&s->Processes,&p->Link);
  p->Magic=ADMISSION_G3_PROCESS_MAGIC;p->State=s;p->Graph.ProcessId=42;
  p->Graph.JobInFlight=true;p->Graph.LiveAdditive=true;
  DXGK_PTE pte={0};pte.Flags=0x41;pte.PageAddress=0x200;
  DXGKARG_BUILDPAGINGBUFFER args={0};args.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;
  args.UpdatePageTable.hProcess=p;args.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_GPU_PHYSICAL;
  args.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentId=2;
  args.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentOffset=0x8000;
  args.UpdatePageTable.NumPageTableEntries=1;args.UpdatePageTable.pPageTableEntries=&pte;
  unsigned before=wait_captures;replay_delay_hook=NULL;
  assert(AdmissionGpuvaG3BuildPagingBuffer(a,&args)==STATUS_DEVICE_BUSY);
  assert(wait_captures==before+1 && wait_capture.Reason==19 && wait_capture.Exit==4);
  assert(wait_capture.Iterations==3000 && p->Graph.JobInFlight);
  memset(&args,0,sizeof(args));args.Operation=DXGK_OPERATION_FLUSH_TLB;
  args.FlushTlb.hProcess=p;args.FlushTlb.RootPageTableAddress.SegmentId=2;
  args.FlushTlb.RootPageTableAddress.SegmentOffset=0x8000;
  args.FlushTlb.StartVirtualAddress=0x8000;args.FlushTlb.EndVirtualAddress=0xc000;
  p->BootstrapIpa=p->Graph.RootIpa=local_ipa+0x8000;p->Graph.LiveAdditive=false;
  diagnostic_process=p;replay_delay_hook=retire_diagnostic_job;
  assert(AdmissionGpuvaG3BuildPagingBuffer(a,&args)==0);
  assert(wait_captures==before+2 && wait_capture.Reason==2 && wait_capture.Exit==1);
  assert(wait_capture.Operation==12 && wait_capture.Va==0x8000 && wait_capture.RangeBytes==0x4000);
  assert(AdmissionGpuvaG3BuildPagingBuffer(a,&args)==0 && wait_captures==before+2);
  /* Each mutation kind reports its own coordinates; idle and fence-only
   * packets report nothing. Timeout retains the existing unlocked return. */
  a->LiveAdditivePaging=1;a->PagingActiveNode=1;a->PagingRecordCount=1;
  for(unsigned kind=0;kind<3;++kind) {
    ADMISSION_PAGING_RECORD *r=&a->PagingRecords[0];memset(r,0,sizeof(*r));
    r->Kind=kind;r->Bytes=0x80;
    if(kind==0){r->Plan.Kind=AppleAgxPhysicalPagingFill;r->Plan.LocalOffset=0x4500;r->Plan.Bytes=0x80;}
    else{r->Virtual.DestinationVa=0x6700;r->Virtual.SourceVa=kind==2 ? 0x2300 : 0;}
    p->Graph.JobInFlight=true;replay_delay_hook=retire_diagnostic_job;
    BOOLEAN held=FALSE;before=wait_captures;
    assert(AdmissionPagingAcquireMutation(a,&held)==0 && held);
    assert(wait_captures==before+1 && wait_capture.Reason==36 && wait_capture.RangeBytes==0x80);
    assert(wait_capture.Operation==(kind==0 ? 260 : kind==1 ? 9 : 8));
    assert(wait_capture.AddressKind==(kind==0 ? 2 : 1));
    assert(wait_capture.Va==(kind==0 ? 0x4500 : 0x6700));
    ExReleaseFastMutex(&s->Lock);
    assert(AdmissionPagingAcquireMutation(a,&held)==0 && held && wait_captures==before+1);
    ExReleaseFastMutex(&s->Lock);
  }
  p->Graph.JobInFlight=true;replay_delay_hook=NULL;
  BOOLEAN held=TRUE;before=wait_captures;
  assert(AdmissionPagingAcquireMutation(a,&held)==STATUS_DEVICE_BUSY && !held);
  assert(wait_captures==before+1 && wait_capture.Exit==4 && wait_capture.Iterations==3000);
  assert(replay_irql==PASSIVE_LEVEL);
  replay_delay_hook=NULL;wait_captures=0;a->GpuvaG3State=saved;free(p);free(s);
}

static void check_virtual_record_addresses(ADMISSION_CONTEXT *a,ADMISSION_G3_PROCESS *p) {
  ADMISSION_RENDER_CONTEXT c={0};ADMISSION_OBJECT_DEVICE device={0};
  ADMISSION_PAGING_MARKER marker={0};ADMISSION_PAGING_RECORD record={0};
  DXGKARG_BUILDPAGINGBUFFER args={0};
  device.Adapter=&a->ObjectAdapter;c.Object.Magic=ADMISSION_OBJECT_CONTEXT_MAGIC;
  c.Object.Device=&device;c.GpuvaG3Process=p;c.GpuvaG3RootIpa=p->Graph.RootIpa;
  args.hSystemContext=&c;args.Operation=DXGK_OPERATION_VIRTUAL_TRANSFER;
  args.pDmaBuffer=&marker;args.DmaSize=sizeof(marker);
  args.pDmaBufferPrivateData=&record;args.DmaBufferPrivateDataSize=sizeof(record);
  args.TransferVirtual.SourceVirtualAddress=0x4000;
  args.TransferVirtual.DestinationVirtualAddress=0x6000;
  args.TransferVirtual.TransferSizeInBytes=0x1800;
  args.TransferVirtual.TransferDirection=DXGK_MEMORY_TRANSFER_LOCAL_TO_LOCAL;
  assert(AdmissionGpuvaG3BuildPagingBuffer(a,&args)==0);
  assert(record.Virtual.SourceVa==0x4000 && record.Virtual.DestinationVa==0x6000);
  assert(record.Bytes==0x1800 && record.SourceIpa!=record.Virtual.SourceVa);
  assert(marker.RecordBytes==sizeof(record) && sizeof(record)==128);
  assert(AdmissionPagingRecordsValid(&record,1,64,sizeof(marker)));
  a->PagingRecords[0]=record;
}
