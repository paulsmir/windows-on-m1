#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_g4_submit.h"

typedef int NTSTATUS;
typedef unsigned char BOOLEAN;
typedef unsigned long long ULONGLONG;
typedef unsigned long ULONG;
typedef unsigned long long ULONG_PTR;
typedef unsigned long long SIZE_T;
typedef unsigned char *PUCHAR;
typedef void *PVOID;
typedef int KIRQL;
#define PASSIVE_LEVEL 0
#define TRUE 1
#define FALSE 0
#define MAXULONG 0xffffffffu
#define MAXULONGLONG (~0ULL)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define NT_SUCCESS(s) ((s)>=0)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define RtlCopyMemory(d,s,n) memcpy((d),(s),(n))
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_INVALID_ADDRESS ((NTSTATUS)0xC0000141)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BB)
#define ADMISSION_CONTEXT_VIRTUAL_ADDRESSING 4u
#define ADMISSION_CONTEXT_SYSTEM 1u
#define ADMISSION_CONTEXT_GDI 2u

typedef struct {
  ULONGLONG RootIpa;
  unsigned Uncertain;
  unsigned AllowProcessRanges;
} APPLE_AGX_GPUVA_G3_GRAPH;
typedef struct _ADMISSION_G3_STATE {
  int Lock;
} ADMISSION_G3_STATE;
typedef struct _ADMISSION_G3_PROCESS {
  ADMISSION_G3_STATE *State;
  APPLE_AGX_GPUVA_G3_GRAPH Graph;
  unsigned Poisoned;
} ADMISSION_G3_PROCESS;
typedef struct {
  unsigned Flags;
  unsigned FenceOutstanding;
} ADMISSION_OBJECT_CONTEXT;
typedef struct {
  unsigned Fence, AllocationCount;
  ULONGLONG ContextToken, AllocationToken, PrivateDataToken;
  unsigned PrivateDataBytes, PrivateDataStart, PrivateDataEnd;
  unsigned DmaStart, DmaEnd, DestinationIndex, DestinationBytes;
  ULONGLONG DestinationCpuToken, DestinationGpuVa, DestinationPhysical;
} ADMISSION_RENDER_PACKET_DESCRIPTION;
typedef struct {
  unsigned State;
  ADMISSION_RENDER_PACKET_DESCRIPTION Description;
} ADMISSION_RENDER_PACKET;
enum { AdmissionRenderPacketEmpty, AdmissionRenderPacketPrepared,
       AdmissionRenderPacketQueued };
typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V2 G4Header;
  unsigned G4Native, BoundFence;
} ADMISSION_BACKEND_IMAGE;
typedef struct { unsigned DestinationBytes; } APPLE_AGX_EXP208_GDI_BINDING;
typedef struct {
  PVOID CpuAddress;
  ULONGLONG GuestIpaAddress, HostPhysicalAddress, Bytes;
} ADMISSION_SCANOUT_MEMORY_VIEW;
typedef struct {
  void *GpuvaG3State;
  int SchedulerLock, SchedulerFaulted, Scheduler, RuntimeReady;
  ADMISSION_RENDER_PACKET RenderPacket;
  ADMISSION_BACKEND_IMAGE BackendImage;
} ADMISSION_CONTEXT;
typedef struct {
  ADMISSION_G3_PROCESS *GpuvaG3Process;
  unsigned GpuvaG3Poisoned;
  ULONGLONG GpuvaG3RootIpa;
  ULONGLONG GpuvaG3DmaBufferVa;
  unsigned GpuvaG3DmaBufferBytes;
  unsigned Win32Transport;
  ADMISSION_OBJECT_CONTEXT Object;
  struct { unsigned Active; } SchedulerContext;
} ADMISSION_RENDER_CONTEXT;
typedef struct {
  struct { unsigned Value; } Flags;
  const void *pDmaBufferPrivateData;
  unsigned DmaBufferPrivateDataSize;
  unsigned DmaBufferUmdPrivateDataSize;
  ULONGLONG DmaBufferVirtualAddress;
  unsigned DmaBufferSize;
  unsigned SubmissionFenceId;
} DXGKARG_SUBMITCOMMANDVIRTUAL;

