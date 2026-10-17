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
extern "C" {
#include "apple_agx_win32_device_info.h"
#include "render_win32_transport.h"
/* agx_win32_screen.c (compiled into this UMD) */
int AgxWin32DeviceInfoValid(const AGX_WIN32_DEVICE_INFO *Info);
}

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
    /* C2: the KMD buffer classes (the D3D10 adapter's query, umd_runtime_device.c) */
    AGX_WIN32_DEVICE_INFO info;
    BOOL info_valid;
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
    /* C2: created with the first heap, so device creation is unchanged */
    D3DKMT_HANDLE paging_queue;
    volatile UINT64 *paging_fence;
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
/* C1f (EXP1350): device initialisation creates a root signature after the
 * layout-sets caps; a refused pfnCalcPrivateRootSignatureSize removes the
 * device (0x887a0005). The driver object records the root argument layout
 * in DWORDs as D3D12 defines its cost (descriptor table 1, root constants
 * Num32BitValues, root CBV/SRV/UAV 2); C5 lowers bindings from it. */
struct AgxD3d12RootParameter {
    UINT type;
    UINT visibility;
    UINT offset; /* in DWORDs */
    UINT dwords;
};
struct AgxD3d12RootSignature {
    UINT parameters;
    UINT static_samplers;
    UINT flags;
    UINT dwords;
    AgxD3d12RootParameter parameter[1];
};
static UINT RootParameterDwords(const D3D12DDI_ROOT_PARAMETER &p) {
    switch (p.ParameterType) {
    case D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: return 1;
    case D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: return p.Constants.Num32BitValues;
    default: return 2; /* root CBV/SRV/UAV: a 64-bit GPU virtual address */
    }
}
static SIZE_T APIENTRY CalcPrivateRootSignatureSize(D3D12DDI_HDEVICE,
                                                    const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 *arg) {
    const UINT count = arg && arg->pRootSignature ? arg->pRootSignature->NumParameters : 0;
    return offsetof(AgxD3d12RootSignature, parameter) + (count ? count : 1) * sizeof(AgxD3d12RootParameter);
}
static HRESULT APIENTRY CreateRootSignature(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 *arg,
                                            D3D12DDI_HROOTSIGNATURE handle) {
    auto *root = static_cast<AgxD3d12RootSignature *>(handle.pDrvPrivate);
    if (!arg || !arg->pRootSignature || !root) return E_INVALIDARG;
    const D3D12DDI_ROOT_SIGNATURE &desc = *arg->pRootSignature;
    root->parameters = desc.NumParameters;
    root->static_samplers = desc.NumStaticSamplers;
    root->flags = static_cast<UINT>(desc.Flags);
    root->dwords = 0;
    for (UINT i = 0; i < desc.NumParameters; ++i) {
        AgxD3d12RootParameter &out = root->parameter[i];
        out.type = static_cast<UINT>(desc.pRootParameters[i].ParameterType);
        out.visibility = static_cast<UINT>(desc.pRootParameters[i].ShaderVisibility);
        out.offset = root->dwords;
        out.dwords = RootParameterDwords(desc.pRootParameters[i]);
        root->dwords += out.dwords;
    }
    const UINT receipt[] = {root->parameters, root->static_samplers, root->flags, root->dwords};
    AdmissionUmdDiagnostic("measure-d3d12-root-signature", S_OK, receipt, 4);
    return S_OK;
}
static VOID APIENTRY DestroyRootSignature(D3D12DDI_HDEVICE, D3D12DDI_HROOTSIGNATURE) {}
/* C1g (EXP1351): device initialisation creates a shader after its (empty)
 * root signature; a refused pfnCalcPrivateShaderSize removes the device.
 * The shader object keeps a copy of the D3D10+ tokenized program
 * (pShaderCode[0] = program type and version, pShaderCode[1] = length in
 * DWORDs); compilation to AGX (DXBC -> NIR -> libagx) happens when a
 * pipeline state is created (C5). */
