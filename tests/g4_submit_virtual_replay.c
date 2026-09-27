#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_g4_submit.h"
#include "apple_agx_g3_private_storage.h"
#include "apple_agx_gpuva_g3_translation.h"

typedef int NTSTATUS;
typedef void VOID;
typedef unsigned char BOOLEAN;
typedef unsigned long long ULONGLONG;
typedef uint32_t ULONG, UINT;
typedef uintptr_t ULONG_PTR;
typedef size_t SIZE_T;
typedef unsigned char *PUCHAR;
typedef void *PVOID;
typedef int KIRQL;
#define _Use_decl_annotations_
#define DISPATCH_LEVEL 2
typedef void *HANDLE;
typedef struct { unsigned Value; } REPLAY_FLAGS;
#define PASSIVE_LEVEL 0
#define TRUE 1
#define FALSE 0
#define MAXULONG 0xffffffffu
#define MAXULONGLONG (~0ULL)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define NT_SUCCESS(s) ((s)>=0)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define RtlCopyMemory(d,s,n) memcpy((d),(s),(n))
#define RtlCompareMemory(a,b,n) (memcmp((a),(b),(n))==0 ? (n) : 0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_INVALID_ADDRESS ((NTSTATUS)0xC0000141)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184)
#define STATUS_DEVICE_HARDWARE_ERROR ((NTSTATUS)0xC0000185)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BB)
#define ADMISSION_CONTEXT_VIRTUAL_ADDRESSING 4u
#define ADMISSION_CONTEXT_SYSTEM 1u
#define ADMISSION_CONTEXT_GDI 2u
#define ADMISSION_MEMORY_LOCAL_SEGMENT 2u

typedef struct _APPLE_AGX_GPUVA_G3_NODE {
  struct _APPLE_AGX_GPUVA_G3_NODE *Next;
  ULONGLONG Ipa, AuxIpa;
  unsigned Index;
} APPLE_AGX_GPUVA_G3_NODE;
typedef struct _ADMISSION_G3_TABLE_SHADOW {
  struct _ADMISSION_G3_TABLE_SHADOW *Next;
  ULONGLONG BrokerIpa;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *LogicalPtes;
} ADMISSION_G3_TABLE_SHADOW;
typedef struct {
  ULONGLONG RootIpa;
  ULONGLONG ProcessId, ProcessGeneration, NextGeneration;
  APPLE_AGX_GPUVA_G3_NODE *Parents;
  unsigned Created, Uncertain;
  ULONGLONG MappingGeneration;
  unsigned AllowProcessRanges;
} APPLE_AGX_GPUVA_G3_GRAPH;
typedef struct _ADMISSION_G3_STATE {
  int Lock;
  struct _ADMISSION_G3_PROCESS *ActiveProcess;
  unsigned ActiveFence;
} ADMISSION_G3_STATE;
typedef struct _ADMISSION_RENDER_CONTEXT ADMISSION_RENDER_CONTEXT;
typedef struct _ADMISSION_G3_PRIVATE_SCENE {
  struct _ADMISSION_G3_PRIVATE_SCENE *Next;
  ADMISSION_RENDER_CONTEXT *Context;
  APPLE_AGX_G3_PRIVATE_SCENE Storage;
  APPLE_AGX_G4_NATIVE_RENDER Geometry;
  ULONG Fence, Queued, Started, GpuDone, Reported, ReleaseRequested, Quarantined;
} ADMISSION_G3_PRIVATE_SCENE;