static int replay_irql, dispatches, bind_ok=1;
static APPLE_AGX_G4_PROCESS_RANGE ranges[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
static int KeGetCurrentIrql(void) { return replay_irql; }
static void ExAcquireFastMutex(int *lock) { assert(!*lock); *lock = 1; }
static void ExReleaseFastMutex(int *lock) { assert(*lock); *lock = 0; }
static void KeAcquireSpinLock(int *lock, KIRQL *irql) {
  assert(!*lock);*lock=1;*irql=0;
}
static void KeReleaseSpinLock(int *lock, KIRQL irql) {
  (void)irql;assert(*lock);*lock=0;
}
static int InterlockedCompareExchange(int *value, int exchange, int compare) {
  int old=*value;if(old==compare)*value=exchange;return old;
}
static int AdmissionPlatformRuntimeReady(ADMISSION_CONTEXT *adapter) {
  return adapter->RuntimeReady;
}
static NTSTATUS AdmissionMemoryRuntimeScanoutView(
    ADMISSION_CONTEXT *adapter, ADMISSION_SCANOUT_MEMORY_VIEW *view) {
  (void)adapter;
  static unsigned char memory[0x4000];
  view->CpuAddress=memory;view->GuestIpaAddress=0x90000000ULL;
  view->HostPhysicalAddress=0x80000000ULL;view->Bytes=sizeof(memory);
  return STATUS_SUCCESS;
}
static int AppleAgxGpuvaG3GraphTranslateVa(
    APPLE_AGX_GPUVA_G3_GRAPH *graph, ULONGLONG va, ULONGLONG *ipa) {
  (void)graph;
  if(va<0x40000000ULL || va>=0x40001000ULL)return 0;
  *ipa=0x90001000ULL+(va-0x40000000ULL);return 1;
}
static unsigned AdmissionRenderPacketState(ADMISSION_RENDER_PACKET *packet) {
  return packet->State;
}
static int AdmissionRenderPacketPrepare(ADMISSION_RENDER_PACKET *packet,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *description) {
  packet->Description=*description;packet->State=AdmissionRenderPacketPrepared;
  return 1;
}
static int AdmissionRenderPacketCancelPrepared(ADMISSION_RENDER_PACKET *packet,
    ULONGLONG context) {
  (void)context;packet->State=AdmissionRenderPacketEmpty;return 1;
}
static int AdmissionRenderPacketQueue(ADMISSION_RENDER_PACKET *packet,
    unsigned fence, ULONGLONG context, ULONGLONG private_data,
    unsigned start, unsigned end) {
  (void)fence;(void)context;(void)private_data;(void)start;(void)end;
  packet->State=AdmissionRenderPacketQueued;return 1;
}
static int AdmissionRenderPacketReset(ADMISSION_RENDER_PACKET *packet,
    unsigned fence, unsigned quiesced) {
  (void)fence;(void)quiesced;packet->State=AdmissionRenderPacketEmpty;return 1;
}
static int AppleAgxSchedulerQueueFence(int *scheduler, unsigned node,
    unsigned engine, unsigned fence) {
  (void)scheduler;(void)node;(void)engine;return fence!=0u;
}
static int AdmissionBackendImageBindG4Submission(ADMISSION_BACKEND_IMAGE *image,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *packet, void *cpu,
    const APPLE_AGX_G4_SUBMIT_VIEW *view,
    APPLE_AGX_EXP208_GDI_BINDING *binding) {
  (void)cpu;(void)view;(void)binding;
  if(!bind_ok)return 0;
  image->G4Native=1;image->BoundFence=packet->Fence;return 1;
}
static int AdmissionBackendImageReleaseSubmission(
    ADMISSION_BACKEND_IMAGE *image, unsigned fence) {
  (void)fence;image->G4Native=0;return 1;
}
static void AdmissionDispatchQueuedWork(ADMISSION_CONTEXT *adapter) {
  (void)adapter;++dispatches;
}
static int AppleAgxGpuvaG3GraphContainsRangeAccess(
    APPLE_AGX_GPUVA_G3_GRAPH *graph, ULONGLONG va, unsigned bytes,
    int write) {
  if (!graph || !bytes) return 0;
  if (va == 0x20000 && !write && bytes == 280) return 1;
  if (va == 0x30000 && !write && bytes == 1) return 1;
  if (va == 0x40000000ULL && write &&
      (bytes==1u || bytes==4096u)) return 1;
  for(unsigned i=0;i<APPLE_AGX_G4_PROCESS_RANGE_COUNT;++i)
    if(va==ranges[i].Va && bytes==ranges[i].Bytes && write)
      return graph->AllowProcessRanges;
  return 0;
}

/* The Python driver inserts the unmodified production KMD functions here. */
#include "g4_submit_virtual_functions.inc"

typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V2 Header;
  APPLE_AGX_G4_NATIVE_HEADER AttachCommand;
  APPLE_AGX_G4_ATTACHMENT Attachment;
  APPLE_AGX_G4_NATIVE_HEADER Command;
  APPLE_AGX_G4_NATIVE_RENDER Render;
} PACKET;

