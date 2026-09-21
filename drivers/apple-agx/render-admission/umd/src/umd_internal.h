#ifndef APPLE_AGX_UMD_INTERNAL_H
#define APPLE_AGX_UMD_INTERNAL_H

#include "direct_flip_contract.h"
#include "render_win32_transport.h"
#include "umd_resource_lifetime.h"
#include "agx_win32_transport.h"
#include "agx_win32_screen.h"
#include "umd_draw_composer.h"

#define ADMISSION_UMD_ADAPTER_MAGIC 0x50414455u /* "UDAP" */
#define ADMISSION_UMD_DEVICE_MAGIC 0x56454455u  /* "UDEV" */
#define ADMISSION_UMD_RESOURCE_MAGIC 0x53455255u /* "URES" */
#define ADMISSION_UMD_SCREEN_BUFFER_LIMIT 64u
#define ADMISSION_UMD_SCREEN_FENCE_LIMIT 64u
#define ADMISSION_UMD_SOURCE_HOLD_LIMIT 64u

typedef struct _ADMISSION_UMD_SCREEN_BUFFER {
  APPLE_AGX_U64 Token;
  APPLE_AGX_U64 Serial;
  const void *NativeBo;
  const void *NativeBackend;
  int (*NativeMapRelease)(const void *, const void *, int);
  APPLE_AGX_U64 NativeBoSerial;
  D3DKMT_HANDLE KernelAllocation;
  /* Explicit residency contribution owned only by the KMT qualification
   * callbacks. Zeroed with this authoritative slot; never another handle map. */
  BOOL KmtResidencyHeld;
  ULONGLONG KmtPagingFence;
  APPLE_AGX_U64 Bytes;
  APPLE_AGX_U64 Alignment;
  PVOID LockedBase;
  APPLE_AGX_U32 ClassId;
  APPLE_AGX_U32 Flags;
  APPLE_AGX_U32 LockedAccess;
  APPLE_AGX_U32 MapEpoch;
  APPLE_AGX_U32 SourceHolds;
  APPLE_AGX_U32 SubmissionHolds;
  BOOL Active;
  BOOL Mapped;
  BOOL Transition;
  BOOL Borrowed;
} ADMISSION_UMD_SCREEN_BUFFER;

typedef struct _ADMISSION_UMD_SCREEN_SOURCE {
  APPLE_AGX_U64 Token;
  APPLE_AGX_U64 Serial;
  APPLE_AGX_U64 Offset;
  APPLE_AGX_U64 Bytes;
  APPLE_AGX_U32 Generation;
  APPLE_AGX_U32 MapEpoch;
  APPLE_AGX_U64 HoldId;
  PVOID Address;
} ADMISSION_UMD_SCREEN_SOURCE;

typedef struct _ADMISSION_UMD_SOURCE_HOLD_RECORD {
  APPLE_AGX_U64 HoldId;
  APPLE_AGX_U64 Token;
  APPLE_AGX_U64 Serial;
  APPLE_AGX_U64 Offset;
  APPLE_AGX_U64 Bytes;
  APPLE_AGX_U32 Generation;
  APPLE_AGX_U32 MapEpoch;
  BOOL Active;
} ADMISSION_UMD_SOURCE_HOLD_RECORD;

typedef struct _ADMISSION_UMD_SCREEN_FENCE {
  HANDLE Event;
  APPLE_AGX_U64 QueryOwner;
  APPLE_AGX_U32 QueryGeneration;
  APPLE_AGX_U32 QueryIssue;
  APPLE_AGX_U32 Token;
  APPLE_AGX_U32 Kind;
  BOOL Active;
  BOOL Completed;
} ADMISSION_UMD_SCREEN_FENCE;

