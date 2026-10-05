#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)
#include "umd_internal.h"
#include "apple_agx_g3_copy_abi.h"
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
#include "render_qualification.h"
#endif

#ifdef APPLE_AGX_GPUVA_WINSYS

#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
static BOOL frame_process_is_dwm(void) {
  static volatile LONG cached = -1;
  LONG observed = InterlockedCompareExchange(&cached, 0, 0);
  if (observed < 0) {
    WCHAR executable[MAX_PATH];
    WCHAR *base = executable;
    DWORD length = GetModuleFileNameW(NULL, executable, ARRAYSIZE(executable));
    BOOL isDwm = FALSE;
    if (length != 0u && length < ARRAYSIZE(executable)) {
      for (DWORD index = 0u; index < length; ++index)
        if (executable[index] == L'\\' || executable[index] == L'/')
          base = &executable[index + 1u];
      isDwm = lstrcmpiW(base, L"dwm.exe") == 0;
    }
    observed = isDwm ? 1 : 0;
    InterlockedExchange(&cached, observed);
  }
  return observed == 1;
}

/* EXP974 measurement only. The diagnostic line carries pid/tid and QPC;
 * elapsed ticks use the reported frequency for offline per-submit joins. */
static void measure_g4_phase(UINT phase, LARGE_INTEGER start,
    UINT count, ULONGLONG bytes, HRESULT status) {
  LARGE_INTEGER end, frequency;
  UINT values[7];
  if (!frame_process_is_dwm()) return;
  (void)QueryPerformanceCounter(&end);
  (void)QueryPerformanceFrequency(&frequency);
  values[0]=phase;
  values[1]=(UINT)(end.QuadPart-start.QuadPart);
  values[2]=(UINT)((ULONGLONG)(end.QuadPart-start.QuadPart)>>32);
  values[3]=(UINT)frequency.QuadPart;
  values[4]=count;
  values[5]=(UINT)bytes;
  values[6]=(UINT)(bytes>>32);
  AdmissionUmdDiagnostic("measure-g4-phase",status,values,ARRAYSIZE(values));
}

#ifdef __cplusplus
extern "C"
#endif
ULONGLONG AdmissionUmdGpuvaFrameArm(ADMISSION_UMD_DEVICE *device,
    D3DKMT_HANDLE allocation, ULONGLONG canonicalVa) {
  ADMISSION_DWM_FRAME_ARM arm = {};
  D3DDDICB_ESCAPE request = {};
  HRESULT status;
  if (device == NULL || allocation == 0u || !frame_process_is_dwm())
    return canonicalVa;
  if (canonicalVa == 0ULL) {
    AcquireSRWLockShared(&device->ScreenBufferLock);
    for (UINT index = 0u; index < ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++index)
      if (device->ScreenBuffers[index].Active &&
          device->ScreenBuffers[index].KernelAllocation == allocation) {
        canonicalVa = device->ScreenBuffers[index].CanonicalGpuVa;
        break;
      }
    ReleaseSRWLockShared(&device->ScreenBufferLock);
  }
  if (device->DwmFrameArmAttempted &&
      device->DwmFrameLastAllocation == allocation &&
      device->DwmFrameLastVa == canonicalVa)
    return canonicalVa;
  if (device->DwmFrameArmAttempts >= 8u)
    return canonicalVa;
  ++device->DwmFrameArmAttempts;
  if (device->KernelCallbacks == NULL ||
      device->KernelCallbacks->pfnEscapeCb == NULL ||
      device->Adapter == NULL || !device->Adapter->RuntimeAdapter.handle ||
      !device->RuntimeDevice.handle || !device->KernelContext)
    return canonicalVa;
  arm.Magic = ADMISSION_DWM_FRAME_ARM_MAGIC;
  arm.Version = ADMISSION_DWM_FRAME_VERSION;
  arm.Bytes = sizeof(arm);
  arm.OsProcessId = GetCurrentProcessId();
  arm.Allocation = allocation;
  arm.CanonicalGpuVa = canonicalVa;
  request.hDevice = device->RuntimeDevice.handle;
  request.hContext = device->KernelContext;
  /* This receipt only updates KMD CPU bookkeeping. Level-two hardware
   * synchronization would drain the adapter on the DWM submission path. */
  request.Flags.HardwareAccess = 0;
  request.pPrivateDriverData = &arm;
  request.PrivateDriverDataSize = sizeof(arm);
  status = device->KernelCallbacks->pfnEscapeCb(
      device->Adapter->RuntimeAdapter.handle, &request);
  if (SUCCEEDED(status)) {
    device->DwmFrameArmAttempts = 0u;
    device->DwmFrameArmAttempted = TRUE;
    device->DwmFrameLastAllocation = allocation;
    device->DwmFrameLastVa = canonicalVa;
  }
  {
    UINT values[6] = {(UINT)allocation, (UINT)canonicalVa,
        (UINT)(canonicalVa >> 32), (UINT)(ULONG_PTR)device->KernelContext,
        (UINT)((ULONGLONG)(ULONG_PTR)device->KernelContext >> 32),
        (UINT)arm.OsProcessId};
    AdmissionUmdDiagnostic("measure-frame-arm", status, values, ARRAYSIZE(values));
  }
  return canonicalVa;
}
#endif

