#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)
extern "C" {
#include "umd_internal.h"
#include "umd_asahi_owner.h"
#include "umd_asahi_batch_adapter.h"
}
#include "agx_win32_asahi_scene.h"
#include "agx_d3d10_windows.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"

thread_local AgxD3d10RefusalScope *AgxD3d10RefusalScope::Current=nullptr;
BOOL AgxD3d10WindowsDiagnosticRefusal(HRESULT status) {
  AgxD3d10RefusalScope *scope=AgxD3d10RefusalScope::Current;
  if(!scope || !scope->Name || SUCCEEDED(status)) return FALSE;
  if(!scope->Emitted) {
    scope->Emitted=true;
    AdmissionUmdDiagnostic(scope->Name,status,scope->Values,scope->Count);
  }
  return TRUE;
}

VOID AgxD3d10WindowsDiagnostic(PCSTR Stage,HRESULT Status,
                               const UINT *Values,UINT Count) {
  AdmissionUmdDiagnostic(Stage,Status,Values,Count);
}

VOID AgxD3d10WindowsDiagnosticSetError(
    PCSTR function,UINT line,HRESULT status) {
  char stage[192];
  if(!function || SUCCEEDED(status)) return;
  if(_snprintf_s(stage,sizeof(stage),_TRUNCATE,
      "reject-seterror fn=%s line=%u",function,line)<0) return;
  AdmissionUmdDiagnostic(stage,status,NULL,0u);
}

VOID AgxD3d10WindowsDiagnosticResource(
    PCSTR Stage,const D3D10DDIARG_CREATERESOURCE *r) {
  UINT values[16]={0};
  if(r) {
    values[0]=(UINT)r->Format;values[1]=(UINT)r->ResourceDimension;
    values[2]=(UINT)r->Usage;values[3]=r->BindFlags;
    values[4]=r->MapFlags;values[5]=r->MiscFlags;
    values[6]=r->MipLevels;values[7]=r->ArraySize;
    values[8]=r->SampleDesc.Count;values[9]=r->SampleDesc.Quality;
    values[10]=r->pPrimaryDesc!=NULL;values[11]=r->pInitialDataUP!=NULL;
    if(r->pMipInfoList && r->MipLevels) {
      values[12]=r->pMipInfoList[0].TexelWidth;
      values[13]=r->pMipInfoList[0].TexelHeight;
      values[14]=r->pMipInfoList[0].TexelDepth;
    }
    values[15]=r->pMipInfoList!=NULL;
  }
  AdmissionUmdDiagnostic(Stage,r?S_OK:E_INVALIDARG,values,16u);
}

VOID AgxD3d10WindowsDiagnosticBufferUsage(
    const D3D10DDIARG_CREATERESOURCE *r) {
  UINT values[5]={0};
  if(r) {
    values[0]=r->Usage;
    values[1]=r->BindFlags;
    values[2]=r->MapFlags;
    values[3]=r->MiscFlags;
    if(r->pMipInfoList && r->MipLevels)
      values[4]=r->pMipInfoList[0].TexelWidth;
  }
  AdmissionUmdDiagnostic("reject-buffer-usage",E_NOTIMPL,values,ARRAYSIZE(values));
}

enum AGX_D3D10_WINDOWS_DEVICE_STAGE {
  AgxD3d10DeviceAllocated,
  AgxD3d10DeviceRuntimeReady,
  AgxD3d10DeviceNativeScreenReady,
  AgxD3d10DeviceNativeContextReady,
  AgxD3d10DeviceReady,
  AgxD3d10DeviceClosing,
  AgxD3d10DeviceNativeContextReleased,
  AgxD3d10DeviceNativeScreenReleased,
  AgxD3d10DeviceRuntimeReleased,
  AgxD3d10DeviceFreed
};

typedef struct _AGX_D3D10_WINDOWS_TERMINAL {
  struct _AGX_D3D10_WINDOWS_TERMINAL *Next;
  HRESULT Error;
  ULONG Stage,ActiveBuffersAndQueryMarkers,NativeContexts,LiveBos;
  BOOL KernelQuiesced;
} AGX_D3D10_WINDOWS_TERMINAL;
enum {
  AgxD3d10TerminalCountMask=0xffffu,
  AgxD3d10TerminalQueryShift=16u
};
static_assert(ADMISSION_UMD_SCREEN_BUFFER_LIMIT<=AgxD3d10TerminalCountMask,
              "terminal active-buffer field is too small");
static_assert(ADMISSION_UMD_SCREEN_FENCE_LIMIT<=AgxD3d10TerminalCountMask,
              "terminal query-marker field is too small");

struct AGX_D3D10_WINDOWS_ADAPTER {
  ADMISSION_UMD_ADAPTER Runtime;
  SRWLOCK Lock;
  ULONG Devices;
  AGX_D3D10_WINDOWS_DEVICE *Owners;
  AGX_D3D10_WINDOWS_TERMINAL *Terminal;
};
struct AGX_D3D10_WINDOWS_DEVICE {
  ADMISSION_UMD_DEVICE Runtime;
  ADMISSION_UMD_ASAHI_OWNER Owner;
  AGX_WIN32_ASAHI_BACKEND Backend;
  AGX_WIN32_ASAHI_OWNER_OPS OwnerOperations;
  struct pipe_screen *Screen;
  struct pipe_context *Context;
  AGX_D3D10_WINDOWS_ADAPTER *Adapter;
  AGX_D3D10_WINDOWS_DEVICE *Next;
  AGX_D3D10_WINDOWS_DEVICE_STAGE Stage;
  HRESULT InitialFailure;
  HRESULT CleanupStatus;
  BOOL KernelQuiesced;
  AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *PendingPresentations;
};
struct AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE {
  AGX_D3D10_WINDOWS_DEVICE *Device;
  ADMISSION_UMD_RESOURCE Resource;
  AGX_WIN32_SCREEN_BUFFER RenderBuffer;
  struct pipe_resource *RenderResource;
  AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *Next;
};