struct AgxD3d12Shader {
    UINT stage; /* the stage the runtime created it for (D3D10_SB program type) */
    UINT dwords;
    UINT tokens[1];
};
static SIZE_T APIENTRY CalcPrivateShaderSize(D3D12DDI_HDEVICE, const UINT *code, D3D12DDI_HROOTSIGNATURE,
                                             const D3D12DDIARG_STAGE_IO_SIGNATURES *) {
    const UINT dwords = code ? code[1] : 0;
    return offsetof(AgxD3d12Shader, tokens) + (dwords ? dwords : 1) * sizeof(UINT);
}
static void StoreShader(const UINT *code, D3D12DDI_HSHADER handle) {
    auto *shader = static_cast<AgxD3d12Shader *>(handle.pDrvPrivate);
    if (!code || !shader || code[1] < 2) {
        if (callbacks && callbacks->pfnSetErrorCb) callbacks->pfnSetErrorCb(runtime_device, E_INVALIDARG);
        return;
    }
    shader->stage = code[0] >> 16;
    shader->dwords = code[1];
    memcpy(shader->tokens, code, code[1] * sizeof(UINT));
    const UINT receipt[] = {shader->stage, code[0] & 0xffffu, shader->dwords};
    AdmissionUmdDiagnostic("measure-d3d12-shader", S_OK, receipt, 3);
}
static VOID APIENTRY CreateShader(D3D12DDI_HDEVICE, const UINT *code, D3D12DDI_HROOTSIGNATURE,
                                  D3D12DDI_HSHADER handle, const D3D12DDIARG_STAGE_IO_SIGNATURES *,
                                  D3D12DDI_CREATE_SHADER_FLAGS) {
    StoreShader(code, handle);
}
static VOID APIENTRY CreateComputeShader(D3D12DDI_HDEVICE, const UINT *code, D3D12DDI_HROOTSIGNATURE,
                                         D3D12DDI_HSHADER handle, D3D12DDI_CREATE_SHADER_FLAGS) {
    StoreShader(code, handle);
}
static VOID APIENTRY DestroyShader(D3D12DDI_HDEVICE, D3D12DDI_HSHADER) {}
/* C1h (EXP1352): after its internal vertex shader (VS 4.0, 15 DWORDs) the
 * runtime builds the state objects a graphics pipeline state references
 * (D3D12DDIARG_CREATE_PIPELINE_STATE_0001: hBlendState, hRasterizerState,
 * hDepthStencilState, hElementLayout); a refused pfnCalcPrivateBlendStateSize
 * removes the device. Each object keeps a copy of its description; the
 * pipeline state translates them to AGX state (C6). */
template<class Desc, UINT Kind> struct AgxD3d12State {
    static SIZE_T APIENTRY size(D3D12DDI_HDEVICE, const Desc *) { return sizeof(Desc); }
    template<class Handle> static VOID APIENTRY create(D3D12DDI_HDEVICE, const Desc *desc, Handle handle) {
        if (!desc || !handle.pDrvPrivate) {
            if (callbacks && callbacks->pfnSetErrorCb) callbacks->pfnSetErrorCb(runtime_device, E_INVALIDARG);
            return;
        }
        memcpy(handle.pDrvPrivate, desc, sizeof(Desc));
        const UINT receipt[] = {Kind, static_cast<UINT>(sizeof(Desc))};
        AdmissionUmdDiagnostic("measure-d3d12-state", S_OK, receipt, 2);
    }
    template<class Handle> static VOID APIENTRY destroy(D3D12DDI_HDEVICE, Handle) {}
};
using BlendState = AgxD3d12State<D3D12DDI_BLEND_DESC, 1>;
using DepthStencilState = AgxD3d12State<D3D12DDI_DEPTH_STENCIL_DESC, 2>;
using RasterizerState = AgxD3d12State<D3D12DDI_RASTERIZER_DESC, 3>;
struct AgxD3d12ElementLayout {
    UINT elements;
    D3D12DDIARG_INPUT_ELEMENT_DESC element[1];
};
static SIZE_T APIENTRY CalcPrivateElementLayoutSize(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATEELEMENTLAYOUT *arg) {
    const UINT count = arg ? arg->NumElements : 0;
    return offsetof(AgxD3d12ElementLayout, element) + (count ? count : 1) * sizeof(D3D12DDIARG_INPUT_ELEMENT_DESC);
}
static VOID APIENTRY CreateElementLayout(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATEELEMENTLAYOUT *arg,
                                         D3D12DDI_HELEMENTLAYOUT handle) {
    auto *layout = static_cast<AgxD3d12ElementLayout *>(handle.pDrvPrivate);
    if (!arg || !layout || (arg->NumElements && !arg->pVertexElements)) {
        if (callbacks && callbacks->pfnSetErrorCb) callbacks->pfnSetErrorCb(runtime_device, E_INVALIDARG);
        return;
    }
    layout->elements = arg->NumElements;
    if (arg->NumElements)
        memcpy(layout->element, arg->pVertexElements, arg->NumElements * sizeof(D3D12DDIARG_INPUT_ELEMENT_DESC));
    const UINT receipt[] = {4u, arg->NumElements};
    AdmissionUmdDiagnostic("measure-d3d12-state", S_OK, receipt, 2);
}
static VOID APIENTRY DestroyElementLayout(D3D12DDI_HDEVICE, D3D12DDI_HELEMENTLAYOUT) {}
/* The pipeline state those objects feed (D3D12DDIARG_CREATE_PIPELINE_STATE_0001):
 * it keeps the creation arguments; the AGX compile of its shaders and the
 * state translation are C5/C6, before any command list can bind it (every
 * command-list DDI is still fail-closed). */
