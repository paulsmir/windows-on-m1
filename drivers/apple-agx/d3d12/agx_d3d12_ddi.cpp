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
#include <climits>
#include <cstring>
#include <type_traits>
extern "C" {
#include "apple_agx_win32_device_info.h"
#include "render_win32_transport.h"
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
#include "agx_d3d12_windows.h"
#endif
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
static D3D12DDI_HRTTABLE list_table;
static volatile LONG queue_generation; /* per-device Win32 generations */
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
    /* C4b: the native Asahi owner (agx_d3d12_windows.cpp), created with the
     * first heap; heaps are its buffer resources and lists copy through it. */
    void *owner;
    BOOL owner_failed;
    /* EXP1365: the KMD binds one Win32 generation per kernel device, so the
     * owner's context and every queue context carry this one */
    ULONG generation;
    /* C5: the owner heaps by GPU VA, so a root descriptor (a VA) resolves
     * to its buffer and offset */
    UINT heap_count;
    struct AgxD3d12Heap *heaps[4096];
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
        device->generation = ((((GetCurrentProcessId() & 0xffffu) << 16) ^
                               static_cast<ULONG>(InterlockedIncrement(&queue_generation))) | 0x80000000u);
        callbacks = arg->p12UMCallbacks;
        runtime_device = arg->hRTDevice;
    }
    const UINT receipt[] = {arg ? arg->Interface : 0, arg ? arg->Version : 0,
                            arg ? static_cast<UINT>(arg->Flags) : 0};
    AdmissionUmdDiagnostic("d3d12-create-device", hr, receipt, 3);
    return hr;
}
static void APIENTRY DestroyDevice(D3D12DDI_HDEVICE handle) {
    AdmissionUmdDiagnostic("d3d12-destroy-device", S_OK, nullptr, 0);
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    auto *device = static_cast<AgxD3d12Device *>(handle.pDrvPrivate);
    if (device && device->owner) {
        (void)AgxD3d12OwnerDestroy(static_cast<AGX_D3D12_OWNER *>(device->owner));
        device->owner = nullptr;
    }
#else
    (void)handle;
#endif
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
    /* C5: root constants and root CBV/SRV/UAV name one shader register */
    UINT shader_register;
    UINT space;
    /* C5c: a descriptor table's ranges in the signature's range array */
    UINT first_range;
    UINT ranges;
};
/* C5c: one descriptor range of a table (offset resolved: APPEND follows the
 * previous range) */
struct AgxD3d12Range {
    UINT type;
    UINT count; /* UINT_MAX = unbounded */
    UINT base_register;
    UINT space;
    UINT offset; /* in descriptors from the table start */
};
struct AgxD3d12RootSignature {
    UINT parameters;
    UINT static_samplers;
    UINT flags;
    UINT dwords;
    UINT range_count;
    AgxD3d12RootParameter parameter[1];
    /* followed by AgxD3d12Range[range_count] */
};
static AgxD3d12Range *RangesOf(const AgxD3d12RootSignature *root) {
    return reinterpret_cast<AgxD3d12Range *>(
        const_cast<AgxD3d12RootParameter *>(&root->parameter[root->parameters ? root->parameters : 1]));
}
/* C6e: the static samplers follow the ranges */
static D3D12DDI_STATIC_SAMPLER *StaticSamplersOf(const AgxD3d12RootSignature *root) {
    return reinterpret_cast<D3D12DDI_STATIC_SAMPLER *>(RangesOf(root) + root->range_count);
}
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
    SIZE_T ranges = 0;
    for (UINT i = 0; i < count; ++i)
        if (arg->pRootSignature->pRootParameters[i].ParameterType == D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            ranges += arg->pRootSignature->pRootParameters[i].DescriptorTable.NumDescriptorRanges;
    const SIZE_T samplers = arg && arg->pRootSignature ? arg->pRootSignature->NumStaticSamplers : 0;
    return offsetof(AgxD3d12RootSignature, parameter) + (count ? count : 1) * sizeof(AgxD3d12RootParameter) +
           ranges * sizeof(AgxD3d12Range) + samplers * sizeof(D3D12DDI_STATIC_SAMPLER);
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
    root->range_count = 0;
    AgxD3d12Range *ranges = RangesOf(root);
    for (UINT i = 0; i < desc.NumParameters; ++i) {
        AgxD3d12RootParameter &out = root->parameter[i];
        out.type = static_cast<UINT>(desc.pRootParameters[i].ParameterType);
        out.visibility = static_cast<UINT>(desc.pRootParameters[i].ShaderVisibility);
        out.offset = root->dwords;
        out.dwords = RootParameterDwords(desc.pRootParameters[i]);
        out.shader_register = out.space = out.first_range = out.ranges = 0;
        if (desc.pRootParameters[i].ParameterType == D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
            const D3D12DDI_ROOT_DESCRIPTOR_TABLE &table = desc.pRootParameters[i].DescriptorTable;
            out.first_range = root->range_count;
            out.ranges = table.NumDescriptorRanges;
            UINT next = 0;
            for (UINT r = 0; r < table.NumDescriptorRanges; ++r) {
                const D3D12DDI_DESCRIPTOR_RANGE &in = table.pDescriptorRanges[r];
                AgxD3d12Range &range = ranges[root->range_count++];
                range.type = static_cast<UINT>(in.RangeType);
                range.count = in.NumDescriptors;
                range.base_register = in.BaseShaderRegister;
                range.space = in.RegisterSpace;
                range.offset = in.OffsetInDescriptorsFromTableStart == static_cast<UINT>(D3D12DDI_DESCRIPTOR_RANGE_OFFSET_APPEND)
                                   ? next : in.OffsetInDescriptorsFromTableStart;
                next = in.NumDescriptors == UINT_MAX ? UINT_MAX : range.offset + in.NumDescriptors;
            }
        }
        if (desc.pRootParameters[i].ParameterType == D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {
            out.shader_register = desc.pRootParameters[i].Constants.ShaderRegister;
            out.space = desc.pRootParameters[i].Constants.RegisterSpace;
        } else if (desc.pRootParameters[i].ParameterType != D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
            out.shader_register = desc.pRootParameters[i].Descriptor.ShaderRegister;
            out.space = desc.pRootParameters[i].Descriptor.RegisterSpace;
        }
        root->dwords += out.dwords;
    }
    if (desc.NumStaticSamplers && desc.pStaticSamplers)
        memcpy(StaticSamplersOf(root), desc.pStaticSamplers, desc.NumStaticSamplers * sizeof(D3D12DDI_STATIC_SAMPLER));
    else
        root->static_samplers = 0;
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
 * it keeps the creation arguments; graphics state translation is C6. C5: a
 * compute pipeline keeps its own copy of the compute program (the runtime
 * may destroy the shader object first) and compiles it on the device's
 * native owner when a list first dispatches it, so device initialisation's
 * internal compute pipeline needs no owner. */
/* C6b: a graphics pipeline also keeps copies of its VS/PS programs and of
 * the blend, rasterizer, depth-stencil and element-layout descriptions, and
 * compiles them on the owner at its first draw. */
static constexpr UINT kPipelineElements = 16;
struct AgxD3d12PipelineState {
    D3D12DDIARG_CREATE_PIPELINE_STATE_0001 desc;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    AGX_D3D12_COMPUTE compute;
    AGX_D3D12_GRAPHICS graphics;
#endif
    HRESULT compiled; /* S_FALSE until the first compile */
    D3D12DDI_BLEND_DESC blend;
    D3D12DDI_RASTERIZER_DESC rasterizer;
    D3D12DDI_DEPTH_STENCIL_DESC depth_stencil;
    UINT elements;
    D3D12DDIARG_INPUT_ELEMENT_DESC element[kPipelineElements];
    UINT cs_dwords, vs_dwords, ps_dwords;
    UINT cs[1]; /* the CS program, then the VS, then the PS */
};
static const AgxD3d12Shader *ComputeShaderOf(const D3D12DDIARG_CREATE_PIPELINE_STATE_0001 *arg) {
    return arg ? static_cast<const AgxD3d12Shader *>(arg->hComputeShader.pDrvPrivate) : nullptr;
}
static const AgxD3d12Shader *ShaderOf(D3D12DDI_HSHADER handle) {
    return static_cast<const AgxD3d12Shader *>(handle.pDrvPrivate);
}
static SIZE_T APIENTRY CalcPrivatePipelineStateSize(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_PIPELINE_STATE_0001 *arg) {
    const AgxD3d12Shader *cs = ComputeShaderOf(arg);
    const AgxD3d12Shader *vs = arg ? ShaderOf(arg->hVertexShader) : nullptr;
    const AgxD3d12Shader *ps = arg ? ShaderOf(arg->hPixelShader) : nullptr;
    const SIZE_T dwords = (cs ? cs->dwords : 0) + (vs ? vs->dwords : 0) + (ps ? ps->dwords : 0);
    return offsetof(AgxD3d12PipelineState, cs) + (dwords ? dwords : 1) * sizeof(UINT);
}
static HRESULT APIENTRY CreatePipelineState(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_PIPELINE_STATE_0001 *arg) {
    auto *pso = arg ? static_cast<AgxD3d12PipelineState *>(arg->hDrvPipelineState.pDrvPrivate) : nullptr;
    if (!pso) return E_INVALIDARG;
    memset(pso, 0, offsetof(AgxD3d12PipelineState, cs));
    pso->desc = *arg;
    pso->compiled = S_FALSE;
    if (const AgxD3d12Shader *cs = ComputeShaderOf(arg)) {
        pso->cs_dwords = cs->dwords;
        memcpy(pso->cs, cs->tokens, cs->dwords * sizeof(UINT));
    }
    if (const AgxD3d12Shader *vs = ShaderOf(arg->hVertexShader)) {
        pso->vs_dwords = vs->dwords;
        memcpy(pso->cs + pso->cs_dwords, vs->tokens, vs->dwords * sizeof(UINT));
    }
    if (const AgxD3d12Shader *ps = ShaderOf(arg->hPixelShader)) {
        pso->ps_dwords = ps->dwords;
        memcpy(pso->cs + pso->cs_dwords + pso->vs_dwords, ps->tokens, ps->dwords * sizeof(UINT));
    }
    if (arg->hBlendState.pDrvPrivate) memcpy(&pso->blend, arg->hBlendState.pDrvPrivate, sizeof(pso->blend));
    if (arg->hRasterizerState.pDrvPrivate) memcpy(&pso->rasterizer, arg->hRasterizerState.pDrvPrivate, sizeof(pso->rasterizer));
    if (arg->hDepthStencilState.pDrvPrivate)
        memcpy(&pso->depth_stencil, arg->hDepthStencilState.pDrvPrivate, sizeof(pso->depth_stencil));
    if (const auto *layout = static_cast<const AgxD3d12ElementLayout *>(arg->hElementLayout.pDrvPrivate)) {
        pso->elements = layout->elements < kPipelineElements ? layout->elements : kPipelineElements + 1u; /* > max: refused at draw */
        if (layout->elements <= kPipelineElements)
            memcpy(pso->element, layout->element, layout->elements * sizeof(D3D12DDIARG_INPUT_ELEMENT_DESC));
    }
    const UINT receipt[] = {arg->hComputeShader.pDrvPrivate != nullptr, arg->hVertexShader.pDrvPrivate != nullptr,
                            arg->hPixelShader.pDrvPrivate != nullptr, arg->NumRenderTargets,
                            static_cast<UINT>(arg->RTVFormats[0]), static_cast<UINT>(arg->DSVFormat),
                            static_cast<UINT>(arg->PrimitiveTopologyType), arg->SampleDesc.Count};
    AdmissionUmdDiagnostic("measure-d3d12-pso", S_OK, receipt, 8);
    return S_OK;
}
static VOID APIENTRY DestroyPipelineState(D3D12DDI_HDEVICE, D3D12DDI_HPIPELINESTATE);
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
    void *native; /* C4b: the owner's buffer resource (pipe_resource) */
    BOOL alias;   /* C8c: a placed resource's view of its parent heap (owns nothing) */
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
    /* C6a: a committed 2D texture (type D3D12DDI_RT_TEXTURE2D); heap->native
     * is the owner texture */
    UINT type;
    DXGI_FORMAT format;
    UINT width, height, layers, levels;
};
#ifndef APPLE_AGX_D3D12_NATIVE_OWNER
static UINT AgxD3d12FormatBlockBytes(DXGI_FORMAT) { return 0; } /* textures need the native owner */
#endif
/* C6a: the allocation size of a committed 2D texture: an upper bound of
 * the owner's layout (whole 16x16 tiles of each level, a 64 KiB granule);
 * 0 = not a texture this device creates (format, samples, dimension). */
static UINT64 TextureBytes(const D3D12DDIARG_CREATERESOURCE_0003 *arg) {
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if (!arg || arg->ResourceType != D3D12DDI_RT_TEXTURE2D || arg->SampleDesc.Count != 1 || !arg->Width ||
        arg->Width > 16384 || !arg->Height || arg->Height > 16384 || !arg->DepthOrArraySize || !arg->MipLevels ||
        arg->MipLevels > 15)
        return 0;
    const UINT block = AgxD3d12FormatBlockBytes(arg->Format);
    if (!block) return 0;
    UINT64 bytes = 0;
    for (UINT level = 0; level < arg->MipLevels; ++level) {
        const UINT64 w = ((arg->Width >> level ? arg->Width >> level : 1) + 15) & ~15ULL;
        const UINT64 h = ((arg->Height >> level ? arg->Height >> level : 1) + 15) & ~15ULL;
        bytes += w * h * block;
    }
    bytes *= arg->DepthOrArraySize;
    return (bytes + 0xffffULL) & ~0xffffULL;
#else
    (void)arg;
    return 0;
#endif
}
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
    if (arg && arg->ResourceType == D3D12DDI_RT_BUFFER && arg->Width && arg->Width <= kVaLimit - kVaMinimum) {
        info->ResourceDataSize = (arg->Width + kHeapPage - 1) & ~(kHeapPage - 1);
    } else if (const UINT64 texture = TextureBytes(arg)) {
        info->ResourceDataSize = texture; /* C6a: committed 2D textures, owner layout */
        info->Layout = arg->Layout;
    } else {
        info->ResourceDataSize = ~0ULL; /* 1D/3D, MSAA and unknown formats: not implemented */
    }
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
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static AGX_D3D12_BUFFER HeapBuffer(const AgxD3d12Heap *heap) {
    AGX_D3D12_BUFFER buffer = {};
    buffer.Resource = heap->native;
    buffer.GpuVa = heap->gpuva;
    buffer.Cpu = heap->cpu;
    buffer.Bytes = heap->bytes;
    return buffer;
}
static void OwnerError(void *context, HRESULT error) {
    auto *device = static_cast<AgxD3d12Device *>(context);
    if (device && device->callbacks && device->callbacks->pfnSetErrorCb)
        device->callbacks->pfnSetErrorCb(device->runtime, error);
}
static AGX_D3D12_OWNER *EnsureOwner(AgxD3d12Device *device) {
    if (device->owner || device->owner_failed) return static_cast<AGX_D3D12_OWNER *>(device->owner);
    AGX_D3D12_OWNER_ARGS args = {};
    args.AdapterCallbacks = adapter_object.callbacks;
    args.RuntimeAdapter = adapter_object.runtime.handle;
    args.KernelCallbacks = device->kernel;
    args.RuntimeDevice = device->runtime.handle;
    args.Error = OwnerError;
    args.ErrorContext = device;
    args.Generation = device->generation;
    AGX_D3D12_OWNER *owner = nullptr;
    const HRESULT hr = AgxD3d12OwnerCreate(&args, &owner);
    device->owner = SUCCEEDED(hr) ? owner : nullptr;
    device->owner_failed = FAILED(hr);
    return static_cast<AGX_D3D12_OWNER *>(device->owner);
}
#endif
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static void RegisterHeap(AgxD3d12Device *device, AgxD3d12Heap *heap) {
    if (device->heap_count < ARRAYSIZE(device->heaps)) device->heaps[device->heap_count++] = heap;
}
static void UnregisterHeap(AgxD3d12Device *device, AgxD3d12Heap *heap) {
    for (UINT i = 0; i < device->heap_count; ++i)
        if (device->heaps[i] == heap) {
            device->heaps[i] = device->heaps[--device->heap_count];
            return;
        }
}
static AgxD3d12Heap *HeapAt(AgxD3d12Device *device, UINT64 va, UINT64 *offset) {
    for (UINT i = 0; va && i < device->heap_count; ++i) {
        AgxD3d12Heap *heap = device->heaps[i];
        if (va >= heap->gpuva && va - heap->gpuva < heap->bytes) {
            *offset = va - heap->gpuva;
            return heap;
        }
    }
    return nullptr;
}
#endif
static void ReleaseHeap(AgxD3d12Device *device, AgxD3d12Heap *heap) {
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if (heap->native) {
        UnregisterHeap(device, heap);
        AGX_D3D12_BUFFER buffer = HeapBuffer(heap);
        AgxD3d12OwnerDestroyBuffer(static_cast<AGX_D3D12_OWNER *>(device->owner), &buffer);
        memset(heap, 0, sizeof(*heap));
        return;
    }
#endif
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
    heap->bytes = arg.ByteSize > kVaLimit ? ~0ULL : (arg.ByteSize + kHeapPage - 1) & ~(kHeapPage - 1);
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    /* C4b: the heap is a buffer of the device's native owner, so copies and
     * their submission residency see it (winsys BOs only, D3D12-C4-copy.md). */
    /* Without an owner (its creation failed) the heap falls back to the C2a
     * kernel allocation below; copies refuse such heaps. */
    if (AGX_D3D12_OWNER *owner = arg.ByteSize && heap->bytes <= MAXUINT32 ? EnsureOwner(device) : nullptr) {
        AGX_D3D12_BUFFER buffer = {};
        HRESULT native = AgxD3d12OwnerCreateBuffer(owner, heap->bytes, &buffer);
        if (SUCCEEDED(native)) {
            heap->native = buffer.Resource;
            heap->gpuva = buffer.GpuVa;
            heap->cpu = buffer.Cpu;
            heap->cpu_visible = TRUE; /* Asahi buffers are CPU mapped (UMA) */
            heap->allocation = AgxD3d12OwnerBufferAllocation(owner, &buffer);
            RegisterHeap(device, heap);
        }
        const UINT receipt[] = {2u, static_cast<UINT>(heap->bytes >> 16), heap->allocation != 0 ? 1u : 0u,
                                static_cast<UINT>(heap->gpuva >> 16), static_cast<UINT>(heap->gpuva >> 32)};
        AdmissionUmdDiagnostic("measure-d3d12-heap", native, receipt, 5);
        return native;
    }
#endif
    /* EXP1355: every early exit names its reason (receipt-only) */
    const UINT refusal = !adapter_object.info_valid ? 1u
                       : !k || !k->pfnAllocateCb || !k->pfnReserveGpuVirtualAddressCb ||
                                 !k->pfnMapGpuVirtualAddressCb || !k->pfnMakeResidentCb ? 2u
                       : !arg.ByteSize || heap->bytes > MAXUINT32 ||
                                 heap->bytes > (heap->cpu_visible ? kCpuVisibleMaximum : kVaLimit - kVaMinimum) ? 3u
                                                                                                             : 0u;
    HRESULT hr = refusal ? E_OUTOFMEMORY : EnsurePagingQueue(device);
    if (FAILED(hr)) {
        const UINT receipt[] = {refusal ? refusal : 4u, heap->cpu_visible ? 1u : 0u,
                                static_cast<UINT>(heap->bytes >> 16)};
        AdmissionUmdDiagnostic("measure-d3d12-heap-refused", hr, receipt, 3);
        return hr;
    }
    ADMISSION_WIN32_ALLOCATION_CREATE create = {};
    if (!AdmissionAllocationDescribe(static_cast<UINT>(heap->bytes), 1u, 1u,
                                     ADMISSION_WIN32_ALLOCATION_STAGING_CPUVISIBLE,
                                     ADMISSION_WIN32_ALLOCATION_FORMAT_A8, 1u, &create.Allocation)) {
        const UINT receipt[] = {5u, heap->cpu_visible ? 1u : 0u, static_cast<UINT>(heap->bytes >> 16)};
        AdmissionUmdDiagnostic("measure-d3d12-heap-refused", E_INVALIDARG, receipt, 3);
        return E_INVALIDARG;
    }
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
    /* C8c: a placed buffer -- no heap description; ReuseBufferGPUVA names
     * the resource of an existing heap and the byte offset in it (the
     * encoding this assumes; receipt measure-d3d12-placed records it). */
    if (!heap_arg && resource && resource_arg && resource_arg->ResourceType == D3D12DDI_RT_BUFFER) {
        const auto *base = static_cast<const AgxD3d12Resource *>(resource_arg->ReuseBufferGPUVA.BaseAddress.UMD.hResource.pDrvPrivate);
        const UINT64 offset = resource_arg->ReuseBufferGPUVA.BaseAddress.UMD.Offset;
        if (heap) memset(heap, 0, sizeof(*heap));
        memset(resource, 0, sizeof(*resource));
        const UINT reason = !base || !base->heap ? 1u
                          : offset > base->heap->bytes || resource_arg->Width > base->heap->bytes - offset ? 6u : 0u;
        if (!reason && heap) {
            /* the placed resource's heap object aliases the parent and
             * destroying it releases nothing. EXP1370: the runtime maps a
             * placed resource through this heap object and does not add
             * the placement offset (CPU and GPU disagreed at offset 64 KiB,
             * 256/65536 bytes equal), so it starts at the resource. */
            *heap = *base->heap;
            heap->alias = TRUE;
            heap->locks = 0;
            if (heap->cpu) heap->cpu = static_cast<BYTE *>(heap->cpu) + offset;
            heap->gpuva += offset;
            heap->bytes -= offset;
        }
        if (!reason) {
            resource->heap = base->heap;
            resource->gpuva = base->heap->gpuva + offset;
            resource->bytes = resource_arg->Width;
            resource->type = D3D12DDI_RT_BUFFER;
        }
        const UINT receipt[] = {reason, base ? 1u : 0u, static_cast<UINT>(offset), static_cast<UINT>(resource_arg->Width)};
        AdmissionUmdDiagnostic("measure-d3d12-placed", reason ? E_NOTIMPL : S_OK, receipt, 4);
        return reason ? E_NOTIMPL : S_OK;
    }
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    /* C6a: a committed 2D texture is one owner texture */
    if (heap_arg && heap && resource && resource_arg && resource_arg->ResourceType == D3D12DDI_RT_TEXTURE2D) {
        const UINT64 bytes = TextureBytes(resource_arg);
        AGX_D3D12_OWNER *owner = bytes ? EnsureOwner(device) : nullptr;
        memset(heap, 0, sizeof(*heap));
        memset(resource, 0, sizeof(*resource));
        AGX_D3D12_BUFFER texture = {};
        const UINT bind = ((resource_arg->Flags & D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET) ? AGX_D3D12_BIND_RENDER_TARGET : 0u) |
                          ((resource_arg->Flags & D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE) ? AGX_D3D12_BIND_SAMPLER : 0u) |
                          ((resource_arg->Flags & D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL) ? AGX_D3D12_BIND_DEPTH_STENCIL : 0u);
        HRESULT hr = !owner ? E_NOTIMPL
                   : AgxD3d12OwnerCreateTexture2D(owner, resource_arg->Format, static_cast<UINT>(resource_arg->Width),
                                                  resource_arg->Height, resource_arg->DepthOrArraySize,
                                                  resource_arg->MipLevels, bind, bytes, &texture);
        if (SUCCEEDED(hr)) {
            heap->native = texture.Resource;
            heap->gpuva = texture.GpuVa;
            heap->bytes = bytes;
            heap->allocation = AgxD3d12OwnerBufferAllocation(owner, &texture);
            resource->heap = heap;
            resource->bytes = bytes;
            resource->type = D3D12DDI_RT_TEXTURE2D;
            resource->format = resource_arg->Format;
            resource->width = static_cast<UINT>(resource_arg->Width);
            resource->height = resource_arg->Height;
            resource->layers = resource_arg->DepthOrArraySize;
            resource->levels = resource_arg->MipLevels;
        }
        const UINT receipt[] = {static_cast<UINT>(resource_arg->Format), static_cast<UINT>(resource_arg->Width),
                                resource_arg->Height, static_cast<UINT>(bytes >> 16), bind};
        AdmissionUmdDiagnostic("measure-d3d12-committed-texture", hr, receipt, 5);
        return hr;
    }
#endif
    /* committed buffers (placed resources and other textures: not yet) */
    if (!heap_arg || !heap || (resource_arg && (!resource || resource_arg->ResourceType != D3D12DDI_RT_BUFFER ||
                                                !resource_arg->Width || resource_arg->Width > kVaLimit - kVaMinimum)))
        return E_NOTIMPL;
    /* EXP1356: for a committed resource the runtime passed a heap ByteSize
     * >= 2^48 (receipt ByteSize>>16 = 0xffffffff); the 64 KiB rounding
     * wrapped it to 0. A committed heap holds exactly its resource, so it is
     * sized by the resource's allocation info whenever ByteSize is not a
     * size this device can map. */
    D3D12DDIARG_CREATEHEAP_0001 sized = *heap_arg;
    if (resource_arg && (!sized.ByteSize || sized.ByteSize > kVaLimit - kVaMinimum))
        sized.ByteSize = (resource_arg->Width + kHeapPage - 1) & ~(kHeapPage - 1);
    const UINT size_receipt[] = {static_cast<UINT>(heap_arg->ByteSize), static_cast<UINT>(heap_arg->ByteSize >> 32),
                                 static_cast<UINT>(sized.ByteSize >> 16)};
    AdmissionUmdDiagnostic("measure-d3d12-committed-size", S_OK, size_receipt, 3);
    HRESULT hr = CreateHeap(device, sized, heap);
    if (SUCCEEDED(hr) && resource_arg) {
        resource->heap = heap;
        resource->gpuva = heap->gpuva;
        resource->bytes = resource_arg->Width;
        resource->type = D3D12DDI_RT_BUFFER;
    } else if (SUCCEEDED(hr) && resource) {
        /* C8c: a heap alone -- its resource handle spans the heap, so placed
         * resources can name it */
        memset(resource, 0, sizeof(*resource));
        resource->heap = heap;
        resource->gpuva = heap->gpuva;
        resource->bytes = heap->bytes;
        resource->type = D3D12DDI_RT_BUFFER;
    }
    return hr;
}
static VOID APIENTRY DestroyHeapAndResource(D3D12DDI_HDEVICE handle, D3D12DDI_HHEAP heap_handle,
                                            D3D12DDI_HRESOURCE resource_handle) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *heap = static_cast<AgxD3d12Heap *>(heap_handle.pDrvPrivate);
    if (resource_handle.pDrvPrivate) memset(resource_handle.pDrvPrivate, 0, sizeof(AgxD3d12Resource));
    if (device && heap && heap->allocation && !heap->alias) ReleaseHeap(device, heap);
}
static HRESULT APIENTRY MapHeap(D3D12DDI_HDEVICE handle, D3D12DDI_HHEAP heap_handle, VOID **data) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *heap = static_cast<AgxD3d12Heap *>(heap_handle.pDrvPrivate);
    if (device && heap && data && heap->native && heap->cpu) {
        *data = heap->cpu; /* C4b: persistent mapping of the owner's buffer */
        return S_OK;
    }
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
    if (!device || !heap || heap->native || !heap->locks || --heap->locks) return;
    D3DDDICB_UNLOCK unlock = {};
    unlock.NumAllocations = 1;
    unlock.phAllocations = &heap->allocation;
    (void)device->kernel->pfnUnlockCb(device->runtime.handle, &unlock);
    heap->cpu = nullptr;
}
/* EXP1357: after the first committed buffer the runtime asks the existing
 * resource's allocation info and kernel allocation handle; refused, the
 * device was removed before Map. A committed buffer reports the layout its
 * creation used (row-major, 64 KiB granule) and its heap's allocation. */