VOID AgxD3d10WindowsDiagnosticState(AGX_D3D10_WINDOWS_DEVICE *d,PCSTR stage) {
  if(!d || d->Stage!=AgxD3d10DeviceReady) return;
  UINT state[16],bindings[16];
  AgxWin32AsahiContextDiagnostic(d->Context,state,bindings);
  AdmissionUmdDiagnostic(stage,d->Runtime.LastScreenError,state,16);
  AdmissionUmdDiagnostic("native-bindings",S_OK,bindings,16);
  ADMISSION_UMD_ASAHI_BATCH *b=(ADMISSION_UMD_ASAHI_BATCH *)d->Runtime.NativeBatchTransaction;
  UINT runtime[12]={ (UINT)d->Runtime.DrawTerminal,d->Runtime.CommandBufferSize,
      d->Runtime.AllocationListSize,d->Runtime.PatchListSize,b!=NULL,
      b?(UINT)b->Phase:0,b?(UINT)b->Submission.Phase:0,
      b?(UINT)b->Submission.RenderStatus:0,b?(UINT)b->Submission.PostStatus:0,
      b?b->Submission.CommandBytes:0,b?b->Submission.Count:0,
      b?b->Submission.Fence:0 };
  AdmissionUmdDiagnostic("native-runtime",d->Runtime.LastScreenError,runtime,12);
}
static BOOL presentation_linear_format(D3DDDIFORMAT windowsFormat,
                                       AGX_WIN32_ASAHI_LINEAR_FORMAT *format) {
  if(!format) return FALSE;
  switch(windowsFormat) {
  case D3DDDIFMT_A8R8G8B8:
    *format=AgxWin32AsahiLinearFormatBgra8Unorm;return TRUE;
  case D3DDDIFMT_A8B8G8R8:
    *format=AgxWin32AsahiLinearFormatRgba8Unorm;return TRUE;
  default: return FALSE;
  }
}

static BOOL presentation_pipe_format(const struct pipe_resource *resource,
                                     enum pipe_format *format) {
  if(!resource || !format) return FALSE;
  switch(resource->format) {
  case PIPE_FORMAT_B8G8R8A8_UNORM:
  case PIPE_FORMAT_R8G8B8A8_UNORM:
    *format=resource->format;return TRUE;
  default: return FALSE;
  }
}

static BOOL collect_presentations(AGX_D3D10_WINDOWS_DEVICE *device) {
  AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE **link=&device->PendingPresentations;
  while(*link) {
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *record=*link;
    (void)AgxWin32AsahiCollect(&device->Backend);
    if(AdmissionUmdScreenAllocationRegistered(&device->Runtime,
                                               record->Resource.KernelAllocation)) {
      link=&record->Next;continue;
    }
    D3D10DDI_HDEVICE deviceHandle={0};D3D10DDI_HRESOURCE resourceHandle={0};
    deviceHandle.pDrvPrivate=&device->Runtime;
    resourceHandle.pDrvPrivate=&record->Resource;
    AdmissionUmdDestroyResource(deviceHandle,resourceHandle);
    *link=record->Next;
    HeapFree(GetProcessHeap(),0,record);
  }
  return device->PendingPresentations==NULL;
}

static HRESULT attach_presentation_render_resource(
    AGX_D3D10_WINDOWS_DEVICE *device,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *record) {
  const ADMISSION_ALLOCATION_DESCRIPTION *desc=&record->Resource.DirectFlip.Allocation;
  AGX_WIN32_ASAHI_LINEAR_FORMAT format;
  if(!presentation_linear_format((D3DDDIFORMAT)desc->Format,&format))
    return E_INVALIDARG;
  const APPLE_AGX_U32 access=AppleAgxWin32BufferCpuRead|
      AppleAgxWin32BufferCpuWrite|AppleAgxWin32BufferGpuRead|
      AppleAgxWin32BufferGpuWrite;
  HRESULT result=AdmissionUmdScreenAdoptAllocation(&device->Runtime,
      record->Resource.KernelAllocation,desc->Size,device->Runtime.Screen.Info.PageBytes,
      AgxWin32BufferClassGeneral,access,&record->RenderBuffer);
  if(FAILED(result)) return result;
  record->RenderResource=AgxWin32AsahiImportLinearColor32(device->Screen,
      &record->RenderBuffer,desc->Width,desc->Height,desc->Pitch,desc->Size,format);
  if(!record->RenderResource) {
    (void)AgxWin32ScreenDestroyBuffer(&device->Runtime.Screen,&record->RenderBuffer);
    ZeroMemory(&record->RenderBuffer,sizeof(record->RenderBuffer));
    return FAILED(device->Runtime.LastScreenError)?device->Runtime.LastScreenError:E_FAIL;
  }
  return S_OK;
}

static void unlink_owner(AGX_D3D10_WINDOWS_DEVICE *owner) {
  if(!owner || !owner->Adapter) return;
  AGX_D3D10_WINDOWS_ADAPTER *adapter=owner->Adapter;
  AcquireSRWLockExclusive(&adapter->Lock);
  AGX_D3D10_WINDOWS_DEVICE **link=&adapter->Owners;
  while(*link && *link!=owner) link=&(*link)->Next;
  if(*link==owner) *link=owner->Next;
  if(adapter->Devices) --adapter->Devices;
  ReleaseSRWLockExclusive(&adapter->Lock);
}

static void unlink_and_free(AGX_D3D10_WINDOWS_DEVICE **inout) {
  if(!inout || !*inout) return;
  AGX_D3D10_WINDOWS_DEVICE *owner=*inout;
  unlink_owner(owner);
  owner->Stage=AgxD3d10DeviceFreed;
  HeapFree(GetProcessHeap(),0,owner);
  *inout=NULL;
}

