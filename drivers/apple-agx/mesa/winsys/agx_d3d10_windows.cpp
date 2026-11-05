#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)
extern "C" {
#include "umd_internal.h"
}
#include "agx_win32_pipe_screen.h"
#include "agx_d3d10_windows.h"

struct AGX_D3D10_WINDOWS_ADAPTER {
  ADMISSION_UMD_ADAPTER Runtime;
  SRWLOCK Lock;
  ULONG Devices;
  AGX_D3D10_WINDOWS_DEVICE *Owners;
};
struct AGX_D3D10_WINDOWS_DEVICE {
  ADMISSION_UMD_DEVICE Runtime;
  AGX_WIN32_PIPE_DEVICE Pipe;
  AGX_D3D10_WINDOWS_ADAPTER *Adapter;
  AGX_D3D10_WINDOWS_DEVICE *Next;
};

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
  AcquireSRWLockExclusive(&Adapter->Lock);
  if (Adapter->Devices == ~(ULONG)0u) {
    ReleaseSRWLockExclusive(&Adapter->Lock);
    HeapFree(GetProcessHeap(), 0, owner);
    return E_OUTOFMEMORY;
  }
  ++Adapter->Devices;
  ReleaseSRWLockExclusive(&Adapter->Lock);
  HRESULT result = AdmissionUmdRuntimeDeviceInitialize(&owner->Runtime,
                                                       &Adapter->Runtime, Args);
  if (SUCCEEDED(result) &&
      !AgxWin32PipeDeviceInitialize(&owner->Pipe, &owner->Runtime.Screen)) {
    AdmissionUmdRuntimeDeviceFinalize(&owner->Runtime);
    result = E_OUTOFMEMORY;
  }
  if (FAILED(result)) {
    AcquireSRWLockExclusive(&Adapter->Lock);
    --Adapter->Devices;
    ReleaseSRWLockExclusive(&Adapter->Lock);
    HeapFree(GetProcessHeap(), 0, owner);
    return result;
  }
  owner->Adapter = Adapter;
  AcquireSRWLockExclusive(&Adapter->Lock);
  owner->Next = Adapter->Owners;
  Adapter->Owners = owner;
  ReleaseSRWLockExclusive(&Adapter->Lock);
  *Device = owner;
  return S_OK;
}

HRESULT AgxD3d10WindowsCloseDevice(AGX_D3D10_WINDOWS_DEVICE **Device) {
  if (Device == NULL || *Device == NULL) return E_INVALIDARG;
  AGX_D3D10_WINDOWS_DEVICE *owner = *Device;
  if (!AgxWin32PipeDeviceClose(&owner->Pipe))
    return HRESULT_FROM_WIN32(ERROR_BUSY);
  AdmissionUmdRuntimeDeviceFinalize(&owner->Runtime);
  AcquireSRWLockExclusive(&owner->Adapter->Lock);
  AGX_D3D10_WINDOWS_DEVICE **link = &owner->Adapter->Owners;
  while (*link != NULL && *link != owner) link = &(*link)->Next;
  if (*link == owner) *link = owner->Next;
  --owner->Adapter->Devices;
  ReleaseSRWLockExclusive(&owner->Adapter->Lock);
  HeapFree(GetProcessHeap(), 0, owner);
  *Device = NULL;
  return S_OK;
}

struct pipe_context *AgxD3d10WindowsContext(AGX_D3D10_WINDOWS_DEVICE *Device) {
  return Device != NULL ? Device->Pipe.Context : NULL;
}
