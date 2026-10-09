#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)
#include "umd_internal.h"
#include "apple_agx_g3_copy_abi.h"
#include "apple_agx_g4_submit.h"
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
    for (UINT index = 0u; index < ADMISSION_UMD_SCREEN_BUFFER_SCAN(device); ++index)
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


/* EXP981 receipt-only: process-wide ring of GPU VA lifecycle operations
 * (all devices of this process share one GPU VA space). A failed copy slot
 * dumps every entry overlapping its canonical VA or naming its token. */
typedef struct _ADMISSION_UMD_VA_EVENT {
  LONGLONG Qpc; ULONGLONG Va, Bytes, Token; const void *Device;
  UINT Op; HRESULT Hr; D3DKMT_HANDLE Allocation;
} ADMISSION_UMD_VA_EVENT;
#define ADMISSION_UMD_VA_RING 8192u
static ADMISSION_UMD_VA_EVENT va_ring[ADMISSION_UMD_VA_RING];
static volatile LONG va_ring_next;
static void va_record(const void *device, UINT op, ULONGLONG token,
                      D3DKMT_HANDLE allocation, ULONGLONG va,
                      ULONGLONG bytes, HRESULT hr) {
  LARGE_INTEGER now; (void)QueryPerformanceCounter(&now);
  ADMISSION_UMD_VA_EVENT *e=&va_ring[(ULONG)InterlockedIncrement(&va_ring_next)%ADMISSION_UMD_VA_RING];
  e->Qpc=now.QuadPart;e->Va=va;e->Bytes=bytes;e->Token=token;e->Device=device;
  e->Op=op;e->Hr=hr;e->Allocation=allocation;
}
static void va_dump(const ADMISSION_UMD_DEVICE *device, ULONGLONG token, ULONGLONG va,
                    D3DKMT_HANDLE allocation) {
  LARGE_INTEGER now; (void)QueryPerformanceCounter(&now);
  LONG last=InterlockedCompareExchange(&va_ring_next,0,0); UINT emitted=0;
  ULONGLONG fences[8]={0}; UINT fenceCount=0;
  /* Pass 1: MakeResident fences that named this allocation (op 6). */
  for(LONG n=0;n<(LONG)ADMISSION_UMD_VA_RING && fenceCount<8u;++n) {
    const ADMISSION_UMD_VA_EVENT *e=&va_ring[(ULONG)(last-n)%ADMISSION_UMD_VA_RING];
    if(e->Qpc && e->Op==6u && allocation && e->Allocation==allocation && e->Bytes)
      fences[fenceCount++]=e->Bytes;
  }
  for(LONG n=0;n<(LONG)ADMISSION_UMD_VA_RING && emitted<64u;++n) {
    const ADMISSION_UMD_VA_EVENT *e=&va_ring[(ULONG)(last-n)%ADMISSION_UMD_VA_RING];
    BOOL match=FALSE;
    if(!e->Qpc) continue;
    if(e->Op==7u) { for(UINT f=0;f<fenceCount;++f) if(e->Bytes==fences[f]) match=TRUE; }
    else match=e->Token==token || (allocation && e->Allocation==allocation) ||
        (e->Op!=6u && e->Va<=va && va<e->Va+(e->Bytes?e->Bytes:1));
    if(!match) continue;
    UINT values[10]={e->Op,e->Device==device,(UINT)((now.QuadPart-e->Qpc)/2400),
        (UINT)e->Token,(UINT)(e->Va>>32),(UINT)e->Va,(UINT)e->Bytes,(UINT)e->Hr,
        (UINT)e->Allocation,(UINT)n};
    AdmissionUmdDiagnostic("reject-va-history",S_OK,values,ARRAYSIZE(values));
    ++emitted;
  }
}
void AdmissionUmdVaRecordDeallocate(const void *device, ULONGLONG token,
                                    D3DKMT_HANDLE allocation, ULONGLONG va,
                                    HRESULT hr) {
  va_record(device,4u,token,allocation,va,0,hr);
}