static VOID APIENTRY CheckExistingResourceAllocationInfo(D3D12DDI_HDEVICE, D3D12DDI_HRESOURCE handle,
                                                         D3D12DDI_RESOURCE_ALLOCATION_INFO *info,
                                                         D3DKMT_HANDLE *allocation) {
    auto *resource = static_cast<AgxD3d12Resource *>(handle.pDrvPrivate);
    if (info) {
        memset(info, 0, sizeof(*info));
        info->ResourceDataAlignment = kHeapPage;
        info->Layout = D3D12DDI_TL_ROW_MAJOR;
        info->ResourceDataSize = resource && resource->heap ? resource->heap->bytes : ~0ULL;
    }
    if (allocation) *allocation = resource && resource->heap ? resource->heap->allocation : 0;
    const UINT receipt[] = {resource && resource->heap ? 1u : 0u,
                            resource && resource->heap ? static_cast<UINT>(resource->heap->bytes >> 16) : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-existing-allocation-info", S_OK, receipt, 2);
}
/* EXP1358: Map/Unmap of a committed buffer asks its subresource layout;
 * refused, the second Map failed. A buffer is one subresource at offset 0
 * spanning its width. */
static VOID APIENTRY CheckSubresourceInfo(D3D12DDI_HDEVICE, D3D12DDI_HRESOURCE handle, UINT subresource,
                                          D3D12DDI_SUBRESOURCE_INFO *info) {
    auto *resource = static_cast<AgxD3d12Resource *>(handle.pDrvPrivate);
    if (!info) return;
    memset(info, 0, sizeof(*info));
    if (resource && subresource == 0) info->RowStride = info->DepthStride = resource->bytes;
    const UINT receipt[] = {subresource, resource ? static_cast<UINT>(resource->bytes) : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-subresource-info", S_OK, receipt, 2);
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
static UINT DescriptorBytes(D3D12DDI_DESCRIPTOR_HEAP_TYPE) {
    return 64u; /* C6e: a sampler record (kind + D3D12DDI_SAMPLER_DESC) needs 56 bytes */
}
static UINT APIENTRY GetDescriptorSizeInBytes(D3D12DDI_HDEVICE, D3D12DDI_DESCRIPTOR_HEAP_TYPE type) {
    return DescriptorBytes(type);
}
/* C5c: what a CBV/SRV/UAV descriptor holds. Lists replay on the native
 * owner (Gallium bindings), so a descriptor is a CPU record of the view --
 * the buffer heap and byte range -- never read by the GPU. */
enum : UINT { kDescriptorNone = 0, kDescriptorCbv = 1, kDescriptorUavBuffer = 2, kDescriptorSrvBuffer = 3,
              kDescriptorRtv = 4, kDescriptorSrvTexture = 5, kDescriptorSampler = 6, kDescriptorDsv = 7 };
struct AgxD3d12Descriptor {
    UINT kind;
    UINT reserved;
    AgxD3d12Heap *heap;
    UINT64 offset;
    UINT64 bytes;
    /* C6a: texture views (RTV): view format, mip, layer range */
    DXGI_FORMAT format;
    UINT level;
    UINT first_layer;
    UINT layers;
    UINT levels; /* C6e: texture SRVs */
};
/* C6e: a sampler descriptor */
struct AgxD3d12SamplerDescriptor {
    UINT kind; /* kDescriptorSampler */
    D3D12DDI_SAMPLER_DESC desc;
};
static_assert(sizeof(AgxD3d12SamplerDescriptor) <= 64, "fits a sampler descriptor");
static_assert(sizeof(AgxD3d12Descriptor) <= 64, "fits a CBV/SRV/UAV descriptor");
static void ViewReceipt(PCSTR stage, UINT reason, UINT a, UINT b) {
    const UINT receipt[] = {reason, a, b};
    AdmissionUmdDiagnostic(stage, reason ? E_NOTIMPL : S_OK, receipt, 3);
}
static VOID APIENTRY CreateConstantBufferView(D3D12DDI_HDEVICE handle, const D3D12DDI_CONSTANT_BUFFER_VIEW_DESC *desc,
                                              D3D12DDI_CPU_DESCRIPTOR_HANDLE dest) {
    auto *out = reinterpret_cast<AgxD3d12Descriptor *>(dest.ptr);
    if (!out) return;
    memset(out, 0, sizeof(*out));
    UINT reason = desc ? 0u : 1u;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    AgxD3d12Device *device = DeviceOf(handle);
    UINT64 offset = 0;
    AgxD3d12Heap *heap = !reason && device && desc->BufferLocation ? HeapAt(device, desc->BufferLocation, &offset) : nullptr;
    if (!reason && !heap) reason = 2u; /* a null CBV (no location) reads zero */
    if (heap) {
        out->kind = kDescriptorCbv;
        out->heap = heap;
        out->offset = offset;
        out->bytes = desc->SizeInBytes;
    }
#else
    (void)handle;
    if (!reason) reason = 3u;
#endif
    ViewReceipt("measure-d3d12-cbv", reason, desc ? desc->SizeInBytes : 0u, 0u);
}
/* Raw and structured buffer UAVs (the dxbc_to_nir shader-buffer binding);
 * typed buffers, textures and append/consume counters write a null
 * descriptor with a receipt naming the reason. */
static VOID APIENTRY CreateUnorderedAccessView(D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002 *arg,
                                               D3D12DDI_CPU_DESCRIPTOR_HANDLE dest) {
    auto *out = reinterpret_cast<AgxD3d12Descriptor *>(dest.ptr);
    if (!out) return;
    memset(out, 0, sizeof(*out));
    const auto *resource = arg ? static_cast<const AgxD3d12Resource *>(arg->hDrvResource.pDrvPrivate) : nullptr;
    const bool raw = arg && (arg->Buffer.Flags & D3D12DDI_BUFFER_UAV_FLAG_RAW);
    const UINT stride = !arg ? 0u : raw ? 4u : arg->Buffer.StructureByteStride;
    UINT reason = !arg || !resource || !resource->heap ? 1u
                : arg->ResourceDimension != D3D12DDI_RD_BUFFER ? 4u
                : !stride ? 5u /* typed buffer UAV */
                : arg->Buffer.hDrvCounterResource.pDrvPrivate ? 7u
                : 0u;
    const UINT64 offset = reason ? 0 : arg->Buffer.FirstElement * stride;
    const UINT64 bytes = reason ? 0 : static_cast<UINT64>(arg->Buffer.NumElements) * stride;
    if (!reason && (offset > resource->bytes || bytes > resource->bytes - offset)) reason = 6u;
#ifndef APPLE_AGX_D3D12_NATIVE_OWNER
    if (!reason) reason = 3u;
#endif
    (void)handle;
    if (!reason) {
        out->kind = kDescriptorUavBuffer;
        out->heap = resource->heap;
        out->offset = resource->gpuva - resource->heap->gpuva + offset;
        out->bytes = bytes;
    }
    ViewReceipt("measure-d3d12-uav", reason, stride, arg ? arg->Buffer.NumElements : 0u);
}
/* C6a: 2D (array) RTVs of committed textures; RTV descriptors are read
 * when a list records them. */
static VOID APIENTRY CreateRenderTargetView(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_RENDER_TARGET_VIEW_0002 *arg,
                                            D3D12DDI_CPU_DESCRIPTOR_HANDLE dest) {
    auto *out = reinterpret_cast<AgxD3d12Descriptor *>(dest.ptr);
    if (!out) return;
    memset(out, 0, sizeof(*out));
    const auto *resource = arg ? static_cast<const AgxD3d12Resource *>(arg->hDrvResource.pDrvPrivate) : nullptr;
    UINT reason = !arg || !resource || !resource->heap ? 1u
                : resource->type != D3D12DDI_RT_TEXTURE2D || arg->ResourceDimension != D3D12DDI_RD_TEXTURE2D ? 4u
                : arg->Tex2D.PlaneSlice || arg->Tex2D.MipSlice >= resource->levels ||
                          arg->Tex2D.FirstArraySlice >= resource->layers ? 6u
                : 0u;
    if (!reason) {
        const UINT layers = arg->Tex2D.ArraySize == UINT_MAX || arg->Tex2D.ArraySize > resource->layers - arg->Tex2D.FirstArraySlice
                                ? resource->layers - arg->Tex2D.FirstArraySlice : (arg->Tex2D.ArraySize ? arg->Tex2D.ArraySize : 1u);
        out->kind = kDescriptorRtv;
        out->heap = resource->heap;
        out->format = arg->Format == DXGI_FORMAT_UNKNOWN ? resource->format : arg->Format;
        out->level = arg->Tex2D.MipSlice;
        out->first_layer = arg->Tex2D.FirstArraySlice;
        out->layers = layers;
    }
    ViewReceipt("measure-d3d12-rtv", reason, arg ? static_cast<UINT>(arg->Format) : 0u,
                arg ? static_cast<UINT>(arg->ResourceDimension) : 0u);
}
/* C6f: 2D (array) DSVs of committed depth textures (read at record) */
static VOID APIENTRY CreateDepthStencilView(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_DEPTH_STENCIL_VIEW *arg,
                                            D3D12DDI_CPU_DESCRIPTOR_HANDLE dest) {
    auto *out = reinterpret_cast<AgxD3d12Descriptor *>(dest.ptr);
    if (!out) return;
    memset(out, 0, sizeof(*out));
    const auto *resource = arg ? static_cast<const AgxD3d12Resource *>(arg->hDrvResource.pDrvPrivate) : nullptr;
    UINT reason = !arg || !resource || !resource->heap ? 1u
                : resource->type != D3D12DDI_RT_TEXTURE2D || arg->ResourceDimension != D3D12DDI_RD_TEXTURE2D ? 4u
                : arg->Tex2D.MipSlice >= resource->levels || arg->Tex2D.FirstArraySlice >= resource->layers ? 6u : 0u;
    if (!reason) {
        out->kind = kDescriptorDsv;
        out->heap = resource->heap;
        out->format = arg->Format == DXGI_FORMAT_UNKNOWN ? resource->format : arg->Format;
        out->level = arg->Tex2D.MipSlice;
        out->first_layer = arg->Tex2D.FirstArraySlice;
        out->layers = arg->Tex2D.ArraySize == UINT_MAX || arg->Tex2D.ArraySize > resource->layers - arg->Tex2D.FirstArraySlice
                          ? resource->layers - arg->Tex2D.FirstArraySlice : (arg->Tex2D.ArraySize ? arg->Tex2D.ArraySize : 1u);
        out->reserved = static_cast<UINT>(arg->Flags); /* read-only depth/stencil */
    }
    ViewReceipt("measure-d3d12-dsv", reason, arg ? static_cast<UINT>(arg->Format) : 0u,
                arg ? static_cast<UINT>(arg->ResourceDimension) : 0u);
}
/* C5d: raw and structured buffer SRVs (read as 32-bit words); typed
 * buffers and textures write a null descriptor with a receipt. */
static VOID APIENTRY CreateShaderResourceView(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002 *arg,
                                              D3D12DDI_CPU_DESCRIPTOR_HANDLE dest) {
    auto *out = reinterpret_cast<AgxD3d12Descriptor *>(dest.ptr);
    if (!out) return;
    memset(out, 0, sizeof(*out));
    const auto *resource = arg ? static_cast<const AgxD3d12Resource *>(arg->hDrvResource.pDrvPrivate) : nullptr;
    /* C6e: 2D (array) texture SRVs of committed textures */
    if (arg && resource && resource->heap && resource->type == D3D12DDI_RT_TEXTURE2D) {
        const D3D12DDIARG_TEX2D_SHADER_RESOURCE_VIEW_0002 &v = arg->Tex2D;
        UINT reason = arg->ResourceDimension != D3D12DDI_RD_TEXTURE2D || v.PlaneSlice ? 4u
                    : v.MostDetailedMip >= resource->levels || v.FirstArraySlice >= resource->layers ? 6u : 0u;
        if (!reason) {
            out->kind = kDescriptorSrvTexture;
            out->heap = resource->heap;
            out->format = arg->Format == DXGI_FORMAT_UNKNOWN ? resource->format : arg->Format;
            out->level = v.MostDetailedMip;
            out->levels = v.MipLevels == UINT_MAX || v.MipLevels > resource->levels - v.MostDetailedMip
                              ? resource->levels - v.MostDetailedMip : (v.MipLevels ? v.MipLevels : 1u);
            out->first_layer = v.FirstArraySlice;
            out->layers = v.ArraySize == UINT_MAX || v.ArraySize > resource->layers - v.FirstArraySlice
                              ? resource->layers - v.FirstArraySlice : (v.ArraySize ? v.ArraySize : 1u);
        }
        ViewReceipt("measure-d3d12-srv", reason, static_cast<UINT>(arg->Format), 0x80000000u | out->levels);
        return;
    }
    const bool raw = arg && (arg->Buffer.Flags & D3D12DDI_BUFFER_SRV_FLAG_RAW);
    const UINT stride = !arg ? 0u : raw ? 4u : arg->Buffer.StructureByteStride;
    UINT reason = !arg || !resource || !resource->heap ? 1u
                : arg->ResourceDimension != D3D12DDI_RD_BUFFER ? 4u
                : !stride ? 5u /* typed buffer SRV */
                : 0u;
    const UINT64 offset = reason ? 0 : arg->Buffer.FirstElement * stride;
    const UINT64 bytes = reason ? 0 : static_cast<UINT64>(arg->Buffer.NumElements) * stride;
    if (!reason && (offset > resource->bytes || bytes > resource->bytes - offset)) reason = 6u;
#ifndef APPLE_AGX_D3D12_NATIVE_OWNER
    if (!reason) reason = 3u;
#endif
    if (!reason) {
        out->kind = kDescriptorSrvBuffer;
        out->heap = resource->heap;
        out->offset = resource->gpuva - resource->heap->gpuva + offset;
        out->bytes = bytes;
    }
    ViewReceipt("measure-d3d12-srv", reason, stride, arg ? arg->Buffer.NumElements : 0u);
}
/* C6e: samplers are CPU records of their description */
static VOID APIENTRY CreateSampler(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_SAMPLER *arg, D3D12DDI_CPU_DESCRIPTOR_HANDLE dest) {
    auto *out = reinterpret_cast<AgxD3d12SamplerDescriptor *>(dest.ptr);
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!arg || !arg->pSamplerDesc) return;
    out->kind = kDescriptorSampler;
    out->desc = *arg->pSamplerDesc;
}
/* Descriptors are CPU records, so copies are byte copies of whole
 * descriptors of the heap type's size. */
static VOID APIENTRY CopyDescriptorsSimple(D3D12DDI_HDEVICE, UINT count, D3D12DDI_CPU_DESCRIPTOR_HANDLE dest,
                                           D3D12DDI_CPU_DESCRIPTOR_HANDLE src, D3D12DDI_DESCRIPTOR_HEAP_TYPE type) {
    if (count && dest.ptr && src.ptr)
        memmove(reinterpret_cast<void *>(dest.ptr), reinterpret_cast<const void *>(src.ptr),
                static_cast<SIZE_T>(count) * DescriptorBytes(type));
}
static VOID APIENTRY CopyDescriptors(D3D12DDI_HDEVICE, UINT dest_ranges, const D3D12DDI_CPU_DESCRIPTOR_HANDLE *dest_starts,
                                     const UINT *dest_sizes, UINT src_ranges,
                                     const D3D12DDI_CPU_DESCRIPTOR_HANDLE *src_starts, const UINT *src_sizes,
                                     D3D12DDI_DESCRIPTOR_HEAP_TYPE type) {
    const SIZE_T size = DescriptorBytes(type);
    UINT di = 0, si = 0, dpos = 0, spos = 0;
    while (dest_starts && src_starts && di < dest_ranges && si < src_ranges) {
        const UINT dcount = dest_sizes ? dest_sizes[di] : 1u, scount = src_sizes ? src_sizes[si] : 1u;
        if (dpos >= dcount) { ++di; dpos = 0; continue; }
        if (spos >= scount) { ++si; spos = 0; continue; }
        memmove(reinterpret_cast<BYTE *>(dest_starts[di].ptr) + dpos * size,
                reinterpret_cast<const BYTE *>(src_starts[si].ptr) + spos * size, size);
        ++dpos;
        ++spos;
    }
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
/* C3a (EXP1360, D3D12-C3-queue-fences.md): CreateCommandQueue first asks
 * pfnCalcPrivateCommandQueueSize. The queue and fence objects record what
 * the runtime gives them; no kernel context or submission exists yet, so
 * Signal/Wait/ExecuteCommandLists stay fail-closed and their receipts name
 * the runtime's next request. A fence keeps the GPU VA placements of its
 * monitored value (a basic monitored fence is signalled by writing there). */
/* C3b (EXP1361): CreateFence carried no GPU VA (the runtime owns the
 * monitored fence) and Signal failed in the runtime (E_INVALIDARG) without
 * a driver call: the runtime signals through the kernel contexts a queue
 * owns, and this queue owned none. Each queue therefore owns one GPU
 * virtual-address kernel context, created through the core-layer
 * pfnCreateContextVirtualCb(hRTCommandQueue) with the KMD context contract
 * the D3D11 device uses (ADMISSION_WIN32_CONTEXT_CREATE). */
struct AgxD3d12CommandQueue {
    D3D12DDI_HRTCOMMANDQUEUE runtime;
    UINT flags;
    HANDLE context;
};
static SIZE_T APIENTRY CalcPrivateCommandQueueSize(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATECOMMANDQUEUE_0001 *) {
    return sizeof(AgxD3d12CommandQueue);
}
static HRESULT APIENTRY CreateCommandQueue(D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATECOMMANDQUEUE_0001 *arg) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *queue = arg ? static_cast<AgxD3d12CommandQueue *>(arg->hDrvCommandQueue.pDrvPrivate) : nullptr;
    if (!device || !queue || !device->callbacks || !device->callbacks->pfnCreateContextVirtualCb) return E_INVALIDARG;
    memset(queue, 0, sizeof(*queue));
    queue->runtime = arg->hRTCommandQueue;
    queue->flags = static_cast<UINT>(arg->QueueFlags);
    ADMISSION_WIN32_CONTEXT_CREATE contract = {};
    contract.Magic = ADMISSION_WIN32_CONTEXT_MAGIC;
    contract.Version = ADMISSION_WIN32_CONTEXT_VERSION;
    contract.Bytes = sizeof(contract);
    contract.Generation = device->generation;
    D3DDDICB_CREATECONTEXTVIRTUAL context = {};
    context.NodeOrdinal = 0;
    context.EngineAffinity = 1;
    context.pPrivateDriverData = &contract;
    context.PrivateDriverDataSize = sizeof(contract);
    HRESULT hr = device->callbacks->pfnCreateContextVirtualCb(arg->hRTCommandQueue, &context);
    if (SUCCEEDED(hr) && !context.hContext) hr = E_FAIL;
    queue->context = SUCCEEDED(hr) ? context.hContext : nullptr;
    const UINT receipt[] = {queue->flags, arg->NodeMask, queue->context != nullptr ? 1u : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-queue", hr, receipt, 3);
    return hr;
}
static VOID APIENTRY DestroyCommandQueue(D3D12DDI_HDEVICE handle, D3D12DDI_HCOMMANDQUEUE queue_handle) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *queue = static_cast<AgxD3d12CommandQueue *>(queue_handle.pDrvPrivate);
    if (!device || !queue || !queue->context || !device->callbacks || !device->callbacks->pfnDestroyContextCb) return;
    D3DDDICB_DESTROYCONTEXT destroy = {};
    destroy.hContext = queue->context;
    (void)device->callbacks->pfnDestroyContextCb(queue->runtime, &destroy);
    queue->context = nullptr;
}
struct AgxD3d12Fence {
    UINT count;
    D3D12DDI_FENCE fence[1];
};
static SIZE_T APIENTRY CalcPrivateFenceSize(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_FENCE *arg) {
    const UINT count = arg && arg->FenceCount ? arg->FenceCount : 1;
    return offsetof(AgxD3d12Fence, fence) + count * sizeof(D3D12DDI_FENCE);
}
static HRESULT APIENTRY CreateFence(D3D12DDI_HDEVICE, D3D12DDI_HFENCE handle, const D3D12DDIARG_CREATE_FENCE *arg) {
    auto *fence = static_cast<AgxD3d12Fence *>(handle.pDrvPrivate);
    if (!fence || !arg || (arg->FenceCount && !arg->Fences)) return E_INVALIDARG;
    fence->count = arg->FenceCount;
    if (arg->FenceCount) memcpy(fence->fence, arg->Fences, arg->FenceCount * sizeof(D3D12DDI_FENCE));
    const UINT64 va = arg->FenceCount ? arg->Fences[0].FenceValue.BaseAddress : 0;
    const UINT receipt[] = {arg->FenceCount, static_cast<UINT>(va), static_cast<UINT>(va >> 32),
                            arg->FenceCount ? static_cast<UINT>(arg->Fences[0].Flags) : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-fence", S_OK, receipt, 4);
    return S_OK;
}
static VOID APIENTRY DestroyFence(D3D12DDI_HDEVICE, D3D12DDI_HFENCE) {}
/* C4a (D3D12-C plan C4): command allocators and command lists. A list
 * records nothing yet (every recording DDI is still fail-closed), so a
 * closed list is empty and ExecuteCommandLists has no GPU work to submit;
 * the runtime's fence signal after it still goes through the queue's
 * context (C3). The GPU copy (libagx copy kernels) is C4b. */
static SIZE_T APIENTRY CalcPrivateCommandAllocatorSize(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATECOMMANDALLOCATOR *) {
    return sizeof(UINT);
}
static HRESULT APIENTRY CreateCommandAllocator(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATECOMMANDALLOCATOR *arg) {
    auto *type = arg ? static_cast<UINT *>(arg->hDrvCommandAllocator.pDrvPrivate) : nullptr;
    if (!type) return E_INVALIDARG;
    *type = static_cast<UINT>(arg->Type);
    return S_OK;
}
static VOID APIENTRY DestroyCommandAllocator(D3D12DDI_HDEVICE, D3D12DDI_HCOMMANDALLOCATOR) {}
static VOID APIENTRY ResetCommandAllocator(D3D12DDI_HCOMMANDALLOCATOR) {}
/* C4b: a recorded CopyBufferRegion (heap-relative byte ranges). */
struct AgxD3d12Copy {
    AgxD3d12Heap *dst;
    UINT64 dst_offset;
    AgxD3d12Heap *src;
    UINT64 src_offset;
    UINT64 bytes;
};
/* C5: a recorded Dispatch: the pipeline and its root UAVs resolved to
 * heap-relative views at shader-buffer slot u# (register space 0, the
 * dxbc_to_nir binding of raw/structured UAVs). */
static constexpr UINT kUavSlots = 8;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static_assert(kUavSlots == AGX_D3D12_UAV_SLOTS, "UAV slots of the owner");
#endif
/* C5b: constant buffer b# (UBO #, the dxbc_to_nir/D3D10 frontend binding)
 * from a root CBV (heap + offset) or root constants (a range of the
 * dispatch's copy of the root DWORDs; the copy keeps 3 DWORDs of slack so
 * a 16-byte-rounded read stays inside it). */
static constexpr UINT kCbSlots = 14; /* D3D12_COMMONSHADER_CONSTANT_BUFFER_HW_SLOT_COUNT */
static constexpr UINT kSrvSlots = 16; /* C5d: buffer SRVs t0..t15 */
static constexpr UINT kSamplerSlots = 16; /* C6e: s0..s15 */
static constexpr UINT kRootArguments = 64; /* a root signature costs at most 64 DWORDs */
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static_assert(kCbSlots == AGX_D3D12_CBV_SLOTS, "constant buffer slots of the owner");
static_assert(kSrvSlots == AGX_D3D12_SRV_SLOTS, "SRV slots of the owner");
static_assert(kSamplerSlots == AGX_D3D12_SAMPLER_SLOTS, "sampler slots of the owner");
#endif
struct AgxD3d12Dispatch {
    AgxD3d12PipelineState *pso;
    AgxD3d12Heap *uav_heap[kUavSlots];
    UINT64 uav_offset[kUavSlots];
    UINT groups[3];
    AgxD3d12Heap *cb_heap[kCbSlots];
    UINT64 cb_offset[kCbSlots];
    UINT cb_constants[kCbSlots]; /* root constants: DWORD offset + 1 into root_dwords (0 = none) */
    UINT cb_count[kCbSlots];     /* root constants: Num32BitValues */
    UINT root_dwords[kRootArguments + 3];
    /* C5c: descriptor-table slots: the descriptor's location, read when the
     * list executes (root signature 1.0 descriptors are volatile until then) */
    const struct AgxD3d12Descriptor *uav_descriptor[kUavSlots];
    const struct AgxD3d12Descriptor *cb_descriptor[kCbSlots];
    /* C5d: buffer SRVs t# from root SRVs or tables */
    AgxD3d12Heap *srv_heap[kSrvSlots];
    UINT64 srv_offset[kSrvSlots];
    const struct AgxD3d12Descriptor *srv_descriptor[kSrvSlots];
    /* C6e: samplers s# from sampler tables or static samplers */
    const struct AgxD3d12SamplerDescriptor *sampler[kSamplerSlots];
    const D3D12DDI_STATIC_SAMPLER *static_sampler[kSamplerSlots];
};
/* C6a: a recorded ClearRenderTargetView (the RTV read at record time) */
static constexpr UINT kClearRects = 8;
struct AgxD3d12Clear {
    AgxD3d12Descriptor rtv;
    FLOAT color[4];
    UINT rects;
    RECT rect[kClearRects];
};
/* C6a: a recorded CopyTextureRegion between a texture subresource region
 * and a pitched buffer footprint */
struct AgxD3d12TextureCopy {
    AgxD3d12Heap *texture;
    UINT level, layer, x, y, width, height, depth;
    AgxD3d12Heap *buffer;
    UINT64 offset;
    UINT pitch, slice_pitch;
    BOOL to_buffer;
};
/* C6b: a recorded draw: the pipeline and a snapshot of the list's output
 * merger, rasterizer and input-assembler state */
static constexpr UINT kVertexBuffers = 16;
struct AgxD3d12VertexBuffer {
    AgxD3d12Heap *heap;
    UINT64 offset;
    UINT stride;
};
struct AgxD3d12Draw {
    AgxD3d12PipelineState *pso;
    /* C6c: the VS and PS root bindings (AgxD3d12Dispatch slot records) */
    AgxD3d12Dispatch vs_bind, ps_bind;
    UINT rtvs;
    AgxD3d12Descriptor rtv[8];
    AgxD3d12Descriptor dsv; /* C6f: kind 0 = none */
    D3D12DDI_VIEWPORT viewport;
    RECT scissor;
    UINT topology;
    AgxD3d12VertexBuffer vb[kVertexBuffers];
    FLOAT blend_factor[4];
    UINT stencil_ref;
    UINT vertices, instances, start_vertex, start_instance;
    /* C6d: indexed draws (index_size 0 = not indexed) */
    AgxD3d12Heap *index_heap;
    UINT64 index_offset;
    UINT index_size;
    INT base_vertex;
};
/* C6f: a recorded ClearDepthStencilView */
struct AgxD3d12ClearDepth {
    AgxD3d12Descriptor dsv;
    UINT flags; /* D3D12_CLEAR_FLAG_DEPTH 1, _STENCIL 2 */
    FLOAT depth;
    UINT stencil;
    UINT rects;
    RECT rect[kClearRects];
};
/* C8b: a recorded texture-to-texture copy (whole: every common mip, all
 * layers, full extents) */
struct AgxD3d12TextureRegionCopy {
    AgxD3d12Heap *dst, *src;
    UINT dst_level, dst_layer, dst_x, dst_y;
    UINT src_level, src_layer, src_x, src_y;
    UINT width, height, layers;
    UINT whole_levels; /* > 0: ResourceCopy of this many levels */
};
enum : UINT { kCommandCopy = 1, kCommandDispatch = 2, kCommandClear = 3, kCommandTextureCopy = 4, kCommandDraw = 5,
              kCommandClearDepth = 6, kCommandTextureRegionCopy = 7 };
struct AgxD3d12Command {
    UINT kind;
    union {
        AgxD3d12Copy copy;
        AgxD3d12Dispatch dispatch;
        AgxD3d12Clear clear;
        AgxD3d12TextureCopy texture_copy;
        AgxD3d12Draw draw;
        AgxD3d12ClearDepth clear_depth;
        AgxD3d12TextureRegionCopy region_copy;
    };
};
/* C8a: commands live in a growing process-heap array owned by the list
 * (capacity kept across Reset, freed with the list) */
static constexpr UINT kListCommands = 1u << 20; /* recording limit per list */
struct AgxD3d12CommandList {
    D3D12DDI_HRTCOMMANDLIST runtime;
    UINT type;
    UINT closed;
    UINT commands; /* recorded commands, replayed in order */
    UINT64 state;  /* EXP1363: state setters the list received (bit = DDI slot) */
    AgxD3d12Device *device;
    /* C5: compute state */
    AgxD3d12PipelineState *pso;
    const AgxD3d12RootSignature *compute_root;
    D3D12DDI_GPU_VIRTUAL_ADDRESS compute_root_va[kRootArguments];
    UINT compute_root_dwords[kRootArguments]; /* C5b: root constants at their parameter's DWORD offset */
    /* C6b: graphics state */
    const AgxD3d12RootSignature *graphics_root;
    D3D12DDI_GPU_VIRTUAL_ADDRESS graphics_root_va[kRootArguments];
    UINT graphics_root_dwords[kRootArguments];
    UINT rtvs;
    AgxD3d12Descriptor rtv[8];
    AgxD3d12Descriptor dsv; /* C6f */
    D3D12DDI_VIEWPORT viewport;
    RECT scissor;
    UINT topology;
    AgxD3d12VertexBuffer vb[kVertexBuffers];
    FLOAT blend_factor[4];
    UINT stencil_ref;
    /* C6d: the index buffer view */
    AgxD3d12Heap *index_heap;
    UINT64 index_offset;
    UINT index_size;
    AgxD3d12Command *command;
    UINT capacity;
};
/* EXP1363: creating/resetting a list makes the runtime set its default
 * state (topology, viewports, scissors, blend factor, stencil ref, PSO,
 * descriptor heaps, predication, SO targets, render targets, IA buffers,
 * root arguments); refused, the device was removed. Every consumer of that
 * state (draws, dispatches, clears, copies) is still fail-closed, so a list
 * records only which state it received; C5/C6 replace each setter with the
 * stored values its consumer needs. */
template<class T, UINT Slot> struct ListState;
template<class R, class... A, UINT Slot> struct ListState<R (APIENTRY *)(D3D12DDI_HCOMMANDLIST, A...), Slot> {
    static R APIENTRY call(D3D12DDI_HCOMMANDLIST handle, A...) {
        auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
        if (list) list->state |= 1ULL << (Slot & 63u);
        if constexpr (!std::is_void_v<R>) return R{};
    }
};
#define AGX_LIST_STATE(M) \
    ListState<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->M), \
              static_cast<UINT>(offsetof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, M) / sizeof(void *))>::call
static SIZE_T APIENTRY CalcPrivateCommandListSize(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_COMMAND_LIST_0001 *) {
    return sizeof(AgxD3d12CommandList);
}
static HRESULT APIENTRY CreateCommandList(D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATE_COMMAND_LIST_0001 *arg) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *list = arg ? static_cast<AgxD3d12CommandList *>(arg->hDrvCommandList.pDrvPrivate) : nullptr;
    if (!device || !list) return E_INVALIDARG;
    memset(list, 0, sizeof(*list));
    list->device = device;
    list->runtime = arg->hRTCommandList;
    list->type = static_cast<UINT>(arg->Type);
    if (device->callbacks && device->callbacks->pfnSetCommandListDDITableCb && list_table.handle)
        device->callbacks->pfnSetCommandListDDITableCb(arg->hRTCommandList, list_table);
    const UINT receipt[] = {list->type, list_table.handle != nullptr ? 1u : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-command-list", S_OK, receipt, 2);
    return S_OK;
}
static VOID APIENTRY DestroyCommandList(D3D12DDI_HDEVICE, D3D12DDI_HCOMMANDLIST handle) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list || !list->command) return;
    HeapFree(GetProcessHeap(), 0, list->command);
    list->command = nullptr;
    list->capacity = list->commands = 0;
}
static VOID APIENTRY CloseCommandList(D3D12DDI_HCOMMANDLIST handle) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (list) list->closed = 1;
}
static VOID APIENTRY ResetCommandList(D3D12DDI_HCOMMANDLIST handle, const D3D12DDIARG_RESETCOMMANDLIST *) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    list->closed = list->commands = 0;
    list->state = 0;
    list->pso = nullptr;
    list->compute_root = nullptr;
    memset(list->compute_root_va, 0, sizeof(list->compute_root_va));
    memset(list->compute_root_dwords, 0, sizeof(list->compute_root_dwords));
    list->graphics_root = nullptr;
    memset(list->graphics_root_va, 0, sizeof(list->graphics_root_va));
    memset(list->graphics_root_dwords, 0, sizeof(list->graphics_root_dwords));
    list->rtvs = 0;
    memset(&list->dsv, 0, sizeof(list->dsv));
    memset(&list->viewport, 0, sizeof(list->viewport));
    memset(&list->scissor, 0, sizeof(list->scissor));
    list->topology = 0;
    memset(list->vb, 0, sizeof(list->vb));
    memset(list->blend_factor, 0, sizeof(list->blend_factor));
    list->stencil_ref = 0;
    list->index_heap = nullptr;
    list->index_offset = 0;
    list->index_size = 0;
}
/* C4b: a buffer copy between committed buffers (heap-backed resources). The
 * list records heap-relative ranges; ExecuteCommandLists replays them on the
 * device's native owner (libagx copy kernels) and waits for completion, so
 * the runtime's later queue signal follows finished work. */