struct AgxD3d12PipelineState {
    D3D12DDIARG_CREATE_PIPELINE_STATE_0001 desc;
};
static SIZE_T APIENTRY CalcPrivatePipelineStateSize(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_PIPELINE_STATE_0001 *) {
    return sizeof(AgxD3d12PipelineState);
}
static HRESULT APIENTRY CreatePipelineState(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_PIPELINE_STATE_0001 *arg) {
    auto *pso = arg ? static_cast<AgxD3d12PipelineState *>(arg->hDrvPipelineState.pDrvPrivate) : nullptr;
    if (!pso) return E_INVALIDARG;
    pso->desc = *arg;
    const UINT receipt[] = {arg->hComputeShader.pDrvPrivate != nullptr, arg->hVertexShader.pDrvPrivate != nullptr,
                            arg->hPixelShader.pDrvPrivate != nullptr, arg->NumRenderTargets,
                            static_cast<UINT>(arg->RTVFormats[0]), static_cast<UINT>(arg->DSVFormat),
                            static_cast<UINT>(arg->PrimitiveTopologyType), arg->SampleDesc.Count};
    AdmissionUmdDiagnostic("measure-d3d12-pso", S_OK, receipt, 8);
    return S_OK;
}
static VOID APIENTRY DestroyPipelineState(D3D12DDI_HDEVICE, D3D12DDI_HPIPELINESTATE) {}
/* C1i (EXP1353): after its internal pipeline state the runtime calls
 * pfnMakeResident; refused, the device is removed. No object this stage
 * creates owns a kernel allocation yet (heaps and resources are C2), so
 * residency has nothing to page in: S_OK without a paging fence. The
 * receipt names what the runtime asks for (count and the first types). */
static void ResidencyReceipt(PCSTR stage, UINT count, const D3D12DDI_HANDLE_AND_TYPE *objects) {
    UINT receipt[5] = {count};
    for (UINT i = 0; objects && i < count && i < 4; ++i) receipt[1 + i] = static_cast<UINT>(objects[i].Type);
    AdmissionUmdDiagnostic(stage, S_OK, receipt, 5);
}
static HRESULT APIENTRY MakeResident(D3D12DDI_HDEVICE, D3D12DDIARG_MAKERESIDENT_0001 *arg) {
    if (!arg) return E_INVALIDARG;
    ResidencyReceipt("measure-d3d12-make-resident", arg->NumObjects, arg->pObjects);
    arg->WaitMask = 0;
    return S_OK;
}
static HRESULT APIENTRY Evict(D3D12DDI_HDEVICE, const D3D12DDIARG_EVICT *arg) {
    if (!arg) return E_INVALIDARG;
    ResidencyReceipt("measure-d3d12-evict", arg->NumObjects, arg->pObjects);
    return S_OK;
}
/* C2a (D3D12-C plan C2): committed heaps and buffers. A heap is one KMD
 * General-class allocation (render_win32_transport.h, the GPUVA winsys's
 * class buffer contract: CPU-visible system memory up to 16 MiB for heaps
 * with a CPU page property, GPU-local otherwise), made resident and mapped
 * at a 64 KiB-aligned GPU VA in the winsys window [64 KiB, 2^39); it stays
 * resident until destroyed, so pfnMakeResident/pfnEvict have nothing to
 * page. A committed buffer is the whole heap. Textures and placed
 * resources are not implemented yet: their allocation info reports
 * UINT64_MAX and their creation fails. */
