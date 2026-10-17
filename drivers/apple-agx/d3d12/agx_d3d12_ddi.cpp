/* D3D12 ABI gate. Deliberately no GPU access. D3D12 C1
 * (investigation/analysis/D3D12-C-implementation-plan.md): inside the
 * APPLE_AGX_D3D12=1 development profile only, the adapter answers the 3D
 * pipeline caps the 26100 runtime asks first (EXP1344) and creates a device
 * whose every DDI stays fail-closed, so the runtime's next requests become
 * measurable. Ordinary processes still get E_NOTIMPL from OpenAdapter12. */
#include <windows.h>
/* d3dkmddi.h expects NTSTATUS, which user-mode windows.h does not define. */
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(disable : 4201)
#include <d3d12umddi.h>
#include <cstddef>
#include <cstring>
#include <type_traits>

/* the UMD trace logger (umd_runtime_device.c; C linkage through umd_internal.h) */
extern "C" VOID AdmissionUmdDiagnostic(PCSTR Stage, HRESULT Status, const UINT *Values, UINT Count);

namespace {
/* D3D12 A3 (EXP1343: OpenAdapter12 returned a NULL driver adapter handle
 * and the runtime answered DXGI_ERROR_UNSUPPORTED without calling any
 * adapter function): the adapter object the runtime passes back to every
 * D3D12DDI_HADAPTER function, holding the runtime adapter and its
 * callbacks (the KMD caps query of D3D12 OpenAdapter). */
struct AgxD3d12Adapter {
    D3D12DDI_HRTADAPTER runtime;
    const D3DDDI_ADAPTERCALLBACKS *callbacks;
};
static AgxD3d12Adapter adapter_object;
static const D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 *callbacks;
static D3D12DDI_HRTDEVICE runtime_device;

/* EXP1347: the runtime destroyed the device after the caps with no traced
 * call; each fail-closed entry records its table (D3D12DDI_TABLE_TYPE) and
 * member offset so the receipt names the DDI the runtime needed. */
template<class T> struct TableOf;
template<> struct TableOf<D3D12DDI_DEVICE_FUNCS_CORE_0003> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_DEVICE_CORE; };
template<> struct TableOf<D3D12DDI_COMMAND_LIST_FUNCS_3D_0003> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D; };
template<> struct TableOf<D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D; };
template<class T, UINT Table, UINT Offset> struct Rejected;
template<class R, class... A, UINT Table, UINT Offset> struct Rejected<R (APIENTRY *)(A...), Table, Offset> {
    static R APIENTRY call(A...) {
        OutputDebugStringA("AGX D3D12 ABI gate: unimplemented DDI\n");
        const UINT receipt[] = {Table, Offset};
        AdmissionUmdDiagnostic("measure-d3d12-rejected", E_NOTIMPL, receipt, 2);
        if (callbacks && callbacks->pfnSetErrorCb)
            callbacks->pfnSetErrorCb(runtime_device, E_NOTIMPL);
        /* Command-list errors need the runtime list handle; no list is created here. */
        (void)(callbacks && callbacks->pfnSetCommandListErrorCb);
        if constexpr (std::is_same_v<R, HRESULT>) return E_NOTIMPL;
        else if constexpr (!std::is_void_v<R>) return R{};
    }
};
#define AGX_REJECT(T, M) Rejected<decltype(((T *)0)->M), TableOf<T>::value, static_cast<UINT>(offsetof(T, M))>::call
#define AGX_ASSIGN(T, M, F) (f).M = &F

static HRESULT APIENTRY GetVersions(D3D12DDI_HADAPTER, UINT32 *count, UINT64 *versions) {
    const UINT receipt[] = {count ? *count : 0, versions != nullptr};
    AdmissionUmdDiagnostic("d3d12-versions", count ? S_OK : E_INVALIDARG, receipt, 2);
    if (!count) return E_INVALIDARG;
    if (!versions) { *count = 1; return S_OK; }
    if (*count < 1) { *count = 1; return E_INVALIDARG; }
    versions[0] = D3D12DDI_SUPPORTED_0003;
    *count = 1;
    return S_OK;
}
/* C1: the pipeline level the development profile admits (D3D12 drivers
 * start at FL 11_0; the C8 baseline gates admission outside the profile). */