static void ListError(AgxD3d12CommandList *list, HRESULT error) {
    if (list && list->device && list->device->callbacks && list->device->callbacks->pfnSetCommandListErrorCb)
        list->device->callbacks->pfnSetCommandListErrorCb(list->runtime, error);
}
/* C8a: the next command slot, growing the array by doubling; NULL (and a
 * list error) when the heap refuses. */
static AgxD3d12Command *AppendCommand(AgxD3d12CommandList *list) {
    if (list->commands == list->capacity) {
        const UINT capacity = list->capacity ? list->capacity * 2u : 64u;
        void *grown = list->command
                          ? HeapReAlloc(GetProcessHeap(), 0, list->command, capacity * sizeof(AgxD3d12Command))
                          : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(AgxD3d12Command));
        if (!grown) {
            ListError(list, E_OUTOFMEMORY);
            return nullptr;
        }
        list->command = static_cast<AgxD3d12Command *>(grown);
        list->capacity = capacity;
    }
    return &list->command[list->commands++];
}
static VOID APIENTRY CopyBufferRegion(D3D12DDI_HCOMMANDLIST handle, D3D12DDIARG_BUFFER_PLACEMENT dst,
                                      D3D12DDIARG_BUFFER_PLACEMENT src, UINT64 bytes) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    auto *d = static_cast<AgxD3d12Resource *>(dst.BaseAddress.UMD.hResource.pDrvPrivate);
    auto *r = static_cast<AgxD3d12Resource *>(src.BaseAddress.UMD.hResource.pDrvPrivate);
    if (!list) return;
    const UINT64 dst_offset = dst.BaseAddress.UMD.Offset, src_offset = src.BaseAddress.UMD.Offset;
    if (!d || !r || !d->heap || !r->heap || !d->heap->native || !r->heap->native || !bytes ||
        dst_offset > d->bytes || bytes > d->bytes - dst_offset || src_offset > r->bytes ||
        bytes > r->bytes - src_offset || list->commands >= kListCommands) {
        ListError(list, E_INVALIDARG);
        return;
    }
    AgxD3d12Command *slot = AppendCommand(list);
    if (!slot) return;
    AgxD3d12Command &command = *slot;
    command.kind = kCommandCopy;
    command.copy = {d->heap, dst_offset, r->heap, src_offset, bytes};
}
/* C6a (investigation/analysis/D3D12-C6-graphics.md) */
static VOID APIENTRY ClearRenderTargetView(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_CPU_DESCRIPTOR_HANDLE view,
                                           const FLOAT color[4], UINT rects, const D3D12DDI_RECT *rect) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    const auto *rtv = reinterpret_cast<const AgxD3d12Descriptor *>(view.ptr);
    if (!rtv || rtv->kind != kDescriptorRtv || !rtv->heap || !color || rects > kClearRects || (rects && !rect) ||
        list->commands >= kListCommands) {
        const UINT receipt[] = {rtv ? rtv->kind : 0u, rects};
        AdmissionUmdDiagnostic("measure-d3d12-clear-refused", E_INVALIDARG, receipt, 2);
        ListError(list, E_INVALIDARG);
        return;
    }
    AgxD3d12Command *slot = AppendCommand(list);
    if (!slot) return;
    AgxD3d12Command &command = *slot;
    command.kind = kCommandClear;
    command.clear.rtv = *rtv;
    memcpy(command.clear.color, color, sizeof(command.clear.color));
    command.clear.rects = rects;
    for (UINT i = 0; i < rects; ++i) {
        command.clear.rect[i].left = rect[i].left;
        command.clear.rect[i].top = rect[i].top;
        command.clear.rect[i].right = rect[i].right;
        command.clear.rect[i].bottom = rect[i].bottom;
    }
}
/* C8b: CopyResource -- whole buffers (the smaller size) or whole 2D
 * textures (every common mip and layer) */