static constexpr UINT64 kHeapPage = 0x10000ULL;
static constexpr UINT64 kVaMinimum = 0x10000ULL;
static constexpr UINT64 kVaLimit = 1ULL << 39;
static constexpr UINT64 kCpuVisibleMaximum = ADMISSION_WIN32_ALLOCATION_LEGACY_MAX_BYTES;
struct AgxD3d12Heap {
    D3DKMT_HANDLE allocation;
    UINT64 gpuva;
    UINT64 bytes;
    BOOL cpu_visible;
    UINT locks;
    void *cpu;
};
struct AgxD3d12Resource {
    AgxD3d12Heap *heap;
    UINT64 gpuva;
    UINT64 bytes;
};
static AgxD3d12Device *DeviceOf(D3D12DDI_HDEVICE handle) {
    return static_cast<AgxD3d12Device *>(handle.pDrvPrivate);
}
static VOID APIENTRY CheckResourceAllocationInfo(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATERESOURCE_0003 *arg,
                                                 D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS, UINT64, UINT,
                                                 D3D12DDI_RESOURCE_ALLOCATION_INFO *info) {
    if (!info) return;
    memset(info, 0, sizeof(*info));
    info->ResourceDataAlignment = kHeapPage;
    info->Layout = D3D12DDI_TL_ROW_MAJOR;
    if (arg && arg->ResourceType == D3D12DDI_RT_BUFFER && arg->Width && arg->Width <= kVaLimit - kVaMinimum)
        info->ResourceDataSize = (arg->Width + kHeapPage - 1) & ~(kHeapPage - 1);
    else
        info->ResourceDataSize = ~0ULL; /* not implemented: textures (C2b) */
    const UINT receipt[] = {arg ? static_cast<UINT>(arg->ResourceType) : 0u,
                            arg ? static_cast<UINT>(arg->Width) : 0u,
                            static_cast<UINT>(info->ResourceDataSize >> 16)};
    AdmissionUmdDiagnostic("measure-d3d12-allocation-info", S_OK, receipt, 3);
}
/* EXP1355: the first committed buffer failed (0x887a0005) after its
 * allocation info with no heap receipt; these entry receipts name what the
 * runtime passes ({heap present, ByteSize/64K, CPU page property, pool,
 * flags}, {resource present, type, width, layout}). */