static ADMISSION_UMD_SCREEN_BUFFER *find_slot(ADMISSION_UMD_DEVICE *device,
                                             uint64_t token) {
  for (UINT i=0; i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++i)
    if (device->ScreenBuffers[i].Active && device->ScreenBuffers[i].Token==token)
      return &device->ScreenBuffers[i];
  return NULL;
}

static void release_copies(ADMISSION_UMD_DEVICE *device,
                           const uint64_t *tokens,unsigned count) {
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  for(unsigned i=0;i<count;++i) {
    auto *slot=find_slot(device,tokens[i]);
    if(slot && slot->CopyHeld) {slot->CopyHeld=FALSE;--slot->SubmissionHolds;}
  }
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
}

static D3DKMT_HANDLE allocation_handle(ADMISSION_UMD_DEVICE *device,
                                       uint64_t token) {
  D3DKMT_HANDLE handle = 0;
  if (!device || device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      !token || device->ScreenClosing) return 0;
  AcquireSRWLockShared(&device->ScreenBufferLock);
  for (UINT i = 0; i < ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++i) {
    ADMISSION_UMD_SCREEN_BUFFER *slot = &device->ScreenBuffers[i];
    if (slot->Active && !slot->Transition && slot->Token == token) {
      handle = slot->KernelAllocation;
      break;
    }
  }
  ReleaseSRWLockShared(&device->ScreenBufferLock);
  return handle;
}

static int reserve_va(void *context, uint64_t bytes, uint64_t minimum,
                      uint64_t maximum, uint64_t *va) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  D3DDDI_RESERVEGPUVIRTUALADDRESS request = {};
  if (!device || !va || !bytes || !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnReserveGpuVirtualAddressCb) return 0;
  request.hAdapter = 0;
  request.MinimumAddress = minimum;
  request.MaximumAddress = maximum;
  request.Size = bytes;
  HRESULT hr = device->KernelCallbacks->pfnReserveGpuVirtualAddressCb(
      device->RuntimeDevice.handle, &request);
  {
    UINT values[5] = {(UINT)(bytes >> 16), (UINT)(minimum >> 16),
                      (UINT)(maximum >> 32), request.hAdapter != 0u,
                      request.VirtualAddress != 0u};
    AdmissionUmdDiagnostic("g4-native-reserve-va-cb", hr, values,
                           ARRAYSIZE(values));
  }
  if (FAILED(hr) || !request.VirtualAddress) return 0;
  *va = request.VirtualAddress;
  return 1;
}

