#include "apple_agx_g4_builder.h"
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
  scene->Queued=1;scene->Fence=41;
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
  assert(AdmissionGpuvaG3CompleteJob(&a,41));
  assert(scene->Queued && AdmissionGpuvaG3PrivateContextBusy(&context));
  /* Step 6 retains the queue hold; only fixture teardown bypasses the future
   * OS-notification owner. Step 7 replaces this with its real retirement hook. */
  scene->Queued=0;
  AdmissionGpuvaG3DetachContext(&context);
  expect_ok("R137 combined destroy",AdmissionDdiDestroyProcess(&a,p));
  free(arena);free(local_cpu);local_cpu=NULL;
  puts("R137 private combined: PASS");
}