static void HeapAndResourceReceipt(PCSTR stage, const D3D12DDIARG_CREATEHEAP_0001 *heap,
                                   const D3D12DDIARG_CREATERESOURCE_0003 *resource, UINT handles) {
    const UINT receipt[] = {heap ? 1u : 0u, heap ? static_cast<UINT>(heap->ByteSize >> 16) : 0u,
                            heap ? static_cast<UINT>(heap->CPUPageProperty) : 0u,
                            heap ? static_cast<UINT>(heap->MemoryPool) : 0u,
                            heap ? static_cast<UINT>(heap->Flags) : 0u, resource ? 1u : 0u,
                            resource ? static_cast<UINT>(resource->ResourceType) : 0u,
                            resource ? static_cast<UINT>(resource->Width) : 0u,
                            resource ? static_cast<UINT>(resource->Layout) : 0u, handles};
    AdmissionUmdDiagnostic(stage, S_OK, receipt, 10);
}
static D3D12DDI_HEAP_AND_RESOURCE_SIZES APIENTRY CalcPrivateHeapAndResourceSizes(
    D3D12DDI_HDEVICE, const D3D12DDIARG_CREATEHEAP_0001 *heap, const D3D12DDIARG_CREATERESOURCE_0003 *resource) {
    HeapAndResourceReceipt("measure-d3d12-heap-sizes", heap, resource, 0);
    return {sizeof(AgxD3d12Heap), sizeof(AgxD3d12Resource)};
}
static HRESULT WaitPaging(AgxD3d12Device *device, UINT64 fence) {
    for (UINT i = 0; fence && device->paging_fence && *device->paging_fence < fence; ++i) {
        if (i > 4000) return E_FAIL; /* about 4 s */
        Sleep(1);
    }
    return S_OK;
}
static HRESULT EnsurePagingQueue(AgxD3d12Device *device) {
    if (device->paging_queue) return S_OK;
    if (!device->kernel || !device->kernel->pfnCreatePagingQueueCb) return E_FAIL;
    D3DDDICB_CREATEPAGINGQUEUE queue = {};
    queue.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    HRESULT hr = device->kernel->pfnCreatePagingQueueCb(device->runtime.handle, &queue);
    if (FAILED(hr) || !queue.hPagingQueue || !queue.FenceValueCPUVirtualAddress)
        return FAILED(hr) ? hr : E_FAIL;
    device->paging_queue = queue.hPagingQueue;
    device->paging_fence = static_cast<volatile UINT64 *>(queue.FenceValueCPUVirtualAddress);
    return S_OK;
}
static void ReleaseHeap(AgxD3d12Device *device, AgxD3d12Heap *heap) {
    if (heap->locks && device->kernel->pfnUnlockCb) {
        D3DDDICB_UNLOCK unlock = {};
        unlock.NumAllocations = 1;
        unlock.phAllocations = &heap->allocation;
        (void)device->kernel->pfnUnlockCb(device->runtime.handle, &unlock);
    }
    if (heap->gpuva && device->kernel->pfnFreeGpuVirtualAddressCb) {
        D3DDDICB_FREEGPUVIRTUALADDRESS va = {};
        va.BaseAddress = heap->gpuva;
        va.Size = heap->bytes;
        (void)device->kernel->pfnFreeGpuVirtualAddressCb(device->runtime.handle, &va);
    }
    if (heap->allocation && device->kernel->pfnDeallocateCb) {
        D3DDDICB_DEALLOCATE release = {};
        release.NumAllocations = 1;
        release.HandleList = &heap->allocation;
        (void)device->kernel->pfnDeallocateCb(device->runtime.handle, &release);
    }
    memset(heap, 0, sizeof(*heap));
}
static HRESULT CreateHeap(AgxD3d12Device *device, const D3D12DDIARG_CREATEHEAP_0001 &arg, AgxD3d12Heap *heap) {
    const D3DDDI_DEVICECALLBACKS *k = device->kernel;
    memset(heap, 0, sizeof(*heap));
    heap->cpu_visible = arg.CPUPageProperty != D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
    heap->bytes = (arg.ByteSize + kHeapPage - 1) & ~(kHeapPage - 1);
    if (!adapter_object.info_valid || !k || !k->pfnAllocateCb || !k->pfnReserveGpuVirtualAddressCb ||
        !k->pfnMapGpuVirtualAddressCb || !k->pfnMakeResidentCb || !arg.ByteSize ||
        heap->bytes > (heap->cpu_visible ? kCpuVisibleMaximum : kVaLimit - kVaMinimum) || heap->bytes > MAXUINT32)
        return E_OUTOFMEMORY;
    HRESULT hr = EnsurePagingQueue(device);
    if (FAILED(hr)) return hr;
    ADMISSION_WIN32_ALLOCATION_CREATE create = {};
    if (!AdmissionAllocationDescribe(static_cast<UINT>(heap->bytes), 1u, 1u,
                                     ADMISSION_WIN32_ALLOCATION_STAGING_CPUVISIBLE,
                                     ADMISSION_WIN32_ALLOCATION_FORMAT_A8, 1u, &create.Allocation))
        return E_INVALIDARG;
    create.Magic = ADMISSION_WIN32_ALLOCATION_MAGIC;
    create.Version = ADMISSION_WIN32_ALLOCATION_VERSION;
    if (!heap->cpu_visible) {
        create.Version = ADMISSION_WIN32_ALLOCATION_VERSION_LOCAL;
        create.Allocation.Type = ADMISSION_WIN32_ALLOCATION_GPU_LOCAL;
        create.Allocation.CpuVisible = 0u;
    }
    create.Bytes = sizeof(create);
    create.ClassId = AgxWin32BufferClassGeneral;
    create.Flags = AppleAgxWin32BufferCpuRead | AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead |
                   AppleAgxWin32BufferGpuWrite;
    D3DDDI_ALLOCATIONINFO info = {};
    info.pPrivateDriverData = &create;
    info.PrivateDriverDataSize = sizeof(create);
    D3DDDICB_ALLOCATE allocate = {};
    allocate.NumAllocations = 1;
    allocate.pAllocationInfo = &info;
    hr = k->pfnAllocateCb(device->runtime.handle, &allocate);
    heap->allocation = info.hAllocation;
    if (SUCCEEDED(hr) && !heap->allocation) hr = E_FAIL;
    if (SUCCEEDED(hr)) {
        D3DDDI_MAKERESIDENT resident = {};
        resident.hPagingQueue = device->paging_queue;
        resident.NumAllocations = 1;
        resident.AllocationList = &heap->allocation;
        resident.Flags.MustSucceed = 1;
        hr = k->pfnMakeResidentCb(device->runtime.handle, &resident);
        if (hr == E_PENDING) hr = WaitPaging(device, resident.PagingFenceValue);
    }
    if (SUCCEEDED(hr)) {
        D3DDDI_RESERVEGPUVIRTUALADDRESS reserve = {};
        reserve.MinimumAddress = kVaMinimum;
        reserve.MaximumAddress = kVaLimit;
        reserve.Size = heap->bytes;
        hr = k->pfnReserveGpuVirtualAddressCb(device->runtime.handle, &reserve);
        heap->gpuva = reserve.VirtualAddress;
        if (SUCCEEDED(hr) && (heap->gpuva < kVaMinimum || (heap->gpuva & (kHeapPage - 1)) ||
                              heap->gpuva >= kVaLimit || heap->bytes > kVaLimit - heap->gpuva))
            hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) {
        D3DDDI_MAPGPUVIRTUALADDRESS map = {};
        map.hPagingQueue = device->paging_queue;
        map.BaseAddress = heap->gpuva;
        map.hAllocation = heap->allocation;
        map.SizeInPages = heap->bytes >> 12;
        map.Protection.Write = 1;
        hr = k->pfnMapGpuVirtualAddressCb(device->runtime.handle, &map);
        if (SUCCEEDED(hr) || hr == E_PENDING) hr = map.VirtualAddress == heap->gpuva ? WaitPaging(device, map.PagingFenceValue) : E_FAIL;
    }
    const UINT receipt[] = {static_cast<UINT>(heap->cpu_visible), static_cast<UINT>(heap->bytes >> 16), heap->allocation != 0u,
                            static_cast<UINT>(heap->gpuva >> 16), static_cast<UINT>(heap->gpuva >> 32)};
    AdmissionUmdDiagnostic("measure-d3d12-heap", hr, receipt, 5);
    if (FAILED(hr)) ReleaseHeap(device, heap);
    return hr;
}
static HRESULT APIENTRY CreateHeapAndResource(D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATEHEAP_0001 *heap_arg,
                                              D3D12DDI_HHEAP heap_handle, D3D12DDI_HRTRESOURCE,
                                              const D3D12DDIARG_CREATERESOURCE_0003 *resource_arg,
                                              const D3D12DDI_CLEAR_VALUES *, D3D12DDI_HRESOURCE resource_handle) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *heap = static_cast<AgxD3d12Heap *>(heap_handle.pDrvPrivate);
    auto *resource = static_cast<AgxD3d12Resource *>(resource_handle.pDrvPrivate);
    HeapAndResourceReceipt("measure-d3d12-create-heap-resource", heap_arg, resource_arg,
                           (heap ? 1u : 0u) | (resource ? 2u : 0u));
    if (!device) return E_INVALIDARG;
    /* committed buffers only (placed resources and textures: C2b) */
    if (!heap_arg || !heap || (resource_arg && (!resource || resource_arg->ResourceType != D3D12DDI_RT_BUFFER ||
                                                resource_arg->Width > heap_arg->ByteSize)))
        return E_NOTIMPL;
    HRESULT hr = CreateHeap(device, *heap_arg, heap);
    if (SUCCEEDED(hr) && resource_arg) {
        resource->heap = heap;
        resource->gpuva = heap->gpuva;
        resource->bytes = resource_arg->Width;
    }
    return hr;
}
static VOID APIENTRY DestroyHeapAndResource(D3D12DDI_HDEVICE handle, D3D12DDI_HHEAP heap_handle,
                                            D3D12DDI_HRESOURCE resource_handle) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *heap = static_cast<AgxD3d12Heap *>(heap_handle.pDrvPrivate);
    if (resource_handle.pDrvPrivate) memset(resource_handle.pDrvPrivate, 0, sizeof(AgxD3d12Resource));
    if (device && heap && heap->allocation) ReleaseHeap(device, heap);
}
static HRESULT APIENTRY MapHeap(D3D12DDI_HDEVICE handle, D3D12DDI_HHEAP heap_handle, VOID **data) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *heap = static_cast<AgxD3d12Heap *>(heap_handle.pDrvPrivate);
    if (!device || !heap || !data || !heap->allocation || !heap->cpu_visible || !device->kernel->pfnLockCb)
        return E_INVALIDARG;
    if (!heap->locks) {
        D3DDDICB_LOCK lock = {};
        lock.hAllocation = heap->allocation;
        lock.Flags.LockEntire = 1;
        HRESULT hr = device->kernel->pfnLockCb(device->runtime.handle, &lock);
        if (FAILED(hr) || !lock.pData) return FAILED(hr) ? hr : E_FAIL;
        heap->cpu = lock.pData;
    }
    ++heap->locks;
    *data = heap->cpu;
    return S_OK;
}
static VOID APIENTRY UnmapHeap(D3D12DDI_HDEVICE handle, D3D12DDI_HHEAP heap_handle) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *heap = static_cast<AgxD3d12Heap *>(heap_handle.pDrvPrivate);
    if (!device || !heap || !heap->locks || --heap->locks) return;
    D3DDDICB_UNLOCK unlock = {};
    unlock.NumAllocations = 1;
    unlock.phAllocations = &heap->allocation;
    (void)device->kernel->pfnUnlockCb(device->runtime.handle, &unlock);
    heap->cpu = nullptr;
}
static D3D12DDI_GPU_VIRTUAL_ADDRESS APIENTRY CheckResourceVirtualAddress(D3D12DDI_HDEVICE, D3D12DDI_HRESOURCE handle) {
    auto *resource = static_cast<AgxD3d12Resource *>(handle.pDrvPrivate);
    return resource ? resource->gpuva : 0;
}
/* C1j (EXP1354): after its internal compute pipeline the runtime asks the
 * descriptor sizes; refused, the device is removed. Sizes follow what each
 * descriptor will hold (Asahi genxml: Texture 24 bytes, PBE 24, Sampler 8,
 * border colour 16): a CBV/SRV/UAV descriptor = texture + PBE + {buffer
 * VA, size, kind} in 64 bytes, a sampler = sampler + border colour in 32,
 * RTV/DSV = a 64-byte CPU record of the view. Shader-visible heaps live in
 * one CPU-visible C2a heap (16 MiB class bound) kept locked, so a CPU
 * handle is its pointer and a GPU handle its VA; other heaps are process
 * memory. The views written into them are not implemented yet. */
