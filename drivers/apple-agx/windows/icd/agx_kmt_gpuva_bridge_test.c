/* Fake-thunk test of the GPUVA D3DKMT bridge, built for x64 and x86 against
 * the pinned WDK (build-icd-bridge-test.ps1). Each callback must forward the
 * device/adapter/context handles and fields the umd_* code passes to the
 * matching D3DKMT thunk, return its outputs, and map STATUS_PENDING to the
 * E_PENDING the paging waits depend on. */
#include "agx_kmt_gpuva_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define ADAPTER 0x40000001u
#define DEVICE 0x40000002u
#define CONTEXT 0x40000003u
#define PAGING 0x40000004u
#define FENCE 0x40000005u

static NTSTATUS next_status;
static unsigned calls;
static D3DKMT_QUERYADAPTERINFO q_query;
static D3DKMT_CREATECONTEXTVIRTUAL q_context;
static D3DKMT_DESTROYCONTEXT q_destroy_context;
static D3DKMT_CREATEPAGINGQUEUE q_paging;
static D3DKMT_CREATESYNCHRONIZATIONOBJECT2 q_sync;
static D3DKMT_CREATEALLOCATION q_alloc; static D3DDDI_ALLOCATIONINFO2 q_alloc_info[2];
static D3DKMT_DESTROYALLOCATION2 q_dealloc;
static D3DDDI_RESERVEGPUVIRTUALADDRESS q_reserve;
static D3DDDI_MAPGPUVIRTUALADDRESS q_map;
static D3DKMT_FREEGPUVIRTUALADDRESS q_free;
static D3DDDI_MAKERESIDENT q_resident;
static D3DKMT_EVICT q_evict;
static D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU q_wait;
static D3DKMT_SUBMITCOMMAND q_submit;
static D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 q_signal_gpu; static D3DKMT_HANDLE q_signal_gpu_ctx;
static D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 q_signal;
static D3DKMT_LOCK2 q_lock; static unsigned unlocks;
static D3DKMT_ESCAPE q_escape;

static NTSTATUS APIENTRY f_query(const D3DKMT_QUERYADAPTERINFO *a) { ++calls; q_query = *a; return next_status; }
static NTSTATUS APIENTRY f_context(D3DKMT_CREATECONTEXTVIRTUAL *a) { ++calls; q_context = *a; a->hContext = CONTEXT; return next_status; }
static NTSTATUS APIENTRY f_destroy_context(const D3DKMT_DESTROYCONTEXT *a) { ++calls; q_destroy_context = *a; return next_status; }
static NTSTATUS APIENTRY f_paging(D3DKMT_CREATEPAGINGQUEUE *a) { ++calls; q_paging = *a; a->hPagingQueue = PAGING; a->hSyncObject = FENCE; a->FenceValueCPUVirtualAddress = (void *)&q_paging; return next_status; }
static NTSTATUS APIENTRY f_destroy_paging(D3DDDI_DESTROYPAGINGQUEUE *a) { ++calls; return a->hPagingQueue == PAGING ? next_status : (NTSTATUS)0xc000000dL; }
static NTSTATUS APIENTRY f_sync(D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *a) { ++calls; q_sync = *a; a->hSyncObject = FENCE; a->Info.MonitoredFence.FenceValueGPUVirtualAddress = 0x123000; return next_status; }
static NTSTATUS APIENTRY f_destroy_sync(const D3DKMT_DESTROYSYNCHRONIZATIONOBJECT *a) { ++calls; return a->hSyncObject == FENCE ? next_status : (NTSTATUS)0xc000000dL; }
static NTSTATUS APIENTRY f_alloc(D3DKMT_CREATEALLOCATION *a) { ++calls; q_alloc = *a; memcpy(q_alloc_info, a->pAllocationInfo2, sizeof(q_alloc_info[0]) * a->NumAllocations); for (UINT i = 0; i < a->NumAllocations; ++i) a->pAllocationInfo2[i].hAllocation = 0x80000000u + i; return next_status; }
static NTSTATUS APIENTRY f_dealloc(const D3DKMT_DESTROYALLOCATION2 *a) { ++calls; q_dealloc = *a; return next_status; }
static NTSTATUS APIENTRY f_reserve(D3DDDI_RESERVEGPUVIRTUALADDRESS *a) { ++calls; q_reserve = *a; a->VirtualAddress = 0x4000000; return next_status; }
static NTSTATUS APIENTRY f_map(D3DDDI_MAPGPUVIRTUALADDRESS *a) { ++calls; q_map = *a; a->VirtualAddress = a->BaseAddress; a->PagingFenceValue = 77; return next_status; }
static NTSTATUS APIENTRY f_free(const D3DKMT_FREEGPUVIRTUALADDRESS *a) { ++calls; q_free = *a; return next_status; }
static NTSTATUS APIENTRY f_resident(D3DDDI_MAKERESIDENT *a) { ++calls; q_resident = *a; a->PagingFenceValue = 99; return next_status; }
static NTSTATUS APIENTRY f_evict(D3DKMT_EVICT *a) { ++calls; q_evict = *a; a->NumBytesToTrim = 4096; return next_status; }
static NTSTATUS APIENTRY f_wait(const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *a) { ++calls; q_wait = *a; return next_status; }
static NTSTATUS APIENTRY f_submit(const D3DKMT_SUBMITCOMMAND *a) { ++calls; q_submit = *a; return next_status; }
static NTSTATUS APIENTRY f_signal_gpu(const D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 *a) { ++calls; q_signal_gpu = *a; q_signal_gpu_ctx = a->BroadcastContextArray[0]; return next_status; }
static NTSTATUS APIENTRY f_signal(const D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 *a) { ++calls; q_signal = *a; return next_status; }
static NTSTATUS APIENTRY f_lock(D3DKMT_LOCK2 *a) { ++calls; q_lock = *a; a->pData = (void *)&q_lock; return next_status; }
static NTSTATUS APIENTRY f_unlock(const D3DKMT_UNLOCK2 *a) { ++calls; ++unlocks; return a->hDevice == DEVICE ? next_status : (NTSTATUS)0xc000000dL; }
static NTSTATUS APIENTRY f_escape(const D3DKMT_ESCAPE *a) { ++calls; q_escape = *a; return next_status; }
static NTSTATUS APIENTRY f_priority(const D3DKMT_SETALLOCATIONPRIORITY *a) { ++calls; return a->hDevice == DEVICE ? next_status : (NTSTATUS)0xc000000dL; }
static NTSTATUS APIENTRY f_residency(const D3DKMT_QUERYALLOCATIONRESIDENCY *a) { ++calls; return a->hDevice == DEVICE ? next_status : (NTSTATUS)0xc000000dL; }

