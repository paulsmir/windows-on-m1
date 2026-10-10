#ifndef G3_VIDMM_REPLAY_SHIM_H
#define G3_VIDMM_REPLAY_SHIM_H
/* Shared-memory retirement boundary is exercised by R153 callback replay. */
#define AdmissionSaveG4Manager(r,f) ((void)(r),(void)(f),1)
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#define FIELD_OFFSET(t, m) offsetof(t, m)
#include "apple_agx_gpuva_g3_translation.h"
#include "apple_agx_gpuva_g3_graph.h"
#include "apple_agx_g4_submit.h"
#include "apple_agx_state.h"
#include "apple_agx_vsync.h"
#include "apple_agx_g3_private_abi.h"
#include "apple_agx_g3_private_failure.h"
#include "apple_agx_g3_private_storage.h"
#include "apple_agx_render_manager.h"
#include "hv_agx_gpuva_v5.h"
#include "hv_agx_gpuva_v5_mmio.h"
#include "hv_agx_retained_backing.h"
#include "apple_agx_g3_copy_abi.h"
#include "apple_agx_win32_device_info.h"
#include "../drivers/apple-agx/render-admission/include/render_allocation.h"
#define ADMISSION_WIN32_ALLOCATION_GPU_LOCAL 0x100u

#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define _Use_decl_annotations_
#define __try if (1)
#define __except(x) else
#define EXCEPTION_EXECUTE_HANDLER 1
typedef void VOID;
typedef void *PVOID;
typedef void *HANDLE;
typedef struct { HANDLE hDevice,hContext,hKmdProcessHandle; struct { unsigned Value; } Flags; void *pPrivateDriverData; unsigned PrivateDriverDataSize; } DXGKARG_ESCAPE;
typedef void *PDEVICE_OBJECT;
typedef unsigned char BOOLEAN, KIRQL, PUCHAR_BYTE, UCHAR;
typedef unsigned int UINT, ULONG;
typedef unsigned short USHORT;
typedef int LONG, NTSTATUS;
typedef unsigned long long ULONGLONG, UINT64;
typedef long long LONGLONG;
typedef uintptr_t ULONG_PTR;
typedef size_t SIZE_T;
#include "../drivers/apple-agx/render-admission/include/render_qualification.h"
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
#define STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER ((NTSTATUS)0xC01E0001)
#define STATUS_GRAPHICS_ALLOCATION_BUSY ((NTSTATUS)0xC01E0102)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009A)
#define STATUS_DEVICE_HARDWARE_ERROR ((NTSTATUS)0xC0000483)
#define STATUS_INTEGER_OVERFLOW ((NTSTATUS)0xC0000095)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011)
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008)
#define STATUS_INVALID_USER_BUFFER ((NTSTATUS)0xC00000E8)
#define NT_SUCCESS(x) ((x) >= 0)
#define MAXSIZE_T SIZE_MAX
#define MAXULONGLONG UINT64_MAX
#define MAXLONGLONG INT64_MAX
#define MAXULONG UINT32_MAX
#define MAXULONG_PTR UINTPTR_MAX
#define POOL_FLAG_NON_PAGED 0
#define ADMISSION_POOL_TAG 0x47335453u
#define ADMISSION_MEMORY_APERTURE_SEGMENT 1u
#define ADMISSION_MEMORY_LOCAL_SEGMENT 2u
#ifndef ADMISSION_GPUVA_G1B_PAGE_PROFILE
#define ADMISSION_GPUVA_G1B_PAGE_PROFILE 64
#endif
#define ADMISSION_G3_PROCESS_MAGIC 0x47335052u
#define ADMISSION_OBJECT_CONTEXT_MAGIC 0x434F4E54u
#define ADMISSION_OBJECT_DEVICE_MAGIC 0x44455643u
#define ADMISSION_CONTEXT_SYSTEM 1u
#define ADMISSION_CPU_PACKET_PAGING 1u
#ifndef ADMISSION_CONTEXT_VALID_FLAGS
#define ADMISSION_CONTEXT_VALID_FLAGS 0x27u
#endif
#define ADMISSION_DMA_BUFFER_SIZE 0x50000u
#define ADMISSION_MAX_PAGING_RECORDS 64u
#define ADMISSION_GDI_DMA_PRIVATE_SIZE 0x51000u
#define ADMISSION_GDI_ALLOCATION_LIST_SIZE 256u
#define ADMISSION_GDI_PATCH_LIST_SIZE 256u
#define ADMISSION_ALLOCATION_LIST_SIZE 64u
#define ADMISSION_PATCH_LIST_SIZE 64u
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define RtlCopyMemory(d,s,n) memcpy((d),(s),(n))
#define RtlMoveMemory(d,s,n) memmove((d),(s),(n))
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
/* R155: bounded paging wait for an in-flight job. The hook simulates the
 * concurrent joined completion; sleeping with the G3 lock held is a bug. */
typedef union { struct { unsigned int LowPart; int HighPart; }; long long QuadPart; } LARGE_INTEGER;
typedef enum { KernelMode, UserMode } KPROCESSOR_MODE;
static LARGE_INTEGER KeQueryPerformanceCounter(LARGE_INTEGER *f) { static long long c; LARGE_INTEGER r; if(f) f->QuadPart=24000000; r.QuadPart=++c; return r; }
static unsigned replay_delay_calls; static void (*replay_delay_hook)(void);
/* Interrupt time advances by every relative wait, so wall-clock bounds hold. */
static unsigned long long replay_interrupt_time;
static unsigned long long KeQueryInterruptTime(void) { return replay_interrupt_time; }
static NTSTATUS KeDelayExecutionThread(KPROCESSOR_MODE m, BOOLEAN a, LARGE_INTEGER *i) {
  (void)m;(void)a;assert(i && i->QuadPart<0);assert(replay_irql==PASSIVE_LEVEL);
  replay_interrupt_time+=(unsigned long long)(-i->QuadPart);
  ++replay_delay_calls; if(replay_delay_hook) replay_delay_hook(); return 0; }
/* EXP1138: the G3 job event; a timed wait behaves like the old delay. */
typedef struct { int Signaled; } KEVENT;
typedef enum { NotificationEvent, SynchronizationEvent } EVENT_TYPE;
enum { Executive };
#define IO_NO_INCREMENT 0
static void KeInitializeEvent(KEVENT *e, EVENT_TYPE t, BOOLEAN s) { (void)t; e->Signaled=s; }
static void KeClearEvent(KEVENT *e) { e->Signaled=0; }
static LONG KeSetEvent(KEVENT *e, LONG inc, BOOLEAN w) { (void)inc;(void)w; e->Signaled=1; return 0; }
static NTSTATUS KeWaitForSingleObject(KEVENT *e, int r, KPROCESSOR_MODE m, BOOLEAN a, LARGE_INTEGER *t) {
  (void)e;(void)r; return KeDelayExecutionThread(m,a,t); }