static UINT DescriptorBytes(D3D12DDI_DESCRIPTOR_HEAP_TYPE type) {
    return type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_SAMPLER ? 32u : 64u;
}
static UINT APIENTRY GetDescriptorSizeInBytes(D3D12DDI_HDEVICE, D3D12DDI_DESCRIPTOR_HEAP_TYPE type) {
    return DescriptorBytes(type);
}
struct AgxD3d12DescriptorHeap {
    AgxD3d12Heap memory; /* shader-visible heaps */
    void *cpu;
    UINT64 gpuva;
    UINT type;
    UINT descriptors;
};
static SIZE_T APIENTRY CalcPrivateDescriptorHeapSize(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 *) {
    return sizeof(AgxD3d12DescriptorHeap);
}
static HRESULT APIENTRY CreateDescriptorHeap(D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 *arg,
                                             D3D12DDI_HDESCRIPTORHEAP heap_handle) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *heap = static_cast<AgxD3d12DescriptorHeap *>(heap_handle.pDrvPrivate);
    if (!device || !arg || !heap || arg->Type >= D3D12DDI_DESCRIPTOR_HEAP_TYPE_NUM_TYPES || !arg->NumDescriptors)
        return E_INVALIDARG;
    memset(heap, 0, sizeof(*heap));
    heap->type = static_cast<UINT>(arg->Type);
    heap->descriptors = arg->NumDescriptors;
    const UINT64 bytes = static_cast<UINT64>(arg->NumDescriptors) * DescriptorBytes(arg->Type);
    HRESULT hr = S_OK;
    if (arg->Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
        D3D12DDIARG_CREATEHEAP_0001 memory = {};
        memory.ByteSize = bytes;
        memory.Alignment = kHeapPage;
        memory.MemoryPool = D3D12DDI_MEMORY_POOL_L0;
        memory.CPUPageProperty = D3D12DDI_CPU_PAGE_PROPERTY_WRITE_COMBINE;
        hr = CreateHeap(device, memory, &heap->memory);
        if (SUCCEEDED(hr)) {
            D3D12DDI_HHEAP memory_handle = {&heap->memory};
            hr = MapHeap(handle, memory_handle, &heap->cpu);
            if (FAILED(hr)) ReleaseHeap(device, &heap->memory);
        }
        heap->gpuva = SUCCEEDED(hr) ? heap->memory.gpuva : 0;
    } else {
        heap->cpu = VirtualAlloc(nullptr, static_cast<SIZE_T>(bytes), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!heap->cpu) hr = E_OUTOFMEMORY;
    }
    const UINT receipt[] = {heap->type, heap->descriptors, static_cast<UINT>(arg->Flags), heap->gpuva != 0};
    AdmissionUmdDiagnostic("measure-d3d12-descriptor-heap", hr, receipt, 4);
    return hr;
}
static VOID APIENTRY DestroyDescriptorHeap(D3D12DDI_HDEVICE handle, D3D12DDI_HDESCRIPTORHEAP heap_handle) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *heap = static_cast<AgxD3d12DescriptorHeap *>(heap_handle.pDrvPrivate);
    if (!device || !heap) return;
    if (heap->memory.allocation) ReleaseHeap(device, &heap->memory);
    else if (heap->cpu) VirtualFree(heap->cpu, 0, MEM_RELEASE);
    memset(heap, 0, sizeof(*heap));
}
static D3D12DDI_CPU_DESCRIPTOR_HANDLE APIENTRY GetCPUDescriptorHandleForHeapStart(D3D12DDI_HDEVICE,
                                                                                 D3D12DDI_HDESCRIPTORHEAP handle) {
    auto *heap = static_cast<AgxD3d12DescriptorHeap *>(handle.pDrvPrivate);
    D3D12DDI_CPU_DESCRIPTOR_HANDLE start = {};
    start.ptr = heap ? reinterpret_cast<SIZE_T>(heap->cpu) : 0;
    return start;
}
static D3D12DDI_GPU_DESCRIPTOR_HANDLE APIENTRY GetGPUDescriptorHandleForHeapStart(D3D12DDI_HDEVICE,
                                                                                 D3D12DDI_HDESCRIPTORHEAP handle) {
    auto *heap = static_cast<AgxD3d12DescriptorHeap *>(handle.pDrvPrivate);
    D3D12DDI_GPU_DESCRIPTOR_HANDLE start = {};
    start.ptr = heap ? heap->gpuva : 0;
    return start;
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
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateElementLayoutSize, CalcPrivateElementLayoutSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateElementLayout, CreateElementLayout);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyElementLayout, DestroyElementLayout);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateBlendStateSize, BlendState::size);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateBlendState, BlendState::create<D3D12DDI_HBLENDSTATE>);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyBlendState, BlendState::destroy<D3D12DDI_HBLENDSTATE>);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDepthStencilStateSize, DepthStencilState::size);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDepthStencilState, DepthStencilState::create<D3D12DDI_HDEPTHSTENCILSTATE>);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyDepthStencilState, DepthStencilState::destroy<D3D12DDI_HDEPTHSTENCILSTATE>);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateRasterizerStateSize, RasterizerState::size);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRasterizerState, RasterizerState::create<D3D12DDI_HRASTERIZERSTATE>);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyRasterizerState, RasterizerState::destroy<D3D12DDI_HRASTERIZERSTATE>);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateShaderSize, CalcPrivateShaderSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateVertexShader, CreateShader);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreatePixelShader, CreateShader);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateGeometryShader, CreateShader);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateComputeShader, CreateComputeShader);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateGeometryShaderWithStreamOutput, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateGeometryShaderWithStreamOutput));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateGeometryShaderWithStreamOutput, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateGeometryShaderWithStreamOutput));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateTessellationShaderSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateTessellationShaderSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateHullShader, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateHullShader));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDomainShader, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDomainShader));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyShader, DestroyShader);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandQueueSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandQueueSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandQueue, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandQueue));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandQueue, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandQueue));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandAllocatorSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandAllocatorSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandAllocator, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandAllocator));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandAllocator, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandAllocator));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnResetCommandAllocator, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnResetCommandAllocator));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivatePipelineStateSize, CalcPrivatePipelineStateSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreatePipelineState, CreatePipelineState);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyPipelineState, DestroyPipelineState);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandListSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandListSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandList, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandList));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandList, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandList));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateFenceSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateFenceSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateFence, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateFence));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyFence, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyFence));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDescriptorHeapSize, CalcPrivateDescriptorHeapSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDescriptorHeap, CreateDescriptorHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyDescriptorHeap, DestroyDescriptorHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetDescriptorSizeInBytes, GetDescriptorSizeInBytes);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetCPUDescriptorHandleForHeapStart, GetCPUDescriptorHandleForHeapStart);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetGPUDescriptorHandleForHeapStart, GetGPUDescriptorHandleForHeapStart);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateShaderResourceView, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateShaderResourceView));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateConstantBufferView, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateConstantBufferView));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateSampler, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateSampler));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateUnorderedAccessView, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateUnorderedAccessView));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRenderTargetView, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRenderTargetView));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDepthStencilView, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDepthStencilView));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateRootSignatureSize, CalcPrivateRootSignatureSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRootSignature, CreateRootSignature);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyRootSignature, DestroyRootSignature);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnSerializeObject, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnSerializeObject));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyObjectSerialization, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyObjectSerialization));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDeserializedObjectSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDeserializedObjectSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDeserializedObject, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDeserializedObject));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnMapHeap, MapHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnUnmapHeap, UnmapHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateHeapAndResourceSizes, CalcPrivateHeapAndResourceSizes);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateHeapAndResource, CreateHeapAndResource);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyHeapAndResource, DestroyHeapAndResource);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnMakeResident, MakeResident);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnEvict, Evict);
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
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceVirtualAddress, CheckResourceVirtualAddress);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceAllocationInfo, CheckResourceAllocationInfo);
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
    adapter_object.info_valid = FALSE;
    if (arg->pAdapterCallbacks && arg->pAdapterCallbacks->pfnQueryAdapterInfoCb) {
        D3DDDICB_QUERYADAPTERINFO query = {};
        query.pPrivateDriverData = &adapter_object.info;
        query.PrivateDriverDataSize = sizeof(adapter_object.info);
        adapter_object.info_valid =
            SUCCEEDED(arg->pAdapterCallbacks->pfnQueryAdapterInfoCb(arg->hRTAdapter.handle, &query)) &&
            AgxWin32DeviceInfoValid(&adapter_object.info);
    }
    arg->hAdapter.pDrvPrivate = &adapter_object;
    FillD3D12DDI_ADAPTERFUNCS(*arg->pAdapterFuncs);
    return S_OK;
}
