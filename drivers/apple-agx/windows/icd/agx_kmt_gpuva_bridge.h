#ifndef AGX_KMT_GPUVA_BRIDGE_H
#define AGX_KMT_GPUVA_BRIDGE_H

/* CS 1.6 ICD plan, phase 1: the D3D runtime callbacks the G4/GPUVA umd_*
 * code calls, implemented on the D3DKMT thunks, so an OpenGL ICD (no D3D
 * runtime) drives the validated staging, residency and submit code
 * unchanged. Runtime handles passed to the callbacks are the bridge itself;
 * kernel context handles travel as HANDLE values holding a D3DKMT_HANDLE. */

#include <windows.h>
/* NTSTATUS: Mesa builds define WIN32_LEAN_AND_MEAN, which skips wincrypt.h
 * and with it the user-mode NTSTATUS typedef of bcrypt.h. */
#include <bcrypt.h>
#include <d3dkmthk.h>
#pragma warning(push)
#pragma warning(disable:4201)
#include <d3d10umddi.h>
#pragma warning(pop)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _AGX_KMT_THUNKS {
  PFND3DKMT_QUERYADAPTERINFO QueryAdapterInfo;
  PFND3DKMT_CREATECONTEXTVIRTUAL CreateContextVirtual;
  PFND3DKMT_DESTROYCONTEXT DestroyContext;
  PFND3DKMT_CREATEPAGINGQUEUE CreatePagingQueue;
  PFND3DKMT_DESTROYPAGINGQUEUE DestroyPagingQueue;
  PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 CreateSynchronizationObject2;
  PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySynchronizationObject;
  PFND3DKMT_CREATEALLOCATION CreateAllocation2;
  PFND3DKMT_DESTROYALLOCATION2 DestroyAllocation2;
  PFND3DKMT_RESERVEGPUVIRTUALADDRESS ReserveGpuVirtualAddress;
  PFND3DKMT_MAPGPUVIRTUALADDRESS MapGpuVirtualAddress;
  PFND3DKMT_FREEGPUVIRTUALADDRESS FreeGpuVirtualAddress;
  PFND3DKMT_MAKERESIDENT MakeResident;
  PFND3DKMT_EVICT Evict;
  PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU WaitForSynchronizationObjectFromCpu;
  PFND3DKMT_SUBMITCOMMAND SubmitCommand;
  PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 SignalSynchronizationObjectFromGpu2;
  PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 SignalSynchronizationObject2;
  PFND3DKMT_LOCK2 Lock2;
  PFND3DKMT_UNLOCK2 Unlock2;
  PFND3DKMT_ESCAPE Escape;
  PFND3DKMT_SETALLOCATIONPRIORITY SetAllocationPriority;
  PFND3DKMT_QUERYALLOCATIONRESIDENCY QueryAllocationResidency;
} AGX_KMT_THUNKS;

typedef enum _AGX_KMT_GPUVA_OP {
  AgxKmtGpuvaNone, AgxKmtGpuvaQueryAdapter, AgxKmtGpuvaCreateContext,
  AgxKmtGpuvaDestroyContext, AgxKmtGpuvaCreatePagingQueue,
  AgxKmtGpuvaDestroyPagingQueue, AgxKmtGpuvaCreateSync,
  AgxKmtGpuvaDestroySync, AgxKmtGpuvaAllocate, AgxKmtGpuvaDeallocate,
  AgxKmtGpuvaReserve, AgxKmtGpuvaMap, AgxKmtGpuvaFree,
  AgxKmtGpuvaMakeResident, AgxKmtGpuvaEvict, AgxKmtGpuvaWaitCpu,
  AgxKmtGpuvaSubmit, AgxKmtGpuvaSignalGpu2, AgxKmtGpuvaSignal2,
  AgxKmtGpuvaLock, AgxKmtGpuvaUnlock, AgxKmtGpuvaEscape,
  AgxKmtGpuvaSetPriority, AgxKmtGpuvaQueryResidency, AgxKmtGpuvaSetError,
  AgxKmtGpuvaOpCount
} AGX_KMT_GPUVA_OP;

/* EXP1189 receipt-only: where the CPU waits for the GPU. Per distinct chain
 * of the first return addresses into this image found on the stack above a
 * fence wait (its callers), the waits and their QPC ticks. */
#define AGX_KMT_WAIT_SITES 8u
#define AGX_KMT_WAIT_DEPTH 8u  /* EXP1192: 4 stopped at agx_sync_all */
#define AGX_KMT_WAIT_FOREIGN 3u
typedef struct _AGX_KMT_WAIT_SITE {
  ULONG_PTR Return[AGX_KMT_WAIT_DEPTH];
  /* EXP1193: the first return addresses into other modules above the
   * chain (who called into the ICD), from the site's first wait. */
  ULONG_PTR Foreign[AGX_KMT_WAIT_FOREIGN];
  UINT Calls;
  LONGLONG Ticks;
} AGX_KMT_WAIT_SITE;

typedef struct _AGX_KMT_GPUVA_RECEIPT {
  UINT Calls[AgxKmtGpuvaOpCount];
  UINT Failures[AgxKmtGpuvaOpCount];
  AGX_KMT_GPUVA_OP LastFailedOp;
  NTSTATUS LastFailedStatus;
  HRESULT LastRuntimeError;
  UINT RuntimeErrors;
  /* EXP1173: QueryPerformanceCounter ticks spent in each op's thunk. */
  LONGLONG Ticks[AgxKmtGpuvaOpCount];
  LONGLONG Begin;
  /* EXP1189: set by the owner when the UMD trace is enabled. */
  BOOL ScanWaits;
  AGX_KMT_WAIT_SITE WaitSites[AGX_KMT_WAIT_SITES];
  UINT WaitSitesLost;
} AGX_KMT_GPUVA_RECEIPT;

typedef struct _AGX_KMT_GPUVA_BRIDGE {
  UINT Magic;
  AGX_KMT_THUNKS Kmt;
  D3DKMT_HANDLE AdapterHandle, DeviceHandle;
  D3DDDI_ADAPTERCALLBACKS AdapterCallbacks;
  D3DDDI_DEVICECALLBACKS DeviceCallbacks;
  D3D11DDI_CORELAYER_DEVICECALLBACKS CoreCallbacks;
  /* Null-filled: GL presents through the ICD, never through DXGI. */
  DXGI_DDI_BASE_CALLBACKS DxgiCallbacks;
  AGX_KMT_GPUVA_RECEIPT Receipt;
} AGX_KMT_GPUVA_BRIDGE;

/* The D3DKMT exports of gdi32.dll; FALSE if any is missing. */
BOOL AgxKmtThunksLoad(AGX_KMT_THUNKS *Thunks);

/* Fill the callback tables over the given adapter/device handles. The
 * bridge address is the runtime adapter, device and core-layer handle. */
HRESULT AgxKmtGpuvaBridgeInitialize(AGX_KMT_GPUVA_BRIDGE *Bridge,
    const AGX_KMT_THUNKS *Thunks, D3DKMT_HANDLE Adapter, D3DKMT_HANDLE Device);

/* NTSTATUS of a thunk -> HRESULT the umd_* code expects from a callback. */
HRESULT AgxKmtGpuvaResult(NTSTATUS Status);

#ifdef __cplusplus
}
#endif

#endif