static HRESULT native_close_error(const AGX_D3D10_WINDOWS_DEVICE *owner) {
  return FAILED(owner->Runtime.LastScreenError) ? owner->Runtime.LastScreenError
                                                : HRESULT_FROM_WIN32(ERROR_BUSY);
}

static HRESULT close_device(AGX_D3D10_WINDOWS_DEVICE **inout) {
  AGX_D3D10_WINDOWS_DEVICE *owner=*inout;
  BOOL consumed=FALSE;
  HRESULT result;
  if(owner->Stage<AgxD3d10DeviceRuntimeReady) {
    unlink_and_free(inout);
    return S_OK;
  }
  if(owner->Stage<AgxD3d10DeviceClosing) owner->Stage=AgxD3d10DeviceClosing;
  owner->Backend.Closing=1;
  if(owner->Stage==AgxD3d10DeviceClosing) {
    if(owner->Backend.ContextCount>(owner->Context?1u:0u))
      return owner->CleanupStatus=HRESULT_FROM_WIN32(ERROR_BUSY);
    owner->Runtime.LastScreenError=S_OK;
    if(owner->Context && !AgxWin32AsahiContextDestroy(owner->Context))
      return owner->CleanupStatus=native_close_error(owner);
    owner->Context=NULL;
    if(!collect_presentations(owner))
      return owner->CleanupStatus=HRESULT_FROM_WIN32(ERROR_BUSY);
    owner->Stage=AgxD3d10DeviceNativeContextReleased;
  }
  if(owner->Stage==AgxD3d10DeviceNativeContextReleased) {
    owner->Runtime.LastScreenError=S_OK;
    if(owner->Screen && !AgxWin32AsahiScreenDestroy(owner->Screen))
      return owner->CleanupStatus=native_close_error(owner);
    owner->Screen=NULL;
    owner->Stage=AgxD3d10DeviceNativeScreenReleased;
  }
  if(owner->Stage==AgxD3d10DeviceNativeScreenReleased) {
    result=AdmissionUmdScreenBeginClose(&owner->Runtime);
    if(FAILED(result)) return owner->CleanupStatus=result;
    result=AdmissionUmdRuntimeDeviceFinalize(&owner->Runtime,&consumed);
    if(!consumed) return owner->CleanupStatus=FAILED(result)?result:E_FAIL;
    owner->Stage=AgxD3d10DeviceRuntimeReleased;
    owner->CleanupStatus=result;
  }
  if(owner->Stage==AgxD3d10DeviceRuntimeReleased) {
    result=owner->CleanupStatus;
    unlink_and_free(inout);
    return result;
  }
  return owner->CleanupStatus=E_FAIL;
}

HRESULT AgxD3d10WindowsOpenAdapter(const D3D10DDIARG_OPENADAPTER *Args,
                                  AGX_D3D10_WINDOWS_ADAPTER **Adapter) {
  if (Adapter == NULL || *Adapter != NULL) return E_INVALIDARG;
  AGX_D3D10_WINDOWS_ADAPTER *owner =
      (AGX_D3D10_WINDOWS_ADAPTER *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                           sizeof(*owner));
  if (owner == NULL) return E_OUTOFMEMORY;
  HRESULT result = AdmissionUmdRuntimeAdapterInitialize(&owner->Runtime, Args);
  if (FAILED(result)) { HeapFree(GetProcessHeap(), 0, owner); return result; }
  InitializeSRWLock(&owner->Lock);
  *Adapter = owner;
  return S_OK;
}

HRESULT AgxD3d10WindowsCloseAdapter(AGX_D3D10_WINDOWS_ADAPTER **Adapter) {
  if (Adapter == NULL || *Adapter == NULL) return E_INVALIDARG;
  AGX_D3D10_WINDOWS_ADAPTER *owner = *Adapter;
  AcquireSRWLockExclusive(&owner->Lock);
  if (owner->Devices != 0u || owner->Owners != NULL) {
    ReleaseSRWLockExclusive(&owner->Lock);
    return HRESULT_FROM_WIN32(ERROR_BUSY);
  }
  ReleaseSRWLockExclusive(&owner->Lock);
  while(owner->Terminal) {
    AGX_D3D10_WINDOWS_TERMINAL *record=owner->Terminal;
    owner->Terminal=record->Next;
    HeapFree(GetProcessHeap(),0,record);
  }
  /* As with the runtime adapter handle, the caller serializes CloseAdapter
   * against new calls using that handle. Device callbacks run outside Lock. */
  HeapFree(GetProcessHeap(), 0, owner);
  *Adapter = NULL;
  return S_OK;
}