static int map_va(void *context, uint64_t token, uint64_t va,
                  uint64_t pages, unsigned protection, uint64_t *fence) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  D3DDDI_MAPGPUVIRTUALADDRESS request = {};
  if (!device || !fence || !device->PagingQueue ||
      !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnMapGpuVirtualAddressCb) return 0;
  request.hAllocation = allocation_handle(device, token);
  if (!request.hAllocation) return 0;
  request.hPagingQueue = device->PagingQueue;
  request.BaseAddress = va;
  request.OffsetInPages = 0;
  request.SizeInPages = pages;
  request.Protection.Write = (protection & AGX_GPUVA_MAP_WRITE) != 0;
  request.Protection.Execute = (protection & AGX_GPUVA_MAP_EXECUTE) != 0;
  HRESULT hr = device->KernelCallbacks->pfnMapGpuVirtualAddressCb(
      device->RuntimeDevice.handle, &request);
  {
    UINT values[5] = {request.hAllocation != 0u,
                      request.hPagingQueue != 0u, (UINT)pages,
                      request.VirtualAddress == va,
                      request.PagingFenceValue != 0u};
    AdmissionUmdDiagnostic("g4-native-map-va-cb", hr, values,
                           ARRAYSIZE(values));
  }
  if (FAILED(hr) && hr != E_PENDING) return 0;
  if (request.VirtualAddress != va) return 3;
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  auto *slot=find_slot(device,token);
  if(slot) slot->CanonicalGpuVa=va;
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  *fence = request.PagingFenceValue;
  return hr == E_PENDING ? 2 : 1;
}

static int free_va(void *context, uint64_t va, uint64_t bytes) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  D3DDDICB_FREEGPUVIRTUALADDRESS request = {};
  if (!device || !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnFreeGpuVirtualAddressCb) return 0;
  request.BaseAddress = va;
  request.Size = bytes;
  return SUCCEEDED(device->KernelCallbacks->pfnFreeGpuVirtualAddressCb(
      device->RuntimeDevice.handle, &request));
}

static D3DKMT_HANDLE *translate_handles(ADMISSION_UMD_DEVICE *device,
                                        const uint64_t *tokens, unsigned count) {
  if (!device || !tokens || !count)
    return NULL;
  D3DKMT_HANDLE *handles = (D3DKMT_HANDLE *)HeapAlloc(
      GetProcessHeap(), 0, (SIZE_T)count * sizeof(*handles));
  if (!handles) return NULL;
  for (unsigned i = 0; i < count; ++i) {
    handles[i] = allocation_handle(device, tokens[i]);
    if (!handles[i]) {
      HeapFree(GetProcessHeap(), 0, handles);
      return NULL;
    }
  }
  return handles;
}

static int evict(void *context, const uint64_t *tokens, unsigned count) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  if (!device || !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnEvictCb) return 0;
  D3DKMT_HANDLE *handles = translate_handles(device, tokens, count);
  if (!handles) return 0;
  D3DDDICB_EVICT request = {};
  request.NumAllocations = count;
  request.AllocationList = handles;
  HRESULT hr = device->KernelCallbacks->pfnEvictCb(
      device->RuntimeDevice.handle, &request);
  HeapFree(GetProcessHeap(), 0, handles);
  if(SUCCEEDED(hr)) release_copies(device,tokens,count);
  return SUCCEEDED(hr);
}

static int make_resident(void *context, const uint64_t *tokens,
                         unsigned count, uint64_t *fence) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  LARGE_INTEGER phase_start;
  (void)QueryPerformanceCounter(&phase_start);
