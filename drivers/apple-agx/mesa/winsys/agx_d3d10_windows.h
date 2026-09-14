#ifndef AGX_D3D10_WINDOWS_H
#define AGX_D3D10_WINDOWS_H

/* Include after the pinned WDK declarations. These are private UMD objects,
 * not a new command ABI or a second renderer. */
struct pipe_context;
typedef struct AGX_D3D10_WINDOWS_ADAPTER AGX_D3D10_WINDOWS_ADAPTER;
typedef struct AGX_D3D10_WINDOWS_DEVICE AGX_D3D10_WINDOWS_DEVICE;
#ifdef __cplusplus
extern "C" {
#endif
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