static const D3D12DDI_3DPIPELINELEVEL kProfileLevel = D3D12DDI_3DPIPELINELEVEL_11_0;
/* Answers a caps type with T, its remaining bytes zero (a larger, newer
 * structure keeps every later field at its "not supported" zero). */
template<class T, class F> static HRESULT Answer(const D3D12DDIARG_GETCAPS *arg, F fill) {
    if (arg->DataSize < sizeof(T)) return E_INVALIDARG;
    memset(arg->pData, 0, arg->DataSize);
    fill(*static_cast<T *>(arg->pData));
    return S_OK;
}
static HRESULT APIENTRY GetCaps(D3D12DDI_HADAPTER, const D3D12DDIARG_GETCAPS *arg) {
    HRESULT hr = E_NOTIMPL;
    if (arg && arg->pData) switch (arg->Type) {
    case D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1:
        /* HighestRuntimeSupportedFeatureLevel is the runtime's input: only
         * the output field is written. */
        if (arg->DataSize >= sizeof(D3D12DDI_3DPIPELINESUPPORT1_DATA_0081)) {
            static_cast<D3D12DDI_3DPIPELINESUPPORT1_DATA_0081 *>(arg->pData)
                ->MaximumDriverSupportedFeatureLevel = kProfileLevel;
            hr = S_OK;
        }
        break;
    case D3D12DDICAPS_TYPE_3DPIPELINESUPPORT:
        hr = Answer<D3D12DDI_3DPIPELINELEVEL>(arg, [](auto &d) { d = kProfileLevel; });
        break;
    /* EXP1345: after CreateDevice the runtime asks D3D12_OPTIONS (28 bytes,
     * the 0003 structure) and fails creation on E_NOTIMPL. C1 answers the
     * 0003-era device caps with the plan's minimum: binding and heap tier
     * 1, nothing optional; AGX is a unified-memory tile-based deferred
     * renderer (Asahi); no coherence is claimed. */
    case D3D12DDICAPS_TYPE_D3D12_OPTIONS:
        hr = Answer<D3D12DDI_D3D12_OPTIONS_DATA_0003>(arg, [](auto &d) {
            d.ResourceBindingTier = D3D12DDI_RESOURCE_BINDING_TIER_1;
            d.ConservativeRasterizationTier = D3D12DDI_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED;
            d.TiledResourcesTier = D3D12DDI_TILED_RESOURCES_TIER_NOT_SUPPORTED;
            d.CrossNodeSharingTier = D3D12DDI_CROSS_NODE_SHARING_TIER_NOT_SUPPORTED;
            d.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation = FALSE;
            d.OutputMergerLogicOp = FALSE;
            d.ResourceHeapTier = D3D12DDI_RESOURCE_HEAP_TIER_1;
        });
        break;
    case D3D12DDICAPS_TYPE_MEMORY_ARCHITECTURE:
        hr = Answer<D3D12DDI_MEMORY_ARCHITECTURE_CAPS>(arg, [](auto &d) {
            d.UMA = TRUE; d.IOCoherent = FALSE; d.CacheCoherent = FALSE;
        });
        break;
    case D3D12DDICAPS_TYPE_SHADER:
        hr = Answer<D3D12DDI_SHADER_CAPS_0003>(arg, [](auto &d) {
            d.MinPrecision = D3D12DDI_SHADER_MIN_PRECISION_NONE;
            d.Doubles = FALSE; d.ShaderSpecifiedStencilRef = FALSE;
            d.TypedUAVLoadAdditionalFormats = FALSE; d.ROVs = FALSE;
        });
        break;
    case D3D12DDICAPS_TYPE_ARCHITECTURE_INFO:
        hr = Answer<D3D12DDI_ARCHITECTURE_INFO_DATA>(arg, [](auto &d) {
            d.TileBasedDeferredRenderer = TRUE;
        });
        break;
    case D3D12DDICAPS_TYPE_GPUVA_CAPS:
        /* D3D12 FL 11_0 needs at least 31 bits per resource; resources are
         * single GPUVA allocations below 4 GiB. */
        hr = Answer<D3D12DDI_GPUVA_CAPS_0004>(arg, [](auto &d) {
            d.MaxGPUVirtualAddressBitsPerResource = 32;
        });
        break;
    case D3D12DDICAPS_TYPE_TEXTURE_LAYOUT:
        /* no device-dependent layouts or swizzles, no 64 KiB standard
         * swizzle, no row-major textures (EXP1346: asked as the 16-byte
         * D3D12DDI_TEXTURE_LAYOUT_CAPS_0001) */
        hr = Answer<D3D12DDI_TEXTURE_LAYOUT_CAPS>(arg, [](auto &) {});
        break;
    case D3D12DDICAPS_TYPE_TEXTURE_LAYOUT_SETS:
        /* EXP1346: asked for row-major ({D3D12DDI_TL_ROW_MAJOR, functional
         * unit}) and creation failed on E_NOTIMPL. EXP1349 + d3d12core 26100:
         * the runtime rejects SubCaps[0].MaxElementSize 0 and requires each
         * alignment to be a nonzero power of two (base <= 512, pitch and
         * depth pitch <= 256) even without row-major textures. The AGX linear
         * contract (Asahi genxml Texture/PBE Address and Stride shr(4),
         * linear layer stride shr(7); ail: largest element 16 bytes): one
         * sub-cap for every functional unit, fixed depth pitch. */
        hr = Answer<D3D12DDI_ROW_MAJOR_LAYOUT_CAPS>(arg, [](auto &d) {
            d.SubCaps[0].MaxElementSize = 16;
            d.SubCaps[0].BaseOffsetAlignment = 16;
            d.SubCaps[0].PitchAlignment = 16;
            d.SubCaps[0].DepthPitchAlignment = 128;
        });
        break;
    default:
        break;
    }
    const UINT receipt[] = {arg ? static_cast<UINT>(arg->Type) : 0, arg ? arg->DataSize : 0};
    AdmissionUmdDiagnostic("d3d12-caps", hr, receipt, 2);
    return hr;
}
/* C1: the driver device the runtime allocates (CalcPrivateDeviceSize). */
struct AgxD3d12Device {
    D3D12DDI_HRTDEVICE runtime;
    const D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 *callbacks;
    const D3DDDI_DEVICECALLBACKS *kernel;
};
static SIZE_T APIENTRY CalcPrivateDeviceSize(D3D12DDI_HADAPTER,
                                             const D3D12DDIARG_CALCPRIVATEDEVICESIZE *arg) {
    const UINT receipt[] = {arg ? arg->Interface : 0, arg ? arg->Version : 0};
    AdmissionUmdDiagnostic("d3d12-device-size", S_OK, receipt, 2);
    return sizeof(AgxD3d12Device);
}
static HRESULT APIENTRY CreateDevice(D3D12DDI_HADAPTER,
                                     const D3D12DDIARG_CREATEDEVICE_0003 *arg) {
    HRESULT hr = arg && arg->hDrvDevice.pDrvPrivate ? S_OK : E_INVALIDARG;
    if (SUCCEEDED(hr)) {
        auto *device = static_cast<AgxD3d12Device *>(arg->hDrvDevice.pDrvPrivate);
        device->runtime = arg->hRTDevice;
        device->callbacks = arg->p12UMCallbacks;
        device->kernel = arg->pKTCallbacks;
        callbacks = arg->p12UMCallbacks;
        runtime_device = arg->hRTDevice;
    }
    const UINT receipt[] = {arg ? arg->Interface : 0, arg ? arg->Version : 0,
                            arg ? static_cast<UINT>(arg->Flags) : 0};
    AdmissionUmdDiagnostic("d3d12-create-device", hr, receipt, 3);
    return hr;
}
static void APIENTRY DestroyDevice(D3D12DDI_HDEVICE) {
    AdmissionUmdDiagnostic("d3d12-destroy-device", S_OK, nullptr, 0);
    callbacks = nullptr;
}
static HRESULT APIENTRY GetOptionalDDITables(D3D12DDI_HADAPTER, UINT32 *entries,
                                             D3D12DDI_TABLE_REQUEST *) {
    AdmissionUmdDiagnostic("d3d12-optional-tables", entries ? S_OK : E_INVALIDARG, nullptr, 0);
    if (!entries) return E_INVALIDARG;
    *entries = 0; /* C1: no optional table */
    return S_OK;
}
/* C1d (EXP1348): device initialisation calls these three device-core DDIs
 * after the architecture caps; a refused one reports E_NOTIMPL through
 * pfnSetErrorCb and creation fails. They answer only what this stage
 * implements: no optional format capability (D3D12DDI_FORMAT_SUPPORT_* are
 * the optional bits; no D3D12 resource exists before C2), single-sample
 * only, and one node. */