#endif
  if (!device || !fence || !device->PagingQueue ||
      !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnMakeResidentCb) return 0;
  D3DKMT_HANDLE *handles = translate_handles(device, tokens, count);
  if (!handles) return 0;
  UINT *priorities = (UINT *)HeapAlloc(GetProcessHeap(), 0,
                                      (SIZE_T)count * sizeof(*priorities));
  if (!priorities) {
    HeapFree(GetProcessHeap(), 0, handles);
    return 0;
  }
  for (unsigned i = 0; i < count; ++i)
    priorities[i] = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  bool valid=!device->DrawTerminal && !device->ScreenClosing;
  for(UINT i=0;valid && i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i)
    if(device->ScreenBuffers[i].CopyHeld) valid=false;
  for(unsigned i=0;valid && i<count;++i) {
    auto *slot=find_slot(device,tokens[i]);
    valid=slot && !slot->Transition && !slot->CopyHeld &&
        !slot->SourceHolds && !slot->SubmissionHolds &&
        (slot->StagingAllocation || slot->Direct) && slot->CanonicalGpuVa &&
        (!slot->Mapped || (slot->NativeBo && slot->NativeMapRelease));
  }
  if(valid) for(unsigned i=0;i<count;++i) {
    auto *slot=find_slot(device,tokens[i]);slot->CopyHeld=TRUE;++slot->SubmissionHolds;
  }
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  if(!valid) {
    HeapFree(GetProcessHeap(),0,priorities);HeapFree(GetProcessHeap(),0,handles);
    return 0;
  }
  D3DDDI_MAKERESIDENT request = {};
  request.hPagingQueue = device->PagingQueue;
  request.NumAllocations = count;
  request.AllocationList = handles;
  request.PriorityList = priorities;
  HRESULT hr = device->KernelCallbacks->pfnMakeResidentCb(
      device->RuntimeDevice.handle, &request);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  measure_g4_phase(1u,phase_start,count,0u,hr);
#endif
  int rollback = 1;
  if (request.NumAllocations && request.NumAllocations < count)
    rollback = evict(context, tokens, request.NumAllocations);
  HeapFree(GetProcessHeap(), 0, priorities);
  HeapFree(GetProcessHeap(), 0, handles);
  if (!rollback) {
    device->DrawTerminal = TRUE;
    return 3;
  }
  if ((FAILED(hr) && hr != E_PENDING) || request.NumAllocations != count) {
    release_copies(device,tokens,count);return 0;
  }
  if(hr==E_PENDING && !request.PagingFenceValue) {
    device->DrawTerminal=TRUE;return 3;
  }
  *fence = request.PagingFenceValue;
  return hr == E_PENDING ? 2 : 1;
}

static int wait_object(ADMISSION_UMD_DEVICE *device,
                       D3DKMT_HANDLE object, uint64_t fence) {
  D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU request = {};
  if (!device || !object || !fence || !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnWaitForSynchronizationObjectFromCpuCb)
    return 0;
  request.ObjectCount = 1;
  request.ObjectHandleArray = &object;
  request.FenceValueArray = &fence;
  return SUCCEEDED(device->KernelCallbacks->pfnWaitForSynchronizationObjectFromCpuCb(
      device->RuntimeDevice.handle, &request));
}

static int wait_paging(void *context, uint64_t fence) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  LARGE_INTEGER phase_start;
  (void)QueryPerformanceCounter(&phase_start);
#endif
  int ok=wait_object(device, device ? device->PagingSyncObject : 0, fence);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  measure_g4_phase(2u,phase_start,1u,0u,ok ? S_OK : E_FAIL);
#endif
  return ok;
}

static int copy_escape(ADMISSION_UMD_DEVICE *device,
                        APPLE_AGX_G3_COPY_REQUEST *payload) {
  D3DDDICB_ESCAPE request={};
  if(!device->Adapter || device->Adapter->Magic!=ADMISSION_UMD_ADAPTER_MAGIC ||
     !device->Adapter->RuntimeAdapter.handle || !device->RuntimeDevice.handle ||
     !device->KernelContext || !device->KernelCallbacks->pfnEscapeCb) return 0;
  request.hDevice=device->RuntimeDevice.handle;
  request.hContext=device->KernelContext;
  /* This buffered ABI accesses host-mapped RAM and logical metadata. KMD
   * enforces allocation/job/paging safety itself; do not ask Windows to idle
   * the whole GPU before each 64-KiB CPU transfer. */
  request.Flags.Value=0;
  request.pPrivateDriverData=payload;request.PrivateDriverDataSize=sizeof(*payload);
  HRESULT status = device->KernelCallbacks->pfnEscapeCb(
      device->Adapter->RuntimeAdapter.handle,&request);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  if (FAILED(status) && frame_process_is_dwm()) {
    static volatile LONG failures;
    if (InterlockedIncrement(&failures) <= 16) {
      UINT values[7] = {(UINT)payload->Allocation, (UINT)payload->GpuVa,
          (UINT)(payload->GpuVa >> 32), payload->Operation,
          (UINT)(ULONG_PTR)device->KernelContext,
          (UINT)((ULONGLONG)(ULONG_PTR)device->KernelContext >> 32),
          (UINT)payload->TransferBytes};
      AdmissionUmdDiagnostic("reject-copy-escape", status, values,
                             ARRAYSIZE(values));
    }
  }
#endif
  return SUCCEEDED(status);
}

