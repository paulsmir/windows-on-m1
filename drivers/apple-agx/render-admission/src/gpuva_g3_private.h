#ifndef ADMISSION_GPUVA_G3_PRIVATE_H
#define ADMISSION_GPUVA_G3_PRIVATE_H

#include "render_admission.h"

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
#define ADMISSION_G3_PROCESS_MAGIC 0x47335052u

typedef struct _ADMISSION_G3_STATE {
  ADMISSION_CONTEXT *Adapter;
  FAST_MUTEX Lock;
  LIST_ENTRY Processes;
  APPLE_AGX_GPUVA_V5_CLIENT Client;
  ULONGLONG NextProcessId;
  ULONG ProcessCount;
  struct _ADMISSION_G3_PROCESS *ActiveProcess;
  ULONG ActiveFence;
  ULONG LastCompletedFence;
  ULONGLONG UnpublishedGroups[32];
} ADMISSION_G3_STATE;

typedef struct _ADMISSION_G3_TABLE_SHADOW {
  struct _ADMISSION_G3_TABLE_SHADOW *Next;
  APPLE_AGX_MEMORY_OBJECT Memory;
  ULONGLONG OriginalIpa, BrokerIpa;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *LogicalPtes;
} ADMISSION_G3_TABLE_SHADOW;

typedef struct _ADMISSION_G3_PROCESS {
  LIST_ENTRY Link;
  ADMISSION_G3_STATE *State;
  APPLE_AGX_GPUVA_G3_GRAPH Graph;
  APPLE_AGX_MEMORY_IO Io;
  APPLE_AGX_MEMORY_OBJECT BootstrapRoot;
  ADMISSION_G3_TABLE_SHADOW *TableShadows;
  ULONGLONG BootstrapIpa;
  ULONG Magic, DeviceRefs, ContextRefs;
  BOOLEAN Poisoned;
} ADMISSION_G3_PROCESS;

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