int main(void) {
  PACKET packet = {0};
  ADMISSION_G3_STATE state = {0};
  ADMISSION_G3_PROCESS process = {0};
  ADMISSION_CONTEXT adapter = {0};
  ADMISSION_RENDER_CONTEXT context = {0};
  DXGKARG_SUBMITCOMMANDVIRTUAL args = {0};
  unsigned required[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
  process.State = &state;
  process.Graph.RootIpa = 0x9bf000000ULL;
  process.Graph.AllowProcessRanges = 1;
  adapter.GpuvaG3State = &state;
  adapter.RuntimeReady=1;
  context.GpuvaG3Process = &process;
  context.GpuvaG3RootIpa = process.Graph.RootIpa;
  context.Win32Transport = 1;
  context.Object.Flags = ADMISSION_CONTEXT_VIRTUAL_ADDRESSING;
  context.SchedulerContext.Active=1;
  packet.Header.Base.Magic = APPLE_AGX_G4_PRIVATE_MAGIC;
  packet.Header.Base.Version = APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA;
  packet.Header.Base.HeaderBytes = sizeof(packet.Header);
  packet.Header.Base.CommandBytes = sizeof(packet) - sizeof(packet.Header);
  packet.Header.Base.CommandVa = 0x20000;
  packet.Header.Base.Reserved = APPLE_AGX_G4_COLOR_BGRA8;
  packet.AttachCommand.Type=APPLE_AGX_G4_FRAGMENT_ATTACHMENTS;
  packet.AttachCommand.Size=sizeof(packet.Attachment);
  packet.AttachCommand.VdmBarrier=0xffffu;
  packet.AttachCommand.CdmBarrier=0xffffu;
  packet.Attachment.Pointer=0x40000000ULL;
  packet.Attachment.Size=4096u;
  packet.Command.Type = APPLE_AGX_G4_RENDER;
  packet.Command.Size = sizeof(packet.Render);
  packet.Render.VdmCtrlStreamBase = 0x30000;
  packet.Render.WidthPx = 32;
  packet.Render.HeightPx = 32;
  packet.Render.Layers = 1;
  packet.Render.UtileWidthPx = 32;
  packet.Render.UtileHeightPx = 32;
  packet.Render.Samples = 1;
  packet.Render.SampleSizeBytes = 8;
  packet.Render.Flags=1u<<2;
  assert(AppleAgxG4ProcessRequiredBytes(&packet.Render,required));
  for (unsigned i = 0; i < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++i) {
    ranges[i].Va = 0x100000ULL + (ULONGLONG)i * 0x400000ULL;
    ranges[i].Bytes = required[i];
    packet.Header.Process[i]=ranges[i];
  }
  args.pDmaBufferPrivateData = &packet;
  args.DmaBufferPrivateDataSize = sizeof(packet);
  args.DmaBufferUmdPrivateDataSize = sizeof(packet);
  args.DmaBufferVirtualAddress = 0x20000;
  args.DmaBufferSize = packet.Header.Base.CommandBytes;
  args.SubmissionFenceId=7u;
  assert(AdmissionG4SubmitVirtualEnvelope(&adapter, &context, &args) ==
         STATUS_SUCCESS);
  assert(dispatches==1 && adapter.RenderPacket.State==
         AdmissionRenderPacketQueued);
  assert(context.Object.FenceOutstanding==7u &&
         context.GpuvaG3DmaBufferVa==args.DmaBufferVirtualAddress);
  adapter.RenderPacket.State=AdmissionRenderPacketEmpty;
  context.Object.FenceOutstanding=0u;
  adapter.BackendImage.G4Native=0u;
  adapter.BackendImage.BoundFence=0u;
  process.Graph.AllowProcessRanges = 0;
  args.SubmissionFenceId=8u;
  assert(AdmissionG4SubmitVirtualEnvelope(&adapter, &context, &args) ==
         STATUS_INVALID_PARAMETER);
  process.Graph.AllowProcessRanges = 1;
  bind_ok=0;
  assert(AdmissionG4SubmitVirtualEnvelope(&adapter,&context,&args)==
         STATUS_INVALID_PARAMETER);
  assert(adapter.RenderPacket.State==AdmissionRenderPacketEmpty &&
         context.Object.FenceOutstanding==0u);
  bind_ok=1;
  process.Graph.Uncertain = 1;
  assert(AdmissionG4SubmitVirtualEnvelope(&adapter, &context, &args) ==
         STATUS_INVALID_PARAMETER);
  assert(state.Lock == 0);
  puts("g4_submit_virtual_replay: PASS");
  return 0;
}