static VOID APIENTRY ResourceCopy(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HRESOURCE dst_handle, D3D12DDI_HRESOURCE src_handle) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    auto *d = static_cast<AgxD3d12Resource *>(dst_handle.pDrvPrivate);
    auto *s = static_cast<AgxD3d12Resource *>(src_handle.pDrvPrivate);
    const UINT refusal = !d || !s || !d->heap || !s->heap || !d->heap->native || !s->heap->native ? 1u
                       : (d->type == D3D12DDI_RT_TEXTURE2D) != (s->type == D3D12DDI_RT_TEXTURE2D) ? 4u
                       : list->commands >= kListCommands ? 3u : 0u;
    const UINT receipt[] = {refusal, d ? d->type : 0u, s ? s->type : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-resource-copy", refusal ? E_INVALIDARG : S_OK, receipt, 3);
    if (refusal) {
        ListError(list, E_INVALIDARG);
        return;
    }
    AgxD3d12Command *slot = AppendCommand(list);
    if (!slot) return;
    if (d->type == D3D12DDI_RT_TEXTURE2D) {
        AgxD3d12TextureRegionCopy rc = {};
        rc.dst = d->heap;
        rc.src = s->heap;
        rc.width = s->width < d->width ? s->width : d->width;
        rc.height = s->height < d->height ? s->height : d->height;
        rc.layers = s->layers < d->layers ? s->layers : d->layers;
        rc.whole_levels = s->levels < d->levels ? s->levels : d->levels;
        slot->kind = kCommandTextureRegionCopy;
        slot->region_copy = rc;
    } else {
        const UINT64 bytes = d->bytes < s->bytes ? d->bytes : s->bytes;
        slot->kind = kCommandCopy;
        slot->copy = {d->heap, d->gpuva - d->heap->gpuva, s->heap, s->gpuva - s->heap->gpuva, bytes};
    }
}
/* C6f */
static VOID APIENTRY ClearDepthStencilView(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_CPU_DESCRIPTOR_HANDLE view, UINT flags,
                                           FLOAT depth, UINT8 stencil, UINT rects, const D3D12DDI_RECT *rect) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    const auto *dsv = reinterpret_cast<const AgxD3d12Descriptor *>(view.ptr);
    if (!dsv || dsv->kind != kDescriptorDsv || !dsv->heap || !(flags & 3u) || rects > kClearRects || (rects && !rect) ||
        list->commands >= kListCommands) {
        const UINT receipt[] = {dsv ? dsv->kind : 0u, flags, rects};
        AdmissionUmdDiagnostic("measure-d3d12-clear-depth-refused", E_INVALIDARG, receipt, 3);
        ListError(list, E_INVALIDARG);
        return;
    }
    AgxD3d12Command *slot = AppendCommand(list);
    if (!slot) return;
    AgxD3d12Command &command = *slot;
    command.kind = kCommandClearDepth;
    command.clear_depth.dsv = *dsv;
    command.clear_depth.flags = flags & 3u;
    command.clear_depth.depth = depth;
    command.clear_depth.stencil = stencil;
    command.clear_depth.rects = rects;
    for (UINT i = 0; i < rects; ++i) {
        command.clear_depth.rect[i].left = rect[i].left;
        command.clear_depth.rect[i].top = rect[i].top;
        command.clear_depth.rect[i].right = rect[i].right;
        command.clear_depth.rect[i].bottom = rect[i].bottom;
    }
}
/* One side of CopyTextureRegion: a texture subresource
 * (D3D12DDI_RL_SELECT_SUBRESOURCE, Offset = subresource index) or a buffer
 * footprint (D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED). */
