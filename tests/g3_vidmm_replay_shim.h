#ifndef G3_VIDMM_REPLAY_SHIM_H
#define G3_VIDMM_REPLAY_SHIM_H
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "apple_agx_gpuva_g3_translation.h"
#include "apple_agx_gpuva_g3_graph.h"
#include "hv_agx_gpuva_v5.h"
#include "hv_agx_gpuva_v5_mmio.h"
#include "hv_agx_retained_backing.h"

#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define _Use_decl_annotations_
#define __try if (1)
#define __except(x) else
#define EXCEPTION_EXECUTE_HANDLER 1
typedef void VOID;
typedef void *PVOID;
typedef void *HANDLE;
typedef void *PDEVICE_OBJECT;
typedef unsigned char BOOLEAN, KIRQL, PUCHAR_BYTE;
typedef unsigned int UINT, ULONG;
typedef int LONG, NTSTATUS;
typedef unsigned long long ULONGLONG, UINT64;
typedef uintptr_t ULONG_PTR;
typedef size_t SIZE_T;
typedef unsigned char *PUCHAR;
typedef const void VOID_CONST;
#define TRUE 1
#define FALSE 0
#define PASSIVE_LEVEL 0
#define DISPATCH_LEVEL 2
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_INVALID_ADDRESS ((NTSTATUS)0xC0000141)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BB)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009A)
#define STATUS_DEVICE_HARDWARE_ERROR ((NTSTATUS)0xC0000483)
#define STATUS_INTEGER_OVERFLOW ((NTSTATUS)0xC0000095)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011)
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008)
#define STATUS_INVALID_USER_BUFFER ((NTSTATUS)0xC00000E8)
#define NT_SUCCESS(x) ((x) >= 0)
#define MAXSIZE_T SIZE_MAX
#define MAXULONGLONG UINT64_MAX
#define MAXULONG UINT32_MAX
#define MAXULONG_PTR UINTPTR_MAX
#define POOL_FLAG_NON_PAGED 0
#define ADMISSION_POOL_TAG 0x47335453u
#define ADMISSION_MEMORY_APERTURE_SEGMENT 1u
#define ADMISSION_MEMORY_LOCAL_SEGMENT 2u
#define ADMISSION_GPUVA_G1B_PAGE_PROFILE 64
#define ADMISSION_G3_PROCESS_MAGIC 0x47335052u
#define ADMISSION_OBJECT_CONTEXT_MAGIC 0x434F4E54u
#define ADMISSION_OBJECT_DEVICE_MAGIC 0x44455643u
#define ADMISSION_CONTEXT_SYSTEM 1u
#ifndef ADMISSION_CONTEXT_VALID_FLAGS
#define ADMISSION_CONTEXT_VALID_FLAGS 0x27u
#endif
#define ADMISSION_DMA_BUFFER_SIZE 0x50000u
#define ADMISSION_GDI_DMA_PRIVATE_SIZE 0x51000u
#define ADMISSION_GDI_ALLOCATION_LIST_SIZE 256u
#define ADMISSION_GDI_PATCH_LIST_SIZE 256u
#define ADMISSION_ALLOCATION_LIST_SIZE 64u
#define ADMISSION_PATCH_LIST_SIZE 64u
#define APPLE_AGX_TRUE 1
#define APPLE_AGX_FALSE 0
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define RtlCopyMemory(d,s,n) memcpy((d),(s),(n))
#define RtlCompareMemory(a,b,n) ((SIZE_T)(memcmp((a),(b),(n))==0 ? (n) : 0))
#define KeMemoryBarrier() __sync_synchronize()
#define CONTAINING_RECORD(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))

typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
static void InitializeListHead(LIST_ENTRY *h) { h->Flink=h->Blink=h; }
static void InsertTailList(LIST_ENTRY *h, LIST_ENTRY *e) { e->Flink=h; e->Blink=h->Blink; h->Blink->Flink=e; h->Blink=e; }
static void RemoveEntryList(LIST_ENTRY *e) { e->Blink->Flink=e->Flink; e->Flink->Blink=e->Blink; }
typedef int FAST_MUTEX;
static KIRQL replay_irql;
static void ExAcquireFastMutex(FAST_MUTEX *m) {(void)m;assert(replay_irql==PASSIVE_LEVEL);replay_irql=1;}
static void ExReleaseFastMutex(FAST_MUTEX *m) {(void)m;assert(replay_irql==1);replay_irql=PASSIVE_LEVEL;}
static void KeAcquireSpinLock(int *m,KIRQL *i) {(void)m;*i=0;}
static void KeReleaseSpinLock(int *m,KIRQL i) {(void)m;(void)i;}
static KIRQL KeGetCurrentIrql(void) { return replay_irql; }
static void *ExAllocatePool2(int pool,SIZE_T bytes,ULONG tag) {(void)pool;(void)tag;return calloc(1,bytes);}
static void ExFreePoolWithTag(void *p,ULONG tag) {(void)tag;free(p);}
static LONG InterlockedCompareExchange(LONG *p,LONG n,LONG old) { LONG v=*p;if(v==old)*p=n;return v; }

typedef struct { long long QuadPart; } PHYSICAL_ADDRESS;
typedef struct { UINT SegmentId, Padding; UINT64 SegmentOffset; } D3DGPU_PHYSICAL_ADDRESS;
typedef union { D3DGPU_PHYSICAL_ADDRESS GpuPhysical; void *CpuVirtual; } DXGK_PAGETABLEUPDATEADDRESS;
typedef enum { DXGK_PAGETABLEUPDATE_CPU_VIRTUAL=0, DXGK_PAGETABLEUPDATE_GPU_VIRTUAL=1, DXGK_PAGETABLEUPDATE_GPU_PHYSICAL=2 } DXGK_PAGETABLEUPDATEMODE;
typedef struct { union { struct { ULONGLONG Valid:1,Zero:1,CacheCoherent:1,ReadOnly:1,NoExecute:1,Segment:5,LargePage:1,PhysicalAdapterIndex:6,PageTablePageSize:2,SystemReserved0:1,Reserved:44; }; ULONGLONG Flags; }; union { ULONGLONG PageAddress,PageTableAddress; }; } DXGK_PTE;
typedef union { struct { UINT Repeat:1,InitialUpdate:1,NotifyEviction:1,Use64KBPages:1,NativeFence:1,Reserved:27; }; UINT Value; } DXGK_UPDATEPAGETABLEFLAGS;
typedef struct { HANDLE hProcess; DXGK_PAGETABLEUPDATEADDRESS PageTableAddress; DXGK_PAGETABLEUPDATEMODE UpdateMode; UINT PageTableLevel,StartIndex,NumPageTableEntries; DXGK_UPDATEPAGETABLEFLAGS Flags; DXGK_PTE *pPageTableEntries,*pPageTableEntries64KB; UINT Reserved0,DriverProtection; ULONGLONG FirstPteVirtualAddress; } DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE;
typedef struct { HANDLE hProcess; D3DGPU_PHYSICAL_ADDRESS RootPageTableAddress; ULONGLONG StartVirtualAddress,EndVirtualAddress; } DXGK_BUILDPAGINGBUFFER_FLUSHTLB;
enum { DXGK_OPERATION_UPDATE_PAGE_TABLE=11, DXGK_OPERATION_FLUSH_TLB=12 };
typedef struct { UINT Operation; void *pDmaBuffer,*pDmaBufferPrivateData; union { DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE UpdatePageTable; DXGK_BUILDPAGINGBUFFER_FLUSHTLB FlushTlb; }; } DXGKARG_BUILDPAGINGBUFFER;
typedef union { struct { UINT SystemProcess:1; UINT Reserved:31; }; UINT Value; } DXGK_CREATEPROCESSFLAGS;
typedef struct { DXGK_CREATEPROCESSFLAGS Flags; UINT NumPasid; void *pPasid,*pProcessName; HANDLE hKmdProcess; } DXGKARG_CREATEPROCESS;
typedef struct { HANDLE hContext; D3DGPU_PHYSICAL_ADDRESS Address; UINT NumEntries; } DXGKARG_SETROOTPAGETABLE;
typedef union { struct { UINT SystemContext:1,GdiContext:1,VirtualAddressing:1,SystemProtected:1,HwQueueSupported:1,TestContext:1; }; UINT Value; } DXGK_CREATECONTEXTFLAGS;
typedef struct { UINT Value; } DXGK_CONTEXTINFO_CAPS;
typedef struct { UINT DmaBufferSize,DmaBufferSegmentSet,DmaBufferPrivateDataSize,AllocationListSize,PatchLocationListSize,Reserved; DXGK_CONTEXTINFO_CAPS Caps; UINT PagingCompanionNodeId; } DXGK_CONTEXTINFO;
typedef struct { DXGK_CREATECONTEXTFLAGS Flags; void *pPrivateDriverData; UINT PrivateDriverDataSize; HANDLE hContext; UINT NodeOrdinal,EngineAffinity; DXGK_CONTEXTINFO ContextInfo; } DXGKARG_CREATECONTEXT;