int main(void) {
  static AGX_KMT_GPUVA_BRIDGE bridge;
  AGX_KMT_THUNKS t = {f_query, f_context, f_destroy_context, f_paging, f_destroy_paging,
      f_sync, f_destroy_sync, f_alloc, f_dealloc, f_reserve, f_map, f_free, f_resident,
      f_evict, f_wait, f_submit, f_signal_gpu, f_signal, f_lock, f_unlock, f_escape,
      f_priority, f_residency};
  const D3DDDI_DEVICECALLBACKS *d;
  HANDLE h = &bridge;
  BYTE blob[104];
  printf("pointer bytes %u\n", (unsigned)sizeof(void *));
  CHECK(AgxKmtGpuvaBridgeInitialize(&bridge, &t, ADAPTER, DEVICE) == S_OK);
  d = &bridge.DeviceCallbacks;

  /* Every callback the umd_* device check requires is present. */
  CHECK(d->pfnCreateContextVirtualCb && d->pfnCreateSynchronizationObject2Cb &&
        d->pfnDestroySynchronizationObjectCb && d->pfnReserveGpuVirtualAddressCb &&
        d->pfnMapGpuVirtualAddressCb && d->pfnFreeGpuVirtualAddressCb &&
        d->pfnSubmitCommandCb && d->pfnSignalSynchronizationObjectFromGpu2Cb &&
        d->pfnDestroyContextCb && d->pfnAllocateCb && d->pfnDeallocateCb &&
        d->pfnLockCb && d->pfnUnlockCb && d->pfnSetPriorityCb && d->pfnQueryResidencyCb &&
        d->pfnSignalSynchronizationObject2Cb && d->pfnMakeResidentCb && d->pfnEvictCb &&
        d->pfnWaitForSynchronizationObjectFromCpuCb && d->pfnCreatePagingQueueCb &&
        d->pfnDestroyPagingQueueCb && d->pfnEscapeCb &&
        bridge.AdapterCallbacks.pfnQueryAdapterInfoCb && bridge.CoreCallbacks.pfnSetErrorCb);

  { D3DDDICB_QUERYADAPTERINFO a = {blob, sizeof(blob)};
    CHECK(bridge.AdapterCallbacks.pfnQueryAdapterInfoCb(h, &a) == S_OK);
    CHECK(q_query.hAdapter == ADAPTER && q_query.Type == KMTQAITYPE_UMDRIVERPRIVATE &&
          q_query.pPrivateDriverData == blob && q_query.PrivateDriverDataSize == sizeof(blob)); }

  { D3DDDICB_CREATECONTEXTVIRTUAL a; ZeroMemory(&a, sizeof(a));
    a.EngineAffinity = 1; a.pPrivateDriverData = blob; a.PrivateDriverDataSize = 16;
    CHECK(d->pfnCreateContextVirtualCb(h, &a) == S_OK);
    CHECK(q_context.hDevice == DEVICE && q_context.EngineAffinity == 1 &&
          q_context.pPrivateDriverData == blob && q_context.PrivateDriverDataSize == 16 &&
          q_context.ClientHint == D3DKMT_CLIENTHINT_OPENGL);
    CHECK(a.hContext == (HANDLE)(UINT_PTR)CONTEXT); }

  { D3DDDICB_DESTROYCONTEXT a = {(HANDLE)(UINT_PTR)CONTEXT};
    CHECK(d->pfnDestroyContextCb(h, &a) == S_OK && q_destroy_context.hContext == CONTEXT); }

  { D3DDDICB_CREATEPAGINGQUEUE a; ZeroMemory(&a, sizeof(a));
    a.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    CHECK(d->pfnCreatePagingQueueCb(h, &a) == S_OK);
    CHECK(q_paging.hDevice == DEVICE && q_paging.Priority == D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL);
    CHECK(a.hPagingQueue == PAGING && a.hSyncObject == FENCE &&
          a.FenceValueCPUVirtualAddress == (void *)&q_paging);
    { D3DDDI_DESTROYPAGINGQUEUE z = {PAGING}; CHECK(d->pfnDestroyPagingQueueCb(h, &z) == S_OK); } }

  { D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 a; ZeroMemory(&a, sizeof(a));
    a.Info.Type = D3DDDI_MONITORED_FENCE;
    CHECK(d->pfnCreateSynchronizationObject2Cb(h, &a) == S_OK);
    CHECK(q_sync.hDevice == DEVICE && q_sync.Info.Type == D3DDDI_MONITORED_FENCE);
    CHECK(a.hSyncObject == FENCE && a.Info.MonitoredFence.FenceValueGPUVirtualAddress == 0x123000);
    { D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT z = {FENCE}; CHECK(d->pfnDestroySynchronizationObjectCb(h, &z) == S_OK); } }

  { D3DDDI_ALLOCATIONINFO info[2]; D3DDDICB_ALLOCATE a; BYTE res[8], p0[48], p1[48];
    ZeroMemory(info, sizeof(info)); ZeroMemory(&a, sizeof(a));
    info[0].pPrivateDriverData = p0; info[0].PrivateDriverDataSize = 48;
    info[1].pPrivateDriverData = p1; info[1].PrivateDriverDataSize = 48; info[1].Flags.Primary = 1;
    a.pPrivateDriverData = res; a.PrivateDriverDataSize = 8; a.NumAllocations = 2; a.pAllocationInfo = info;
    CHECK(d->pfnAllocateCb(h, &a) == S_OK);
    CHECK(q_alloc.hDevice == DEVICE && q_alloc.NumAllocations == 2 &&
          q_alloc.pPrivateDriverData == res && q_alloc.PrivateDriverDataSize == 8 && !q_alloc.Flags.CreateResource);
    CHECK(q_alloc_info[0].pPrivateDriverData == p0 && q_alloc_info[1].PrivateDriverDataSize == 48 &&
          q_alloc_info[1].Flags.Primary && !q_alloc_info[0].Flags.Primary);
    CHECK(info[0].hAllocation == 0x80000000u && info[1].hAllocation == 0x80000001u);
    /* A runtime resource does not exist in an ICD. */
    a.hResource = (HANDLE)1; calls = 0;
    CHECK(d->pfnAllocateCb(h, &a) == E_INVALIDARG && calls == 0); }

  { D3DKMT_HANDLE list[2] = {0x80000000u, 0x80000001u}; D3DDDICB_DEALLOCATE a = {NULL, 2, list};
    CHECK(d->pfnDeallocateCb(h, &a) == S_OK);
    CHECK(q_dealloc.hDevice == DEVICE && q_dealloc.AllocationCount == 2 && q_dealloc.phAllocationList == list); }

  { D3DDDI_RESERVEGPUVIRTUALADDRESS a; ZeroMemory(&a, sizeof(a));
    a.Size = 0x10000; a.MinimumAddress = 0x1000000; a.MaximumAddress = 0x8000000000ull;
    CHECK(d->pfnReserveGpuVirtualAddressCb(h, &a) == S_OK);
    CHECK(q_reserve.hAdapter == ADAPTER && q_reserve.Size == 0x10000 && q_reserve.MaximumAddress == 0x8000000000ull);
    CHECK(a.VirtualAddress == 0x4000000); }

  { D3DDDI_MAPGPUVIRTUALADDRESS a; ZeroMemory(&a, sizeof(a));
    a.hPagingQueue = PAGING; a.hAllocation = 0x80000000u; a.BaseAddress = 0x4000000; a.SizeInPages = 4;
    next_status = (NTSTATUS)0x00000103L;
    CHECK(d->pfnMapGpuVirtualAddressCb(h, &a) == E_PENDING);
    next_status = 0;
    CHECK(q_map.hPagingQueue == PAGING && q_map.SizeInPages == 4 && a.PagingFenceValue == 77 &&
          a.VirtualAddress == 0x4000000); }

  { D3DDDICB_FREEGPUVIRTUALADDRESS a = {0x4000000, 0x10000};
    CHECK(d->pfnFreeGpuVirtualAddressCb(h, &a) == S_OK);
    CHECK(q_free.hAdapter == ADAPTER && q_free.BaseAddress == 0x4000000 && q_free.Size == 0x10000); }

  { D3DKMT_HANDLE list[1] = {0x80000000u}; UINT prio = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
    D3DDDI_MAKERESIDENT a; ZeroMemory(&a, sizeof(a));
    a.hPagingQueue = PAGING; a.NumAllocations = 1; a.AllocationList = list; a.PriorityList = &prio;
    next_status = (NTSTATUS)0x00000103L;
    CHECK(d->pfnMakeResidentCb(h, &a) == E_PENDING && a.PagingFenceValue == 99);
    next_status = 0;
    CHECK(q_resident.hPagingQueue == PAGING && q_resident.AllocationList == list); }

  { D3DKMT_HANDLE list[1] = {0x80000000u}; D3DDDICB_EVICT a; ZeroMemory(&a, sizeof(a));
    a.NumAllocations = 1; a.AllocationList = list; a.Flags.EvictOnlyIfNecessary = 1;
    CHECK(d->pfnEvictCb(h, &a) == S_OK);
    CHECK(q_evict.hDevice == DEVICE && q_evict.Flags.EvictOnlyIfNecessary && a.NumBytesToTrim == 4096); }

  { D3DKMT_HANDLE objs[1] = {FENCE}; UINT64 values[1] = {5};
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU a; ZeroMemory(&a, sizeof(a));
    a.ObjectCount = 1; a.ObjectHandleArray = objs; a.FenceValueArray = values;
    CHECK(d->pfnWaitForSynchronizationObjectFromCpuCb(h, &a) == S_OK);
    CHECK(q_wait.hDevice == DEVICE && q_wait.ObjectHandleArray == objs && q_wait.FenceValueArray == values); }

  { D3DDDICB_SUBMITCOMMAND a; ZeroMemory(&a, sizeof(a));
    a.Commands = 0x5000000; a.CommandLength = 256; a.BroadcastContextCount = 1;
    a.BroadcastContext[0] = (HANDLE)(UINT_PTR)CONTEXT; a.pPrivateDriverData = blob; a.PrivateDriverDataSize = 64;
    CHECK(d->pfnSubmitCommandCb(h, &a) == S_OK);
    CHECK(q_submit.Commands == 0x5000000 && q_submit.CommandLength == 256 &&
          q_submit.BroadcastContextCount == 1 && q_submit.BroadcastContext[0] == CONTEXT &&
          q_submit.pPrivateDriverData == blob && q_submit.PrivateDriverDataSize == 64 &&
          !q_submit.NumPrimaries && !q_submit.NumHistoryBuffers);
#ifdef _WIN64
    /* A context handle wider than D3DKMT_HANDLE is refused before the thunk. */
    a.BroadcastContext[0] = (HANDLE)(UINT_PTR)0x100000000ull; calls = 0;
    CHECK(d->pfnSubmitCommandCb(h, &a) == E_INVALIDARG && calls == 0);
#endif
  }

  { D3DKMT_HANDLE objs[1] = {FENCE}; HANDLE ctx[1] = {(HANDLE)(UINT_PTR)CONTEXT};
    D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 a; ZeroMemory(&a, sizeof(a));
    a.ObjectCount = 1; a.ObjectHandleArray = objs; a.BroadcastContextCount = 1;
    a.BroadcastContextArray = ctx; a.FenceValue = 42;
    CHECK(d->pfnSignalSynchronizationObjectFromGpu2Cb(h, &a) == S_OK);
    CHECK(q_signal_gpu.ObjectCount == 1 && q_signal_gpu.ObjectHandleArray == objs &&
          q_signal_gpu_ctx == CONTEXT && q_signal_gpu.FenceValue == 42); }

  { D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT2 a; HANDLE event = (HANDLE)(UINT_PTR)0x1234;
    ZeroMemory(&a, sizeof(a));
    a.hContext = (HANDLE)(UINT_PTR)CONTEXT; a.ObjectCount = 1; a.ObjectHandleArray[0] = FENCE;
    a.Flags.EnqueueCpuEvent = 1; a.CpuEventHandle = event;
    CHECK(d->pfnSignalSynchronizationObject2Cb(h, &a) == S_OK);
    CHECK(q_signal.hContext == CONTEXT && q_signal.ObjectHandleArray[0] == FENCE &&
          q_signal.Flags.EnqueueCpuEvent && q_signal.CpuEventHandle == event); }

  { D3DDDICB_LOCK a; ZeroMemory(&a, sizeof(a));
    a.hAllocation = 0x80000000u; a.Flags.LockEntire = 1; a.Flags.ReadOnly = 1;
    CHECK(d->pfnLockCb(h, &a) == S_OK);
    CHECK(q_lock.hDevice == DEVICE && q_lock.hAllocation == 0x80000000u && a.pData == (void *)&q_lock);
    /* Lock2 has no Discard/NoOverwrite/partial-page semantics. */
    a.Flags.Discard = 1; calls = 0;
    CHECK(d->pfnLockCb(h, &a) == E_INVALIDARG && calls == 0); }

  { D3DKMT_HANDLE list[2] = {0x80000000u, 0x80000001u}; D3DDDICB_UNLOCK a = {2, list};
    CHECK(d->pfnUnlockCb(h, &a) == S_OK && unlocks == 2); }

  { D3DDDICB_ESCAPE a; ZeroMemory(&a, sizeof(a));
    a.hDevice = h; a.hContext = (HANDLE)(UINT_PTR)CONTEXT; a.pPrivateDriverData = blob; a.PrivateDriverDataSize = 32;
    CHECK(d->pfnEscapeCb(h, &a) == S_OK);
    CHECK(q_escape.hAdapter == ADAPTER && q_escape.hDevice == DEVICE && q_escape.hContext == CONTEXT &&
          q_escape.Type == D3DKMT_ESCAPE_DRIVERPRIVATE && q_escape.pPrivateDriverData == blob &&
          q_escape.PrivateDriverDataSize == 32); }

  { D3DKMT_HANDLE list[1] = {0x80000000u}; UINT prio[1] = {1};
    D3DDDICB_SETPRIORITY a = {NULL, 1, list, prio}; D3DDDI_RESIDENCYSTATUS status[1];
    D3DDDICB_QUERYRESIDENCY r = {NULL, 1, list, status};
    CHECK(d->pfnSetPriorityCb(h, &a) == S_OK && d->pfnQueryResidencyCb(h, &r) == S_OK); }

  /* Failures are counted and mapped; a foreign handle is refused. */
  next_status = (NTSTATUS)0xc00002b6L;
  { D3DDDICB_ESCAPE a; ZeroMemory(&a, sizeof(a)); a.pPrivateDriverData = blob; a.PrivateDriverDataSize = 32;
    CHECK(d->pfnEscapeCb(h, &a) == D3DDDIERR_DEVICEREMOVED);
    CHECK(bridge.Receipt.Failures[AgxKmtGpuvaEscape] == 1 &&
          bridge.Receipt.LastFailedOp == AgxKmtGpuvaEscape &&
          bridge.Receipt.LastFailedStatus == (NTSTATUS)0xc00002b6L);
    CHECK(d->pfnEscapeCb((HANDLE)blob, &a) == E_INVALIDARG); }
  next_status = 0;
  { D3D10DDI_HRTCORELAYER core; core.handle = h;
    bridge.CoreCallbacks.pfnSetErrorCb(core, E_OUTOFMEMORY);
    CHECK(bridge.Receipt.RuntimeErrors == 1 && bridge.Receipt.LastRuntimeError == E_OUTOFMEMORY); }
  CHECK(AgxKmtGpuvaResult(0) == S_OK && AgxKmtGpuvaResult((NTSTATUS)0x103L) == E_PENDING &&
        AgxKmtGpuvaResult((NTSTATUS)0xc0000017L) == E_OUTOFMEMORY &&
        AgxKmtGpuvaResult((NTSTATUS)0xc000000dL) == E_INVALIDARG);
  printf("agx_kmt_gpuva_bridge_test: PASS\n");
  return 0;
}