enum {
  AdmissionUmdFenceDraw = 0u,
  AdmissionUmdFenceQueryAttached = 1u,
  AdmissionUmdFenceQueryDetached = 2u
};

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
  PFND3D10DDI_SETERROR_CB SetErrorCallback;
  DXGI_DDI_BASE_CALLBACKS *DxgiCallbacks;
  HANDLE KernelContext;
  HANDLE QuiescedKernelContext;
  BOOL KernelContextQuiesced;
  ULONG Win32Generation;
  PVOID CommandBuffer;
  UINT CommandBufferSize;
  D3DDDI_ALLOCATIONLIST *AllocationList;
  UINT AllocationListSize;
  D3DDDI_PATCHLOCATIONLIST *PatchList;
  UINT PatchListSize;
  D3DKMT_HANDLE PagingQueue;
  D3DKMT_HANDLE PagingSyncObject;
  volatile UINT64 *PagingFenceAddress;
  AGX_WIN32_SCREEN Screen;
  ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[ADMISSION_UMD_SCREEN_BUFFER_LIMIT];
  ADMISSION_UMD_SCREEN_FENCE ScreenFences[ADMISSION_UMD_SCREEN_FENCE_LIMIT];
  APPLE_AGX_U64 NextScreenToken;
  APPLE_AGX_U64 NextScreenSerial;
  APPLE_AGX_U64 NextSourceHoldId;
  APPLE_AGX_U64 OwnerCookie;
  APPLE_AGX_U64 LastDrawRequest;
  ADMISSION_UMD_DRAW_SUBMISSION *DrawSubmission;
  BOOL DrawTerminal;
  volatile LONG RenderCbSequence;
  APPLE_AGX_U32 NativeBackendCount;
  void *NativeBatchTransaction;
  APPLE_AGX_U64 LastNativeRequest;
  BOOL ScreenClosing;
  APPLE_AGX_U32 NextScreenFence;
  HRESULT LastScreenError;
  ADMISSION_UMD_RETIREMENT_QUEUE Retirement;
  HRESULT LastRetirementError;
  ULONG RetirementErrorCount;
  ULONG RetirementUndeallocated;
  BOOL RetirementTerminal;
  SRWLOCK ScreenBufferLock;
  ADMISSION_UMD_SOURCE_HOLD_RECORD SourceHolds[ADMISSION_UMD_SOURCE_HOLD_LIMIT];
} ADMISSION_UMD_DEVICE;

typedef struct _ADMISSION_UMD_RESOURCE {
  ULONG Magic;
  D3D10DDI_HRTRESOURCE RuntimeResource;
  D3DKMT_HANDLE KernelAllocation;
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE DirectFlip;
  ADMISSION_UMD_RETIREMENT *Retirement;
} ADMISSION_UMD_RESOURCE;

VOID APIENTRY AdmissionUmdOpenResource(
    D3D10DDI_HDEVICE DeviceHandle,
    const D3D10DDIARG_OPENRESOURCE *OpenResource,
    D3D10DDI_HRESOURCE ResourceHandle,
    D3D10DDI_HRTRESOURCE RuntimeResource);
VOID APIENTRY AdmissionUmdCreateResource(
    D3D10DDI_HDEVICE DeviceHandle,
    const D3D11DDIARG_CREATERESOURCE *CreateResource,
    D3D10DDI_HRESOURCE ResourceHandle,
    D3D10DDI_HRTRESOURCE RuntimeResource);
VOID APIENTRY AdmissionUmdDestroyResource(
    D3D10DDI_HDEVICE DeviceHandle, D3D10DDI_HRESOURCE ResourceHandle);
HRESULT AdmissionUmdSubmitPresent(ADMISSION_UMD_DEVICE *Device,
                                  ADMISSION_UMD_RESOURCE *Source,
                                  PVOID DxgiContext);
HRESULT APIENTRY AdmissionUmdSetDisplayMode(
    DXGI_DDI_ARG_SETDISPLAYMODE *Args);

