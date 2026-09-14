#include "agx_kmt_native_bridge.h"
#pragma warning(push)
#pragma warning(disable:4201)
#include <d3d10umddi.h>
#pragma warning(pop)
#include <stdio.h>
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "umd_internal.h"
#include "umd_asahi_owner.h"
#ifdef __cplusplus
}
#endif

#define KMT_NATIVE_MAGIC 0x424b4741u
#define KMT_STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000dL)
#define KMT_STATUS_NOT_SUPPORTED ((NTSTATUS)0xc00000bbL)
#define KMT_STATUS_INVALID_STATE ((NTSTATUS)0xc0000184L)
#define KMT_STATUS_TIMEOUT ((NTSTATUS)0x00000102L)

typedef struct _AGX_KMT_NATIVE_DUMP_OPS {
  HANDLE (WINAPI *Create)(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
  BOOL (WINAPI *Write)(HANDLE,LPCVOID,DWORD,LPDWORD,LPOVERLAPPED);
  BOOL (WINAPI *Flush)(HANDLE);
  BOOL (WINAPI *Close)(HANDLE);
} AGX_KMT_NATIVE_DUMP_OPS;

typedef NTSTATUS (APIENTRY *AGX_KMT_NATIVE_RENDER_OP)(D3DKMT_RENDER *);

static const AGX_KMT_NATIVE_DUMP_OPS NativeDumpOperations={
  CreateFileW,WriteFile,FlushFileBuffers,CloseHandle
};

struct _AGX_KMT_NATIVE_BRIDGE {
  UINT Magic;
  DWORD Thread, TimeoutMs;
  D3DKMT_HANDLE AdapterHandle, DeviceHandle, PagingHandle, ContextHandle;
  volatile const UINT64 *PagingFence;
  PFND3DKMT_MAKERESIDENT MakeResident;
  PFND3DKMT_EVICT Evict;
  BOOL Ready;
  BOOL OwnershipUncertain;
  ADMISSION_UMD_ADAPTER Adapter;
  ADMISSION_UMD_DEVICE Device;
  AGX_WIN32_ASAHI_BACKEND Backend;
  ADMISSION_UMD_ASAHI_OWNER Owner;
  AGX_WIN32_ASAHI_OWNER_OPS OwnerOperations;
  D3DDDI_ADAPTERCALLBACKS AdapterCallbacks;
  D3DDDI_DEVICECALLBACKS DeviceCallbacks;
  D3D10DDI_CORELAYER_DEVICECALLBACKS CoreCallbacks;
  DXGI_DDI_BASE_CALLBACKS DxgiCallbacks;
  AGX_KMT_NATIVE_RECEIPT Receipt;
};

static AGX_KMT_NATIVE_BRIDGE *bridge(HANDLE context) {
  AGX_KMT_NATIVE_BRIDGE *b=(AGX_KMT_NATIVE_BRIDGE *)context;
  return b && b->Magic==KMT_NATIVE_MAGIC && b->Thread==GetCurrentThreadId() ? b : NULL;
}

static HRESULT record(AGX_KMT_NATIVE_BRIDGE *b, AGX_KMT_NATIVE_STAGE stage,
    NTSTATUS status, BOOL called, D3DKMT_HANDLE handle, UINT count) {
  HRESULT result=status>=0 ? S_OK : HRESULT_FROM_NT(status);
  if(!b) return E_INVALIDARG;
  b->Receipt.Bytes=sizeof(b->Receipt); b->Receipt.Stage=stage;
  b->Receipt.Status=status; b->Receipt.Result=result; b->Receipt.CalledKmt=called;
  b->Receipt.Handle=handle; b->Receipt.Count=count; ++b->Receipt.Sequence;
  printf("NATIVE_KMT: sequence=%llu stage=%u called=%u status=0x%08lx result=0x%08lx handle=%u count=%u\n",
      b->Receipt.Sequence,(unsigned)stage,(unsigned)called,(ULONG)status,(ULONG)result,handle,count);
  fflush(stdout);
  return result;
}

static HRESULT reject(AGX_KMT_NATIVE_BRIDGE *b,AGX_KMT_NATIVE_STAGE stage) {
  return record(b,stage,KMT_STATUS_INVALID_PARAMETER,FALSE,0,0);
}

/* Keep the exact command identity and CREATE_NEW collision behavior together
 * with the render boundary. The qualification test substitutes only the
 * external Win32/KMT operations; production always supplies the table above. */
static HRESULT dump_command(AGX_KMT_NATIVE_BRIDGE *b,const void *command,
    DWORD commandBytes,const AGX_KMT_NATIVE_DUMP_OPS *operations) {
  wchar_t name[128];
  HANDLE file;
  DWORD written=0,error=0;
  AGX_KMT_NATIVE_DUMP_STAGE stage=AgxKmtNativeDumpName;
  if(swprintf_s(name,ARRAYSIZE(name),L"native-kmt-command-%08x-%016llx.bin",
      b->Receipt.Win32Generation,b->Receipt.CommandHash)<0) {
    error=ERROR_INVALID_NAME;
  } else {
    stage=AgxKmtNativeDumpCreate;
    file=operations->Create(name,GENERIC_WRITE,FILE_SHARE_READ,NULL,
        CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) {
      error=GetLastError();
    } else {
      stage=AgxKmtNativeDumpWrite;
      if(!operations->Write(file,command,commandBytes,&written,NULL)) {
        error=GetLastError();
      } else if(written!=commandBytes) {
        error=ERROR_WRITE_FAULT;
      } else {
        stage=AgxKmtNativeDumpFlush;
        if(!operations->Flush(file)) error=GetLastError();
      }
      if(!operations->Close(file) && !error) {
        stage=AgxKmtNativeDumpClose;
        error=GetLastError();
      }
    }
  }
  if(!error) stage=AgxKmtNativeDumpComplete;
  b->Receipt.CommandDumpStage=stage;
  b->Receipt.CommandDumpError=error;
  b->Receipt.CommandDumpBytes=written;
  printf("NATIVE_KMT_COMMAND_DUMP: stage=%u error=%lu bytes=%lu\n",
      (unsigned)stage,error,written);
  fflush(stdout);
  if(error) {
    HRESULT result=HRESULT_FROM_WIN32(error);
    b->Receipt.Stage=AgxKmtNativeCommandDump;
    b->Receipt.Status=KMT_STATUS_INVALID_STATE;
    b->Receipt.Result=result;
    b->Receipt.CalledKmt=FALSE;
    b->Receipt.Handle=b->ContextHandle;
    b->Receipt.Count=written;
    ++b->Receipt.Sequence;
    printf("NATIVE_KMT_COMMAND_DUMP_REJECT: sequence=%llu operation=%u error=%lu result=0x%08lx bytes=%lu\n",
        b->Receipt.Sequence,(unsigned)stage,error,(ULONG)result,written);
    fflush(stdout);
    return result;
  }
  return S_OK;
}

static HRESULT dump_then_render(AGX_KMT_NATIVE_BRIDGE *b,const void *command,
    DWORD commandBytes,D3DKMT_RENDER *request,const AGX_KMT_NATIVE_DUMP_OPS *dumpOperations,
    AGX_KMT_NATIVE_RENDER_OP renderOperation) {
  HRESULT result=dump_command(b,command,commandBytes,dumpOperations);
  if(FAILED(result)) return result;
  ++b->Receipt.Renders;
  return record(b,AgxKmtNativeRender,renderOperation(request),
      renderOperation==D3DKMTRender,b->ContextHandle,request->AllocationCount);
}

/* The existing device's ScreenBuffers are the authoritative allocation table.
 * No callback caches another token/handle/resource identity registry. */
static BOOL owned(AGX_KMT_NATIVE_BRIDGE *b,D3DKMT_HANDLE handle) {
  BOOL found=FALSE;
  if(!b || b->Device.Magic!=ADMISSION_UMD_DEVICE_MAGIC || !handle) return FALSE;
  AcquireSRWLockShared(&b->Device.ScreenBufferLock);
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i)
    if(b->Device.ScreenBuffers[i].Active && b->Device.ScreenBuffers[i].KernelAllocation==handle) {
      found=TRUE; break;
    }
  ReleaseSRWLockShared(&b->Device.ScreenBufferLock);
  return found;
}

