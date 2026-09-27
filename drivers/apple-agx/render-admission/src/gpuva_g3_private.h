#ifndef ADMISSION_GPUVA_G3_PRIVATE_H
#define ADMISSION_GPUVA_G3_PRIVATE_H

#include "render_admission.h"
#include "apple_agx_g3_private_storage.h"
#include "apple_agx_g3_private_abi.h"

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
#define ADMISSION_G3_PROCESS_MAGIC 0x47335052u

typedef struct _ADMISSION_G3_STATE {
  ADMISSION_CONTEXT *Adapter;
  FAST_MUTEX Lock;
  LIST_ENTRY Processes;
  APPLE_AGX_GPUVA_V5_CLIENT Client;
  APPLE_AGX_GPUVA_G3_REGISTRY Registry;
  APPLE_AGX_G3_PRIVATE_POOL PrivatePool;
  ULONGLONG NextProcessId;
  ULONG ProcessCount;
  struct _ADMISSION_G3_PROCESS *ActiveProcess;
  ULONG ActiveFence;
  ULONG LastCompletedFence;
  ULONG PrivateCompletionFence;
  ULONGLONG UnpublishedGroups[32];
} ADMISSION_G3_STATE;

typedef struct _ADMISSION_G3_TABLE_SHADOW {
  struct _ADMISSION_G3_TABLE_SHADOW *Next;
  APPLE_AGX_MEMORY_OBJECT Memory;
  ULONGLONG OriginalIpa, BrokerIpa;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *LogicalPtes;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *ResidentPtes;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *PendingPtes;
} ADMISSION_G3_TABLE_SHADOW;

typedef struct _ADMISSION_G3_PRIVATE_SCENE {
  struct _ADMISSION_G3_PRIVATE_SCENE *Next;
  ADMISSION_RENDER_CONTEXT *Context;
  APPLE_AGX_G3_PRIVATE_SCENE Storage;
  APPLE_AGX_G4_NATIVE_RENDER Geometry;
  ULONG Fence, Submitting, Queued, Started, GpuDone, Reported, ReleaseRequested, Quarantined;
} ADMISSION_G3_PRIVATE_SCENE;

typedef struct _ADMISSION_G3_PROCESS {
  LIST_ENTRY Link;
  ADMISSION_G3_STATE *State;
  APPLE_AGX_GPUVA_G3_GRAPH Graph;
  APPLE_AGX_MEMORY_IO Io;
  APPLE_AGX_MEMORY_OBJECT BootstrapRoot;
  ADMISSION_G3_TABLE_SHADOW *TableShadows;
  ULONGLONG BootstrapIpa;
  ULONGLONG PrivateVa;
  ULONGLONG PrivateMiddleIpa, PrivateLeafIpa;
  APPLE_AGX_G3_PRIVATE_EXTENT PrivateTables[2];
  APPLE_AGX_G3_PRIVATE_MANAGER PrivateManager;
  ADMISSION_G3_PRIVATE_SCENE *PrivateScenes;
  ADMISSION_RENDER_CONTEXT *Contexts;
  HANDLE DxgkProcess;
  ULONG Magic, DeviceRefs, ContextRefs;
  BOOLEAN Poisoned;
} ADMISSION_G3_PROCESS;

NTSTATUS AdmissionG3PreparePrivateStorage(
    ADMISSION_G3_PROCESS *Process, const APPLE_AGX_G4_NATIVE_RENDER *Render,
    APPLE_AGX_G3_PRIVATE_MANAGER *Manager, APPLE_AGX_G3_PRIVATE_SCENE *Scene);

ADMISSION_G3_PROCESS *AdmissionGpuvaG3FindProcess(
    ADMISSION_G3_STATE *State, HANDLE ProcessHandle);
NTSTATUS AdmissionGpuvaG3ResolveTable(
    ADMISSION_CONTEXT *Adapter, const DXGK_PAGETABLEUPDATEADDRESS *Address,
    DXGK_PAGETABLEUPDATEMODE Mode, ULONGLONG *TableIpa);
NTSTATUS AdmissionGpuvaG3BrokerTable(
    ADMISSION_G3_PROCESS *Process, ULONGLONG OriginalIpa,
    BOOLEAN Create, ULONGLONG *BrokerIpa);
NTSTATUS AdmissionGpuvaG3MirrorTable(
    ADMISSION_G3_PROCESS *Process, ULONGLONG OriginalIpa,
    PVOID OriginalCpuAddress);
NTSTATUS AdmissionGpuvaG3BuildPagingBuffer(ADMISSION_CONTEXT *Adapter,
                                           DXGKARG_BUILDPAGINGBUFFER *Args);
NTSTATUS AdmissionG3ExecuteVirtualPaging(
    ADMISSION_CONTEXT *Adapter, const ADMISSION_PAGING_RECORD *Record);
NTSTATUS AdmissionGpuvaG3SubmitVirtualPaging(
    ADMISSION_CONTEXT *Adapter, ADMISSION_RENDER_CONTEXT *Context,
    const DXGKARG_SUBMITCOMMANDVIRTUAL *Args);
NTSTATUS AdmissionGpuvaG3BeginJob(ADMISSION_CONTEXT *Adapter,
                                  ADMISSION_RENDER_CONTEXT *Context,
                                  ULONG Fence);
BOOLEAN AdmissionGpuvaG3CompleteJob(ADMISSION_CONTEXT *Adapter, ULONG Fence);

#endif
#endif