HRESULT AgxD3d10WindowsCreateDevice(AGX_D3D10_WINDOWS_ADAPTER *Adapter,
                                   const D3D10DDIARG_CREATEDEVICE *Args,
                                   AGX_D3D10_WINDOWS_DEVICE **Device) {
  if (Adapter == NULL || Device == NULL || *Device != NULL) return E_INVALIDARG;
  AGX_D3D10_WINDOWS_DEVICE *owner =
      (AGX_D3D10_WINDOWS_DEVICE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                          sizeof(*owner));
  if (owner == NULL) return E_OUTOFMEMORY;
  owner->Adapter=Adapter;
  owner->Stage=AgxD3d10DeviceAllocated;
  owner->InitialFailure=S_OK;
  owner->CleanupStatus=S_OK;
  AcquireSRWLockExclusive(&Adapter->Lock);
  if (Adapter->Devices == ~(ULONG)0u) {
    ReleaseSRWLockExclusive(&Adapter->Lock);
    HeapFree(GetProcessHeap(), 0, owner);
    return E_OUTOFMEMORY;
  }
  ++Adapter->Devices;
  owner->Next=Adapter->Owners;
  Adapter->Owners=owner;
  ReleaseSRWLockExclusive(&Adapter->Lock);
  HRESULT result = AdmissionUmdRuntimeDeviceInitialize(&owner->Runtime,
                                                       &Adapter->Runtime, Args);
  if(FAILED(result)) goto failed;
  owner->Stage=AgxD3d10DeviceRuntimeReady;
  owner->Owner.Device=&owner->Runtime;
  owner->Owner.Backend=&owner->Backend;
  AdmissionUmdAsahiOwnerOperations(&owner->OwnerOperations);
  owner->Screen=AgxWin32AsahiScreenCreateForWindows(&owner->Backend,&owner->Runtime.Screen,
      &owner->OwnerOperations,&owner->Owner,AdmissionUmdAsahiBatchOperations());
  if(!owner->Screen) {
    owner->Screen=AgxWin32AsahiScreenRecover(&owner->Backend);
    if(owner->Screen) owner->Stage=AgxD3d10DeviceNativeScreenReady;
    result=FAILED(owner->Runtime.LastScreenError)?owner->Runtime.LastScreenError:E_OUTOFMEMORY;
    goto failed;
  }
  owner->Stage=AgxD3d10DeviceNativeScreenReady;
  owner->Context=AgxWin32AsahiContextCreate(owner->Screen,&owner->Owner);
  if(!owner->Context) { result=E_OUTOFMEMORY;goto failed; }
  owner->Stage=AgxD3d10DeviceNativeContextReady;
  owner->Stage=AgxD3d10DeviceReady;
  *Device = owner;
  return S_OK;
failed:
  owner->InitialFailure=result;
  {
    AGX_D3D10_WINDOWS_DEVICE *cleanup=owner;
    HRESULT cleanupResult=close_device(&cleanup);
    if(cleanup) {
      cleanup->CleanupStatus=cleanupResult;
      *Device=cleanup;
    }
  }
  return result;
}

HRESULT AgxD3d10WindowsCloseDevice(AGX_D3D10_WINDOWS_DEVICE **Device) {
  if (Device == NULL || *Device == NULL) return E_INVALIDARG;
  return close_device(Device);
}

static HRESULT terminalize_device(AGX_D3D10_WINDOWS_DEVICE **inout,HRESULT error) {
  AGX_D3D10_WINDOWS_DEVICE *owner=*inout;
  AGX_D3D10_WINDOWS_ADAPTER *adapter=owner->Adapter;
  ULONG stage=owner->Stage,activeBuffers=0,queryMarkers=0;
  ULONG nativeContexts=owner->Backend.ContextCount,liveBos=owner->Backend.LiveBos;
  BOOL kernelQuiesced=owner->KernelQuiesced;
  static_assert(sizeof(*owner)>=sizeof(AGX_D3D10_WINDOWS_TERMINAL),
                "terminal record must fit the consumed owner allocation");
  if(SUCCEEDED(error)) error=E_FAIL;
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i)
    if(owner->Runtime.ScreenBuffers[i].Active) ++activeBuffers;
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_FENCE_LIMIT;++i)
    if(owner->Runtime.ScreenFences[i].Active &&
       owner->Runtime.ScreenFences[i].Kind!=AdmissionUmdFenceDraw)
      ++queryMarkers;
  ULONG packedCounts=0;
  if(activeBuffers<=AgxD3d10TerminalCountMask &&
     queryMarkers<=AgxD3d10TerminalCountMask) {
    packedCounts=activeBuffers|
        (queryMarkers<<AgxD3d10TerminalQueryShift);
  } else {
    error=E_FAIL;
  }
  AdmissionUmdSetError(&owner->Runtime,error);
  unlink_owner(owner);
  ZeroMemory(owner,sizeof(*owner));
  AGX_D3D10_WINDOWS_TERMINAL *record=(AGX_D3D10_WINDOWS_TERMINAL *)owner;
  record->Error=error;record->Stage=stage;
  record->ActiveBuffersAndQueryMarkers=packedCounts;
  record->NativeContexts=nativeContexts;record->LiveBos=liveBos;
  record->KernelQuiesced=kernelQuiesced;
  AcquireSRWLockExclusive(&adapter->Lock);
  record->Next=adapter->Terminal;
  adapter->Terminal=record;
  ReleaseSRWLockExclusive(&adapter->Lock);
  *inout=NULL;
  return error;
}

HRESULT AgxD3d10WindowsDestroyDeviceDdi(AGX_D3D10_WINDOWS_DEVICE **Device,
                                       BOOL *Consumed) {
  AGX_D3D10_WINDOWS_DEVICE *owner;
  BOOL destroyed=FALSE;
  HRESULT result;
  if(Consumed) *Consumed=FALSE;
  if(!Device || !*Device || !Consumed) return E_INVALIDARG;
  owner=*Device;
  if(owner->Stage<AgxD3d10DeviceClosing) owner->Stage=AgxD3d10DeviceClosing;
  owner->Backend.Closing=1;
  if(owner->Backend.ContextCount>(owner->Context?1u:0u))
    return terminalize_device(Device,HRESULT_FROM_WIN32(ERROR_BUSY));
  result=AdmissionUmdRuntimeDeviceDestroyKernelContext(&owner->Runtime,&destroyed);
  if(FAILED(result) || !destroyed)
    return terminalize_device(Device,FAILED(result)?result:E_FAIL);
  owner->KernelQuiesced=TRUE;
  if(owner->Context && !AgxWin32AsahiContextRetire(owner->Context,INFINITE))
    return terminalize_device(Device,E_FAIL);
  result=close_device(Device);
  if(*Device) return terminalize_device(Device,FAILED(result)?result:E_FAIL);
  *Consumed=TRUE;
  return result;
}

struct pipe_context *AgxD3d10WindowsContext(AGX_D3D10_WINDOWS_DEVICE *Device) {
  return Device != NULL && Device->Stage==AgxD3d10DeviceReady ? Device->Context : NULL;
}