/* R161: paging-worker quiescence and encoded-record accounting. */
static int replay_paging_pending; static UINT replay_encoded_records;
static int replay_quiescence_flip_after_success;
static UINT replay_quiescence_calls,replay_quiescence_flip_on_call;
static BOOLEAN replay_pool_fail;
static UINT replay_pool_calls;
static volatile LONG *replay_query_claim_watch;
static KIRQL replay_query_claim_irql;
static void *ExAllocatePool2(int pool,SIZE_T bytes,ULONG tag) {(void)pool;(void)tag;++replay_pool_calls;return replay_pool_fail?NULL:calloc(1,bytes);}
static void ExFreePoolWithTag(void *p,ULONG tag) {(void)tag;free(p);}
static LONG InterlockedExchange(volatile LONG *p,LONG n) {LONG old=*p;*p=n;return old;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG n,LONG old) { LONG v=*p;if(v==old){if(p==replay_query_claim_watch && n==1)replay_query_claim_irql=replay_irql;*p=n;}return v; }

typedef struct { long long QuadPart; } PHYSICAL_ADDRESS;
typedef struct { UINT SegmentId, Padding; UINT64 SegmentOffset; } D3DGPU_PHYSICAL_ADDRESS;
typedef union { D3DGPU_PHYSICAL_ADDRESS GpuPhysical; void *CpuVirtual; } DXGK_PAGETABLEUPDATEADDRESS;
typedef enum { DXGK_PAGETABLEUPDATE_CPU_VIRTUAL=0, DXGK_PAGETABLEUPDATE_GPU_VIRTUAL=1, DXGK_PAGETABLEUPDATE_GPU_PHYSICAL=2 } DXGK_PAGETABLEUPDATEMODE;
typedef struct { union { struct { ULONGLONG Valid:1,Zero:1,CacheCoherent:1,ReadOnly:1,NoExecute:1,Segment:5,LargePage:1,PhysicalAdapterIndex:6,PageTablePageSize:2,SystemReserved0:1,Reserved:44; }; ULONGLONG Flags; }; union { ULONGLONG PageAddress,PageTableAddress; }; } DXGK_PTE;
typedef union { struct { UINT Repeat:1,InitialUpdate:1,NotifyEviction:1,Use64KBPages:1,NativeFence:1,Reserved:27; }; UINT Value; } DXGK_UPDATEPAGETABLEFLAGS;
typedef struct { HANDLE hProcess,hAllocation; ULONGLONG AllocationOffsetInBytes; DXGK_PAGETABLEUPDATEADDRESS PageTableAddress; DXGK_PAGETABLEUPDATEMODE UpdateMode; UINT PageTableLevel,StartIndex,NumPageTableEntries; DXGK_UPDATEPAGETABLEFLAGS Flags; DXGK_PTE *pPageTableEntries,*pPageTableEntries64KB; UINT Reserved0,DriverProtection; ULONGLONG FirstPteVirtualAddress; } DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE;
typedef struct { HANDLE hProcess; D3DGPU_PHYSICAL_ADDRESS RootPageTableAddress; ULONGLONG StartVirtualAddress,EndVirtualAddress; } DXGK_BUILDPAGINGBUFFER_FLUSHTLB;
enum { DXGK_OPERATION_VIRTUAL_TRANSFER=8, DXGK_OPERATION_VIRTUAL_FILL=9,
       DXGK_OPERATION_UPDATE_PAGE_TABLE=11, DXGK_OPERATION_FLUSH_TLB=12,
       DXGK_OPERATION_SIGNAL_MONITORED_FENCE=16 };
enum { DXGK_MEMORY_TRANSFER_LOCAL_TO_SYSTEM=0,
       DXGK_MEMORY_TRANSFER_SYSTEM_TO_LOCAL=1,
       DXGK_MEMORY_TRANSFER_LOCAL_TO_LOCAL=2 };
typedef struct { HANDLE hAllocation; ULONGLONG AllocationOffsetInBytes,
  FillSizeInBytes; UINT FillPattern; ULONGLONG DestinationVirtualAddress;
} DXGK_BUILDPAGINGBUFFER_FILLVIRTUAL;
typedef struct { HANDLE hAllocation; ULONGLONG AllocationOffsetInBytes,
  TransferSizeInBytes, SourceVirtualAddress, DestinationVirtualAddress,
  SourcePageTable; UINT TransferDirection; union { struct {
  UINT Src64KBPages:1,Dst64KBPages:1,Reserved:30; }; UINT Flags; } Flags;
  ULONGLONG DestinationPageTable;
} DXGK_BUILDPAGINGBUFFER_TRANSFERVIRTUAL;
typedef struct { ULONGLONG MonitoredFenceGpuVa,MonitoredFenceValue; }
  DXGK_BUILDPAGINGBUFFER_SIGNALMONITOREDFENCE;
typedef struct { UINT Operation, DmaSize, DmaBufferPrivateDataSize,
  MultipassOffset,DmaBufferWriteOffset; void *pDmaBuffer,*pDmaBufferPrivateData;
  HANDLE hSystemContext;
  union { DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE UpdatePageTable;
    DXGK_BUILDPAGINGBUFFER_FLUSHTLB FlushTlb;
    DXGK_BUILDPAGINGBUFFER_FILLVIRTUAL FillVirtual;
    DXGK_BUILDPAGINGBUFFER_TRANSFERVIRTUAL TransferVirtual;
    DXGK_BUILDPAGINGBUFFER_SIGNALMONITOREDFENCE SignalMonitoredFence; };
} DXGKARG_BUILDPAGINGBUFFER;
enum { AdmissionPagingPhysical=0, AdmissionPagingVirtualFill=1,
       AdmissionPagingVirtualTransfer=2, AdmissionPagingMonitoredFence=3 };
typedef struct { UINT Magic,Version,RecordBytes,Reserved; } ADMISSION_PAGING_MARKER;
enum { AppleAgxPhysicalPagingUpload=1, AppleAgxPhysicalPagingDownload=2,
       AppleAgxPhysicalPagingLocalCopy=3, AppleAgxPhysicalPagingFill=4,
       AppleAgxPhysicalPagingDiscard=5 };
typedef struct { UINT Kind; ULONGLONG LocalOffset,SecondLocalOffset,
  SystemOffset,Bytes; UINT FillPattern; } APPLE_AGX_PHYSICAL_PAGING_PLAN;
typedef struct { ADMISSION_PAGING_MARKER Header; APPLE_AGX_PHYSICAL_PAGING_PLAN Plan;
  void *SystemMdl; UINT Kind,SourceSegment,DestinationSegment,PatternOffset;
  ULONGLONG SourceIpa,DestinationIpa; UINT Bytes,FillPattern;
  ULONGLONG FenceValue;
} ADMISSION_PAGING_RECORD;
#define ADMISSION_PAGING_MAGIC 0x504d4152u
#define ADMISSION_PAGING_VERSION 1u
#define ADMISSION_PAGING_LOCAL_SEGMENT 2u
#define ADMISSION_PAGING_LOCAL_RUN_MAX 0x400000u
typedef union { struct { UINT Paging:1,Present:1,RedirectedPresent:1,
  NullRendering:1,Flip:1,FlipWithNoWait:1,ContextSwitch:1,Resubmission:1,
  VirtualMachineData:1,Reserved:23; }; UINT Value; }
  DXGK_SUBMITCOMMANDFLAGS;
