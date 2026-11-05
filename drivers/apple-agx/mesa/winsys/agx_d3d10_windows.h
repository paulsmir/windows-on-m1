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
struct pipe_context *AgxD3d10WindowsContext(AGX_D3D10_WINDOWS_DEVICE *Device);
#ifdef __cplusplus
}
#endif
#endif