static ADMISSION_UMD_SCREEN_BUFFER *slot_locked(AGX_KMT_NATIVE_BRIDGE *b,D3DKMT_HANDLE handle) {
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i)
    if(b->Device.ScreenBuffers[i].Active && b->Device.ScreenBuffers[i].KernelAllocation==handle)
      return &b->Device.ScreenBuffers[i];
  return NULL;
}

static HRESULT wait_paging(AGX_KMT_NATIVE_BRIDGE *b,D3DKMT_HANDLE handle,UINT64 expected) {
  ULONGLONG started=GetTickCount64();
  b->Receipt.PagingExpected=expected;
  for(;;) {
    UINT64 observed=*b->PagingFence;
    b->Receipt.PagingObserved=observed;
    if(observed>=expected) return S_OK;
    if(GetTickCount64()-started>=b->TimeoutMs) {
      (void)record(b,AgxKmtNativePagingWait,KMT_STATUS_TIMEOUT,FALSE,handle,1);
      b->Receipt.Result=HRESULT_FROM_WIN32(WAIT_TIMEOUT);
      printf("NATIVE_KMT_PAGING_TIMEOUT: expected=%llu observed=%llu timeout_ms=%lu\n",
          expected,observed,b->TimeoutMs);
      return b->Receipt.Result;
    }
    Sleep(1);
  }
}

static HRESULT make_resident_one(AGX_KMT_NATIVE_BRIDGE *b,D3DKMT_HANDLE handle) {
  D3DDDI_MAKERESIDENT request={0};
  UINT priority=D3DDDI_ALLOCATIONPRIORITY_NORMAL;
  ADMISSION_UMD_SCREEN_BUFFER *slot;
  BOOL held;
  UINT64 fence;
  HRESULT result;
  NTSTATUS status;
  if(!owned(b,handle) || b->OwnershipUncertain) return reject(b,AgxKmtNativeResident);
  AcquireSRWLockShared(&b->Device.ScreenBufferLock);
  slot=slot_locked(b,handle);
  held=slot->KmtResidencyHeld;fence=slot->KmtPagingFence;
  ReleaseSRWLockShared(&b->Device.ScreenBufferLock);
  if(held) {
    ++b->Receipt.ResidencyReuses;
    return wait_paging(b,handle,fence);
  }
  /* One handle per call: no ambiguity about partial batch admission. A
   * successful/pending call contributes exactly one reference, kept through
   * every later Lock/Render and balanced only after unmap/ordered retirement. */
  request.hPagingQueue=b->PagingHandle;request.NumAllocations=1;
  request.AllocationList=&handle;request.PriorityList=&priority;
  status=b->MakeResident(&request);
  result=record(b,AgxKmtNativeResident,status,b->MakeResident==D3DKMTMakeResident,handle,1);
  if(FAILED(result)) return result;
  if((status!=0 && status!=(NTSTATUS)0x00000103L) || request.NumAllocations!=1) {
    b->OwnershipUncertain=TRUE;b->Ready=FALSE;
    return record(b,AgxKmtNativeResident,KMT_STATUS_INVALID_STATE,FALSE,handle,1);
  }
  AcquireSRWLockExclusive(&b->Device.ScreenBufferLock);
  slot=slot_locked(b,handle);
  if(!slot || slot->KmtResidencyHeld) {
    ReleaseSRWLockExclusive(&b->Device.ScreenBufferLock);
    b->OwnershipUncertain=TRUE;b->Ready=FALSE;
    return record(b,AgxKmtNativeResident,KMT_STATUS_INVALID_STATE,FALSE,handle,1);
  }
  slot->KmtResidencyHeld=TRUE;
  slot->KmtPagingFence=request.PagingFenceValue;
  fence=slot->KmtPagingFence;
  ReleaseSRWLockExclusive(&b->Device.ScreenBufferLock);
  ++b->Receipt.ResidencyAcquires;
  /* Store before waiting: timeout retains this exact contribution/fence. */
  return wait_paging(b,handle,fence);
}