typedef struct _ADMISSION_G3_PROCESS {
  ULONGLONG PrivateVa;
  APPLE_AGX_G3_PRIVATE_MANAGER PrivateManager;
  ADMISSION_G3_PRIVATE_SCENE *PrivateScenes;
  ADMISSION_G3_STATE *State;
  APPLE_AGX_GPUVA_G3_GRAPH Graph;
  ADMISSION_G3_TABLE_SHADOW *TableShadows;
  unsigned Poisoned;
} ADMISSION_G3_PROCESS;
typedef struct { void *Adapter; } REPLAY_DEVICE;
typedef struct {
  unsigned Flags, Magic;
  REPLAY_DEVICE *Device;
  unsigned FenceOutstanding;
} ADMISSION_OBJECT_CONTEXT;
#define ADMISSION_OBJECT_CONTEXT_MAGIC 0x47444358u
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
  unsigned char G4Command[APPLE_AGX_G4_NATIVE_MAX_BYTES];
  unsigned G4CommandBytes, G4Native, BoundFence;
  APPLE_AGX_G4_PRIVATE_LEASE G4Lease;
} ADMISSION_BACKEND_IMAGE;
typedef struct { unsigned DestinationBytes; } APPLE_AGX_EXP208_GDI_BINDING;
typedef struct {
  PVOID CpuAddress;
  ULONGLONG GuestIpaAddress, HostPhysicalAddress, Bytes;
} ADMISSION_SCANOUT_MEMORY_VIEW;
typedef struct {
  void *GpuvaG3State;
  int Started;
  int ObjectAdapter;
  volatile int G4SubmitFailureClaim;
  volatile int G4SubmitFailureCount;
  struct _ADMISSION_G4_SUBMIT_FAILURE {
    unsigned Version, Bytes, Branch, Status, DownstreamStatus;
    ULONGLONG DmaBufferVirtualAddress;
    unsigned DmaBufferSize, PrivateDataSize, UmdPrivateDataSize;
    unsigned Flags, ContextFlags, Pid, TotalFailures;
    unsigned Subsite, Kind, Ordinal, AccessBytes, Write, GraphPresent;
    ULONGLONG Va, OwnerProcessId, RootIpa, ProcessGeneration,
        MappingGeneration;
    ULONGLONG LogicalIpa[4];
    unsigned LogicalSegment[4], LogicalFlags[4];
  } G4SubmitFailure;
  int SchedulerLock, SchedulerFaulted, Scheduler, RuntimeReady;
  ADMISSION_RENDER_PACKET RenderPacket;
  ADMISSION_BACKEND_IMAGE BackendImage;
} ADMISSION_CONTEXT;
struct _ADMISSION_RENDER_CONTEXT {
  ULONGLONG GpuvaG3PrivateManagerGeneration;
  ADMISSION_G3_PROCESS *GpuvaG3Process;
  unsigned GpuvaG3Poisoned;
  ULONGLONG GpuvaG3RootIpa;
  ULONGLONG GpuvaG3DmaBufferVa, GpuvaG3MappingGeneration;
  unsigned GpuvaG3DmaBufferBytes;
  unsigned Win32Transport;
  ADMISSION_OBJECT_CONTEXT Object;
  struct { unsigned Active; } SchedulerContext;
};
typedef struct {
  REPLAY_FLAGS Flags;
  void *hContext;
  unsigned NodeOrdinal, EngineOrdinal, VidPnSourceId, FlipInterval;
  const void *pDmaBufferPrivateData;
  unsigned DmaBufferPrivateDataSize;
  unsigned DmaBufferUmdPrivateDataSize;
  ULONGLONG DmaBufferVirtualAddress;
  unsigned DmaBufferSize;
  unsigned SubmissionFenceId;
} DXGKARG_SUBMITCOMMANDVIRTUAL;
typedef struct { unsigned BytesUsed; } APPLE_AGX_DMA_SHADOW;
typedef struct {
  void *hContext;
  ULONGLONG DmaBufferVirtualAddress;
  unsigned DmaBufferSize, DmaBufferSubmissionStartOffset;
  unsigned DmaBufferSubmissionEndOffset;
  const void *pDmaBufferPrivateData;
  unsigned DmaBufferPrivateDataSize;
  unsigned DmaBufferPrivateDataSubmissionStartOffset;
  unsigned DmaBufferPrivateDataSubmissionEndOffset;
  unsigned SubmissionFenceId, VidPnSourceId, FlipInterval;
  REPLAY_FLAGS Flags;
  unsigned EngineOrdinal, NodeOrdinal;
} DXGKARG_SUBMITCOMMAND;
#define ADMISSION_GDI_DMA_PRIVATE_SIZE 64u
static NTSTATUS AdmissionGpuvaG3SubmitVirtualPaging(
    ADMISSION_CONTEXT *a, ADMISSION_RENDER_CONTEXT *c,
    const DXGKARG_SUBMITCOMMANDVIRTUAL *v) {
  (void)a;(void)c;(void)v;return STATUS_INVALID_PARAMETER;
}
static int AppleAgxDmaShadowOpen(APPLE_AGX_DMA_SHADOW *s,
    const void *p, unsigned n) { (void)s;(void)p;(void)n;return 0; }