struct AgxD3d12CopySide {
    AgxD3d12Resource *resource;
    UINT64 offset;
    const D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT *footprint;
};
static bool CopySide(const D3D12DDIARG_BUFFER_PLACEMENT *placement, D3D12DDIARG_PLACED_RESOURCE layout,
                     AgxD3d12CopySide *side) {
    side->resource = placement ? static_cast<AgxD3d12Resource *>(placement->BaseAddress.UMD.hResource.pDrvPrivate) : nullptr;
    side->offset = placement ? placement->BaseAddress.UMD.Offset : 0;
    side->footprint = layout.Layout == D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED
                          ? static_cast<const D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT *>(layout.pLayout) : nullptr;
    if (!side->resource || !side->resource->heap) return false;
    if (layout.Layout == D3D12DDI_RL_SELECT_SUBRESOURCE) return side->resource->type == D3D12DDI_RT_TEXTURE2D;
    return side->footprint && side->resource->type != D3D12DDI_RT_TEXTURE2D;
}
static VOID APIENTRY CopyTextureRegion(D3D12DDI_HCOMMANDLIST handle, const D3D12DDIARG_BUFFER_PLACEMENT *dst_placement,
                                       D3D12DDIARG_PLACED_RESOURCE dst_layout, UINT dst_x, UINT dst_y, UINT dst_z,
                                       const D3D12DDIARG_BUFFER_PLACEMENT *src_placement,
                                       D3D12DDIARG_PLACED_RESOURCE src_layout, const D3D12DDI_BOX *box) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    AgxD3d12CopySide dst = {}, src = {};
    UINT refusal = !CopySide(dst_placement, dst_layout, &dst) || !CopySide(src_placement, src_layout, &src) ? 1u
                 : dst.footprint && src.footprint ? 4u /* buffer <-> buffer footprints */
                 : list->commands >= kListCommands ? 3u : 0u;
    if (!refusal && !dst.footprint && !src.footprint) {
        /* C8b: texture subresource to texture subresource */
        AgxD3d12TextureRegionCopy rc = {};
        const UINT dsub = static_cast<UINT>(dst.offset), ssub = static_cast<UINT>(src.offset);
        rc.dst = dst.resource->heap;
        rc.src = src.resource->heap;
        rc.dst_level = dsub % dst.resource->levels;
        rc.dst_layer = dsub / dst.resource->levels + dst_z;
        rc.dst_x = dst_x;
        rc.dst_y = dst_y;
        rc.src_level = ssub % src.resource->levels;
        rc.src_layer = ssub / src.resource->levels + (box ? static_cast<UINT>(box->Front) : 0u);
        rc.src_x = box ? static_cast<UINT>(box->Left) : 0u;
        rc.src_y = box ? static_cast<UINT>(box->Top) : 0u;
        const UINT sw = src.resource->width >> rc.src_level ? src.resource->width >> rc.src_level : 1u;
        const UINT sh = src.resource->height >> rc.src_level ? src.resource->height >> rc.src_level : 1u;
        rc.width = box ? static_cast<UINT>(box->Right - box->Left) : sw;
        rc.height = box ? static_cast<UINT>(box->Bottom - box->Top) : sh;
        rc.layers = box ? static_cast<UINT>(box->Back - box->Front) : 1u;
        const UINT receipt[] = {0u, 2u, rc.width, rc.height, rc.layers};
        AdmissionUmdDiagnostic("measure-d3d12-copy-texture", S_OK, receipt, 5);
        AgxD3d12Command *slot = AppendCommand(list);
        if (!slot) return;
        slot->kind = kCommandTextureRegionCopy;
        slot->region_copy = rc;
        return;
    }
    AgxD3d12TextureCopy copy = {};
    if (!refusal) {
        const bool to_buffer = dst.footprint != nullptr;
        const AgxD3d12CopySide &texture = to_buffer ? src : dst, &buffer = to_buffer ? dst : src;
        const UINT block = AgxD3d12FormatBlockBytes(texture.resource->format);
        const UINT sub = static_cast<UINT>(texture.offset);
        copy.texture = texture.resource->heap;
        copy.level = sub % texture.resource->levels;
        copy.layer = sub / texture.resource->levels;
        copy.buffer = buffer.resource->heap;
        copy.pitch = buffer.footprint->Pitch;
        copy.slice_pitch = buffer.footprint->SlicePitch;
        copy.to_buffer = to_buffer;
        /* the box addresses the source; the destination point the destination */
        const UINT bx = box ? static_cast<UINT>(box->Left) : 0u, by = box ? static_cast<UINT>(box->Top) : 0u;
        const UINT bz = box ? static_cast<UINT>(box->Front) : 0u;
        UINT w, h, d;
        if (box) {
            w = static_cast<UINT>(box->Right - box->Left);
            h = static_cast<UINT>(box->Bottom - box->Top);
            d = static_cast<UINT>(box->Back - box->Front);
        } else if (to_buffer) {
            w = texture.resource->width >> copy.level ? texture.resource->width >> copy.level : 1u;
            h = texture.resource->height >> copy.level ? texture.resource->height >> copy.level : 1u;
            d = 1;
        } else {
            w = buffer.footprint->PhysicalWidth;
            h = buffer.footprint->PhysicalHeight;
            d = buffer.footprint->PhysicalDepth ? buffer.footprint->PhysicalDepth : 1u;
        }
        copy.width = w;
        copy.height = h;
        copy.depth = d;
        if (to_buffer) {
            copy.x = bx;
            copy.y = by;
            copy.offset = buffer.resource->gpuva - buffer.resource->heap->gpuva + buffer.offset +
                          static_cast<UINT64>(dst_z) * copy.slice_pitch + static_cast<UINT64>(dst_y) * copy.pitch +
                          static_cast<UINT64>(dst_x) * block;
            copy.layer += bz;
        } else {
            copy.x = dst_x;
            copy.y = dst_y;
            copy.layer += dst_z;
            copy.offset = buffer.resource->gpuva - buffer.resource->heap->gpuva + buffer.offset +
                          static_cast<UINT64>(bz) * copy.slice_pitch + static_cast<UINT64>(by) * copy.pitch +
                          static_cast<UINT64>(bx) * block;
        }
        if (!block || (buffer.footprint->Format != texture.resource->format &&
                       AgxD3d12FormatBlockBytes(buffer.footprint->Format) != block) ||
            copy.layer >= texture.resource->layers || !w || !h || !d)
            refusal = 5u;
    }
    const UINT receipt[] = {refusal, copy.to_buffer ? 1u : 0u, copy.width, copy.height, copy.pitch};
    AdmissionUmdDiagnostic("measure-d3d12-copy-texture", refusal ? E_NOTIMPL : S_OK, receipt, 5);
    if (refusal) {
        ListError(list, refusal == 3u ? E_INVALIDARG : E_NOTIMPL);
        return;
    }
    AgxD3d12Command *slot = AppendCommand(list);
    if (!slot) return;
    AgxD3d12Command &command = *slot;
    command.kind = kCommandTextureCopy;
    command.texture_copy = copy;
}
/* C6b: graphics state setters (each also records its default-state bit) */
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
#define AGX_VB_SETTER IASetVertexBuffers
#define AGX_IB_SETTER IASetIndexBuffer
#else
#define AGX_VB_SETTER AGX_LIST_STATE(pfnIASetVertexBuffers)
#define AGX_IB_SETTER AGX_LIST_STATE(pfnIASetIndexBuffer)
#endif
#define AGX_LIST_BIT(M) (1ULL << ((offsetof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, M) / sizeof(void *)) & 63u))
static VOID APIENTRY OMSetRenderTargets(D3D12DDI_HCOMMANDLIST handle, UINT count, const D3D12DDI_CPU_DESCRIPTOR_HANDLE *rtvs,
                                        BOOL single_range, const D3D12DDI_CPU_DESCRIPTOR_HANDLE *dsv) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    list->state |= AGX_LIST_BIT(pfnOMSetRenderTargets);
    list->rtvs = 0;
    memset(&list->dsv, 0, sizeof(list->dsv));
    if (count > 8 || (count && !rtvs)) {
        const UINT receipt[] = {count, dsv && dsv->ptr ? 1u : 0u};
        AdmissionUmdDiagnostic("measure-d3d12-render-targets-refused", E_INVALIDARG, receipt, 2);
        ListError(list, E_INVALIDARG);
        return;
    }
    if (dsv && dsv->ptr) {
        /* C6f: the DSV is read now, like the RTVs */
        const auto *view = reinterpret_cast<const AgxD3d12Descriptor *>(dsv->ptr);
        if (view->kind == kDescriptorDsv) list->dsv = *view;
    }
    for (UINT i = 0; i < count; ++i) {
        const SIZE_T at = single_range ? rtvs[0].ptr + i * DescriptorBytes(D3D12DDI_DESCRIPTOR_HEAP_TYPE_RTV) : rtvs[i].ptr;
        const auto *view = reinterpret_cast<const AgxD3d12Descriptor *>(at);
        list->rtv[i] = view && view->kind == kDescriptorRtv ? *view : AgxD3d12Descriptor{};
    }
    list->rtvs = count;
}
static VOID APIENTRY RsSetViewports(D3D12DDI_HCOMMANDLIST handle, UINT count, const D3D12DDI_VIEWPORT *viewports) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    list->state |= AGX_LIST_BIT(pfnRsSetViewports);
    if (count && viewports) list->viewport = viewports[0]; /* one viewport (no SV_ViewportArrayIndex yet) */
    else memset(&list->viewport, 0, sizeof(list->viewport));
}
static VOID APIENTRY RsSetScissorRects(D3D12DDI_HCOMMANDLIST handle, UINT count, const D3D12DDI_RECT *rects) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    list->state |= AGX_LIST_BIT(pfnRsSetScissorRects);
    memset(&list->scissor, 0, sizeof(list->scissor));
    if (count && rects) {
        list->scissor.left = rects[0].left;
        list->scissor.top = rects[0].top;
        list->scissor.right = rects[0].right;
        list->scissor.bottom = rects[0].bottom;
    }
}
static VOID APIENTRY IaSetTopology(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_PRIMITIVE_TOPOLOGY topology) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    list->state |= AGX_LIST_BIT(pfnIaSetTopology);
    list->topology = static_cast<UINT>(topology);
}
static VOID APIENTRY OmSetBlendFactor(D3D12DDI_HCOMMANDLIST handle, const FLOAT factor[4]) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    list->state |= AGX_LIST_BIT(pfnOmSetBlendFactor);
    if (factor) memcpy(list->blend_factor, factor, sizeof(list->blend_factor));
}
static VOID APIENTRY OmSetStencilRef(D3D12DDI_HCOMMANDLIST handle, UINT ref) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    list->state |= AGX_LIST_BIT(pfnOmSetStencilRef);
    list->stencil_ref = ref;
}
static VOID APIENTRY SetGraphicsRootSignature(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HROOTSIGNATURE root) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    list->graphics_root = static_cast<const AgxD3d12RootSignature *>(root.pDrvPrivate);
    memset(list->graphics_root_va, 0, sizeof(list->graphics_root_va));
    memset(list->graphics_root_dwords, 0, sizeof(list->graphics_root_dwords));
}
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static VOID APIENTRY IASetVertexBuffers(D3D12DDI_HCOMMANDLIST handle, UINT start, UINT count,
                                        const D3D12DDI_VERTEX_BUFFER_VIEW *views) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    list->state |= AGX_LIST_BIT(pfnIASetVertexBuffers);
    for (UINT i = 0; i < count && start + i < kVertexBuffers; ++i) {
        AgxD3d12VertexBuffer &vb = list->vb[start + i];
        vb = {};
        if (!views || !views[i].BufferLocation) continue;
        vb.heap = HeapAt(list->device, views[i].BufferLocation, &vb.offset);
        vb.stride = views[i].StrideInBytes;
        if (!vb.heap) ListError(list, E_INVALIDARG);
    }
}
/* C6d: R16_UINT/R32_UINT index buffers; a null view unbinds */
static VOID APIENTRY IASetIndexBuffer(D3D12DDI_HCOMMANDLIST handle, const D3D12DDI_INDEX_BUFFER_VIEW *view) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    list->state |= AGX_LIST_BIT(pfnIASetIndexBuffer);
    list->index_heap = nullptr;
    list->index_offset = 0;
    list->index_size = 0;
    if (!view || !view->BufferLocation) return;
    const UINT size = view->Format == DXGI_FORMAT_R16_UINT ? 2u : view->Format == DXGI_FORMAT_R32_UINT ? 4u : 0u;
    list->index_heap = size ? HeapAt(list->device, view->BufferLocation, &list->index_offset) : nullptr;
    list->index_size = list->index_heap ? size : 0u;
    if (!list->index_heap) ListError(list, E_INVALIDARG);
}
static HRESULT CompileGraphics(AgxD3d12Device *device, AgxD3d12PipelineState *pso) {
    if (pso->compiled != S_FALSE) return pso->compiled;
    AGX_D3D12_OWNER *owner = EnsureOwner(device);
    AGX_D3D12_GRAPHICS_DESC desc = {};
    desc.Vs = pso->vs_dwords ? pso->cs + pso->cs_dwords : nullptr;
    desc.Ps = pso->ps_dwords ? pso->cs + pso->cs_dwords + pso->vs_dwords : nullptr;
    desc.AlphaToCoverage = pso->blend.AlphaToCoverageEnable;
    desc.IndependentBlend = pso->blend.IndependentBlendEnable;
    for (UINT i = 0; i < 8; ++i) {
        const D3D12DDI_RENDER_TARGET_BLEND_DESC &rt = pso->blend.RenderTarget[i];
        desc.Rt[i].BlendEnable = rt.BlendEnable;
        desc.Rt[i].Src = rt.SrcBlend;
        desc.Rt[i].Dst = rt.DestBlend;
        desc.Rt[i].Op = rt.BlendOp;
        desc.Rt[i].SrcAlpha = rt.SrcBlendAlpha;
        desc.Rt[i].DstAlpha = rt.DestBlendAlpha;
        desc.Rt[i].OpAlpha = rt.BlendOpAlpha;
        desc.Rt[i].WriteMask = rt.RenderTargetWriteMask;
    }
    desc.SampleMask = pso->desc.SampleMask;
    desc.FillMode = pso->rasterizer.FillMode;
    desc.CullMode = pso->rasterizer.CullMode;
    desc.FrontCounterClockwise = pso->rasterizer.FrontCounterClockwise;
    desc.DepthBias = pso->rasterizer.DepthBias;
    desc.DepthBiasClamp = pso->rasterizer.DepthBiasClamp;
    desc.SlopeScaledDepthBias = pso->rasterizer.SlopeScaledDepthBias;
    desc.DepthClipEnable = pso->rasterizer.DepthClipEnable;
    desc.DepthEnable = pso->depth_stencil.DepthEnable;
    desc.DepthWrite = pso->depth_stencil.DepthWriteMask != 0;
    desc.DepthFunc = pso->depth_stencil.DepthFunc;
    desc.StencilEnable = pso->depth_stencil.StencilEnable;
    desc.StencilReadMask = pso->depth_stencil.StencilReadMask;
    desc.StencilWriteMask = pso->depth_stencil.StencilWriteMask;
    desc.Front = {static_cast<unsigned>(pso->depth_stencil.FrontFace.StencilFailOp),
                  static_cast<unsigned>(pso->depth_stencil.FrontFace.StencilDepthFailOp),
                  static_cast<unsigned>(pso->depth_stencil.FrontFace.StencilPassOp),
                  static_cast<unsigned>(pso->depth_stencil.FrontFace.StencilFunc)};
    desc.Back = {static_cast<unsigned>(pso->depth_stencil.BackFace.StencilFailOp),
                 static_cast<unsigned>(pso->depth_stencil.BackFace.StencilDepthFailOp),
                 static_cast<unsigned>(pso->depth_stencil.BackFace.StencilPassOp),
                 static_cast<unsigned>(pso->depth_stencil.BackFace.StencilFunc)};
    desc.NumElements = pso->elements;
    for (UINT i = 0; i < pso->elements && i < kPipelineElements; ++i) {
        desc.Element[i].Slot = pso->element[i].InputSlot;
        desc.Element[i].Offset = pso->element[i].AlignedByteOffset;
        desc.Element[i].Register = pso->element[i].InputRegister;
        desc.Element[i].PerInstance = pso->element[i].InputSlotClass == D3D12DDI_INPUT_CLASSIFICIATION_PER_INSTANCE_DATA /* sic, WDK spelling */;
        desc.Element[i].StepRate = pso->element[i].InstanceDataStepRate;
        desc.Element[i].Format = pso->element[i].Format;
    }
    pso->compiled = owner && pso->elements <= kPipelineElements ? AgxD3d12OwnerCreateGraphics(owner, &desc, &pso->graphics) : E_FAIL;
    return pso->compiled;
}
#endif
/* C5 (investigation/analysis/D3D12-C5-compute.md): compute state. Setting
 * a root signature resets the root arguments (D3D12 semantics). */