static HRESULT make_resident(AGX_KMT_NATIVE_BRIDGE *b,const D3DKMT_HANDLE *handles,UINT count) {
  if(!b || b->OwnershipUncertain || !handles || !count || count>ADMISSION_UMD_SCREEN_BUFFER_LIMIT)
    return reject(b,AgxKmtNativeResident);
  for(UINT i=0;i<count;++i) {
    HRESULT result=make_resident_one(b,handles[i]);
    if(FAILED(result)) return result;
  }
  return S_OK;
}

static HRESULT evict_reference(AGX_KMT_NATIVE_BRIDGE *b,D3DKMT_HANDLE handle) {
  ADMISSION_UMD_SCREEN_BUFFER *slot;
  D3DKMT_EVICT request={0};
  BOOL held,eligible;
  UINT64 fence;
  HRESULT result;
  AcquireSRWLockShared(&b->Device.ScreenBufferLock);
  slot=slot_locked(b,handle);
  if(!slot) { ReleaseSRWLockShared(&b->Device.ScreenBufferLock);return reject(b,AgxKmtNativeEvict); }
  held=slot->KmtResidencyHeld;fence=slot->KmtPagingFence;
  eligible=!slot->Mapped && !slot->SubmissionHolds && !slot->SourceHolds;
  ReleaseSRWLockShared(&b->Device.ScreenBufferLock);
  if(!held) return S_OK;
  if(!eligible) return record(b,AgxKmtNativeEvict,KMT_STATUS_INVALID_STATE,FALSE,handle,1);
  result=wait_paging(b,handle,fence);
  if(FAILED(result)) return result;
  request.hDevice=b->DeviceHandle;request.NumAllocations=1;request.AllocationList=&handle;
  result=record(b,AgxKmtNativeEvict,b->Evict(&request),b->Evict==D3DKMTEvict,handle,1);
  if(FAILED(result)) return result;
  AcquireSRWLockExclusive(&b->Device.ScreenBufferLock);
  slot=slot_locked(b,handle);
  if(!slot || !slot->KmtResidencyHeld) {
    ReleaseSRWLockExclusive(&b->Device.ScreenBufferLock);
    b->OwnershipUncertain=TRUE;b->Ready=FALSE;
    return record(b,AgxKmtNativeEvict,KMT_STATUS_INVALID_STATE,FALSE,handle,1);
  }
  slot->KmtResidencyHeld=FALSE;slot->KmtPagingFence=0;
  ReleaseSRWLockExclusive(&b->Device.ScreenBufferLock);
  ++b->Receipt.ResidencyEvicts;
  return S_OK;
}

static HRESULT APIENTRY query_adapter(HANDLE context,const D3DDDICB_QUERYADAPTERINFO *args) {
  AGX_KMT_NATIVE_BRIDGE *b=bridge(context);
  D3DKMT_QUERYADAPTERINFO query={0};
  if(!b || !args || !args->pPrivateDriverData ||
      args->PrivateDriverDataSize!=sizeof(AGX_WIN32_DEVICE_INFO)) return reject(b,AgxKmtNativeQuery);
  query.hAdapter=b->AdapterHandle;query.Type=KMTQAITYPE_UMDRIVERPRIVATE;
  query.pPrivateDriverData=args->pPrivateDriverData;
  query.PrivateDriverDataSize=args->PrivateDriverDataSize;
  return record(b,AgxKmtNativeQuery,D3DKMTQueryAdapterInfo(&query),TRUE,b->AdapterHandle,1);
}

static HRESULT APIENTRY create_context(HANDLE context,D3DDDICB_CREATECONTEXT *args) {
  AGX_KMT_NATIVE_BRIDGE *b=bridge(context);
  D3DKMT_CREATECONTEXT request={0};
  const ADMISSION_WIN32_CONTEXT_CREATE *privateData=args?
      (const ADMISSION_WIN32_CONTEXT_CREATE *)args->pPrivateDriverData:NULL;
  HRESULT result;
  NTSTATUS status;
  if(!b || !args || b->ContextHandle || args->NodeOrdinal || args->EngineAffinity!=1 ||
      args->Flags.Value || !privateData || args->PrivateDriverDataSize!=sizeof(*privateData) ||
      privateData->Magic!=ADMISSION_WIN32_CONTEXT_MAGIC ||
      privateData->Version!=ADMISSION_WIN32_CONTEXT_VERSION || !privateData->Generation)
    return reject(b,AgxKmtNativeContextCreate);
  request.hDevice=b->DeviceHandle;request.NodeOrdinal=args->NodeOrdinal;
  request.EngineAffinity=args->EngineAffinity;request.Flags=args->Flags;
  request.pPrivateDriverData=args->pPrivateDriverData;
  request.PrivateDriverDataSize=args->PrivateDriverDataSize;
  request.ClientHint=D3DKMT_CLIENTHINT_UNKNOWN;
  status=D3DKMTCreateContext(&request);
  result=record(b,AgxKmtNativeContextCreate,status,TRUE,request.hContext,1);
  if(FAILED(result)) return result;
  b->ContextHandle=request.hContext;
  args->hContext=(HANDLE)(UINT_PTR)request.hContext;
  args->pCommandBuffer=request.pCommandBuffer;args->CommandBufferSize=request.CommandBufferSize;
  args->pAllocationList=request.pAllocationList;args->AllocationListSize=request.AllocationListSize;
  args->pPatchLocationList=request.pPatchLocationList;args->PatchLocationListSize=request.PatchLocationListSize;
  args->CommandBuffer=request.CommandBuffer;
  return S_OK;
}

static HRESULT destroy_context_handle(AGX_KMT_NATIVE_BRIDGE *b,D3DKMT_HANDLE handle) {
  D3DKMT_DESTROYCONTEXT request={0};
  HRESULT result;
  if(!b || !handle || handle!=b->ContextHandle) return reject(b,AgxKmtNativeContextDestroy);
  if(b->OwnershipUncertain) return record(b,AgxKmtNativeContextDestroy,KMT_STATUS_INVALID_STATE,FALSE,handle,1);
  request.hContext=handle;
  result=record(b,AgxKmtNativeContextDestroy,D3DKMTDestroyContext(&request),TRUE,handle,1);
  if(SUCCEEDED(result)) b->ContextHandle=0;
  return result;
}

