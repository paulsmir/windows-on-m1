#include "apple_agx_g4_builder.h"
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
#endif
#include "apple_agx_render_template_rebase.h"
typedef struct {
  struct { struct { int (*PrivateEscape)(void *,APPLE_AGX_G3_PRIVATE_REQUEST *); } Ops; void *Context; } Gpuva;
} AGX_WIN32_ASAHI_BACKEND;
typedef struct { APPLE_AGX_G4_PRIVATE_LEASE Lease; } AGX_G4_BATCH;
typedef struct { ADMISSION_CONTEXT *Adapter; DXGKARG_ESCAPE Escape; } R137_TRANSPORT;
static int r137_escape_transport(void *opaque,APPLE_AGX_G3_PRIVATE_REQUEST *q) {
  R137_TRANSPORT *t=opaque;
  t->Escape.pPrivateDriverData=q;t->Escape.PrivateDriverDataSize=sizeof(*q);
  return NT_SUCCESS(AdmissionDdiEscape(t->Adapter,&t->Escape));
}
#include "g4_mesa_attachment_shim.h"
#include "g3_r137_mesa_prepare.inc"
static void r137_private_combined(void) {
  ADMISSION_CONTEXT a={0};ADMISSION_G3_STATE state={0};REPLAY_BROKER broker={0};
  APPLE_AGX_GPUVA_V5_IO io={&broker,ReplayWrite64,ReplayRead64,ReplayWrite32,ReplayBarrier, 0};
  assert(posix_memalign((void **)&local_cpu,0x4000,(size_t)local_bytes)==0);
  memset(local_cpu,0,(size_t)local_bytes);
  ReplayBrokerInit(&broker);InitializeListHead(&state.Processes);
  assert(AppleAgxGpuvaV5ClientInit(&state.Client,&io));
  {AGX_GPUVA_V5_REQUEST r={0};AGX_GPUVA_V5_RESPONSE reply={0};
   r.Command=AGX_GPUVA_V5_CREATE;assert(AppleAgxGpuvaV5ClientCall(&state.Client,&r,&reply));}
  a.Started=TRUE;a.GpuvaG3State=&state;state.Adapter=&a;a.ObjectAdapter=&a;
  ADMISSION_G3_PROCESS *p=sys_process(&a,0x10000,0);
  DXGK_PTE command[16]={0},color[250]={0};
  for(unsigned i=0;i<16;++i) {command[i].Flags=1;command[i].PageAddress=(system_ipa+i*0x1000ULL)>>12;}
  expect_ok("R137 command backing",sys_update(&a,p,local_cpu+0x18000,0,16,16,command,0,0));
  for(unsigned i=0;i<250;++i) {color[i].Flags=0x41;color[i].PageAddress=(0x200000ULL+i*0x10000ULL)>>12;}
  expect_ok("R137 ordinary primary",sys_update(&a,p,local_cpu+0x18000,0,2,250,color,1,0));
  ADMISSION_DEVICE device={0};ADMISSION_RENDER_CONTEXT context={0};
  device.Object.Magic=ADMISSION_OBJECT_DEVICE_MAGIC;device.Object.Adapter=&a.ObjectAdapter;device.GpuvaG3Process=p;
  context.Object.Magic=ADMISSION_OBJECT_CONTEXT_MAGIC;context.Object.Device=&device.Object;
  context.Win32Transport=TRUE;context.GpuvaG3RootIpa=p->Graph.RootIpa;
  expect_ok("R137 combined attach",AdmissionGpuvaG3AttachContext(&context,&device));
  /* Force this process's real private leaf maps to backing above physical
   * offset32MiB while its reserved GPUVA still spans exactly32MiB. */
  APPLE_AGX_G3_PRIVATE_EXTENT high_backing_fill[4]={{0}};
  for(unsigned i=0;i<4;++i)
    assert(AppleAgxG3PrivateAllocate(&state.PrivatePool,100+i,8u<<20,&high_backing_fill[i]));
  R137_TRANSPORT t={.Adapter=&a,.Escape={.hDevice=&device,.hContext=&context,.hKmdProcessHandle=p,.Flags={1}}};
  AGX_WIN32_ASAHI_BACKEND umd={.Gpuva={.Ops={r137_escape_transport},.Context=&t}};
  AGX_G4_BATCH batch={0};
  AGX_G4_PRIVATE packet={0};
  APPLE_AGX_G4_NATIVE_RENDER render={0}, *r=&render;
  r->WidthPx=2560;r->HeightPx=1600;r->Layers=r->Samples=1;r->SampleSizeBytes=8;
  r->UtileWidthPx=r->UtileHeightPx=16;r->Flags=1u<<2;
  r->VdmCtrlStreamBase=r->IspScissorBase=r->IspDbiasBase=0x10000;
  APPLE_AGX_G4_PROCESS_RANGE ranges[9];
  /* EXP1115 (Asahi buffer.rs): a 1280x800 render backs only its
   * min_tvb_blocks of the 32-block heap window; the 2560x1600 ACQUIRE below
   * grows the idle manager through the real escape and broker. */
  APPLE_AGX_G4_PROCESS_RANGE small_ranges[9];
  {
    APPLE_AGX_G4_NATIVE_RENDER small=render;
    AGX_G4_BATCH small_batch={0};
    APPLE_AGX_G3_PRIVATE_REQUEST drop={0};
    ULONGLONG ipa=0;
    small.WidthPx=1280;small.HeightPx=800;small.UtileWidthPx=small.UtileHeightPx=32;
    assert(prepare_process_buffers(&umd,&small_batch,&small,small_ranges));
    assert(p->PrivateManager.Blocks==8u && p->PrivateManager.GrownCount==0u);
    assert(small_ranges[2].Va==p->PrivateVa+APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET);
    assert(small_ranges[2].Bytes==32u*0x20000u);
    assert(AppleAgxGpuvaG3GraphTranslateVa(&p->Graph,small_ranges[2].Va+8u*0x20000u-0x4000u,&ipa));
    assert(!AppleAgxGpuvaG3GraphTranslateVa(&p->Graph,small_ranges[2].Va+8u*0x20000u,&ipa));
    /* The submit parser accepts the heap window although only its backed
     * prefix is mapped: the firmware is told about those blocks only. */
    {
      AGX_G4_PRIVATE small_packet={0};
      struct agx_resource small_target={.bo=&small_target,.va=0x20000,
          .layout={.size_B=1280ULL*800*4}};
      struct agx_batch small_mesa={.key={.nr_cbufs=1,
          .cbufs={{.texture=&small_target}}}};
      APPLE_AGX_G4_NATIVE_HEADER small_header={.Type=APPLE_AGX_G4_RENDER,.Size=sizeof(small)};
      APPLE_AGX_G4_SUBMIT_VIEW small_view={0};
      APPLE_AGX_G4_FAILURE small_failure={0};
      ADMISSION_G3_PRIVATE_SCENE *small_scene;
      unsigned small_bytes;
      assert(append_attachments(&small_mesa,&small_packet));
      assert(append_native(&small_packet,&small_header,sizeof(small_header)));
      assert(append_native(&small_packet,&small,sizeof(small)));
      small_bytes=small_packet.Header.V2.Base.CommandBytes;
      assert(AppleAgxG4ComposeHeaderV3(&small_packet.Header,&small,0x10000,small_bytes,
          APPLE_AGX_G4_COLOR_BGRA8,small_ranges,&small_batch.Lease));
      small_scene=AdmissionG4FindPrivateScene(p,&context,&small_batch.Lease,41,FALSE);
      assert(small_scene);
      assert(AppleAgxG4ParseSubmitEx(&small_packet,sizeof(small_packet),
          sizeof(small_packet.Header)+small_bytes,0x10000,small_bytes,
          AdmissionG4PrivateGraphAccess,small_scene,&small_view,&small_failure)==AppleAgxG4ParseOk);
    }
    drop.Magic=APPLE_AGX_G3_PRIVATE_MAGIC;drop.Version=1;drop.Bytes=sizeof(drop);
    drop.Operation=APPLE_AGX_G3_PRIVATE_RELEASE;
    drop.ManagerId=small_batch.Lease.ManagerId;drop.ManagerGeneration=small_batch.Lease.ManagerGeneration;
    drop.SceneId=small_batch.Lease.SceneId;drop.SceneGeneration=small_batch.Lease.SceneGeneration;
    assert(r137_escape_transport(&t,&drop));
    assert(!p->PrivateScenes);
  }
  assert(prepare_process_buffers(&umd,&batch,r,ranges));
  assert(p->PrivateManager.Blocks==32u && p->PrivateManager.GrownCount==1u);
  assert(state.PrivateStats[ADMISSION_G3_PRIVATE_STAT_HEAP_GROW]==1u);
  assert(ranges[2].Va==small_ranges[2].Va && ranges[2].Bytes==small_ranges[2].Bytes);
  {
    ULONGLONG ipa=0;
    assert(AppleAgxGpuvaG3GraphTranslateVa(&p->Graph,ranges[2].Va+8u*0x20000u,&ipa));
    assert(ipa==local_ipa+vidmm_local_bytes+p->PrivateManager.Grown[0].Offset);
    assert(AppleAgxGpuvaG3GraphTranslateVa(&p->Graph,ranges[2].Va+32u*0x20000u-0x4000u,&ipa));
  }
  /* Exercise the existing per-owner refusal through the actual typed escape.
   * A later failure must not overwrite its pre-rollback first-cause receipt. */
  {
    /* 4 table/list units, a 64-unit heap and 27-unit scenes (2560x1600,
     * 16x16 utiles): the per-owner quota fits `fit` scenes. */
    const unsigned fit=(APPLE_AGX_G3_PROCESS_UNITS-68u)/27u;
    AGX_G4_BATCH extra[8]={{0}}, refused={0};
    APPLE_AGX_G4_PROCESS_RANGE scratch[9];
    APPLE_AGX_G3_PRIVATE_REQUEST drop={0};
    a.PhysicalDeviceObject=(PDEVICE_OBJECT)1;
    ULONG writes=private_registry_writes;
    assert(fit>=2u && fit<=8u);
    for(unsigned i=1;i<fit;++i) assert(prepare_process_buffers(&umd,&extra[i],r,scratch));
    assert(private_registry_writes==writes);
    assert(!prepare_process_buffers(&umd,&refused,r,scratch));
    assert(a.G3PrivateFailureClaim==2 && private_registry_writes==writes+1);
    APPLE_AGX_G3_PRIVATE_FAILURE first=a.G3PrivateFailure;
    assert(first.Version==1 && first.Bytes==sizeof(first));
    assert(first.Branch==11 && first.Status==(UINT)STATUS_INSUFFICIENT_RESOURCES);
    assert(first.PreparePredicate==3 && first.FailedRange<9);
    assert(first.ProcessId==p->Graph.ProcessId && first.ContextHandle==(uintptr_t)&context);
    assert(first.SceneCount==fit && first.ContextSceneCount==fit && first.ManagerPresent);
    assert(!memcmp(&first,&private_registry_receipt,sizeof(first)));
    assert(!prepare_process_buffers(&umd,&refused,r,scratch));
    assert(!memcmp(&first,&a.G3PrivateFailure,sizeof(first)));
    assert(private_registry_writes==writes+1);
    drop.Magic=APPLE_AGX_G3_PRIVATE_MAGIC;drop.Version=1;drop.Bytes=sizeof(drop);
    drop.Operation=APPLE_AGX_G3_PRIVATE_RELEASE;
    for(unsigned i=1;i<fit;++i) {
      drop.ManagerId=extra[i].Lease.ManagerId;drop.ManagerGeneration=extra[i].Lease.ManagerGeneration;
      drop.SceneId=extra[i].Lease.SceneId;drop.SceneGeneration=extra[i].Lease.SceneGeneration;
      assert(r137_escape_transport(&t,&drop));
    }
  }
  struct agx_resource target={.bo=&target,.va=0x20000,
      .layout={.size_B=2560ULL*1600*4}};
  struct agx_batch mesa_batch={.key={.nr_cbufs=1,
      .cbufs={{.texture=&target}}}};
  assert(append_attachments(&mesa_batch,&packet));
  APPLE_AGX_G4_NATIVE_HEADER render_header={.Type=APPLE_AGX_G4_RENDER,.Size=sizeof(*r)};
  assert(append_native(&packet,&render_header,sizeof(render_header)));
  assert(append_native(&packet,r,sizeof(*r)));
  unsigned bytes=packet.Header.V2.Base.CommandBytes;
  assert(AppleAgxG4ComposeHeaderV3(&packet.Header,r,0x10000,bytes,APPLE_AGX_G4_COLOR_BGRA8,ranges,&batch.Lease));
  ADMISSION_G3_PRIVATE_SCENE *scene=AdmissionG4FindPrivateScene(p,&context,&batch.Lease,41,FALSE);
  assert(scene);
  APPLE_AGX_G4_SUBMIT_VIEW view={0};
  APPLE_AGX_G4_FAILURE failure={0};
  APPLE_AGX_G4_PARSE_RESULT parsed=AppleAgxG4ParseSubmitEx(&packet,sizeof(packet),sizeof(packet.Header)+bytes,0x10000,bytes,
      AdmissionG4PrivateGraphAccess,scene,&view,&failure);
  if(parsed!=AppleAgxG4ParseOk) fprintf(stderr,"private parse=%u subsite=%u kind=%u ordinal=%u va=%llx bytes=%u\n",
      parsed,failure.Subsite,failure.Kind,failure.Ordinal,failure.Va,failure.Bytes);
  assert(parsed==AppleAgxG4ParseOk);
  assert(AdmissionG4PrivateGeometry(scene,&view));
  unsigned char *arena=malloc(AppleAgxRenderTemplateBytes());assert(arena);
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  APPLE_AGX_EXP208_RELOCATION_OBJECT objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT]={{0}};
  assert(AppleAgxRenderTemplateMaterialize(arena,AppleAgxRenderTemplateBytes(),&roots));
  assert(AppleAgxRenderTemplateBuildRelocationObjectsRebased(arena,AppleAgxRenderTemplateBytes(),
      0x800000000ULL,0x1503800000ULL,0x1503800000ULL,AppleAgxRenderTemplateBytes(),objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,&roots));
  assert(AppleAgxG4BuildTa3d(&view,arena,AppleAgxRenderTemplateBytes(),1,objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
  for(unsigned i=0;i<9;++i) {
    ULONGLONG ipa=0;
    assert(AppleAgxGpuvaG3GraphTranslateVa(&p->Graph,ranges[i].Va,&ipa));
    const APPLE_AGX_G3_PRIVATE_EXTENT *extent=i<3 ? &p->PrivateManager.Extents[i] : &scene->Storage.Extents[i-3];
    assert(ipa==local_ipa+vidmm_local_bytes+extent->Offset);
  }
  /* Queue owner is exercised by the outer-DDI replay. Model its exact hold
   * here, retaining the real BeginJob/parser/graph/wire/firmware builder. */
  scene->Queued=1;scene->Fence=41;context.GpuvaG3PrivateFence=41;
  if (getenv("G3_REPLAY_R154_RESUBMIT")) {
    /* Submit transfer is tested by the real envelope/scheduler replay. Here
     * drive real private storage through suspension, transfer and completion. */
    scene->Fence=40;context.GpuvaG3PrivateFence=40;
    APPLE_AGX_G3_PRIVATE_REQUEST early={0};
    early.Magic=APPLE_AGX_G3_PRIVATE_MAGIC;early.Version=1;early.Bytes=sizeof(early);
    early.Operation=APPLE_AGX_G3_PRIVATE_RELEASE;
    early.ManagerId=batch.Lease.ManagerId;early.ManagerGeneration=batch.Lease.ManagerGeneration;
    early.SceneId=batch.Lease.SceneId;early.SceneGeneration=batch.Lease.SceneGeneration;
    assert(r137_escape_transport(&t,&early));
    replay_irql=DISPATCH_LEVEL;
    AdmissionGpuvaG3PrivatePreempt(&context,999); /* Wrong fence cannot publish. */
    assert(!context.GpuvaG3PreemptFence);
    AdmissionGpuvaG3PrivatePreempt(&context,40);
    replay_irql=PASSIVE_LEVEL;
    assert(AdmissionG3PrivateReap(p) && p->PrivateScenes==scene && scene->Queued);
    assert(!AdmissionG4FindPrivateScene(p,&context,&batch.Lease,41,FALSE));
    assert(AdmissionG4FindPrivateResubmission(p,&context,&batch.Lease,41)==scene);
    scene->Fence=41;context.GpuvaG3PrivateFence=41;context.GpuvaG3PreemptFence=0;
  }
  a.BackendImage.G4Native=1;a.BackendImage.BoundFence=41;
  a.BackendImage.G4CommandBytes=bytes;a.BackendImage.G4Lease=batch.Lease;
  assert(AppleAgxG4ComposeHeaderV2(&a.BackendImage.G4Header,r,0x10000,bytes,
      APPLE_AGX_G4_COLOR_BGRA8,ranges));
  memcpy(a.BackendImage.Commands,view.Native,bytes);
  context.GpuvaG3MappingGeneration=p->Graph.MappingGeneration;
  context.GpuvaG3DmaBufferVa=0x10000;context.GpuvaG3DmaBufferBytes=bytes;
  ++a.BackendImage.G4Lease.SceneGeneration;
  assert(!NT_SUCCESS(AdmissionGpuvaG3BeginJob(&a,&context,41)));
  --a.BackendImage.G4Lease.SceneGeneration;
  expect_ok("R137 private BeginJob",AdmissionGpuvaG3BeginJob(&a,&context,41));
  assert(scene->Started && p->Graph.JobInFlight);
  ADMISSION_PLATFORM_RUNTIME runtime={.Adapter=&a};
  a.InterfaceValid=TRUE;a.Interface.DxgkCbSynchronizeExecution=replay_sync;
  a.Interface.DxgkCbNotifyInterrupt=replay_notify;a.Interface.DxgkCbQueueDpc=replay_queue_dpc;
  a.RenderPacket.State=AdmissionRenderPacketActive;
  a.RenderPacket.Description.Fence=41;a.RenderPacket.Description.ContextToken=(ULONGLONG)(ULONG_PTR)&context;
  context.Object.FenceOutstanding=41;replay_active_fence=41;replay_sync_fail=1;
  APPLE_AGX_G3_PRIVATE_REQUEST release={0};
  release.Magic=APPLE_AGX_G3_PRIVATE_MAGIC;release.Version=1;release.Bytes=sizeof(release);
  release.Operation=APPLE_AGX_G3_PRIVATE_RELEASE;
  release.ManagerId=batch.Lease.ManagerId;release.ManagerGeneration=batch.Lease.ManagerGeneration;
  release.SceneId=batch.Lease.SceneId;release.SceneGeneration=batch.Lease.SceneGeneration;
  unsigned scratch_offset=scene->Storage.Extents[0].Offset,scratch_bytes=scene->Storage.Extents[0].Bytes;
  unsigned scratch_va_offset=scene->Storage.Extents[0].VaOffset;
  unsigned char *scratch=local_cpu+(40u<<20)+scratch_offset;
  memset(scratch,0x5a,scratch_bytes); /* Model completed GPU writes. */
  assert(r137_escape_transport(&t,&release));
  assert(scene->Queued && scene->ReleaseRequested && scratch[0]==0x5a);
  assert(!AdmissionGpuvaG3PrivateReported(&a,&context,41));
  runtime.Backend.TaComplete=1;
  assert(!AdmissionBackendComplete(&runtime,41,0,0,AppleAgxBackendCompletionSuccess));
  assert(p->Graph.JobInFlight && scene->Queued);
  runtime.Backend.D3Complete=1;
  assert(!AdmissionBackendComplete(&runtime,41,0,0,AppleAgxBackendCompletionSuccess));
  assert(!p->Graph.JobInFlight && scene->Queued && !replay_notify_count);
  assert(context.Object.FenceOutstanding==41 && AdmissionGpuvaG3PrivateContextBusy(&context));
  assert(!NT_SUCCESS(AdmissionGpuvaG3BeginJob(&a,&context,99)));
  assert(!p->Graph.JobInFlight); /* Notify retry owns the adapter boundary. */
  assert(scratch[0]==0x5a && state.PrivatePool.Blocks[scratch_offset>>16].Owner);
  const char *fault=getenv("G3_REPLAY_R137_QUARANTINE");
  replay_sync_fail=0;
  assert(AdmissionBackendComplete(&runtime,41,0,0,AppleAgxBackendCompletionSuccess));
  assert(replay_notify_count==1 && !context.Object.FenceOutstanding);
  /* EXP1086: the reported, released scene stays mapped for reuse. */
  assert(p->PrivateScenes==scene && scene->Cached && !context.GpuvaG3PrivateFence);
  assert(state.PrivatePool.Blocks[scratch_offset>>16].Owner && scratch[0]==0x5a);
  if(fault && !strcmp(fault,"revoke")) {
    /* Its eventual unmap (here: context retirement) quarantines on a failed revoke. */
    assert(!p->Poisoned);
    broker.sync_failures=2;
    assert(!AdmissionGpuvaG3PrivateRetireContext(&context));
    assert(p->Poisoned && p->Graph.Uncertain && scene->Quarantined);
    assert(state.PrivatePool.Blocks[scratch_offset>>16].Owner && scratch[0]==0x5a);
    goto Quarantine;
  }
  AGX_G4_BATCH next={0};
  UINT broker_commands=broker.commands;
  assert(prepare_process_buffers(&umd,&next,r,ranges));
  /* Reuse: no table change and no broker call; zeroed as construction does. */
  assert(broker.commands==broker_commands);
  assert(p->PrivateScenes==scene && !scene->Cached && !scene->ReleaseRequested);
  for(unsigned i=0;i<scratch_bytes;++i) assert(!scratch[i]);
  assert(next.Lease.SceneGeneration!=batch.Lease.SceneGeneration);
  assert(!r137_escape_transport(&t,&release)); /* Old generation cannot release reused bytes. */
  scene=p->PrivateScenes;assert(scene && scene->Storage.Ranges[3].Va==p->PrivateVa+scratch_va_offset);
  if (getenv("G3_REPLAY_R154_RESUBMIT")) {
    const char *mode=getenv("G3_REPLAY_R154_RESUBMIT");
    scene->Queued=1;scene->Fence=42;context.GpuvaG3PrivateFence=42;
    release.SceneId=next.Lease.SceneId;release.SceneGeneration=next.Lease.SceneGeneration;
    if (strcmp(mode,"late-release")) assert(r137_escape_transport(&t,&release));
    replay_irql=DISPATCH_LEVEL;
    AdmissionGpuvaG3PrivatePreempt(&context,42);
    replay_irql=PASSIVE_LEVEL;
    assert(AdmissionG3PrivateReap(p));
    if (!strcmp(mode,"late-release")) assert(r137_escape_transport(&t,&release));
    assert(p->PrivateScenes==scene); /* A deferred UMD release cannot free a suspended packet. */
    assert(scene->Queued && !scene->Started && scene->ReleaseRequested);
    if (!strcmp(mode,"uncertain")) {
      replay_irql=DISPATCH_LEVEL;
      AdmissionGpuvaG3PrivateCancel(&context,42,TRUE);
      replay_irql=PASSIVE_LEVEL;
      assert(!AdmissionG3PrivateReap(p));
      assert(scene->Quarantined && p->PrivateScenes==scene && scene->Queued);
      assert(!AdmissionG4FindPrivateResubmission(p,&context,&next.Lease,43));
      assert(!AdmissionGpuvaG3PrivateRetireContext(&context));
      goto Quarantine;
    }
    if (!strcmp(mode,"cancel")) {
      replay_irql=DISPATCH_LEVEL;
      AdmissionGpuvaG3PrivateCancel(&context,42,FALSE);
      replay_irql=PASSIVE_LEVEL;
      assert(AdmissionG3PrivateReap(p) && !p->PrivateScenes);
      assert(!context.GpuvaG3PreemptFence && !context.GpuvaG3PrivateFence);
      assert(!AdmissionG4FindPrivateResubmission(p,&context,&next.Lease,43));
    }
    assert(AdmissionGpuvaG3PrivateRetireContext(&context));
    assert(!p->PrivateScenes && !context.GpuvaG3PrivateFence);
    AdmissionGpuvaG3DetachContext(&context);
    expect_ok("R154 suspended teardown",AdmissionDdiDestroyProcess(&a,p));
    free(arena);free(local_cpu);local_cpu=NULL;
    puts("R154 suspended private lifetime: PASS");
    return;
  }
  scene->Queued=1;scene->Submitting=1;scene->Fence=42;context.GpuvaG3PrivateFence=42;
  release.SceneId=next.Lease.SceneId;release.SceneGeneration=next.Lease.SceneGeneration;
  assert(r137_escape_transport(&t,&release));
  replay_irql=DISPATCH_LEVEL;
  AdmissionGpuvaG3PrivateCancel(&context,999,FALSE);assert(!context.GpuvaG3CancelFence);
  AdmissionGpuvaG3PrivateCancel(&context,42,FALSE);
  assert(replay_irql==DISPATCH_LEVEL && scene->Queued);
  replay_irql=PASSIVE_LEVEL;
  assert(AdmissionGpuvaG3PrivateContextBusy(&context)); /* Submit still owns its local pointer. */
  AdmissionG4PrivateUnqueue(p,scene,42);
  assert(!AdmissionGpuvaG3PrivateContextBusy(&context));
  assert(!p->PrivateScenes && !state.PrivatePool.Blocks[scratch_offset>>16].Owner);
  if(fault && !strcmp(fault,"reset")) {
    next=(AGX_G4_BATCH){0};assert(prepare_process_buffers(&umd,&next,r,ranges));
    scene=p->PrivateScenes;scene->Queued=1;scene->Fence=43;
    context.GpuvaG3PrivateFence=43;context.GpuvaG3CancelFence=0;
    a.BackendImage.G4Native=1;a.BackendImage.BoundFence=43;
    a.BackendImage.G4Lease=next.Lease;a.BackendImage.G4CommandBytes=bytes;
    assert(AppleAgxG4ComposeHeaderV2(&a.BackendImage.G4Header,r,0x10000,bytes,
        APPLE_AGX_G4_COLOR_BGRA8,ranges));
    memcpy(a.BackendImage.Commands,view.Native,bytes);
    context.GpuvaG3MappingGeneration=p->Graph.MappingGeneration;
    expect_ok("R137 job before uncertain reset",AdmissionGpuvaG3BeginJob(&a,&context,43));
    assert(!AdmissionGpuvaG3PrivateReset(&a));
    replay_irql=DISPATCH_LEVEL;AdmissionGpuvaG3PrivateCancel(&context,43,TRUE);replay_irql=0;
    context.Object.FenceOutstanding=0; /* Software reset does not authorize release. */
    assert(AdmissionGpuvaG3PrivateContextBusy(&context));
    assert(scene->Quarantined && p->Poisoned && p->Graph.JobInFlight);
    assert(!AdmissionGpuvaG3PrivateRetireContext(&context));
    goto Quarantine;
  }
  assert(AdmissionGpuvaG3PrivateRetireContext(&context));
  assert(!p->PrivateManager.Generation); /* Last context drops manager ownership. */
  AdmissionGpuvaG3DetachContext(&context);
  expect_ok("R137 combined destroy",AdmissionDdiDestroyProcess(&a,p));
  for(unsigned i=0;i<4;++i)
    assert(AppleAgxG3PrivateFree(&state.PrivatePool,100+i,&high_backing_fill[i]));
  free(arena);free(local_cpu);local_cpu=NULL;
  puts("R137 private combined: PASS");
  return;
Quarantine:
  assert(AdmissionDdiDestroyProcess(&a,p)==STATUS_DEVICE_BUSY);
  assert(!r137_escape_transport(&t,&release));
  /* Intentional adapter-lifetime retention has no quiescence proof. Do not
   * invent a successful reset just to satisfy the host leak checker. */
#if __has_feature(address_sanitizer)
  __lsan_ignore_object(p);
#endif
  free(arena);
  puts("R137 private quarantine: PASS");
}
