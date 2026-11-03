/* Real KMD/graph/broker closure; OS allocation and stage2 are fixture services. */
#include <unistd.h>
#include <sys/wait.h>
static APPLE_AGX_GPUVA_G3_GRAPH *waiting_graph;
static unsigned observed_nested;
static void retire_one_job(void) {
  assert(waiting_graph && waiting_graph->JobInFlight);
  if (waiting_graph->JobNested) ++observed_nested;
  assert(AppleAgxGpuvaG3GraphEndJob(waiting_graph));
}
static NTSTATUS map_update(ADMISSION_CONTEXT *a, ADMISSION_G3_PROCESS *p,
    UINT level, ULONGLONG offset, UINT first, UINT count, DXGK_PTE *ptes) {
  DXGKARG_BUILDPAGINGBUFFER args={0};
  args.Operation=DXGK_OPERATION_UPDATE_PAGE_TABLE;
  args.UpdatePageTable.hProcess=p;
  args.UpdatePageTable.UpdateMode=DXGK_PAGETABLEUPDATE_GPU_PHYSICAL;
  args.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentId=2;
  args.UpdatePageTable.PageTableAddress.GpuPhysical.SegmentOffset=offset;
  args.UpdatePageTable.PageTableLevel=level;
  args.UpdatePageTable.StartIndex=first;
  args.UpdatePageTable.NumPageTableEntries=count;
  args.UpdatePageTable.FirstPteVirtualAddress=(ULONGLONG)first*0x1000ULL;
  args.UpdatePageTable.pPageTableEntries=ptes;
  args.UpdatePageTable.Flags.InitialUpdate=ptes[0].Valid ? 0u : 1u;
  return AdmissionGpuvaG3BuildPagingBuffer(a,&args);
}
int main(void) {
  ADMISSION_CONTEXT *a=calloc(1,sizeof(*a));
  ADMISSION_G3_STATE *state=calloc(1,sizeof(*state));
  ADMISSION_G3_PROCESS *p=calloc(1,sizeof(*p));
  REPLAY_BROKER b={0};
  APPLE_AGX_GPUVA_V5_IO io={&b,ReplayWrite64,ReplayRead64,ReplayWrite32,ReplayBarrier,0};
  DXGK_PTE leaf[8]={0}, parent={0}, zeros[4]={0};
  unsigned i, before;
  ADMISSION_G3_LIVE_WAIT_ENTRY detail={0};
  ULONGLONG leaf_broker=0, root_broker=0;
  assert(a&&state&&p);
  assert(posix_memalign((void **)&local_cpu,0x4000,(size_t)local_bytes)==0);
  memset(local_cpu,0,(size_t)local_bytes);
  ReplayBrokerInit(&b);InitializeListHead(&state->Processes);
  assert(AppleAgxGpuvaV5ClientInit(&state->Client,&io));
  {AGX_GPUVA_V5_REQUEST q={0};AGX_GPUVA_V5_RESPONSE r={0};q.Command=AGX_GPUVA_V5_CREATE;
   assert(AppleAgxGpuvaV5ClientCall(&state->Client,&q,&r));}
  assert(AppleAgxGpuvaV5ClientHasLiveAdditive(&state->Client));
  a->Started=TRUE;a->GpuvaG3State=state;a->LiveAdditivePaging=1;state->Adapter=a;
  p->State=state;p->Magic=ADMISSION_G3_PROCESS_MAGIC;p->BootstrapIpa=local_ipa+0x100000;
  assert(AppleAgxGpuvaG3GraphInit(&p->Graph,&state->Client,1,1,
      AdmissionG3AllocateNode,AdmissionG3FreeNode,NULL));
  p->Graph.SharedBackingGeneration=local_ipa;
  assert(AppleAgxGpuvaG3GraphCreate(&p->Graph,p->BootstrapIpa,false));
  InsertTailList(&state->Processes,&p->Link);state->ProcessCount=1;
  for(i=0;i<8;++i){leaf[i].Flags=0x41;leaf[i].PageAddress=0x200+i;}
  assert(map_update(a,p,0,0x8000,0,4,leaf)==0);
  parent.Flags=0x41;parent.PageTableAddress=8;
  assert(map_update(a,p,1,0x4000,0,1,&parent)==0);
  parent.PageTableAddress=4;
  assert(map_update(a,p,2,0x10000,0,1,&parent)==0);
  assert(AdmissionGpuvaG3BrokerTable(p,local_ipa+0x8000,FALSE,&leaf_broker)==0);
  assert(AdmissionGpuvaG3BrokerTable(p,local_ipa+0x10000,FALSE,&root_broker)==0);
  assert(p->Graph.RootIpa==root_broker);
  assert(AppleAgxGpuvaG3GraphNegotiateLive(&p->Graph,true));
  assert(AppleAgxGpuvaG3GraphBeginJob(&p->Graph,1));
  assert(AppleAgxGpuvaG3GraphBeginNestedJob(&p->Graph));
  before=replay_delay_calls;
  assert(map_update(a,p,0,0x8000,4,4,leaf+4)==0);
  assert(replay_delay_calls==before && p->Graph.JobInFlight && p->Graph.JobNested);
  assert(AppleAgxGpuvaG3GraphLeaf(&p->Graph,leaf_broker,1));
  /* Whole-parent preflight rejects a later self-reference before first store. */
  DXGK_PTE parents[2]={{0},{0}};
  parents[0].Flags=parents[1].Flags=0x41;
  parents[0].PageTableAddress=12;parents[1].PageTableAddress=16;
  DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE parent_update={0};
  parent_update.PageTableLevel=1;parent_update.NumPageTableEntries=2;
  parent_update.pPageTableEntries=parents;
  assert(AdmissionG3LiveUpdateReason(p,local_ipa+0x10000,&parent_update,a,&detail)!=AdmissionLiveWaitEligible);
  parents[1].PageTableAddress=20;
  assert(AdmissionG3LiveUpdateReason(p,local_ipa+0x14000,&parent_update,a,&detail)!=AdmissionLiveWaitEligible);
  /* A completely new table and parent edge are additive too. */
  assert(map_update(a,p,0,0xc000,0,4,zeros)==0);
  assert(replay_delay_calls==before && p->Graph.JobNested);
  assert(map_update(a,p,0,0xc000,0,4,leaf)==0);
  parent.PageTableAddress=12;
  assert(map_update(a,p,1,0x4000,1,1,&parent)==0);
  assert(replay_delay_calls==before && p->Graph.JobNested);
  assert(!AppleAgxGpuvaG3GraphNegotiateLive(&p->Graph,false));
  /* A later destructive member must stop the entire batch before publication. */
  DXGK_PTE mixed[8];memcpy(mixed,leaf,sizeof(mixed));mixed[7].Flags=0;
  DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE update={0};
  update.NumPageTableEntries=8;update.pPageTableEntries=mixed;update.StartIndex=8;
  update.FirstPteVirtualAddress=0x8000;
  assert(AdmissionG3LiveUpdateReason(p,local_ipa+0x8000,&update,a,&detail)!=AdmissionLiveWaitEligible);
  update.NumPageTableEntries=1;update.pPageTableEntries=leaf;update.StartIndex=9;
  update.FirstPteVirtualAddress=0x9000;
  assert(AdmissionG3LiveUpdateReason(p,local_ipa+0x8000,&update,a,&detail)!=AdmissionLiveWaitEligible);
  /* Fault isolation intentionally exits the quarantined child without freeing
   * retained backing. Parent checks child assertions and cleans its own graph. */
  pid_t child=fork();assert(child>=0);
  if(child==0){
    DXGK_PTE fresh[8]={{0}};
    for(i=0;i<8;++i){fresh[i].Flags=0x41;fresh[i].PageAddress=0x300+i;}
    live_invalidate_count=0;live_fail_invalidation=2;
    NTSTATUS failure=map_update(a,p,0,0x8000,8,8,fresh);
    assert(!NT_SUCCESS(failure)&&p->Poisoned&&p->Graph.Uncertain&&gpuva_v5.tainted);
    ADMISSION_G3_TABLE_SHADOW *shadow=p->TableShadows;
    while(shadow&&shadow->BrokerIpa!=leaf_broker)shadow=shadow->Next;
    assert(shadow&&shadow->PendingPtes&&shadow->ResidentPtes);
    assert(shadow->PendingPtes[8].GuestIpa==local_ipa+0x300000);
    assert(shadow->PendingPtes[12].GuestIpa==local_ipa+0x304000);
    const APPLE_AGX_GPUVA_G3_NODE *visible=AppleAgxGpuvaG3GraphLeaf(&p->Graph,leaf_broker,2);
    assert(visible&&visible->AuxIpa==local_ipa+0x300000);
    unsigned retained=0;
    for(APPLE_AGX_GPUVA_G3_NODE *backing=p->Graph.Backings;backing;backing=backing->Next)
      if(backing->Ipa==local_ipa+0x300000||backing->Ipa==local_ipa+0x304000)++retained;
    assert(retained==2&&p->Graph.JobInFlight&&p->Graph.JobNested);
    assert(hv_agx_gpuva_v5_backing_by_ipa(&gpuva_v5,local_ipa+0x300000));
    assert(hv_agx_gpuva_v5_backing_by_ipa(&gpuva_v5,local_ipa+0x304000));
    _Exit(0);
  }
  int child_status=0;assert(waitpid(child,&child_status,0)==child);
  assert(WIFEXITED(child_status)&&WEXITSTATUS(child_status)==0);
  /* Real BuildPagingBuffer must wait for BOTH depth-two jobs before unmap. */
  waiting_graph=&p->Graph;replay_delay_hook=retire_one_job;
  assert(map_update(a,p,0,0x8000,0,4,zeros)==0);
  assert(replay_delay_calls==before+2 && observed_nested==1 && !p->Graph.JobInFlight);
  assert(!AppleAgxGpuvaG3GraphLeaf(&p->Graph,leaf_broker,0));
  replay_delay_hook=NULL;
  /* Actual destructive-worker gate: fence-only bypass, then retain the
   * job-admission mutex after both jobs have ended until CPU writes stop. */
  assert(AppleAgxGpuvaG3GraphBeginJob(&p->Graph,1));
  assert(AppleAgxGpuvaG3GraphBeginNestedJob(&p->Graph));
  a->PagingActiveNode=1;a->PagingRecordCount=1;
  a->PagingRecords[0].Kind=AdmissionPagingMonitoredFence;
  BOOLEAN held=FALSE;
  before=replay_delay_calls;
  assert(AdmissionPagingAcquireMutation(a,&held)==0 && !held);
  assert(p->Graph.JobNested && replay_delay_calls==before);
  a->PagingRecords[0].Kind=AdmissionPagingVirtualFill;
  replay_delay_hook=retire_one_job;
  assert(AdmissionPagingAcquireMutation(a,&held)==0 && held);
  assert(replay_delay_calls==before+2 && !p->Graph.JobInFlight);
  assert(replay_irql==APC_LEVEL);
  ExReleaseFastMutex(&state->Lock);
  assert(replay_irql==PASSIVE_LEVEL);
  replay_delay_hook=NULL;
  assert(AppleAgxGpuvaG3GraphDestroy(&p->Graph));
  assert(!p->Graph.LiveAdditive);
  assert(AppleAgxGpuvaG3GraphCreate(&p->Graph,p->BootstrapIpa,false));
  assert(!p->Graph.LiveAdditive);
  assert(AppleAgxGpuvaG3GraphDestroy(&p->Graph));
  while(p->TableShadows){ADMISSION_G3_TABLE_SHADOW *s=p->TableShadows;p->TableShadows=s->Next;
    free(s->LogicalPtes);free(s->ResidentPtes);free(s->PendingPtes);free(s);}
  free(p);free(state);free(a);free(local_cpu);
  puts("live KMD publication PASS");return 0;
}