static HRESULT APIENTRY destroy_context(HANDLE context,const D3DDDICB_DESTROYCONTEXT *args) {
  AGX_KMT_NATIVE_BRIDGE *b=bridge(context);
  if(!b || !args || (UINT_PTR)args->hContext>MAXUINT32) return reject(b,AgxKmtNativeContextDestroy);
  return destroy_context_handle(b,(D3DKMT_HANDLE)(UINT_PTR)args->hContext);
}

static HRESULT APIENTRY allocate(HANDLE context,D3DDDICB_ALLOCATE *args) {
  AGX_KMT_NATIVE_BRIDGE *b=bridge(context);
  D3DKMT_CREATEALLOCATION request={0};
  const ADMISSION_WIN32_ALLOCATION_CREATE *description;
  HRESULT result;
  NTSTATUS status;
  if(!b || b->OwnershipUncertain || !args || args->hResource || args->NumAllocations!=1 || !args->pAllocationInfo ||
      args->pPrivateDriverData || args->PrivateDriverDataSize ||
      !args->pAllocationInfo[0].pPrivateDriverData ||
      args->pAllocationInfo[0].PrivateDriverDataSize!=sizeof(*description)) return reject(b,AgxKmtNativeAllocate);
  description=(const ADMISSION_WIN32_ALLOCATION_CREATE *)args->pAllocationInfo[0].pPrivateDriverData;
  if(description->Magic!=ADMISSION_WIN32_ALLOCATION_MAGIC ||
      description->Version!=ADMISSION_WIN32_ALLOCATION_VERSION ||
      description->Bytes!=sizeof(*description) ||
      description->ClassId<AgxWin32BufferClassGeneral || description->ClassId>AgxWin32BufferClassEncoder)
    return reject(b,AgxKmtNativeAllocate);
  request.hDevice=b->DeviceHandle;request.NumAllocations=1;
  request.pAllocationInfo=args->pAllocationInfo;
  status=D3DKMTCreateAllocation(&request);
  result=record(b,AgxKmtNativeAllocate,status,TRUE,
      args->pAllocationInfo[0].hAllocation,1);
  if(SUCCEEDED(result)) {
    if(!args->pAllocationInfo[0].hAllocation || request.hResource) {
      b->OwnershipUncertain=TRUE; b->Ready=FALSE;
      return record(b,AgxKmtNativeAllocate,KMT_STATUS_INVALID_STATE,FALSE,
          args->pAllocationInfo[0].hAllocation,1);
    }
    args->hKMResource=request.hResource;
    ++b->Receipt.Allocations;
    printf("NATIVE_KMT_ALLOCATION: handle=%u class=%u bytes=%llu flags=0x%x\n",
        args->pAllocationInfo[0].hAllocation,description->ClassId,description->Allocation.Size,description->Flags);
  }
  return result;
}

static HRESULT APIENTRY lock_buffer(HANDLE context,D3DDDICB_LOCK *args) {
  AGX_KMT_NATIVE_BRIDGE *b=bridge(context);
  D3DKMT_LOCK request={0};
  HRESULT result;
  /* The existing native owner requests whole non-discard mappings. Preserve
   * these flags exactly; do not invent IgnoreSync/aperture/renaming semantics. */
  if(!b || !args || !owned(b,args->hAllocation) || !args->Flags.LockEntire ||
      (args->Flags.Value&~0x13u) || (args->Flags.ReadOnly && args->Flags.WriteOnly) ||
      args->NumPages || args->pPages || args->PrivateDriverData) return reject(b,AgxKmtNativeLock);
  result=make_resident(b,&args->hAllocation,1);
  if(FAILED(result)) return result;
  request.hDevice=b->DeviceHandle;request.hAllocation=args->hAllocation;
  request.Flags=args->Flags;request.PrivateDriverData=args->PrivateDriverData;
  request.NumPages=args->NumPages;request.pPages=args->pPages;
  result=record(b,AgxKmtNativeLock,D3DKMTLock(&request),TRUE,args->hAllocation,1);
  if(FAILED(result)) return result;
  if(request.hAllocation!=args->hAllocation) {
    /* No Discard was requested. Keep the actual changed handle visible in
     * diagnostics and stop rather than silently changing the owner identity. */
    b->OwnershipUncertain=TRUE; b->Ready=FALSE;
    (void)record(b,AgxKmtNativeLock,KMT_STATUS_INVALID_STATE,FALSE,request.hAllocation,1);
    return b->Receipt.Result;
  }
  if(!request.pData) {
    b->OwnershipUncertain=TRUE; b->Ready=FALSE;
    return reject(b,AgxKmtNativeLock);
  }
  args->pData=request.pData;args->GpuVirtualAddress=request.GpuVirtualAddress;
  ++b->Receipt.Locks;
  return S_OK;
}

static HRESULT APIENTRY unlock_buffer(HANDLE context,const D3DDDICB_UNLOCK *args) {
  AGX_KMT_NATIVE_BRIDGE *b=bridge(context);
  D3DKMT_UNLOCK request={0};
  HRESULT result;
  if(!b || !args || !args->NumAllocations || args->NumAllocations>ADMISSION_UMD_SCREEN_BUFFER_LIMIT ||
      !args->phAllocations) return reject(b,AgxKmtNativeUnlock);
  for(UINT i=0;i<args->NumAllocations;++i)
    if(!owned(b,args->phAllocations[i])) return reject(b,AgxKmtNativeUnlock);
  request.hDevice=b->DeviceHandle;request.NumAllocations=args->NumAllocations;request.phAllocations=args->phAllocations;
  result=record(b,AgxKmtNativeUnlock,D3DKMTUnlock(&request),TRUE,args->phAllocations[0],args->NumAllocations);
  if(SUCCEEDED(result)) b->Receipt.Unlocks+=args->NumAllocations;
  return result;
}

