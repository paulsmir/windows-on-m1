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
}
#include "agx_win32_asahi_scene.h"
#include "agx_d3d10_windows.h"

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
  ULONG Stage,ActiveBuffers,NativeContexts,LiveBos;
  BOOL KernelQuiesced;
} AGX_D3D10_WINDOWS_TERMINAL;

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
};

static void unlink_owner(AGX_D3D10_WINDOWS_DEVICE *owner) {
  AGX_D3D10_WINDOWS_ADAPTER *adapter=owner->Adapter;
  AcquireSRWLockExclusive(&adapter->Lock);
  AGX_D3D10_WINDOWS_DEVICE **link=&adapter->Owners;
  while(*link && *link!=owner) link=&(*link)->Next;
  if(*link==owner) *link=owner->Next;
  if(adapter->Devices) --adapter->Devices;
  ReleaseSRWLockExclusive(&adapter->Lock);
}

static void unlink_and_free(AGX_D3D10_WINDOWS_DEVICE **inout) {
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
  ULONG stage=owner->Stage,activeBuffers=0;
  ULONG nativeContexts=owner->Backend.ContextCount,liveBos=owner->Backend.LiveBos;
  BOOL kernelQuiesced=owner->KernelQuiesced;
  static_assert(sizeof(*owner)>=sizeof(AGX_D3D10_WINDOWS_TERMINAL),
                "terminal record must fit the consumed owner allocation");
  if(SUCCEEDED(error)) error=E_FAIL;
  AdmissionUmdSetError(&owner->Runtime,error);
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i)
    if(owner->Runtime.ScreenBuffers[i].Active) ++activeBuffers;
  unlink_owner(owner);
  ZeroMemory(owner,sizeof(*owner));
  AGX_D3D10_WINDOWS_TERMINAL *record=(AGX_D3D10_WINDOWS_TERMINAL *)owner;
  record->Error=error;record->Stage=stage;record->ActiveBuffers=activeBuffers;
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
    ++Receipt->Count;Receipt->ActiveBuffers+=record->ActiveBuffers;
    Receipt->NativeContexts+=record->NativeContexts;Receipt->LiveBos+=record->LiveBos;
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