#if defined(__cplusplus)
extern "C" {
#endif
VOID AdmissionUmdDiagnostic(PCSTR Stage, HRESULT Status,
                            const UINT *Values, UINT Count);
BOOL AdmissionUmdDiagnosticEnabled(VOID);
VOID AdmissionUmdSetError(ADMISSION_UMD_DEVICE *Device, HRESULT Error);
BOOL AdmissionUmdNextRenderSequence(
    ADMISSION_UMD_DEVICE *Device, UINT *Sequence);
/* Populates validated metadata only. Caller owns stable adapter storage. */
HRESULT AdmissionUmdRuntimeAdapterInitialize(
    ADMISSION_UMD_ADAPTER *Adapter, const D3D10DDIARG_OPENADAPTER *Args);
/* Initializes Windows ownership only; does not publish a DDI table or caps.
 * Storage and callback lifetimes belong to the calling runtime device. */
HRESULT AdmissionUmdRuntimeDeviceInitialize(
    ADMISSION_UMD_DEVICE *Device, ADMISSION_UMD_ADAPTER *Adapter,
    const D3D10DDIARG_CREATEDEVICE *Args);
/* Synchronously quiesces and destroys the runtime kernel context while its
 * callback table is still valid. Success clears the context and borrowed
 * command/list pointers so finalization cannot call it twice. */
HRESULT AdmissionUmdRuntimeDeviceDestroyKernelContext(
    ADMISSION_UMD_DEVICE *Device, BOOL *Destroyed);
/* Consumed is true only after the kernel context and all device-owned storage
 * have been released and Device has been cleared. A failed, unconsumed result
 * leaves the same storage reachable for retry. */
HRESULT AdmissionUmdRuntimeDeviceFinalize(ADMISSION_UMD_DEVICE *Device,
                                          BOOL *Consumed);
HRESULT AdmissionUmdScreenInitialize(ADMISSION_UMD_DEVICE *Device);
HRESULT AdmissionUmdScreenFinalize(ADMISSION_UMD_DEVICE *Device,
                                   ULONG *Undeallocated);
BOOL AdmissionUmdScreenHasLiveSources(ADMISSION_UMD_DEVICE *Device);
HRESULT AdmissionUmdScreenBeginClose(ADMISSION_UMD_DEVICE *Device);
VOID AdmissionUmdScreenCancelClose(ADMISSION_UMD_DEVICE *Device);
HRESULT AdmissionUmdScreenSignalFence(ADMISSION_UMD_DEVICE *Device,
                                      APPLE_AGX_U32 *Fence);
HRESULT AdmissionUmdScreenSignalQueryFence(ADMISSION_UMD_DEVICE *Device,
    APPLE_AGX_U64 Owner, APPLE_AGX_U32 Generation, APPLE_AGX_U32 Issue,
    APPLE_AGX_U32 *Fence);
HRESULT AdmissionUmdScreenPollQueryFence(ADMISSION_UMD_DEVICE *Device,
    APPLE_AGX_U64 Owner, APPLE_AGX_U32 Generation, APPLE_AGX_U32 Issue,
    APPLE_AGX_U32 Fence, BOOL *Completed);
HRESULT AdmissionUmdScreenConsumeQueryFence(ADMISSION_UMD_DEVICE *Device,
    APPLE_AGX_U64 Owner, APPLE_AGX_U32 Generation, APPLE_AGX_U32 Issue,
    APPLE_AGX_U32 Fence);
HRESULT AdmissionUmdScreenDetachQueryFence(ADMISSION_UMD_DEVICE *Device,
    APPLE_AGX_U64 Owner, APPLE_AGX_U32 Generation, APPLE_AGX_U32 Issue,
    APPLE_AGX_U32 Fence);
HRESULT AdmissionUmdScreenCollectDetachedQueryFences(
    ADMISSION_UMD_DEVICE *Device);
HRESULT AdmissionUmdScreenAdoptAllocation(
    ADMISSION_UMD_DEVICE *Device, D3DKMT_HANDLE KernelAllocation,
    APPLE_AGX_U64 Bytes, APPLE_AGX_U64 Alignment,
    APPLE_AGX_U32 ClassId, APPLE_AGX_U32 Flags,
    AGX_WIN32_SCREEN_BUFFER *Buffer);
BOOL AdmissionUmdScreenAllocationRegistered(
    ADMISSION_UMD_DEVICE *Device, D3DKMT_HANDLE KernelAllocation);
HRESULT AdmissionUmdScreenQuerySource(ADMISSION_UMD_DEVICE *Device,
                                      APPLE_AGX_U64 Token,
                                      ADMISSION_UMD_SCREEN_SOURCE *Source);
HRESULT AdmissionUmdScreenAcquireSource(
    ADMISSION_UMD_DEVICE *Device,
    const ADMISSION_UMD_SCREEN_SOURCE *Expected,
    ADMISSION_UMD_SCREEN_SOURCE *Held);
HRESULT AdmissionUmdScreenReleaseSource(
    ADMISSION_UMD_DEVICE *Device,
    const ADMISSION_UMD_SCREEN_SOURCE *Held);
HRESULT AdmissionUmdScreenAssociateNativeBo(
    ADMISSION_UMD_DEVICE *Device, APPLE_AGX_U64 Token,
    const void *NativeBo, APPLE_AGX_U64 NativeBoSerial);
HRESULT AdmissionUmdScreenQueryNativeBo(
    ADMISSION_UMD_DEVICE *Device, const void *NativeBo,
    APPLE_AGX_U64 NativeBoSerial, ADMISSION_UMD_SCREEN_SOURCE *Source);
HRESULT AdmissionUmdScreenDetachNativeBo(
    ADMISSION_UMD_DEVICE *Device, APPLE_AGX_U64 Token,
    const void *NativeBo, APPLE_AGX_U64 NativeBoSerial);
HRESULT AdmissionUmdScreenPrepareSubmissionMaps(
    ADMISSION_UMD_DEVICE *Device,
    const ADMISSION_UMD_DRAW_SUBMISSION *Submission);
#if defined(__cplusplus)
}
#endif

#endif /* APPLE_AGX_UMD_INTERNAL_H */