static HRESULT APIENTRY deallocate(HANDLE context,const D3DDDICB_DEALLOCATE *args) {
  AGX_KMT_NATIVE_BRIDGE *b=bridge(context);
  D3DKMT_DESTROYALLOCATION2 request={0};
  HRESULT result;
  if(!b || !args || args->hResource || !args->NumAllocations ||
      args->NumAllocations>ADMISSION_UMD_SCREEN_BUFFER_LIMIT || !args->HandleList)
    return reject(b,AgxKmtNativeDeallocate);
  if(b->OwnershipUncertain) return record(b,AgxKmtNativeDeallocate,KMT_STATUS_INVALID_STATE,FALSE,args->HandleList[0],args->NumAllocations);
  for(UINT i=0;i<args->NumAllocations;++i)
    if(!owned(b,args->HandleList[i])) return reject(b,AgxKmtNativeDeallocate);
  for(UINT i=0;i<args->NumAllocations;++i) {
    result=evict_reference(b,args->HandleList[i]);
    if(FAILED(result)) return result;
  }
  request.hDevice=b->DeviceHandle;request.phAllocationList=args->HandleList;
  request.AllocationCount=args->NumAllocations;
  request.Flags.AssumeNotInUse=0;request.Flags.SynchronousDestroy=1;
  result=record(b,AgxKmtNativeDeallocate,D3DKMTDestroyAllocation2(&request),TRUE,args->HandleList[0],args->NumAllocations);
  if(SUCCEEDED(result)) b->Receipt.Deallocations+=args->NumAllocations;
  return result;
}

static HRESULT APIENTRY render(HANDLE context,D3DDDICB_RENDER *args) {
  AGX_KMT_NATIVE_BRIDGE *b=bridge(context);
  D3DKMT_RENDER request={0};
  D3DKMT_HANDLE handles[ADMISSION_UMD_SCREEN_BUFFER_LIMIT];
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  HRESULT result;
  if(!b || !args || !b->ContextHandle || (UINT_PTR)args->hContext!=b->ContextHandle ||
      args->Flags.Value || args->BroadcastContextCount || args->NumPatchLocations ||
      args->pPrivateDriverData || args->PrivateDriverDataSize || !args->NumAllocations ||
      args->NumAllocations>ARRAYSIZE(handles) || args->NumAllocations>b->Device.AllocationListSize ||
      !b->Device.AllocationList || !args->CommandLength || args->CommandOffset>b->Device.CommandBufferSize ||
      args->CommandLength>b->Device.CommandBufferSize-args->CommandOffset)
    return reject(b,AgxKmtNativeRender);
  if(AppleAgxWin32CommandValidate((unsigned char *)b->Device.CommandBuffer+args->CommandOffset,
      args->CommandLength,b->Device.Win32Generation,args->NumAllocations,&view)!=AppleAgxWin32AbiSuccess ||
      view.Header->Version!=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH || !view.Draw || !view.NativeBatch)
    return reject(b,AgxKmtNativeRender);
  for(UINT i=0;i<args->NumAllocations;++i) handles[i]=b->Device.AllocationList[i].hAllocation;
  result=make_resident(b,handles,args->NumAllocations);
  if(FAILED(result)) return result;
  b->Receipt.CommandHash=view.Header->ContentHash;
  b->Receipt.Win32Generation=view.Header->Generation;
  b->Receipt.References=view.Header->ReferenceCount;
  b->Receipt.Relocations=view.Draw->RelocationCount;
  b->Receipt.CommandBytes=args->CommandLength;
  printf("NATIVE_KMT_COMMAND: hash=0x%016llx generation=%u references=%u relocations=%u bytes=%u context=%u\n",
      b->Receipt.CommandHash,b->Receipt.Win32Generation,b->Receipt.References,
      b->Receipt.Relocations,b->Receipt.CommandBytes,b->ContextHandle);
  request.hContext=b->ContextHandle;request.CommandOffset=args->CommandOffset;
  request.CommandLength=args->CommandLength;request.AllocationCount=args->NumAllocations;
  request.PatchLocationCount=args->NumPatchLocations;
  request.NewCommandBufferSize=args->NewCommandBufferSize;
  request.NewAllocationListSize=args->NewAllocationListSize;
  request.NewPatchLocationListSize=args->NewPatchLocationListSize;
  result=dump_then_render(b,(unsigned char *)b->Device.CommandBuffer+args->CommandOffset,
      args->CommandLength,&request,&NativeDumpOperations,D3DKMTRender);
  args->pNewCommandBuffer=request.pNewCommandBuffer;args->NewCommandBufferSize=request.NewCommandBufferSize;
  args->pNewAllocationList=request.pNewAllocationList;args->NewAllocationListSize=request.NewAllocationListSize;
  args->pNewPatchLocationList=request.pNewPatchLocationList;args->NewPatchLocationListSize=request.NewPatchLocationListSize;
  args->NewCommandBuffer=request.NewCommandBuffer;args->QueuedBufferCount=request.QueuedBufferCount;
  return result;
}

static HRESULT APIENTRY signal_event(HANDLE context,const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT2 *args) {
  AGX_KMT_NATIVE_BRIDGE *b=bridge(context);
  D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 request={0};
  D3DDDICB_SIGNALFLAGS allowed={0};
  allowed.EnqueueCpuEvent=1;
  if(!b || !args || !b->ContextHandle || (UINT_PTR)args->hContext!=b->ContextHandle ||
      args->ObjectCount || args->BroadcastContextCount || !args->CpuEventHandle ||
      args->Flags.Value!=allowed.Value) return reject(b,AgxKmtNativeSignal);
  request.hContext=b->ContextHandle;request.ObjectCount=args->ObjectCount;
  request.Flags=args->Flags;request.CpuEventHandle=args->CpuEventHandle;
  ++b->Receipt.Signals;
  return record(b,AgxKmtNativeSignal,D3DKMTSignalSynchronizationObject2(&request),TRUE,b->ContextHandle,0);
}

static VOID APIENTRY owner_error(D3D10DDI_HRTCORELAYER context,HRESULT error) {
  AGX_KMT_NATIVE_BRIDGE *b=bridge(context.handle);
  if(!b) return;
  (void)record(b,AgxKmtNativeOwnerError,KMT_STATUS_INVALID_STATE,FALSE,b->ContextHandle,0);
  b->Receipt.Result=error;
  printf("NATIVE_KMT_OWNER_ERROR: result=0x%08lx\n",(ULONG)error);
}