typedef struct { HANDLE hContext; UINT SubmissionFenceId,NodeOrdinal,EngineOrdinal;
  DXGK_SUBMITCOMMANDFLAGS Flags; } DXGKARG_SUBMITCOMMAND;
typedef struct { HANDLE hContext; ULONGLONG DmaBufferVirtualAddress;
  UINT DmaBufferSize; void *pDmaBufferPrivateData;
  UINT DmaBufferPrivateDataSize,DmaBufferUmdPrivateDataSize,SubmissionFenceId;
  DXGK_SUBMITCOMMANDFLAGS Flags; UINT NodeOrdinal,EngineOrdinal;
} DXGKARG_SUBMITCOMMANDVIRTUAL;
typedef union { struct { UINT SystemProcess:1; UINT Reserved:31; }; UINT Value; } DXGK_CREATEPROCESSFLAGS;
typedef struct { DXGK_CREATEPROCESSFLAGS Flags; UINT NumPasid; void *pPasid,*pProcessName; HANDLE hKmdProcess,hDxgkProcess; } DXGKARG_CREATEPROCESS;
typedef struct { HANDLE hDxgkProcess; ULONGLONG SizeInBytes; UINT Alignment;
  ULONGLONG StartVirtualAddress,BaseAddress;
  union { struct { UINT AllowUserModeMapping:1; }; UINT Flags; };
} DXGKARGCB_RESERVEGPUVIRTUALADDRESSRANGE;
static UINT reserve_calls;
static NTSTATUS reserve_status;
static HANDLE reserve_process;
static ULONGLONG reserve_base=1ULL<<36;
static NTSTATUS ReplayReserveVa(HANDLE adapter, DXGKARGCB_RESERVEGPUVIRTUALADDRESSRANGE *args) {
  assert(KeGetCurrentIrql()==PASSIVE_LEVEL && adapter==(HANDLE)0x1234);
  assert(args->SizeInBytes==0x02000000ULL && args->Alignment==0x02000000u);
  assert(!args->Flags && !args->BaseAddress);
  reserve_process=args->hDxgkProcess; ++reserve_calls;
  args->StartVirtualAddress=reserve_base;
  return reserve_status;
}
typedef struct { ULONG InterruptType; struct { ULONG SubmissionFenceId,NodeOrdinal,EngineOrdinal; } DmaCompleted; } DXGKARGCB_NOTIFY_INTERRUPT_DATA;
typedef struct { UINT hObject,Type; struct { UINT DeviceSpecific; } Flags; } DXGKARGCB_GETHANDLEDATA;
typedef struct { HANDLE ReleaseHandle; UINT Type; } DXGKARGCB_RELEASEHANDLEDATA;
#define DXGK_HANDLE_ALLOCATION 1u
typedef struct { HANDLE DeviceHandle;
 NTSTATUS (*DxgkCbReserveGpuVirtualAddressRange)(HANDLE, DXGKARGCB_RESERVEGPUVIRTUALADDRESSRANGE *);
 NTSTATUS (*DxgkCbSynchronizeExecution)(HANDLE,BOOLEAN (*)(PVOID),PVOID,ULONG,BOOLEAN *);
 VOID (*DxgkCbNotifyInterrupt)(HANDLE,const DXGKARGCB_NOTIFY_INTERRUPT_DATA *);
 BOOLEAN (*DxgkCbQueueDpc)(HANDLE);
 PVOID (*DxgkCbAcquireHandleData)(const DXGKARGCB_GETHANDLEDATA *,HANDLE *);
 VOID (*DxgkCbReleaseHandleData)(DXGKARGCB_RELEASEHANDLEDATA);
} DXGKRNL_INTERFACE;
typedef struct { HANDLE hContext; D3DGPU_PHYSICAL_ADDRESS Address; UINT NumEntries; } DXGKARG_SETROOTPAGETABLE;
typedef union { struct { UINT SystemContext:1,GdiContext:1,VirtualAddressing:1,SystemProtected:1,HwQueueSupported:1,TestContext:1; }; UINT Value; } DXGK_CREATECONTEXTFLAGS;
typedef union { struct { UINT NoPatchingRequired:1,DriverManagesResidency:1,
  UseIoMmu:1,Reserved:29; }; UINT Value; } DXGK_CONTEXTINFO_CAPS;
typedef struct { UINT DmaBufferSize,DmaBufferSegmentSet,DmaBufferPrivateDataSize,AllocationListSize,PatchLocationListSize,Reserved; DXGK_CONTEXTINFO_CAPS Caps; UINT PagingCompanionNodeId; } DXGK_CONTEXTINFO;
typedef struct { DXGK_CREATECONTEXTFLAGS Flags; void *pPrivateDriverData; UINT PrivateDriverDataSize; HANDLE hContext; UINT NodeOrdinal,EngineAffinity; DXGK_CONTEXTINFO ContextInfo; } DXGKARG_CREATECONTEXT;


typedef struct { int unused; } ADMISSION_WIN32_CONTEXT_CREATE;
typedef enum { AdmissionWin32TransportSuccess=0 } ADMISSION_WIN32_TRANSPORT_RESULT;
static ADMISSION_WIN32_TRANSPORT_RESULT AdmissionWin32ContextCreateValidate(const void *p,UINT n,APPLE_AGX_BOOL sys,APPLE_AGX_BOOL legacy,APPLE_AGX_U32 *gen,APPLE_AGX_BOOL *transport) {(void)p;(void)n;(void)sys;(void)legacy;*gen=0;*transport=0;return AdmissionWin32TransportSuccess;}
typedef struct { int unused; } APPLE_AGX_MEMORY_IO;
typedef struct { void *AllocationHandle,*CpuAddress,*AllocationCpuBase; ULONGLONG DeviceAddress; } APPLE_AGX_MEMORY_OBJECT;
typedef struct { struct { UINT Contiguous; } Flags; } REPLAY_ADL;
typedef struct { ULONGLONG GuestIpaBase,Size; REPLAY_ADL *Adl; } ADMISSION_PHYSICAL_ALLOCATION;
typedef enum { AppleAgxMemoryResultOk=0 } APPLE_AGX_MEMORY_RESULT;
typedef struct { ULONGLONG GuestIpaAddress,Bytes,PoolBytes; void *CpuAddress; } ADMISSION_SCANOUT_MEMORY_VIEW;
typedef struct { UINT Version,Bytes,Branch,Level,Index,PageTablePageSize,Status,UpdateMode,GraphLastStatus,GraphUncertain; ULONGLONG TableAddress,TableIpa,PteFlags,PageAddress,ChildIpa; UINT TableFirstNonzeroIndex,TableAddBranch; ULONGLONG TableFirstNonzeroWord,BrokerTableIpa; } ADMISSION_G3_PAGING_FAILURE;
typedef struct { UINT Version,Bytes,Branch,RootSegment,ResolveStatus,BrokerStatus; ULONGLONG Process,RootOffset,ResolvedRootIpa,GraphRootIpa,InputStart,InputEnd,FlushStart,FlushEnd; } ADMISSION_G3_FLUSH_RECEIPT;
typedef struct _ADMISSION_CONTEXT ADMISSION_CONTEXT;
typedef struct _ADMISSION_G3_PROCESS ADMISSION_G3_PROCESS;
typedef struct _ADMISSION_OBJECT_DEVICE { UINT Magic; void *Adapter; } ADMISSION_OBJECT_DEVICE;
typedef struct { UINT Magic,Flags; ADMISSION_OBJECT_DEVICE *Device;
  UINT FenceOutstanding; } ADMISSION_OBJECT_CONTEXT;
