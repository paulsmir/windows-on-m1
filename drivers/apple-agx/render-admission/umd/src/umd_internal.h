#ifndef APPLE_AGX_UMD_INTERNAL_H
#define APPLE_AGX_UMD_INTERNAL_H

#include "direct_flip_contract.h"
#include "render_win32_transport.h"
#include "umd_resource_lifetime.h"
#include "agx_win32_transport.h"
#include "agx_win32_screen.h"

#define ADMISSION_UMD_ADAPTER_MAGIC 0x50414455u /* "UDAP" */
#define ADMISSION_UMD_DEVICE_MAGIC 0x56454455u  /* "UDEV" */
#define ADMISSION_UMD_RESOURCE_MAGIC 0x53455255u /* "URES" */
#define ADMISSION_UMD_SCREEN_BUFFER_LIMIT 64u

typedef struct _ADMISSION_UMD_SCREEN_BUFFER {
  APPLE_AGX_U64 Token;
  D3DKMT_HANDLE KernelAllocation;
  APPLE_AGX_U64 Bytes;
  APPLE_AGX_U64 Alignment;
  PVOID LockedBase;
  APPLE_AGX_U32 ClassId;
  APPLE_AGX_U32 Flags;
  APPLE_AGX_U32 LockedAccess;
  BOOL Active;
  BOOL Mapped;
} ADMISSION_UMD_SCREEN_BUFFER;

typedef struct _ADMISSION_UMD_ADAPTER {
  ULONG Magic;
  D3D10DDI_HRTADAPTER RuntimeAdapter;
  UINT Interface;
  UINT Version;
  const D3DDDI_ADAPTERCALLBACKS *Callbacks;
  AGX_WIN32_DEVICE_INFO DeviceInfo;
} ADMISSION_UMD_ADAPTER;

typedef struct _ADMISSION_UMD_DEVICE {
  ULONG Magic;
  ADMISSION_UMD_ADAPTER *Adapter;
  D3D10DDI_HRTDEVICE RuntimeDevice;
  D3D10DDI_HRTCORELAYER RuntimeCoreLayer;
  const D3DDDI_DEVICECALLBACKS *KernelCallbacks;
  const D3D11DDI_CORELAYER_DEVICECALLBACKS *UserCallbacks;
  DXGI_DDI_BASE_CALLBACKS *DxgiCallbacks;
  HANDLE KernelContext;
  ULONG Win32Generation;
  PVOID CommandBuffer;
  UINT CommandBufferSize;
  D3DDDI_ALLOCATIONLIST *AllocationList;
  UINT AllocationListSize;
  D3DDDI_PATCHLOCATIONLIST *PatchList;
  UINT PatchListSize;
  AGX_WIN32_SCREEN Screen;
  ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[ADMISSION_UMD_SCREEN_BUFFER_LIMIT];
  APPLE_AGX_U64 NextScreenToken;
  HRESULT LastScreenError;
  ADMISSION_UMD_RETIREMENT_QUEUE Retirement;
  HRESULT LastRetirementError;
  ULONG RetirementErrorCount;
  ULONG RetirementUndeallocated;
  BOOL RetirementTerminal;
} ADMISSION_UMD_DEVICE;

typedef struct _ADMISSION_UMD_RESOURCE {
  ULONG Magic;
  D3D10DDI_HRTRESOURCE RuntimeResource;
  D3DKMT_HANDLE KernelAllocation;
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE DirectFlip;
  ADMISSION_UMD_RETIREMENT *Retirement;
} ADMISSION_UMD_RESOURCE;

VOID AdmissionUmdSetError(ADMISSION_UMD_DEVICE *Device, HRESULT Error);
#if defined(__cplusplus)
extern "C" {
#endif
HRESULT AdmissionUmdScreenInitialize(ADMISSION_UMD_DEVICE *Device);
HRESULT AdmissionUmdScreenFinalize(ADMISSION_UMD_DEVICE *Device,
                                   ULONG *Undeallocated);
#if defined(__cplusplus)
}
#endif

#endif /* APPLE_AGX_UMD_INTERNAL_H */