BOOL AgxD3d10WindowsIdentity(AGX_D3D10_WINDOWS_DEVICE *Device,
                             ULONGLONG *OwnerCookie,
                             ULONG *DeviceGeneration) {
  if(!Device || Device->Stage!=AgxD3d10DeviceReady ||
     !OwnerCookie || !DeviceGeneration || !Device->Runtime.OwnerCookie ||
     !Device->Runtime.Win32Generation) return FALSE;
  *OwnerCookie=Device->Runtime.OwnerCookie;
  *DeviceGeneration=Device->Runtime.Win32Generation;
  return TRUE;
}

HRESULT AgxD3d10WindowsFlushStatus(AGX_D3D10_WINDOWS_DEVICE *Device) {
  if(!Device || Device->Stage!=AgxD3d10DeviceReady) return E_INVALIDARG;
  AgxD3d10WindowsDiagnosticState(Device,"flush-state");
  ADMISSION_UMD_ASAHI_BATCH *batch=
      (ADMISSION_UMD_ASAHI_BATCH *)Device->Runtime.NativeBatchTransaction;
  if(batch && (FAILED(batch->Submission.RenderStatus) ||
               FAILED(batch->Submission.PostStatus)))
    return FAILED(batch->Submission.PostStatus)?batch->Submission.PostStatus:
        batch->Submission.RenderStatus;
  if(!Device->Backend.Failed && !Device->Runtime.DrawTerminal) return S_OK;
  return FAILED(Device->Runtime.LastScreenError)?
      Device->Runtime.LastScreenError:E_FAIL;
}

static HRESULT flush_retire(AGX_D3D10_WINDOWS_DEVICE *Device,DWORD timeout) {
  if(!Device || Device->Stage!=AgxD3d10DeviceReady || !Device->Context)
    return E_INVALIDARG;
  Device->Context->flush(Device->Context,NULL,0);
  HRESULT result=AgxD3d10WindowsFlushStatus(Device);
  if(FAILED(result)) return result;
  if(!AgxWin32AsahiContextRetire(Device->Context,timeout)) {
    result=FAILED(Device->Runtime.LastScreenError)?
        Device->Runtime.LastScreenError:HRESULT_FROM_WIN32(ERROR_BUSY);
    /* Only an unsignalled fence in a zero-time poll is a nonblocking retry.
     * Preserve real errors and keep all submission holds until completion. */
    if(timeout==0 && result==HRESULT_FROM_WIN32(ERROR_TIMEOUT))
      return DXGI_DDI_ERR_WASSTILLDRAWING;
    return result;
  }
  if(!collect_presentations(Device))
    return timeout==0 ? DXGI_DDI_ERR_WASSTILLDRAWING : HRESULT_FROM_WIN32(ERROR_BUSY);
  return S_OK;
}
HRESULT AgxD3d10WindowsFlushRetire(AGX_D3D10_WINDOWS_DEVICE *Device) {
  return flush_retire(Device,INFINITE);
}
HRESULT AgxD3d10WindowsTryFlushRetire(AGX_D3D10_WINDOWS_DEVICE *Device) {
  return flush_retire(Device,0);
}

HRESULT AgxD3d10WindowsQuerySignal(AGX_D3D10_WINDOWS_DEVICE *Device,
    ULONGLONG OwnerCookie, ULONG DeviceGeneration, ULONG Issue, ULONG *Fence) {
  APPLE_AGX_U32 token=0;
  if(Fence) *Fence=0;
  if(!Device || Device->Stage!=AgxD3d10DeviceReady || !Fence) return E_INVALIDARG;
  HRESULT result=AdmissionUmdScreenSignalQueryFence(&Device->Runtime,OwnerCookie,
      DeviceGeneration,Issue,&token);
  if(SUCCEEDED(result)) *Fence=token;
  return result;
}
HRESULT AgxD3d10WindowsQueryPoll(AGX_D3D10_WINDOWS_DEVICE *Device,
    ULONGLONG OwnerCookie, ULONG DeviceGeneration, ULONG Issue, ULONG Fence,
    BOOL *Completed) {
  if(!Device || Device->Stage!=AgxD3d10DeviceReady) return E_INVALIDARG;
  return AdmissionUmdScreenPollQueryFence(&Device->Runtime,OwnerCookie,
      DeviceGeneration,Issue,Fence,Completed);
}
HRESULT AgxD3d10WindowsQueryConsume(AGX_D3D10_WINDOWS_DEVICE *Device,
    ULONGLONG OwnerCookie, ULONG DeviceGeneration, ULONG Issue, ULONG Fence) {
  if(!Device || Device->Stage!=AgxD3d10DeviceReady) return E_INVALIDARG;
  return AdmissionUmdScreenConsumeQueryFence(&Device->Runtime,OwnerCookie,
      DeviceGeneration,Issue,Fence);
}
HRESULT AgxD3d10WindowsQueryDetach(AGX_D3D10_WINDOWS_DEVICE *Device,
    ULONGLONG OwnerCookie, ULONG DeviceGeneration, ULONG Issue, ULONG Fence) {
  if(!Device || Device->Stage!=AgxD3d10DeviceReady) return E_INVALIDARG;
  return AdmissionUmdScreenDetachQueryFence(&Device->Runtime,OwnerCookie,
      DeviceGeneration,Issue,Fence);
}
HRESULT AgxD3d10WindowsQueryCollect(AGX_D3D10_WINDOWS_DEVICE *Device) {
  if(!Device || Device->Stage!=AgxD3d10DeviceReady) return E_INVALIDARG;
  return AdmissionUmdScreenCollectDetachedQueryFences(&Device->Runtime);
}