typedef struct _ADMISSION_DEVICE { ADMISSION_OBJECT_DEVICE Object; LONG Win32Generation; ADMISSION_G3_PROCESS *GpuvaG3Process; } ADMISSION_DEVICE;
typedef struct { ADMISSION_ALLOCATION_OBJECT Object; ULONG QualificationCookie,Win32ClassId,Win32Flags,Presentation; } ADMISSION_ALLOCATION_HANDLE;
#define ADMISSION_OPEN_ALLOCATION_MAGIC 0x4f504152u
typedef struct { ULONG Magic; ADMISSION_DEVICE *Device; UINT RuntimeAllocation;
 ADMISSION_ALLOCATION_OBJECT *Allocation; BOOLEAN ReadOnly;
 ULONG Win32Generation,Win32ClassId,Win32Flags; } ADMISSION_OPEN_ALLOCATION;
typedef struct { int unused; } ADMISSION_SCHEDULER_CONTEXT;
typedef struct { int unused; } ADMISSION_PREPATCHED_RENDER;
typedef struct _ADMISSION_RENDER_CONTEXT { ADMISSION_OBJECT_CONTEXT Object; UINT Win32Generation; BOOLEAN Win32Transport,GpuvaG3Poisoned; ADMISSION_SCHEDULER_CONTEXT SchedulerContext; ADMISSION_PREPATCHED_RENDER PrepatchedRender; ADMISSION_G3_PROCESS *GpuvaG3Process; struct _ADMISSION_RENDER_CONTEXT *GpuvaG3NextContext; volatile LONG GpuvaG3PrivateFence,GpuvaG3CancelFence,GpuvaG3CancelUncertain, GpuvaG3PreemptFence; volatile LONG GpuvaG3PrivateFence2,GpuvaG3CancelFence2,GpuvaG3PreemptFence2; ULONG GpuvaG3SecondFence; BOOLEAN GpuvaG3Closing; ULONGLONG GpuvaG3PrivateManagerGeneration; ULONGLONG GpuvaG3LastSetRootIpa; ULONG GpuvaG3SetRootCount; ULONGLONG GpuvaG3RootIpa,GpuvaG3DmaBufferVa,GpuvaG3MappingGeneration; ULONG GpuvaG3DmaBufferBytes; } ADMISSION_RENDER_CONTEXT;
static inline void AdmissionContextRetireFence(ADMISSION_RENDER_CONTEXT *c, ULONG f) {
  if (!c || !f) return;
  if (c->Object.FenceOutstanding == f) { c->Object.FenceOutstanding = c->GpuvaG3SecondFence; c->GpuvaG3SecondFence = 0; }
  else if (c->GpuvaG3SecondFence == f) c->GpuvaG3SecondFence = 0;
}

#define ADMISSION_G3_LEAF_HISTORY_COUNT 256u
#define ADMISSION_G3_LEAF_RING 8192u
#define ADMISSION_G3_LEAF_EVENT_RESET 0x10u
#define ADMISSION_G3_LEAF_EVENT_SYSTEM_RETIRE 0x20u
typedef struct _ADMISSION_G3_LEAF_HISTORY {
  ULONGLONG Qpc, ProcessId, TableIpa, Allocation, FirstVa, MappingGeneration;
  ULONG First, Count, ValidCount, Flags, Status, FirstSegment;
} ADMISSION_G3_LEAF_HISTORY;
/* EXP987 receipt-only: R155/R165 BuildPagingBuffer waits for a process's
 * in-flight job. Snapshot of the condition when the wait began. */
typedef struct _ADMISSION_G3_PAGING_WAIT_RECEIPT {
  ULONG Version, Bytes, Waits, Timeouts;
  ULONGLONG TotalIterations, TotalTicks, QpcFrequency;
  ULONGLONG MaxTicks, MaxQpc, MaxGraphProcessId, MaxActiveGraphProcessId;
  ULONG MaxIterations, MaxOperation, MaxJobInFlight, MaxLease;
  ULONG MaxActiveIsProcess, MaxActiveFence, MaxLastCompletedFence,
      MaxPrivateCompletionFence;
} ADMISSION_G3_PAGING_WAIT_RECEIPT;
/* EXP988: QUERY predicate57 re-validation (PTE not yet populated by VidMm). */
typedef struct _ADMISSION_G3_PTE_WAIT_RECEIPT {
  ULONG Version, Bytes, Waited, Recovered, TimedOut, MaxIterations;
  ULONGLONG MaxTicks, TotalTicks, QpcFrequency, LastVa;
} ADMISSION_G3_PTE_WAIT_RECEIPT;
/* EXP990 receipt-only: what the GPU will read for a G4 render, sampled at
 * BeginJob through the process logical PTEs (bit0 resolved, bit1 all valid). */
typedef struct _ADMISSION_G4_DRAW_SNAP {
  ULONG Fence, Flags, PppCtrl, Width, Height, BgUsc, EotUsc, Process;
  ULONGLONG VdmBase, ScissorBase, DbiasBase, VdmIpa;
  ULONG VdmState, ScissorState, DbiasState, Reserved;
  UCHAR Vdm[256];
  UCHAR Scissor[32];
  UCHAR Dbias[16];
  /* EXP991: chain decoded from the VDM stream at job time. */
  ULONGLONG PppAddr[4], PipeAddr;
  ULONG PppState[4], PipeState, PppCount, IndexWord, IndexAt;
  UCHAR Ppp[4][64];
  UCHAR Pipe[64];
} ADMISSION_G4_DRAW_SNAP;
#define ADMISSION_G4_DRAW_SNAP_COUNT 4u
typedef struct _ADMISSION_G4_DRAW_SNAPSHOT {
  ULONG Version, Bytes, Next, Reserved;
  ADMISSION_G4_DRAW_SNAP Slot[ADMISSION_G4_DRAW_SNAP_COUNT];
} ADMISSION_G4_DRAW_SNAPSHOT;
/* EXP992 receipt-only: final firmware TA/3D/microsequence bytes of a 77x77
 * marker render at BeginJob (template objects 19, 18, 15, 17). */