static int transfer_slot(ADMISSION_UMD_DEVICE *device,
                          ADMISSION_UMD_SCREEN_BUFFER *slot,bool download,
                          UINT *transfer_count,ULONGLONG *transfer_bytes) {
  auto *payload=(APPLE_AGX_G3_COPY_REQUEST *)HeapAlloc(
      GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(APPLE_AGX_G3_COPY_REQUEST));
  if(!payload) return 0;
  payload->Magic=APPLE_AGX_G3_COPY_MAGIC;payload->Version=APPLE_AGX_G3_COPY_VERSION;
  payload->Bytes=sizeof(*payload);payload->Allocation=slot->KernelAllocation;
  payload->GpuVa=slot->CanonicalGpuVa;payload->Operation=APPLE_AGX_G3_COPY_QUERY;
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  (void)AdmissionUmdGpuvaFrameArm(device, slot->KernelAllocation,
                                   slot->CanonicalGpuVa);
#endif
  UINT step=1u; /* EXP870 diagnostic: 1 query 2 lock 3 transfer 4 unmap 5 unlock */
  HRESULT lock_hr=S_OK;
  int success=copy_escape(device,payload) && payload->ProcessGeneration && payload->MappingGeneration;
  if(success) step=2u;
  BYTE *address=(BYTE *)slot->LockedBase;
  bool temporary=!slot->Mapped, locked=false;
  if(success && temporary) {
    D3DDDICB_LOCK lock={};lock.hAllocation=slot->StagingAllocation;
    lock.Flags.LockEntire=1;
    lock_hr=device->KernelCallbacks->pfnLockCb(
        device->RuntimeDevice.handle,&lock);
    locked=SUCCEEDED(lock_hr);
    if(locked) address=(BYTE *)lock.pData;
    success=locked && address && lock.hAllocation==slot->StagingAllocation;
    if(locked && lock.hAllocation!=slot->StagingAllocation) {
      /* No Discard was requested: renaming violates the callback contract. */
      device->DrawTerminal=TRUE;success=0;
    }
  }
  if(success && (!address || !payload->ProcessGeneration || !payload->MappingGeneration)) success=0;
  if(success) step=3u;
  if(success) for(uint64_t offset=0;offset<slot->Bytes;) {
    UINT count=(UINT)((slot->Bytes-offset)>APPLE_AGX_G3_COPY_CAPACITY ?
        APPLE_AGX_G3_COPY_CAPACITY : slot->Bytes-offset);
    payload->Operation=download ? APPLE_AGX_G3_COPY_DOWNLOAD : APPLE_AGX_G3_COPY_UPLOAD;
    payload->Offset=offset;payload->TransferBytes=count;
    if(!download) CopyMemory(payload->Data,address+offset,count);
    if(!copy_escape(device,payload)) {success=0;break;}
    if(download) CopyMemory(address+offset,payload->Data,count);
    if(transfer_count) ++*transfer_count;
    if(transfer_bytes) *transfer_bytes+=count;
    offset+=count;
  }
  /* Borrowed/imported storage must be unlocked before publication. Native
   * persistent maps can be uncached only through the native BO owner. */
  if(success) step=4u;
  bool uncache=slot->Borrowed && slot->Mapped;
  if(uncache && !slot->NativeMapRelease(slot->NativeBo,address,FALSE)) {
    device->DrawTerminal=TRUE;success=0;uncache=false;
  }
  if(locked || (address && uncache)) {
    D3DDDICB_UNLOCK unlock={};unlock.NumAllocations=1;
    unlock.phAllocations=&slot->StagingAllocation;
    if(FAILED(device->KernelCallbacks->pfnUnlockCb(device->RuntimeDevice.handle,&unlock))) {
      /* Keep the lock and both allocation owners; teardown is uncertain. */
      if(temporary) {
        AcquireSRWLockExclusive(&device->ScreenBufferLock);
        slot->LockedBase=address;slot->Mapped=TRUE;
        ReleaseSRWLockExclusive(&device->ScreenBufferLock);
      }
      device->DrawTerminal=TRUE;success=0;
    } else if(uncache) {
      if(!slot->NativeMapRelease(slot->NativeBo,address,TRUE)) {
        device->DrawTerminal=TRUE;success=0;
      }
      AcquireSRWLockExclusive(&device->ScreenBufferLock);
      slot->LockedBase=NULL;slot->Mapped=FALSE;slot->LockedAccess=0;
      ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    }
  }
  if(!success) {
    UINT values[9]={step,(UINT)download,(UINT)(slot->CanonicalGpuVa>>32),
        (UINT)slot->CanonicalGpuVa,(UINT)slot->Bytes,(UINT)lock_hr,
        (UINT)temporary | ((UINT)locked<<1) | ((UINT)(address!=NULL)<<2) |
        ((UINT)slot->Mapped<<3) | ((UINT)slot->Borrowed<<4),
        (UINT)payload->ProcessGeneration,(UINT)payload->MappingGeneration};
    AdmissionUmdDiagnostic("reject-copy-slot",E_FAIL,values,ARRAYSIZE(values));
  }
  HeapFree(GetProcessHeap(),0,payload);return success;
}