static ADMISSION_UMD_SCREEN_BUFFER *find_slot(ADMISSION_UMD_DEVICE *device,
                                             uint64_t token) {
  for (UINT i=0; i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(device); ++i)
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
  for (UINT i = 0; i < ADMISSION_UMD_SCREEN_BUFFER_SCAN(device); ++i) {
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
  va_record(device,1u,0,0,request.VirtualAddress,bytes,hr);
  if (FAILED(hr) || !request.VirtualAddress) return 0;
  *va = request.VirtualAddress;
  return 1;
}

static int wait_paging(void *context, uint64_t fence);
/* EXP1043 diagnostic: attribute each paging-fence CPU wait to its caller and
 * to the slot it serves (site in the phase-2 count, slot shape in bytes). */
static int wait_paging_at(ADMISSION_UMD_DEVICE *device, uint64_t fence,
                          UINT site, uint64_t token);

/* EXP1004: VidMm writes invalid PTEs when it maps a non-resident allocation
 * and, for fresh BOs, did not re-send valid ones after the later persistent
 * MakeResident (EXP1001 leaf ring; EXP1003 touch did not repair them). Take
 * the slot's persistent residency reference first, so the Map paging
 * operation writes valid entries. Best effort: on failure the mapping
 * proceeds as before and make_resident takes the reference later. */
static void resident_before_map(ADMISSION_UMD_DEVICE *device, uint64_t token,
                                D3DKMT_HANDLE handle) {
  AcquireSRWLockShared(&device->ScreenBufferLock);
  auto *slot=find_slot(device,token);
  bool needed=slot && !slot->Resident && slot->KernelAllocation==handle;
  ReleaseSRWLockShared(&device->ScreenBufferLock);
  if(!needed || !device->KernelCallbacks->pfnMakeResidentCb) return;
  UINT priority=D3DDDI_ALLOCATIONPRIORITY_NORMAL;
  D3DDDI_MAKERESIDENT request={};
  request.hPagingQueue=device->PagingQueue;
  request.NumAllocations=1;request.AllocationList=&handle;
  request.PriorityList=&priority;
  HRESULT hr=device->KernelCallbacks->pfnMakeResidentCb(
      device->RuntimeDevice.handle,&request);
  va_record(device,6u,token,handle,0,request.PagingFenceValue,hr);
  if(FAILED(hr) && hr!=E_PENDING) return;
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  slot=find_slot(device,token);
  if(slot && slot->KernelAllocation==handle) slot->Resident=TRUE;
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  if(hr==E_PENDING && request.PagingFenceValue)
    (void)wait_paging_at(device,request.PagingFenceValue,2u,token);
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
  resident_before_map(device, token, request.hAllocation);
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
  va_record(device,2u,token,request.hAllocation,va,pages<<12,hr);
  device->PagingWaitToken = token;
  if (FAILED(hr) && hr != E_PENDING) return 0;
  if (request.VirtualAddress != va) return 3;
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  auto *slot=find_slot(device,token);
  /* EXP981: a second mapping of the same slot replaces its canonical VA. */
  if(slot && slot->CanonicalGpuVa && slot->CanonicalGpuVa!=va)
    va_record(device,5u,token,request.hAllocation,slot->CanonicalGpuVa,0,S_OK);
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
  HRESULT hr = device->KernelCallbacks->pfnFreeGpuVirtualAddressCb(
      device->RuntimeDevice.handle, &request);
  va_record(device,3u,0,0,va,bytes,hr);
  return SUCCEEDED(hr);
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

/* EXP978: the end of a submission releases only its copy holds. Residency is
 * a persistent per-slot reference (see make_resident); evicting after every
 * submit made VidMm page the whole working set out and back in (~350 MB/s
 * each way in EXP977 ETW), stalling paging waits and invalidating PTEs. */
static int evict(void *context, const uint64_t *tokens, unsigned count) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  if (!device) return 0;
  release_copies(device,tokens,count);
  return 1;
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
  for(UINT i=0;valid && i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(device);++i)
    if(device->ScreenBuffers[i].CopyHeld) valid=false;
  for(unsigned i=0;valid && i<count;++i) {
    auto *slot=find_slot(device,tokens[i]);
    valid=slot && !slot->Transition && !slot->CopyHeld &&
        !slot->SourceHolds && !slot->SubmissionHolds &&
        (slot->StagingAllocation || slot->PrivateStaging || slot->Direct ||
         slot->SystemDirect) &&
        slot->CanonicalGpuVa &&
        (!slot->Mapped || slot->SystemDirect ||
         (slot->NativeBo && slot->NativeMapRelease));
  }
  if(valid) for(unsigned i=0;i<count;++i) {
    auto *slot=find_slot(device,tokens[i]);slot->CopyHeld=TRUE;++slot->SubmissionHolds;
  }
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  if(!valid) {
    HeapFree(GetProcessHeap(),0,priorities);HeapFree(GetProcessHeap(),0,handles);
    return 0;
  }
  /* Only slots without their persistent residency reference need a call. */
  unsigned pending=0;
  AcquireSRWLockShared(&device->ScreenBufferLock);
  for(unsigned i=0;i<count;++i) {
    auto *slot=find_slot(device,tokens[i]);
    if(slot && !slot->Resident) handles[pending++]=handles[i];
  }
  ReleaseSRWLockShared(&device->ScreenBufferLock);
  if(!pending) {
    HeapFree(GetProcessHeap(), 0, priorities);
    HeapFree(GetProcessHeap(), 0, handles);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
    measure_g4_phase(1u,phase_start,0u,0u,S_OK);
#endif
    *fence = 0;
    return 1;
  }
  D3DDDI_MAKERESIDENT request = {};
  request.hPagingQueue = device->PagingQueue;
  request.NumAllocations = pending;
  request.AllocationList = handles;
  request.PriorityList = priorities;
  HRESULT hr = device->KernelCallbacks->pfnMakeResidentCb(
      device->RuntimeDevice.handle, &request);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  measure_g4_phase(1u,phase_start,pending,0u,hr);
#endif
  /* EXP983: one ring entry per allocation named by this MakeResident. */
  for(unsigned i=0;i<pending;++i)
    va_record(device,6u,0,handles[i],0,request.PagingFenceValue,hr);
  /* Allocations the runtime accepted now hold a residency reference, even
   * when the call failed part-way: record them so it is never taken twice. */
  unsigned accepted = (SUCCEEDED(hr) || hr == E_PENDING) ? pending :
      (request.NumAllocations < pending ? request.NumAllocations : 0u);
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  for(unsigned i=0;i<accepted;++i)
    for(UINT j=0;j<ADMISSION_UMD_SCREEN_BUFFER_SCAN(device);++j) {
      auto *slot=&device->ScreenBuffers[j];
      if(slot->Active && !slot->Resident &&
         slot->KernelAllocation==handles[i])
        {slot->Resident=TRUE;break;}
    }
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  HeapFree(GetProcessHeap(), 0, priorities);
  HeapFree(GetProcessHeap(), 0, handles);
  if ((FAILED(hr) && hr != E_PENDING) || request.NumAllocations != pending) {
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

static int wait_paging_at(ADMISSION_UMD_DEVICE *device, uint64_t fence,
                          UINT site, uint64_t token) {
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  LARGE_INTEGER phase_start;
  (void)QueryPerformanceCounter(&phase_start);
#endif
  int ok=wait_object(device, device ? device->PagingSyncObject : 0, fence);
  va_record(device,7u,0,0,0,fence,ok ? S_OK : E_FAIL);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  ULONGLONG shape=0;
  if(device && token) {
    AcquireSRWLockShared(&device->ScreenBufferLock);
    auto *slot=find_slot(device,token);
    if(slot)
      shape=(slot->Bytes>>10) | ((ULONGLONG)slot->ClassId<<32) |
          ((ULONGLONG)slot->Borrowed<<40) | ((ULONGLONG)slot->SystemDirect<<41) |
          ((ULONGLONG)slot->Direct<<42) | ((ULONGLONG)(slot->PrivateStaging!=NULL)<<43) |
          ((ULONGLONG)slot->Flags<<48);
    ReleaseSRWLockShared(&device->ScreenBufferLock);
  }
  measure_g4_phase(2u,phase_start,site,shape,ok ? S_OK : E_FAIL);
#else
  (void)site;(void)token;
#endif
  return ok;
}

static int wait_paging(void *context, uint64_t fence) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  /* Called by the winsys after map_va (bind) or make_resident (submit). */
  uint64_t token = device ? device->PagingWaitToken : 0;
  if (device) device->PagingWaitToken = 0;
  return wait_paging_at(device, fence, token ? 1u : 5u, token);
}

/* EXP1016 measurement only: DWM CPU read cost of staging inspection versus
 * copy-escape latency, aggregated and emitted every 128 samples. */
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
static volatile LONG64 exp1016_stats[9]; /* hash, escape, EXP1019 second hash: calls/bytes/ticks */
static void exp1016_note(UINT kind, ULONGLONG bytes, LONGLONG start) {
  LARGE_INTEGER end, frequency;
  if(!frame_process_is_dwm()) return;
  (void)QueryPerformanceCounter(&end);
  LONG64 calls=InterlockedIncrement64(&exp1016_stats[kind*3u]);
  InterlockedAdd64(&exp1016_stats[kind*3u+1u],(LONG64)bytes);
  InterlockedAdd64(&exp1016_stats[kind*3u+2u],end.QuadPart-start);
  if(calls%128) return;
  (void)QueryPerformanceFrequency(&frequency);
  UINT values[7];
  values[0]=kind;values[1]=(UINT)calls;
  values[2]=(UINT)(exp1016_stats[kind*3u+1u]>>10);
  values[3]=(UINT)exp1016_stats[kind*3u+2u];
  values[4]=(UINT)((ULONGLONG)exp1016_stats[kind*3u+2u]>>32);
  values[5]=(UINT)frequency.QuadPart;values[6]=0u;
  AdmissionUmdDiagnostic("measure-staging-cost",S_OK,values,ARRAYSIZE(values));
}
static LONGLONG exp1016_now(void) {
  LARGE_INTEGER now; (void)QueryPerformanceCounter(&now); return now.QuadPart;
}
#define EXP1016_START(name) LONGLONG name=exp1016_now()
#define EXP1016_NOTE(kind,bytes,name) exp1016_note((kind),(bytes),(name))
#else
#define EXP1016_START(name) do {} while(0)
#define EXP1016_NOTE(kind,bytes,name) do {} while(0)
#endif

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
  EXP1016_START(escape_start);
  HRESULT status = device->KernelCallbacks->pfnEscapeCb(
      device->Adapter->RuntimeAdapter.handle,&request);
  EXP1016_NOTE(1u,payload->TransferBytes,escape_start);
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

/* Returns 1 when the staging copy still equals the canonical allocation
 * (unchanged since the last upload/download), 0 when an upload is needed,
 * and -1 when the staging copy could not be inspected safely. */
static int staging_unchanged(ADMISSION_UMD_DEVICE *device,
                             ADMISSION_UMD_SCREEN_BUFFER *slot) {
  if(!slot->Sync.Valid || (slot->Borrowed && slot->Mapped)) return 0;
  BYTE *address=(BYTE *)slot->LockedBase;
  if(!slot->Mapped && slot->PrivateStaging) address=slot->PrivateStaging;
  bool temporary=!slot->Mapped && !slot->PrivateStaging, locked=false;
  if(temporary) {
    D3DDDICB_LOCK lock={};lock.hAllocation=slot->StagingAllocation;
    lock.Flags.LockEntire=1;lock.Flags.ReadOnly=1;
    HRESULT hr=device->KernelCallbacks->pfnLockCb(device->RuntimeDevice.handle,&lock);
    locked=SUCCEEDED(hr);
    if(!locked) return 0;
    if(lock.hAllocation!=slot->StagingAllocation) {device->DrawTerminal=TRUE;address=NULL;}
    else address=(BYTE *)lock.pData;
  }
  EXP1016_START(hash_start);
  int unchanged=address &&
      !AdmissionUmdStagingUploadNeeded(&slot->Sync,
          AdmissionUmdStagingHash(address,slot->Bytes),slot->Bytes);
  if(address) EXP1016_NOTE(0u,slot->Bytes,hash_start);
  if(locked) {
    D3DDDICB_UNLOCK unlock={};unlock.NumAllocations=1;
    unlock.phAllocations=&slot->StagingAllocation;
    if(FAILED(device->KernelCallbacks->pfnUnlockCb(device->RuntimeDevice.handle,&unlock))) {
      AcquireSRWLockExclusive(&device->ScreenBufferLock);
      slot->LockedBase=address;slot->Mapped=TRUE;
      ReleaseSRWLockExclusive(&device->ScreenBufferLock);
      device->DrawTerminal=TRUE;return -1;
    }
  }
  return device->DrawTerminal ? -1 : unchanged;
}

static int signal_render(ADMISSION_UMD_DEVICE *device,uint64_t next);

/* EXP1003: VidMm populates the PTEs of a MakeResident'ed mapping only when
 * the device is next scheduled (EXP1001 leaf ring: the mapping's last update
 * stayed the invalid one written at Map time). A CPU-time copy into a slot
 * before its first scheduled use therefore reads unpopulated PTEs. An empty
 * touch submission makes VidSch schedule the device first; the KMD completes
 * it on its CPU queue without GPU work. */
static int touch_device(ADMISSION_UMD_DEVICE *device,uint64_t va) {
  APPLE_AGX_G4_TOUCH touch={APPLE_AGX_G4_TOUCH_MAGIC,(unsigned)sizeof(touch)};
  D3DDDICB_SUBMITCOMMAND request={};
  if(!va || !device->KernelContext || !device->RenderSyncObject ||
     !device->KernelCallbacks->pfnSubmitCommandCb ||
     device->NextRenderFence>UINT64_MAX-2) return 0;
  request.Commands=va;request.CommandLength=sizeof(uint32_t);
  request.BroadcastContextCount=1;request.BroadcastContext[0]=device->KernelContext;
  request.pPrivateDriverData=&touch;request.PrivateDriverDataSize=sizeof(touch);
  request.RenderCBSequence=(UINT)InterlockedIncrement(&device->RenderCbSequence);
  HRESULT hr=device->KernelCallbacks->pfnSubmitCommandCb(
      device->RuntimeDevice.handle,&request);
  uint64_t next=device->NextRenderFence+1;
  int ok=SUCCEEDED(hr) && signal_render(device,next) &&
      wait_object(device,device->RenderSyncObject,next);
  if(ok) device->NextRenderFence=next;
  UINT values[2]={(UINT)va,(UINT)ok};
  AdmissionUmdDiagnostic("measure-touch",hr,values,ARRAYSIZE(values));
  return ok;
}

/* EXP1006: the failing mappings keep the invalid entries written at Map time
 * (EXP1001/EXP1004 leaf rings) although the allocation is resident. Mapping
 * the same allocation at the same VA again makes VidMm rewrite the range from
 * the allocation's current placement. */
static int map_canonical_as(ADMISSION_UMD_DEVICE *device,
                            const ADMISSION_UMD_SCREEN_BUFFER *slot,
                            D3DKMT_HANDLE allocation) {
  D3DDDI_MAPGPUVIRTUALADDRESS request={};
  if(!slot->CanonicalGpuVa || !allocation || !slot->Bytes ||
     !device->KernelCallbacks->pfnMapGpuVirtualAddressCb) return 0;
  request.hPagingQueue=device->PagingQueue;
  request.BaseAddress=slot->CanonicalGpuVa;
  request.hAllocation=allocation;
  request.OffsetInPages=0;
  request.SizeInPages=((slot->Bytes+0xffffULL)&~0xffffULL)>>12;
  request.Protection.Write=(slot->Flags & AppleAgxWin32BufferGpuWrite)!=0;
  request.Protection.Execute=slot->ClassId==AgxWin32BufferClassShader;
  HRESULT hr=device->KernelCallbacks->pfnMapGpuVirtualAddressCb(
      device->RuntimeDevice.handle,&request);
  va_record(device,2u,slot->Token,request.hAllocation,request.BaseAddress,
            request.SizeInPages<<12,hr);
  int ok=(SUCCEEDED(hr) || hr==E_PENDING) &&
      request.VirtualAddress==slot->CanonicalGpuVa &&
      (hr!=E_PENDING || (request.PagingFenceValue &&
                         wait_paging_at(device,request.PagingFenceValue,3u,slot->Token)));
  UINT values[3]={(UINT)slot->CanonicalGpuVa,(UINT)(slot->CanonicalGpuVa>>32),(UINT)ok};
  AdmissionUmdDiagnostic("measure-remap",hr,values,ARRAYSIZE(values));
  return ok;
}

/* EXP1011: the no-op residency is a property of one VidMm allocation (EXP1009
 * trace; ~1 % of fresh allocations): neither re-requesting residency nor
 * re-mapping repairs it. Give the slot a fresh canonical allocation at the
 * same GPU VA (Mesa keeps its VA), make it resident, map it, and release the
 * affected one. Staging, holds and the slot identity are unchanged. */
static int replace_canonical(ADMISSION_UMD_DEVICE *device,
                             ADMISSION_UMD_SCREEN_BUFFER *slot) {
  D3DKMT_HANDLE fresh=0, old=slot->KernelAllocation;
  if(slot->Direct || slot->SystemDirect || slot->Borrowed || !old || !slot->CanonicalGpuVa ||
     !device->KernelCallbacks->pfnMakeResidentCb) return 0;
  HRESULT hr=AdmissionUmdScreenNewCanonical(device,slot->ClassId,slot->Flags,
                                            slot->Bytes,&fresh);
  int ok=SUCCEEDED(hr) && fresh;
  if(ok) {
    UINT priority=D3DDDI_ALLOCATIONPRIORITY_NORMAL;
    D3DDDI_MAKERESIDENT request={};
    request.hPagingQueue=device->PagingQueue;
    request.NumAllocations=1;request.AllocationList=&fresh;
    request.PriorityList=&priority;
    hr=device->KernelCallbacks->pfnMakeResidentCb(device->RuntimeDevice.handle,&request);
    va_record(device,6u,slot->Token,fresh,0,request.PagingFenceValue,hr);
    ok=(SUCCEEDED(hr) || hr==E_PENDING) &&
        (hr!=E_PENDING || (request.PagingFenceValue &&
                           wait_paging_at(device,request.PagingFenceValue,4u,slot->Token)));
  }
  if(ok) ok=map_canonical_as(device,slot,fresh);
  if(ok) {
    AcquireSRWLockExclusive(&device->ScreenBufferLock);
    slot->KernelAllocation=fresh;slot->Resident=TRUE;
    AdmissionUmdStagingInvalidate(&slot->Sync);
    AdmissionUmdStagingChunksInvalidate(&slot->Chunks);
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    HRESULT freed=AdmissionUmdScreenFreeAllocation(device,old);
    AdmissionUmdVaRecordDeallocate(device,slot->Token,old,slot->CanonicalGpuVa,freed);
  } else if(fresh) {
    (void)AdmissionUmdScreenFreeAllocation(device,fresh);
  }
  UINT values[3]={(UINT)old,(UINT)fresh,(UINT)ok};
  AdmissionUmdDiagnostic("measure-replace-canonical",hr,values,ARRAYSIZE(values));
  return ok;
}

static int query_canonical(ADMISSION_UMD_DEVICE *device,
                           ADMISSION_UMD_SCREEN_BUFFER *slot,
                           APPLE_AGX_G3_COPY_REQUEST *payload) {
  int ok=copy_escape(device,payload) && payload->ProcessGeneration &&
      payload->MappingGeneration;
  if(!ok && replace_canonical(device,slot)) {
    payload->Allocation=slot->KernelAllocation;
    payload->Operation=APPLE_AGX_G3_COPY_QUERY;payload->Offset=0;
    payload->TransferBytes=0;payload->ProcessGeneration=0;payload->MappingGeneration=0;
    ok=copy_escape(device,payload) && payload->ProcessGeneration &&
        payload->MappingGeneration;
  }
  if(ok) slot->Queried=TRUE;
  return ok;
}

static int transfer_slot(ADMISSION_UMD_DEVICE *device,
                          ADMISSION_UMD_SCREEN_BUFFER *slot,bool download,
                          UINT *transfer_count,ULONGLONG *transfer_bytes) {
  int unchanged=0;
  if(!download) {
    unchanged=staging_unchanged(device,slot);
    if(unchanged<0) return 0;
  }
  {
    UINT values[6]={(UINT)slot->Token,(UINT)(slot->Token>>32),(UINT)download,
        (UINT)unchanged,(UINT)slot->Bytes,
        (UINT)slot->Mapped|((UINT)slot->Borrowed<<1)|((UINT)slot->GpuWritten<<2)|
        ((UINT)slot->Queried<<3)|((UINT)(slot->PrivateStaging!=NULL)<<4)};
    AdmissionUmdDiagnostic("ddi-slot-xfer",S_OK,values,ARRAYSIZE(values));
  }
  if(unchanged) return 1;
  AdmissionUmdStagingInvalidate(&slot->Sync);
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
  int success=query_canonical(device,slot,payload);
  if(success) step=2u;
  BYTE *address=(BYTE *)slot->LockedBase;
  if(!slot->Mapped && slot->PrivateStaging) address=slot->PrivateStaging;
  bool temporary=!slot->Mapped && !slot->PrivateStaging, locked=false;
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
  /* EXP999: skip chunks whose content equals the canonical copy. */
  bool chunked=AdmissionUmdStagingChunked(slot->Bytes,APPLE_AGX_G3_COPY_CAPACITY)!=0;
  if(success) for(uint64_t offset=0;offset<slot->Bytes;) {
    UINT count=(UINT)((slot->Bytes-offset)>APPLE_AGX_G3_COPY_CAPACITY ?
        APPLE_AGX_G3_COPY_CAPACITY : slot->Bytes-offset);
    UINT chunk=(UINT)(offset/APPLE_AGX_G3_COPY_CAPACITY);
    unsigned long long chunk_hash=0;
    if(!download && chunked) {
      chunk_hash=AdmissionUmdStagingHash(address+offset,count);
      if(AdmissionUmdStagingChunkCurrent(&slot->Chunks,chunk,chunk_hash)) {
        offset+=count;continue;
      }
    }
    payload->Operation=download ? APPLE_AGX_G3_COPY_DOWNLOAD : APPLE_AGX_G3_COPY_UPLOAD;
    payload->Offset=offset;payload->TransferBytes=count;
    if(!download) CopyMemory(payload->Data,address+offset,count);
    if(!copy_escape(device,payload)) {success=0;break;}
    if(download) CopyMemory(address+offset,payload->Data,count);
    if(chunked)
      AdmissionUmdStagingChunkStore(&slot->Chunks,chunk,download ?
          AdmissionUmdStagingHash(address+offset,count) : chunk_hash);
    if(transfer_count) ++*transfer_count;
    if(transfer_bytes) *transfer_bytes+=count;
    offset+=count;
  }
  if(success && chunked) AdmissionUmdStagingChunksValidate(&slot->Chunks);
  else AdmissionUmdStagingChunksInvalidate(&slot->Chunks);
  /* Staging now equals the canonical allocation in both directions. */
  if(success)
    AdmissionUmdStagingRecord(&slot->Sync,
        AdmissionUmdStagingHash(address,slot->Bytes),slot->Bytes);
  /* EXP989 receipt-only: sampled content of what crossed CPU<->GPU. */
  if(success) {
    UINT samples=0u,nonzero=0u;
    for(uint64_t at=0;at+4u<=slot->Bytes;at+=256u) {
      UINT word; CopyMemory(&word,address+at,sizeof(word));
      ++samples; if(word) ++nonzero;
    }
    UINT values[8]={(UINT)download,(UINT)slot->Token,(UINT)slot->Bytes,nonzero,samples,
        slot->ClassId,(UINT)temporary | ((UINT)slot->Mapped<<1) |
        ((UINT)slot->Borrowed<<2) | ((UINT)slot->Direct<<3),(UINT)slot->KernelAllocation};
    AdmissionUmdDiagnostic("measure-xfer",S_OK,values,ARRAYSIZE(values));
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
    /* EXP1059: a Direct shadow map is ordinary memory, not a VidMm lock. */
    bool shadow=!locked && slot->PrivateStaging && address==slot->PrivateStaging;
    if(!shadow &&
       FAILED(device->KernelCallbacks->pfnUnlockCb(device->RuntimeDevice.handle,&unlock))) {
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
    va_dump(device,slot->Token,slot->CanonicalGpuVa,slot->KernelAllocation);
  }
  HeapFree(GetProcessHeap(),0,payload);return success;
}

/* EXP1059: a Direct slot (an imported presentation surface) has no
 * CPU-visible allocation, so a CPU map used to fail and callers such as
 * UpdateSubresourceUP wrote through a null view (ApplicationFrameHost
 * c0000005).  Serve the map from a private shadow: download the canonical
 * content on map; upload the changed chunks through the copy escape before a
 * submission or a present uses the surface (dropping the map, as for borrowed
 * staging) or at unmap.  Mesa has already synchronized the GPU users. */
static bool direct_shadow_slot(const ADMISSION_UMD_SCREEN_BUFFER *slot) {
  return slot && slot->Direct && !slot->SystemDirect &&
         !slot->StagingAllocation && slot->KernelAllocation &&
         slot->CanonicalGpuVa && slot->Bytes;
}

/* EXP995: staging of a slot that is neither CPU-mapped nor borrowed (shared
 * with another device) is read or written by nobody until a CPU map exists.
 * Such slots skip the post-submission download (GpuWritten stays pending until
 * AdmissionUmdGpuvaPrepareCpuMap) and the per-submission staging inspection. */
static bool cpu_quiet(const ADMISSION_UMD_SCREEN_BUFFER *slot) {
  return !slot->Mapped && !slot->Borrowed;
}

/* The held slots a completed submission downloads (transfer_held(true)):
 * GPU-written, CPU-visible (mapped or shared) and not presentation-direct. */
static bool download_due(const ADMISSION_UMD_SCREEN_BUFFER *slot) {
  return slot->CopyHeld && !slot->Direct && !slot->SystemDirect &&
         (slot->Flags & AppleAgxWin32BufferGpuWrite) && slot->GpuWritten &&
         !cpu_quiet(slot);
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
  /* EXP1003: one touch before the first copy into any never-queried slot. */
  if(!download) {
    uint64_t fresh=0;
    for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(device) && !fresh;++i) {
      auto *slot=&device->ScreenBuffers[i];
      if(slot->CopyHeld && !slot->Direct && !slot->SystemDirect && !slot->Queried &&
         !(cpu_quiet(slot) && slot->Sync.Valid)) fresh=slot->CanonicalGpuVa;
    }
    if(fresh) (void)touch_device(device,fresh);
  }
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(device);++i) {
    auto *slot=&device->ScreenBuffers[i];
    /* EXP1059: upload a CPU-mapped Direct shadow before the GPU uses the
     * surface and drop the map; the next map downloads the GPU result. */
    bool shadow=!download && direct_shadow_slot(slot) && slot->PrivateStaging &&
        slot->Mapped && slot->NativeBo && slot->NativeMapRelease;
    if(download ? !download_due(slot) :
       (!slot->CopyHeld || (slot->Direct && !shadow) || slot->SystemDirect ||
        (cpu_quiet(slot) && slot->Sync.Valid))) {
      if(slot->CopyHeld && !download) {
        UINT values[4]={(UINT)slot->Token,(UINT)(slot->Token>>32),
            (UINT)slot->Direct|((UINT)slot->SystemDirect<<1)|
            ((UINT)cpu_quiet(slot)<<2)|((UINT)slot->Sync.Valid<<3),
            (UINT)slot->Bytes};
        AdmissionUmdDiagnostic("ddi-slot-skip",S_OK,values,ARRAYSIZE(values));
      }
      continue;
    }
    if(!transfer_slot(device,slot,download,&measured_count,&measured_bytes)) {
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
      measure_g4_phase(download ? 6u : 3u,phase_start,measured_count,
          measured_bytes,E_FAIL);
#endif
      return 0;
    }
    if(download) {
      AcquireSRWLockExclusive(&device->ScreenBufferLock);
      slot->GpuWritten=FALSE;
      ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    }
  }
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  measure_g4_phase(download ? 6u : 3u,phase_start,measured_count,
      measured_bytes,S_OK);
#endif
  return 1;
}

int AdmissionUmdGpuvaPrepareDirectMap(ADMISSION_UMD_DEVICE *device,
                                      uint64_t token) {
  BYTE *shadow=NULL;
  AcquireSRWLockShared(&device->ScreenBufferLock);
  auto *slot=find_slot(device,token);
  bool direct=direct_shadow_slot(slot) && !slot->Mapped && !slot->Transition &&
      !slot->SubmissionHolds;
  bool allocate=direct && !slot->PrivateStaging;
  SIZE_T bytes=direct ? (SIZE_T)((slot->Bytes+0xffffULL)&~0xffffULL) : 0;
  ReleaseSRWLockShared(&device->ScreenBufferLock);
  if(!direct) return 1;
  if(allocate) {
    shadow=(BYTE *)VirtualAlloc(NULL,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!shadow) return 0;
    AcquireSRWLockExclusive(&device->ScreenBufferLock);
    slot=find_slot(device,token);
    if(slot && direct_shadow_slot(slot) && !slot->PrivateStaging) {
      slot->PrivateStaging=shadow;shadow=NULL;
    }
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    if(shadow) {(void)VirtualFree(shadow,0,MEM_RELEASE);return 0;}
  }
  return transfer_slot(device,slot,true,NULL,NULL);
}

int AdmissionUmdGpuvaFinishDirectMap(ADMISSION_UMD_DEVICE *device,
                                     uint64_t token) {
  AcquireSRWLockShared(&device->ScreenBufferLock);
  auto *slot=find_slot(device,token);
  bool direct=direct_shadow_slot(slot) && slot->PrivateStaging &&
      !slot->Mapped && !slot->Transition;
  ReleaseSRWLockShared(&device->ScreenBufferLock);
  return direct ? transfer_slot(device,slot,false,NULL,NULL) : 1;
}

/* EXP1059: a presented Direct surface may carry CPU writes that no
 * submission has uploaded yet; publish them before the present. */
int AdmissionUmdGpuvaPublishDirectMap(ADMISSION_UMD_DEVICE *device,
                                      uint64_t token) {
  AcquireSRWLockShared(&device->ScreenBufferLock);
  auto *slot=find_slot(device,token);
  bool publish=direct_shadow_slot(slot) && slot->PrivateStaging &&
      slot->Mapped && !slot->Transition && !slot->SubmissionHolds &&
      !slot->SourceHolds && slot->NativeBo && slot->NativeMapRelease;
  ReleaseSRWLockShared(&device->ScreenBufferLock);
  return publish ? transfer_slot(device,slot,false,NULL,NULL) : 1;
}

/* EXP995: before the first CPU map of a slot, bring a pending GPU result
 * into its staging. Returns 0 only when that download failed. */
int AdmissionUmdGpuvaPrepareCpuMap(ADMISSION_UMD_DEVICE *device,uint64_t token) {
  AcquireSRWLockShared(&device->ScreenBufferLock);
  auto *slot=find_slot(device,token);
  bool pending=slot && !slot->Direct && !slot->SystemDirect && !slot->Mapped && !slot->Transition &&
      !slot->SubmissionHolds && slot->GpuWritten;
  ReleaseSRWLockShared(&device->ScreenBufferLock);
  if(!pending) return 1;
  if(!transfer_slot(device,slot,true,NULL,NULL)) return 0;
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  slot->GpuWritten=FALSE;
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
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

/* EXP985: record which held slots this submission writes (render targets and
 * depth/stencil named by the winsys). Only these are downloaded afterwards. */
static int mark_written(ADMISSION_UMD_DEVICE *device, const uint64_t *written,
                        unsigned written_count) {
  int ok=1;
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  for(unsigned i=0;i<written_count && ok;++i) {
    ADMISSION_UMD_SCREEN_BUFFER *slot=NULL;
    for(UINT j=0;j<ADMISSION_UMD_SCREEN_BUFFER_SCAN(device);++j)
      if(device->ScreenBuffers[j].Active && device->ScreenBuffers[j].Token==written[i]) {
        slot=&device->ScreenBuffers[j];break;
      }
    if(!slot || slot->Transition || !slot->CopyHeld) ok=0;
    else slot->GpuWritten=TRUE;
  }
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  return ok;
}

/* EXP1082: whether completing the submission now held downloads anything a
 * CPU or another device may read. */
static bool completion_needs_download(ADMISSION_UMD_DEVICE *device) {
  bool due=false;
  AcquireSRWLockShared(&device->ScreenBufferLock);
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(device) && !due;++i)
    due=download_due(&device->ScreenBuffers[i]);
  ReleaseSRWLockShared(&device->ScreenBufferLock);
  return due;
}

/* EXP1082: completion work of the queued job: wait for its fence, then
 * download the GPU-written CPU-visible slots it still holds. Runs once,
 * before any wait for that fence returns; a failure is terminal for the
 * device. It calls no runtime GPU-signal callback: it may run inside the
 * device's destruction (EXP1066: D3D11 crashed in
 * SignalSynchronizationObjectFromGpu2CB there). */
static int complete_render(ADMISSION_UMD_DEVICE *device) {
  uint64_t internal=device->PendingRenderFence;
  if(!internal) return 1;
  device->PendingRenderFence=0;
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  LARGE_INTEGER wait_start;
  (void)QueryPerformanceCounter(&wait_start);
#endif
  int waited=wait_object(device,device->RenderSyncObject,internal);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  measure_g4_phase(5u,wait_start,device->PendingRenderSequence,0u,
      waited ? S_OK : E_FAIL);
#endif
  if(!waited || !transfer_held(device,true)) {
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
    device->FrameSubmitStatus = E_FAIL;
#endif
    device->DrawTerminal=TRUE;return 0;
  }
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  device->FrameCompletedFence = internal;
#endif
  return 1;
}

/* EXP1080/EXP1081: DWM spent ~12 ms of every ~55 ms frame waiting inside
 * submit() for each job (1.7 ms x ~8 submissions) while its CPU work for the
 * next batch ran only afterwards. Return once the job and its fence signal
 * are queued when its completion downloads nothing: the winsys keeps the
 * submission held and retires it (wait_render) before the next submission
 * or a CPU access, so the GPU runs while the CPU builds the next batch. A
 * job that wrote a CPU-visible slot still completes here, so that slot's
 * staging is current when submit() returns, as before. */
static int finish_submission(ADMISSION_UMD_DEVICE *device,uint64_t internal,
                             UINT sequence,uint64_t *fence) {
  if(!completion_needs_download(device)) {
    device->PendingRenderFence=internal;
    device->PendingRenderSequence=sequence;
    device->NextRenderFence=internal;
    *fence=internal;
    return 1;
  }
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  LARGE_INTEGER wait_start;
  (void)QueryPerformanceCounter(&wait_start);
#endif
  int waited=wait_object(device,device->RenderSyncObject,internal);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  measure_g4_phase(5u,wait_start,sequence,0u,waited ? S_OK : E_FAIL);
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
#endif
  return 1;
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
  /* The winsys retires the previous submission first; keep at most one
   * queued job even if it did not, before this one's uploads. */
  if(device->PendingRenderFence && !complete_render(device)) return 2;
  AcquireSRWLockShared(&device->ScreenBufferLock);
  for (unsigned i = 0; i < written_count; ++i) {
    ADMISSION_UMD_SCREEN_BUFFER *slot = NULL;
    for (UINT j = 0; j < ADMISSION_UMD_SCREEN_BUFFER_SCAN(device); ++j)
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
  {
    UINT values[10]={written_count,0u,0u,0u,0u,0u,0u,0u,0u,0u};
    for(unsigned i=0;i<written_count && i<4u;++i) {
      values[1u+2u*i]=(UINT)written[i];values[2u+2u*i]=(UINT)(written[i]>>32);
    }
    values[9]=bytes;
    AdmissionUmdDiagnostic("ddi-submit",S_OK,values,ARRAYSIZE(values));
  }
  if(!mark_written(device,written,written_count) || !transfer_held(device,false)) {
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
  if(!signal_render(device,internal)) {
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
    device->FrameSubmitStatus = E_FAIL;
#endif
    device->DrawTerminal=TRUE;return 2;
  }
  int finished=finish_submission(device,internal,request.RenderCBSequence,fence);
  if(finished!=1) return finished;
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
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
  if (device && device->PendingRenderFence &&
      fence >= device->PendingRenderFence && !complete_render(device))
    return 0;
  int waited = wait_object(device, device ? device->RenderSyncObject : 0, fence);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  if (waited && device != NULL && device->FrameCompletedFence < fence)
    device->FrameCompletedFence = fence;
#endif
  return waited;
}

/* EXP1082: non-blocking: whether the GPU has signalled this render fence. */
static int query_render(void *context, uint64_t fence) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  return device && device->RenderFenceAddress && fence &&
         *device->RenderFenceAddress >= fence;
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
      submit, wait_render, evict, private_escape, query_render};
  return &operations;
}
#endif