static VOID APIENTRY CheckFormatSupport(D3D12DDI_HDEVICE, DXGI_FORMAT, UINT *support) {
    if (support) *support = 0;
}
static VOID APIENTRY CheckMultisampleQualityLevels(D3D12DDI_HDEVICE, DXGI_FORMAT, UINT samples,
                                                   D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAGS,
                                                   UINT *levels) {
    if (levels) *levels = samples == 1 ? 1u : 0u;
}
static VOID APIENTRY QueryNodeMap(D3D12DDI_HDEVICE, UINT adapters, UINT *map) {
    for (UINT i = 0; map && i < adapters; ++i) map[i] = 0; /* physical adapter i -> node 0 */
}
static HRESULT APIENTRY CloseAdapter(D3D12DDI_HADAPTER) {
    AdmissionUmdDiagnostic("d3d12-close-adapter", S_OK, nullptr, 0);
    return S_OK;
}
static HRESULT APIENTRY FillDDITable(D3D12DDI_HADAPTER, D3D12DDI_TABLE_TYPE, void *, SIZE_T,
                                     UINT, D3D12DDI_HRTTABLE);
static void FillD3D12DDI_ADAPTERFUNCS(D3D12DDI_ADAPTERFUNCS &f) {
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnCalcPrivateDeviceSize, CalcPrivateDeviceSize);
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnCreateDevice, CreateDevice);
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnCloseAdapter, CloseAdapter);
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnGetSupportedVersions, GetVersions);
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnGetCaps, GetCaps);
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnGetOptionalDDITables, GetOptionalDDITables);
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnFillDDITable, FillDDITable);
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnDestroyDevice, DestroyDevice);
}
static void FillD3D12DDI_DEVICE_FUNCS_CORE_0003(D3D12DDI_DEVICE_FUNCS_CORE_0003 &f) {
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckFormatSupport, CheckFormatSupport);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckMultisampleQualityLevels, CheckMultisampleQualityLevels);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetMipPacking, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetMipPacking));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateElementLayoutSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateElementLayoutSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateElementLayout, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateElementLayout));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyElementLayout, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyElementLayout));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateBlendStateSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateBlendStateSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateBlendState, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateBlendState));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyBlendState, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyBlendState));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDepthStencilStateSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDepthStencilStateSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDepthStencilState, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDepthStencilState));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyDepthStencilState, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyDepthStencilState));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateRasterizerStateSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateRasterizerStateSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRasterizerState, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRasterizerState));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyRasterizerState, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyRasterizerState));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateShaderSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateShaderSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateVertexShader, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateVertexShader));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreatePixelShader, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreatePixelShader));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateGeometryShader, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateGeometryShader));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateComputeShader, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateComputeShader));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateGeometryShaderWithStreamOutput, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateGeometryShaderWithStreamOutput));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateGeometryShaderWithStreamOutput, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateGeometryShaderWithStreamOutput));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateTessellationShaderSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateTessellationShaderSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateHullShader, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateHullShader));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDomainShader, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDomainShader));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyShader, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyShader));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandQueueSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandQueueSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandQueue, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandQueue));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandQueue, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandQueue));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandAllocatorSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandAllocatorSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandAllocator, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandAllocator));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandAllocator, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandAllocator));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnResetCommandAllocator, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnResetCommandAllocator));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivatePipelineStateSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivatePipelineStateSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreatePipelineState, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreatePipelineState));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyPipelineState, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyPipelineState));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandListSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandListSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandList, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandList));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandList, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandList));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateFenceSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateFenceSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateFence, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateFence));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyFence, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyFence));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDescriptorHeapSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDescriptorHeapSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDescriptorHeap, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDescriptorHeap));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyDescriptorHeap, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyDescriptorHeap));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetDescriptorSizeInBytes, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetDescriptorSizeInBytes));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetCPUDescriptorHandleForHeapStart, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetCPUDescriptorHandleForHeapStart));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetGPUDescriptorHandleForHeapStart, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetGPUDescriptorHandleForHeapStart));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateShaderResourceView, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateShaderResourceView));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateConstantBufferView, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateConstantBufferView));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateSampler, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateSampler));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateUnorderedAccessView, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateUnorderedAccessView));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRenderTargetView, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRenderTargetView));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDepthStencilView, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDepthStencilView));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateRootSignatureSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateRootSignatureSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRootSignature, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRootSignature));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyRootSignature, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyRootSignature));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnSerializeObject, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnSerializeObject));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyObjectSerialization, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyObjectSerialization));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDeserializedObjectSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDeserializedObjectSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDeserializedObject, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDeserializedObject));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnMapHeap, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnMapHeap));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnUnmapHeap, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnUnmapHeap));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateHeapAndResourceSizes, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateHeapAndResourceSizes));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateHeapAndResource, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateHeapAndResource));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyHeapAndResource, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyHeapAndResource));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnMakeResident, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnMakeResident));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnEvict, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnEvict));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateOpenedHeapAndResourceSizes, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateOpenedHeapAndResourceSizes));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnOpenHeapAndResource, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnOpenHeapAndResource));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCopyDescriptors, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCopyDescriptors));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCopyDescriptorsSimple, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCopyDescriptorsSimple));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateQueryHeapSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateQueryHeapSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateQueryHeap, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateQueryHeap));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyQueryHeap, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyQueryHeap));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandSignatureSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandSignatureSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandSignature, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandSignature));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandSignature, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandSignature));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceVirtualAddress, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceVirtualAddress));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceAllocationInfo, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceAllocationInfo));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckSubresourceInfo, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckSubresourceInfo));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckExistingResourceAllocationInfo, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckExistingResourceAllocationInfo));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnOfferResources, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnOfferResources));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnReclaimResources, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnReclaimResources));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetImplicitPhysicalAdapterMask, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetImplicitPhysicalAdapterMask));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetPresentPrivateDriverDataSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetPresentPrivateDriverDataSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnQueryNodeMap, QueryNodeMap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnRetrieveShaderComment, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnRetrieveShaderComment));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceAllocationHandle, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceAllocationHandle));
}
static void FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0003(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 &f) {
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCloseCommandList, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCloseCommandList));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResetCommandList, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResetCommandList));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDrawInstanced, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDrawInstanced));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDrawIndexedInstanced, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDrawIndexedInstanced));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDispatch, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDispatch));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearUnorderedAccessViewUint, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearUnorderedAccessViewUint));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearUnorderedAccessViewFloat, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearUnorderedAccessViewFloat));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearRenderTargetView, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearRenderTargetView));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearDepthStencilView, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearDepthStencilView));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDiscardResource, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDiscardResource));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyTextureRegion, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyTextureRegion));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceCopy, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceCopy));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyTiles, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyTiles));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyBufferRegion, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyBufferRegion));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceResolveSubresource, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceResolveSubresource));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnExecuteBundle, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnExecuteBundle));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnExecuteIndirect, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnExecuteIndirect));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceBarrier, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceBarrier));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnBlt, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnBlt));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnPresent, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnPresent));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnBeginQuery, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnBeginQuery));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnEndQuery, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnEndQuery));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResolveQueryData, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResolveQueryData));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetPredication, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetPredication));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIaSetTopology, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIaSetTopology));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnRsSetViewports, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnRsSetViewports));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnRsSetScissorRects, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnRsSetScissorRects));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOmSetBlendFactor, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOmSetBlendFactor));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOmSetStencilRef, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOmSetStencilRef));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetPipelineState, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetPipelineState));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetDescriptorHeaps, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetDescriptorHeaps));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootSignature, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootSignature));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootSignature, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootSignature));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootDescriptorTable, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootDescriptorTable));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootDescriptorTable, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootDescriptorTable));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRoot32BitConstant, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRoot32BitConstant));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRoot32BitConstant, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRoot32BitConstant));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRoot32BitConstants, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRoot32BitConstants));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRoot32BitConstants, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRoot32BitConstants));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootConstantBufferView, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootConstantBufferView));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootConstantBufferView, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootConstantBufferView));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootShaderResourceView, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootShaderResourceView));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootShaderResourceView, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootShaderResourceView));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootUnorderedAccessView, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootUnorderedAccessView));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootUnorderedAccessView, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootUnorderedAccessView));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIASetIndexBuffer, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIASetIndexBuffer));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIASetVertexBuffers, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIASetVertexBuffers));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSOSetTargets, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSOSetTargets));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOMSetRenderTargets, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOMSetRenderTargets));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetMarker, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetMarker));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearRootArguments, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearRootArguments));
}
static void FillD3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 &f) {
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnExecuteCommandLists, AGX_REJECT(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnExecuteCommandLists));
    f.pfnUnused = nullptr;
    f.pfnUnused2 = nullptr;
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnUpdateTileMappings, AGX_REJECT(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnUpdateTileMappings));
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnCopyTileMappings, AGX_REJECT(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnCopyTileMappings));
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnSignalFence, AGX_REJECT(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnSignalFence));
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnWaitForFence, AGX_REJECT(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnWaitForFence));
}
/* C1: the core tables the runtime requests for the negotiated 0003 device,
 * every entry fail-closed; other tables are refused. */