static int transfer_held(ADMISSION_UMD_DEVICE *device,bool download) {
  UINT measured_count=0u;
  ULONGLONG measured_bytes=0u;
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  LARGE_INTEGER phase_start;
  (void)QueryPerformanceCounter(&phase_start);
#endif
  if(!device->KernelCallbacks->pfnLockCb || !device->KernelCallbacks->pfnUnlockCb)
    return 0;
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i) {
    auto *slot=&device->ScreenBuffers[i];
    if(!slot->CopyHeld || slot->Direct ||
       (download && !(slot->Flags & AppleAgxWin32BufferGpuWrite))) continue;
    if(!transfer_slot(device,slot,download,&measured_count,&measured_bytes)) {
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
      measure_g4_phase(download ? 6u : 3u,phase_start,measured_count,
          measured_bytes,E_FAIL);
#endif
      return 0;
    }
  }
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  measure_g4_phase(download ? 6u : 3u,phase_start,measured_count,
      measured_bytes,S_OK);
#endif
  return 1;
}

static int signal_render(ADMISSION_UMD_DEVICE *device,uint64_t next) {
  D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 signal={};
  D3DKMT_HANDLE object=device->RenderSyncObject;
  HANDLE context_handle=device->KernelContext;
  signal.ObjectCount=1;signal.ObjectHandleArray=&object;
  signal.BroadcastContextCount=1;signal.BroadcastContextArray=&context_handle;
  signal.MonitoredFenceValueArray=&next;
  HRESULT result=device->KernelCallbacks->pfnSignalSynchronizationObjectFromGpu2Cb(
      device->RuntimeDevice.handle,&signal);
  AdmissionUmdDiagnostic("g4-signal-render-fence",result,NULL,0u);
  return SUCCEEDED(result);
}

static int submit(void *context, const uint64_t *written,
                  unsigned written_count, uint64_t va, uint32_t bytes,
                  const void *private_data, uint32_t private_bytes,
                  uint64_t *fence) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  D3DDDICB_SUBMITCOMMAND request = {};
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  D3DKMT_HANDLE trackedAllocation = 0u;
  ULONGLONG trackedVa = 0ULL;