HRESULT AgxKmtNativeBridgeCreate(D3DKMT_HANDLE adapter,D3DKMT_HANDLE device,
    D3DKMT_HANDLE paging,volatile const UINT64 *pagingFence,DWORD timeout,
    AGX_KMT_NATIVE_BRIDGE **out) {
  AGX_KMT_NATIVE_BRIDGE *b;
  D3D10DDIARG_OPENADAPTER open={0};
  D3D10DDIARG_CREATEDEVICE create={0};
  HRESULT result;
  if(!out || *out || !adapter || !device || !paging || !pagingFence ||
      ((UINT_PTR)pagingFence&7u) || !timeout || timeout>60000u) return E_INVALIDARG;
  b=(AGX_KMT_NATIVE_BRIDGE *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*b));
  if(!b) return E_OUTOFMEMORY;
  b->Magic=KMT_NATIVE_MAGIC;b->Thread=GetCurrentThreadId();b->TimeoutMs=timeout;
  b->AdapterHandle=adapter;b->DeviceHandle=device;b->PagingHandle=paging;b->PagingFence=pagingFence;
  b->MakeResident=D3DKMTMakeResident;b->Evict=D3DKMTEvict;
  b->Receipt.Bytes=sizeof(b->Receipt);*out=b;
  b->AdapterCallbacks.pfnQueryAdapterInfoCb=query_adapter;
  b->DeviceCallbacks.pfnCreateContextCb=create_context;b->DeviceCallbacks.pfnDestroyContextCb=destroy_context;
  b->DeviceCallbacks.pfnAllocateCb=allocate;b->DeviceCallbacks.pfnDeallocateCb=deallocate;
  b->DeviceCallbacks.pfnLockCb=lock_buffer;b->DeviceCallbacks.pfnUnlockCb=unlock_buffer;
  b->DeviceCallbacks.pfnRenderCb=render;b->DeviceCallbacks.pfnSignalSynchronizationObject2Cb=signal_event;
  b->CoreCallbacks.pfnSetErrorCb=owner_error;
  open.hRTAdapter.handle=b;open.Interface=D3D10_0_DDI_INTERFACE_VERSION;
  open.Version=D3D10_0_DDI_VERSION_VISTA_GOLD;open.pAdapterCallbacks=&b->AdapterCallbacks;
  result=AdmissionUmdRuntimeAdapterInitialize(&b->Adapter,&open);
  if(FAILED(result)) return result;
  if(!b->Adapter.DeviceInfo.BootGeneration || b->Adapter.DeviceInfo.GpuGeneration!=13u ||
      b->Adapter.DeviceInfo.GpuVariant!=AgxWin32GpuG13G || b->Adapter.DeviceInfo.PageBytes!=0x4000u)
    return record(b,AgxKmtNativeQuery,KMT_STATUS_NOT_SUPPORTED,FALSE,adapter,1);
  printf("NATIVE_KMT_DEVICE_CONTRACT: boot=%u generation=%u variant=%u page=%u source=compiled_kmd_contract\n",
      b->Adapter.DeviceInfo.BootGeneration,b->Adapter.DeviceInfo.GpuGeneration,
      b->Adapter.DeviceInfo.GpuVariant,b->Adapter.DeviceInfo.PageBytes);
  create.hRTDevice.handle=b;create.hRTCoreLayer.handle=b;
  create.Interface=D3D10_0_DDI_INTERFACE_VERSION;create.Version=D3D10_0_DDI_VERSION_VISTA_GOLD;
  create.pKTCallbacks=&b->DeviceCallbacks;create.pUMCallbacks=&b->CoreCallbacks;
  /* Qualification publishes no D3D/DXGI DDI table or capability. The unused
   * DXGI callback table stays null-filled, never a successful Present stub. */
  create.DXGIBaseDDI.pDXGIBaseCallbacks=&b->DxgiCallbacks;
  result=AdmissionUmdRuntimeDeviceInitialize(&b->Device,&b->Adapter,&create);
  if(FAILED(result)) return result;
  b->Owner.Device=&b->Device;b->Owner.Backend=&b->Backend;
  AdmissionUmdAsahiOwnerOperations(&b->OwnerOperations);
  b->Ready=TRUE;
  return S_OK;
}

HRESULT AgxKmtNativeBridgeGetBinding(AGX_KMT_NATIVE_BRIDGE *b,AGX_KMT_NATIVE_BINDING *out) {
  if(!bridge(b) || !b->Ready || b->OwnershipUncertain || !out) return E_INVALIDARG;
  AGX_KMT_NATIVE_BINDING binding={&b->Device.Screen,&b->Backend,&b->OwnerOperations,
      &b->Owner,AdmissionUmdAsahiBatchOperations(),&b->Adapter.DeviceInfo,b->ContextHandle};
  *out=binding;
  return S_OK;
}

HRESULT AgxKmtNativeBridgeGetReceipt(const AGX_KMT_NATIVE_BRIDGE *b,AGX_KMT_NATIVE_RECEIPT *out) {
  if(!b || b->Magic!=KMT_NATIVE_MAGIC || !out) return E_INVALIDARG;
  *out=b->Receipt;
  return S_OK;
}

HRESULT AgxKmtNativeBridgeClose(AGX_KMT_NATIVE_BRIDGE **inout) {
  AGX_KMT_NATIVE_BRIDGE *b;
  HRESULT result;
  ULONG remaining=0;
  if(!inout || !*inout || !bridge(*inout)) return E_INVALIDARG;
  b=*inout;
  if(b->OwnershipUncertain) return HRESULT_FROM_WIN32(ERROR_BUSY);
  if(b->Backend.Native || b->Device.NativeBackendCount || b->Device.NativeBatchTransaction || b->Device.DrawSubmission)
    return HRESULT_FROM_WIN32(ERROR_BUSY);
  if(b->Device.Magic==ADMISSION_UMD_DEVICE_MAGIC) {
    /* Check retryable owner cleanup before the terminal runtime finalizer,
     * which intentionally clears its storage after reporting a terminal error. */
    result=AdmissionUmdScreenFinalize(&b->Device,&remaining);
    if(FAILED(result) || remaining) return FAILED(result)?result:E_FAIL;
    BOOL consumed=FALSE;
    result=AdmissionUmdRuntimeDeviceFinalize(&b->Device,&consumed);
    if(!consumed) return FAILED(result)?result:E_FAIL;
  }
  if(b->ContextHandle) {
    result=destroy_context_handle(b,b->ContextHandle);
    if(FAILED(result)) return result;
  }
  b->Magic=0;HeapFree(GetProcessHeap(),0,b);*inout=NULL;
  return S_OK;
}

