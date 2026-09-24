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
#define STATUS_DEVICE_HARDWARE_ERROR ((NTSTATUS)0xC0000185)
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
#define ADMISSION_MEMORY_LOCAL_SEGMENT 2u
#define ADMISSION_GPUVA_G1B_PAGE_PROFILE 64
#define ADMISSION_G3_PROCESS_MAGIC 0x47335052u
#define ADMISSION_OBJECT_CONTEXT_MAGIC 0x434F4E54u
#define ADMISSION_OBJECT_DEVICE_MAGIC 0x44455643u
#define ADMISSION_CONTEXT_SYSTEM 1u
#ifndef ADMISSION_CONTEXT_VALID_FLAGS
#define ADMISSION_CONTEXT_VALID_FLAGS 0x27u
#endif
#define ADMISSION_DMA_BUFFER_SIZE 0x10000u
#define ADMISSION_GDI_DMA_PRIVATE_SIZE 0x1000u
#define ADMISSION_GDI_ALLOCATION_LIST_SIZE 1u
#define ADMISSION_GDI_PATCH_LIST_SIZE 1u
#define ADMISSION_ALLOCATION_LIST_SIZE 1u
#define ADMISSION_PATCH_LIST_SIZE 1u
#define APPLE_AGX_TRUE 1
#define APPLE_AGX_FALSE 0
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define RtlCopyMemory(d,s,n) memcpy((d),(s),(n))
#define CONTAINING_RECORD(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))

typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
static void InitializeListHead(LIST_ENTRY *h) { h->Flink=h->Blink=h; }
static void InsertTailList(LIST_ENTRY *h, LIST_ENTRY *e) { e->Flink=h; e->Blink=h->Blink; h->Blink->Flink=e; h->Blink=e; }
static void RemoveEntryList(LIST_ENTRY *e) { e->Blink->Flink=e->Flink; e->Flink->Blink=e->Blink; }
typedef int FAST_MUTEX;
static void ExAcquireFastMutex(FAST_MUTEX *m) {(void)m;}
static void ExReleaseFastMutex(FAST_MUTEX *m) {(void)m;}
static void KeAcquireSpinLock(int *m,KIRQL *i) {(void)m;*i=0;}
static void KeReleaseSpinLock(int *m,KIRQL i) {(void)m;(void)i;}
static KIRQL KeGetCurrentIrql(void) { return 0; }
static void *ExAllocatePool2(int pool,SIZE_T bytes,ULONG tag) {(void)pool;(void)tag;return calloc(1,bytes);}
static void ExFreePoolWithTag(void *p,ULONG tag) {(void)tag;free(p);}
static LONG InterlockedCompareExchange(LONG *p,LONG n,LONG old) { LONG v=*p;if(v==old)*p=n;return v; }

typedef struct { long long QuadPart; } PHYSICAL_ADDRESS;
typedef struct { UINT SegmentId, Padding; UINT64 SegmentOffset; } D3DGPU_PHYSICAL_ADDRESS;
typedef union { D3DGPU_PHYSICAL_ADDRESS GpuPhysical; void *CpuVirtual; } DXGK_PAGETABLEUPDATEADDRESS;
typedef enum { DXGK_PAGETABLEUPDATE_GPU_PHYSICAL=0, DXGK_PAGETABLEUPDATE_CPU_VIRTUAL=1 } DXGK_PAGETABLEUPDATEMODE;
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
typedef struct { UINT DmaBufferSize,DmaBufferSegmentSet,DmaBufferPrivateDataSize,AllocationListSize,PatchLocationListSize; } DXGK_CONTEXTINFO;
typedef struct { DXGK_CREATECONTEXTFLAGS Flags; void *pPrivateDriverData; UINT PrivateDriverDataSize; HANDLE hContext; UINT NodeOrdinal,EngineAffinity; DXGK_CONTEXTINFO ContextInfo; } DXGKARG_CREATECONTEXT;