#endif
  if (!device || !fence || !device->KernelContext ||
      !device->RenderSyncObject || !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnSubmitCommandCb ||
      !device->KernelCallbacks->pfnSignalSynchronizationObjectFromGpu2Cb ||
      device->NextRenderFence > UINT64_MAX - 2 ||
      written_count > D3DDDI_MAX_WRITTEN_PRIMARIES ||
      (written_count && !written)) return 0;
  AcquireSRWLockShared(&device->ScreenBufferLock);
  for (unsigned i = 0; i < written_count; ++i) {
    ADMISSION_UMD_SCREEN_BUFFER *slot = NULL;
    for (UINT j = 0; j < ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++j)
      if (device->ScreenBuffers[j].Active &&
          device->ScreenBuffers[j].Token == written[i]) {
        slot = &device->ScreenBuffers[j];
        break;
      }
    if (!slot || slot->Transition || !slot->CopyHeld || !slot->KernelAllocation) {
      ReleaseSRWLockShared(&device->ScreenBufferLock);
      return 0;
    }
    if (slot->Direct && slot->WrittenPrimary) {
      UINT index;
      for (index=0u; index<request.NumPrimaries; ++index)
        if (request.WrittenPrimaries[index]==slot->KernelAllocation) break;
      if (index==request.NumPrimaries) {
        if (request.NumPrimaries==D3DDDI_MAX_WRITTEN_PRIMARIES) {
          ReleaseSRWLockShared(&device->ScreenBufferLock);
          return 0;
        }
        request.WrittenPrimaries[request.NumPrimaries++]=slot->KernelAllocation;
      }
    }
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
    if (i == 0u) {
      trackedAllocation = slot->KernelAllocation;
      trackedVa = slot->CanonicalGpuVa;
    }
#endif
    /* Direct displayable BOs are GPU write targets. VidSch must know their
     * allocation handles before it schedules a flip of either surface. */
  }
  ReleaseSRWLockShared(&device->ScreenBufferLock);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  if (trackedAllocation != 0u)
    (void)AdmissionUmdGpuvaFrameArm(device, trackedAllocation, trackedVa);
#endif
  if(!transfer_held(device,false)) {
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
    device->FrameSubmitStatus = E_FAIL;
#endif
    return device->DrawTerminal ? 2 : 0;
  }
  request.Commands = va;
  request.CommandLength = bytes;
  request.BroadcastContextCount = 1;
  request.BroadcastContext[0] = device->KernelContext;
  request.pPrivateDriverData = (void *)private_data;
  request.PrivateDriverDataSize = private_bytes;
  request.RenderCBSequence = (UINT)InterlockedIncrement(&device->RenderCbSequence);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  LARGE_INTEGER submit_start;
  (void)QueryPerformanceCounter(&submit_start);
#endif
  HRESULT submit_result = device->KernelCallbacks->pfnSubmitCommandCb(
      device->RuntimeDevice.handle, &request);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  measure_g4_phase(4u,submit_start,request.RenderCBSequence,bytes,submit_result);
#endif
  UINT submit_values[4] = {request.CommandLength,
      request.PrivateDriverDataSize, request.NumPrimaries,
      request.RenderCBSequence};
  AdmissionUmdDiagnostic("g4-submit-command-cb", submit_result,
                         submit_values, ARRAYSIZE(submit_values));
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  if (frame_process_is_dwm()) {
    static volatile LONG observed;
    LONG ordinal = InterlockedIncrement(&observed);
    if (ordinal > 0 && ((ordinal & (ordinal - 1)) == 0)) {
      UINT primary[7] = {(UINT)ordinal, written_count,
          request.NumPrimaries,
          request.NumPrimaries ? request.WrittenPrimaries[0] : 0u,
          trackedAllocation, (UINT)submit_result,
          request.RenderCBSequence};
      AdmissionUmdDiagnostic("measure-written-submit", submit_result,
                             primary, ARRAYSIZE(primary));
    }
  }
  device->FrameSubmitStatus = submit_result;
#endif
  if (FAILED(submit_result)) return 0;
  uint64_t internal = device->NextRenderFence + 1;
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  device->FrameSubmittedFence = internal;
#endif
  int signaled=signal_render(device,internal);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  LARGE_INTEGER wait_start;
  (void)QueryPerformanceCounter(&wait_start);