typedef struct _ADMISSION_G4_FW_SNAP {
  ULONG Fence, Sizes[4];
  ULONGLONG GpuVa[4];
  UCHAR Ta[1564];
  UCHAR D3[2420];
  UCHAR Seq15[512];
  UCHAR Seq17[512];
  /* EXP993: native-graph (GPU table) inspection of the TA buffers. */
  ULONGLONG CheckVa[10], PrivateVa;
  ULONG CheckOk[10], CheckReason[10], CheckLevel[10], CheckCount;
} ADMISSION_G4_FW_SNAP;
typedef struct _ADMISSION_G4_FW_SNAPSHOT {
  ULONG Version, Bytes, Next, Reserved;
  ADMISSION_G4_FW_SNAP Slot[4];
} ADMISSION_G4_FW_SNAPSHOT;
#define ADMISSION_G3_ALLOC_TRACK_COUNT 1024u
/* EXP982: last valid leaf mapping per VidMm allocation handle. */
typedef struct _ADMISSION_G3_ALLOC_TRACK {
  ULONGLONG Allocation, ProcessId, LastValidVa, LastValidQpc, LastAnyQpc, LastAnyVa;
  ULONG LastValidCount, Maps, LastSegment, LastFlags;
  /* EXP983: physical/virtual FILL and TRANSFER paging operations. */
  ULONGLONG LastFillQpc, LastTransferQpc;
  ULONG Fills, Transfers, LastPagingSegment, LastPagingOperation;
} ADMISSION_G3_ALLOC_TRACK;
typedef struct _ADMISSION_G3_LEAF_HISTORY_SNAPSHOT {
  ULONG Version, Bytes, Next, Predicate;
  ULONGLONG FailVa, FailProcessId, FailTableIpa, FailAllocation, Qpc, QpcFrequency;
  ULONG FailIndex, Reserved;
  ADMISSION_G3_LEAF_HISTORY Records[ADMISSION_G3_LEAF_HISTORY_COUNT];
  /* EXP982 (Version 2): failing allocation and raw failing PTE. */
  ULONGLONG KmdAllocation, AllocationSize, PteGuestIpa, PteAllocation, PteAllocationOffset;
  ULONG AllocationType, PteFound, PteSegment, PteFlags, TrackFound, TrackReserved;
  ADMISSION_G3_ALLOC_TRACK Track;
} ADMISSION_G3_LEAF_HISTORY_SNAPSHOT;
#define ADMISSION_G3_UPLOAD_TRACE_COUNT 64u
typedef struct { ULONGLONG ProcessId,Va; ULONG Bytes,Hash,GpuHash,Checks; } ADMISSION_G3_UPLOAD_TRACE;
typedef struct _ADMISSION_G3_STATE { ADMISSION_CONTEXT *Adapter; FAST_MUTEX Lock; KEVENT JobEvent; LIST_ENTRY Processes; APPLE_AGX_GPUVA_V5_CLIENT Client; APPLE_AGX_GPUVA_G3_REGISTRY Registry; APPLE_AGX_G3_PRIVATE_POOL PrivatePool; ULONGLONG NextProcessId; ULONG ProcessCount; ADMISSION_G3_PROCESS *ActiveProcess; ULONG ActiveFence,LastCompletedFence,PrivateCompletionFence; ULONGLONG UnpublishedGroups[32]; ADMISSION_G3_UPLOAD_TRACE UploadTrace[ADMISSION_G3_UPLOAD_TRACE_COUNT]; ULONG UploadTraceNext,UploadVerifyChecks,UploadVerifyMismatch,UploadVerifyUnmapped; ADMISSION_G3_UPLOAD_TRACE UploadFirstMismatch; ADMISSION_G3_LEAF_HISTORY LeafHistory[ADMISSION_G3_LEAF_RING]; ULONG LeafHistoryNext; ADMISSION_G3_ALLOC_TRACK AllocTrack[ADMISSION_G3_ALLOC_TRACK_COUNT]; ULONGLONG PrivateStats[16]; LONGLONG ProcessReceiptQpc; } ADMISSION_G3_STATE;
enum { ADMISSION_G3_PRIVATE_STAT_HIT, ADMISSION_G3_PRIVATE_STAT_MISS, ADMISSION_G3_PRIVATE_STAT_TRIM, ADMISSION_G3_PRIVATE_STAT_PRESSURE,
  ADMISSION_G3_PRIVATE_STAT_RELEASED, ADMISSION_G3_PRIVATE_STAT_MAPPED_PAGES, ADMISSION_G3_PRIVATE_STAT_UNMAPPED_PAGES,
  ADMISSION_G3_PRIVATE_STAT_CACHED_RELEASE, ADMISSION_G3_PRIVATE_STAT_CACHED_REAP, ADMISSION_G3_PRIVATE_STAT_RELEASE_QUEUED,
  ADMISSION_G3_PRIVATE_STAT_MISS_OTHER_GEOMETRY, ADMISSION_G3_PRIVATE_STAT_HEAP_GROW, ADMISSION_G3_PRIVATE_STAT_MISS_SHAPE=12 };
typedef struct _ADMISSION_G3_TABLE_SHADOW { struct _ADMISSION_G3_TABLE_SHADOW *Next,*NextBroker; APPLE_AGX_MEMORY_OBJECT Memory; ULONGLONG OriginalIpa,BrokerIpa; APPLE_AGX_GPUVA_G3_LOGICAL_PTE *LogicalPtes,*ResidentPtes,*PendingPtes; } ADMISSION_G3_TABLE_SHADOW;
typedef struct _ADMISSION_G3_PRIVATE_SCENE {
  struct _ADMISSION_G3_PRIVATE_SCENE *Next;
  ADMISSION_RENDER_CONTEXT *Context;
  APPLE_AGX_G3_PRIVATE_SCENE Storage;
  APPLE_AGX_G4_NATIVE_RENDER Geometry;
  ULONG Fence, ResumeFence, Submitting, Queued, Started, GpuDone, Reported, ReleaseRequested, Quarantined;
  ULONG HeapBlocks; /* EXP1115 */
  ULONG Cached;
  ULONGLONG CachedAt;
} ADMISSION_G3_PRIVATE_SCENE;
#define ADMISSION_G3_PRIVATE_SCENE_CACHE 8u
#define ADMISSION_G3_PROCESS_RECEIPT_SLOTS 16u
#define ADMISSION_G3_PROCESS_RECEIPT_WORDS (8u + 16u * ADMISSION_G3_PROCESS_RECEIPT_SLOTS)

struct _ADMISSION_G3_PROCESS {
  LIST_ENTRY Link;
  ADMISSION_G3_STATE *State;
  APPLE_AGX_GPUVA_G3_GRAPH Graph;
  APPLE_AGX_MEMORY_IO Io;
  APPLE_AGX_MEMORY_OBJECT BootstrapRoot;
  ADMISSION_G3_TABLE_SHADOW *TableShadows;
  ADMISSION_G3_TABLE_SHADOW *TableShadowBrokerBuckets[256];
  ULONGLONG BootstrapIpa,LastSetRootIpa;
  ULONG SetRootCount;
  ULONGLONG PrivateVa;
  ULONGLONG PrivateMiddleIpa, PrivateLeafIpa;
  ULONGLONG PrivateCacheClock; /* EXP1087 */
  ULONG MaxTvbBlocks, MaxShape; /* EXP1114 */
  APPLE_AGX_G3_PRIVATE_EXTENT PrivateTables[2];
  APPLE_AGX_G3_PRIVATE_MANAGER PrivateManager;
  APPLE_AGX_RENDER_MANAGER_STATE FirmwareManager;
  ADMISSION_G3_PRIVATE_SCENE *PrivateScenes;
  ADMISSION_RENDER_CONTEXT *Contexts;
  HANDLE DxgkProcess;
  ULONG Magic, DeviceRefs, ContextRefs, OsProcessId;
  BOOLEAN Poisoned;
  ULONG PoisonSite, PoisonBrokerStatus;
  BOOLEAN CpuOnlyMappings;
};
#define ADMISSION_G3_POISON(Process, File) do { \
    (Process)->Poisoned = TRUE; \
    if (!(Process)->PoisonSite) \
      (Process)->PoisonSite = ((ULONG)(File) << 16) | (ULONG)__LINE__; \
  } while (0)