static AgxD3d12CommandList *ListOf(D3D12DDI_HCOMMANDLIST handle) {
    return static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
}
static VOID APIENTRY SetPipelineState(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HPIPELINESTATE pso) {
    AgxD3d12CommandList *list = ListOf(handle);
    if (!list) return;
    list->state |= 1ULL << ((offsetof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetPipelineState) / sizeof(void *)) & 63u);
    list->pso = static_cast<AgxD3d12PipelineState *>(pso.pDrvPrivate);
}
static VOID APIENTRY SetComputeRootSignature(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HROOTSIGNATURE root) {
    AgxD3d12CommandList *list = ListOf(handle);
    if (!list) return;
    list->compute_root = static_cast<const AgxD3d12RootSignature *>(root.pDrvPrivate);
    memset(list->compute_root_va, 0, sizeof(list->compute_root_va));
    memset(list->compute_root_dwords, 0, sizeof(list->compute_root_dwords));
}
/* The runtime's default state at list creation/reset (EXP1363) and an
 * explicit clear: root argument values go, the signature stays. */
static VOID APIENTRY ClearRootArguments(D3D12DDI_HCOMMANDLIST handle) {
    AgxD3d12CommandList *list = ListOf(handle);
    if (!list) return;
    list->state |= 1ULL << ((offsetof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearRootArguments) / sizeof(void *)) & 63u);
    memset(list->compute_root_va, 0, sizeof(list->compute_root_va));
    memset(list->compute_root_dwords, 0, sizeof(list->compute_root_dwords));
    memset(list->graphics_root_va, 0, sizeof(list->graphics_root_va));
    memset(list->graphics_root_dwords, 0, sizeof(list->graphics_root_dwords));
}
/* A root buffer view (CBV/SRV/UAV) argument must name a parameter of that
 * type of the bind point's signature (C6c: compute or graphics). */