#endif
  int waited=signaled && wait_object(device,device->RenderSyncObject,internal);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  measure_g4_phase(5u,wait_start,request.RenderCBSequence,0u,
      waited ? S_OK : E_FAIL);
#endif
  if(!waited || !transfer_held(device,true) ||
     !signal_render(device,internal+1)) {
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
    device->FrameSubmitStatus = E_FAIL;
#endif
    device->DrawTerminal=TRUE;return 2;
  }
  device->NextRenderFence=internal+1;
  *fence=internal+1;
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  device->FrameCompletedFence = internal;
  {
    static volatile LONG receipts;
    if (InterlockedIncrement(&receipts) <= 16) {
      UINT values[9] = {(UINT)(ULONG_PTR)device->KernelContext,
          (UINT)((ULONGLONG)(ULONG_PTR)device->KernelContext >> 32),
          (UINT)va, (UINT)(va >> 32), (UINT)internal,
          (UINT)(internal >> 32), (UINT)*fence, (UINT)(*fence >> 32),
          request.RenderCBSequence};
      AdmissionUmdDiagnostic("measure-render-fence", S_OK, values,
                             ARRAYSIZE(values));
    }
  }
#endif
  return 1;
}

static int wait_render(void *context, uint64_t fence) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  int waited = wait_object(device, device ? device->RenderSyncObject : 0, fence);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  if (waited && device != NULL && device->FrameCompletedFence < fence)
    device->FrameCompletedFence = fence;
#endif
  return waited;
}

static int private_escape(void *context, APPLE_AGX_G3_PRIVATE_REQUEST *payload) {
  ADMISSION_UMD_DEVICE *device=(ADMISSION_UMD_DEVICE *)context;
  D3DDDICB_ESCAPE request={};
  if(!device || device->Magic!=ADMISSION_UMD_DEVICE_MAGIC || !payload ||
     !device->Adapter || device->Adapter->Magic!=ADMISSION_UMD_ADAPTER_MAGIC ||
     !device->Adapter->RuntimeAdapter.handle || !device->RuntimeDevice.handle ||
     !device->KernelContext || !device->KernelCallbacks ||
     !device->KernelCallbacks->pfnEscapeCb || device->ScreenClosing) return 0;
  /* Unlike the other device callbacks, EscapeCb takes hRTAdapter. The
   * optional context must be paired with its owning hRTDevice in the request. */
  request.hDevice=device->RuntimeDevice.handle;
  /* The KMD owns scene lifetime. RELEASE may acknowledge a deferred free;
   * its reaper retains queued/submitting/leased storage until owner safety
   * and broker unmap/revoke are proven, without adapter-global GPU idle. */
  request.Flags.HardwareAccess=0u;
  request.hContext=device->KernelContext;
  request.pPrivateDriverData=payload;request.PrivateDriverDataSize=sizeof(*payload);
  UINT operation=payload->Operation,width=payload->Width,height=payload->Height,
      utile=(payload->UtileWidth<<8)|payload->UtileHeight,layers=payload->Layers,
      samples=payload->Samples;
  HRESULT hr=device->KernelCallbacks->pfnEscapeCb(
      device->Adapter->RuntimeAdapter.handle,&request);
  if(FAILED(hr)) {
    /* EXP873 diagnostic: which private scene request the KMD refused. */
    UINT values[7]={operation,(UINT)hr,width,height,utile,layers,samples};
    AdmissionUmdDiagnostic("reject-private-escape",hr,values,ARRAYSIZE(values));
  }
  return SUCCEEDED(hr);
}

#ifdef __cplusplus
extern "C"
#endif
const AGX_WIN32_GPUVA_OPS *AdmissionUmdGpuvaOperations(void) {
  static const AGX_WIN32_GPUVA_OPS operations = {
      reserve_va, map_va, free_va, make_resident, wait_paging,
      submit, wait_render, evict, private_escape};
  return &operations;
}
#endif