#if defined(AGX_KMT_NATIVE_BRIDGE_TEST)
static UINT residency_test_make_calls,residency_test_evict_calls;
static BOOL residency_test_evict_fail;
static NTSTATUS APIENTRY residency_test_make(D3DDDI_MAKERESIDENT *args) {
  ++residency_test_make_calls;
  if(args->hPagingQueue!=5 || args->NumAllocations!=1 || args->AllocationList[0]!=7)
    return KMT_STATUS_INVALID_PARAMETER;
  args->PagingFenceValue=9;
  return (NTSTATUS)0x00000103L;
}
static NTSTATUS APIENTRY residency_test_evict(D3DKMT_EVICT *args) {
  ++residency_test_evict_calls;
  if(args->hDevice!=3 || args->NumAllocations!=1 || args->AllocationList[0]!=7)
    return KMT_STATUS_INVALID_PARAMETER;
  return residency_test_evict_fail ? KMT_STATUS_INVALID_STATE : 0;
}
unsigned AgxKmtNativeBridgeResidencyContractTest(void) {
  AGX_KMT_NATIVE_BRIDGE *b=(AGX_KMT_NATIVE_BRIDGE *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*b));
  volatile UINT64 pagingFence=8;
  unsigned errors=0;
  D3DKMT_HANDLE twice[2]={7,7};
  ADMISSION_UMD_SCREEN_BUFFER *slot;
  if(!b) return 1;
#define RESIDENCY_CHECK(x) do { if(!(x)) { ++errors;printf("NATIVE_KMT_RESIDENCY_TEST_FAIL: line=%u %s\n",(unsigned)__LINE__,#x); } } while(0)
  b->Magic=KMT_NATIVE_MAGIC;b->Thread=GetCurrentThreadId();b->DeviceHandle=3;b->PagingHandle=5;
  b->TimeoutMs=1;b->PagingFence=&pagingFence;b->MakeResident=residency_test_make;b->Evict=residency_test_evict;
  b->Device.Magic=ADMISSION_UMD_DEVICE_MAGIC;InitializeSRWLock(&b->Device.ScreenBufferLock);
  slot=&b->Device.ScreenBuffers[0];slot->Active=TRUE;slot->KernelAllocation=7;
  residency_test_make_calls=residency_test_evict_calls=0;residency_test_evict_fail=FALSE;
  RESIDENCY_CHECK(make_resident_one(b,7)==HRESULT_FROM_WIN32(WAIT_TIMEOUT));
  RESIDENCY_CHECK(slot->KmtResidencyHeld && slot->KmtPagingFence==9 && residency_test_make_calls==1);
  RESIDENCY_CHECK(make_resident_one(b,7)==HRESULT_FROM_WIN32(WAIT_TIMEOUT));
  RESIDENCY_CHECK(residency_test_make_calls==1);
  pagingFence=9;
  RESIDENCY_CHECK(make_resident(b,twice,2)==S_OK && residency_test_make_calls==1);
  slot->Mapped=TRUE;
  RESIDENCY_CHECK(FAILED(evict_reference(b,7)) && residency_test_evict_calls==0);
  slot->Mapped=FALSE;slot->SubmissionHolds=1;
  RESIDENCY_CHECK(FAILED(evict_reference(b,7)) && residency_test_evict_calls==0);
  slot->SubmissionHolds=0;slot->SourceHolds=1;
  RESIDENCY_CHECK(FAILED(evict_reference(b,7)) && residency_test_evict_calls==0);
  slot->SourceHolds=0;residency_test_evict_fail=TRUE;
  RESIDENCY_CHECK(FAILED(evict_reference(b,7)) && slot->KmtResidencyHeld && slot->KmtPagingFence==9);
  residency_test_evict_fail=FALSE;
  RESIDENCY_CHECK(evict_reference(b,7)==S_OK && !slot->KmtResidencyHeld && !slot->KmtPagingFence);
  RESIDENCY_CHECK(evict_reference(b,7)==S_OK && residency_test_evict_calls==2);
  RESIDENCY_CHECK(b->Receipt.ResidencyAcquires==1 && b->Receipt.ResidencyReuses==3 && b->Receipt.ResidencyEvicts==1);
  /* Failed destruction can be retried after a successful Evict without
   * another decrement. A later real use reacquires exactly once. */
  RESIDENCY_CHECK(make_resident_one(b,7)==S_OK && residency_test_make_calls==2);
  RESIDENCY_CHECK(evict_reference(b,7)==S_OK && residency_test_evict_calls==3);
  RESIDENCY_CHECK(b->Receipt.ResidencyAcquires==2 && b->Receipt.ResidencyEvicts==2);
  printf("NATIVE_KMT_RESIDENCY_TEST: errors=%u acquires=%u reuses=%u evicts=%u\n",errors,
      b->Receipt.ResidencyAcquires,b->Receipt.ResidencyReuses,b->Receipt.ResidencyEvicts);
#undef RESIDENCY_CHECK
  HeapFree(GetProcessHeap(),0,b);
  return errors;
}

typedef struct _AGX_KMT_NATIVE_DUMP_TEST_STATE {
  AGX_KMT_NATIVE_DUMP_STAGE Failure;
  BOOL ShortWrite;
  UINT Order,Violations,RenderCalls;
  const void *Command;
  DWORD CommandBytes;
} AGX_KMT_NATIVE_DUMP_TEST_STATE;

static AGX_KMT_NATIVE_DUMP_TEST_STATE DumpTest;

static HANDLE WINAPI dump_test_create(LPCWSTR name,DWORD access,DWORD share,
    LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD attributes,HANDLE templateFile) {
  DumpTest.Order=DumpTest.Order*10u+1u;
  if(wcscmp(name,L"native-kmt-command-1234abcd-0123456789abcdef.bin") ||
      access!=GENERIC_WRITE || share!=FILE_SHARE_READ || security ||
      disposition!=CREATE_NEW || attributes!=FILE_ATTRIBUTE_NORMAL || templateFile)
    ++DumpTest.Violations;
  if(DumpTest.Failure==AgxKmtNativeDumpCreate) {
    SetLastError(ERROR_ACCESS_DENIED);
    return INVALID_HANDLE_VALUE;
  }
  return (HANDLE)(UINT_PTR)0x1234u;
}