template<class T> static HRESULT FillTable(void *table, SIZE_T size, void (*fill)(T &)) {
    if (!table || size < sizeof(T)) return E_INVALIDARG;
    memset(table, 0, size);
    fill(*static_cast<T *>(table));
    return S_OK;
}
static HRESULT APIENTRY FillDDITable(D3D12DDI_HADAPTER, D3D12DDI_TABLE_TYPE type, void *table,
                                     SIZE_T size, UINT version, D3D12DDI_HRTTABLE) {
    HRESULT hr = E_NOTIMPL;
    switch (type) {
    case D3D12DDI_TABLE_TYPE_DEVICE_CORE:
        hr = FillTable<D3D12DDI_DEVICE_FUNCS_CORE_0003>(table, size, FillD3D12DDI_DEVICE_FUNCS_CORE_0003);
        break;
    case D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D:
        hr = FillTable<D3D12DDI_COMMAND_LIST_FUNCS_3D_0003>(table, size, FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0003);
        break;
    case D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D:
        hr = FillTable<D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001>(table, size, FillD3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001);
        break;
    default:
        break;
    }
    const UINT receipt[] = {static_cast<UINT>(type), static_cast<UINT>(size), version};
    AdmissionUmdDiagnostic("d3d12-fill-table", hr, receipt, 3);
    return hr;
}
} // namespace
extern "C" HRESULT APIENTRY OpenAdapter12(D3D12DDIARG_OPENADAPTER *arg) {
    char enabled[2] = {};
    if (GetEnvironmentVariableA("APPLE_AGX_D3D12", enabled, sizeof(enabled)) != 1 || enabled[0] != '1')
        return E_NOTIMPL;
    const UINT receipt[] = {arg != nullptr, arg && arg->pAdapterFuncs != nullptr,
                            arg && arg->pAdapterCallbacks != nullptr};
    AdmissionUmdDiagnostic("d3d12-open-adapter", arg && arg->pAdapterFuncs ? S_OK : E_INVALIDARG, receipt, 3);
    if (!arg || !arg->pAdapterFuncs) return E_INVALIDARG;
    adapter_object.runtime = arg->hRTAdapter;
    adapter_object.callbacks = arg->pAdapterCallbacks;
    arg->hAdapter.pDrvPrivate = &adapter_object;
    FillD3D12DDI_ADAPTERFUNCS(*arg->pAdapterFuncs);
    return S_OK;
}