typedef unsigned int APPLE_AGX_U32,APPLE_AGX_BOOL;
typedef struct { int unused; } ADMISSION_WIN32_CONTEXT_CREATE;
typedef enum { AdmissionWin32TransportSuccess=0 } ADMISSION_WIN32_TRANSPORT_RESULT;
static ADMISSION_WIN32_TRANSPORT_RESULT AdmissionWin32ContextCreateValidate(const void *p,UINT n,APPLE_AGX_BOOL sys,APPLE_AGX_BOOL legacy,APPLE_AGX_U32 *gen,APPLE_AGX_BOOL *transport) {(void)p;(void)n;(void)sys;(void)legacy;*gen=0;*transport=0;return AdmissionWin32TransportSuccess;}
typedef struct { int unused; } APPLE_AGX_MEMORY_IO;
typedef struct { void *AllocationHandle,*CpuAddress,*AllocationCpuBase; ULONGLONG DeviceAddress; } APPLE_AGX_MEMORY_OBJECT;
typedef struct { ULONGLONG GuestIpaBase; } ADMISSION_PHYSICAL_ALLOCATION;
typedef enum { AppleAgxMemoryResultOk=0 } APPLE_AGX_MEMORY_RESULT;
typedef struct { void *Client; ULONGLONG RootIpa; UINT LastStatus,Uncertain,Created; } APPLE_AGX_GPUVA_G3_GRAPH;
typedef struct { ULONGLONG GuestIpaAddress,Bytes; void *CpuAddress; } ADMISSION_SCANOUT_MEMORY_VIEW;
typedef struct { UINT Version,Bytes,Branch,Index,Status,Level,UpdateMode,PageTablePageSize,GraphLastStatus,GraphUncertain; ULONGLONG ChildIpa,PteFlags,PageAddress,TableAddress,TableIpa; } ADMISSION_G3_PAGING_FAILURE;
typedef struct _ADMISSION_CONTEXT ADMISSION_CONTEXT;
typedef struct _ADMISSION_G3_PROCESS ADMISSION_G3_PROCESS;
typedef struct _ADMISSION_OBJECT_DEVICE { UINT Magic; void *Adapter; } ADMISSION_OBJECT_DEVICE;
typedef struct { UINT Magic; ADMISSION_OBJECT_DEVICE *Device; UINT FenceOutstanding; } ADMISSION_OBJECT_CONTEXT;
typedef struct _ADMISSION_DEVICE { ADMISSION_OBJECT_DEVICE Object; LONG Win32Generation; ADMISSION_G3_PROCESS *GpuvaG3Process; } ADMISSION_DEVICE;
typedef struct { int unused; } ADMISSION_SCHEDULER_CONTEXT;
typedef struct { int unused; } ADMISSION_PREPATCHED_RENDER;
typedef struct _ADMISSION_RENDER_CONTEXT { ADMISSION_OBJECT_CONTEXT Object; UINT Win32Generation; BOOLEAN Win32Transport,GpuvaG3Poisoned; ADMISSION_SCHEDULER_CONTEXT SchedulerContext; ADMISSION_PREPATCHED_RENDER PrepatchedRender; ADMISSION_G3_PROCESS *GpuvaG3Process; ULONGLONG GpuvaG3RootIpa; } ADMISSION_RENDER_CONTEXT;
typedef struct _ADMISSION_G3_STATE { ADMISSION_CONTEXT *Adapter; FAST_MUTEX Lock; LIST_ENTRY Processes; void *Client; ULONGLONG NextProcessId; ULONG ProcessCount; ADMISSION_G3_PROCESS *ActiveProcess; } ADMISSION_G3_STATE;
struct _ADMISSION_G3_PROCESS { LIST_ENTRY Link; ADMISSION_G3_STATE *State; APPLE_AGX_GPUVA_G3_GRAPH Graph; APPLE_AGX_MEMORY_IO Io; APPLE_AGX_MEMORY_OBJECT BootstrapRoot; ULONGLONG BootstrapIpa; ULONG Magic,DeviceRefs,ContextRefs; BOOLEAN Poisoned; };
struct _ADMISSION_CONTEXT { void *GpuvaG3State; BOOLEAN Started; PDEVICE_OBJECT PhysicalDeviceObject; ADMISSION_CONTEXT *ObjectAdapter; int SchedulerLock,Scheduler; };