HRESULT AgxD3d10WindowsPresentationOpen(
    AGX_D3D10_WINDOWS_DEVICE *Device,
    const D3D10DDIARG_OPENRESOURCE *OpenResource,
    D3D10DDI_HRTRESOURCE RuntimeResource,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE **Resource) {
  if(!Device || Device->Stage!=AgxD3d10DeviceReady || !OpenResource ||
     !Resource || *Resource || !RuntimeResource.handle) return E_INVALIDARG;
  AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *record=
      (AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *)HeapAlloc(
          GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*record));
  if(!record) return E_OUTOFMEMORY;
  D3D10DDI_HDEVICE deviceHandle={0};
  D3D10DDI_HRESOURCE resourceHandle={0};
  deviceHandle.pDrvPrivate=&Device->Runtime;
  resourceHandle.pDrvPrivate=&record->Resource;
  AdmissionUmdOpenResource(deviceHandle,OpenResource,resourceHandle,RuntimeResource);
  if(record->Resource.Magic!=ADMISSION_UMD_RESOURCE_MAGIC ||
     !record->Resource.Retirement) {
    HeapFree(GetProcessHeap(),0,record);
    return FAILED(Device->Runtime.LastScreenError)?
        Device->Runtime.LastScreenError:E_INVALIDARG;
  }
  HRESULT result=attach_presentation_render_resource(Device,record);
  AdmissionUmdDiagnostic("presentation-import",result,NULL,0u);
  if(FAILED(result)) {
    AdmissionUmdDestroyResource(deviceHandle,resourceHandle);
    HeapFree(GetProcessHeap(),0,record);return result;
  }
  record->Device=Device;*Resource=record;return S_OK;
}

HRESULT AgxD3d10WindowsPresentationCreate(
    AGX_D3D10_WINDOWS_DEVICE *Device,
    const D3D10DDIARG_CREATERESOURCE *CreateResource,
    D3D10DDI_HRTRESOURCE RuntimeResource,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE **Resource) {
  if(!Device || Device->Stage!=AgxD3d10DeviceReady || !CreateResource ||
     !Resource || *Resource || !RuntimeResource.handle) return E_INVALIDARG;
  AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *record=
      (AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *)HeapAlloc(
          GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*record));
  if(!record) return E_OUTOFMEMORY;
  D3D11DDIARG_CREATERESOURCE create={0};
  create.pMipInfoList=CreateResource->pMipInfoList;
  create.pInitialDataUP=CreateResource->pInitialDataUP;
  create.ResourceDimension=CreateResource->ResourceDimension;
  create.Usage=CreateResource->Usage;create.BindFlags=CreateResource->BindFlags;
  create.MapFlags=CreateResource->MapFlags;create.MiscFlags=CreateResource->MiscFlags;
  create.Format=CreateResource->Format;create.SampleDesc=CreateResource->SampleDesc;
  create.MipLevels=CreateResource->MipLevels;create.ArraySize=CreateResource->ArraySize;
  create.pPrimaryDesc=CreateResource->pPrimaryDesc;
  D3D10DDI_HDEVICE deviceHandle={0};D3D10DDI_HRESOURCE resourceHandle={0};
  deviceHandle.pDrvPrivate=&Device->Runtime;resourceHandle.pDrvPrivate=&record->Resource;
  AdmissionUmdCreateResource(deviceHandle,&create,resourceHandle,RuntimeResource);
  if(record->Resource.Magic!=ADMISSION_UMD_RESOURCE_MAGIC ||
     !record->Resource.Retirement) {
    HeapFree(GetProcessHeap(),0,record);
    return E_INVALIDARG;
  }
  HRESULT result=attach_presentation_render_resource(Device,record);
  AdmissionUmdDiagnostic("presentation-import",result,NULL,0u);
  if(FAILED(result)) {
    AdmissionUmdDestroyResource(deviceHandle,resourceHandle);
    HeapFree(GetProcessHeap(),0,record);return result;
  }
  record->Device=Device;*Resource=record;return S_OK;
}

HRESULT AgxD3d10WindowsPresentationDestroy(
    AGX_D3D10_WINDOWS_DEVICE *Device,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE **Resource) {
  if(!Device || !Resource || !*Resource || (*Resource)->Device!=Device ||
     Device->Stage<AgxD3d10DeviceRuntimeReady ||
     Device->Stage>=AgxD3d10DeviceRuntimeReleased) return E_INVALIDARG;
  AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *record=*Resource;
  if(record->RenderResource) {
    AgxWin32AsahiResourceRelease(&record->RenderResource);
  }
  record->Next=Device->PendingPresentations;
  Device->PendingPresentations=record;*Resource=NULL;
  (void)collect_presentations(Device);
  return S_OK;
}

HRESULT AgxD3d10WindowsPresentationSubmit(
    AGX_D3D10_WINDOWS_DEVICE *Device,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *Resource,
    PVOID DxgiContext) {
  if(!Device || Device->Stage!=AgxD3d10DeviceReady || !Resource ||
     Resource->Device!=Device ||
     Resource->Resource.Magic!=ADMISSION_UMD_RESOURCE_MAGIC)
    return E_INVALIDARG;
  if(!AgxWin32AsahiContextFlushForPresent(Device->Context)) return E_FAIL;
  HRESULT result=AgxD3d10WindowsFlushStatus(Device);
  if(FAILED(result)) return result;
  return AdmissionUmdSubmitPresent(&Device->Runtime,&Resource->Resource,DxgiContext);
}

HRESULT AgxD3d10WindowsPresentationSetDisplayMode(
    AGX_D3D10_WINDOWS_DEVICE *Device,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *Resource) {
  if(!Device || Device->Stage!=AgxD3d10DeviceReady || !Resource ||
     Resource->Device!=Device ||
     Resource->Resource.Magic!=ADMISSION_UMD_RESOURCE_MAGIC)
    return E_INVALIDARG;
  DXGI_DDI_ARG_SETDISPLAYMODE args={0};
  args.hDevice=(DXGI_DDI_HDEVICE)(UINT_PTR)&Device->Runtime;
  args.hResource=(DXGI_DDI_HRESOURCE)(UINT_PTR)&Resource->Resource;
  return AdmissionUmdSetDisplayMode(&args);
}

