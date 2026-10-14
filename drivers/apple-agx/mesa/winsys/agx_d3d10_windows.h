#ifndef AGX_D3D10_WINDOWS_H
#define AGX_D3D10_WINDOWS_H

/* Include after the pinned WDK declarations. These are private UMD objects,
 * not a new command ABI or a second renderer. */
struct pipe_context;
struct pipe_resource;
#define AGX_DXGI_STATUS_NOT_RESIDENT ((HRESULT)0x08760875L)
#define AGX_DXGI_STATUS_RESIDENT_IN_SHARED_MEMORY ((HRESULT)0x08760876L)
#ifndef ADMISSION_UMD_PRESENT_MEASURE_KIND_DEFINED
#define ADMISSION_UMD_PRESENT_MEASURE_KIND_DEFINED
typedef enum ADMISSION_UMD_PRESENT_MEASURE_KIND {
  AdmissionUmdMeasureNativeDevice,
  AdmissionUmdMeasureNativePresentEntry,
  AdmissionUmdMeasureNativePresentReturn,
  AdmissionUmdMeasureNativeBltEntry,
  AdmissionUmdMeasureNativeBltReturn,
  AdmissionUmdMeasureNativeFlushStage,
  AdmissionUmdMeasureNativeSubmitEntry,
  AdmissionUmdMeasureNativeSubmitReturn,
  AdmissionUmdMeasurePresentCallbackEnter,
  AdmissionUmdMeasurePresentCallbackReturn,
  AdmissionUmdMeasureLegacyPresentEntry,
  AdmissionUmdMeasureLegacyPresent1Entry,
  AdmissionUmdMeasureNativeContextFlush,
  AdmissionUmdMeasureNativeFlushStatus,
  AdmissionUmdMeasurePresentGuard,
  AdmissionUmdMeasureCount
} ADMISSION_UMD_PRESENT_MEASURE_KIND;
#endif
typedef struct AGX_D3D10_WINDOWS_ADAPTER AGX_D3D10_WINDOWS_ADAPTER;
typedef struct AGX_D3D10_WINDOWS_DEVICE AGX_D3D10_WINDOWS_DEVICE;
typedef struct AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE;
#ifdef __cplusplus
extern "C" {
#endif
VOID AgxD3d10WindowsDiagnostic(PCSTR Stage, HRESULT Status,
                              const UINT *Values, UINT Count);
VOID AgxD3d10WindowsPresentMeasure(UINT Kind, HRESULT Status,
                                  const UINT *Values, UINT Count);
VOID AgxD3d10WindowsDiagnosticSetError(
    PCSTR Function, UINT Line, HRESULT Status);
BOOL AgxD3d10WindowsDiagnosticRefusal(HRESULT Status);
VOID AgxD3d10WindowsDiagnosticState(AGX_D3D10_WINDOWS_DEVICE *, PCSTR Stage);
VOID AgxD3d10WindowsDiagnosticResource(
    PCSTR Stage, const D3D10DDIARG_CREATERESOURCE *Resource);
VOID AgxD3d10WindowsDiagnosticBufferUsage(
    const D3D10DDIARG_CREATERESOURCE *Resource);
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
/* EXP1287 diagnostic switches (environment of the calling process, default
 * off = the EXP1284 answers): the D3D screen's multisampling and the
 * frontend's multisample caps/quality reporting. */
BOOL AgxD3d10WindowsDiagnosticSwitch(const char *Name);
/* EXP1288 diagnostic (default 0 = the EXP1284 answers): the hex mask
 * APPLE_AGX_D3D_MSAA_CAPS selects the multisample caps/quality reporting --
 * 0x01 the EXP1287 set (2x/4x on BGRA8/BGRX8 UNORM/SRGB, RGBA8_UNORM,
 * RGBA16_FLOAT, D16, D24S8, D32, D32S8; "1" keeps the EXP1287 switch),
 * 0x02 one quality level at SampleCount 1 for every format (the DDI page),
 * 0x04 with 0x01 every member of those formats' cast families (TYPELESS and
 * the depth families' colour formats), 0x08 with 0x01 the BGRA8/BGRX8
 * family only. With any bit set every Check* answer is receipted
 * (measure-msaa-check: kind 1 caps / 2 levels, format, sample count,
 * answer). */
UINT AgxD3d10WindowsMsaaProbe(VOID);
BOOL AgxD3d10WindowsMsaaFormat(DXGI_FORMAT Format, UINT Probe);
VOID AgxD3d10WindowsMsaaReceipt(UINT Kind, UINT Format, UINT SampleCount,
                                UINT Answer);
HRESULT AgxD3d10WindowsFlushStatus(AGX_D3D10_WINDOWS_DEVICE *Device);
HRESULT AgxD3d10WindowsFlushDeferredResources(AGX_D3D10_WINDOWS_DEVICE *Device);
HRESULT AgxD3d10WindowsFlushRetire(AGX_D3D10_WINDOWS_DEVICE *Device);
HRESULT AgxD3d10WindowsTryFlushRetire(AGX_D3D10_WINDOWS_DEVICE *Device);
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
/* Stack-scoped observer: nested helper errors retain the originating DDI's
 * arguments. No callback replacement, allocation, or graphics state change. */
class AgxD3d10RefusalScope {
 public:
  static thread_local AgxD3d10RefusalScope *Current;
  PCSTR Name;
  const UINT *Values;
  UINT Count;
  bool Emitted;
  AgxD3d10RefusalScope *Previous;
  AgxD3d10RefusalScope(PCSTR name,const UINT *values,UINT count)
    : Name(name),Values(values),Count(count),Emitted(false),Previous(Current) {
    Current=this;
  }
  ~AgxD3d10RefusalScope() { Current=Previous; }
  AgxD3d10RefusalScope(const AgxD3d10RefusalScope &)=delete;
  AgxD3d10RefusalScope &operator=(const AgxD3d10RefusalScope &)=delete;
};
#endif
#endif