typedef struct { int unused; } REPLAY_APERTURE;
typedef struct {
  UINT G4Native,BoundFence,G4CommandBytes;
  APPLE_AGX_G4_PRIVATE_HEADER_V2 G4Header;
  unsigned char Commands[APPLE_AGX_G4_NATIVE_MAX_BYTES];
  APPLE_AGX_G4_PRIVATE_LEASE G4Lease;
  APPLE_AGX_RENDER_MANAGER_STATE *G4Manager;
  APPLE_AGX_RENDER_MANAGER_KEY G4ManagerKey;
} ADMISSION_BACKEND_IMAGE;
typedef struct { int Valid; } ADMISSION_BACKEND_IMAGE_SNAPSHOT;
typedef struct { unsigned State; struct { ULONG Fence; ULONGLONG ContextToken; } Description; } REPLAY_PACKET;
struct _ADMISSION_CONTEXT { REPLAY_PACKET RenderPacket; BOOLEAN InterfaceValid; LONG RenderDpcFence,SchedulerDpcPending; DXGKRNL_INTERFACE Interface; void *GpuvaG3State; BOOLEAN Started;
  PDEVICE_OBJECT PhysicalDeviceObject; ADMISSION_CONTEXT *ObjectAdapter;
  volatile LONG G3CopyQueryFailureClaim;
  ULONG G3CopyQueryFailurePredicate,G3CopyQueryFailureStatus;
  APPLE_AGX_G3_COPY_QUERY_RECEIPT G3CopyQueryFailure;
  volatile LONG G3CopyTransferFailureClaim;
  APPLE_AGX_G3_COPY_TRANSFER_FAILURE G3CopyTransferFailure;
  volatile LONG G3LeafHistoryClaim;
  ADMISSION_G3_LEAF_HISTORY_SNAPSHOT G3LeafHistorySnapshot;
  ADMISSION_G3_PAGING_WAIT_RECEIPT G3PagingWait;
  ADMISSION_G3_PTE_WAIT_RECEIPT G3PteWait;
  ADMISSION_G4_DRAW_SNAPSHOT G4DrawSnapshot;
  ADMISSION_G4_FW_SNAPSHOT G4FwSnapshot;
  volatile LONG G4DrawSnapshotDirty;
  volatile LONG G3PteWaitDirty;
  volatile LONG G3PagingWaitDirty;
  volatile LONG G3PrivateFailureClaim;
  APPLE_AGX_G3_PRIVATE_FAILURE G3PrivateFailure;
  int SchedulerLock,Scheduler,PagingLock;
  LONG PagingRecordsUnsubmitted;
  ULONG CpuQueueCount,PagingFence,PagingLastSubmittedFence,PagingLastCompletedFence;
  volatile LONG PagingPending,PagingWorkersActive,PagingDpcPending,PagingDpcsActive,SchedulerFaulted;
  ADMISSION_BACKEND_IMAGE BackendImage;
  ADMISSION_BACKEND_IMAGE_SNAPSHOT BackendSnapshot;
  struct { REPLAY_APERTURE Aperture; } Memory;
};
static NTSTATUS AdmissionDwmDdiProbeQueryWindows(ADMISSION_CONTEXT *context,
    ADMISSION_DWM_DDI_PROBE *query) {
  (void)context;(void)query;return STATUS_NOT_SUPPORTED;
}
#define PLUGPLAY_REGKEY_DEVICE 1u
#define KEY_SET_VALUE 2u
#define REG_BINARY 3u
typedef struct { const wchar_t *Buffer; } UNICODE_STRING;
static void RtlInitUnicodeString(UNICODE_STRING *name,const wchar_t *value) {name->Buffer=value;}
static NTSTATUS ZwSetValueKey(HANDLE key,const UNICODE_STRING *name,ULONG title,
    ULONG type,void *data,ULONG bytes) {
  (void)title;(void)data;(void)bytes;
  assert(key==(HANDLE)0x5588 && name && !wcscmp(name->Buffer,L"Wom1G3CopyPagingQuiescence") && type==REG_BINARY);
  return STATUS_SUCCESS;
}
static UINT r145_references;
static ULONG query_registry_writes,query_registry_flushes,query_registry_receipt[42];
static ULONG private_registry_writes;
static ULONG transfer_registry_writes;
static APPLE_AGX_G3_COPY_TRANSFER_FAILURE transfer_registry_receipt;
static APPLE_AGX_G3_PRIVATE_FAILURE private_registry_receipt;
#define HandleToULong(h) ((ULONG)(uintptr_t)(h))
static HANDLE PsGetCurrentProcessId(void) {return (HANDLE)0x887;}
static NTSTATUS IoOpenDeviceRegistryKey(PDEVICE_OBJECT device,ULONG kind,ULONG access,HANDLE *key) {
  assert(device && kind==1 && access==2 && replay_irql==PASSIVE_LEVEL && !r145_references);
  *key=(HANDLE)0x5588;return STATUS_SUCCESS;
}
static unsigned leaf_history_registry_writes;
static unsigned private_procs_registry_writes;
static void WriteBinary(HANDLE key,const wchar_t *name,const VOID *data,ULONG bytes) {
  if (!wcscmp(name,L"Wom1G3CopyTransferFailure")) {
    assert(key==(HANDLE)0x5588 && bytes==sizeof(transfer_registry_receipt) && replay_irql==PASSIVE_LEVEL);
    memcpy(&transfer_registry_receipt,data,bytes);++transfer_registry_writes;return;
  }
  if (!wcscmp(name,L"Wom1G3PrivateAcquireFailure")) {
    assert(key==(HANDLE)0x5588 && bytes==sizeof(private_registry_receipt) && replay_irql==PASSIVE_LEVEL);
    memcpy(&private_registry_receipt,data,bytes);++private_registry_writes;return;
  }
  if (!wcscmp(name,L"Wom1G3PrivateProcs")) {
    /* EXP1114 receipt-only snapshot; never flushed. */
    assert(key==(HANDLE)0x5588 && bytes==ADMISSION_G3_PROCESS_RECEIPT_WORDS*sizeof(ULONG) && replay_irql==PASSIVE_LEVEL);
    ++private_procs_registry_writes;return;
  }
  if (!wcscmp(name,L"Wom1G3LeafHistory")) {
    /* EXP979 diagnostic snapshot at the first predicate57; flush not counted. */
    assert(key==(HANDLE)0x5588 && bytes==sizeof(ADMISSION_G3_LEAF_HISTORY_SNAPSHOT) && replay_irql==PASSIVE_LEVEL);
    ++leaf_history_registry_writes;--query_registry_flushes;return;
  }
  assert(key==(HANDLE)0x5588 && !wcscmp(name,L"Wom1G3CopyQueryFailure"));
  assert((bytes==16 || bytes==144 || bytes==168) && replay_irql==PASSIVE_LEVEL);
  memcpy(query_registry_receipt,data,bytes);++query_registry_writes;
}
static NTSTATUS ZwFlushKey(HANDLE key) {assert(key==(HANDLE)0x5588);++query_registry_flushes;return STATUS_SUCCESS;}
static void ZwClose(HANDLE key) {assert(key==(HANDLE)0x5588);}
static BOOLEAN AdmissionPagingQuiescent(ADMISSION_CONTEXT *a) {
  (void)a;
  ++replay_quiescence_calls;
  if (replay_quiescence_flip_on_call == replay_quiescence_calls) {
    replay_quiescence_flip_on_call=0;
    replay_paging_pending=1;
    return TRUE;
  }
  if (replay_quiescence_flip_after_success) {
    replay_quiescence_flip_after_success=0;
    replay_paging_pending=1;
    return TRUE;
  }
  return replay_paging_pending==0;
}
static void AdmissionPagingNoteEncoded(ADMISSION_CONTEXT *a,UINT n) {(void)a;replay_encoded_records+=n;}
int AdmissionPagingLocalRun(const ADMISSION_PAGING_RECORD *);
int AdmissionPagingRecordsValid(const ADMISSION_PAGING_RECORD *,UINT,UINT,UINT);
static UINT replay_paging_submits,replay_paging_submit_bytes,replay_paging_submit_fence;
static NTSTATUS AdmissionCpuQueueSubmit(ADMISSION_CONTEXT *adapter,
    const DXGKARG_SUBMITCOMMAND *args,ULONG kind,const VOID *data,UINT bytes) {
  (void)adapter;
  if (!args || kind!=ADMISSION_CPU_PACKET_PAGING || !args->Flags.Paging ||
      !data || !bytes || bytes%sizeof(ADMISSION_PAGING_RECORD) ||
      !AdmissionPagingRecordsValid(data,bytes/sizeof(ADMISSION_PAGING_RECORD),
          ADMISSION_MAX_PAGING_RECORDS,
          bytes/sizeof(ADMISSION_PAGING_RECORD)*sizeof(ADMISSION_PAGING_MARKER)))
    return STATUS_INVALID_PARAMETER;
  ++replay_paging_submits;
  replay_paging_submit_bytes=bytes;
  replay_paging_submit_fence=args->SubmissionFenceId;
  return STATUS_SUCCESS;
}
NTSTATUS AdmissionGpuvaG3SubmitVirtualPaging(ADMISSION_CONTEXT *,
    ADMISSION_RENDER_CONTEXT *,const DXGKARG_SUBMITCOMMANDVIRTUAL *);