static NTSTATUS AdmissionDdiSubmitRender(ADMISSION_CONTEXT *a,
    const DXGKARG_SUBMITCOMMAND *p) {
  (void)a;(void)p;return STATUS_INVALID_PARAMETER;
}

static int replay_irql, dispatches, bind_ok=1;
static int graph_begin_calls;
static int output_mapped=1;
static NTSTATUS scanout_status = STATUS_SUCCESS;
static int receipt_queues;
static void __attribute__((unused)) AdmissionRenderCorrelationSubmitFailureWindows(
    ADMISSION_CONTEXT *a) { (void)a; ++receipt_queues; }
static void *__attribute__((unused)) PsGetCurrentProcessId(void) { return (void *)(uintptr_t)1234; }
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
static int InterlockedCompareExchange(volatile int *value, int exchange, int compare) {
  int old=*value;if(old==compare)*value=exchange;return old;
}
static int InterlockedIncrement(volatile int *value) { return ++*value; }
static int InterlockedExchange(volatile int *value, int exchange) {
  int old=*value;*value=exchange;return old;
}
#define KeMemoryBarrier() __sync_synchronize()
#define HandleToULong(x) ((unsigned)(uintptr_t)(x))
#define UNREFERENCED_PARAMETER(x) ((void)(x))
static int AdmissionPlatformRuntimeReady(ADMISSION_CONTEXT *adapter) {
  return adapter->RuntimeReady;
}
static NTSTATUS AdmissionMemoryRuntimeScanoutView(
    ADMISSION_CONTEXT *adapter, ADMISSION_SCANOUT_MEMORY_VIEW *view) {
  (void)adapter;
  if (!NT_SUCCESS(scanout_status)) return scanout_status;
  static unsigned char memory[0x4000];
  view->CpuAddress=memory;view->GuestIpaAddress=0x90000000ULL;
  view->HostPhysicalAddress=0x80000000ULL;view->Bytes=sizeof(memory);
  return STATUS_SUCCESS;
}
static int AppleAgxGpuvaG3GraphTranslateVa(
    APPLE_AGX_GPUVA_G3_GRAPH *graph, ULONGLONG va, ULONGLONG *ipa) {
  (void)graph;
  if (!output_mapped) return 0;
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
  (void)cpu;(void)binding;
  if(!bind_ok)return 0;
  APPLE_AGX_G4_NATIVE_RENDER render;
  memcpy(&render,view->Render,sizeof(render));
  if (!AppleAgxG4ComposeHeaderV2(&image->G4Header,&render,view->CommandVa,
          view->CommandBytes,view->ColorFormat,view->Process)) return 0;
  image->G4Lease=view->Lease;
  memcpy(image->G4Command, view->Native, view->CommandBytes);
  image->G4CommandBytes=view->CommandBytes;
  image->G4Native=1;image->BoundFence=packet->Fence;return 1;
}
static int AppleAgxGpuvaG3GraphContainsRangeAccess(
    APPLE_AGX_GPUVA_G3_GRAPH *graph, ULONGLONG va, unsigned bytes,
    int write);