static unsigned char *local_cpu;
static ULONGLONG local_ipa=0x10000000ULL;
static ULONGLONG local_bytes=0x4000000ULL;
static PHYSICAL_ADDRESS MmGetPhysicalAddress(void *p) { PHYSICAL_ADDRESS a={0};if(local_cpu && (unsigned char *)p>=local_cpu && (unsigned char *)p<local_cpu+local_bytes) a.QuadPart=(long long)(local_ipa+((unsigned char *)p-local_cpu));return a; }
static NTSTATUS AdmissionMemoryRuntimeScanoutView(ADMISSION_CONTEXT *a,ADMISSION_SCANOUT_MEMORY_VIEW *v) {(void)a;v->GuestIpaAddress=local_ipa;v->Bytes=local_bytes;v->CpuAddress=local_cpu;return STATUS_SUCCESS;}
static NTSTATUS AdmissionMemoryRuntimeBorrowIo(ADMISSION_CONTEXT *a,APPLE_AGX_MEMORY_IO *io) {(void)a;(void)io;return STATUS_SUCCESS;}
static APPLE_AGX_MEMORY_RESULT AppleAgxMemoryAllocateAligned(APPLE_AGX_MEMORY_IO *io,ULONGLONG n,ULONGLONG align,APPLE_AGX_MEMORY_OBJECT *o) {(void)io;(void)n;(void)align;static ADMISSION_PHYSICAL_ALLOCATION alloc;alloc.GuestIpaBase=local_ipa;o->AllocationHandle=&alloc;o->CpuAddress=o->AllocationCpuBase=local_cpu;o->DeviceAddress=local_ipa;return AppleAgxMemoryResultOk;}
static APPLE_AGX_MEMORY_RESULT AppleAgxMemoryRelease(APPLE_AGX_MEMORY_IO *io,APPLE_AGX_MEMORY_OBJECT *o) {(void)io;(void)o;return AppleAgxMemoryResultOk;}
static bool AppleAgxGpuvaG3GraphInit(APPLE_AGX_GPUVA_G3_GRAPH *g,void *client,ULONGLONG id,ULONGLONG gen,void *(*alloc)(void *,ULONGLONG),void (*freefn)(void *,void *),void *opaque) {(void)id;(void)gen;(void)alloc;(void)freefn;(void)opaque;g->Client=client;return true;}
static bool AppleAgxGpuvaG3GraphCreate(APPLE_AGX_GPUVA_G3_GRAPH *g,ULONGLONG root,bool paging) {(void)paging;g->RootIpa=root;g->Created=1;return true;}
static bool AppleAgxGpuvaG3GraphRegisterTable(APPLE_AGX_GPUVA_G3_GRAPH *g,ULONGLONG ipa,UINT level) {(void)g;(void)ipa;(void)level;return true;}
static bool AppleAgxGpuvaG3GraphBindRoot(APPLE_AGX_GPUVA_G3_GRAPH *g,ULONGLONG ipa) {g->RootIpa=ipa;return true;}
static bool AppleAgxGpuvaG3GraphUpdateParent(APPLE_AGX_GPUVA_G3_GRAPH *g,ULONGLONG table,UINT index,ULONGLONG child) {(void)g;(void)table;(void)index;(void)child;return true;}
static bool AppleAgxGpuvaG3GraphUpdateLeaf(APPLE_AGX_GPUVA_G3_GRAPH *g,ULONGLONG table,UINT index,ULONGLONG leaf,bool write) {(void)g;(void)table;(void)index;(void)leaf;(void)write;return true;}
static bool AppleAgxGpuvaG3GraphFlush(APPLE_AGX_GPUVA_G3_GRAPH *g,ULONGLONG start,ULONGLONG end) {(void)g;(void)start;(void)end;return true;}
static bool AppleAgxGpuvaG3GraphDestroy(APPLE_AGX_GPUVA_G3_GRAPH *g) {g->Created=0;return true;}
static void AdmissionRecordGpuvaG3CreateInput(PDEVICE_OBJECT p,DXGKARG_CREATEPROCESS *a,int started,KIRQL irql) {(void)p;(void)a;(void)started;(void)irql;}
static void AdmissionRecordGpuvaG3ContextInput(PDEVICE_OBJECT p,DXGKARG_CREATECONTEXT *a,KIRQL irql) {(void)p;(void)a;(void)irql;}
static void AdmissionRecordGpuvaG3PagingFailure(ADMISSION_CONTEXT *a,ADMISSION_G3_PAGING_FAILURE *f) {(void)a;(void)f;}
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
static NTSTATUS AdmissionGpuvaG3AttachContext(ADMISSION_RENDER_CONTEXT *,ADMISSION_DEVICE *);
static void AdmissionGpuvaG3DetachContext(ADMISSION_RENDER_CONTEXT *);
#endif