struct pipe_resource *AgxD3d10WindowsPresentationPipeResource(
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *Resource) {
  return Resource ? Resource->RenderResource : NULL;
}

HRESULT AgxD3d10WindowsPresentationRotate(
    AGX_D3D10_WINDOWS_DEVICE *Device,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE **Resources,UINT Count) {
  if(!Device || Device->Stage!=AgxD3d10DeviceReady || !Resources || Count<2u)
    return E_INVALIDARG;
  HRESULT result=AgxD3d10WindowsFlushRetire(Device);
  if(FAILED(result)) return result;
  enum pipe_format rotationFormat=PIPE_FORMAT_NONE;
  AcquireSRWLockExclusive(&Device->Runtime.ScreenBufferLock);
  for(UINT i=0;i<Count;++i) {
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *r=Resources[i];
    ADMISSION_UMD_SCREEN_BUFFER *slot=NULL;
    enum pipe_format format;
    if(!r || r->Device!=Device ||
       r->Resource.Magic!=ADMISSION_UMD_RESOURCE_MAGIC ||
       !r->Resource.Retirement || !r->RenderBuffer.Transport.Token ||
       !presentation_pipe_format(r->RenderResource,&format) ||
       (i && format!=rotationFormat)) {
      ReleaseSRWLockExclusive(&Device->Runtime.ScreenBufferLock);return E_INVALIDARG;
    }
    if(!i) rotationFormat=format;
    for(UINT j=0;j<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++j)
      if(Device->Runtime.ScreenBuffers[j].Active &&
         Device->Runtime.ScreenBuffers[j].Token==r->RenderBuffer.Transport.Token)
        slot=&Device->Runtime.ScreenBuffers[j];
    if(!slot || !slot->Borrowed || slot->Transition || slot->SubmissionHolds ||
       slot->SourceHolds || slot->KernelAllocation!=r->Resource.KernelAllocation) {
      ReleaseSRWLockExclusive(&Device->Runtime.ScreenBufferLock);
      return HRESULT_FROM_WIN32(ERROR_BUSY);
    }
  }
  D3DKMT_HANDLE saved=Resources[0]->Resource.KernelAllocation;
  for(UINT i=0;i+1u<Count;++i) {
    Resources[i]->Resource.KernelAllocation=
        Resources[i+1u]->Resource.KernelAllocation;
    Resources[i]->Resource.Retirement->KernelAllocation=
        Resources[i+1u]->Resource.KernelAllocation;
  }
  Resources[Count-1u]->Resource.KernelAllocation=saved;
  Resources[Count-1u]->Resource.Retirement->KernelAllocation=saved;
  for(UINT i=0;i<Count;++i)
    for(UINT j=0;j<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++j)
      if(Device->Runtime.ScreenBuffers[j].Active &&
         Device->Runtime.ScreenBuffers[j].Token==Resources[i]->RenderBuffer.Transport.Token)
        Device->Runtime.ScreenBuffers[j].KernelAllocation=
            Resources[i]->Resource.KernelAllocation;
  ReleaseSRWLockExclusive(&Device->Runtime.ScreenBufferLock);
  return S_OK;
}

static HRESULT resource_allocation(
    AGX_D3D10_WINDOWS_DEVICE *device,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *presentation,
    struct pipe_resource *resource,D3DKMT_HANDLE *allocation) {
  if(allocation) *allocation=0;
  if(!device || device->Stage!=AgxD3d10DeviceReady || !allocation)
    return E_INVALIDARG;
  if(presentation) {
    if(presentation->Device!=device ||
       presentation->Resource.Magic!=ADMISSION_UMD_RESOURCE_MAGIC ||
       !presentation->Resource.KernelAllocation) return E_INVALIDARG;
    *allocation=presentation->Resource.KernelAllocation;return S_OK;
  }
  AGX_WIN32_RELOC_ALLOCATION identity={0};
  if(!resource || !AgxWin32AsahiResourceIdentity(resource,&identity))
    return E_INVALIDARG;
  AcquireSRWLockShared(&device->Runtime.ScreenBufferLock);
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i) {
    ADMISSION_UMD_SCREEN_BUFFER *slot=&device->Runtime.ScreenBuffers[i];
    if(slot->Active && !slot->Transition && slot->Token==identity.Token &&
       slot->Serial==identity.Serial && slot->Bytes==identity.Bytes) {
      *allocation=slot->KernelAllocation;break;
    }
  }
  ReleaseSRWLockShared(&device->Runtime.ScreenBufferLock);
  return *allocation ? S_OK : E_INVALIDARG;
}

HRESULT AgxD3d10WindowsSetResourcePriority(
    AGX_D3D10_WINDOWS_DEVICE *device,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *presentation,
    struct pipe_resource *resource,UINT priority) {
  D3DKMT_HANDLE allocation=0;D3DDDICB_SETPRIORITY args={0};
  HRESULT result=resource_allocation(device,presentation,resource,&allocation);
  if(FAILED(result) || !device->Runtime.KernelCallbacks ||
     !device->Runtime.KernelCallbacks->pfnSetPriorityCb) return E_INVALIDARG;
  args.NumAllocations=1;args.HandleList=&allocation;args.pPriorities=&priority;
  return device->Runtime.KernelCallbacks->pfnSetPriorityCb(
      device->Runtime.RuntimeDevice.handle,&args);
}

