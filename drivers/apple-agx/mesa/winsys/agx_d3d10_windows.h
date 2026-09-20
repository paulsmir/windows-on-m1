#ifndef AGX_D3D10_WINDOWS_H
#define AGX_D3D10_WINDOWS_H

/* Include after the pinned WDK declarations. These are private UMD objects,
 * not a new command ABI or a second renderer. */
struct pipe_context;
struct pipe_resource;
#define AGX_DXGI_STATUS_NOT_RESIDENT ((HRESULT)0x08760875L)
#define AGX_DXGI_STATUS_RESIDENT_IN_SHARED_MEMORY ((HRESULT)0x08760876L)
typedef struct AGX_D3D10_WINDOWS_ADAPTER AGX_D3D10_WINDOWS_ADAPTER;
typedef struct AGX_D3D10_WINDOWS_DEVICE AGX_D3D10_WINDOWS_DEVICE;
typedef struct AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE;
#ifdef __cplusplus
extern "C" {
#endif
VOID AgxD3d10WindowsDiagnostic(PCSTR Stage, HRESULT Status,
                              const UINT *Values, UINT Count);
VOID AgxD3d10WindowsDiagnosticResource(
    PCSTR Stage, const D3D10DDIARG_CREATERESOURCE *Resource);
HRESULT AgxD3d10WindowsOpenAdapter(const D3D10DDIARG_OPENADAPTER *Args,
                                  AGX_D3D10_WINDOWS_ADAPTER **Adapter);
HRESULT AgxD3d10WindowsCloseAdapter(AGX_D3D10_WINDOWS_ADAPTER **Adapter);
HRESULT AgxD3d10WindowsCreateDevice(AGX_D3D10_WINDOWS_ADAPTER *Adapter,
                                   const D3D10DDIARG_CREATEDEVICE *Args,
                                   AGX_D3D10_WINDOWS_DEVICE **Device);
/* Busy leaves the owner reachable from Adapter. Terminal Windows cleanup
 * errors follow the shared runtime's error-callback policy. */
HRESULT AgxD3d10WindowsCloseDevice(AGX_D3D10_WINDOWS_DEVICE **Device);
/* Void-DDI teardown: consumes the owner synchronously on success. On terminal
 * failure it disables callbacks and records CPU-only obligations on Adapter;
 * no retryable Device token survives the invocation. */
HRESULT AgxD3d10WindowsDestroyDeviceDdi(AGX_D3D10_WINDOWS_DEVICE **Device,
                                       BOOL *Consumed);
struct pipe_context *AgxD3d10WindowsContext(AGX_D3D10_WINDOWS_DEVICE *Device);
BOOL AgxD3d10WindowsIdentity(AGX_D3D10_WINDOWS_DEVICE *Device,
                             ULONGLONG *OwnerCookie,
                             ULONG *DeviceGeneration);
HRESULT AgxD3d10WindowsFlushStatus(AGX_D3D10_WINDOWS_DEVICE *Device);
HRESULT AgxD3d10WindowsFlushRetire(AGX_D3D10_WINDOWS_DEVICE *Device);
HRESULT AgxD3d10WindowsQuerySignal(AGX_D3D10_WINDOWS_DEVICE *Device,
    ULONGLONG OwnerCookie, ULONG DeviceGeneration, ULONG Issue, ULONG *Fence);
HRESULT AgxD3d10WindowsQueryPoll(AGX_D3D10_WINDOWS_DEVICE *Device,
    ULONGLONG OwnerCookie, ULONG DeviceGeneration, ULONG Issue, ULONG Fence,
    BOOL *Completed);
HRESULT AgxD3d10WindowsQueryConsume(AGX_D3D10_WINDOWS_DEVICE *Device,
    ULONGLONG OwnerCookie, ULONG DeviceGeneration, ULONG Issue, ULONG Fence);
HRESULT AgxD3d10WindowsQueryDetach(AGX_D3D10_WINDOWS_DEVICE *Device,
    ULONGLONG OwnerCookie, ULONG DeviceGeneration, ULONG Issue, ULONG Fence);
HRESULT AgxD3d10WindowsQueryCollect(AGX_D3D10_WINDOWS_DEVICE *Device);
HRESULT AgxD3d10WindowsPresentationOpen(
    AGX_D3D10_WINDOWS_DEVICE *Device,
    const D3D10DDIARG_OPENRESOURCE *OpenResource,
    D3D10DDI_HRTRESOURCE RuntimeResource,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE **Resource);
HRESULT AgxD3d10WindowsPresentationCreate(
    AGX_D3D10_WINDOWS_DEVICE *Device,
    const D3D10DDIARG_CREATERESOURCE *CreateResource,
    D3D10DDI_HRTRESOURCE RuntimeResource,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE **Resource);
HRESULT AgxD3d10WindowsPresentationDestroy(
    AGX_D3D10_WINDOWS_DEVICE *Device,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE **Resource);
HRESULT AgxD3d10WindowsPresentationSubmit(
    AGX_D3D10_WINDOWS_DEVICE *Device,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *Resource,
    PVOID DxgiContext);
HRESULT AgxD3d10WindowsPresentationSetDisplayMode(
    AGX_D3D10_WINDOWS_DEVICE *Device,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *Resource);
struct pipe_resource *AgxD3d10WindowsPresentationPipeResource(
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *Resource);
HRESULT AgxD3d10WindowsPresentationRotate(
    AGX_D3D10_WINDOWS_DEVICE *Device,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE **Resources,
    UINT Count);
HRESULT AgxD3d10WindowsSetResourcePriority(
    AGX_D3D10_WINDOWS_DEVICE *,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *, struct pipe_resource *, UINT);
HRESULT AgxD3d10WindowsQueryResourceResidency(
    AGX_D3D10_WINDOWS_DEVICE *,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *, struct pipe_resource *,
    DXGI_DDI_RESIDENCY *);
HRESULT AgxD3d10WindowsPresentationBlt(
    AGX_D3D10_WINDOWS_DEVICE *,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *);
#if defined(ADMISSION_UMD_PIPE_FACTORY_TEST)
struct _ADMISSION_UMD_DEVICE;
struct _ADMISSION_UMD_DEVICE *AgxD3d10WindowsRuntimeForTest(AGX_D3D10_WINDOWS_DEVICE *);
void *AgxD3d10WindowsOwnerForTest(AGX_D3D10_WINDOWS_DEVICE *);
typedef struct _AGX_D3D10_WINDOWS_TERMINAL_RECEIPT {
  ULONG Count,ActiveBuffers,NativeContexts,LiveBos,QueryMarkers,Quiesced;
  BOOL CallbacksCleared;
} AGX_D3D10_WINDOWS_TERMINAL_RECEIPT;
BOOL AgxD3d10WindowsTerminalReceiptForTest(AGX_D3D10_WINDOWS_ADAPTER *,
    AGX_D3D10_WINDOWS_TERMINAL_RECEIPT *);
#endif
#ifdef __cplusplus
}
#endif
#endif