typedef unsigned int APPLE_AGX_U32,APPLE_AGX_BOOL;
typedef struct { int unused; } ADMISSION_WIN32_CONTEXT_CREATE;
typedef enum { AdmissionWin32TransportSuccess=0 } ADMISSION_WIN32_TRANSPORT_RESULT;
static ADMISSION_WIN32_TRANSPORT_RESULT AdmissionWin32ContextCreateValidate(const void *p,UINT n,APPLE_AGX_BOOL sys,APPLE_AGX_BOOL legacy,APPLE_AGX_U32 *gen,APPLE_AGX_BOOL *transport) {(void)p;(void)n;(void)sys;(void)legacy;*gen=0;*transport=0;return AdmissionWin32TransportSuccess;}
typedef struct { int unused; } APPLE_AGX_MEMORY_IO;
typedef struct { void *AllocationHandle,*CpuAddress,*AllocationCpuBase; ULONGLONG DeviceAddress; } APPLE_AGX_MEMORY_OBJECT;
typedef struct { struct { UINT Contiguous; } Flags; } REPLAY_ADL;
typedef struct { ULONGLONG GuestIpaBase,Size; REPLAY_ADL *Adl; } ADMISSION_PHYSICAL_ALLOCATION;
typedef enum { AppleAgxMemoryResultOk=0 } APPLE_AGX_MEMORY_RESULT;
typedef struct { ULONGLONG GuestIpaAddress,Bytes; void *CpuAddress; } ADMISSION_SCANOUT_MEMORY_VIEW;
typedef struct { UINT Version,Bytes,Branch,Level,Index,PageTablePageSize,Status,UpdateMode,GraphLastStatus,GraphUncertain; ULONGLONG TableAddress,TableIpa,PteFlags,PageAddress,ChildIpa; UINT TableFirstNonzeroIndex,TableAddBranch; ULONGLONG TableFirstNonzeroWord,BrokerTableIpa; } ADMISSION_G3_PAGING_FAILURE;
typedef struct { UINT Version,Bytes,Branch,RootSegment,ResolveStatus,BrokerStatus; ULONGLONG Process,RootOffset,ResolvedRootIpa,GraphRootIpa,InputStart,InputEnd,FlushStart,FlushEnd; } ADMISSION_G3_FLUSH_RECEIPT;
typedef struct _ADMISSION_CONTEXT ADMISSION_CONTEXT;
typedef struct _ADMISSION_G3_PROCESS ADMISSION_G3_PROCESS;
typedef struct _ADMISSION_OBJECT_DEVICE { UINT Magic; void *Adapter; } ADMISSION_OBJECT_DEVICE;
typedef struct { UINT Magic; ADMISSION_OBJECT_DEVICE *Device; UINT FenceOutstanding; } ADMISSION_OBJECT_CONTEXT;
typedef struct _ADMISSION_DEVICE { ADMISSION_OBJECT_DEVICE Object; LONG Win32Generation; ADMISSION_G3_PROCESS *GpuvaG3Process; } ADMISSION_DEVICE;
typedef struct { int unused; } ADMISSION_SCHEDULER_CONTEXT;
typedef struct { int unused; } ADMISSION_PREPATCHED_RENDER;
typedef struct _ADMISSION_RENDER_CONTEXT { ADMISSION_OBJECT_CONTEXT Object; UINT Win32Generation; BOOLEAN Win32Transport,GpuvaG3Poisoned; ADMISSION_SCHEDULER_CONTEXT SchedulerContext; ADMISSION_PREPATCHED_RENDER PrepatchedRender; ADMISSION_G3_PROCESS *GpuvaG3Process; ULONGLONG GpuvaG3RootIpa; } ADMISSION_RENDER_CONTEXT;
typedef struct _ADMISSION_G3_STATE { ADMISSION_CONTEXT *Adapter; FAST_MUTEX Lock; LIST_ENTRY Processes; APPLE_AGX_GPUVA_V5_CLIENT Client; ULONGLONG NextProcessId; ULONG ProcessCount; ADMISSION_G3_PROCESS *ActiveProcess; ULONGLONG UnpublishedGroups[32]; } ADMISSION_G3_STATE;
typedef struct _ADMISSION_G3_TABLE_SHADOW { struct _ADMISSION_G3_TABLE_SHADOW *Next; APPLE_AGX_MEMORY_OBJECT Memory; ULONGLONG OriginalIpa,BrokerIpa; APPLE_AGX_GPUVA_G3_LOGICAL_PTE *LogicalPtes; } ADMISSION_G3_TABLE_SHADOW;
struct _ADMISSION_G3_PROCESS { LIST_ENTRY Link; ADMISSION_G3_STATE *State; APPLE_AGX_GPUVA_G3_GRAPH Graph; APPLE_AGX_MEMORY_IO Io; APPLE_AGX_MEMORY_OBJECT BootstrapRoot; ADMISSION_G3_TABLE_SHADOW *TableShadows; ULONGLONG BootstrapIpa; ULONG Magic,DeviceRefs,ContextRefs; BOOLEAN Poisoned; };
struct _ADMISSION_CONTEXT { void *GpuvaG3State; BOOLEAN Started; PDEVICE_OBJECT PhysicalDeviceObject; ADMISSION_CONTEXT *ObjectAdapter; int SchedulerLock,Scheduler; };