static int AppleAgxGpuvaG3GraphContainsRange(
    APPLE_AGX_GPUVA_G3_GRAPH *graph, ULONGLONG va, unsigned bytes) {
  return AppleAgxGpuvaG3GraphContainsRangeAccess(graph, va, bytes, 0);
}
static int AdmissionG3OutputMatchesLocal(ADMISSION_CONTEXT *adapter,
    APPLE_AGX_GPUVA_G3_GRAPH *graph) { (void)adapter;(void)graph;return 1; }
static int AppleAgxGpuvaG3GraphBeginJob(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    unsigned slot) { (void)graph;(void)slot;++graph_begin_calls;return 1; }
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
  APPLE_AGX_GPUVA_G3_NODE middle = {0}, root_edge = {0};
  ADMISSION_G3_TABLE_SHADOW shadow = {0};
  static APPLE_AGX_GPUVA_G3_LOGICAL_PTE logical[8192];
  unsigned required[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
  process.State = &state;
  process.PrivateVa = 1ULL << 36;
  process.Graph.RootIpa = 0x9bf000000ULL;
  process.Graph.Created = 1u;
  process.Graph.ProcessId = 17u;
  process.Graph.ProcessGeneration = 23u;
  process.Graph.NextGeneration = 31u;
  process.Graph.MappingGeneration = 31u;
  root_edge.Ipa = process.Graph.RootIpa;
  root_edge.AuxIpa = 0x9bf004000ULL;
  root_edge.Next = &middle;
  middle.Ipa = root_edge.AuxIpa;
  middle.AuxIpa = 0x9bf008000ULL;
  process.Graph.Parents = &root_edge;
  shadow.BrokerIpa = middle.AuxIpa;
  shadow.LogicalPtes = logical;
  process.TableShadows = &shadow;
  for (unsigned i = 0u; i < 4u; ++i) {
    logical[0x100u + i].GuestIpa = 0x90000000ULL + i * 0x1000ULL;
    logical[0x100u + i].SegmentId = 0u;
    logical[0x100u + i].Flags = 3u;
  }
  logical[0x20u].GuestIpa = 0x81000000ULL;
  logical[0x20u].Flags = 1u;
  logical[0x21u].GuestIpa = 0x91003000ULL;
  logical[0x21u].Flags = 1u;
  process.Graph.AllowProcessRanges = 1;
  adapter.GpuvaG3State = &state;
  adapter.RuntimeReady=1;
  context.GpuvaG3Process = &process;
  context.GpuvaG3RootIpa = process.Graph.RootIpa;
  context.Win32Transport = 1;
  context.Object.Flags = ADMISSION_CONTEXT_VIRTUAL_ADDRESSING;
  context.Object.Magic = ADMISSION_OBJECT_CONTEXT_MAGIC;
  static REPLAY_DEVICE device;
  device.Adapter = &adapter.ObjectAdapter;
  context.Object.Device = &device;
  adapter.Started = 1;
  args.hContext = &context;
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
  adapter.G4SubmitFailureClaim = 0;
  adapter.G4SubmitFailureCount = 0;
  memset(&adapter.G4SubmitFailure, 0, sizeof(adapter.G4SubmitFailure));
  receipt_queues = 0;
  args.hContext = NULL;
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
         STATUS_INVALID_PARAMETER);
  assert(adapter.G4SubmitFailure.Branch == 1u);
  assert(adapter.G4SubmitFailure.Status == (unsigned)STATUS_INVALID_PARAMETER);
  assert(adapter.G4SubmitFailureCount == 1 && receipt_queues == 1);
  args.hContext = &context;
  process.Graph.Uncertain = 0;
  packet.Header.Base.Magic = 0;
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
         STATUS_INVALID_PARAMETER);
  assert(adapter.G4SubmitFailure.Branch == 1u);
  assert(adapter.G4SubmitFailureCount == 2);
  assert(adapter.G4SubmitFailure.DmaBufferVirtualAddress == args.DmaBufferVirtualAddress);
  assert(adapter.G4SubmitFailure.Pid == 1234u);
  assert(adapter.G4SubmitFailureClaim == 2 && receipt_queues == 2);
  adapter.G4SubmitFailureClaim = 0;
  adapter.G4SubmitFailureCount = 0;
  memset(&adapter.G4SubmitFailure, 0, sizeof(adapter.G4SubmitFailure));
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
         STATUS_INVALID_PARAMETER);
  assert(adapter.G4SubmitFailure.Branch == 9u);
  assert(adapter.G4SubmitFailure.Version == 2u);
  assert(adapter.G4SubmitFailure.DownstreamStatus != 0u);
  assert(adapter.G4SubmitFailure.PrivateDataSize == sizeof(packet));
  assert(adapter.G4SubmitFailure.UmdPrivateDataSize == sizeof(packet));
  assert(adapter.G4SubmitFailure.DmaBufferSize == args.DmaBufferSize);
  assert(adapter.G4SubmitFailure.Flags == args.Flags.Value);
  assert(adapter.G4SubmitFailure.ContextFlags == context.Object.Flags);
  adapter.G4SubmitFailureClaim = 0;
  process.Graph.AllowProcessRanges = 0;
  packet.Header.Base.Magic = APPLE_AGX_G4_PRIVATE_MAGIC;
  for (unsigned n = 0; n < 129u; ++n)
    assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
           STATUS_INVALID_PARAMETER);
  args.hContext = NULL;
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
         STATUS_INVALID_PARAMETER);
  args.hContext = &context;
  assert(adapter.G4SubmitFailure.Branch == 9u);
  assert(adapter.G4SubmitFailure.Subsite == AppleAgxG4FailureAccess);
  assert(adapter.G4SubmitFailure.Kind == AppleAgxG4AccessProcess);
  assert(adapter.G4SubmitFailure.Ordinal == 0u);
  assert(adapter.G4SubmitFailure.Va == ranges[0].Va);
  assert(adapter.G4SubmitFailure.OwnerProcessId == 17u);
  assert(adapter.G4SubmitFailure.RootIpa == process.Graph.RootIpa);
  assert(adapter.G4SubmitFailure.ProcessGeneration == 23u);
  assert(adapter.G4SubmitFailure.MappingGeneration == 31u);
  assert(adapter.G4SubmitFailure.GraphPresent == 0u);
  for (unsigned i = 0u; i < 4u; ++i) {
    assert(adapter.G4SubmitFailure.LogicalIpa[i] ==
           0x90000000ULL + i * 0x1000ULL);
    assert(adapter.G4SubmitFailure.LogicalSegment[i] == 0u);
    assert(adapter.G4SubmitFailure.LogicalFlags[i] == 3u);
  }
  assert(adapter.G4SubmitFailureCount == 131);
  process.Graph.AllowProcessRanges = 1;
  adapter.G4SubmitFailureClaim = 0;
  output_mapped = 0;
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
         STATUS_INVALID_PARAMETER);
  assert(adapter.G4SubmitFailure.Subsite == AppleAgxG4FailureOutput);
  output_mapped = 1;
  packet.Header.Base.Magic = APPLE_AGX_G4_PRIVATE_MAGIC;
  adapter.G4SubmitFailureClaim = 0;
  adapter.G4SubmitFailureCount = 0;
  memset(&adapter.G4SubmitFailure, 0, sizeof(adapter.G4SubmitFailure));
  context.Object.Magic = 0;
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
         STATUS_INVALID_PARAMETER);
  assert(adapter.G4SubmitFailure.Branch == 2u);
  assert(adapter.G4SubmitFailure.Status == (unsigned)STATUS_INVALID_PARAMETER);
  context.Object.Magic = ADMISSION_OBJECT_CONTEXT_MAGIC;
  adapter.G4SubmitFailureClaim = 0;
  adapter.G4SubmitFailureCount = 0;
  memset(&adapter.G4SubmitFailure, 0, sizeof(adapter.G4SubmitFailure));
  scanout_status = STATUS_INVALID_DEVICE_STATE;
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
         STATUS_INVALID_PARAMETER);
  assert(adapter.G4SubmitFailure.Branch == 7u);
  assert(adapter.G4SubmitFailure.DownstreamStatus ==
         (unsigned)STATUS_INVALID_DEVICE_STATE);
  assert(state.Lock == 0);
  scanout_status = STATUS_SUCCESS;
  adapter.G4SubmitFailureClaim = 0;
  adapter.RenderPacket.State = AdmissionRenderPacketEmpty;
  context.Object.FenceOutstanding = 0u;
  adapter.BackendImage.G4Native = 0u;
  packet.Header.Base.CommandVa = 0x20f80ULL;
  args.DmaBufferVirtualAddress = 0x20f80ULL;
  args.SubmissionFenceId = 13u;
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) == STATUS_SUCCESS);
  assert(context.GpuvaG3DmaBufferVa == 0x20f80ULL);
  ++process.Graph.MappingGeneration; /* invalidate/remap, even the same PFN */
  assert(AdmissionGpuvaG3BeginJob(&adapter, &context, 13u) != STATUS_SUCCESS);
  assert(graph_begin_calls == 0);
  --process.Graph.MappingGeneration;
  assert(AdmissionGpuvaG3BeginJob(&adapter, &context, 13u) == STATUS_SUCCESS);
  assert(graph_begin_calls == 1);
  state.ActiveProcess = NULL;
  assert(AppleAgxGpuvaG3InvalidateLogical64K(logical, 2u, 1u));
  assert(logical[0x20u].Flags == 0u && logical[0x21u].Flags == 0u);
  assert(logical[0x100u].Flags == 3u);
  assert(AdmissionGpuvaG3BeginJob(&adapter, &context, 13u) != STATUS_SUCCESS);
  assert(graph_begin_calls == 1);
  logical[0x20u].GuestIpa = 0x81000000ULL;
  logical[0x20u].Flags = 1u;
  logical[0x21u].GuestIpa = 0x91003000ULL;
  logical[0x21u].Flags = 1u;
  logical[0x21u].Flags = 0u;
  assert(AdmissionGpuvaG3BeginJob(&adapter, &context, 13u) != STATUS_SUCCESS);
  assert(graph_begin_calls == 1);
  logical[0x21u].Flags = 1u;
  context.GpuvaG3DmaBufferVa++;
  assert(AdmissionGpuvaG3BeginJob(&adapter, &context, 13u) != STATUS_SUCCESS);
  assert(graph_begin_calls == 1);
  context.GpuvaG3DmaBufferVa--;
  adapter.RenderPacket.State = AdmissionRenderPacketEmpty;
  context.Object.FenceOutstanding = 0u;
  adapter.BackendImage.G4Native = 0u;
  logical[0x21u].Flags = 0u;
  args.SubmissionFenceId = 14u;
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
         STATUS_INVALID_PARAMETER);
  assert(adapter.G4SubmitFailureClaim == 2);
  logical[0x21u].Flags = 1u;
  context.GpuvaG3RootIpa++;
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
         STATUS_INVALID_PARAMETER);
  context.GpuvaG3RootIpa--;
  args.DmaBufferSize--;
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
         STATUS_INVALID_PARAMETER);
  args.DmaBufferSize++;
  packet.Header.Base.CommandBytes--;
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
         STATUS_INVALID_PARAMETER);
  packet.Header.Base.CommandBytes++;
  packet.Render.VdmCtrlStreamBase = args.DmaBufferVirtualAddress;
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
         STATUS_INVALID_PARAMETER);
  packet.Render.VdmCtrlStreamBase = 0x30000ULL;
  packet.Attachment.Pointer = args.DmaBufferVirtualAddress;
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
         STATUS_INVALID_PARAMETER);
  packet.Attachment.Pointer = 0x40000000ULL;
  packet.Render.Bg.Usc = 0x20040u;
  assert(AdmissionDdiSubmitCommandVirtual(&adapter, &args) ==
         STATUS_INVALID_PARAMETER);
  packet.Render.Bg.Usc=0;
  struct { APPLE_AGX_G4_PRIVATE_HEADER_V3 Header; unsigned char Native[280]; } v3={0};
  ADMISSION_G3_PRIVATE_SCENE scene={0};
  scene.Context=&context;scene.Geometry=packet.Render;scene.Storage.Generation=9;
  process.PrivateManager.Generation=5;context.GpuvaG3PrivateManagerGeneration=5;
  process.PrivateScenes=&scene;
  ULONGLONG private_va=process.PrivateVa;
  for(unsigned i=0;i<9;++i) {
    ranges[i]=(APPLE_AGX_G4_PROCESS_RANGE){private_va,required[i],0};
    scene.Storage.Ranges[i]=ranges[i];private_va+=required[i];
  }
  APPLE_AGX_G4_PRIVATE_LEASE lease={17,5,9,9};
  memcpy(v3.Native,&packet.AttachCommand,sizeof(v3.Native));
  assert(AppleAgxG4ComposeHeaderV3(&v3.Header,&packet.Render,args.DmaBufferVirtualAddress,
      sizeof(v3.Native),APPLE_AGX_G4_COLOR_BGRA8,ranges,&lease));
  args.pDmaBufferPrivateData=&v3;
  args.DmaBufferPrivateDataSize=args.DmaBufferUmdPrivateDataSize=sizeof(v3);
  args.SubmissionFenceId=21;
  ++v3.Header.Lease.SceneGeneration;
  assert(AdmissionG4SubmitVirtualEnvelope(&adapter,&context,&args)!=STATUS_SUCCESS);
  --v3.Header.Lease.SceneGeneration;
  ADMISSION_RENDER_CONTEXT other=context;
  assert(AdmissionG4SubmitVirtualEnvelope(&adapter,&other,&args)!=STATUS_SUCCESS);
  ++scene.Geometry.WidthPx;
  assert(AdmissionG4SubmitVirtualEnvelope(&adapter,&context,&args)!=STATUS_SUCCESS);
  --scene.Geometry.WidthPx;
  bind_ok=0;
  assert(AdmissionG4SubmitVirtualEnvelope(&adapter,&context,&args)!=STATUS_SUCCESS);
  assert(!scene.Queued && !scene.Started);
  bind_ok=1;
  assert(AdmissionG4SubmitVirtualEnvelope(&adapter,&context,&args)==STATUS_SUCCESS);
  assert(scene.Queued && scene.Fence==21 && AdmissionGpuvaG3PrivateContextBusy(&context));
  ++adapter.BackendImage.G4Lease.SceneGeneration;
  assert(AdmissionGpuvaG3BeginJob(&adapter,&context,21)!=STATUS_SUCCESS);
  --adapter.BackendImage.G4Lease.SceneGeneration;
  ++process.Graph.MappingGeneration;
  assert(AdmissionGpuvaG3BeginJob(&adapter,&context,21)!=STATUS_SUCCESS);
  --process.Graph.MappingGeneration;
  assert(AdmissionGpuvaG3BeginJob(&adapter,&context,21)==STATUS_SUCCESS);
  assert(scene.Started);
  puts("g4_submit_virtual_replay: PASS");
  return 0;
}
