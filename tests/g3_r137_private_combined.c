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
#include "g3_r137_mesa_prepare.inc"
static void r137_private_combined(void) {
  ADMISSION_CONTEXT a={0};ADMISSION_G3_STATE state={0};REPLAY_BROKER broker={0};
  APPLE_AGX_GPUVA_V5_IO io={&broker,ReplayWrite64,ReplayRead64,ReplayWrite32,ReplayBarrier};
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
  R137_TRANSPORT t={.Adapter=&a,.Escape={.hDevice=&device,.hContext=&context,.hKmdProcessHandle=p,.Flags={1}}};
  AGX_WIN32_ASAHI_BACKEND umd={.Gpuva={.Ops={r137_escape_transport},.Context=&t}};
  AGX_G4_BATCH batch={0};
  struct {
    APPLE_AGX_G4_PRIVATE_HEADER_V3 Header;
    APPLE_AGX_G4_NATIVE_HEADER AttachmentCommand;
    APPLE_AGX_G4_ATTACHMENT Attachment;
    APPLE_AGX_G4_NATIVE_HEADER RenderCommand;
    APPLE_AGX_G4_NATIVE_RENDER Render;
  } packet={0};
  APPLE_AGX_G4_NATIVE_RENDER *r=&packet.Render;
  r->WidthPx=2560;r->HeightPx=1600;r->Layers=r->Samples=1;r->SampleSizeBytes=8;
  r->UtileWidthPx=r->UtileHeightPx=16;r->Flags=1u<<2;
  r->VdmCtrlStreamBase=r->IspScissorBase=r->IspDbiasBase=0x10000;
  APPLE_AGX_G4_PROCESS_RANGE ranges[9];
  assert(prepare_process_buffers(&umd,&batch,r,ranges));
  packet.AttachmentCommand=(APPLE_AGX_G4_NATIVE_HEADER){.Type=APPLE_AGX_G4_FRAGMENT_ATTACHMENTS,.Size=sizeof(packet.Attachment),.VdmBarrier=0xffff,.CdmBarrier=0xffff};
  packet.Attachment=(APPLE_AGX_G4_ATTACHMENT){0x20000,2560ULL*1600*4,0,0};
  packet.RenderCommand=(APPLE_AGX_G4_NATIVE_HEADER){.Type=APPLE_AGX_G4_RENDER,.Size=sizeof(*r)};
  unsigned bytes=sizeof(packet)-sizeof(packet.Header);
  assert(AppleAgxG4ComposeHeaderV3(&packet.Header,r,0x10000,bytes,APPLE_AGX_G4_COLOR_BGRA8,ranges,&batch.Lease));
  ADMISSION_G3_PRIVATE_SCENE *scene=AdmissionG4FindPrivateScene(p,&context,&batch.Lease,41,FALSE);
  assert(scene);
  APPLE_AGX_G4_SUBMIT_VIEW view={0};
  APPLE_AGX_G4_FAILURE failure={0};
  APPLE_AGX_G4_PARSE_RESULT parsed=AppleAgxG4ParseSubmitEx(&packet,sizeof(packet),sizeof(packet),0x10000,bytes,
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
    assert(ipa>=local_ipa+(40ULL<<20) && ipa<local_ipa+(56ULL<<20));
  }
  /* Queue owner is exercised by the outer-DDI replay. Model its exact hold
   * here, retaining the real BeginJob/parser/graph/wire/firmware builder. */
  scene->Queued=1;scene->Fence=41;context.GpuvaG3PrivateFence=41;
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
  if(fault && !strcmp(fault,"revoke")) broker.sync_failures=2;
  replay_sync_fail=0;
  assert(AdmissionBackendComplete(&runtime,41,0,0,AppleAgxBackendCompletionSuccess));
  assert(replay_notify_count==1 && !context.Object.FenceOutstanding);
  if(fault && !strcmp(fault,"revoke")) {
    assert(p->Poisoned && p->Graph.Uncertain && scene->Quarantined);
    assert(state.PrivatePool.Blocks[scratch_offset>>16].Owner && scratch[0]==0x5a);
    assert(!AdmissionGpuvaG3PrivateRetireContext(&context));
    goto Quarantine;
  }
  assert(!p->PrivateScenes && !context.GpuvaG3PrivateFence);
  assert(!state.PrivatePool.Blocks[scratch_offset>>16].Owner);
  for(unsigned i=0;i<scratch_bytes;++i) assert(!scratch[i]);
  AGX_G4_BATCH next={0};
  assert(prepare_process_buffers(&umd,&next,r,ranges));
  assert(next.Lease.SceneGeneration!=batch.Lease.SceneGeneration);
  assert(!r137_escape_transport(&t,&release)); /* Old generation cannot release reused bytes. */
  scene=p->PrivateScenes;assert(scene && scene->Storage.Ranges[3].Va==p->PrivateVa+scratch_offset);
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