static void SetRootBufferView(AgxD3d12CommandList *list, UINT index, D3D12DDI_GPU_VIRTUAL_ADDRESS va, UINT type,
                              bool graphics = false) {
    const AgxD3d12RootSignature *root = graphics ? list->graphics_root : list->compute_root;
    if (!root || index >= root->parameters || index >= kRootArguments || root->parameter[index].type != type) {
        ListError(list, E_INVALIDARG);
        return;
    }
    (graphics ? list->graphics_root_va : list->compute_root_va)[index] = va;
}
static VOID APIENTRY SetComputeRootConstantBufferView(D3D12DDI_HCOMMANDLIST handle, UINT index,
                                                      D3D12DDI_GPU_VIRTUAL_ADDRESS va) {
    if (AgxD3d12CommandList *list = ListOf(handle)) SetRootBufferView(list, index, va, D3D12DDI_ROOT_PARAMETER_TYPE_CBV);
}
static VOID APIENTRY SetComputeRootShaderResourceView(D3D12DDI_HCOMMANDLIST handle, UINT index,
                                                      D3D12DDI_GPU_VIRTUAL_ADDRESS va) {
    if (AgxD3d12CommandList *list = ListOf(handle)) SetRootBufferView(list, index, va, D3D12DDI_ROOT_PARAMETER_TYPE_SRV);
}
/* C5c: a table argument is the GPU handle of its first descriptor. */
static VOID APIENTRY SetComputeRootDescriptorTable(D3D12DDI_HCOMMANDLIST handle, UINT index,
                                                   D3D12DDI_GPU_DESCRIPTOR_HANDLE base) {
    if (AgxD3d12CommandList *list = ListOf(handle))
        SetRootBufferView(list, index, base.ptr, D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
}
/* Root constants: DestOffset + count must stay inside the parameter. */
static void SetRoot32BitConstants(AgxD3d12CommandList *list, bool graphics, UINT index, UINT count, const void *data,
                                  UINT offset) {
    const AgxD3d12RootSignature *root = graphics ? list->graphics_root : list->compute_root;
    if (!root || index >= root->parameters || (count && !data) ||
        root->parameter[index].type != D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS ||
        offset > root->parameter[index].dwords || count > root->parameter[index].dwords - offset ||
        root->parameter[index].offset + root->parameter[index].dwords > kRootArguments) {
        ListError(list, E_INVALIDARG);
        return;
    }
    if (count)
        memcpy(&(graphics ? list->graphics_root_dwords : list->compute_root_dwords)[root->parameter[index].offset + offset],
               data, count * sizeof(UINT));
}
static VOID APIENTRY SetComputeRoot32BitConstants(D3D12DDI_HCOMMANDLIST handle, UINT index, UINT count,
                                                  const void *data, UINT offset) {
    if (AgxD3d12CommandList *list = ListOf(handle)) SetRoot32BitConstants(list, false, index, count, data, offset);
}
static VOID APIENTRY SetComputeRoot32BitConstant(D3D12DDI_HCOMMANDLIST handle, UINT index, UINT value, UINT offset) {
    SetComputeRoot32BitConstants(handle, index, 1, &value, offset);
}
/* C6c: the graphics bind point */
static VOID APIENTRY SetGraphicsRoot32BitConstants(D3D12DDI_HCOMMANDLIST handle, UINT index, UINT count,
                                                   const void *data, UINT offset) {
    if (AgxD3d12CommandList *list = ListOf(handle)) SetRoot32BitConstants(list, true, index, count, data, offset);
}
static VOID APIENTRY SetGraphicsRoot32BitConstant(D3D12DDI_HCOMMANDLIST handle, UINT index, UINT value, UINT offset) {
    SetGraphicsRoot32BitConstants(handle, index, 1, &value, offset);
}
static VOID APIENTRY SetGraphicsRootConstantBufferView(D3D12DDI_HCOMMANDLIST handle, UINT index,
                                                       D3D12DDI_GPU_VIRTUAL_ADDRESS va) {
    if (AgxD3d12CommandList *list = ListOf(handle)) SetRootBufferView(list, index, va, D3D12DDI_ROOT_PARAMETER_TYPE_CBV, true);
}
static VOID APIENTRY SetGraphicsRootShaderResourceView(D3D12DDI_HCOMMANDLIST handle, UINT index,
                                                       D3D12DDI_GPU_VIRTUAL_ADDRESS va) {
    if (AgxD3d12CommandList *list = ListOf(handle)) SetRootBufferView(list, index, va, D3D12DDI_ROOT_PARAMETER_TYPE_SRV, true);
}
static VOID APIENTRY SetGraphicsRootDescriptorTable(D3D12DDI_HCOMMANDLIST handle, UINT index,
                                                    D3D12DDI_GPU_DESCRIPTOR_HANDLE base) {
    if (AgxD3d12CommandList *list = ListOf(handle))
        SetRootBufferView(list, index, base.ptr, D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, true);
}
static VOID APIENTRY SetComputeRootUnorderedAccessView(D3D12DDI_HCOMMANDLIST handle, UINT index,
                                                       D3D12DDI_GPU_VIRTUAL_ADDRESS va) {
    if (AgxD3d12CommandList *list = ListOf(handle)) SetRootBufferView(list, index, va, D3D12DDI_ROOT_PARAMETER_TYPE_UAV);
}
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static HRESULT CompileCompute(AgxD3d12Device *device, AgxD3d12PipelineState *pso) {
    if (pso->compiled != S_FALSE) return pso->compiled;
    AGX_D3D12_OWNER *owner = EnsureOwner(device);
    pso->compiled = owner ? AgxD3d12OwnerCreateCompute(owner, pso->cs, &pso->compute) : E_FAIL;
    return pso->compiled;
}
#endif
static VOID APIENTRY DestroyPipelineState(D3D12DDI_HDEVICE handle, D3D12DDI_HPIPELINESTATE pso_handle) {
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    AgxD3d12Device *device = DeviceOf(handle);
    auto *pso = static_cast<AgxD3d12PipelineState *>(pso_handle.pDrvPrivate);
    if (device && device->owner && pso && pso->compiled == S_OK) {
        AgxD3d12OwnerDestroyCompute(static_cast<AGX_D3D12_OWNER *>(device->owner), &pso->compute);
        AgxD3d12OwnerDestroyGraphics(static_cast<AGX_D3D12_OWNER *>(device->owner), &pso->graphics);
    }
#else
    (void)handle;
    (void)pso_handle;
#endif
}
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
/* C5c: a table's CBV/UAV ranges become slots b#/u# pointing at their
 * descriptors in the shader-visible heap (an owner heap, resolved by the
 * table's GPU handle); the descriptors are read at execution. SRV and
 * sampler ranges refuse (reason 4); a table outside its heap refuses
 * (reason 6). */
static UINT BindTable(AgxD3d12CommandList *list, const AgxD3d12RootSignature *root, const AgxD3d12RootParameter &p,
                      UINT64 base, AgxD3d12Dispatch *dispatch) {
    UINT64 offset = 0;
    AgxD3d12Heap *heap = HeapAt(list->device, base, &offset);
    if (!heap || !heap->cpu) return 6u;
    const AgxD3d12Range *ranges = RangesOf(root) + p.first_range;
    for (UINT r = 0; r < p.ranges; ++r) {
        const AgxD3d12Range &range = ranges[r];
        if (range.type == D3D12DDI_DESCRIPTOR_RANGE_TYPE_SAMPLER) {
            /* C6e: a sampler table (sampler heaps hold only samplers) */
            if (range.space || range.base_register >= kSamplerSlots) return 5u;
            const UINT count = range.count == UINT_MAX || range.count > kSamplerSlots - range.base_register
                                   ? kSamplerSlots - range.base_register : range.count;
            for (UINT d = 0; d < count; ++d) {
                const UINT64 at = offset + (static_cast<UINT64>(range.offset) + d) * DescriptorBytes(D3D12DDI_DESCRIPTOR_HEAP_TYPE_SAMPLER);
                if (range.offset == UINT_MAX || at + sizeof(AgxD3d12SamplerDescriptor) > heap->bytes) {
                    if (range.count == UINT_MAX) break;
                    return 6u;
                }
                dispatch->sampler[range.base_register + d] =
                    reinterpret_cast<const AgxD3d12SamplerDescriptor *>(static_cast<const BYTE *>(heap->cpu) + at);
            }
            continue;
        }
        const bool uav = range.type == D3D12DDI_DESCRIPTOR_RANGE_TYPE_UAV;
        const bool srv = range.type == D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV;
        if (!uav && !srv && range.type != D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV) return 4u;
        const UINT slots = uav ? kUavSlots : srv ? kSrvSlots : kCbSlots;
        if (range.space || range.base_register >= slots) return 5u;
        const UINT count = range.count == UINT_MAX || range.count > slots - range.base_register
                               ? slots - range.base_register : range.count;
        for (UINT d = 0; d < count; ++d) {
            const UINT64 at = offset + (static_cast<UINT64>(range.offset) + d) * DescriptorBytes(D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            if (range.offset == UINT_MAX || at + sizeof(AgxD3d12Descriptor) > heap->bytes) {
                if (range.count == UINT_MAX) break; /* an unbounded range ends with its heap */
                return 6u;
            }
            auto *descriptor = reinterpret_cast<const AgxD3d12Descriptor *>(static_cast<const BYTE *>(heap->cpu) + at);
            (uav ? dispatch->uav_descriptor : srv ? dispatch->srv_descriptor : dispatch->cb_descriptor)[range.base_register + d] =
                descriptor;
        }
    }
    return 0u;
}
#endif
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
/* C6c: resolves the parameters of a root signature visible to one stage
 * (D3D12DDI_SHADER_VISIBILITY_*; kAnyStage for compute, which ignores
 * visibility) into slot records: root UAVs/CBVs/SRVs (VA -> owner heap),
 * root constants (a copy of the root DWORDs), CBV/SRV/UAV tables. Returns
 * the refusal reason (0 = bound). */
static constexpr UINT kAnyStage = ~0u;
static UINT ResolveRoot(AgxD3d12CommandList *list, const AgxD3d12RootSignature *root, const UINT64 *va, const UINT *dwords,
                        UINT stage, AgxD3d12Dispatch *out) {
    for (UINT i = 0; root && i < root->parameters; ++i) {
        const AgxD3d12RootParameter &p = root->parameter[i];
        if (stage != kAnyStage && p.visibility != D3D12DDI_SHADER_VISIBILITY_ALL && p.visibility != stage) continue;
        if (p.space || i >= kRootArguments) return 5u;
        if (p.type == D3D12DDI_ROOT_PARAMETER_TYPE_UAV) {
            if (p.shader_register >= kUavSlots) return 5u;
            if (stage != kAnyStage) return 4u; /* graphics UAVs: not yet */
            if (!(out->uav_heap[p.shader_register] = HeapAt(list->device, va[i], &out->uav_offset[p.shader_register])))
                return 6u;
        } else if (p.type == D3D12DDI_ROOT_PARAMETER_TYPE_CBV) {
            if (p.shader_register >= kCbSlots) return 5u;
            if (!(out->cb_heap[p.shader_register] = HeapAt(list->device, va[i], &out->cb_offset[p.shader_register])))
                return 6u;
        } else if (p.type == D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {
            if (p.shader_register >= kCbSlots || p.offset + p.dwords > kRootArguments) return 5u;
            out->cb_constants[p.shader_register] = p.offset + 1u;
            out->cb_count[p.shader_register] = p.dwords;
        } else if (p.type == D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
            if (const UINT refusal = BindTable(list, root, p, va[i], out)) return refusal;
            if (stage != kAnyStage)
                for (UINT u = 0; u < kUavSlots; ++u)
                    if (out->uav_descriptor[u]) return 4u; /* graphics UAVs: not yet */
        } else if (p.type == D3D12DDI_ROOT_PARAMETER_TYPE_SRV) {
            if (p.shader_register >= kSrvSlots) return 5u;
            if (!(out->srv_heap[p.shader_register] = HeapAt(list->device, va[i], &out->srv_offset[p.shader_register])))
                return 6u;
        } else {
            return 4u;
        }
    }
    /* C6e: static samplers visible to the stage (compute: not yet) */
    const D3D12DDI_STATIC_SAMPLER *statics = root ? StaticSamplersOf(root) : nullptr;
    for (UINT i = 0; root && i < root->static_samplers; ++i) {
        const D3D12DDI_STATIC_SAMPLER &s = statics[i];
        if (stage != kAnyStage && s.ShaderVisibility != D3D12DDI_SHADER_VISIBILITY_ALL &&
            static_cast<UINT>(s.ShaderVisibility) != stage)
            continue;
        if (s.RegisterSpace || s.ShaderRegister >= kSamplerSlots) return 5u;
        out->static_sampler[s.ShaderRegister] = &s;
    }
    if (stage == kAnyStage)
        for (UINT s = 0; s < kSamplerSlots; ++s)
            if (out->sampler[s] || out->static_sampler[s]) return 4u; /* compute samplers: not yet */
    memcpy(out->root_dwords, dwords, kRootArguments * sizeof(UINT));
    return 0u;
}
#endif
/* Dispatch resolves the compute root signature now: root UAVs (C5), root
 * CBVs and root constants (C5b), CBV/UAV descriptor tables (C5c); root SRVs
 * and SRV/sampler ranges refuse the dispatch with a receipt naming the
 * reason. */
static VOID APIENTRY Dispatch(D3D12DDI_HCOMMANDLIST handle, UINT x, UINT y, UINT z) {
    AgxD3d12CommandList *list = ListOf(handle);
    if (!list || !x || !y || !z) return; /* an empty grid does nothing */
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    AgxD3d12PipelineState *pso = list->pso;
    const AgxD3d12RootSignature *root = list->compute_root;
    AgxD3d12Dispatch dispatch = {};
    UINT refusal = !pso || !pso->cs_dwords ? 1u : !root ? 2u : list->commands >= kListCommands ? 3u : 0u;
    if (!refusal) refusal = ResolveRoot(list, root, list->compute_root_va, list->compute_root_dwords, kAnyStage, &dispatch);
    if (!refusal && FAILED(CompileCompute(list->device, pso))) refusal = 7u;
    const UINT receipt[] = {refusal, x, y, z, root ? root->parameters : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-dispatch", refusal ? E_NOTIMPL : S_OK, receipt, 5);
    if (refusal) {
        ListError(list, refusal == 3u || refusal == 6u ? E_INVALIDARG : E_NOTIMPL);
        return;
    }
    dispatch.pso = pso;
    dispatch.groups[0] = x;
    dispatch.groups[1] = y;
    dispatch.groups[2] = z;
    AgxD3d12Command *slot = AppendCommand(list);
    if (!slot) return;
    AgxD3d12Command &command = *slot;
    command.kind = kCommandDispatch;
    command.dispatch = dispatch;
#else
    ListError(list, E_NOTIMPL);
#endif
}
/* C6b: DrawInstanced snapshots the graphics state; C6c resolves the
 * graphics root signature for the VS and the PS. Geometry/tessellation
 * stages, graphics UAVs, samplers and depth-stencil views refuse with
 * receipt measure-d3d12-draw {reason}. */
static void RecordDraw(AgxD3d12CommandList *list, UINT vertices, UINT instances, UINT start_vertex, INT base_vertex,
                       UINT start_instance, bool indexed) {
    if (!list || !vertices || !instances) return;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    AgxD3d12PipelineState *pso = list->pso;
    UINT refusal = !pso || !pso->vs_dwords ? 1u
                 : pso->desc.hGeometryShader.pDrvPrivate || pso->desc.hHullShader.pDrvPrivate ||
                           pso->desc.hDomainShader.pDrvPrivate ? 4u
                 : (!list->rtvs || !list->rtv[0].heap) && list->dsv.kind != kDescriptorDsv ? 5u
                 : indexed && !list->index_heap ? 6u
                 : list->commands >= kListCommands ? 3u : 0u;
    AgxD3d12Dispatch vs_bind = {}, ps_bind = {};
    if (!refusal) refusal = ResolveRoot(list, list->graphics_root, list->graphics_root_va, list->graphics_root_dwords,
                                        D3D12DDI_SHADER_VISIBILITY_VERTEX, &vs_bind);
    if (!refusal) refusal = ResolveRoot(list, list->graphics_root, list->graphics_root_va, list->graphics_root_dwords,
                                        D3D12DDI_SHADER_VISIBILITY_PIXEL, &ps_bind);
    if (!refusal && FAILED(CompileGraphics(list->device, pso))) refusal = 7u;
    const UINT receipt[] = {refusal, vertices, instances, list->rtvs, list->topology, indexed ? list->index_size : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-draw", refusal ? E_NOTIMPL : S_OK, receipt, 6);
    if (refusal) {
        ListError(list, refusal == 3u ? E_INVALIDARG : E_NOTIMPL);
        return;
    }
    AgxD3d12Command *slot = AppendCommand(list);
    if (!slot) return;
    AgxD3d12Command &command = *slot;
    command.kind = kCommandDraw;
    AgxD3d12Draw &draw = command.draw;
    draw.pso = pso;
    draw.vs_bind = vs_bind;
    draw.ps_bind = ps_bind;
    draw.rtvs = list->rtvs;
    memcpy(draw.rtv, list->rtv, sizeof(draw.rtv));
    draw.dsv = list->dsv;
    draw.viewport = list->viewport;
    draw.scissor = list->scissor;
    draw.topology = list->topology;
    memcpy(draw.vb, list->vb, sizeof(draw.vb));
    memcpy(draw.blend_factor, list->blend_factor, sizeof(draw.blend_factor));
    draw.stencil_ref = list->stencil_ref;
    draw.vertices = vertices;
    draw.instances = instances;
    draw.start_vertex = start_vertex;
    draw.start_instance = start_instance;
    if (indexed) {
        draw.index_heap = list->index_heap;
        draw.index_offset = list->index_offset;
        draw.index_size = list->index_size;
        draw.base_vertex = base_vertex;
    }
#else
    (void)start_vertex;
    (void)base_vertex;
    (void)start_instance;
    (void)indexed;
    ListError(list, E_NOTIMPL);
#endif
}
static VOID APIENTRY DrawInstanced(D3D12DDI_HCOMMANDLIST handle, UINT vertices, UINT instances, UINT start_vertex,
                                   UINT start_instance) {
    RecordDraw(static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate), vertices, instances, start_vertex, 0,
               start_instance, false);
}
/* C6d: the strip cut value comes from the pipeline (IBStripCutValue) */
static VOID APIENTRY DrawIndexedInstanced(D3D12DDI_HCOMMANDLIST handle, UINT indices, UINT instances, UINT start_index,
                                          INT base_vertex, UINT start_instance) {
    RecordDraw(static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate), indices, instances, start_index, base_vertex,
               start_instance, true);
}
/* Between copies on one queue the owner's libagx copies already carry
 * AGX_BARRIER_ALL and execution completes before the next list; transitions
 * need no AGX work yet. */
static VOID APIENTRY ResourceBarrier(D3D12DDI_HCOMMANDLIST, UINT, const D3D12DDIARG_RESOURCE_BARRIER_0003 *) {}
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
/* The owner bindings of one stage's slot records: root views and table
 * descriptors (read now, at execution) of the right kind. */
struct AgxD3d12OwnerBindings {
    AGX_D3D12_BUFFER uav_buffers[kUavSlots], cb_buffers[kCbSlots], srv_buffers[kSrvSlots];
    AGX_D3D12_UAV uavs[kUavSlots];
    AGX_D3D12_CBV cbvs[kCbSlots];
    AGX_D3D12_SRV srvs[kSrvSlots];
    AGX_D3D12_SAMPLER samplers[kSamplerSlots];
};
/* C6e: the static border colours of D3D12DDI_STATIC_BORDER_COLOR */
static void OwnerSampler(const D3D12DDI_SAMPLER_DESC &d, AGX_D3D12_SAMPLER *s) {
    s->Valid = 1;
    s->Filter = static_cast<unsigned>(d.Filter);
    s->AddressU = static_cast<unsigned>(d.AddressU);
    s->AddressV = static_cast<unsigned>(d.AddressV);
    s->AddressW = static_cast<unsigned>(d.AddressW);
    s->MaxAnisotropy = d.MaxAnisotropy;
    s->ComparisonFunc = static_cast<unsigned>(d.ComparisonFunc);
    s->MipLodBias = d.MipLODBias;
    s->MinLod = d.MinLOD;
    s->MaxLod = d.MaxLOD;
    memcpy(s->BorderColor, d.BorderColor, sizeof(s->BorderColor));
}
static void OwnerBindings(const AgxD3d12Dispatch &b, AgxD3d12OwnerBindings *o) {
    memset(o, 0, sizeof(*o));
    for (UINT u = 0; u < kUavSlots; ++u) {
        const AgxD3d12Descriptor *view = b.uav_descriptor[u];
        if (view && view->kind == kDescriptorUavBuffer && view->heap) {
            o->uav_buffers[u] = HeapBuffer(view->heap);
            o->uavs[u] = {&o->uav_buffers[u], view->offset, view->bytes};
        }
        if (!b.uav_heap[u]) continue;
        o->uav_buffers[u] = HeapBuffer(b.uav_heap[u]);
        o->uavs[u] = {&o->uav_buffers[u], b.uav_offset[u], 0};
    }
    for (UINT c = 0; c < kCbSlots; ++c) {
        const AgxD3d12Descriptor *view = b.cb_descriptor[c];
        if (view && view->kind == kDescriptorCbv && view->heap) {
            o->cb_buffers[c] = HeapBuffer(view->heap);
            o->cbvs[c].Buffer = &o->cb_buffers[c];
            o->cbvs[c].Offset = view->offset;
            o->cbvs[c].Bytes = static_cast<unsigned>(view->bytes);
        } else if (b.cb_heap[c]) {
            o->cb_buffers[c] = HeapBuffer(b.cb_heap[c]);
            o->cbvs[c].Buffer = &o->cb_buffers[c];
            o->cbvs[c].Offset = b.cb_offset[c];
        } else if (b.cb_constants[c]) {
            o->cbvs[c].Data = &b.root_dwords[b.cb_constants[c] - 1u];
            o->cbvs[c].Bytes = (b.cb_count[c] * sizeof(UINT) + 15u) & ~15u;
        }
    }
    for (UINT s = 0; s < kSrvSlots; ++s) {
        const AgxD3d12Descriptor *view = b.srv_descriptor[s];
        if (view && view->kind == kDescriptorSrvBuffer && view->heap) {
            o->srv_buffers[s] = HeapBuffer(view->heap);
            o->srvs[s] = {&o->srv_buffers[s], view->offset, view->bytes};
        } else if (view && view->kind == kDescriptorSrvTexture && view->heap) {
            o->srv_buffers[s] = HeapBuffer(view->heap);
            o->srvs[s].Buffer = &o->srv_buffers[s];
            o->srvs[s].Texture = 1;
            o->srvs[s].Format = view->format;
            o->srvs[s].FirstLevel = view->level;
            o->srvs[s].Levels = view->levels;
            o->srvs[s].FirstLayer = view->first_layer;
            o->srvs[s].Layers = view->layers;
        } else if (b.srv_heap[s]) {
            o->srv_buffers[s] = HeapBuffer(b.srv_heap[s]);
            o->srvs[s] = {&o->srv_buffers[s], b.srv_offset[s], 0};
        }
    }
    for (UINT s = 0; s < kSamplerSlots; ++s) {
        if (b.sampler[s] && b.sampler[s]->kind == kDescriptorSampler) {
            OwnerSampler(b.sampler[s]->desc, &o->samplers[s]);
        } else if (const D3D12DDI_STATIC_SAMPLER *st = b.static_sampler[s]) {
            static const FLOAT border[5][4] = {{0, 0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1, 1}, {0, 0, 0, 1}, {1, 1, 1, 1}};
            D3D12DDI_SAMPLER_DESC d = {};
            d.Filter = st->Filter;
            d.AddressU = st->AddressU;
            d.AddressV = st->AddressV;
            d.AddressW = st->AddressW;
            d.MipLODBias = st->MipLODBias;
            d.MaxAnisotropy = st->MaxAnisotropy;
            d.ComparisonFunc = st->ComparisonFunc;
            memcpy(d.BorderColor, border[static_cast<UINT>(st->BorderColor) < 5 ? static_cast<UINT>(st->BorderColor) : 0],
                   sizeof(d.BorderColor));
            d.MinLOD = st->MinLOD;
            d.MaxLOD = st->MaxLOD;
            OwnerSampler(d, &o->samplers[s]);
        }
    }
}
/* Replay of the large commands in their own (non-inlined) frames, so the
 * owner-binding records of one command kind are live at a time
 * (STACK_FRAMES gate: EXP1369 build measured 12944 bytes before this). */
static __declspec(noinline) HRESULT ReplayDispatch(AGX_D3D12_OWNER *owner, const AgxD3d12Dispatch &dispatch) {
    AgxD3d12OwnerBindings bind;
    OwnerBindings(dispatch, &bind);
    return AgxD3d12OwnerDispatch(owner, &dispatch.pso->compute, bind.uavs, bind.cbvs, bind.srvs, dispatch.groups[0],
                                 dispatch.groups[1], dispatch.groups[2]);
}
struct AgxD3d12DrawScratch {
    AGX_D3D12_BUFFER rtv_textures[8], vb_buffers[kVertexBuffers], dsv_texture, index_buffer;
    AGX_D3D12_DRAW draw;
    AgxD3d12OwnerBindings vs_bind, ps_bind;
};
static __declspec(noinline) HRESULT ReplayDraw(AGX_D3D12_OWNER *owner, const AgxD3d12Draw &d) {
    /* the draw's owner records (~10 KB) live on the process heap */
    auto *scratch = static_cast<AgxD3d12DrawScratch *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(AgxD3d12DrawScratch)));
    if (!scratch) return E_OUTOFMEMORY;
    AGX_D3D12_BUFFER(&rtv_textures)[8] = scratch->rtv_textures;
    AGX_D3D12_BUFFER(&vb_buffers)[kVertexBuffers] = scratch->vb_buffers;
    AGX_D3D12_DRAW &draw = scratch->draw;
    draw.Pipeline = &d.pso->graphics;
    draw.NumRtvs = d.rtvs;
    for (UINT r = 0; r < d.rtvs; ++r) {
        if (!d.rtv[r].heap) continue;
        rtv_textures[r] = HeapBuffer(d.rtv[r].heap);
        draw.Rtv[r].Texture = &rtv_textures[r];
        draw.Rtv[r].Format = d.rtv[r].format;
        draw.Rtv[r].Level = d.rtv[r].level;
        draw.Rtv[r].FirstLayer = d.rtv[r].first_layer;
        draw.Rtv[r].Layers = d.rtv[r].layers;
    }
    AGX_D3D12_BUFFER &dsv_texture = scratch->dsv_texture;
    if (d.dsv.kind == kDescriptorDsv && d.dsv.heap) {
        dsv_texture = HeapBuffer(d.dsv.heap);
        draw.Dsv.Texture = &dsv_texture;
        draw.Dsv.Format = d.dsv.format;
        draw.Dsv.Level = d.dsv.level;
        draw.Dsv.FirstLayer = d.dsv.first_layer;
        draw.Dsv.Layers = d.dsv.layers;
    }
    draw.Viewport[0] = d.viewport.TopLeftX;
    draw.Viewport[1] = d.viewport.TopLeftY;
    draw.Viewport[2] = d.viewport.Width;
    draw.Viewport[3] = d.viewport.Height;
    draw.Viewport[4] = d.viewport.MinDepth;
    draw.Viewport[5] = d.viewport.MaxDepth;
    draw.Scissor = d.scissor;
    draw.Topology = d.topology;
    for (UINT v = 0; v < kVertexBuffers; ++v) {
        if (!d.vb[v].heap) continue;
        vb_buffers[v] = HeapBuffer(d.vb[v].heap);
        draw.Vb[v].Buffer = &vb_buffers[v];
        draw.Vb[v].Offset = d.vb[v].offset;
        draw.Vb[v].Stride = d.vb[v].stride;
    }
    memcpy(draw.BlendFactor, d.blend_factor, sizeof(draw.BlendFactor));
    draw.StencilRef = d.stencil_ref;
    draw.VertexCount = d.vertices;
    draw.InstanceCount = d.instances;
    draw.StartVertex = d.start_vertex;
    draw.StartInstance = d.start_instance;
    AGX_D3D12_BUFFER &index_buffer = scratch->index_buffer;
    if (d.index_size && d.index_heap) {
        index_buffer = HeapBuffer(d.index_heap);
        draw.IndexBuffer = &index_buffer;
        draw.IndexOffset = d.index_offset;
        draw.IndexSize = d.index_size;
        draw.BaseVertex = d.base_vertex;
        draw.RestartIndex = d.pso->desc.IBStripCutValue == D3D12DDI_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF ? 0xffffu
                          : d.pso->desc.IBStripCutValue == D3D12DDI_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF ? 0xffffffffu
                                                                                                      : 0u;
    }
    AgxD3d12OwnerBindings &vs_bind = scratch->vs_bind, &ps_bind = scratch->ps_bind;
    OwnerBindings(d.vs_bind, &vs_bind);
    OwnerBindings(d.ps_bind, &ps_bind);
    memcpy(draw.Vs.Cbvs, vs_bind.cbvs, sizeof(draw.Vs.Cbvs));
    memcpy(draw.Vs.Srvs, vs_bind.srvs, sizeof(draw.Vs.Srvs));
    memcpy(draw.Ps.Cbvs, ps_bind.cbvs, sizeof(draw.Ps.Cbvs));
    memcpy(draw.Ps.Srvs, ps_bind.srvs, sizeof(draw.Ps.Srvs));
    memcpy(draw.Vs.Samplers, vs_bind.samplers, sizeof(draw.Vs.Samplers));
    memcpy(draw.Ps.Samplers, ps_bind.samplers, sizeof(draw.Ps.Samplers));
    const HRESULT hr = AgxD3d12OwnerDraw(owner, &draw);
    HeapFree(GetProcessHeap(), 0, scratch);
    return hr;
}
#endif
static VOID APIENTRY ExecuteCommandLists(D3D12DDI_HCOMMANDQUEUE, UINT count, const D3D12DDI_HCOMMANDLIST *lists) {
    UINT commands = 0;
    HRESULT hr = S_OK;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    AgxD3d12Device *device = nullptr;
#endif
    for (UINT i = 0; lists && i < count; ++i) {
        auto *list = static_cast<AgxD3d12CommandList *>(lists[i].pDrvPrivate);
        if (!list) continue;
        commands += list->commands;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
        for (UINT c = 0; SUCCEEDED(hr) && c < list->commands; ++c) {
            const AgxD3d12Command &command = list->command[c];
            device = list->device;
            auto *owner = static_cast<AGX_D3D12_OWNER *>(device->owner);
            if (command.kind == kCommandCopy) {
                const AgxD3d12Copy &copy = command.copy;
                AGX_D3D12_BUFFER dst = HeapBuffer(copy.dst), src = HeapBuffer(copy.src);
                hr = AgxD3d12OwnerCopy(owner, &dst, copy.dst_offset, &src, copy.src_offset, copy.bytes);
            } else if (command.kind == kCommandClear) {
                const AgxD3d12Clear &clear = command.clear;
                AGX_D3D12_BUFFER texture = HeapBuffer(clear.rtv.heap);
                hr = AgxD3d12OwnerClearRenderTarget(owner, &texture, clear.rtv.format, clear.rtv.level,
                                                    clear.rtv.first_layer, clear.rtv.layers, clear.color,
                                                    clear.rects ? clear.rect : nullptr, clear.rects);
            } else if (command.kind == kCommandTextureRegionCopy) {
                const AgxD3d12TextureRegionCopy &rc = command.region_copy;
                AGX_D3D12_BUFFER dst = HeapBuffer(rc.dst), src = HeapBuffer(rc.src);
                if (rc.whole_levels) {
                    for (UINT level = 0; SUCCEEDED(hr) && level < rc.whole_levels; ++level) {
                        const UINT w = rc.width >> level ? rc.width >> level : 1u, h = rc.height >> level ? rc.height >> level : 1u;
                        hr = AgxD3d12OwnerCopyTextureRegion(owner, &dst, level, 0, 0, 0, &src, level, 0, 0, 0, w, h, rc.layers);
                    }
                } else {
                    hr = AgxD3d12OwnerCopyTextureRegion(owner, &dst, rc.dst_level, rc.dst_layer, rc.dst_x, rc.dst_y, &src,
                                                        rc.src_level, rc.src_layer, rc.src_x, rc.src_y, rc.width,
                                                        rc.height, rc.layers);
                }
            } else if (command.kind == kCommandClearDepth) {
                const AgxD3d12ClearDepth &clear = command.clear_depth;
                AGX_D3D12_BUFFER texture = HeapBuffer(clear.dsv.heap);
                hr = AgxD3d12OwnerClearDepthStencil(owner, &texture, clear.dsv.format, clear.dsv.level,
                                                    clear.dsv.first_layer, clear.dsv.layers, clear.flags, clear.depth,
                                                    clear.stencil, clear.rects ? clear.rect : nullptr, clear.rects);
            } else if (command.kind == kCommandDraw) {
                hr = ReplayDraw(owner, command.draw);
            } else if (command.kind == kCommandTextureCopy) {
                const AgxD3d12TextureCopy &tc = command.texture_copy;
                AGX_D3D12_BUFFER texture = HeapBuffer(tc.texture), buffer = HeapBuffer(tc.buffer);
                AGX_D3D12_TEXTURE_COPY copy = {};
                copy.Texture = &texture;
                copy.Level = tc.level;
                copy.Layer = tc.layer;
                copy.X = tc.x;
                copy.Y = tc.y;
                copy.Width = tc.width;
                copy.Height = tc.height;
                copy.Depth = tc.depth;
                copy.Buffer = &buffer;
                copy.Offset = tc.offset;
                copy.Pitch = tc.pitch;
                copy.SlicePitch = tc.slice_pitch;
                copy.ToBuffer = tc.to_buffer;
                hr = AgxD3d12OwnerCopyTexture(owner, &copy);
            } else {
                hr = ReplayDispatch(owner, command.dispatch);
            }
        }
#endif
    }
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if (SUCCEEDED(hr) && device && device->owner)
        hr = AgxD3d12OwnerSubmitAndWait(static_cast<AGX_D3D12_OWNER *>(device->owner));
    if (FAILED(hr) && device && device->callbacks && device->callbacks->pfnSetErrorCb)
        device->callbacks->pfnSetErrorCb(device->runtime, hr);
#endif
    const UINT receipt[] = {count, commands};
    AdmissionUmdDiagnostic("measure-d3d12-execute", hr, receipt, 2);
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
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandQueueSize, CalcPrivateCommandQueueSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandQueue, CreateCommandQueue);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandQueue, DestroyCommandQueue);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandAllocatorSize, CalcPrivateCommandAllocatorSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandAllocator, CreateCommandAllocator);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandAllocator, DestroyCommandAllocator);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnResetCommandAllocator, ResetCommandAllocator);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivatePipelineStateSize, CalcPrivatePipelineStateSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreatePipelineState, CreatePipelineState);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyPipelineState, DestroyPipelineState);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandListSize, CalcPrivateCommandListSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandList, CreateCommandList);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandList, DestroyCommandList);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateFenceSize, CalcPrivateFenceSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateFence, CreateFence);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyFence, DestroyFence);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDescriptorHeapSize, CalcPrivateDescriptorHeapSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDescriptorHeap, CreateDescriptorHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyDescriptorHeap, DestroyDescriptorHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetDescriptorSizeInBytes, GetDescriptorSizeInBytes);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetCPUDescriptorHandleForHeapStart, GetCPUDescriptorHandleForHeapStart);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetGPUDescriptorHandleForHeapStart, GetGPUDescriptorHandleForHeapStart);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateShaderResourceView, CreateShaderResourceView);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateConstantBufferView, CreateConstantBufferView);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateSampler, CreateSampler);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateUnorderedAccessView, CreateUnorderedAccessView);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRenderTargetView, CreateRenderTargetView);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDepthStencilView, CreateDepthStencilView);
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
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCopyDescriptors, CopyDescriptors);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCopyDescriptorsSimple, CopyDescriptorsSimple);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateQueryHeapSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateQueryHeapSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateQueryHeap, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateQueryHeap));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyQueryHeap, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyQueryHeap));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandSignatureSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandSignatureSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandSignature, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandSignature));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandSignature, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandSignature));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceVirtualAddress, CheckResourceVirtualAddress);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceAllocationInfo, CheckResourceAllocationInfo);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckSubresourceInfo, CheckSubresourceInfo);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckExistingResourceAllocationInfo, CheckExistingResourceAllocationInfo);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnOfferResources, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnOfferResources));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnReclaimResources, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnReclaimResources));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetImplicitPhysicalAdapterMask, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetImplicitPhysicalAdapterMask));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetPresentPrivateDriverDataSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetPresentPrivateDriverDataSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnQueryNodeMap, QueryNodeMap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnRetrieveShaderComment, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnRetrieveShaderComment));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceAllocationHandle, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceAllocationHandle));
}
static void FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0003(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 &f) {
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCloseCommandList, CloseCommandList);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResetCommandList, ResetCommandList);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDrawInstanced, DrawInstanced);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDrawIndexedInstanced, DrawIndexedInstanced);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDispatch, Dispatch);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearUnorderedAccessViewUint, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearUnorderedAccessViewUint));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearUnorderedAccessViewFloat, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearUnorderedAccessViewFloat));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearRenderTargetView, ClearRenderTargetView);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearDepthStencilView, ClearDepthStencilView);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDiscardResource, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDiscardResource));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyTextureRegion, CopyTextureRegion);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceCopy, ResourceCopy);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyTiles, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyTiles));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyBufferRegion, CopyBufferRegion);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceResolveSubresource, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceResolveSubresource));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnExecuteBundle, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnExecuteBundle));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnExecuteIndirect, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnExecuteIndirect));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceBarrier, ResourceBarrier);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnBlt, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnBlt));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnPresent, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnPresent));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnBeginQuery, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnBeginQuery));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnEndQuery, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnEndQuery));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResolveQueryData, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResolveQueryData));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetPredication, AGX_LIST_STATE(pfnSetPredication));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIaSetTopology, IaSetTopology);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnRsSetViewports, RsSetViewports);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnRsSetScissorRects, RsSetScissorRects);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOmSetBlendFactor, OmSetBlendFactor);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOmSetStencilRef, OmSetStencilRef);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetPipelineState, SetPipelineState);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetDescriptorHeaps, AGX_LIST_STATE(pfnSetDescriptorHeaps));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootSignature, SetComputeRootSignature);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootSignature, SetGraphicsRootSignature);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootDescriptorTable, SetComputeRootDescriptorTable);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootDescriptorTable, SetGraphicsRootDescriptorTable);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRoot32BitConstant, SetComputeRoot32BitConstant);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRoot32BitConstant, SetGraphicsRoot32BitConstant);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRoot32BitConstants, SetComputeRoot32BitConstants);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRoot32BitConstants, SetGraphicsRoot32BitConstants);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootConstantBufferView, SetComputeRootConstantBufferView);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootConstantBufferView, SetGraphicsRootConstantBufferView);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootShaderResourceView, SetComputeRootShaderResourceView);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootShaderResourceView, SetGraphicsRootShaderResourceView);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootUnorderedAccessView, SetComputeRootUnorderedAccessView);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootUnorderedAccessView, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootUnorderedAccessView));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIASetIndexBuffer, AGX_IB_SETTER);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIASetVertexBuffers, AGX_VB_SETTER);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSOSetTargets, AGX_LIST_STATE(pfnSOSetTargets));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOMSetRenderTargets, OMSetRenderTargets);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetMarker, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetMarker));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearRootArguments, ClearRootArguments);
}
static void FillD3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 &f) {
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnExecuteCommandLists, ExecuteCommandLists);
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
                                     SIZE_T size, UINT version, D3D12DDI_HRTTABLE rt_table) {
    HRESULT hr = E_NOTIMPL;
    /* C4a: a command list must name the runtime table its DDIs come from
     * (pfnSetCommandListDDITableCb at creation); keep the first one. */
    if (type == D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D && !list_table.handle) list_table = rt_table;
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