enum { AdmissionG4RejectPagingInput=2, AdmissionG4RejectPagingShape,
  AdmissionG4RejectPagingRecords, AdmissionG4RejectPagingQueue };
/* Receipt sink only; keep the paging return status and poison side effect. */
static NTSTATUS AdmissionG4SubmitReject(ADMISSION_CONTEXT *a,
    ADMISSION_RENDER_CONTEXT *c, const DXGKARG_SUBMITCOMMANDVIRTUAL *v,
    ULONG branch, NTSTATUS status, ULONG downstream, BOOLEAN poison) {
  (void)a; (void)v; (void)branch; (void)downstream;
  if (poison && c) c->GpuvaG3Poisoned=TRUE;
  return status;
}

/* Step-5 output view is outside this lifetime replay; the real G4 submit
 * suite separately covers output resolution. No queue or graph is mocked. */
static BOOLEAN AdmissionG3OutputMatchesLocal(ADMISSION_CONTEXT *a,
    APPLE_AGX_GPUVA_G3_GRAPH *g) {(void)a;(void)g;return TRUE;}
typedef ADMISSION_SCANOUT_MEMORY_VIEW ADMISSION_BACKEND_MEMORY_VIEW;
static unsigned char *local_cpu;
static ULONGLONG local_ipa=0x10000000ULL;
static ULONGLONG local_bytes=0x8000000ULL;
static ULONGLONG vidmm_local_bytes=40ULL<<20;
static unsigned char system_cpu[0x4000];
static ULONGLONG system_ipa=0x851000000ULL;
enum { AppleAgxSoftwareApertureOk=0, AppleAgxSoftwareApertureOutOfRange=1 };
static int AppleAgxSoftwareApertureResolve(REPLAY_APERTURE *a,
    ULONGLONG offset,ULONGLONG *physical) {
  (void)a;
  if (offset<0x1000 || offset>=0x2000) return AppleAgxSoftwareApertureOutOfRange;
  *physical=system_ipa+offset;
  return AppleAgxSoftwareApertureOk;
}
static void *MmMapIoSpace(PHYSICAL_ADDRESS pa,SIZE_T bytes,int cache) {
  (void)cache;
  return pa.QuadPart>=0 && (ULONGLONG)pa.QuadPart>=system_ipa &&
      (ULONGLONG)pa.QuadPart-system_ipa<=sizeof(system_cpu)-bytes ?
      system_cpu+((ULONGLONG)pa.QuadPart-system_ipa) : NULL;
}
static void MmUnmapIoSpace(void *p,SIZE_T bytes) {(void)p;(void)bytes;}
#define MmCached 1
NTSTATUS AdmissionG3ExecuteVirtualPaging(ADMISSION_CONTEXT *,
    const ADMISSION_PAGING_RECORD *);