HRESULT AgxD3d10WindowsQueryResourceResidency(
    AGX_D3D10_WINDOWS_DEVICE *device,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *presentation,
    struct pipe_resource *resource,DXGI_DDI_RESIDENCY *status) {
  D3DKMT_HANDLE allocation=0;D3DDDI_RESIDENCYSTATUS kernelStatus;
  D3DDDICB_QUERYRESIDENCY args={0};
  if(status) *status=(DXGI_DDI_RESIDENCY)0;
  HRESULT result=resource_allocation(device,presentation,resource,&allocation);
  if(FAILED(result) || !status || !device->Runtime.KernelCallbacks ||
     !device->Runtime.KernelCallbacks->pfnQueryResidencyCb) return E_INVALIDARG;
  args.NumAllocations=1;args.HandleList=&allocation;
  args.pResidencyStatus=&kernelStatus;
  result=device->Runtime.KernelCallbacks->pfnQueryResidencyCb(
      device->Runtime.RuntimeDevice.handle,&args);
  if(FAILED(result)) return result;
  switch(kernelStatus) {
  case D3DDDI_RESIDENCYSTATUS_RESIDENTINGPUMEMORY:
    *status=DXGI_DDI_RESIDENCY_FULLY_RESIDENT;return S_OK;
  case D3DDDI_RESIDENCYSTATUS_RESIDENTINSHAREDMEMORY:
    *status=DXGI_DDI_RESIDENCY_RESIDENT_IN_SHARED_MEMORY;
    return AGX_DXGI_STATUS_RESIDENT_IN_SHARED_MEMORY;
  case D3DDDI_RESIDENCYSTATUS_NOTRESIDENT:
    *status=DXGI_DDI_RESIDENCY_EVICTED_TO_DISK;
    return AGX_DXGI_STATUS_NOT_RESIDENT;
  default: return E_FAIL;
  }
}

HRESULT AgxD3d10WindowsPresentationBlt(
    AGX_D3D10_WINDOWS_DEVICE *device,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *destination,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *source) {
  if(!device || device->Stage!=AgxD3d10DeviceReady || !destination || !source ||
     destination==source || destination->Device!=device || source->Device!=device ||
     !destination->RenderResource || !source->RenderResource ||
     destination->Resource.Magic!=ADMISSION_UMD_RESOURCE_MAGIC ||
     source->Resource.Magic!=ADMISSION_UMD_RESOURCE_MAGIC ||
     !device->Context || !device->Context->blit)
    return E_INVALIDARG;
  enum pipe_format destinationFormat,sourceFormat;
  if(!presentation_pipe_format(destination->RenderResource,&destinationFormat) ||
     !presentation_pipe_format(source->RenderResource,&sourceFormat))
    return E_INVALIDARG;
  struct pipe_blit_info info={0};
  info.dst.resource=destination->RenderResource;info.dst.level=0;
  info.dst.box.x=0;info.dst.box.y=0;info.dst.box.z=0;
  info.dst.box.width=2560;info.dst.box.height=1600;info.dst.box.depth=1;
  info.dst.format=destinationFormat;
  info.src.resource=source->RenderResource;info.src.level=0;
  info.src.box=info.dst.box;info.src.format=sourceFormat;
  info.mask=PIPE_MASK_RGBA;info.filter=PIPE_TEX_FILTER_NEAREST;
  device->Context->blit(device->Context,&info);
  return AgxD3d10WindowsFlushStatus(device);
}

#if defined(ADMISSION_UMD_PIPE_FACTORY_TEST)
ADMISSION_UMD_DEVICE *AgxD3d10WindowsRuntimeForTest(AGX_D3D10_WINDOWS_DEVICE *Device) {
  return Device && Device->Stage>=AgxD3d10DeviceRuntimeReady &&
      Device->Stage<AgxD3d10DeviceRuntimeReleased ? &Device->Runtime : NULL;
}
void *AgxD3d10WindowsOwnerForTest(AGX_D3D10_WINDOWS_DEVICE *Device) {
  return Device && Device->Stage>=AgxD3d10DeviceNativeScreenReady &&
      Device->Stage<AgxD3d10DeviceNativeScreenReleased ? &Device->Owner : NULL;
}
BOOL AgxD3d10WindowsTerminalReceiptForTest(AGX_D3D10_WINDOWS_ADAPTER *Adapter,
    AGX_D3D10_WINDOWS_TERMINAL_RECEIPT *Receipt) {
  if(!Adapter || !Receipt) return FALSE;
  ZeroMemory(Receipt,sizeof(*Receipt));
  Receipt->CallbacksCleared=TRUE;
  AcquireSRWLockShared(&Adapter->Lock);
  for(AGX_D3D10_WINDOWS_TERMINAL *record=Adapter->Terminal;record;record=record->Next) {
    AGX_D3D10_WINDOWS_DEVICE *former=(AGX_D3D10_WINDOWS_DEVICE *)record;
    ++Receipt->Count;Receipt->ActiveBuffers+=
        record->ActiveBuffersAndQueryMarkers&AgxD3d10TerminalCountMask;
    Receipt->NativeContexts+=record->NativeContexts;Receipt->LiveBos+=record->LiveBos;
    Receipt->QueryMarkers+=(record->ActiveBuffersAndQueryMarkers>>
        AgxD3d10TerminalQueryShift)&AgxD3d10TerminalCountMask;
    Receipt->Quiesced+=record->KernelQuiesced?1u:0u;
    if(SUCCEEDED(record->Error) || former->Runtime.KernelCallbacks ||
       former->Runtime.SetErrorCallback || former->Owner.Device ||
       former->Owner.Backend || former->Backend.Ops.Enter ||
       former->Backend.Ops.Leave || former->Backend.Ops.Associate ||
       former->Backend.Ops.Detach || former->Backend.Ops.Identity ||
       former->Backend.Ops.NextBo || former->Backend.ContextCreate ||
       former->Backend.ContextDestroy || former->Backend.BatchOps ||
       former->Backend.BatchOwner)
      Receipt->CallbacksCleared=FALSE;
  }
  ReleaseSRWLockShared(&Adapter->Lock);
  return TRUE;
}
#endif