static BOOL WINAPI dump_test_write(HANDLE file,LPCVOID data,DWORD bytes,
    LPDWORD written,LPOVERLAPPED overlapped) {
  DumpTest.Order=DumpTest.Order*10u+2u;
  if(file!=(HANDLE)(UINT_PTR)0x1234u || data!=DumpTest.Command ||
      bytes!=DumpTest.CommandBytes || !written || overlapped)
    ++DumpTest.Violations;
  if(DumpTest.Failure==AgxKmtNativeDumpWrite) {
    *written=0;
    SetLastError(ERROR_WRITE_FAULT);
    return FALSE;
  }
  *written=DumpTest.ShortWrite ? bytes-1u : bytes;
  return TRUE;
}

static BOOL WINAPI dump_test_flush(HANDLE file) {
  DumpTest.Order=DumpTest.Order*10u+3u;
  if(file!=(HANDLE)(UINT_PTR)0x1234u) ++DumpTest.Violations;
  if(DumpTest.Failure==AgxKmtNativeDumpFlush) {
    SetLastError(ERROR_GEN_FAILURE);
    return FALSE;
  }
  return TRUE;
}

static BOOL WINAPI dump_test_close(HANDLE file) {
  DumpTest.Order=DumpTest.Order*10u+4u;
  if(file!=(HANDLE)(UINT_PTR)0x1234u) ++DumpTest.Violations;
  if(DumpTest.Failure==AgxKmtNativeDumpClose) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  return TRUE;
}

static NTSTATUS APIENTRY dump_test_render(D3DKMT_RENDER *request) {
  ++DumpTest.RenderCalls;
  if(!request || request->hContext!=77u || request->AllocationCount!=1u)
    ++DumpTest.Violations;
  return 0;
}

unsigned AgxKmtNativeBridgeCommandDumpContractTest(void) {
  static const struct {
    AGX_KMT_NATIVE_DUMP_STAGE Failure;
    BOOL ShortWrite;
    AGX_KMT_NATIVE_DUMP_STAGE ExpectedStage;
    DWORD ExpectedError,ExpectedBytes;
    UINT ExpectedOrder,ExpectedRenderCalls;
  } Cases[]={
    {AgxKmtNativeDumpNone,FALSE,AgxKmtNativeDumpComplete,ERROR_SUCCESS,4u,1234u,1u},
    {AgxKmtNativeDumpCreate,FALSE,AgxKmtNativeDumpCreate,ERROR_ACCESS_DENIED,0u,1u,0u},
    {AgxKmtNativeDumpWrite,FALSE,AgxKmtNativeDumpWrite,ERROR_WRITE_FAULT,0u,124u,0u},
    {AgxKmtNativeDumpNone,TRUE,AgxKmtNativeDumpWrite,ERROR_WRITE_FAULT,3u,124u,0u},
    {AgxKmtNativeDumpFlush,FALSE,AgxKmtNativeDumpFlush,ERROR_GEN_FAILURE,4u,1234u,0u},
    {AgxKmtNativeDumpClose,FALSE,AgxKmtNativeDumpClose,ERROR_INVALID_HANDLE,4u,1234u,0u}
  };
  static const AGX_KMT_NATIVE_DUMP_OPS Operations={
    dump_test_create,dump_test_write,dump_test_flush,dump_test_close
  };
  static const unsigned char Command[4]={0x41u,0x47u,0x58u,0x21u};
  unsigned errors=0;
#define DUMP_CHECK(x) do { if(!(x)) { ++errors;printf("NATIVE_KMT_DUMP_TEST_FAIL: line=%u case=%u %s\n",(unsigned)__LINE__,i,#x); } } while(0)
  for(UINT i=0;i<ARRAYSIZE(Cases);++i) {
    AGX_KMT_NATIVE_BRIDGE b={0};
    D3DKMT_RENDER request={0};
    HRESULT result;
    b.ContextHandle=77u;
    b.Receipt.Bytes=sizeof(b.Receipt);
    b.Receipt.CommandHash=0x0123456789abcdefull;
    b.Receipt.Win32Generation=0x1234abcdu;
    request.hContext=77u;
    request.AllocationCount=1u;
    ZeroMemory(&DumpTest,sizeof(DumpTest));
    DumpTest.Failure=Cases[i].Failure;
    DumpTest.ShortWrite=Cases[i].ShortWrite;
    DumpTest.Command=Command;
    DumpTest.CommandBytes=sizeof(Command);
    result=dump_then_render(&b,Command,sizeof(Command),&request,&Operations,dump_test_render);
    DUMP_CHECK(DumpTest.Order==Cases[i].ExpectedOrder);
    DUMP_CHECK(DumpTest.Violations==0u);
    DUMP_CHECK(DumpTest.RenderCalls==Cases[i].ExpectedRenderCalls);
    DUMP_CHECK(b.Receipt.CommandDumpStage==Cases[i].ExpectedStage);
    DUMP_CHECK(b.Receipt.CommandDumpError==Cases[i].ExpectedError);
    DUMP_CHECK(b.Receipt.CommandDumpBytes==Cases[i].ExpectedBytes);
    if(Cases[i].ExpectedError) {
      DUMP_CHECK(result==HRESULT_FROM_WIN32(Cases[i].ExpectedError));
      DUMP_CHECK(b.Receipt.Stage==AgxKmtNativeCommandDump);
      DUMP_CHECK(!b.Receipt.CalledKmt && b.Receipt.Renders==0u);
    } else {
      DUMP_CHECK(result==S_OK);
      DUMP_CHECK(b.Receipt.Stage==AgxKmtNativeRender);
      DUMP_CHECK(b.Receipt.Renders==1u);
    }
  }
  printf("NATIVE_KMT_COMMAND_DUMP_TEST: errors=%u cases=%u\n",errors,(unsigned)ARRAYSIZE(Cases));
#undef DUMP_CHECK
  return errors;
}
#endif