static PHYSICAL_ADDRESS MmGetPhysicalAddress(void *p) { PHYSICAL_ADDRESS a={0};if(local_cpu && (unsigned char *)p>=local_cpu && (unsigned char *)p<local_cpu+local_bytes) a.QuadPart=(long long)(local_ipa+((unsigned char *)p-local_cpu));return a; }
static NTSTATUS AdmissionMemoryRuntimePrivateView(ADMISSION_CONTEXT *a,ADMISSION_BACKEND_MEMORY_VIEW *v) {(void)a;v->GuestIpaAddress=local_ipa+vidmm_local_bytes;v->Bytes=(ULONGLONG)APPLE_AGX_G3_PRIVATE_UNITS*APPLE_AGX_G3_PRIVATE_UNIT;v->CpuAddress=local_cpu+vidmm_local_bytes;return STATUS_SUCCESS;}
static NTSTATUS replay_local_view_status;
static NTSTATUS AdmissionMemoryRuntimeLocalView(ADMISSION_CONTEXT *a,ADMISSION_SCANOUT_MEMORY_VIEW *v) {(void)a;memset(v,0,sizeof(*v));v->GuestIpaAddress=local_ipa;v->Bytes=vidmm_local_bytes;v->CpuAddress=local_cpu;return replay_local_view_status;}
#ifdef G3_REPLAY_FULL_LOCAL
#define APPLE_AGX_SCANOUT_J313_POOL_SIZE (56ULL<<20)
NTSTATUS AdmissionMemoryRuntimeScanoutView(ADMISSION_CONTEXT *,ADMISSION_SCANOUT_MEMORY_VIEW *);
#else
static NTSTATUS AdmissionMemoryRuntimeScanoutView(ADMISSION_CONTEXT *a,ADMISSION_SCANOUT_MEMORY_VIEW *v) {return AdmissionMemoryRuntimeLocalView(a,v);}
#endif
static NTSTATUS AdmissionMemoryRuntimeBorrowIo(ADMISSION_CONTEXT *a,APPLE_AGX_MEMORY_IO *io) {(void)a;(void)io;return STATUS_SUCCESS;}
static APPLE_AGX_MEMORY_RESULT AppleAgxMemoryAllocateAligned(APPLE_AGX_MEMORY_IO *io,ULONGLONG n,ULONGLONG align,APPLE_AGX_MEMORY_OBJECT *o) {
  (void)io;assert(n==0x4000 && align==0x4000);
  static ADMISSION_PHYSICAL_ALLOCATION alloc[512];static REPLAY_ADL adl[512];static UINT count;
  assert(count<512);
  ULONGLONG offset=vidmm_local_bytes+(ULONGLONG)APPLE_AGX_G3_PRIVATE_UNITS*APPLE_AGX_G3_PRIVATE_UNIT+(ULONGLONG)count*0x4000ULL;
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
  uint64_t blocked_ipa, bad_subpage;
  UINT sync_failures, invalidate_failures;
  UINT commands;
  uint64_t last_flush_start, last_flush_end;
  UINT flush_commands;
} REPLAY_BROKER;
static struct hv_agx_gpuva_v5 gpuva_v5;
/* Platform global touched by the extracted gpuva_execute (mailbox attach);
 * the replay drives each broker through its own REPLAY_BROKER wire. */
static struct hv_agx_gpuva_v5_wire gpuva_v5_wire;
static bool request_powered = true;
static void gpuva_execute(void *, const AGX_GPUVA_V5_REQUEST *, AGX_GPUVA_V5_RESPONSE *);
/* Feed the production translate_guest implementation an explicit stage-2
 * model. System payload is nonidentity; tables remain in the local reserve. */
typedef uint64_t u64;
static struct {u64 phys_base,mem_size;} cur_boot_args;
static struct {u64 ramdisk_base,ramdisk_max_size;} J313_AUTONOMOUS_LAYOUT;
static u64 root_base,root_length;
static bool replay_identity_ram;
static bool launch_memory_valid;
static struct hv_contract_snapshot launch_memory;
static REPLAY_BROKER *translation_broker;
static u64 hv_ipa_to_pa(u64 ipa) {
  REPLAY_BROKER *b=translation_broker;
  if ((b->blocked_ipa && (ipa & ~0x3fffULL)==b->blocked_ipa) ||
      (b->bad_subpage && (ipa & ~0xfffULL)==b->bad_subpage)) return 0;
  if (replay_identity_ram && ipa>=0x850000000ULL && ipa<0x9df708000ULL) return ipa;
  if (ipa>=local_ipa && ipa-local_ipa<local_bytes) return ipa;
  if (ipa>=system_ipa && ipa-system_ipa<0x10000000ULL) return ipa+0x10000000ULL;
  return 0; /* MMIO/software/protected stage-2 slot */
}
static u64 translate_guest(void *,u64);
static uint64_t ReplayTranslate(void *opaque,uint64_t ipa) {
  REPLAY_BROKER *b=opaque;
  translation_broker=b;launch_memory=b->memory;launch_memory_valid=true;
  cur_boot_args.phys_base=b->memory.boot.ram_base;
  cur_boot_args.mem_size=b->memory.boot.ram_size;
  return translate_guest(opaque,ipa);
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
static bool ReplaySync(void *opaque) {REPLAY_BROKER *b=opaque;if(b->sync_failures){--b->sync_failures;return false;}return true;}
static bool ReplayInvalidate(void *opaque,unsigned slot) {REPLAY_BROKER *b=opaque;if(b->invalidate_failures){--b->invalidate_failures;return false;}return slot>0 && slot<HV_AGX_GPUVA_V5_SLOTS;}
static bool ReplayPrefix(void *opaque) {(void)opaque;return true;}
static bool ReplayLegacySlot63(void *opaque) {(void)opaque;return false;}
static void ReplayBrokerInit(REPLAY_BROKER *b) {
  struct hv_agx_gpuva_v5_ops ops={b,ReplayTranslate,ReplayMapPage,ReplayReadSlot,
      ReplayWriteSlot,ReplaySync,ReplayInvalidate,ReplayPrefix,ReplayLegacySlot63};
  b->memory.boot.ram_base=local_ipa<system_ipa+0x10000000ULL ? local_ipa : system_ipa+0x10000000ULL;
  ULONGLONG ram_end=local_ipa+local_bytes>system_ipa+0x20000000ULL ?
      local_ipa+local_bytes : system_ipa+0x20000000ULL;
  b->memory.boot.ram_size=ram_end-b->memory.boot.ram_base;
  b->memory.region_count=2;
  b->memory.regions[1].kind=HV_CONTRACT_REGION_GUEST_RAM;
  b->memory.regions[1].base=system_ipa+0x10000000ULL;
  b->memory.regions[1].size=0x10000000ULL;
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
static void AdmissionRecordG3PagingWait(ADMISSION_CONTEXT *a) {(void)a;}
static void AdmissionRecordG3PteWait(ADMISSION_CONTEXT *a) {(void)a;}
static void AdmissionRecordG4DrawSnapshot(ADMISSION_CONTEXT *a) {(void)a;}
static void AdmissionRecordGpuvaG3UnpublishedGroups(ADMISSION_CONTEXT *a,const ULONGLONG *counts) {(void)a;(void)counts;}
static void AppleAgxSchedulerContextInitialize(ADMISSION_SCHEDULER_CONTEXT *c) {(void)c;}
static void AdmissionPrepatchedInitialize(ADMISSION_PREPATCHED_RENDER *p) {(void)p;}
static bool AdmissionPrepatchedActive(ADMISSION_PREPATCHED_RENDER *p) {(void)p;return false;}
static bool AdmissionObjectsCreateContext(ADMISSION_OBJECT_DEVICE *device,HANDLE runtime,UINT node,UINT affinity,UINT flags,ADMISSION_OBJECT_CONTEXT *context) {(void)runtime;if(node!=0||affinity!=1)return false;context->Magic=ADMISSION_OBJECT_CONTEXT_MAGIC;context->Flags=flags;context->Device=device;return true;}
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
#if defined(G3_PRIVATE_COMBINED)
#include "g3_r137_completion_shim.h"
#endif
#endif

/* Read-only display diagnostic is outside the VidMm replay boundary. */
static NTSTATUS AdmissionScanoutQueryTimeline(ADMISSION_CONTEXT *c, APPLE_AGX_VSYNC_QUERY *q) { (void)c; (void)q; return STATUS_INVALID_PARAMETER; }