static unsigned char *local_cpu;
static ULONGLONG local_ipa=0x10000000ULL;
static ULONGLONG local_bytes=0x4000000ULL;
static PHYSICAL_ADDRESS MmGetPhysicalAddress(void *p) { PHYSICAL_ADDRESS a={0};if(local_cpu && (unsigned char *)p>=local_cpu && (unsigned char *)p<local_cpu+local_bytes) a.QuadPart=(long long)(local_ipa+((unsigned char *)p-local_cpu));return a; }
static NTSTATUS AdmissionMemoryRuntimeScanoutView(ADMISSION_CONTEXT *a,ADMISSION_SCANOUT_MEMORY_VIEW *v) {(void)a;v->GuestIpaAddress=local_ipa;v->Bytes=0x3800000ULL;v->CpuAddress=local_cpu;return STATUS_SUCCESS;}
static NTSTATUS AdmissionMemoryRuntimeBorrowIo(ADMISSION_CONTEXT *a,APPLE_AGX_MEMORY_IO *io) {(void)a;(void)io;return STATUS_SUCCESS;}
static APPLE_AGX_MEMORY_RESULT AppleAgxMemoryAllocateAligned(APPLE_AGX_MEMORY_IO *io,ULONGLONG n,ULONGLONG align,APPLE_AGX_MEMORY_OBJECT *o) {
  (void)io;assert(n==0x4000 && align==0x4000);
  static ADMISSION_PHYSICAL_ALLOCATION alloc[512];static REPLAY_ADL adl[512];static UINT count;
  assert(count<512);
  ULONGLONG offset=0x3800000ULL+(ULONGLONG)count*0x4000ULL;
  adl[count].Flags.Contiguous=1;
  alloc[count].GuestIpaBase=local_ipa+offset;alloc[count].Size=0x4000;alloc[count].Adl=&adl[count];
  o->AllocationHandle=&alloc[count];o->CpuAddress=o->AllocationCpuBase=local_cpu+offset;
  o->DeviceAddress=local_ipa+offset;++count;return AppleAgxMemoryResultOk;
}
static APPLE_AGX_MEMORY_RESULT AppleAgxMemoryRelease(APPLE_AGX_MEMORY_IO *io,APPLE_AGX_MEMORY_OBJECT *o) {(void)io;o->AllocationHandle=NULL;return AppleAgxMemoryResultOk;}
typedef struct {
  struct hv_agx_gpuva_v5_wire wire;
  struct hv_contract_snapshot memory;
  uint64_t slots[HV_AGX_GPUVA_V5_SLOTS][2];
  uint64_t blocked_ipa;
  UINT commands;
  uint64_t last_flush_start, last_flush_end;
  UINT flush_commands;
} REPLAY_BROKER;
static struct hv_agx_gpuva_v5 gpuva_v5;
static bool request_powered = true;
static void gpuva_execute(void *, const AGX_GPUVA_V5_REQUEST *, AGX_GPUVA_V5_RESPONSE *);
static uint64_t ReplayTranslate(void *opaque,uint64_t ipa) {
  REPLAY_BROKER *b=opaque;
  if (ipa==b->blocked_ipa || ipa<local_ipa || ipa-local_ipa>local_bytes-0x4000 ||
      !hv_agx_retained_backing_allowed(&b->memory,ipa,0x4000,0,0)) return 0;
  return ipa;
}
static uint64_t *ReplayMapPage(void *opaque,uint64_t ipa,uint64_t pa) {
  (void)opaque;
  return ipa==pa && pa>=local_ipa && pa-local_ipa<=local_bytes-0x4000 ?
      (uint64_t *)(local_cpu+pa-local_ipa) : NULL;
}
static bool ReplayReadSlot(void *opaque,unsigned slot,uint64_t *low,uint64_t *high) {
  REPLAY_BROKER *b=opaque;
  if (slot>=HV_AGX_GPUVA_V5_SLOTS) return false;
  *low=b->slots[slot][0]; *high=b->slots[slot][1]; return true;
}
static bool ReplayWriteSlot(void *opaque,unsigned slot,uint64_t low,uint64_t high) {
  REPLAY_BROKER *b=opaque;
  if (!slot || slot>=HV_AGX_GPUVA_V5_SLOTS || high) return false;
  b->slots[slot][0]=low; b->slots[slot][1]=high; return true;
}
static bool ReplaySync(void *opaque) {(void)opaque;return true;}
static bool ReplayInvalidate(void *opaque,unsigned slot) {(void)opaque;return slot>0 && slot<HV_AGX_GPUVA_V5_SLOTS;}
static bool ReplayPrefix(void *opaque) {(void)opaque;return true;}
static bool ReplayLegacySlot63(void *opaque) {(void)opaque;return false;}
static void ReplayBrokerInit(REPLAY_BROKER *b) {
  struct hv_agx_gpuva_v5_ops ops={b,ReplayTranslate,ReplayMapPage,ReplayReadSlot,
      ReplayWriteSlot,ReplaySync,ReplayInvalidate,ReplayPrefix,ReplayLegacySlot63};
  b->memory.boot.ram_base=local_ipa;
  b->memory.boot.ram_size=local_bytes;
  b->memory.region_count=1;
  b->memory.regions[0].kind=HV_CONTRACT_REGION_GUEST_RAM;
  b->memory.regions[0].base=local_ipa;
  b->memory.regions[0].size=local_bytes;
  b->slots[0][0]=0x91000001;
  b->slots[0][1]=0x90000001;
  assert(hv_agx_gpuva_v5_init(&gpuva_v5,7,&ops)==HV_AGX_GPUVA_V5_OK);
}
static bool ReplayWrite64(void *opaque,unsigned offset,unsigned long long value) {
  REPLAY_BROKER *b=opaque; uint64_t word=value;
  if (offset<AGX_GPUVA_V5_OFFSET) return false;
  return hv_agx_gpuva_v5_mmio(&b->wire,offset-AGX_GPUVA_V5_OFFSET,&word,true,3,
                              gpuva_execute,NULL);
}
static bool ReplayWrite32(void *opaque,unsigned offset,unsigned value) {
  REPLAY_BROKER *b=opaque; uint64_t word=value;
  if (offset<AGX_GPUVA_V5_OFFSET) return false;
  bool ok=hv_agx_gpuva_v5_mmio(&b->wire,offset-AGX_GPUVA_V5_OFFSET,&word,true,2,
                               gpuva_execute,NULL);
  if (ok && offset==AGX_GPUVA_V5_OFFSET+AGX_GPUVA_V5_DOORBELL) ++b->commands;
  if (ok && offset==AGX_GPUVA_V5_OFFSET+AGX_GPUVA_V5_DOORBELL &&
      b->wire.request.Command==AGX_GPUVA_V5_FLUSH_TLB) {
    b->last_flush_start=b->wire.request.LogicalIpa[0];
    b->last_flush_end=b->wire.request.LogicalIpa[1];
    ++b->flush_commands;
  }
  return ok;
}
static bool ReplayRead64(void *opaque,unsigned offset,unsigned long long *value) {
  REPLAY_BROKER *b=opaque; uint64_t word=0;
  if (offset<AGX_GPUVA_V5_OFFSET) return false;
  bool ok=hv_agx_gpuva_v5_mmio(&b->wire,offset-AGX_GPUVA_V5_OFFSET,&word,false,3,
                               gpuva_execute,NULL);
  if (ok) *value=word;
  return ok;
}
static void ReplayBarrier(void *opaque) {(void)opaque;}
static void AdmissionRecordGpuvaG3CreateInput(PDEVICE_OBJECT p,DXGKARG_CREATEPROCESS *a,int started,KIRQL irql) {(void)p;(void)a;(void)started;(void)irql;}
static void AdmissionRecordGpuvaG3ContextInput(PDEVICE_OBJECT p,DXGKARG_CREATECONTEXT *a,KIRQL irql) {(void)p;(void)a;(void)irql;}
static void AdmissionRecordGpuvaG3DmaContext(PDEVICE_OBJECT p,DXGKARG_CREATECONTEXT *a) {(void)p;(void)a;}
static ADMISSION_G3_PAGING_FAILURE last_paging_failure;
static void AdmissionRecordGpuvaG3PagingFailure(ADMISSION_CONTEXT *a,ADMISSION_G3_PAGING_FAILURE *f) {(void)a;if(f->Branch)last_paging_failure=*f;}
static ADMISSION_G3_FLUSH_RECEIPT last_flush_receipt;
static void AdmissionRecordGpuvaG3Flush(ADMISSION_CONTEXT *a,const ADMISSION_G3_FLUSH_RECEIPT *r) {(void)a;if(KeGetCurrentIrql()==PASSIVE_LEVEL)last_flush_receipt=*r;}
static void AdmissionRecordGpuvaG3UnpublishedGroups(ADMISSION_CONTEXT *a,const ULONGLONG *counts) {(void)a;(void)counts;}
static void AppleAgxSchedulerContextInitialize(ADMISSION_SCHEDULER_CONTEXT *c) {(void)c;}
static void AdmissionPrepatchedInitialize(ADMISSION_PREPATCHED_RENDER *p) {(void)p;}
static bool AdmissionPrepatchedActive(ADMISSION_PREPATCHED_RENDER *p) {(void)p;return false;}
static bool AdmissionObjectsCreateContext(ADMISSION_OBJECT_DEVICE *device,HANDLE runtime,UINT node,UINT affinity,UINT flags,ADMISSION_OBJECT_CONTEXT *context) {(void)runtime;(void)flags;if(node!=0||affinity!=1)return false;context->Magic=ADMISSION_OBJECT_CONTEXT_MAGIC;context->Device=device;return true;}
static bool AdmissionObjectsDestroyContext(ADMISSION_OBJECT_CONTEXT *c) {(void)c;return true;}
static bool AppleAgxSchedulerCreateContext(int *s,ADMISSION_SCHEDULER_CONTEXT *c,UINT node,UINT affinity) {(void)s;(void)c;return node==0&&affinity==1;}
static bool AppleAgxSchedulerDestroyContext(int *s,ADMISSION_SCHEDULER_CONTEXT *c) {(void)s;(void)c;return true;}

/* Prototypes for cross-file calls inside extracted production functions. */
static ADMISSION_G3_PROCESS *AdmissionGpuvaG3FindProcess(ADMISSION_G3_STATE *,HANDLE);
static NTSTATUS AdmissionGpuvaG3ResolveTable(ADMISSION_CONTEXT *,const DXGK_PAGETABLEUPDATEADDRESS *,DXGK_PAGETABLEUPDATEMODE,ULONGLONG *);
static NTSTATUS AdmissionGpuvaG3BrokerTable(ADMISSION_G3_PROCESS *,ULONGLONG,BOOLEAN,ULONGLONG *);
static NTSTATUS AdmissionGpuvaG3MirrorTable(ADMISSION_G3_PROCESS *,ULONGLONG,PVOID);
static NTSTATUS AdmissionGpuvaG3AttachContext(ADMISSION_RENDER_CONTEXT *,ADMISSION_DEVICE *);
static void AdmissionGpuvaG3DetachContext(ADMISSION_RENDER_CONTEXT *);
#endif
