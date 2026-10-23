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
#include <cstdint>
#include <climits>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <new>
#include <tuple>
#include <type_traits>
#include "agx_d3d12_view_mapping.h"
#include "agx_d3d12_r1_blend_projection.h"
#include "agx_d3d12_r1_shader_span.h"
#include "agx_d3d12_zero_cache_library.h"
#include "agx_shader_input_provider.h"
#include "agx_d3d12_r1_io_span.h"
#include "agx_d3d12_r1_shader_transaction.h"
extern "C" {
#include "apple_agx_win32_device_info.h"
#include "render_win32_transport.h"
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
#include "agx_d3d12_windows.h"
#include "agx_d3d12_tile_mapping.h"
#include "../mesa/dxbc/dxbc_minmax_bits.h"
#endif
/* agx_win32_screen.c (compiled into this UMD) */
int AgxWin32DeviceInfoValid(const AGX_WIN32_DEVICE_INFO *Info);
}

#if defined(APPLE_AGX_D3D12_PREPARED_NIR_OWNER) && APPLE_AGX_D3D12_PREPARED_NIR_OWNER
#include "../mesa/dxil/agx_owned_nir_handoff.h"
#endif

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
    /* C50: the interface the runtime negotiated (CalcPrivateDeviceSize
     * Interface/Version, D3D12DDI_SUPPORTED_* form): CORE_0012 and CORE_0033
     * have the same size, so FillDDITable selects tables by it. */
    UINT64 negotiated;
};
static AgxD3d12Adapter adapter_object;
static D3D12DDI_HRTTABLE list_table;
static volatile LONG queue_generation; /* per-device Win32 generations */

/* EXP1347: the runtime destroyed the device after the caps with no traced
 * call; each fail-closed entry records its table (D3D12DDI_TABLE_TYPE) and
 * member offset so the receipt names the DDI the runtime needed. */
template<class T> struct TableOf;
template<> struct TableOf<D3D12DDI_DEVICE_FUNCS_CORE_0003> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_DEVICE_CORE; };
template<> struct TableOf<D3D12DDI_DEVICE_FUNCS_CORE_0010> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_DEVICE_CORE; };
template<> struct TableOf<D3D12DDI_DEVICE_FUNCS_CORE_0033> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_DEVICE_CORE; };
template<> struct TableOf<D3D12DDI_DEVICE_FUNCS_CORE_0040> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_DEVICE_CORE; };
template<> struct TableOf<D3D12DDI_DEVICE_FUNCS_CORE_0070> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_DEVICE_CORE; };
template<> struct TableOf<D3D12DDI_DEVICE_FUNCS_CORE_0075> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_DEVICE_CORE; };
template<> struct TableOf<D3D12DDI_COMMAND_LIST_FUNCS_3D_0003> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D; };
template<> struct TableOf<D3D12DDI_COMMAND_LIST_FUNCS_3D_0033> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D; };
template<> struct TableOf<D3D12DDI_COMMAND_LIST_FUNCS_3D_0040> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D; };
template<> struct TableOf<D3D12DDI_COMMAND_LIST_FUNCS_3D_0062> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D; };
template<> struct TableOf<D3D12DDI_COMMAND_LIST_FUNCS_3D_0074> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D; };
template<> struct TableOf<D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001> { static constexpr UINT value = D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D; };
static void ReportRejectedDeviceError(D3D12DDI_HDEVICE handle, HRESULT error);
static void ReportRejectedListError(D3D12DDI_HCOMMANDLIST handle, HRESULT error);
static void ReportRejectedQueueError(D3D12DDI_HCOMMANDQUEUE handle, HRESULT error);
template<class T, UINT Table, UINT Offset> struct Rejected;
template<class R, class... A, UINT Table, UINT Offset> struct Rejected<R (APIENTRY *)(A...), Table, Offset> {
    static R APIENTRY call(A... args) {
        ((void)args, ...);
        OutputDebugStringA("AGX D3D12 ABI gate: unimplemented DDI\n");
        const UINT receipt[] = {Table, Offset};
        AdmissionUmdDiagnostic("measure-d3d12-rejected", E_NOTIMPL, receipt, 2);
        if constexpr (Table == D3D12DDI_TABLE_TYPE_DEVICE_CORE) {
            static_assert(sizeof...(A) > 0 &&
                          std::is_same_v<std::tuple_element_t<0, std::tuple<A...>>, D3D12DDI_HDEVICE>,
                          "rejected DEVICE_CORE DDI must start with HDEVICE");
            ReportRejectedDeviceError(std::get<0>(std::forward_as_tuple(args...)), E_NOTIMPL);
        } else if constexpr (Table == D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D) {
            static_assert(sizeof...(A) > 0 &&
                          std::is_same_v<std::tuple_element_t<0, std::tuple<A...>>, D3D12DDI_HCOMMANDLIST>,
                          "rejected COMMAND_LIST DDI must start with HCOMMANDLIST");
            ReportRejectedListError(std::get<0>(std::forward_as_tuple(args...)), E_NOTIMPL);
        } else {
            static_assert(Table == D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D && sizeof...(A) > 0 &&
                          std::is_same_v<std::tuple_element_t<0, std::tuple<A...>>, D3D12DDI_HCOMMANDQUEUE>,
                          "rejected COMMAND_QUEUE DDI must start with HCOMMANDQUEUE");
            ReportRejectedQueueError(std::get<0>(std::forward_as_tuple(args...)), E_NOTIMPL);
        }
        if constexpr (std::is_same_v<R, HRESULT>) return E_NOTIMPL;
        else if constexpr (!std::is_void_v<R>) return R{};
    }
};
#define AGX_REJECT(T, M) Rejected<decltype(((T *)0)->M), TableOf<T>::value, static_cast<UINT>(offsetof(T, M))>::call
#define AGX_ASSIGN(T, M, F) (f).M = &F
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
#define AGX_TILED_QUEUE(M, F) F
#define AGX_TILED_DEVICE(T, M, F) F
#else
#define AGX_TILED_QUEUE(M, F) AGX_REJECT(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, M)
#define AGX_TILED_DEVICE(T, M, F) AGX_REJECT(T, M)
#endif

/* C44: SM 6 needs the R1 interface (0011): the 0003 runtime never asks
 * D3D12DDICAPS_TYPE_0011_SHADER_MODELS and keeps applications at SM 5.1.
 * Validated on J313 in APPLE_AGX_D3D12_SM6=1 processes (EXP1859-EXP1865:
 * probes 103/103 on ARM64 and x64, samples and MiniEngine as the 0003
 * interface, the runtime converting DXBC to DXIL), R1 build 2 with SM 6.0
 * and wave operations is the default; APPLE_AGX_D3D12_SM6=0 keeps a process
 * at the 0003 interface and SM 5.1. */
/* 12_1 ROVs (D3D12-FL-program.md "ROV path"): DXIL and DXBC ROV shaders
 * take AGX's pixel interlock; ordering validated on hardware (EXP1893:
 * C59A, C59B), reported by default since then (the runtime shows it at FL
 * >= 11_1); APPLE_AGX_D3D12_ROV=0 opts a process out. */
/* 12_1 conservative rasterization Tier 1 (the TIR expansion GS with a
 * touched-pixel pixel shader, behind the vertex or domain shader or inside
 * the application's GS): validated on hardware (C61A EXP1891B, C61B EXP1895,
 * C61C EXP1896) and reported by default; APPLE_AGX_D3D12_CR=0 opts a
 * process out. Stream output with CR keeps plain rasterization (receipt). */
static BOOL ConservativeRasterEnabled() {
    char value[2] = {};
    return !(GetEnvironmentVariableA("APPLE_AGX_D3D12_CR", value, sizeof(value)) == 1 && value[0] == '0');
}

static BOOL RovEnabled() {
    char value[2] = {};
    return !(GetEnvironmentVariableA("APPLE_AGX_D3D12_ROV", value, sizeof(value)) == 1 && value[0] == '0');
}

/* C53 (D3D12-C53-tiled-textures.md): Tiled Resources Tier 1 and the TILED
 * format bit are reported only in a process with APPLE_AGX_D3D12_TILED=1
 * until reserved buffers and textures are validated on hardware (HW1, HW2). */
static BOOL TiledEnabled() {
    char value[2] = {};
    return GetEnvironmentVariableA("APPLE_AGX_D3D12_TILED", value, sizeof(value)) == 1 && value[0] == '1';
}

static bool ShaderModel6Enabled() {
    static const bool enabled = [] {
        char value[2] = {};
        return !(GetEnvironmentVariableA("APPLE_AGX_D3D12_SM6", value, sizeof(value)) == 1 && value[0] == '0');
    }();
    return enabled;
}
/* C50 (D3D12-C50-interface-0033.md): SM 6.1 is D3D12DDI_SHADER_MODEL_6_1_RELEASE_0033,
 * so it needs R3 build 3 (0033); C51 (D3D12-C51-interface-plan.md): SM 6.2 is
 * _0042, R4 build 2; SM 6.3-6.5 are _0054/_0062/_0071. EXP1871: the runtime
 * accepts 0076 (and 0065, 0054), not 0071, so SM 6.3-6.5 ride on 0076 (R7
 * build 6). APPLE_AGX_D3D12_DDI=0033, 0042 or 0076 offers that interface to
 * an SM6 process until hardware validates its tables. */
/* EXP1869: offered {0071, 0042, ...} the runtime negotiated 0042.
 * APPLE_AGX_D3D12_DDI=scan offers every WDK build from 0099 down to 0043
 * so d3d12-device-size names the highest one the runtime accepts (EXP1870:
 * 0099); sNNN offers only the builds at or below NNN. A negotiated
 * interface without tables here fails closed (FillDDITable). */
static constexpr UINT64 kInterfaceScan = ~0ULL;
static UINT ScanBound() {
    static const UINT bound = [] {
        char value[5] = {};
        if (GetEnvironmentVariableA("APPLE_AGX_D3D12_DDI", value, sizeof(value)) != 4 || value[0] != 's') return 0u;
        UINT n = 0;
        for (UINT i = 1; i < 4; ++i) {
            if (value[i] < '0' || value[i] > '9') return 0u;
            n = n * 10 + static_cast<UINT>(value[i] - '0');
        }
        return n;
    }();
    return bound;
}
/* a D3D12DDI_SUPPORTED_00NN value's NN: interface minor + build, the build
 * itself from 0090 (D3D12DDI_BUILD_VERSION_0090 is 90) */
static UINT InterfaceNumber(UINT64 version) {
    const UINT build = static_cast<UINT>((version >> 16) & 0xffffu);
    return build >= 90u ? build : static_cast<UINT>((version >> 32) & 0xffffu) + build;
}
static const UINT64 kScanVersions[] = {
    D3D12DDI_SUPPORTED_0099, D3D12DDI_SUPPORTED_0098, D3D12DDI_SUPPORTED_0097, D3D12DDI_SUPPORTED_0096,
    D3D12DDI_SUPPORTED_0095, D3D12DDI_SUPPORTED_0094, D3D12DDI_SUPPORTED_0093, D3D12DDI_SUPPORTED_0092,
    D3D12DDI_SUPPORTED_0091, D3D12DDI_SUPPORTED_0090, D3D12DDI_SUPPORTED_0089, D3D12DDI_SUPPORTED_0088,
    D3D12DDI_SUPPORTED_0086, D3D12DDI_SUPPORTED_0084, D3D12DDI_SUPPORTED_0083, D3D12DDI_SUPPORTED_0082,
    D3D12DDI_SUPPORTED_0081, D3D12DDI_SUPPORTED_0080, D3D12DDI_SUPPORTED_0076, D3D12DDI_SUPPORTED_0075,
    D3D12DDI_SUPPORTED_0074, D3D12DDI_SUPPORTED_0073, D3D12DDI_SUPPORTED_0072, D3D12DDI_SUPPORTED_0071,
    D3D12DDI_SUPPORTED_0070, D3D12DDI_SUPPORTED_0065, D3D12DDI_SUPPORTED_0064, D3D12DDI_SUPPORTED_0063,
    D3D12DDI_SUPPORTED_0062, D3D12DDI_SUPPORTED_0061, D3D12DDI_SUPPORTED_0060, D3D12DDI_SUPPORTED_0054,
    D3D12DDI_SUPPORTED_0053, D3D12DDI_SUPPORTED_0052, D3D12DDI_SUPPORTED_0051, D3D12DDI_SUPPORTED_0050,
    D3D12DDI_SUPPORTED_0043};
/* C55: validated on J313 (EXP1871/EXP1872: probes 105/107 at 0033, 106/107
 * at 0042, 107/107 at 0076 on ARM64 and x64; samples and MiniEngine through
 * 0042), interface 0076 with SM 6.5 is the default of an SM6 process;
 * APPLE_AGX_D3D12_DDI=0012 keeps R1 build 2 (SM 6.0), 0033/0042 the
 * intermediate interfaces, APPLE_AGX_D3D12_SM6=0 the 0003 interface. */
static UINT64 InterfaceOptIn() {
    static const UINT64 offered = [] {
        char value[5] = {};
        if (!ShaderModel6Enabled()) return UINT64{0};
        if (GetEnvironmentVariableA("APPLE_AGX_D3D12_DDI", value, sizeof(value)) != 4) return D3D12DDI_SUPPORTED_0076;
        return !memcmp(value, "scan", 4) || ScanBound() ? kInterfaceScan
               : !memcmp(value, "0076", 4) ? D3D12DDI_SUPPORTED_0076
               : !memcmp(value, "0042", 4) ? D3D12DDI_SUPPORTED_0042
               : !memcmp(value, "0033", 4) ? D3D12DDI_SUPPORTED_0033
               : !memcmp(value, "0012", 4) ? UINT64{0}
                                           : D3D12DDI_SUPPORTED_0076;
    }();
    return offered;
}
static bool Negotiated0033() { return adapter_object.negotiated == D3D12DDI_SUPPORTED_0033; }
static bool Negotiated0042() { return adapter_object.negotiated == D3D12DDI_SUPPORTED_0042; }
static bool Negotiated0076() { return adapter_object.negotiated == D3D12DDI_SUPPORTED_0076; }
static bool NegotiatedR2() { return adapter_object.negotiated >= D3D12DDI_SUPPORTED_0021; }
/* C48: R1 build 2 (0012) adds the wave caps (D3D12DDI_SHADER_CAPS_0012) and
 * one device entry, pfnGetDebugAllocationInfo; SM6 processes negotiate it. */
static HRESULT APIENTRY GetVersions(D3D12DDI_HADAPTER, UINT32 *count, UINT64 *versions) {
    UINT scanned = 0;
    UINT64 scan[ARRAYSIZE(kScanVersions)] = {};
    if (InterfaceOptIn() == kInterfaceScan)
        for (UINT64 v : kScanVersions)
            if (!ScanBound() || InterfaceNumber(v) <= ScanBound()) scan[scanned++] = v;
    const UINT offered = scanned                                       ? scanned + 5u
                         : InterfaceOptIn() == D3D12DDI_SUPPORTED_0076 ? 6u
                         : InterfaceOptIn() == D3D12DDI_SUPPORTED_0042 ? 5u
                         : InterfaceOptIn() == D3D12DDI_SUPPORTED_0033 ? 4u
                         : ShaderModel6Enabled()                       ? 3u
                                                                       : 1u;
    const UINT receipt[] = {count ? *count : 0, versions != nullptr, offered};
    AdmissionUmdDiagnostic("d3d12-versions", count ? S_OK : E_INVALIDARG, receipt, 3);
    if (!count) return E_INVALIDARG;
    if (!versions) { *count = offered; return S_OK; }
    if (*count < offered) { *count = offered; return E_INVALIDARG; }
    UINT n = 0;
    for (UINT i = 0; i < scanned; ++i) versions[n++] = scan[i];
    if (offered == 6u) versions[n++] = D3D12DDI_SUPPORTED_0076;
    if (offered >= 5u) versions[n++] = D3D12DDI_SUPPORTED_0042;
    if (offered >= 4u) versions[n++] = D3D12DDI_SUPPORTED_0033;
    if (offered >= 3u) {
        versions[n++] = D3D12DDI_SUPPORTED_0012;
        versions[n++] = D3D12DDI_SUPPORTED_0011;
    }
    versions[n++] = D3D12DDI_SUPPORTED_0003;
    *count = n;
    return S_OK;
}
/* C1: the pipeline level the development profile admits (D3D12 drivers
 * start at FL 11_0; the C8 baseline gates admission outside the profile). */
static const D3D12DDI_3DPIPELINELEVEL kProfileLevel = D3D12DDI_3DPIPELINELEVEL_11_0;
/* FL 11_1 (D3D12-FL-program.md): logic ops (C57), 64 UAVs (C47 tier 2) and
 * forced sample counts (C60A), validated at 11_1 on hardware (EXP1890:
 * C62A, C57A, C60A); the default since then, APPLE_AGX_D3D12_FL111=0 keeps a
 * process at 11_0. The runtime reports ROVs and conservative rasterization
 * only at 11_1 and above (EXP1891). */
static D3D12DDI_3DPIPELINELEVEL ProfileLevel() {
    char value[2] = {};
    return GetEnvironmentVariableA("APPLE_AGX_D3D12_FL111", value, sizeof(value)) == 1 && value[0] == '0'
               ? kProfileLevel : D3D12DDI_3DPIPELINELEVEL_11_1;
}
/* Answers a caps type with T, its remaining bytes zero (a larger, newer
 * structure keeps every later field at its "not supported" zero). */
template<class T, class F> static HRESULT Answer(const D3D12DDIARG_GETCAPS *arg, F fill) {
    if (arg->DataSize < sizeof(T)) return E_INVALIDARG;
    memset(arg->pData, 0, arg->DataSize);
    fill(*static_cast<T *>(arg->pData));
    return S_OK;
}
/* C21: immutable legacy two-slot registry. See resolved-layout-contract note.
 * IDs are descending native square sizes32..1; only four-byte texels admitted. */
static HRESULT VolumeLayoutCaps(const D3D12DDIARG_GETCAPS *arg) {
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if(!arg || !arg->pData) return E_INVALIDARG;
    if(arg->Type==D3D12DDICAPS_TYPE_TEXTURE_LAYOUT) {
        if(!arg->pInfo) {
            if(arg->DataSize!=sizeof(D3D12DDI_TEXTURE_LAYOUT_CAPS) &&
               arg->DataSize!=sizeof(D3D12DDI_TEXTURE_LAYOUT_CAPS_0001)) return E_INVALIDARG;
            memset(arg->pData,0,arg->DataSize);
            auto *caps=static_cast<D3D12DDI_TEXTURE_LAYOUT_CAPS *>(arg->pData);
            caps->DeviceDependentLayoutCount=6;caps->DeviceDependentSwizzleCount=6;
            return S_OK;
        }
        UINT index=0;memcpy(&index,arg->pInfo,sizeof(index));
        if(index>=6 || arg->DataSize!=2*sizeof(D3D12DDI_SWIZZLE_PATTERN)) return E_INVALIDARG;
        const D3D12DDI_SWIZZLE_PATTERN patterns[2]={
            static_cast<D3D12DDI_SWIZZLE_PATTERN>(0x100u+index),
            static_cast<D3D12DDI_SWIZZLE_PATTERN>(0x100u+(index<5?index+1:5u))};
        memcpy(arg->pData,patterns,sizeof(patterns));return S_OK;
    }
    /* the interleave bits of pattern index (four-byte texels, 32>>index square) */
    const auto interleave=[](UINT index,D3D12DDI_SWIZZLE_BIT_ENTRY *bits) {
        UINT n=0;
        for(UINT b=0;b<2;++b) {auto &v=bits[n++];v.Valid=1;v.SourceBitIndex=static_cast<UINT8>(b);}
        for(UINT b=0;b<5-index;++b) {
            auto &x=bits[n++];x.Valid=1;x.SourceBitIndex=static_cast<UINT8>(b+2);
            auto &y=bits[n++];y.Valid=1;y.ChannelIndex=1;y.SourceBitIndex=static_cast<UINT8>(b);
        }
    };
    if(arg->Type==D3D12DDICAPS_TYPE_SWIZZLE_PATTERN) {
        if(!arg->pInfo || arg->DataSize!=sizeof(D3D12DDI_SWIZZLE_PATTERN_DESC_0003)) return E_INVALIDARG;
        UINT index=0;memcpy(&index,arg->pInfo,sizeof(index));if(index>=6)return E_INVALIDARG;
        D3D12DDI_SWIZZLE_PATTERN_DESC_0003 pattern={};
        interleave(index,pattern.InterleavePatternSourceBits);
        memcpy(arg->pData,&pattern,sizeof(pattern));return S_OK;
    }
    /* EXP1870: R2+ runtimes ask the 0022 forms (1060/1061) instead. The same
     * registry: caps _0026 (no standard swizzle, no row-major textures,
     * parameterized patterns), per layout six patterns per plane slot (the
     * 0022 MipLevelSwizzleTransition[5] segments; 0003's two patterns, the
     * second kept for every later segment; transitions stay 0xff), and the
     * 0022 descriptor with the 0003 interleave and no XOR terms. */
    if(arg->Type==D3D12DDICAPS_TYPE_0022_TEXTURE_LAYOUT) {
        if(!arg->pInfo) {
            if(arg->DataSize!=sizeof(D3D12DDI_TEXTURE_LAYOUT_CAPS_0026)) return E_INVALIDARG;
            memset(arg->pData,0,arg->DataSize);
            auto *caps=static_cast<D3D12DDI_TEXTURE_LAYOUT_CAPS_0026 *>(arg->pData);
            caps->DeviceDependentLayoutCount=6;caps->DeviceDependentSwizzleCount=6;
            return S_OK;
        }
        UINT index=0;memcpy(&index,arg->pInfo,sizeof(index));
        if(index>=6 || arg->DataSize!=18*sizeof(D3D12DDI_SWIZZLE_PATTERN)) return E_INVALIDARG;
        D3D12DDI_SWIZZLE_PATTERN patterns[18];
        for(UINT i=0;i<18;++i)
            patterns[i]=static_cast<D3D12DDI_SWIZZLE_PATTERN>(0x100u+(i%6==0?index:(index<5?index+1:5u)));
        memcpy(arg->pData,patterns,sizeof(patterns));return S_OK;
    }
    if(arg->Type==D3D12DDICAPS_TYPE_0022_SWIZZLE_PATTERN) {
        if(!arg->pInfo || arg->DataSize!=sizeof(D3D12DDI_SWIZZLE_PATTERN_DESC_0022)) return E_INVALIDARG;
        UINT index=0;memcpy(&index,arg->pInfo,sizeof(index));if(index>=6)return E_INVALIDARG;
        D3D12DDI_SWIZZLE_PATTERN_DESC_0022 pattern={};
        interleave(index,pattern.InterleavePatternSourceBits);
        memcpy(arg->pData,&pattern,sizeof(pattern));return S_OK;
    }
#else
    (void)arg;
#endif
    return E_NOTIMPL;
}
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static constexpr BOOL kTypedUavLoads = TRUE; /* C49 */
#else
static constexpr BOOL kTypedUavLoads = FALSE;
#endif
static HRESULT APIENTRY GetCaps(D3D12DDI_HADAPTER, const D3D12DDIARG_GETCAPS *arg) {
    HRESULT hr = E_NOTIMPL;
    if (arg && arg->pData) switch (arg->Type) {
    case D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1:
        /* HighestRuntimeSupportedFeatureLevel is the runtime's input: only
         * the output field is written. */
        if (arg->DataSize >= sizeof(D3D12DDI_3DPIPELINESUPPORT1_DATA_0081)) {
            static_cast<D3D12DDI_3DPIPELINESUPPORT1_DATA_0081 *>(arg->pData)
                ->MaximumDriverSupportedFeatureLevel = ProfileLevel();
            hr = S_OK;
        }
        break;
    case D3D12DDICAPS_TYPE_3DPIPELINESUPPORT:
        hr = Answer<D3D12DDI_3DPIPELINELEVEL>(arg, [](auto &d) { d = ProfileLevel(); });
        break;
    /* C44: SM 5.1 and 6.0 (DXIL through agx_dxil_to_nir) under R1 */
    case D3D12DDICAPS_TYPE_0011_SHADER_MODELS:
        if (ShaderModel6Enabled() && arg->DataSize >= sizeof(D3D12DDI_D3D12_SHADER_MODELS_DATA_0011)) {
            /* C50: SM 6.1 at 0033: SV_ViewID lowers to view 0 (ViewInstancingTier
             * NOT_SUPPORTED, View Instancing spec), barycentrics stay unsupported;
             * C51: SM 6.2 at 0042 (native 16-bit ops stay unsupported); C52:
             * SM 6.3-6.5 at 0076 (their core intrinsics -- packed dot products,
             * wave match and multi-prefix -- lower through dxil-spirv; DXR,
             * mesh shaders, VRS and sampler feedback stay unsupported) */
            static const D3D12DDI_SHADER_MODEL models[] = {D3D12DDI_SHADER_MODEL_5_1_RELEASE_0011,
                                                           D3D12DDI_SHADER_MODEL_6_0_RELEASE_0011,
                                                           D3D12DDI_SHADER_MODEL_6_1_RELEASE_0033,
                                                           D3D12DDI_SHADER_MODEL_6_2_RELEASE_0042,
                                                           D3D12DDI_SHADER_MODEL_6_3_RELEASE_0054,
                                                           D3D12DDI_SHADER_MODEL_6_4_RELEASE_0062,
                                                           D3D12DDI_SHADER_MODEL_6_5_RELEASE_0071};
            const UINT offered = Negotiated0076() ? 7u : Negotiated0042() ? 4u : Negotiated0033() ? 3u : 2u;
            auto *data = static_cast<D3D12DDI_D3D12_SHADER_MODELS_DATA_0011 *>(arg->pData);
            if (!data->pNumShaderModelsSupported) {
                hr = E_INVALIDARG;
            } else if (!data->pShaderModelsSupported) {
                *data->pNumShaderModelsSupported = offered;
                hr = S_OK;
            } else {
                const UINT n = *data->pNumShaderModelsSupported < offered ? *data->pNumShaderModelsSupported : offered;
                memcpy(data->pShaderModelsSupported, models, n * sizeof(models[0]));
                *data->pNumShaderModelsSupported = n;
                hr = S_OK;
            }
        }
        break;
    /* EXP1345: after CreateDevice the runtime asks D3D12_OPTIONS (28 bytes,
     * the 0003 structure) and fails creation on E_NOTIMPL. C1 answers the
     * 0003-era device caps with the plan's minimum: binding and heap tier
     * 1, nothing optional; AGX is a unified-memory tile-based deferred
     * renderer (Asahi); no coherence is claimed. */
    case D3D12DDICAPS_TYPE_D3D12_OPTIONS:
        hr = Answer<D3D12DDI_D3D12_OPTIONS_DATA_0003>(arg, [](auto &d) {
            /* C47 (D3D12-C47-binding-tier2.md): SRV tables over the full heap,
             * 64 UAVs (counters included) and 2048 samplers per stage through
             * the descriptor heap; 14 CBVs per stage */
            d.ResourceBindingTier = D3D12DDI_RESOURCE_BINDING_TIER_2;
            d.ConservativeRasterizationTier = ConservativeRasterEnabled() ? D3D12DDI_CONSERVATIVE_RASTERIZATION_TIER_1
                                                                          : D3D12DDI_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED;
            d.TiledResourcesTier = TiledEnabled() ? D3D12DDI_TILED_RESOURCES_TIER_1 /* C53 */
                                                  : D3D12DDI_TILED_RESOURCES_TIER_NOT_SUPPORTED;
            d.CrossNodeSharingTier = D3D12DDI_CROSS_NODE_SHARING_TIER_NOT_SUPPORTED;
            d.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation = FALSE;
            d.OutputMergerLogicOp = TRUE; /* C57 */
            d.ResourceHeapTier = D3D12DDI_RESOURCE_HEAP_TIER_1;
        });
        break;
    /* EXP1869: an R2+ runtime failed device creation on the unanswered
     * UMD-based queue priority caps (1062) after the tables; the R2+ caps
     * whose "not supported" value is defined answer it (R1 runtimes keep
     * the validated E_NOTIMPL) */
    case D3D12DDICAPS_TYPE_0023_UMD_BASED_COMMAND_QUEUE_PRIORITY:
        if (NegotiatedR2())
            hr = Answer<D3D12DDICAPS_UMD_BASED_COMMAND_QUEUE_PRIORITY_DATA_0023>(
                arg, [](auto &d) { d.SupportedQueueFlagsForGlobalRealtimeQueues = D3D12DDI_COMMAND_QUEUE_FLAG_NONE; });
        break;
    case D3D12DDICAPS_TYPE_0022_CPU_PAGE_TABLE_FALSE_POSITIVES:
        if (NegotiatedR2())
            hr = Answer<D3D12DDI_COMMAND_QUEUE_FLAGS>(arg, [](auto &d) { d = D3D12DDI_COMMAND_QUEUE_FLAG_NONE; });
        break;
    case D3D12DDICAPS_TYPE_0030_PROTECTED_RESOURCE_SESSION_SUPPORT:
        /* NodeIndex is the input: only Support is written */
        if (NegotiatedR2() &&
            arg->DataSize >= sizeof(D3D12DDI_PROTECTED_RESOURCE_SESSION_SUPPORT_DATA_0030)) {
            static_cast<D3D12DDI_PROTECTED_RESOURCE_SESSION_SUPPORT_DATA_0030 *>(arg->pData)->Support =
                D3D12DDI_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAG_0030_NONE;
            hr = S_OK;
        }
        break;
    case D3D12DDICAPS_TYPE_0074_PROTECTED_RESOURCE_SESSION_TYPE_COUNT:
        /* no protected session types (NodeIndex is the input) */
        if (NegotiatedR2() && arg->DataSize >= sizeof(D3D12DDI_PROTECTED_RESOURCE_SESSION_TYPE_COUNT_DATA_0074)) {
            static_cast<D3D12DDI_PROTECTED_RESOURCE_SESSION_TYPE_COUNT_DATA_0074 *>(arg->pData)->Count = 0;
            hr = S_OK;
        }
        break;
    case D3D12DDICAPS_TYPE_EXECUTECOMMANDLISTS_PARALLELISM:
    case D3D12DDICAPS_TYPE_0073_SUPPORT_BATCHED_MARKERS:
        if (NegotiatedR2())
            hr = Answer<BOOL>(arg, [](auto &d) { d = FALSE; });
        break;
    /* C52: 0050+ runtimes may ask for scheduling groups; 0 = none */
    case D3D12DDICAPS_TYPE_0050_HARDWARE_SCHEDULING_CAPS:
        hr = Answer<D3D12DDICAPS_HARDWARE_SCHEDULING_CAPS_0050>(arg, [](auto &d) { d.ComputeQueuesPer3DQueue = 0; });
        break;
    case D3D12DDICAPS_TYPE_MEMORY_ARCHITECTURE:
        /* EXP1821: the x86/x64 D3D12Core refuses IOCoherent FALSE on UMA
         * (CDevice::CacheHeapCaps: "UMA systems must set TRUE", 0x887A0020).
         * AGX accesses to system memory are coherent with the CPU caches
         * (Asahi maps DRM_ASAHI_GEM_WRITEBACK BOs CPU-cached and Mesa does no
         * cache maintenance for them), and this driver maps every CPU-visible
         * allocation write-combined, so the runtime never needs to flush.
         * CacheCoherent stays FALSE: CPU mappings are not write-back. */
        hr = Answer<D3D12DDI_MEMORY_ARCHITECTURE_CAPS>(arg, [](auto &d) {
            d.UMA = TRUE; d.IOCoherent = TRUE; d.CacheCoherent = FALSE;
        });
        break;
    case D3D12DDICAPS_TYPE_SHADER:
        /* C49: typed UAV loads of the TypedUavLoadFormat set (CheckFormatSupport
         * reports UAV_READS for them) with the native owner */
        /* C48: under 0012 the wave caps: AGX runs 32-wide SIMD groups (Asahi
         * subgroup size 32), every SM 6.0 wave/quad intrinsic lowers to AGX
         * subgroup operations (test_dxil_to_nir); 128 lanes per core (Apple
         * M1 GPU: 128 ALUs per core) times the present cores (EXP1317:
         * CoreMask 0xfe, seven) */
        if (ShaderModel6Enabled() && arg->DataSize >= sizeof(D3D12DDI_SHADER_CAPS_0012)) {
            hr = Answer<D3D12DDI_SHADER_CAPS_0012>(arg, [](auto &d) {
                d.MinPrecision = D3D12DDI_SHADER_MIN_PRECISION_NONE;
                d.TypedUAVLoadAdditionalFormats = kTypedUavLoads;
                d.ROVs = RovEnabled(); /* EXP1889: SM 6 processes take this answer */
                d.WaveOps = TRUE;
                d.WaveLaneCountMin = d.WaveLaneCountMax = 32;
                const AGX_WIN32_DEVICE_INFO &info = adapter_object.info;
                UINT cores = info.CoreMask ? 0u : info.CoresPerCluster;
                for (UINT mask = info.CoreMask; mask; mask &= mask - 1u) ++cores;
                d.TotalLaneCount = adapter_object.info_valid ? 128u * cores : 0u;
            });
            break;
        }
        hr = Answer<D3D12DDI_SHADER_CAPS_0003>(arg, [](auto &d) {
            d.MinPrecision = D3D12DDI_SHADER_MIN_PRECISION_NONE;
            d.Doubles = FALSE; d.ShaderSpecifiedStencilRef = FALSE;
            d.TypedUAVLoadAdditionalFormats = kTypedUavLoads; d.ROVs = RovEnabled();
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
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
        hr=VolumeLayoutCaps(arg);
#else
        hr=Answer<D3D12DDI_TEXTURE_LAYOUT_CAPS>(arg, [](auto &) {});
#endif
        break;
    case D3D12DDICAPS_TYPE_SWIZZLE_PATTERN:
        hr=VolumeLayoutCaps(arg);break;
    /* EXP1870: the R2+ forms of the two cases above */
    case D3D12DDICAPS_TYPE_0022_TEXTURE_LAYOUT:
        if (!NegotiatedR2()) break;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
        hr=VolumeLayoutCaps(arg);
#else
        hr=arg->pInfo ? E_INVALIDARG : Answer<D3D12DDI_TEXTURE_LAYOUT_CAPS_0026>(arg, [](auto &) {});
#endif
        break;
    case D3D12DDICAPS_TYPE_0022_SWIZZLE_PATTERN:
        if (NegotiatedR2()) hr=VolumeLayoutCaps(arg);
        break;
    case D3D12DDICAPS_TYPE_TEXTURE_LAYOUT1:
        /* <=2patterns: stay in the documented legacy mode. */
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
    /* C4b: the native Asahi owner (agx_d3d12_windows.cpp), admitted during
     * CreateDevice; heaps are its buffer resources and lists copy through it. */
    void *volatile owner;
    volatile LONG owner_failed;
    volatile LONG owner_initializing;
    /* EXP1365: the KMD binds one Win32 generation per kernel device, so the
     * owner's context and every queue context carry this one */
    ULONG generation;
    AgxCpuProvider *shader_provider;
    AgxCpuDevice shader_provider_device;
    AgxD3d12Device *identity_next;
    /* C5: the owner heaps by GPU VA, so a root descriptor (a VA) resolves
     * to its buffer and offset */
    SRWLOCK heap_lock;
    UINT heap_count;
    struct AgxD3d12Heap *heaps[4096];
    /* EXP1855: the shader-visible CBV/SRV/UAV descriptor heaps whose
     * shadows a CPU descriptor handle may point into (heap_lock) */
    UINT mirrored_count;
    struct AgxD3d12Heap *mirrored[64];
    /* EXP1868: the kernel callbacks the owner uses, with the allocation
     * entries an R2+ runtime leaves empty bridged to its core-layer
     * pfnAllocateCb/pfnDeallocateCb (KernelAllocationBridge) */
    D3DDDI_DEVICECALLBACKS kernel_bridge;
};
struct AgxD3d12PipelineState;
static SRWLOCK identity_registry_lock = SRWLOCK_INIT;
static AgxD3d12Device *live_identity_devices = nullptr;
static AgxD3d12PipelineState *live_identity_psos = nullptr;

/* Keep this lock around address-only metadata. No runtime or native callback
 * executes under it. Runtime-private storage remains owned by the runtime. */
static void PublishDeviceIdentity(AgxD3d12Device *device) {
    AcquireSRWLockExclusive(&identity_registry_lock);
    for (auto *current = live_identity_devices; current; current = current->identity_next)
        if (current == device) {
            ReleaseSRWLockExclusive(&identity_registry_lock);
            return;
        }
    device->identity_next = live_identity_devices;
    live_identity_devices = device;
    ReleaseSRWLockExclusive(&identity_registry_lock);
}
static void RetireDeviceIdentity(AgxD3d12Device *device) {
    AcquireSRWLockExclusive(&identity_registry_lock);
    auto **entry = &live_identity_devices;
    while (*entry && *entry != device) entry = &(*entry)->identity_next;
    if (*entry) {
        *entry = device->identity_next;
        device->identity_next = nullptr;
    }
    ReleaseSRWLockExclusive(&identity_registry_lock);
}
/* EXP1868: under interface 0033 the owner refused the kernel callbacks
 * (AdmissionUmdRuntimeDeviceInitializeForQueue E_INVALIDARG before its
 * first paging queue). From 0022 the runtime offers allocation through
 * D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 (pfnAllocateCb/pfnDeallocateCb,
 * D3D12DDICB_ALLOCATE_0022); the bridge serves the owner's D3DDDICB_ALLOCATE
 * (D3DDDI_ALLOCATIONINFO) and D3DDDICB_DEALLOCATE calls through them. */
static const D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 *CoreLayer0022Of(HANDLE runtime) {
    const D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 *result = nullptr;
    AcquireSRWLockShared(&identity_registry_lock);
    for (auto *device = live_identity_devices; device; device = device->identity_next)
        if (device->runtime.handle == runtime) {
            result = reinterpret_cast<const D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 *>(device->callbacks);
            break;
        }
    ReleaseSRWLockShared(&identity_registry_lock);
    return result;
}
static HRESULT APIENTRY BridgeAllocateCb(HANDLE runtime, D3DDDICB_ALLOCATE *arg) {
    const auto *core = CoreLayer0022Of(runtime);
    if (!core || !core->pfnAllocateCb || !arg || (arg->NumAllocations && !arg->pAllocationInfo) ||
        arg->NumAllocations > 16)
        return E_INVALIDARG;
    D3D12DDI_ALLOCATION_INFO_0022 info[16] = {};
    for (UINT i = 0; i < arg->NumAllocations; ++i) {
        const D3DDDI_ALLOCATIONINFO &in = arg->pAllocationInfo[i];
        info[i].pSystemMem = in.pSystemMem;
        info[i].pPrivateDriverData = in.pPrivateDriverData;
        info[i].PrivateDriverDataSize = in.PrivateDriverDataSize;
        info[i].VidPnSourceId = in.VidPnSourceId;
        info[i].Flags = static_cast<D3D12DDI_ALLOCATION_INFO_FLAGS_0022>(
            (in.Flags.Primary ? D3D12DDI_ALLOCATION_INFO_FLAGS_0022_PRIMARY : 0) |
            (in.Flags.Stereo ? D3D12DDI_ALLOCATION_INFO_FLAGS_0022_STEREO : 0));
    }
    D3D12DDICB_ALLOCATE_0022 allocate = {};
    allocate.pPrivateDriverData = arg->pPrivateDriverData;
    allocate.PrivateDriverDataSize = arg->PrivateDriverDataSize;
    allocate.hResource = arg->hResource;
    allocate.NumAllocations = arg->NumAllocations;
    allocate.pAllocationInfo = info;
    D3D12DDI_HRTDEVICE device = {};
    device.handle = runtime;
    const HRESULT hr = core->pfnAllocateCb(device, &allocate);
    if (SUCCEEDED(hr)) {
        arg->hKMResource = allocate.hKMResource;
        for (UINT i = 0; i < arg->NumAllocations; ++i) arg->pAllocationInfo[i].hAllocation = info[i].hAllocation;
    }
    return hr;
}
static HRESULT APIENTRY BridgeDeallocateCb(HANDLE runtime, const D3DDDICB_DEALLOCATE *arg) {
    const auto *core = CoreLayer0022Of(runtime);
    if (!core || !core->pfnDeallocateCb || !arg) return E_INVALIDARG;
    D3D12DDICB_DEALLOCATE_0022 deallocate = {};
    deallocate.hResource = arg->hResource;
    deallocate.NumAllocations = arg->NumAllocations;
    deallocate.HandleList = arg->HandleList;
    D3D12DDI_HRTDEVICE device = {};
    device.handle = runtime;
    return core->pfnDeallocateCb(device, &deallocate);
}
static_assert(std::is_same_v<decltype(&BridgeAllocateCb), PFND3DDDI_ALLOCATECB>);
static_assert(std::is_same_v<decltype(&BridgeDeallocateCb), PFND3DDDI_DEALLOCATECB>);
/* the owner's kernel callback set (AdmissionUmdRuntimeDeviceInitializeForQueue):
 * bit i set = entry i absent, receipted at CreateDevice */
static UINT KernelCallbackGaps(const D3DDDI_DEVICECALLBACKS *k) {
    if (!k) return ~0u;
    const void *entries[] = {
        reinterpret_cast<const void *>(k->pfnAllocateCb), reinterpret_cast<const void *>(k->pfnDeallocateCb),
        reinterpret_cast<const void *>(k->pfnLockCb), reinterpret_cast<const void *>(k->pfnUnlockCb),
        reinterpret_cast<const void *>(k->pfnSignalSynchronizationObject2Cb),
        reinterpret_cast<const void *>(k->pfnMakeResidentCb), reinterpret_cast<const void *>(k->pfnEvictCb),
        reinterpret_cast<const void *>(k->pfnWaitForSynchronizationObjectFromCpuCb),
        reinterpret_cast<const void *>(k->pfnCreatePagingQueueCb), reinterpret_cast<const void *>(k->pfnDestroyPagingQueueCb),
        reinterpret_cast<const void *>(k->pfnCreateSynchronizationObject2Cb),
        reinterpret_cast<const void *>(k->pfnDestroySynchronizationObjectCb),
        reinterpret_cast<const void *>(k->pfnReserveGpuVirtualAddressCb),
        reinterpret_cast<const void *>(k->pfnMapGpuVirtualAddressCb),
        reinterpret_cast<const void *>(k->pfnFreeGpuVirtualAddressCb), reinterpret_cast<const void *>(k->pfnSubmitCommandCb),
        reinterpret_cast<const void *>(k->pfnSignalSynchronizationObjectFromGpu2Cb),
        reinterpret_cast<const void *>(k->pfnCreateContextVirtualCb), reinterpret_cast<const void *>(k->pfnDestroyContextCb)};
    UINT gaps = 0;
    for (UINT i = 0; i < ARRAYSIZE(entries); ++i)
        if (!entries[i]) gaps |= 1u << i;
    return gaps;
}
static const D3DDDI_DEVICECALLBACKS *AdmitKernelCallbacks(AgxD3d12Device *device, const D3D12DDIARG_CREATEDEVICE_0003 *arg) {
    const UINT gaps = KernelCallbackGaps(arg->pKTCallbacks);
    const auto *core = reinterpret_cast<const D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 *>(arg->p12UMCallbacks);
    const bool r2 = adapter_object.negotiated >= D3D12DDI_SUPPORTED_0022 && core;
    const UINT receipt[] = {gaps, r2 ? 1u : 0u, r2 && core->pfnAllocateCb ? 1u : 0u, r2 && core->pfnDeallocateCb ? 1u : 0u};
    AdmissionUmdDiagnostic("d3d12-kernel-callbacks", S_OK, receipt, 4);
    if (!arg->pKTCallbacks || !(gaps & 3u) || !r2 || !core->pfnAllocateCb || !core->pfnDeallocateCb)
        return arg->pKTCallbacks;
    device->kernel_bridge = *arg->pKTCallbacks;
    if (!device->kernel_bridge.pfnAllocateCb) device->kernel_bridge.pfnAllocateCb = BridgeAllocateCb;
    if (!device->kernel_bridge.pfnDeallocateCb) device->kernel_bridge.pfnDeallocateCb = BridgeDeallocateCb;
    return &device->kernel_bridge;
}
extern "C" const void *AgxD3d12ZeroCacheResolveDevice(D3D12DDI_HDEVICE handle) {
    const void *result = nullptr;
    AcquireSRWLockShared(&identity_registry_lock);
    for (auto *device = live_identity_devices; device; device = device->identity_next)
        if (device == handle.pDrvPrivate) {
            result = device;
            break;
        }
    ReleaseSRWLockShared(&identity_registry_lock);
    return result;
}
static void ReportRejectedDeviceError(D3D12DDI_HDEVICE handle, HRESULT error) {
    auto *device = static_cast<AgxD3d12Device *>(handle.pDrvPrivate);
    if (device && device->callbacks && device->callbacks->pfnSetErrorCb)
        device->callbacks->pfnSetErrorCb(device->runtime, error);
}
static SIZE_T APIENTRY CalcPrivateDeviceSize(D3D12DDI_HADAPTER,
                                             const D3D12DDIARG_CALCPRIVATEDEVICESIZE *arg) {
    const UINT receipt[] = {arg ? arg->Interface : 0, arg ? arg->Version : 0};
    AdmissionUmdDiagnostic("d3d12-device-size", S_OK, receipt, 2);
    if (arg) adapter_object.negotiated = (static_cast<UINT64>(arg->Interface) << 32) | arg->Version;
    return sizeof(AgxD3d12Device);
}
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static AGX_D3D12_OWNER *EnsureOwner(AgxD3d12Device *device);
#endif
static HRESULT APIENTRY CreateDevice(D3D12DDI_HADAPTER,
                                     const D3D12DDIARG_CREATEDEVICE_0003 *arg) {
    HRESULT hr = arg && arg->hDrvDevice.pDrvPrivate ? S_OK : E_INVALIDARG;
    if (SUCCEEDED(hr)) {
        auto *device = static_cast<AgxD3d12Device *>(arg->hDrvDevice.pDrvPrivate);
        /* Runtime allocation may contain reused bytes; all lazy state is ours. */
        memset(device, 0, sizeof(*device));
        InitializeSRWLock(&device->heap_lock);
        device->runtime = arg->hRTDevice;
        device->callbacks = arg->p12UMCallbacks;
        device->kernel = AdmitKernelCallbacks(device, arg);
        device->generation = ((((GetCurrentProcessId() & 0xffffu) << 16) ^
                               static_cast<ULONG>(InterlockedIncrement(&queue_generation))) | 0x80000000u);
        device->shader_provider = AgxCpuProviderCreate();
        if (!device->shader_provider) {
            hr = E_OUTOFMEMORY;
        } else {
            const auto status = AgxCpuRegisterDevice(device->shader_provider,
                arg->hDrvDevice, nullptr, nullptr, &device->shader_provider_device);
            if (status != AgxCpuProviderStatus::Ok) {
                (void)AgxCpuProviderDestroy(&device->shader_provider);
                hr = status == AgxCpuProviderStatus::ProjectOOM ||
                     status == AgxCpuProviderStatus::Exhausted ? E_OUTOFMEMORY : E_INVALIDARG;
            } else {
                PublishDeviceIdentity(device);
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
                /* Complete the one-time native owner construction before the
                 * runtime can issue free-threaded resource creates. */
                AGX_D3D12_OWNER *owner = EnsureOwner(device);
                /* C53 HW1: off-by-default reserved-buffer self-test; receipts
                 * only, the caps are unchanged (D3D12-C53-tiled-buffers-plan.md) */
                char selftest[2] = {};
                if (owner && GetEnvironmentVariableA("APPLE_AGX_D3D12_RESERVED_SELFTEST", selftest,
                                                     sizeof(selftest)) == 1 && selftest[0] == '1')
                    (void)AgxD3d12OwnerReservedSelfTest(owner);
#endif
            }
        }
    }
    const UINT receipt[] = {arg ? arg->Interface : 0, arg ? arg->Version : 0,
                            arg ? static_cast<UINT>(arg->Flags) : 0};
    AdmissionUmdDiagnostic("d3d12-create-device", hr, receipt, 3);
    return hr;
}
static void APIENTRY DestroyDevice(D3D12DDI_HDEVICE handle) {
    AdmissionUmdDiagnostic("d3d12-destroy-device", S_OK, nullptr, 0);
    auto *device = static_cast<AgxD3d12Device *>(handle.pDrvPrivate);
    RetireDeviceIdentity(device);
    if (device && device->shader_provider) {
        if (device->shader_provider_device.value)
            (void)AgxCpuRetireDevice(device->shader_provider, device->shader_provider_device);
        (void)AgxCpuProviderDestroy(&device->shader_provider);
    }
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if (device && device->owner) {
        (void)AgxD3d12OwnerDestroy(static_cast<AGX_D3D12_OWNER *>(device->owner));
        device->owner = nullptr;
    }
#else
    (void)handle;
#endif
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
 * implements (D3D12DDI_FORMAT_SUPPORT_* are the optional bits) and one node.
 *
 * C32 (EXP1827: 2x/4x levels reported with no format-support bit threw in
 * D3D12Core!CDevice::LLOCompleteLayerConstruction): for each typeless
 * format the runtime walks its cast set; a member without
 * D3D12DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET must report 0 levels at
 * every count above 1, a member with it the typeless format's levels, and
 * the bit must agree across the set (the typeless format itself must not
 * report it when D3D12 decides its multisample support). EXP1828: a format
 * D3D12 never multisample-renders (CD3D11FormatHelper::
 * MultisampleRenderTargetSupport -1: the typeless ones and the depth view
 * formats R24_UNORM_X8_TYPELESS, X24_TYPELESS_G8_UINT,
 * R32_FLOAT_X8X24_TYPELESS, X32_TYPELESS_G8X24_UINT) must not report the
 * bit either, so those view formats report nothing. So multisampling
 * is answered per cast set: the sets below (the AGX's 1x1-block colour and
 * depth formats up to 64 bits per sample; not R10G10B10A2, whose set may
 * hold the XR_BIAS display format) report 2x and 4x everywhere,
 * their typed members the multisample bits, everything else nothing. */
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static bool MultisampleCastSet(DXGI_FORMAT format, bool *typeless) {
    *typeless = false;
    switch (format) {
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R32G32_TYPELESS: case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        *typeless = true;
        return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM: case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_FLOAT: case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM: case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R16G16_UNORM: case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM: case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R8G8_UINT: case DXGI_FORMAT_R8G8_SNORM: case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM: case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_UINT: case DXGI_FORMAT_R8_SNORM: case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return true;
    default:
        return false;
    }
}
#endif
/* C49: typed UAV loads beyond R32 (TypedUAVLoadAdditionalFormats): the set
 * D3D12 requires together with the cap (RGBA32/16/8, R16, R8 float/uint/
 * sint/unorm) and the optional formats Asahi takes as shader images
 * (agx_is_format_supported: every valid pixel format but emulated RGB32 and
 * alpha/luminance). Typed UAVs carry no format in the shader (the DXBC and
 * DXIL frontends emit PIPE_FORMAT_NONE), Asahi loads them by the view's
 * format (image_load_formatted). */
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static bool TypedUavLoadFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UINT: case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_UINT: case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_UINT: case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_SINT:
    /* optional */
    case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R32G32_FLOAT: case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_UINT: case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R16G16_UNORM: case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM: case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R8G8_UINT: case DXGI_FORMAT_R8G8_SNORM: case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_SNORM: case DXGI_FORMAT_R8_SNORM:
        return true;
    default:
        return false;
    }
}
#endif
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
/* C53: formats a reserved Texture2D takes (agx_win32_sparse: elements of 1,
 * 2, 4, 8 or 16 bytes, 1x1 texels or BC 4x4 blocks), except the depth
 * family: reserved depth/stencil resources are refused. */
static bool TiledFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM:
        return false;
    default:
        break;
    }
    unsigned bw = 0, bh = 0;
    const unsigned block = AgxD3d12FormatBlock(format, &bw, &bh);
    return block == 1 || block == 2 || block == 4 || block == 8 || block == 16;
}
#endif
static VOID APIENTRY CheckFormatSupport(D3D12DDI_HDEVICE, DXGI_FORMAT format, UINT *support) {
    if (!support) return;
    *support = 0;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    bool typeless = false;
    if (MultisampleCastSet(format, &typeless) && !typeless)
        *support = D3D12DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET | D3D12DDI_FORMAT_SUPPORT_MULTISAMPLE_LOAD;
    if (TypedUavLoadFormat(format)) *support |= D3D12DDI_FORMAT_SUPPORT_UAV_READS;
    if (TiledEnabled() && TiledFormat(format)) *support |= D3D12DDI_FORMAT_SUPPORT_TILED; /* C53 */
#else
    (void)format;
#endif
}
static VOID APIENTRY CheckMultisampleQualityLevels(D3D12DDI_HDEVICE, DXGI_FORMAT format, UINT samples,
                                                   D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAGS flags,
                                                   UINT *levels) {
    if (!levels) return;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    /* C32: one quality level of 2x and 4x for every member of a
     * multisampled cast set; no tiled multisampled resources */
    bool typeless = false;
    *levels = samples == 1 ? 1u
            : !flags && (samples == 2 || samples == 4) && MultisampleCastSet(format, &typeless) ? 1u : 0u;
#else
    (void)format;
    (void)flags;
    *levels = samples == 1 ? 1u : 0u;
#endif
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
    AgxCpuRoot provider_root;
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
#include "agx_d3d12_r1_root_canonical.h"
#include "agx_d3d12_r1_root_layout.h"
static UINT RootParameterDwords(const D3D12DDI_ROOT_PARAMETER &p) {
    switch (p.ParameterType) {
    case D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: return 1;
    case D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: return p.Constants.Num32BitValues;
    default: return 2; /* root CBV/SRV/UAV: a 64-bit GPU virtual address */
    }
}
static constexpr AgxR1RootLayoutPolicy kCurrentRootLayoutPolicy = {64u, UINT_MAX, 2032u, 64u};
static SIZE_T APIENTRY CalcPrivateRootSignatureSize(D3D12DDI_HDEVICE,
                                                    const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 *arg) {
    SIZE_T bytes = 0;
    UINT ranges = 0;
    return arg && AgxD3d12MeasureR1RootPrivate(arg->pRootSignature, kCurrentRootLayoutPolicy,
                                                 &bytes, &ranges) ? bytes : 0;
}
static HRESULT APIENTRY CreateRootSignature(D3D12DDI_HDEVICE device_handle, const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 *arg,
                                            D3D12DDI_HROOTSIGNATURE handle) {
    auto *root = static_cast<AgxD3d12RootSignature *>(handle.pDrvPrivate);
    auto *device = static_cast<AgxD3d12Device *>(device_handle.pDrvPrivate);
    SIZE_T bytes = 0;
    UINT measured_ranges = 0;
    if (!root || !device || !device->shader_provider ||
        !device->shader_provider_device.value || !arg ||
        !AgxD3d12MeasureR1RootPrivate(arg->pRootSignature, kCurrentRootLayoutPolicy,
                                      &bytes, &measured_ranges))
        return E_INVALIDARG;
    /* Calc reserved the same checked extent; Create receives no capacity. */
    (void)bytes;
    (void)measured_ranges;
    root->provider_root = {};
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
    SIZE_T canonical_bytes = 0;
    if (!AgxD3d12MeasureRootCanonical(root, bytes, &canonical_bytes))
        return E_INVALIDARG;
    auto *canonical = static_cast<unsigned char *>(std::malloc(canonical_bytes));
    if (!canonical) return E_OUTOFMEMORY;
    SIZE_T written = 0;
    const bool encoded = AgxD3d12EncodeRootCanonical(root, bytes, canonical,
                                                       canonical_bytes, &written);
    AgxCpuProviderStatus registered = AgxCpuProviderStatus::Invalid;
    if (encoded && written == canonical_bytes)
        registered = AgxCpuRegisterRoot(device->shader_provider,
            device->shader_provider_device, handle, {canonical, written}, &root->provider_root);
    std::free(canonical);
    if (registered != AgxCpuProviderStatus::Ok)
        return registered == AgxCpuProviderStatus::ProjectOOM ||
               registered == AgxCpuProviderStatus::Exhausted ? E_OUTOFMEMORY : E_INVALIDARG;
    const UINT receipt[] = {root->parameters, root->static_samplers, root->flags, root->dwords};
    AdmissionUmdDiagnostic("measure-d3d12-root-signature", S_OK, receipt, 4);
    return S_OK;
}
static VOID APIENTRY DestroyRootSignature(D3D12DDI_HDEVICE device_handle,
                                          D3D12DDI_HROOTSIGNATURE handle) {
    auto *device = static_cast<AgxD3d12Device *>(device_handle.pDrvPrivate);
    auto *root = static_cast<AgxD3d12RootSignature *>(handle.pDrvPrivate);
    if (device && device->shader_provider && root && root->provider_root.value) {
        (void)AgxCpuRetireRoot(device->shader_provider, device->shader_provider_device,
                               root->provider_root);
        root->provider_root = {};
    }
}
/* C1g (EXP1351): device initialisation creates a shader after its (empty)
 * root signature; a refused pfnCalcPrivateShaderSize removes the device.
 * The shader object keeps a copy of the D3D10+ tokenized program
 * (pShaderCode[0] = program type and version, pShaderCode[1] = length in
 * DWORDs); compilation to AGX (DXBC -> NIR -> libagx) happens when a
 * pipeline state is created (C5). */
/* C39 (D3D12-C39-stream-output.md): a stream-output declaration as
 * pfnCreateGeometryShaderWithStreamOutput passes it (entries over
 * kSoEntries or strides over 4 keep entries = UINT_MAX: refused at compile) */
static constexpr UINT kSoEntries = 64;
struct AgxD3d12StreamOutput {
    UINT declared, entries, rasterized, num_strides, strides[4];
    struct { UINT stream, slot, reg, mask; } entry[kSoEntries];
    UINT position_registers; /* output-signature registers holding SV_Position (DXIL capture) */
};
struct AgxD3d12Shader {
    UINT stage; /* the stage the runtime created it for (D3D10_SB program type) */
    UINT hs_registers, hs_positions; /* C37: a hull shader's pass-through registers */
    AgxD3d12StreamOutput so; /* C39 */
    UINT dwords;
    UINT tokens[1];
};
static SIZE_T APIENTRY CalcPrivateShaderSize(D3D12DDI_HDEVICE, const UINT *code, D3D12DDI_HROOTSIGNATURE,
                                             const D3D12DDIARG_STAGE_IO_SIGNATURES *) {
    const UINT dwords = code ? code[1] : 0;
    return offsetof(AgxD3d12Shader, tokens) + (dwords ? dwords : 1) * sizeof(UINT);
}
static void StoreShader(D3D12DDI_HDEVICE device, const UINT *code, D3D12DDI_HSHADER handle) {
    auto *shader = static_cast<AgxD3d12Shader *>(handle.pDrvPrivate);
    if (!code || !shader || code[1] < 2) {
        ReportRejectedDeviceError(device, E_INVALIDARG);
        return;
    }
    shader->stage = code[0] >> 16;
    shader->hs_registers = shader->hs_positions = 0;
    shader->so.declared = 0;
    shader->dwords = code[1];
    memcpy(shader->tokens, code, code[1] * sizeof(UINT));
    const UINT receipt[] = {shader->stage, code[0] & 0xffffu, shader->dwords};
    AdmissionUmdDiagnostic("measure-d3d12-shader", S_OK, receipt, 3);
}
static VOID APIENTRY CreateShader(D3D12DDI_HDEVICE device, const UINT *code, D3D12DDI_HROOTSIGNATURE,
                                  D3D12DDI_HSHADER handle, const D3D12DDIARG_STAGE_IO_SIGNATURES *,
                                  D3D12DDI_CREATE_SHADER_FLAGS) {
    StoreShader(device, code, handle);
}
static VOID APIENTRY CreateComputeShader(D3D12DDI_HDEVICE device, const UINT *code, D3D12DDI_HROOTSIGNATURE,
                                         D3D12DDI_HSHADER handle, D3D12DDI_CREATE_SHADER_FLAGS) {
    StoreShader(device, code, handle);
}
/* C37 (D3D12-C37-tessellation.md): hull and domain shaders keep their
 * program as the other stages do; a hull shader also keeps the registers of
 * its output (control point) signature, what a pass-through control point
 * phase copies (the D3D11 frontend's hs_passthrough), and which registers
 * of its input signature hold the vertex shader's SV_Position. */
static SIZE_T APIENTRY CalcPrivateTessellationShaderSize(D3D12DDI_HDEVICE device, const UINT *code,
                                                         D3D12DDI_HROOTSIGNATURE root,
                                                         const D3D12DDIARG_TESSELLATION_IO_SIGNATURES *) {
    return CalcPrivateShaderSize(device, code, root, nullptr);
}
static VOID APIENTRY CreateTessellationShader(D3D12DDI_HDEVICE device, const UINT *code, D3D12DDI_HROOTSIGNATURE,
                                              D3D12DDI_HSHADER handle, const D3D12DDIARG_TESSELLATION_IO_SIGNATURES *sig,
                                              D3D12DDI_CREATE_SHADER_FLAGS) {
    StoreShader(device, code, handle);
    auto *shader = static_cast<AgxD3d12Shader *>(handle.pDrvPrivate);
    if (!shader || !code || code[1] < 2 || shader->stage != D3D11_SB_HULL_SHADER || !sig) return;
    for (UINT i = 0; sig->pOutputSignature && i < sig->NumOutputSignatureEntries; ++i)
        if (sig->pOutputSignature[i].Register < 32) shader->hs_registers |= 1u << sig->pOutputSignature[i].Register;
    for (UINT i = 0; sig->pInputSignature && i < sig->NumInputSignatureEntries; ++i)
        if (sig->pInputSignature[i].Register < 32 && sig->pInputSignature[i].SystemValue == D3D10_SB_NAME_POSITION)
            shader->hs_positions |= 1u << sig->pInputSignature[i].Register;
}
/* C39: a geometry shader with stream output -- or, with the vertex
 * shader's program or none, the stream output of a pipeline without a
 * geometry shader -- keeps its program and its declaration */
static SIZE_T APIENTRY CalcPrivateGeometryShaderWithStreamOutput(
    D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT *arg,
    const D3D12DDIARG_STAGE_IO_SIGNATURES *) {
    return CalcPrivateShaderSize(device, arg ? arg->pShaderCode : nullptr, {}, nullptr);
}
static VOID APIENTRY CreateGeometryShaderWithStreamOutput(
    D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT *arg, D3D12DDI_HSHADER handle,
    const D3D12DDIARG_STAGE_IO_SIGNATURES *signatures, D3D12DDI_CREATE_SHADER_FLAGS) {
    auto *shader = static_cast<AgxD3d12Shader *>(handle.pDrvPrivate);
    if (!arg || !shader) {
        ReportRejectedDeviceError(device, E_INVALIDARG);
        return;
    }
    if (arg->pShaderCode) {
        StoreShader(device, arg->pShaderCode, handle);
    } else {
        shader->stage = UINT_MAX;
        shader->hs_registers = shader->hs_positions = 0;
        shader->dwords = 0;
    }
    AgxD3d12StreamOutput &so = shader->so;
    memset(&so, 0, sizeof(so));
    so.declared = 1;
    so.rasterized = arg->RasterizedStream;
    const bool fits = arg->NumEntries <= kSoEntries && arg->NumStrides <= 4 && (!arg->NumEntries || arg->pOutputStreamDecl) &&
                      (!arg->NumStrides || arg->BufferStridesInBytes);
    so.entries = fits ? arg->NumEntries : UINT_MAX;
    so.num_strides = fits ? arg->NumStrides : 0u;
    for (UINT i = 0; fits && i < arg->NumEntries; ++i)
        so.entry[i] = {arg->pOutputStreamDecl[i].Stream, arg->pOutputStreamDecl[i].OutputSlot,
                       arg->pOutputStreamDecl[i].RegisterIndex, arg->pOutputStreamDecl[i].RegisterMask};
    for (UINT i = 0; fits && i < arg->NumStrides; ++i) so.strides[i] = arg->BufferStridesInBytes[i];
    /* the R1 runtime hands the shader over as DXIL, whose SV_Position output
     * is a builtin rather than a signature row: the signature names it */
    for (UINT i = 0; signatures && signatures->pOutputSignature && i < signatures->NumOutputSignatureEntries; ++i)
        if (signatures->pOutputSignature[i].SystemValue == D3D10_SB_NAME_POSITION &&
            signatures->pOutputSignature[i].Register < 32)
            so.position_registers |= 1u << signatures->pOutputSignature[i].Register;
    const UINT receipt[] = {arg->pShaderCode ? shader->stage : UINT_MAX, arg->NumEntries, arg->NumStrides,
                            arg->RasterizedStream, fits ? 1u : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-so-shader", fits ? S_OK : E_NOTIMPL, receipt, 5);
}
/* C44 (R1, D3D12DDI_DEVICE_FUNCS_CORE_0010): every shader stage arrives as
 * D3D12DDIARG_CREATE_SHADER_0010 (code, root signature, I/O signatures,
 * flags, pipeline-library reference); the 0003 entries keep the work. A
 * library reference names the zero-cache library, which holds nothing. */
static bool R1ShaderReference(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_SHADER_0010 *arg) {
    if (arg && SUCCEEDED(AgxD3d12ZeroCacheValidateLibraryReference(device, &arg->LibraryReference))) return true;
    ReportRejectedDeviceError(device, E_INVALIDARG);
    return false;
}
static SIZE_T APIENTRY CalcPrivateShaderSize0010(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_SHADER_0010 *arg) {
    return CalcPrivateShaderSize(device, arg ? arg->pShaderCode : nullptr, {}, nullptr);
}
static VOID APIENTRY CreateShader0010(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_SHADER_0010 *arg,
                                      D3D12DDI_HSHADER handle) {
    if (R1ShaderReference(device, arg))
        CreateShader(device, arg->pShaderCode, arg->hRootSignature, handle, arg->IOSignatures.Standard, arg->Flags);
}
static VOID APIENTRY CreateComputeShader0010(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_SHADER_0010 *arg,
                                             D3D12DDI_HSHADER handle) {
    if (R1ShaderReference(device, arg)) CreateComputeShader(device, arg->pShaderCode, arg->hRootSignature, handle, arg->Flags);
}
static VOID APIENTRY CreateTessellationShader0010(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_SHADER_0010 *arg,
                                                  D3D12DDI_HSHADER handle) {
    if (R1ShaderReference(device, arg))
        CreateTessellationShader(device, arg->pShaderCode, arg->hRootSignature, handle, arg->IOSignatures.Tessellation,
                                 arg->Flags);
}
static D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT ProjectR1StreamOutput(
    const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010 &arg) {
    D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT legacy = {};
    legacy.pShaderCode = arg.CreateShader.pShaderCode;
    legacy.hRootSignature = arg.CreateShader.hRootSignature;
    legacy.pOutputStreamDecl = arg.pOutputStreamDecl;
    legacy.NumEntries = arg.NumEntries;
    legacy.BufferStridesInBytes = arg.BufferStridesInBytes;
    legacy.NumStrides = arg.NumStrides;
    legacy.RasterizedStream = arg.RasterizedStream;
    return legacy;
}
static SIZE_T APIENTRY CalcPrivateGeometryShaderWithStreamOutput0010(
    D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010 *arg) {
    if (!arg) return CalcPrivateShaderSize(device, nullptr, {}, nullptr);
    const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT legacy = ProjectR1StreamOutput(*arg);
    return CalcPrivateGeometryShaderWithStreamOutput(device, &legacy, nullptr);
}
static VOID APIENTRY CreateGeometryShaderWithStreamOutput0010(
    D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010 *arg, D3D12DDI_HSHADER handle) {
    if (!arg || !R1ShaderReference(device, &arg->CreateShader)) return;
    const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT legacy = ProjectR1StreamOutput(*arg);
    CreateGeometryShaderWithStreamOutput(device, &legacy, handle, arg->CreateShader.IOSignatures.Standard,
                                         arg->CreateShader.Flags);
}
static_assert(std::is_same_v<decltype(&CalcPrivateShaderSize0010), PFND3D12DDI_CALC_PRIVATE_SHADER_SIZE_0010>);
static_assert(std::is_same_v<decltype(&CreateShader0010), PFND3D12DDI_CREATE_SHADER_0010>);
static_assert(std::is_same_v<decltype(&CalcPrivateGeometryShaderWithStreamOutput0010),
                             PFND3D12DDI_CALC_PRIVATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010>);
static_assert(std::is_same_v<decltype(&CreateGeometryShaderWithStreamOutput0010),
                             PFND3D12DDI_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010>);
static VOID APIENTRY DestroyShader(D3D12DDI_HDEVICE, D3D12DDI_HSHADER) {}
/* C1h (EXP1352): after its internal vertex shader (VS 4.0, 15 DWORDs) the
 * runtime builds the state objects a graphics pipeline state references
 * (D3D12DDIARG_CREATE_PIPELINE_STATE_0001: hBlendState, hRasterizerState,
 * hDepthStencilState, hElementLayout); a refused pfnCalcPrivateBlendStateSize
 * removes the device. Each object keeps a copy of its description; the
 * pipeline state translates them to AGX state (C6). */
template<class Desc, UINT Kind> struct AgxD3d12State {
    static SIZE_T APIENTRY size(D3D12DDI_HDEVICE, const Desc *) { return sizeof(Desc); }
    template<class Handle> static VOID APIENTRY create(D3D12DDI_HDEVICE device, const Desc *desc, Handle handle) {
        if (!desc || !handle.pDrvPrivate) {
            ReportRejectedDeviceError(device, E_INVALIDARG);
            return;
        }
        memcpy(handle.pDrvPrivate, desc, sizeof(Desc));
        const UINT receipt[] = {Kind, static_cast<UINT>(sizeof(Desc))};
        AdmissionUmdDiagnostic("measure-d3d12-state", S_OK, receipt, 2);
    }
    template<class Handle> static VOID APIENTRY destroy(D3D12DDI_HDEVICE, Handle) {}
};
using BlendState = AgxD3d12State<D3D12DDI_BLEND_DESC, 1>;
/* CPU-only R1 blend adapter. The 0010 table is not advertised yet. */
static SIZE_T APIENTRY CalcPrivateBlendStateSize0010(D3D12DDI_HDEVICE device,
                                                     const D3D12DDI_BLEND_DESC_0010 *desc) {
    if (!desc || FAILED(AgxD3d12ZeroCacheValidateLibraryReference(device, &desc->LibraryReference)))
        return 0;
    return sizeof(D3D12DDI_BLEND_DESC);
}
static VOID APIENTRY CreateBlendState0010(D3D12DDI_HDEVICE device,
                                          const D3D12DDI_BLEND_DESC_0010 *desc,
                                          D3D12DDI_HBLENDSTATE handle) {
    if (!desc || !handle.pDrvPrivate) {
        ReportRejectedDeviceError(device, E_INVALIDARG);
        return;
    }
    const HRESULT reference = AgxD3d12ZeroCacheValidateLibraryReference(device, &desc->LibraryReference);
    if (FAILED(reference)) {
        ReportRejectedDeviceError(device, reference);
        return;
    }
    D3D12DDI_BLEND_DESC projected;
    if (!AgxD3d12ProjectR1Blend(desc, &projected)) {
        ReportRejectedDeviceError(device, E_INVALIDARG);
        return;
    }
    BlendState::create(device, &projected, handle);
}
static_assert(std::is_same_v<decltype(&CalcPrivateBlendStateSize0010),
                             PFND3D12DDI_CALCPRIVATEBLENDSTATESIZE_0010>);
static_assert(std::is_same_v<decltype(&CreateBlendState0010),
                             PFND3D12DDI_CREATEBLENDSTATE_0010>);
using DepthStencilState = AgxD3d12State<D3D12DDI_DEPTH_STENCIL_DESC, 2>;
using RasterizerState = AgxD3d12State<D3D12DDI_RASTERIZER_DESC, 3>;
/* CPU-only independent R1 state projections; CORE_0010 is not advertised. */
static D3D12DDI_DEPTH_STENCIL_DESC ProjectR1Depth(const D3D12DDI_DEPTH_STENCIL_DESC_0010 *input) {
    D3D12DDI_DEPTH_STENCIL_DESC state;
    std::memset(&state, 0, sizeof(state));
    state.DepthEnable = input->DepthEnable;
    state.DepthWriteMask = input->DepthWriteMask;
    state.DepthFunc = input->DepthFunc;
    state.StencilEnable = input->StencilEnable;
    state.FrontEnable = input->FrontEnable;
    state.BackEnable = input->BackEnable;
    state.StencilReadMask = input->StencilReadMask;
    state.StencilWriteMask = input->StencilWriteMask;
    state.FrontFace.StencilFailOp = input->FrontFace.StencilFailOp;
    state.FrontFace.StencilDepthFailOp = input->FrontFace.StencilDepthFailOp;
    state.FrontFace.StencilPassOp = input->FrontFace.StencilPassOp;
    state.FrontFace.StencilFunc = input->FrontFace.StencilFunc;
    state.BackFace.StencilFailOp = input->BackFace.StencilFailOp;
    state.BackFace.StencilDepthFailOp = input->BackFace.StencilDepthFailOp;
    state.BackFace.StencilPassOp = input->BackFace.StencilPassOp;
    state.BackFace.StencilFunc = input->BackFace.StencilFunc;
    return state;
}
static SIZE_T APIENTRY CalcPrivateDepthStencilStateSize0010(D3D12DDI_HDEVICE device,
    const D3D12DDI_DEPTH_STENCIL_DESC_0010 *desc) {
    if (!desc || FAILED(AgxD3d12ZeroCacheValidateLibraryReference(device, &desc->LibraryReference)))
        return 0;
    return sizeof(D3D12DDI_DEPTH_STENCIL_DESC);
}
static VOID APIENTRY CreateDepthStencilState0010(D3D12DDI_HDEVICE device,
    const D3D12DDI_DEPTH_STENCIL_DESC_0010 *desc, D3D12DDI_HDEPTHSTENCILSTATE handle) {
    if (!desc || !handle.pDrvPrivate) {
        ReportRejectedDeviceError(device, E_INVALIDARG);
        return;
    }
    const HRESULT reference = AgxD3d12ZeroCacheValidateLibraryReference(device, &desc->LibraryReference);
    if (FAILED(reference)) {
        ReportRejectedDeviceError(device, reference);
        return;
    }
    const D3D12DDI_DEPTH_STENCIL_DESC state = ProjectR1Depth(desc);
    DepthStencilState::create(device, &state, handle);
}
static D3D12DDI_RASTERIZER_DESC ProjectR1Raster(const D3D12DDI_RASTERIZER_DESC_0010 *input) {
    D3D12DDI_RASTERIZER_DESC state;
    std::memset(&state, 0, sizeof(state));
    state.FillMode = input->FillMode;
    state.CullMode = input->CullMode;
    state.FrontCounterClockwise = input->FrontCounterClockwise;
    state.DepthBias = input->DepthBias;
    state.DepthBiasClamp = input->DepthBiasClamp;
    state.SlopeScaledDepthBias = input->SlopeScaledDepthBias;
    state.DepthClipEnable = input->DepthClipEnable;
    state.ScissorEnable = input->ScissorEnable;
    state.MultisampleEnable = input->MultisampleEnable;
    state.AntialiasedLineEnable = input->AntialiasedLineEnable;
    state.ForcedSampleCount = input->ForcedSampleCount;
    state.ConservativeRasterizationMode = input->ConservativeRasterizationMode;
    return state;
}
static SIZE_T APIENTRY CalcPrivateRasterizerStateSize0010(D3D12DDI_HDEVICE device,
    const D3D12DDI_RASTERIZER_DESC_0010 *desc) {
    if (!desc || FAILED(AgxD3d12ZeroCacheValidateLibraryReference(device, &desc->LibraryReference)))
        return 0;
    return sizeof(D3D12DDI_RASTERIZER_DESC);
}
static VOID APIENTRY CreateRasterizerState0010(D3D12DDI_HDEVICE device,
    const D3D12DDI_RASTERIZER_DESC_0010 *desc, D3D12DDI_HRASTERIZERSTATE handle) {
    if (!desc || !handle.pDrvPrivate) {
        ReportRejectedDeviceError(device, E_INVALIDARG);
        return;
    }
    const HRESULT reference = AgxD3d12ZeroCacheValidateLibraryReference(device, &desc->LibraryReference);
    if (FAILED(reference)) {
        ReportRejectedDeviceError(device, reference);
        return;
    }
    const D3D12DDI_RASTERIZER_DESC state = ProjectR1Raster(desc);
    RasterizerState::create(device, &state, handle);
}
static_assert(std::is_same_v<decltype(&CalcPrivateDepthStencilStateSize0010),
                             PFND3D12DDI_CALCPRIVATEDEPTHSTENCILSTATESIZE_0010>);
static_assert(std::is_same_v<decltype(&CreateDepthStencilState0010),
                             PFND3D12DDI_CREATEDEPTHSTENCILSTATE_0010>);
static_assert(std::is_same_v<decltype(&CalcPrivateRasterizerStateSize0010),
                             PFND3D12DDI_CALCPRIVATERASTERIZERSTATESIZE_0010>);
static_assert(std::is_same_v<decltype(&CreateRasterizerState0010),
                             PFND3D12DDI_CREATERASTERIZERSTATE_0010>);
struct AgxD3d12ElementLayout {
    UINT elements;
    D3D12DDIARG_INPUT_ELEMENT_DESC element[1];
};
static SIZE_T APIENTRY CalcPrivateElementLayoutSize(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATEELEMENTLAYOUT *arg) {
    const UINT count = arg ? arg->NumElements : 0;
    return offsetof(AgxD3d12ElementLayout, element) + (count ? count : 1) * sizeof(D3D12DDIARG_INPUT_ELEMENT_DESC);
}
static VOID APIENTRY CreateElementLayout(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATEELEMENTLAYOUT *arg,
                                         D3D12DDI_HELEMENTLAYOUT handle) {
    auto *layout = static_cast<AgxD3d12ElementLayout *>(handle.pDrvPrivate);
    if (!arg || !layout || (arg->NumElements && !arg->pVertexElements)) {
        ReportRejectedDeviceError(device, E_INVALIDARG);
        return;
    }
    layout->elements = arg->NumElements;
    if (arg->NumElements)
        memcpy(layout->element, arg->pVertexElements, arg->NumElements * sizeof(D3D12DDIARG_INPUT_ELEMENT_DESC));
    const UINT receipt[] = {4u, arg->NumElements};
    AdmissionUmdDiagnostic("measure-d3d12-state", S_OK, receipt, 2);
}
/* CPU-only R1 element-layout adapter. The 0010 table is not advertised yet. */
static SIZE_T APIENTRY CalcPrivateElementLayoutSize0010(D3D12DDI_HDEVICE device,
    const D3D12DDIARG_CREATEELEMENTLAYOUT_0010 *arg) {
    if (!arg || (arg->NumElements && !arg->pVertexElements) ||
        FAILED(AgxD3d12ZeroCacheValidateLibraryReference(device, &arg->LibraryReference)))
        return 0;
    const D3D12DDIARG_CREATEELEMENTLAYOUT legacy = {arg->pVertexElements, arg->NumElements};
    return CalcPrivateElementLayoutSize(device, &legacy);
}
static VOID APIENTRY CreateElementLayout0010(D3D12DDI_HDEVICE device,
    const D3D12DDIARG_CREATEELEMENTLAYOUT_0010 *arg, D3D12DDI_HELEMENTLAYOUT handle) {
    if (!arg || !handle.pDrvPrivate || (arg->NumElements && !arg->pVertexElements)) {
        ReportRejectedDeviceError(device, E_INVALIDARG);
        return;
    }
    const HRESULT reference = AgxD3d12ZeroCacheValidateLibraryReference(device, &arg->LibraryReference);
    if (FAILED(reference)) {
        ReportRejectedDeviceError(device, reference);
        return;
    }
    const D3D12DDIARG_CREATEELEMENTLAYOUT legacy = {arg->pVertexElements, arg->NumElements};
    CreateElementLayout(device, &legacy, handle);
}
static_assert(std::is_same_v<decltype(&CalcPrivateElementLayoutSize0010),
                             PFND3D12DDI_CALCPRIVATEELEMENTLAYOUTSIZE_0010>);
static_assert(std::is_same_v<decltype(&CreateElementLayout0010),
                             PFND3D12DDI_CREATEELEMENTLAYOUT_0010>);
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
    AgxD3d12Device *identity_owner;
    AgxD3d12PipelineState *identity_next;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    /* Different recording threads may first use the same PSO concurrently. */
    SRWLOCK compile_lock;
    AGX_D3D12_COMPUTE compute;
    AGX_D3D12_GRAPHICS graphics;
    /* C65V: the owner description of the graphics compile, and the min/max
     * variant a draw or dispatch uses while a MINIMUM/MAXIMUM heap sampler is
     * bound (C65S: or a MINIMUM/MAXIMUM slot sampler). Variants are
     * allocated on first use: an AGX_D3D12_GRAPHICS (five binding maps) is
     * ~28 KB, which every PSO would otherwise carry. */
    AGX_D3D12_GRAPHICS_DESC owner_desc;
    AGX_D3D12_SO_DECL owner_so;
    struct AgxD3d12MinmaxVariant *minmax; /* kMinmaxVariants entries */
    UINT minmax_count;
#endif
    HRESULT compiled; /* S_FALSE until the first compile */
    D3D12DDI_BLEND_DESC blend;
    D3D12DDI_RASTERIZER_DESC rasterizer;
    D3D12DDI_DEPTH_STENCIL_DESC depth_stencil;
    UINT elements;
    D3D12DDIARG_INPUT_ELEMENT_DESC element[kPipelineElements];
    UINT cs_dwords, vs_dwords, ps_dwords, gs_dwords;
    UINT hs_dwords, ds_dwords, hs_registers, hs_positions; /* C37 */
    AgxD3d12StreamOutput so; /* C39: declared = the pipeline captures stream output */
    UINT cs[1]; /* the CS program, then the VS, the PS, the GS (C27), the HS and the DS (C37) */
};
static void PublishPsoIdentity(AgxD3d12PipelineState *pso, AgxD3d12Device *device) {
    AcquireSRWLockExclusive(&identity_registry_lock);
    for (auto *current = live_identity_psos; current; current = current->identity_next)
        if (current == pso) {
            ReleaseSRWLockExclusive(&identity_registry_lock);
            return;
        }
    pso->identity_owner = device;
    pso->identity_next = live_identity_psos;
    live_identity_psos = pso;
    ReleaseSRWLockExclusive(&identity_registry_lock);
}
static void RetirePsoIdentity(AgxD3d12PipelineState *pso) {
    AcquireSRWLockExclusive(&identity_registry_lock);
    auto **entry = &live_identity_psos;
    while (*entry && *entry != pso) entry = &(*entry)->identity_next;
    if (*entry) {
        *entry = pso->identity_next;
        pso->identity_owner = nullptr;
        pso->identity_next = nullptr;
    }
    ReleaseSRWLockExclusive(&identity_registry_lock);
}
extern "C" const void *AgxD3d12ZeroCacheResolvePsoOwner(D3D12DDI_HPIPELINESTATE handle) {
    const void *result = nullptr;
    AcquireSRWLockShared(&identity_registry_lock);
    for (auto *pso = live_identity_psos; pso; pso = pso->identity_next)
        if (pso == handle.pDrvPrivate) {
            for (auto *device = live_identity_devices; device; device = device->identity_next)
                if (device == pso->identity_owner) {
                    result = device;
                    break;
                }
            break;
        }
    ReleaseSRWLockShared(&identity_registry_lock);
    return result;
}
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
    const AgxD3d12Shader *gs = arg ? ShaderOf(arg->hGeometryShader) : nullptr;
    const AgxD3d12Shader *hs = arg ? ShaderOf(arg->hHullShader) : nullptr;
    const AgxD3d12Shader *ds = arg ? ShaderOf(arg->hDomainShader) : nullptr;
    const SIZE_T dwords = (cs ? cs->dwords : 0) + (vs ? vs->dwords : 0) + (ps ? ps->dwords : 0) + (gs ? gs->dwords : 0) +
                          (hs ? hs->dwords : 0) + (ds ? ds->dwords : 0);
    return offsetof(AgxD3d12PipelineState, cs) + (dwords ? dwords : 1) * sizeof(UINT);
}
static HRESULT APIENTRY CreatePipelineState(D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATE_PIPELINE_STATE_0001 *arg) {
    auto *device = static_cast<AgxD3d12Device *>(handle.pDrvPrivate);
    auto *pso = arg ? static_cast<AgxD3d12PipelineState *>(arg->hDrvPipelineState.pDrvPrivate) : nullptr;
    if (!device || !pso) return E_INVALIDARG;
    memset(pso, 0, offsetof(AgxD3d12PipelineState, cs));
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    InitializeSRWLock(&pso->compile_lock);
#endif
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
    /* C27: a geometry shader; C39: with stream output, whose object holds
     * the vertex shader's program or none when the pipeline has no GS */
    if (const AgxD3d12Shader *gs = ShaderOf(arg->hGeometryShader)) {
        if (gs->so.declared) pso->so = gs->so;
        if (gs->dwords && gs->stage == D3D10_SB_GEOMETRY_SHADER) {
            pso->gs_dwords = gs->dwords;
            memcpy(pso->cs + pso->cs_dwords + pso->vs_dwords + pso->ps_dwords, gs->tokens, gs->dwords * sizeof(UINT));
        }
    }
    UINT *tess = pso->cs + pso->cs_dwords + pso->vs_dwords + pso->ps_dwords + pso->gs_dwords;
    if (const AgxD3d12Shader *hs = ShaderOf(arg->hHullShader)) { /* C37 */
        pso->hs_dwords = hs->dwords;
        pso->hs_registers = hs->hs_registers;
        pso->hs_positions = hs->hs_positions;
        memcpy(tess, hs->tokens, hs->dwords * sizeof(UINT));
    }
    if (const AgxD3d12Shader *ds = ShaderOf(arg->hDomainShader)) {
        pso->ds_dwords = ds->dwords;
        memcpy(tess + pso->hs_dwords, ds->tokens, ds->dwords * sizeof(UINT));
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
    PublishPsoIdentity(pso, device);
    return S_OK;
}
/* CPU-only R1 PSO adapter. The 0010 table is not advertised yet. */
static D3D12DDIARG_CREATE_PIPELINE_STATE_0001 ProjectR1PipelineState(
    const D3D12DDIARG_CREATE_PIPELINE_STATE_0010 *input, D3D12DDI_HPIPELINESTATE destination) {
    D3D12DDIARG_CREATE_PIPELINE_STATE_0001 result;
    std::memset(&result, 0, sizeof(result));
    result.hDrvPipelineState = destination;
    result.hComputeShader = input->hComputeShader;
    result.hVertexShader = input->hVertexShader;
    result.hPixelShader = input->hPixelShader;
    result.hDomainShader = input->hDomainShader;
    result.hHullShader = input->hHullShader;
    result.hGeometryShader = input->hGeometryShader;
    result.hRootSignature = input->hRootSignature;
    result.hBlendState = input->hBlendState;
    result.SampleMask = input->SampleMask;
    result.hRasterizerState = input->hRasterizerState;
    result.hDepthStencilState = input->hDepthStencilState;
    result.hElementLayout = input->hElementLayout;
    result.IBStripCutValue = input->IBStripCutValue;
    result.PrimitiveTopologyType = input->PrimitiveTopologyType;
    result.NumRenderTargets = input->NumRenderTargets;
    for (UINT i = 0; i < 8; ++i) result.RTVFormats[i] = input->RTVFormats[i];
    result.DSVFormat = input->DSVFormat;
    result.SampleDesc = input->SampleDesc;
    result.NodeMask = input->NodeMask;
    return result;
}
static SIZE_T APIENTRY CalcPrivatePipelineStateSize0010(D3D12DDI_HDEVICE device,
    const D3D12DDIARG_CREATE_PIPELINE_STATE_0010 *arg) {
    if (!arg || FAILED(AgxD3d12ZeroCacheValidateLibraryReference(device, &arg->LibraryReference)))
        return 0;
    const D3D12DDIARG_CREATE_PIPELINE_STATE_0001 legacy = ProjectR1PipelineState(arg, {});
    return CalcPrivatePipelineStateSize(device, &legacy);
}
static HRESULT APIENTRY CreatePipelineState0010(D3D12DDI_HDEVICE device,
    const D3D12DDIARG_CREATE_PIPELINE_STATE_0010 *arg, D3D12DDI_HPIPELINESTATE destination) {
    if (!arg || !destination.pDrvPrivate) return E_INVALIDARG;
    const HRESULT reference = AgxD3d12ZeroCacheValidateLibraryReference(device, &arg->LibraryReference);
    if (FAILED(reference)) return reference;
    const D3D12DDIARG_CREATE_PIPELINE_STATE_0001 legacy = ProjectR1PipelineState(arg, destination);
    return CreatePipelineState(device, &legacy);
}
static_assert(std::is_same_v<decltype(&CalcPrivatePipelineStateSize0010),
                             PFND3D12DDI_CALC_PRIVATE_PIPELINE_STATE_SIZE_0010>);
static_assert(std::is_same_v<decltype(&CreatePipelineState0010),
                             PFND3D12DDI_CREATE_PIPELINE_STATE_0010>);
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
    void *presentation; /* C7b: the owner presentation record of a shareable render target */
    BOOL volume;  /* C21: DEFAULT volume heaps are never CPU mapped */
    BOOL alias;   /* C8c: a placed resource's view of its parent heap (owns nothing) */
    D3DKMT_HANDLE allocation;
    UINT64 gpuva;
    UINT64 bytes;
    BOOL cpu_visible;
    UINT locks;
    void *cpu;
    /* EXP1855: a shader-visible descriptor heap's cpu is a cached shadow of
     * its descriptors (the replay reads them every draw; the GPU buffer is
     * write-combined); mirror is the buffer's own mapping, which receives
     * the heap forms the shaders read (MirrorHeapForms) */
    unsigned char *mirror;
    /* C53 B3: a reserved buffer (tiles != nullptr): tiles[t] is the heap and
     * heap tile mapped at 64 KiB tile t (heap nullptr: NULL), the snapshot
     * CopyTileMappings reads; the owner keeps the GPU mappings themselves */
    struct AgxD3d12TileMapping { AgxD3d12Heap *heap; UINT tile; } *tiles;
    UINT tile_count;
    /* C53 T2D: a reserved Texture2D's tiling (tiles[] in its resource tile
     * order); nullptr for buffers */
    struct AGX_TILE_GEOMETRY *geometry;
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
    UINT64 volume_offsets[16]; /* immutable queried physical mip starts,3D only */
    UINT64 volume_depth_stride;
    D3D12DDI_TEXTURE_LAYOUT volume_layout;
    UINT8 volume_transition;
    /* C36 (EXP1832): a placed texture's heap object -- the runtime passes a
     * placed resource without one */
    AgxD3d12Heap own_heap;
};
#ifndef APPLE_AGX_D3D12_NATIVE_OWNER
static UINT AgxD3d12FormatBlockBytes(DXGI_FORMAT) { return 0; } /* textures need the native owner */
static UINT AgxD3d12FormatBlock(DXGI_FORMAT, unsigned *, unsigned *) { return 0; }
#endif
/* C6a: the allocation size of a committed 2D texture: an upper bound of
 * the owner's layout (whole 16x16 tiles of each level, a 64 KiB granule);
 * 0 = not a texture this device creates (format, samples, dimension). */
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
/* Existing0x105 registry entry is the4BPP1x1 block pattern; every1D mip uses it. */
static bool LineLayoutInfo(const D3D12DDIARG_CREATERESOURCE_0003 *arg,AGX_D3D12_VOLUME_LAYOUT *layout,
                           D3D12DDI_TEXTURE_LAYOUT *resolved) {
 if(!arg || !layout || arg->ResourceType!=D3D12DDI_RT_TEXTURE1D || arg->Height!=1 || (!arg->DepthOrArraySize || arg->DepthOrArraySize>D3D12_REQ_TEXTURE1D_ARRAY_AXIS_DIMENSION) ||
    arg->SampleDesc.Count!=1 || arg->SampleDesc.Quality ||
    (arg->Flags&~D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE))return false;
 const auto selected=static_cast<D3D12DDI_TEXTURE_LAYOUT>(0x105u);
 if(arg->Layout!=D3D12DDI_TL_UNDEFINED && arg->Layout!=selected)return false;
 const UINT bind=(arg->Flags&D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE)?AGX_D3D12_BIND_SAMPLER:0u;
 if(arg->Width>D3D12_REQ_TEXTURE1D_U_DIMENSION || !arg->MipLevels ||
    arg->MipLevels>D3D12_REQ_SUBRESOURCES/arg->DepthOrArraySize ||
    FAILED(AgxD3d12QueryTexture1DArrayLayout(arg->Format,static_cast<UINT>(arg->Width),arg->DepthOrArraySize,arg->MipLevels,bind,layout)))return false;
 if(resolved)*resolved=selected;return true;
}

static bool VolumeLayoutInfo(const D3D12DDIARG_CREATERESOURCE_0003 *arg,AGX_D3D12_VOLUME_LAYOUT *layout,
                             D3D12DDI_TEXTURE_LAYOUT *resolved,UINT8 *transition,
                             UINT *reason=nullptr,HRESULT *queryStatus=nullptr) {
    /* Diagnostic-only: 0 success, 1 input domain, 2 incoming layout,
     * 3 native query refusal; HRESULT 1 means query was not called. */
    if(reason)*reason=1u;
    if(queryStatus)*queryStatus=static_cast<HRESULT>(1);
    if(!arg || !layout || arg->ResourceType!=D3D12DDI_RT_TEXTURE3D || arg->SampleDesc.Count!=1 ||
       arg->SampleDesc.Quality || !arg->Width || arg->Width>32 || arg->Height!=arg->Width ||
       (arg->Width&(arg->Width-1)) || !arg->DepthOrArraySize || arg->DepthOrArraySize>2048 ||
       !arg->MipLevels || arg->MipLevels>2 ||
       (arg->Format!=DXGI_FORMAT_R32_UINT && arg->Format!=DXGI_FORMAT_R8G8B8A8_UNORM) ||
       (arg->Flags&~(D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET|D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE))) return false;
    UINT index=0;for(UINT side=32;side>arg->Width;side>>=1) ++index;
    const auto selected=static_cast<D3D12DDI_TEXTURE_LAYOUT>(0x100u+index);
    if(reason)*reason=2u;
    if(arg->Layout!=D3D12DDI_TL_UNDEFINED && arg->Layout!=selected) return false;
    const UINT bind=((arg->Flags&D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET)?AGX_D3D12_BIND_RENDER_TARGET:0u)|
                    ((arg->Flags&D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE)?AGX_D3D12_BIND_SAMPLER:0u);
    if(reason)*reason=3u;
    const HRESULT status=AgxD3d12QueryTexture3DLayout(arg->Format,static_cast<UINT>(arg->Width),arg->Height,
        arg->DepthOrArraySize,arg->MipLevels,bind,layout);
    if(queryStatus)*queryStatus=status;
    if(FAILED(status)) return false;
    if(reason)*reason=0u;
    if(resolved)*resolved=selected;
    if(transition)*transition=(arg->MipLevels>1 && arg->Width>1)?1u:0xffu;
    return true;
}
#endif
static UINT64 TextureBytes(const D3D12DDIARG_CREATERESOURCE_0003 *arg) {
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if(arg && arg->ResourceType==D3D12DDI_RT_TEXTURE1D) {
        AGX_D3D12_VOLUME_LAYOUT layout={};return LineLayoutInfo(arg,&layout,nullptr)?layout.Bytes:0;
    }
    if(arg && arg->ResourceType==D3D12DDI_RT_TEXTURE3D) {
        AGX_D3D12_VOLUME_LAYOUT layout={};return VolumeLayoutInfo(arg,&layout,nullptr,nullptr)?layout.Bytes:0;
    }
    /* C32: 2x/4x multisampled textures have one level */
    const UINT samples = arg ? arg->SampleDesc.Count : 0u;
    if (!arg || arg->ResourceType != D3D12DDI_RT_TEXTURE2D || arg->SampleDesc.Quality ||
        (samples != 1 && ((samples != 2 && samples != 4) || arg->MipLevels != 1 ||
                          !AgxD3d12FormatMultisample(arg->Format, samples))) || !arg->Width ||
        arg->Width > 16384 || !arg->Height || arg->Height > 16384 || !arg->DepthOrArraySize || !arg->MipLevels ||
        arg->MipLevels > 15)
        return 0;
    /* C29: BC1-BC7 count whole 4x4 blocks */
    unsigned bw = 0, bh = 0;
    const UINT block = AgxD3d12FormatBlock(arg->Format, &bw, &bh);
    if (!block) return 0;
    UINT64 bytes = 0;
    for (UINT level = 0; level < arg->MipLevels; ++level) {
        const UINT64 w = (((arg->Width >> level ? arg->Width >> level : 1) + bw - 1) / bw + 15) & ~15ULL;
        const UINT64 h = (((arg->Height >> level ? arg->Height >> level : 1) + bh - 1) / bh + 15) & ~15ULL;
        bytes += w * h * block;
    }
    bytes *= static_cast<UINT64>(arg->DepthOrArraySize) * samples;
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
                                                 D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS, UINT64 alignmentRestriction, UINT,
                                                 D3D12DDI_RESOURCE_ALLOCATION_INFO *info) {
    if (!info) return;
    memset(info, 0, sizeof(*info));
    info->ResourceDataAlignment = kHeapPage;
    /* Absent metadata has byte alignment, including zero-sized regions. */
    info->AdditionalDataHeaderAlignment = 1;
    info->AdditionalDataAlignment = 1;
    info->Layout = D3D12DDI_TL_ROW_MAJOR;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if(arg && arg->ResourceType==D3D12DDI_RT_TEXTURE1D) {
        AGX_D3D12_VOLUME_LAYOUT layout={};
        if((!alignmentRestriction || (!(alignmentRestriction&(alignmentRestriction-1)) && alignmentRestriction>=kHeapPage)) &&
           LineLayoutInfo(arg,&layout,&info->Layout)) {info->ResourceDataSize=layout.Bytes;info->MipLevelSwizzleTransition=0xffu;}
        else {info->Layout=D3D12DDI_TL_UNDEFINED;info->ResourceDataSize=~0ULL;}
        return;
    }
    if(arg && arg->ResourceType==D3D12DDI_RT_TEXTURE3D) {
        AGX_D3D12_VOLUME_LAYOUT layout={};
        const UINT input[] = {static_cast<UINT>(arg->ResourceType),static_cast<UINT>(arg->Width),
            static_cast<UINT>(arg->Width>>32),arg->Height,arg->DepthOrArraySize,arg->MipLevels,
            static_cast<UINT>(arg->Format),arg->SampleDesc.Count,arg->SampleDesc.Quality,
            static_cast<UINT>(arg->Layout),static_cast<UINT>(arg->Flags),
            static_cast<UINT>(alignmentRestriction),static_cast<UINT>(alignmentRestriction>>32)};
        AdmissionUmdDiagnostic("measure-d3d12-volume-query-input",S_OK,input,13);
        UINT reason=4u; /* alignment restriction rejected, native query not called */
        HRESULT queryStatus=static_cast<HRESULT>(1);
        if((!alignmentRestriction || (!(alignmentRestriction&(alignmentRestriction-1)) && alignmentRestriction>=kHeapPage)) &&
           VolumeLayoutInfo(arg,&layout,&info->Layout,&info->MipLevelSwizzleTransition,&reason,&queryStatus)) info->ResourceDataSize=layout.Bytes;
        else {info->Layout=D3D12DDI_TL_UNDEFINED;info->ResourceDataSize=~0ULL;}
        const UINT output[] = {reason,static_cast<UINT>(info->ResourceDataSize),
            static_cast<UINT>(info->ResourceDataSize>>32),static_cast<UINT>(info->ResourceDataAlignment),
            static_cast<UINT>(info->ResourceDataAlignment>>32),static_cast<UINT>(info->Layout),
            info->MipLevelSwizzleTransition,static_cast<UINT>(layout.NativeBytes),
            static_cast<UINT>(layout.NativeBytes>>32),static_cast<UINT>(layout.DepthStride),
            static_cast<UINT>(layout.DepthStride>>32),static_cast<UINT>(layout.MipOffsets[0]),
            static_cast<UINT>(layout.MipOffsets[1])};
        AdmissionUmdDiagnostic("measure-d3d12-volume-query-output",queryStatus,output,13);
        return;
    }
#else
    (void)alignmentRestriction;
#endif
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
    auto *cached = static_cast<AGX_D3D12_OWNER *>(
        InterlockedCompareExchangePointer(&device->owner, nullptr, nullptr));
    if (cached || InterlockedCompareExchange(&device->owner_failed, 0, 0)) return cached;
    /* CreateDevice performs this admission before publishing the device to
     * callers. A synchronous callback reentry must not construct a second
     * partially initialized owner or wait on its own constructor. */
    if (InterlockedCompareExchange(&device->owner_initializing, 1, 0)) return nullptr;
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
    if (SUCCEEDED(hr) && owner)
        (void)InterlockedExchangePointer(&device->owner, owner);
    else
        (void)InterlockedExchange(&device->owner_failed, 1);
    (void)InterlockedExchange(&device->owner_initializing, 0);
    return SUCCEEDED(hr) ? owner : nullptr;
}
#endif
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
/* Metadata only: never hold this lock across native/core callbacks. Returned
 * heap pointers retain the caller's existing valid-resource lifetime contract. */
static void RegisterHeap(AgxD3d12Device *device, AgxD3d12Heap *heap) {
    AcquireSRWLockExclusive(&device->heap_lock);
    if (device->heap_count < ARRAYSIZE(device->heaps)) device->heaps[device->heap_count++] = heap;
    ReleaseSRWLockExclusive(&device->heap_lock);
}
static void UnregisterHeap(AgxD3d12Device *device, AgxD3d12Heap *heap) {
    AcquireSRWLockExclusive(&device->heap_lock);
    for (UINT i = 0; i < device->heap_count; ++i)
        if (device->heaps[i] == heap) {
            device->heaps[i] = device->heaps[--device->heap_count];
            break;
        }
    ReleaseSRWLockExclusive(&device->heap_lock);
}
static AgxD3d12Heap *HeapAt(AgxD3d12Device *device, UINT64 va, UINT64 *offset) {
    AgxD3d12Heap *result = nullptr;
    AcquireSRWLockShared(&device->heap_lock);
    for (UINT i = 0; va && i < device->heap_count; ++i) {
        AgxD3d12Heap *heap = device->heaps[i];
        if (va >= heap->gpuva && va - heap->gpuva < heap->bytes) {
            *offset = va - heap->gpuva;
            result = heap;
            break;
        }
    }
    ReleaseSRWLockShared(&device->heap_lock);
    return result;
}
/* EXP1855: the mirrored descriptor heap whose shadow holds a CPU handle */
static AgxD3d12Heap *MirroredHeapAt(AgxD3d12Device *device, const void *cpu) {
    AgxD3d12Heap *result = nullptr;
    if (!device || !cpu) return nullptr;
    AcquireSRWLockShared(&device->heap_lock);
    for (UINT i = 0; i < device->mirrored_count; ++i) {
        AgxD3d12Heap *heap = device->mirrored[i];
        const BYTE *base = static_cast<const BYTE *>(heap->cpu);
        if (static_cast<const BYTE *>(cpu) >= base && static_cast<UINT64>(static_cast<const BYTE *>(cpu) - base) < heap->bytes) {
            result = heap;
            break;
        }
    }
    ReleaseSRWLockShared(&device->heap_lock);
    return result;
}
#endif
static void ReleaseHeap(AgxD3d12Device *device, AgxD3d12Heap *heap) {
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if (heap->native && heap->presentation) {
        AgxD3d12OwnerDestroyPresentationTexture(static_cast<AGX_D3D12_OWNER *>(device->owner), heap->presentation);
        memset(heap, 0, sizeof(*heap));
        return;
    }
    if (heap->native) {
        UnregisterHeap(device, heap);
        /* C53 B3: reserved buffers keep the GPU mapping (the owner holds the
         * backing BO); their snapshot forgets this heap */
        AcquireSRWLockExclusive(&device->heap_lock);
        for (UINT i = 0; i < device->heap_count; ++i)
            for (UINT t = 0; device->heaps[i]->tiles && t < device->heaps[i]->tile_count; ++t)
                if (device->heaps[i]->tiles[t].heap == heap) device->heaps[i]->tiles[t] = {};
        ReleaseSRWLockExclusive(&device->heap_lock);
        free(heap->tiles);
        free(heap->geometry);
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
        HRESULT native = AgxD3d12OwnerCreateBuffer(owner, heap->bytes, heap->cpu_visible, &buffer);
        if (SUCCEEDED(native)) {
            heap->native = buffer.Resource;
            heap->gpuva = buffer.GpuVa;
            heap->cpu = buffer.Cpu; /* NULL for a NOT_AVAILABLE heap: Map refuses it */
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
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static HRESULT CreateLineResource(AgxD3d12Device *device,const D3D12DDIARG_CREATEHEAP_0001 *heap_arg,
                                   AgxD3d12Heap *heap,const D3D12DDIARG_CREATERESOURCE_0003 *arg,
                                   AgxD3d12Resource *resource) {
    if(!device || !heap_arg || !heap || !resource || !arg ||
       heap_arg->CPUPageProperty!=D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE ||
       (heap_arg->Flags && heap_arg->Flags!=((arg->Flags&D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET)?
            D3D12DDI_HEAP_FLAG_RT_DS_TEXTURES:D3D12DDI_HEAP_FLAG_NON_RT_DS_TEXTURES)) ||
       (heap_arg->Alignment && heap_arg->Alignment!=kHeapPage)) return E_NOTIMPL;
    AGX_D3D12_VOLUME_LAYOUT layout={};D3D12DDI_TEXTURE_LAYOUT resolved={};const UINT8 transition=0xffu;
    if(arg->Layout==D3D12DDI_TL_UNDEFINED || !LineLayoutInfo(arg,&layout,&resolved) ||
       (heap_arg->ByteSize && heap_arg->ByteSize!=~0ULL && heap_arg->ByteSize<layout.Bytes)) return E_NOTIMPL;
    AGX_D3D12_OWNER *owner=EnsureOwner(device);if(!owner)return E_FAIL;
    const UINT bind=((arg->Flags&D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET)?AGX_D3D12_BIND_RENDER_TARGET:0u)|
                    ((arg->Flags&D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE)?AGX_D3D12_BIND_SAMPLER:0u);
    AGX_D3D12_BUFFER texture={};
    HRESULT hr=AgxD3d12OwnerCreateTexture1DArray(owner,arg->Format,static_cast<UINT>(arg->Width),arg->DepthOrArraySize,arg->MipLevels,bind,layout.Bytes,&texture);
    if(FAILED(hr))return hr;
    memset(heap,0,sizeof(*heap));memset(resource,0,sizeof(*resource));
    heap->native=texture.Resource;heap->gpuva=texture.GpuVa;heap->bytes=layout.Bytes;heap->volume=TRUE;
    heap->allocation=AgxD3d12OwnerBufferAllocation(owner,&texture);
    resource->heap=heap;resource->gpuva=heap->gpuva;resource->bytes=layout.Bytes;resource->type=D3D12DDI_RT_TEXTURE1D;
    resource->format=arg->Format;resource->width=static_cast<UINT>(arg->Width);resource->height=arg->Height;
    resource->layers=arg->DepthOrArraySize;resource->levels=arg->MipLevels;
    memcpy(resource->volume_offsets,layout.MipOffsets,sizeof(resource->volume_offsets));
    resource->volume_depth_stride=layout.DepthStride;resource->volume_layout=resolved;resource->volume_transition=transition;
    return S_OK;
}
static HRESULT CreateVolumeResource(AgxD3d12Device *device,const D3D12DDIARG_CREATEHEAP_0001 *heap_arg,
                                   AgxD3d12Heap *heap,const D3D12DDIARG_CREATERESOURCE_0003 *arg,
                                   AgxD3d12Resource *resource) {
    if(!device || !heap_arg || !heap || !resource || !arg ||
       heap_arg->CPUPageProperty!=D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE ||
       (heap_arg->Flags && heap_arg->Flags!=((arg->Flags&D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET)?
            D3D12DDI_HEAP_FLAG_RT_DS_TEXTURES:D3D12DDI_HEAP_FLAG_NON_RT_DS_TEXTURES)) ||
       (heap_arg->Alignment && heap_arg->Alignment!=kHeapPage)) return E_NOTIMPL;
    AGX_D3D12_VOLUME_LAYOUT layout={};D3D12DDI_TEXTURE_LAYOUT resolved={};UINT8 transition=0;
    if(arg->Layout==D3D12DDI_TL_UNDEFINED || !VolumeLayoutInfo(arg,&layout,&resolved,&transition) ||
       (heap_arg->ByteSize && heap_arg->ByteSize!=~0ULL && heap_arg->ByteSize<layout.Bytes)) return E_NOTIMPL;
    AGX_D3D12_OWNER *owner=EnsureOwner(device);if(!owner)return E_FAIL;
    const UINT bind=((arg->Flags&D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET)?AGX_D3D12_BIND_RENDER_TARGET:0u)|
                    ((arg->Flags&D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE)?AGX_D3D12_BIND_SAMPLER:0u);
    AGX_D3D12_BUFFER texture={};
    HRESULT hr=AgxD3d12OwnerCreateTexture3D(owner,arg->Format,static_cast<UINT>(arg->Width),arg->Height,
        arg->DepthOrArraySize,arg->MipLevels,bind,layout.Bytes,&texture);
    if(FAILED(hr))return hr;
    memset(heap,0,sizeof(*heap));memset(resource,0,sizeof(*resource));
    heap->native=texture.Resource;heap->gpuva=texture.GpuVa;heap->bytes=layout.Bytes;heap->volume=TRUE;
    heap->allocation=AgxD3d12OwnerBufferAllocation(owner,&texture);
    resource->heap=heap;resource->gpuva=heap->gpuva;resource->bytes=layout.Bytes;resource->type=D3D12DDI_RT_TEXTURE3D;
    resource->format=arg->Format;resource->width=static_cast<UINT>(arg->Width);resource->height=arg->Height;
    resource->layers=arg->DepthOrArraySize;resource->levels=arg->MipLevels;
    memcpy(resource->volume_offsets,layout.MipOffsets,sizeof(resource->volume_offsets));
    resource->volume_depth_stride=layout.DepthStride;resource->volume_layout=resolved;resource->volume_transition=transition;
    return S_OK;
}
#endif
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static HRESULT CreateReservedBuffer(AgxD3d12Device *device, AgxD3d12Heap *heap,
                                    const D3D12DDIARG_CREATERESOURCE_0003 *arg, AgxD3d12Resource *resource) {
    const UINT64 tile = 0x10000ull;
    const UINT64 bytes = arg->Width > 0xffffffffull - (tile - 1) ? 0 : (arg->Width + tile - 1) & ~(tile - 1);
    AGX_D3D12_OWNER *owner = bytes ? EnsureOwner(device) : nullptr;
    AGX_D3D12_BUFFER buffer = {};
    memset(heap, 0, sizeof(*heap));
    memset(resource, 0, sizeof(*resource));
    HRESULT hr = owner ? AgxD3d12OwnerCreateReservedBuffer(owner, bytes, &buffer) : E_INVALIDARG;
    AgxD3d12Heap::AgxD3d12TileMapping *tiles = SUCCEEDED(hr) ? static_cast<AgxD3d12Heap::AgxD3d12TileMapping *>(
        calloc(static_cast<size_t>(bytes / tile), sizeof(*tiles))) : nullptr;
    if (SUCCEEDED(hr) && !tiles) {
        AgxD3d12OwnerDestroyBuffer(owner, &buffer);
        hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr)) {
        heap->native = buffer.Resource;
        heap->gpuva = buffer.GpuVa;
        heap->bytes = buffer.Bytes;
        heap->allocation = AgxD3d12OwnerBufferAllocation(owner, &buffer);
        heap->tiles = tiles;
        heap->tile_count = static_cast<UINT>(bytes / tile);
        RegisterHeap(device, heap);
        resource->heap = heap;
        resource->gpuva = heap->gpuva;
        resource->bytes = arg->Width;
        resource->type = D3D12DDI_RT_BUFFER;
    }
    const UINT receipt[] = {static_cast<UINT>(arg->Width), static_cast<UINT>(arg->Width >> 32), heap->tile_count};
    AdmissionUmdDiagnostic("measure-d3d12-reserved", hr, receipt, 3);
    return hr;
}
#endif
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
/* C53 T2D (D3D12-C53-tiled-textures.md): a reserved Texture2D (array) of
 * D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE layout, every tile unmapped; its
 * heap records the tiling (AGX_TILE_GEOMETRY) and a tile snapshot as
 * reserved buffers do. */
static HRESULT CreateReservedTexture(AgxD3d12Device *device, AgxD3d12Heap *heap,
                                     const D3D12DDIARG_CREATERESOURCE_0003 *arg, AgxD3d12Resource *resource) {
    AGX_D3D12_OWNER *owner = EnsureOwner(device);
    AGX_D3D12_BUFFER texture = {};
    AGX_D3D12_RESERVED_TILING tiling = {};
    memset(heap, 0, sizeof(*heap));
    memset(resource, 0, sizeof(*resource));
    const UINT bind = ((arg->Flags & D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET) ? AGX_D3D12_BIND_RENDER_TARGET : 0u) |
                      ((arg->Flags & D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE) ? AGX_D3D12_BIND_SAMPLER : 0u);
    HRESULT hr = !owner || arg->SampleDesc.Count > 1 || !arg->MipLevels || arg->MipLevels > AGX_TILE_MAX_LEVELS ||
                         (arg->Flags & D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL) || arg->Width > 0xffffffffull
                     ? E_INVALIDARG
                     : AgxD3d12OwnerCreateReservedTexture2D(owner, arg->Format, static_cast<UINT>(arg->Width), arg->Height,
                                                            arg->DepthOrArraySize, arg->MipLevels, bind, &texture,
                                                            &tiling);
    AGX_TILE_GEOMETRY *geometry = SUCCEEDED(hr) ? static_cast<AGX_TILE_GEOMETRY *>(calloc(1, sizeof(AGX_TILE_GEOMETRY)))
                                                : nullptr;
    if (geometry) {
        geometry->MipLevels = arg->MipLevels;
        geometry->ArraySize = arg->DepthOrArraySize;
        geometry->StandardMips = tiling.StandardMips;
        geometry->PackedTiles = tiling.PackedTiles;
        memcpy(geometry->TilesX, tiling.TilesX, sizeof(geometry->TilesX));
        memcpy(geometry->TilesY, tiling.TilesY, sizeof(geometry->TilesY));
    }
    const UINT tiles = geometry ? AgxTileResourceTiles(geometry) : 0u;
    AgxD3d12Heap::AgxD3d12TileMapping *snapshot = tiles ? static_cast<AgxD3d12Heap::AgxD3d12TileMapping *>(
        calloc(tiles, sizeof(AgxD3d12Heap::AgxD3d12TileMapping))) : nullptr;
    if (SUCCEEDED(hr) && !snapshot) {
        AgxD3d12OwnerDestroyBuffer(owner, &texture);
        free(geometry);
        geometry = nullptr;
        hr = tiles ? E_OUTOFMEMORY : E_INVALIDARG;
    }
    if (SUCCEEDED(hr)) {
        heap->native = texture.Resource;
        heap->gpuva = texture.GpuVa;
        heap->bytes = static_cast<UINT64>(tiles) * 0x10000ull;
        heap->allocation = AgxD3d12OwnerBufferAllocation(owner, &texture);
        heap->tiles = snapshot;
        heap->tile_count = tiles;
        heap->geometry = geometry;
        RegisterHeap(device, heap);
        resource->heap = heap;
        resource->bytes = heap->bytes;
        resource->type = D3D12DDI_RT_TEXTURE2D;
        resource->format = arg->Format;
        resource->width = static_cast<UINT>(arg->Width);
        resource->height = arg->Height;
        resource->layers = arg->DepthOrArraySize;
        resource->levels = arg->MipLevels;
    }
    const UINT receipt[] = {static_cast<UINT>(arg->Format), static_cast<UINT>(arg->Width), arg->Height,
                            arg->DepthOrArraySize | (static_cast<UINT>(arg->MipLevels) << 16), tiles};
    AdmissionUmdDiagnostic("measure-d3d12-reserved-texture2d", hr, receipt, 5);
    return hr;
}
#endif
static HRESULT APIENTRY CreateHeapAndResource(D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATEHEAP_0001 *heap_arg,
                                              D3D12DDI_HHEAP heap_handle, D3D12DDI_HRTRESOURCE runtime_resource,
                                              const D3D12DDIARG_CREATERESOURCE_0003 *resource_arg,
                                              const D3D12DDI_CLEAR_VALUES *, D3D12DDI_HRESOURCE resource_handle) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *heap = static_cast<AgxD3d12Heap *>(heap_handle.pDrvPrivate);
    auto *resource = static_cast<AgxD3d12Resource *>(resource_handle.pDrvPrivate);
    HeapAndResourceReceipt("measure-d3d12-create-heap-resource", heap_arg, resource_arg,
                           (heap ? 1u : 0u) | (resource ? 2u : 0u));
    if (!device) return E_INVALIDARG;
    (void)runtime_resource; /* C7b: used by the native owner's presentation textures */
    /* C8c: a placed buffer -- no heap description; ReuseBufferGPUVA names
     * the resource of an existing heap and the byte offset in it (the
     * encoding this assumes; receipt measure-d3d12-placed records it). */
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    /* C53 B3 [H]: a reserved buffer -- no heap description and no parent
     * resource (receipt measure-d3d12-reserved). Unreachable until the tier
     * flip: the runtime refuses CreateReservedResource at tier 0. */
    if (!heap_arg && heap && resource && resource_arg && resource_arg->ResourceType == D3D12DDI_RT_BUFFER &&
        !resource_arg->ReuseBufferGPUVA.BaseAddress.UMD.hResource.pDrvPrivate)
        return CreateReservedBuffer(device, heap, resource_arg, resource);
    /* C53 T2D: a reserved Texture2D, likewise */
    if (!heap_arg && resource && resource_arg && resource_arg->ResourceType == D3D12DDI_RT_TEXTURE2D &&
        resource_arg->Layout == D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE &&
        !resource_arg->ReuseBufferGPUVA.BaseAddress.UMD.hResource.pDrvPrivate)
        return CreateReservedTexture(device, heap ? heap : &resource->own_heap, resource_arg, resource);
#endif
    if (!heap_arg && resource && resource_arg && resource_arg->ResourceType == D3D12DDI_RT_BUFFER) {
        const auto *base = static_cast<const AgxD3d12Resource *>(resource_arg->ReuseBufferGPUVA.BaseAddress.UMD.hResource.pDrvPrivate);
        const UINT64 offset = resource_arg->ReuseBufferGPUVA.BaseAddress.UMD.Offset;
        if (heap) memset(heap, 0, sizeof(*heap));
        memset(resource, 0, sizeof(*resource));
        const UINT reason = !base || !base->heap ? 1u
                          : offset > base->heap->bytes || resource_arg->Width > base->heap->bytes - offset ? 6u : 0u;
        if (!reason && heap) {
            /* the placed resource's heap object aliases the parent and
             * destroying it releases nothing. The runtime adds the placement
             * offset to a Map itself (EXP1371: offsetting the alias moved
             * the CPU view past the resource), so the alias starts where
             * the parent does. */
            *heap = *base->heap;
            heap->alias = TRUE;
            heap->locks = 0;
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
    if(resource_arg && resource_arg->ResourceType==D3D12DDI_RT_TEXTURE1D)
        return CreateLineResource(device,heap_arg,heap,resource_arg,resource);
    if(resource_arg && resource_arg->ResourceType==D3D12DDI_RT_TEXTURE3D)
        return CreateVolumeResource(device,heap_arg,heap,resource_arg,resource);
    /* C6a: a committed 2D texture is one owner texture. C36: so is a placed
     * one (no heap description; ReuseBufferGPUVA names the parent heap):
     * the owner keeps textures in their own twiddled BOs, so it does not
     * share the parent's memory -- D3D12 leaves an aliased resource's
     * content undefined until it is cleared, copied or discarded anyway.
     * The placed resource's heap object owns that texture. */
    const bool placed = !heap_arg && resource_arg && resource_arg->ResourceType == D3D12DDI_RT_TEXTURE2D &&
                        resource_arg->ReuseBufferGPUVA.BaseAddress.UMD.hResource.pDrvPrivate;
    AgxD3d12Heap *const texture_heap = heap ? heap : placed && resource ? &resource->own_heap : nullptr;
    if ((heap_arg || placed) && texture_heap && resource && resource_arg && resource_arg->ResourceType == D3D12DDI_RT_TEXTURE2D) {
        const UINT64 bytes = TextureBytes(resource_arg);
        AGX_D3D12_OWNER *owner = bytes ? EnsureOwner(device) : nullptr;
        memset(texture_heap, 0, sizeof(*texture_heap));
        memset(resource, 0, sizeof(*resource));
        AGX_D3D12_BUFFER texture = {};
        const UINT bind = ((resource_arg->Flags & D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET) ? AGX_D3D12_BIND_RENDER_TARGET : 0u) |
                          ((resource_arg->Flags & D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE) ? AGX_D3D12_BIND_SAMPLER : 0u) |
                          ((resource_arg->Flags & D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL) ? AGX_D3D12_BIND_DEPTH_STENCIL : 0u);
        /* C7b: a 2D render target the runtime can share (DXGI swapchain
         * buffers among them) gets a kernel allocation of the runtime
         * resource -- the D3D10/11 presentation-resource contract (BGRA8 or
         * RGBA8, one mip, layer and sample, linear) */
        const bool presentable = !placed && (bind & AGX_D3D12_BIND_RENDER_TARGET) && runtime_resource.handle &&
                                 resource_arg->MipLevels == 1 && resource_arg->DepthOrArraySize == 1 &&
                                 resource_arg->SampleDesc.Count == 1 && /* EXP1829 C32A: a 4x target became 1x */
                                 (resource_arg->Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                                  resource_arg->Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
                                  resource_arg->Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                                  /* C40: an RGBA8 presentation allocation converted at present */
                                  resource_arg->Format == DXGI_FORMAT_R10G10B10A2_UNORM);
        UINT presentation_allocation = 0;
        HRESULT hr = !owner ? E_NOTIMPL
                   : presentable
                         ? AgxD3d12OwnerCreatePresentationTexture(owner, resource_arg->Format,
                                                                  static_cast<UINT>(resource_arg->Width), resource_arg->Height,
                                                                  (bind & AGX_D3D12_BIND_SAMPLER) != 0,
                                                                  runtime_resource.handle, &texture, &texture_heap->presentation,
                                                                  &presentation_allocation)
                         : AgxD3d12OwnerCreateTexture2D(owner, resource_arg->Format, static_cast<UINT>(resource_arg->Width),
                                                        resource_arg->Height, resource_arg->DepthOrArraySize,
                                                        resource_arg->MipLevels, bind, bytes, &texture,
                                                        resource_arg->SampleDesc.Count);
        if (SUCCEEDED(hr)) {
            texture_heap->native = texture.Resource;
            texture_heap->gpuva = texture.GpuVa;
            texture_heap->bytes = bytes;
            texture_heap->allocation = presentable ? presentation_allocation : AgxD3d12OwnerBufferAllocation(owner, &texture);
            resource->heap = texture_heap;
            resource->bytes = bytes;
            resource->type = D3D12DDI_RT_TEXTURE2D;
            resource->format = resource_arg->Format;
            resource->width = static_cast<UINT>(resource_arg->Width);
            resource->height = resource_arg->Height;
            resource->layers = resource_arg->DepthOrArraySize;
            resource->levels = resource_arg->MipLevels;
        }
        const UINT receipt[] = {static_cast<UINT>(resource_arg->Format), static_cast<UINT>(resource_arg->Width),
                                resource_arg->Height, static_cast<UINT>(bytes >> 16),
                                bind | (presentable ? 0x100u : 0u) | (placed ? 0x200u : 0u)};
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
    auto *resource = static_cast<AgxD3d12Resource *>(resource_handle.pDrvPrivate);
    /* C36: a placed texture releases the heap object it holds itself */
    if (device && !heap && resource && resource->heap == &resource->own_heap && resource->own_heap.allocation)
        ReleaseHeap(device, &resource->own_heap);
    if (resource) memset(resource, 0, sizeof(AgxD3d12Resource));
    if (device && heap && (heap->allocation || heap->tiles) && !heap->alias) ReleaseHeap(device, heap);
}
static HRESULT APIENTRY MapHeap(D3D12DDI_HDEVICE handle, D3D12DDI_HHEAP heap_handle, VOID **data) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *heap = static_cast<AgxD3d12Heap *>(heap_handle.pDrvPrivate);
    if(heap && heap->volume) {if(data)*data=nullptr;return E_INVALIDARG;}
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
    /* Absent metadata has byte alignment, including zero-sized regions. */
    info->AdditionalDataHeaderAlignment = 1;
    info->AdditionalDataAlignment = 1;
        info->Layout = resource && (resource->type==D3D12DDI_RT_TEXTURE3D || resource->type==D3D12DDI_RT_TEXTURE1D) ? resource->volume_layout:D3D12DDI_TL_ROW_MAJOR;
        info->ResourceDataSize = resource && resource->heap ? resource->heap->bytes : ~0ULL;
        if(resource && (resource->type==D3D12DDI_RT_TEXTURE3D || resource->type==D3D12DDI_RT_TEXTURE1D)) info->MipLevelSwizzleTransition=resource->volume_transition;
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
    if(resource && resource->type==D3D12DDI_RT_TEXTURE1D) {
        if(resource->levels && resource->layers && subresource<static_cast<UINT64>(resource->levels)*resource->layers) {
            const UINT mip=subresource%resource->levels,slice=subresource/resource->levels;
            info->Offset=static_cast<UINT64>(slice)*resource->volume_depth_stride+resource->volume_offsets[mip];
            info->RowStride=4ull*(resource->width>>mip?resource->width>>mip:1u);info->DepthStride=resource->volume_depth_stride;
        }
    } else if(resource && resource->type==D3D12DDI_RT_TEXTURE3D) {
        if(subresource<resource->levels) {
            const UINT side=(resource->width>>subresource)?resource->width>>subresource:1u;
            info->Offset=resource->volume_offsets[subresource];
            info->RowStride=4ull*side*side;info->DepthStride=resource->volume_depth_stride;
        }
    } else {
        if (resource && subresource == 0) info->RowStride = info->DepthStride = resource->bytes;
    }
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
/* C47: a CBV/SRV/UAV descriptor is its CPU record (64 bytes) followed by its
 * heap form (AgxD3d12HeapForm), which shaders read through bindless handles */
static constexpr UINT kCbvSrvUavDescriptorBytes = 160u;
static constexpr UINT kSamplerDescriptorBytes = 128u;
static constexpr UINT kHeapRanges = 64u; /* DXBC_HEAP_RANGE_MAX */
static constexpr UINT kHeapFormOffset = 64u;
static UINT DescriptorBytes(D3D12DDI_DESCRIPTOR_HEAP_TYPE type) {
    /* C47: sampler descriptors carry a heap form (AgxD3d12SamplerForm) too */
    if (type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) return kCbvSrvUavDescriptorBytes;
    if (type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_SAMPLER) return kSamplerDescriptorBytes;
    return 64u; /* C6e: a sampler record (kind + D3D12DDI_SAMPLER_DESC) needs 56 bytes */
}
static UINT APIENTRY GetDescriptorSizeInBytes(D3D12DDI_HDEVICE, D3D12DDI_DESCRIPTOR_HEAP_TYPE type) {
    return DescriptorBytes(type);
}
/* C5c: what a CBV/SRV/UAV descriptor holds. Lists replay on the native
 * owner (Gallium bindings), so a descriptor is a CPU record of the view --
 * the buffer heap and byte range -- never read by the GPU. */
enum : UINT { kDescriptorNone = 0, kDescriptorCbv = 1, kDescriptorUavBuffer = 2, kDescriptorSrvBuffer = 3,
              kDescriptorRtv = 4, kDescriptorSrvTexture = 5, kDescriptorSampler = 6, kDescriptorDsv = 7,
              kDescriptorUavTexture = 8, kDescriptorUavTypedBuffer = 9 /* C9: typed UAVs (image u#) */ };
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
    UINT component_mapping; /* C18: encoded SRV fetch-result routing */
    union {
        UINT cube; /* C19: cube-class SRV; arrayness belongs to the shader */
        UINT counter_offset; /* C33: a structured UAV's counter word (UavCounter) */
    };
};
/* C33: a buffer UAV's counter heap lives in the bytes of the SRV-only levels
 * and component_mapping, keeping descriptors 64 bytes */
static_assert(offsetof(AgxD3d12Descriptor, component_mapping) == offsetof(AgxD3d12Descriptor, levels) + sizeof(UINT) &&
                  sizeof(AgxD3d12Heap *) <= 2 * sizeof(UINT),
              "the counter heap fits levels and component_mapping");
static void SetUavCounter(AgxD3d12Descriptor *d, AgxD3d12Heap *heap, UINT offset) {
    memcpy(reinterpret_cast<unsigned char *>(d) + offsetof(AgxD3d12Descriptor, levels), &heap, sizeof(heap));
    d->counter_offset = offset;
}
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static AgxD3d12Heap *UavCounter(const AgxD3d12Descriptor &d, UINT *offset) {
    AgxD3d12Heap *heap = nullptr;
    if (d.kind != kDescriptorUavBuffer) return nullptr;
    memcpy(&heap, reinterpret_cast<const unsigned char *>(&d) + offsetof(AgxD3d12Descriptor, levels), sizeof(heap));
    *offset = d.counter_offset;
    return heap;
}
#endif
/* C6e: a sampler descriptor */
struct AgxD3d12SamplerDescriptor {
    UINT kind; /* kDescriptorSampler */
    D3D12DDI_SAMPLER_DESC desc;
};
static_assert(sizeof(AgxD3d12SamplerDescriptor) <= kHeapFormOffset, "fits a sampler descriptor");
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
/* C47: a sampler descriptor's heap form behind its CPU record */
static_assert(kHeapFormOffset + sizeof(AGX_D3D12_HEAP_SAMPLER) <= kSamplerDescriptorBytes, "the sampler heap form fits");
static AGX_D3D12_HEAP_SAMPLER *SamplerFormOf(void *descriptor) {
    return reinterpret_cast<AGX_D3D12_HEAP_SAMPLER *>(static_cast<BYTE *>(descriptor) + kHeapFormOffset);
}
#endif
static_assert(sizeof(AgxD3d12Descriptor) <= kHeapFormOffset, "fits a CBV/SRV/UAV descriptor");
/* C47: the heap form: Asahi's packed texture descriptor of the view (null
 * for anything but a texture SRV), room for a PBE descriptor (typed UAVs),
 * the layout key the texture had when packed and the view's shader dimension
 * (DXBC_SRV_*) */
struct AgxD3d12HeapForm {
    BYTE texture[24];
    BYTE pbe[24];
    BYTE counter[24]; /* a structured UAV counter's PBE */
    UINT64 key;
    UINT dimension;
    UINT reserved[3];
};
static_assert(kHeapFormOffset + sizeof(AgxD3d12HeapForm) == kCbvSrvUavDescriptorBytes, "the heap form ends the descriptor");
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static_assert(kCbvSrvUavDescriptorBytes == DXBC_HEAP_DESCRIPTOR_STRIDE && kHeapFormOffset == DXBC_HEAP_DESCRIPTOR_AGX_OFFSET &&
                  kHeapRanges == DXBC_HEAP_RANGE_MAX && kHeapRanges == AGX_D3D12_HEAP_RANGES &&
                  sizeof(AgxD3d12HeapForm::texture) == AGX_D3D12_HEAP_TEXTURE_BYTES,
              "the translator's heap layout (dxbc_bindings.h)");
#endif
static AgxD3d12HeapForm *HeapFormOf(void *descriptor) {
    return reinterpret_cast<AgxD3d12HeapForm *>(static_cast<BYTE *>(descriptor) + kHeapFormOffset);
}
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
/* EXP1855: copies the heap forms of count descriptors from a heap's shadow
 * (first is in it) to the GPU buffer the shaders read */
static void MirrorHeapForms(const AgxD3d12Heap *heap, const void *first, UINT count) {
    if (!heap || !heap->mirror || !heap->cpu || !first || !count) return;
    const SIZE_T offset = static_cast<SIZE_T>(static_cast<const BYTE *>(first) - static_cast<const BYTE *>(heap->cpu));
    if (static_cast<const BYTE *>(first) < static_cast<const BYTE *>(heap->cpu) || offset % kCbvSrvUavDescriptorBytes ||
        offset + static_cast<UINT64>(count) * kCbvSrvUavDescriptorBytes > heap->bytes)
        return;
    for (UINT i = 0; i < count; ++i) {
        const SIZE_T at = offset + static_cast<SIZE_T>(i) * kCbvSrvUavDescriptorBytes + kHeapFormOffset;
        memcpy(heap->mirror + at, static_cast<const BYTE *>(heap->cpu) + at, sizeof(AgxD3d12HeapForm));
    }
}
#endif
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
    /* a null CBV is a valid descriptor: its receipt keeps reason 2 but is
     * not a failure (EXP1834 HDR: 8760 null CBVs, each failure record
     * forcing a synchronous trace write) */
    const UINT receipt[] = {reason, desc ? desc->SizeInBytes : 0u, 0u};
    AdmissionUmdDiagnostic("measure-d3d12-cbv", reason && reason != 2u ? E_NOTIMPL : S_OK, receipt, 3);
}
/* Raw and structured buffer UAVs (the dxbc_to_nir shader-buffer binding);
 * typed buffers, textures and append/consume counters write a null
 * descriptor with a receipt naming the reason. */
static bool VolumeViewRange(const AgxD3d12Resource *r,UINT level,UINT first,UINT requested,UINT *count) {
    if(level>=r->levels) return false;
    const UINT depth=r->layers>>level ? r->layers>>level:1u;
    if(first>=depth) return false;
    *count=requested==UINT_MAX?depth-first:requested;
    return *count && *count<=depth-first;
}
static VOID APIENTRY CreateUnorderedAccessView(D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002 *arg,
                                               D3D12DDI_CPU_DESCRIPTOR_HANDLE dest) {
    auto *out = reinterpret_cast<AgxD3d12Descriptor *>(dest.ptr);
    if (!out) return;
    memset(out, 0, sizeof(*out));
    (void)handle;
    const auto *resource = arg ? static_cast<const AgxD3d12Resource *>(arg->hDrvResource.pDrvPrivate) : nullptr;
    if(arg && resource && resource->heap && resource->type==D3D12DDI_RT_TEXTURE1D) {
        const auto &v=arg->Tex1D;
        const UINT layers=v.ArraySize==UINT_MAX && resource->layers==1 && !v.FirstArraySlice?1u:v.ArraySize;
        const UINT reason=arg->ResourceDimension!=D3D12DDI_RD_TEXTURE1D || v.MipSlice>=resource->levels ||
            v.FirstArraySlice>=resource->layers || !layers || layers>resource->layers-v.FirstArraySlice ||
            (arg->Format!=DXGI_FORMAT_UNKNOWN && arg->Format!=resource->format)?6u:0u;
        if(!reason) {out->kind=kDescriptorUavTexture;out->heap=resource->heap;out->format=resource->format;
            out->level=v.MipSlice;out->levels=1;out->first_layer=v.FirstArraySlice;out->layers=layers;}
        ViewReceipt("measure-d3d12-uav",reason,static_cast<UINT>(arg->Format),v.MipSlice);return;
    }
    if(arg && resource && resource->heap && resource->type==D3D12DDI_RT_TEXTURE3D) {
        UINT count=0;
        const UINT depth=arg->Tex3D.MipSlice<resource->levels ? (resource->layers>>arg->Tex3D.MipSlice ? resource->layers>>arg->Tex3D.MipSlice:1u):0;
        const UINT reason=arg->ResourceDimension!=D3D12DDI_RD_TEXTURE3D ||
            !VolumeViewRange(resource,arg->Tex3D.MipSlice,arg->Tex3D.FirstW,arg->Tex3D.WSize,&count) ||
            arg->Tex3D.FirstW || count!=depth ? 6u:0u;
        if(!reason) {out->kind=kDescriptorUavTexture;out->heap=resource->heap;
            out->format=arg->Format==DXGI_FORMAT_UNKNOWN?resource->format:arg->Format;
            out->level=arg->Tex3D.MipSlice;out->levels=1;out->layers=count;}
        ViewReceipt("measure-d3d12-uav",reason,static_cast<UINT>(arg->Format),count);return;
    }
    /* C9: a typed UAV of a 2D (array) texture: one mip, a slice range */
    if (arg && resource && resource->heap && resource->type == D3D12DDI_RT_TEXTURE2D) {
        const D3D12DDIARG_TEX2D_UNORDERED_ACCESS_VIEW_0002 &v = arg->Tex2D;
        const UINT reason = arg->ResourceDimension != D3D12DDI_RD_TEXTURE2D || v.PlaneSlice ? 4u
                          : v.MipSlice >= resource->levels || v.FirstArraySlice >= resource->layers ? 6u : 0u;
        if (!reason) {
            out->kind = kDescriptorUavTexture;
            out->heap = resource->heap;
            out->format = arg->Format == DXGI_FORMAT_UNKNOWN ? resource->format : arg->Format;
            out->level = v.MipSlice;
            out->levels = 1;
            out->first_layer = v.FirstArraySlice;
            out->layers = v.ArraySize == UINT_MAX || v.ArraySize > resource->layers - v.FirstArraySlice
                              ? resource->layers - v.FirstArraySlice : (v.ArraySize ? v.ArraySize : 1u);
        }
        ViewReceipt("measure-d3d12-uav", reason, static_cast<UINT>(arg->Format), 0x80000000u | v.MipSlice);
        return;
    }
    const bool raw = arg && (arg->Buffer.Flags & D3D12DDI_BUFFER_UAV_FLAG_RAW);
    /* C9: a typed buffer UAV (a format, neither raw nor structured) */
    const bool typed = arg && !raw && !arg->Buffer.StructureByteStride && arg->Format != DXGI_FORMAT_UNKNOWN;
    UINT stride = !arg ? 0u : raw ? 4u : arg->Buffer.StructureByteStride;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if (typed) stride = AgxD3d12FormatBlockBytes(arg->Format);
#endif
    /* C33: a counter belongs to a structured UAV and is one aligned word of
     * a buffer */
    const auto *counter = arg ? static_cast<const AgxD3d12Resource *>(arg->Buffer.hDrvCounterResource.pDrvPrivate) : nullptr;
    UINT reason = !arg || !resource || !resource->heap ? 1u
                : arg->ResourceDimension != D3D12DDI_RD_BUFFER ? 4u
                : !stride ? 5u /* a typed format the owner cannot address */
                : counter && (raw || typed || !counter->heap || counter->type != D3D12DDI_RT_BUFFER ||
                              arg->Buffer.CounterOffsetInBytes % 4u || counter->bytes < 4u ||
                              arg->Buffer.CounterOffsetInBytes > counter->bytes - 4u ||
                              counter->gpuva < counter->heap->gpuva) ? 7u
                : 0u;
    const UINT64 offset = reason ? 0 : arg->Buffer.FirstElement * stride;
    const UINT64 bytes = reason ? 0 : static_cast<UINT64>(arg->Buffer.NumElements) * stride;
    if (!reason && (offset > resource->bytes || bytes > resource->bytes - offset)) reason = 6u;
#ifndef APPLE_AGX_D3D12_NATIVE_OWNER
    if (!reason) reason = 3u;
#endif
    if (!reason) {
        out->kind = typed ? kDescriptorUavTypedBuffer : kDescriptorUavBuffer;
        out->heap = resource->heap;
        out->offset = resource->gpuva - resource->heap->gpuva + offset;
        out->bytes = bytes;
        out->format = typed ? arg->Format : DXGI_FORMAT_UNKNOWN;
        if (counter)
            SetUavCounter(out, counter->heap,
                          static_cast<UINT>(counter->gpuva - counter->heap->gpuva + arg->Buffer.CounterOffsetInBytes));
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
    if(arg && resource && resource->heap && resource->type==D3D12DDI_RT_TEXTURE3D) {
        UINT count=0;
        const UINT reason=arg->ResourceDimension!=D3D12DDI_RD_TEXTURE3D ||
            !VolumeViewRange(resource,arg->Tex3D.MipSlice,arg->Tex3D.FirstW,arg->Tex3D.WSize,&count) || count!=1 ? 6u:0u;
        if(!reason) {out->kind=kDescriptorRtv;out->heap=resource->heap;
            out->format=arg->Format==DXGI_FORMAT_UNKNOWN?resource->format:arg->Format;
            out->level=arg->Tex3D.MipSlice;out->first_layer=arg->Tex3D.FirstW;out->layers=1;}
        ViewReceipt("measure-d3d12-rtv",reason,static_cast<UINT>(arg->Format),count);return;
    }
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
    /* C18: mapping is descriptor state, including typed buffer views. */
    if (arg && !AgxD3d12ComponentMappingValid(arg->Shader4ComponentMapping)) {
        ViewReceipt("measure-d3d12-srv", 8u, arg->Shader4ComponentMapping, 0u);
        return;
    }
    const auto *resource = arg ? static_cast<const AgxD3d12Resource *>(arg->hDrvResource.pDrvPrivate) : nullptr;
    if(arg && resource && resource->heap && resource->type==D3D12DDI_RT_TEXTURE1D) {
        const auto &v=arg->Tex1D;UINT levels=0;
        const UINT layers=v.ArraySize==UINT_MAX && resource->layers==1 && !v.FirstArraySlice?1u:v.ArraySize;
        UINT reason=arg->ResourceDimension!=D3D12DDI_RD_TEXTURE1D || v.ResourceMinLODClamp!=0.0f ||
            v.MostDetailedMip>=resource->levels || v.FirstArraySlice>=resource->layers ||
            !layers || layers>resource->layers-v.FirstArraySlice ||
            (arg->Format!=DXGI_FORMAT_UNKNOWN && arg->Format!=resource->format)?6u:0u;
        if(!reason) {levels=v.MipLevels==UINT_MAX?resource->levels-v.MostDetailedMip:v.MipLevels;
            if(!levels || levels>resource->levels-v.MostDetailedMip)reason=6u;}
        if(!reason) {out->kind=kDescriptorSrvTexture;out->heap=resource->heap;out->format=resource->format;
            out->level=v.MostDetailedMip;out->levels=levels;out->first_layer=v.FirstArraySlice;out->layers=layers;out->component_mapping=arg->Shader4ComponentMapping;}
        ViewReceipt("measure-d3d12-srv",reason,static_cast<UINT>(arg->Format),levels);return;
    }
    if(arg && resource && resource->heap && resource->type==D3D12DDI_RT_TEXTURE3D) {
        const auto &v=arg->Tex3D;
        UINT levels=0;
        UINT reason=arg->ResourceDimension!=D3D12DDI_RD_TEXTURE3D || v.ResourceMinLODClamp!=0.0f ||
            v.MostDetailedMip>=resource->levels ? 6u:0u;
        if(!reason) {levels=v.MipLevels==UINT_MAX?resource->levels-v.MostDetailedMip:v.MipLevels;
            if(!levels || levels>resource->levels-v.MostDetailedMip)reason=6u;}
        if(!reason) {out->kind=kDescriptorSrvTexture;out->heap=resource->heap;
            out->format=arg->Format==DXGI_FORMAT_UNKNOWN?resource->format:arg->Format;
            out->level=v.MostDetailedMip;out->levels=levels;out->layers=1;
            out->component_mapping=arg->Shader4ComponentMapping;}
        ViewReceipt("measure-d3d12-srv",reason,static_cast<UINT>(arg->Format),levels);return;
    }
    /* C19: a cube view reuses an ordinary square 2D-array allocation.
     * The shader distinguishes nonarray cubes from one-element cube arrays. */
    if (arg && resource && resource->heap && resource->type == D3D12DDI_RT_TEXTURE2D &&
        arg->ResourceDimension == D3D12DDI_RD_TEXTURECUBE) {
        const D3D12DDIARG_TEXCUBE_SHADER_RESOURCE_VIEW &v = arg->TexCube;
        UINT reason = resource->width != resource->height || v.ResourceMinLODClamp != 0.0f ? 4u
                    : !v.NumCubes || v.First2DArrayFace >= resource->layers ||
                      v.NumCubes > (resource->layers - v.First2DArrayFace) / 6u ||
                      v.MostDetailedMip >= resource->levels ? 6u : 0u;
        const UINT levels = reason ? 0u : v.MipLevels == UINT_MAX ? resource->levels - v.MostDetailedMip : v.MipLevels;
        if (!reason && (!levels || levels > resource->levels - v.MostDetailedMip ||
                        v.First2DArrayFace + v.NumCubes * 6u - 1u > 0xffffu ||
                        v.MostDetailedMip + levels - 1u > 0xffu)) reason = 6u;
        if (!reason) {
            out->kind = kDescriptorSrvTexture;
            out->component_mapping = arg->Shader4ComponentMapping;
            out->cube = 1;
            out->heap = resource->heap;
            out->format = arg->Format == DXGI_FORMAT_UNKNOWN ? resource->format : arg->Format;
            out->level = v.MostDetailedMip;
            out->levels = levels;
            out->first_layer = v.First2DArrayFace;
            out->layers = v.NumCubes * 6u;
        }
        ViewReceipt("measure-d3d12-srv", reason, static_cast<UINT>(arg->Format), 0xc0000000u | levels);
        return;
    }
    /* C6e: 2D (array) texture SRVs of committed textures */
    if (arg && resource && resource->heap && resource->type == D3D12DDI_RT_TEXTURE2D) {
        const D3D12DDIARG_TEX2D_SHADER_RESOURCE_VIEW_0002 &v = arg->Tex2D;
        UINT reason = arg->ResourceDimension != D3D12DDI_RD_TEXTURE2D || v.PlaneSlice ? 4u
                    : v.MostDetailedMip >= resource->levels || v.FirstArraySlice >= resource->layers ? 6u : 0u;
        if (!reason) {
            out->kind = kDescriptorSrvTexture;
            out->component_mapping = arg->Shader4ComponentMapping;
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
    /* C9: a typed buffer SRV (a format, neither raw nor structured) */
    const bool typed = arg && !raw && !arg->Buffer.StructureByteStride && arg->Format != DXGI_FORMAT_UNKNOWN;
    UINT stride = !arg ? 0u : raw ? 4u : arg->Buffer.StructureByteStride;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if (typed) stride = AgxD3d12FormatBlockBytes(arg->Format);
#endif
    UINT reason = !arg || !resource || !resource->heap ? 1u
                : arg->ResourceDimension != D3D12DDI_RD_BUFFER ? 4u
                : !typed && arg->Shader4ComponentMapping != AGX_D3D12_COMPONENT_MAPPING_IDENTITY ? 8u
                : !stride ? 5u /* a typed format the owner cannot address */
                : 0u;
    const UINT64 offset = reason ? 0 : arg->Buffer.FirstElement * stride;
    const UINT64 bytes = reason ? 0 : static_cast<UINT64>(arg->Buffer.NumElements) * stride;
    if (!reason && (offset > resource->bytes || bytes > resource->bytes - offset)) reason = 6u;
#ifndef APPLE_AGX_D3D12_NATIVE_OWNER
    if (!reason) reason = 3u;
#endif
    if (!reason) {
        out->kind = kDescriptorSrvBuffer;
        out->component_mapping = arg->Shader4ComponentMapping;
        out->heap = resource->heap;
        out->offset = resource->gpuva - resource->heap->gpuva + offset;
        out->bytes = bytes;
        out->format = typed ? arg->Format : DXGI_FORMAT_UNKNOWN;
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
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static void PackHeapForm(AGX_D3D12_OWNER *owner, AgxD3d12Descriptor *view);
static void PackUavHeapForm(AGX_D3D12_OWNER *owner, AgxD3d12Descriptor *view);
static bool UavDescriptorKind(UINT kind) {
    return kind == kDescriptorUavBuffer || kind == kDescriptorUavTypedBuffer || kind == kDescriptorUavTexture;
}
#endif
/* C47: a view written into a CBV/SRV/UAV heap also gets its heap form; a
 * texture SRV's shader dimension follows its D3D12 view (the DDI names no
 * array dimension: more than one layer of a 1D view, or more than one cube,
 * is an array) */
static void PackViewHeapForm(D3D12DDI_HDEVICE handle, D3D12DDI_CPU_DESCRIPTOR_HANDLE dest, UINT resource_dimension);
static void FillHeapForm(D3D12DDI_HDEVICE handle, D3D12DDI_CPU_DESCRIPTOR_HANDLE dest, UINT resource_dimension) {
    PackViewHeapForm(handle, dest, resource_dimension);
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    MirrorHeapForms(MirroredHeapAt(DeviceOf(handle), reinterpret_cast<const void *>(dest.ptr)),
                    reinterpret_cast<const void *>(dest.ptr), 1u);
#endif
}
static void PackViewHeapForm(D3D12DDI_HDEVICE handle, D3D12DDI_CPU_DESCRIPTOR_HANDLE dest, UINT resource_dimension) {
    auto *view = reinterpret_cast<AgxD3d12Descriptor *>(dest.ptr);
    if (!view) return;
    AgxD3d12HeapForm *form = HeapFormOf(view);
    memset(form, 0, sizeof(*form));
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    AgxD3d12NullTextureDescriptor(form->texture);
    if (UavDescriptorKind(view->kind) && view->heap) {
        /* C47: a UAV's texture and PBE descriptors */
        AgxD3d12Device *device = DeviceOf(handle);
        AGX_D3D12_OWNER *owner = device ? EnsureOwner(device) : nullptr;
        if (owner) PackHeapForm(owner, view);
        return;
    }
    if (view->kind == kDescriptorSrvBuffer && view->heap) {
        /* C47: a buffer SRV's texture-buffer descriptor */
        form->dimension = DXBC_SRV_OTHER;
        AgxD3d12Device *device = DeviceOf(handle);
        AGX_D3D12_OWNER *owner = device ? EnsureOwner(device) : nullptr;
        if (owner) PackHeapForm(owner, view);
        return;
    }
    if (view->kind != kDescriptorSrvTexture || !view->heap) return;
    form->dimension = resource_dimension == D3D12DDI_RD_TEXTURE1D ? (view->layers > 1 ? DXBC_SRV_1D_ARRAY : DXBC_SRV_1D)
                    : resource_dimension == D3D12DDI_RD_TEXTURE3D ? DXBC_SRV_3D
                    : resource_dimension == D3D12DDI_RD_TEXTURECUBE ? (view->layers > 6u ? DXBC_SRV_CUBE_ARRAY : DXBC_SRV_CUBE)
                    : DXBC_SRV_OTHER;
    AgxD3d12Device *device = DeviceOf(handle);
    AGX_D3D12_OWNER *owner = device ? EnsureOwner(device) : nullptr;
    if (owner) PackHeapForm(owner, view);
#else
    (void)handle;
    (void)resource_dimension;
#endif
}
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
/* C47: packs a texture SRV's heap form for its recorded dimension (also when
 * the replay finds its texture re-laid out) */
static void PackHeapForm(AGX_D3D12_OWNER *owner, AgxD3d12Descriptor *view) {
    AgxD3d12HeapForm *form = HeapFormOf(view);
    if (UavDescriptorKind(view->kind)) {
        PackUavHeapForm(owner, view);
        return;
    }
    /* the SRV as OwnerBindings hands it to the owner */
    AGX_D3D12_BUFFER buffer = HeapBuffer(view->heap);
    AGX_D3D12_SRV srv = {};
    srv.Buffer = &buffer;
    if (view->kind == kDescriptorSrvBuffer) {
        srv.Offset = view->offset;
        srv.Bytes = view->bytes;
        srv.ComponentMapping = view->component_mapping;
        srv.Format = view->format;
        unsigned long long key = 0;
        form->key = SUCCEEDED(AgxD3d12OwnerSrvDescriptor(owner, &srv, DXBC_SRV_OTHER, form->texture, &key)) ? key : 0u;
        return;
    }
    srv.Texture = 1;
    srv.Cube = view->cube;
    srv.ComponentMapping = view->component_mapping;
    srv.Format = view->format;
    srv.FirstLevel = view->level;
    srv.Levels = view->levels;
    srv.FirstLayer = view->first_layer;
    srv.Layers = view->layers;
    unsigned long long key = 0;
    form->key = SUCCEEDED(AgxD3d12OwnerSrvDescriptor(owner, &srv, static_cast<uint8_t>(form->dimension), form->texture, &key))
                    ? key : 0u;
}
#endif
static VOID APIENTRY CreateShaderResourceViewInHeap(D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002 *arg,
                                                    D3D12DDI_CPU_DESCRIPTOR_HANDLE dest) {
    CreateShaderResourceView(handle, arg, dest);
    FillHeapForm(handle, dest, arg ? static_cast<UINT>(arg->ResourceDimension) : 0u);
}
static VOID APIENTRY CreateConstantBufferViewInHeap(D3D12DDI_HDEVICE handle, const D3D12DDI_CONSTANT_BUFFER_VIEW_DESC *desc,
                                                    D3D12DDI_CPU_DESCRIPTOR_HANDLE dest) {
    CreateConstantBufferView(handle, desc, dest);
    FillHeapForm(handle, dest, 0u);
}
static VOID APIENTRY CreateUnorderedAccessViewInHeap(D3D12DDI_HDEVICE handle,
                                                     const D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002 *arg,
                                                     D3D12DDI_CPU_DESCRIPTOR_HANDLE dest) {
    CreateUnorderedAccessView(handle, arg, dest);
    FillHeapForm(handle, dest, 0u);
}
/* Descriptors are CPU records, so copies are byte copies of whole
 * descriptors of the heap type's size. */
static VOID APIENTRY CopyDescriptorsSimple(D3D12DDI_HDEVICE handle, UINT count, D3D12DDI_CPU_DESCRIPTOR_HANDLE dest,
                                           D3D12DDI_CPU_DESCRIPTOR_HANDLE src, D3D12DDI_DESCRIPTOR_HEAP_TYPE type) {
    if (count && dest.ptr && src.ptr)
        memmove(reinterpret_cast<void *>(dest.ptr), reinterpret_cast<const void *>(src.ptr),
                static_cast<SIZE_T>(count) * DescriptorBytes(type));
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if (count && dest.ptr && src.ptr && type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        MirrorHeapForms(MirroredHeapAt(DeviceOf(handle), reinterpret_cast<const void *>(dest.ptr)),
                        reinterpret_cast<const void *>(dest.ptr), count);
#else
    (void)handle;
#endif
}
static VOID APIENTRY CopyDescriptors(D3D12DDI_HDEVICE handle, UINT dest_ranges, const D3D12DDI_CPU_DESCRIPTOR_HANDLE *dest_starts,
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
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    /* EXP1855: shader-visible destinations publish their heap forms */
    AgxD3d12Device *device = DeviceOf(handle);
    for (UINT r = 0; type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && dest_starts && r < dest_ranges; ++r)
        MirrorHeapForms(MirroredHeapAt(device, reinterpret_cast<const void *>(dest_starts[r].ptr)),
                        reinterpret_cast<const void *>(dest_starts[r].ptr), dest_sizes ? dest_sizes[r] : 1u);
#else
    (void)handle;
#endif
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
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
        /* EXP1855 (DynamicIndexing: 7 ms per frame walking descriptors in
         * write-combined memory): CPU handles name a cached shadow; the GPU
         * buffer receives the heap forms (MirrorHeapForms) */
        if (SUCCEEDED(hr) && heap->memory.native) {
            void *shadow = VirtualAlloc(nullptr, static_cast<SIZE_T>(heap->memory.bytes), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!shadow) {
                ReleaseHeap(device, &heap->memory);
                heap->cpu = nullptr;
                hr = E_OUTOFMEMORY;
            } else {
                heap->memory.mirror = static_cast<unsigned char *>(heap->cpu);
                heap->memory.cpu = heap->cpu = shadow;
            }
        }
#endif
        heap->gpuva = SUCCEEDED(hr) ? heap->memory.gpuva : 0;
    } else {
        heap->cpu = VirtualAlloc(nullptr, static_cast<SIZE_T>(bytes), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!heap->cpu) hr = E_OUTOFMEMORY;
    }
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    /* C47: unpopulated entries read as Asahi's null texture */
    if (SUCCEEDED(hr) && heap->cpu && arg->Type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        for (UINT i = 0; i < heap->descriptors; ++i)
            AgxD3d12NullTextureDescriptor(HeapFormOf(static_cast<BYTE *>(heap->cpu) + static_cast<SIZE_T>(i) * kCbvSrvUavDescriptorBytes)->texture);
    if (SUCCEEDED(hr) && heap->memory.mirror) {
        if (arg->Type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
            memcpy(heap->memory.mirror, heap->cpu, static_cast<SIZE_T>(heap->memory.bytes));
            AcquireSRWLockExclusive(&device->heap_lock);
            if (device->mirrored_count < ARRAYSIZE(device->mirrored)) device->mirrored[device->mirrored_count++] = &heap->memory;
            else hr = E_OUTOFMEMORY;
            ReleaseSRWLockExclusive(&device->heap_lock);
        }
        if (FAILED(hr)) {
            VirtualFree(heap->memory.cpu, 0, MEM_RELEASE);
            heap->memory.cpu = heap->memory.mirror;
            heap->memory.mirror = nullptr;
            ReleaseHeap(device, &heap->memory);
            heap->cpu = nullptr;
            heap->gpuva = 0;
        }
    }
#endif
    const UINT receipt[] = {heap->type, heap->descriptors, static_cast<UINT>(arg->Flags), heap->gpuva != 0};
    AdmissionUmdDiagnostic("measure-d3d12-descriptor-heap", hr, receipt, 4);
    return hr;
}
static VOID APIENTRY DestroyDescriptorHeap(D3D12DDI_HDEVICE handle, D3D12DDI_HDESCRIPTORHEAP heap_handle) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *heap = static_cast<AgxD3d12DescriptorHeap *>(heap_handle.pDrvPrivate);
    if (!device || !heap) return;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if (heap->memory.mirror) {
        AcquireSRWLockExclusive(&device->heap_lock);
        for (UINT i = 0; i < device->mirrored_count; ++i)
            if (device->mirrored[i] == &heap->memory) {
                device->mirrored[i] = device->mirrored[--device->mirrored_count];
                break;
            }
        ReleaseSRWLockExclusive(&device->heap_lock);
        VirtualFree(heap->memory.cpu, 0, MEM_RELEASE);
        heap->memory.cpu = heap->memory.mirror; /* the buffer's own mapping */
        heap->memory.mirror = nullptr;
    }
#endif
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
    AgxD3d12Device *device;
};
static void ReportRejectedQueueError(D3D12DDI_HCOMMANDQUEUE handle, HRESULT error) {
    auto *queue = static_cast<AgxD3d12CommandQueue *>(handle.pDrvPrivate);
    if (!queue) return;
    D3D12DDI_HDEVICE device = {};
    device.pDrvPrivate = queue->device;
    ReportRejectedDeviceError(device, error);
}
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
/* C53 B3 (D3D12-C53-tiled-buffers-plan.md): tile mappings of reserved
 * buffers, applied CPU-synchronously under the owner lock in call order (the
 * owner submits recorded work and retires the submissions using the buffer
 * before a remap). Unreachable until the tier flip. */
struct AgxTileEmit {
    AGX_D3D12_OWNER *owner;
    AgxD3d12Heap *reserved, *heap;
    HRESULT hr;
};
static int EmitTileRun(void *context, const AGX_TILE_RUN *run) {
    auto *e = static_cast<AgxTileEmit *>(context);
    const AGX_D3D12_BUFFER reserved = HeapBuffer(e->reserved);
    const AGX_D3D12_BUFFER backing = e->heap ? HeapBuffer(e->heap) : AGX_D3D12_BUFFER{};
    const bool null = run->Kind == AGX_TILE_RANGE_NULL, reuse = run->Kind == AGX_TILE_RANGE_REUSE;
    if (e->reserved->geometry) {
        /* C53 T2D: a texture tile at a time (its pages are not contiguous) */
        for (UINT t = 0; t < run->Tiles; ++t) {
            const UINT heap_tile = null ? 0u : reuse ? run->HeapTile : run->HeapTile + t;
            unsigned slice = 0, mip = 0, x = 0, y = 0;
            int packed = 0;
            if (!AgxTileLocate(e->reserved->geometry, run->ResourceTile + t, &slice, &mip, &x, &y, &packed)) {
                e->hr = E_INVALIDARG;
                return 0;
            }
            e->hr = AgxD3d12OwnerMapReservedTextureTile(e->owner, &reserved, slice, packed ? TRUE : FALSE, mip, x, y,
                                                        null ? nullptr : &backing, heap_tile);
            if (FAILED(e->hr)) return 0;
            e->reserved->tiles[run->ResourceTile + t] = null ? AgxD3d12Heap::AgxD3d12TileMapping{}
                                                             : AgxD3d12Heap::AgxD3d12TileMapping{e->heap, heap_tile};
        }
        return 1;
    }
    for (UINT t = 0; t < run->Tiles;) {
        const UINT n = reuse ? 1u : run->Tiles; /* a shared tile maps one resource tile at a time */
        const UINT heap_tile = reuse ? run->HeapTile : run->HeapTile + t;
        e->hr = AgxD3d12OwnerMapReservedTiles(e->owner, &reserved, run->ResourceTile + t, n, null ? nullptr : &backing,
                                              null ? 0u : heap_tile);
        if (FAILED(e->hr)) return 0;
        for (UINT i = 0; i < n; ++i)
            e->reserved->tiles[run->ResourceTile + t + i] = null ? AgxD3d12Heap::AgxD3d12TileMapping{}
                                                                 : AgxD3d12Heap::AgxD3d12TileMapping{e->heap, heap_tile + i};
        t += n;
    }
    return 1;
}
static VOID APIENTRY UpdateTileMappings(D3D12DDI_HCOMMANDQUEUE handle, D3D12DDI_HRESOURCE resource_handle, UINT regions,
                                        const D3D12DDI_TILED_RESOURCE_COORDINATE *starts,
                                        const D3D12DDI_TILE_REGION_SIZE *sizes, D3D12DDI_HHEAP heap_handle, UINT ranges,
                                        const D3D12DDI_TILE_RANGE_FLAGS *flags, const UINT *heap_starts, const UINT *counts,
                                        D3D12DDI_TILE_MAPPING_FLAGS) {
    auto *queue = static_cast<AgxD3d12CommandQueue *>(handle.pDrvPrivate);
    auto *resource = static_cast<AgxD3d12Resource *>(resource_handle.pDrvPrivate);
    auto *heap = static_cast<AgxD3d12Heap *>(heap_handle.pDrvPrivate);
    AgxD3d12Device *device = queue ? queue->device : nullptr;
    AGX_D3D12_OWNER *owner = device ? static_cast<AGX_D3D12_OWNER *>(device->owner) : nullptr;
    AgxD3d12Heap *reserved = resource ? resource->heap : nullptr;
    HRESULT hr = owner && reserved && reserved->tiles && (!heap || heap->native) && regions && ranges &&
                 regions <= 0x10000u && ranges <= 0x10000u ? S_OK : E_INVALIDARG;
    AGX_TILE_COORD *coord = SUCCEEDED(hr) && starts ? static_cast<AGX_TILE_COORD *>(calloc(regions, sizeof(*coord))) : nullptr;
    AGX_TILE_REGION *size = SUCCEEDED(hr) && sizes ? static_cast<AGX_TILE_REGION *>(calloc(regions, sizeof(*size))) : nullptr;
    unsigned *kind = SUCCEEDED(hr) && flags ? static_cast<unsigned *>(calloc(ranges, sizeof(*kind))) : nullptr;
    if (SUCCEEDED(hr) && ((starts && !coord) || (sizes && !size) || (flags && !kind))) hr = E_OUTOFMEMORY;
    for (UINT i = 0; SUCCEEDED(hr) && i < regions; ++i) {
        if (coord) coord[i] = {starts[i].X, starts[i].Y, starts[i].Z, starts[i].Subresource};
        if (size) size[i] = {sizes[i].NumTiles, sizes[i].UseBox ? 1u : 0u, sizes[i].Width, sizes[i].Height, sizes[i].Depth};
    }
    for (UINT i = 0; SUCCEEDED(hr) && kind && i < ranges; ++i) kind[i] = static_cast<unsigned>(flags[i]);
    if (SUCCEEDED(hr)) {
        AgxTileEmit emit = {owner, reserved, heap, S_OK};
        AgxD3d12OwnerLock(owner);
        const unsigned heap_tiles = heap ? static_cast<unsigned>(heap->bytes / 0x10000ull) : 0u;
        const int walked = reserved->geometry
            ? AgxTileMappingWalkGeometry(reserved->geometry, regions, coord, size, ranges, kind, heap_starts, counts,
                                         heap_tiles, EmitTileRun, &emit)
            : AgxTileMappingWalk(reserved->tile_count, regions, coord, size, ranges, kind, heap_starts, counts, heap_tiles,
                                 EmitTileRun, &emit);
        AgxD3d12OwnerUnlock(owner);
        hr = walked ? S_OK : FAILED(emit.hr) ? emit.hr : E_INVALIDARG;
    }
    free(coord);
    free(size);
    free(kind);
    const UINT receipt[] = {regions, ranges, reserved ? reserved->tile_count : 0u, heap ? 1u : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-update-tiles", hr, receipt, 4);
    if (FAILED(hr)) ReportRejectedQueueError(handle, hr);
}
/* The tiling of a reserved resource: its texture geometry, or one row of
 * 64 KiB tiles for a buffer. */
static AGX_TILE_GEOMETRY ReservedGeometry(const AgxD3d12Heap *heap) {
    if (heap->geometry) return *heap->geometry;
    AGX_TILE_GEOMETRY buffer = {1u, 1u, 1u, 0u, {heap->tile_count}, {1u}};
    return buffer;
}
static VOID APIENTRY CopyTileMappings(D3D12DDI_HCOMMANDQUEUE handle, D3D12DDI_HRESOURCE dst_handle,
                                      const D3D12DDI_TILED_RESOURCE_COORDINATE *dst_start, D3D12DDI_HRESOURCE src_handle,
                                      const D3D12DDI_TILED_RESOURCE_COORDINATE *src_start,
                                      const D3D12DDI_TILE_REGION_SIZE *size, D3D12DDI_TILE_MAPPING_FLAGS) {
    auto *queue = static_cast<AgxD3d12CommandQueue *>(handle.pDrvPrivate);
    auto *dst_resource = static_cast<AgxD3d12Resource *>(dst_handle.pDrvPrivate);
    auto *src_resource = static_cast<AgxD3d12Resource *>(src_handle.pDrvPrivate);
    AgxD3d12Device *device = queue ? queue->device : nullptr;
    AGX_D3D12_OWNER *owner = device ? static_cast<AGX_D3D12_OWNER *>(device->owner) : nullptr;
    AgxD3d12Heap *dst = dst_resource ? dst_resource->heap : nullptr, *src = src_resource ? src_resource->heap : nullptr;
    const UINT tiles = size ? size->NumTiles : 0u;
    HRESULT hr = owner && dst && src && dst->tiles && src->tiles && dst_start && src_start && tiles ? S_OK : E_INVALIDARG;
    /* C53 T2D: both regions walk their own resource's tile order (a box is
     * rows of its start's standard mip) */
    const AGX_TILE_GEOMETRY dst_geometry = SUCCEEDED(hr) ? ReservedGeometry(dst) : AGX_TILE_GEOMETRY{};
    const AGX_TILE_GEOMETRY src_geometry = SUCCEEDED(hr) ? ReservedGeometry(src) : AGX_TILE_GEOMETRY{};
    const AGX_TILE_COORD dst_at = dst_start ? AGX_TILE_COORD{dst_start->X, dst_start->Y, dst_start->Z, dst_start->Subresource}
                                            : AGX_TILE_COORD{};
    const AGX_TILE_COORD src_at = src_start ? AGX_TILE_COORD{src_start->X, src_start->Y, src_start->Z, src_start->Subresource}
                                            : AGX_TILE_COORD{};
    const AGX_TILE_REGION region = size ? AGX_TILE_REGION{size->NumTiles, size->UseBox ? 1u : 0u, size->Width, size->Height,
                                                          size->Depth}
                                        : AGX_TILE_REGION{};
    /* the source snapshot first, with the destination indices: the regions
     * may overlap (one resource) */
    AgxD3d12Heap::AgxD3d12TileMapping *snapshot = SUCCEEDED(hr) ? static_cast<AgxD3d12Heap::AgxD3d12TileMapping *>(
        calloc(tiles, sizeof(AgxD3d12Heap::AgxD3d12TileMapping))) : nullptr;
    UINT *targets = SUCCEEDED(hr) ? static_cast<UINT *>(calloc(tiles, sizeof(UINT))) : nullptr;
    if (SUCCEEDED(hr) && (!snapshot || !targets)) hr = E_OUTOFMEMORY;
    for (UINT k = 0; SUCCEEDED(hr) && k < tiles; ++k) {
        unsigned long long s = 0, d = 0;
        if (!AgxTileRegionTile(&src_geometry, &src_at, &region, k, &s) ||
            !AgxTileRegionTile(&dst_geometry, &dst_at, &region, k, &d) || s >= src->tile_count || d >= dst->tile_count) {
            hr = E_INVALIDARG;
            break;
        }
        snapshot[k] = src->tiles[s];
        targets[k] = static_cast<UINT>(d);
    }
    if (SUCCEEDED(hr)) {
        AgxD3d12OwnerLock(owner);
        for (UINT k = 0; SUCCEEDED(hr) && k < tiles;) {
            /* runs of consecutive destination tiles onto consecutive heap tiles */
            UINT n = 1;
            while (k + n < tiles && targets[k + n] == targets[k] + n && snapshot[k + n].heap == snapshot[k].heap &&
                   (!snapshot[k].heap || snapshot[k + n].tile == snapshot[k].tile + n)) ++n;
            AgxTileEmit emit = {owner, dst, snapshot[k].heap, S_OK};
            const AGX_TILE_RUN run = {targets[k], n,
                                      static_cast<unsigned>(snapshot[k].heap ? AGX_TILE_RANGE_HEAP : AGX_TILE_RANGE_NULL),
                                      snapshot[k].tile};
            hr = EmitTileRun(&emit, &run) ? S_OK : emit.hr;
            k += n;
        }
        AgxD3d12OwnerUnlock(owner);
    }
    free(snapshot);
    free(targets);
    const UINT receipt[] = {tiles, dst_start ? dst_start->X : 0u, src_start ? src_start->X : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-copy-tile-mappings", hr, receipt, 3);
    if (FAILED(hr)) ReportRejectedQueueError(handle, hr);
}
/* C53 T2D: D3D12_PACKED_MIP_INFO's NumPackedMips and NumTilesForPackedMips
 * (per array slice) of a reserved texture; 0 and 0 for anything else */
static VOID APIENTRY GetMipPacking(D3D12DDI_HDEVICE, D3D12DDI_HRESOURCE resource_handle, UINT *packed_mips,
                                   UINT *packed_tiles) {
    const auto *resource = static_cast<const AgxD3d12Resource *>(resource_handle.pDrvPrivate);
    const AGX_TILE_GEOMETRY *geometry = resource && resource->heap ? resource->heap->geometry : nullptr;
    if (packed_mips) *packed_mips = geometry ? geometry->MipLevels - geometry->StandardMips : 0u;
    if (packed_tiles) *packed_tiles = geometry ? geometry->PackedTiles : 0u;
}
#endif
static SIZE_T APIENTRY CalcPrivateCommandQueueSize(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATECOMMANDQUEUE_0001 *) {
    return sizeof(AgxD3d12CommandQueue);
}
static HRESULT APIENTRY CreateCommandQueue(D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATECOMMANDQUEUE_0001 *arg) {
    AgxD3d12Device *device = DeviceOf(handle);
    auto *queue = arg ? static_cast<AgxD3d12CommandQueue *>(arg->hDrvCommandQueue.pDrvPrivate) : nullptr;
    if (!device || !queue || !device->callbacks || !device->callbacks->pfnCreateContextVirtualCb) return E_INVALIDARG;
    memset(queue, 0, sizeof(*queue));
    queue->device = device;
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
static constexpr UINT kSrvSlots = 128; /* Tier1 SRVs t0..t127, matching Asahi sampler views */
static constexpr UINT kSamplerSlots = 16; /* C6e: s0..s15 */
static constexpr UINT kRootArguments = 64; /* a root signature costs at most 64 DWORDs */
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static_assert(kCbSlots == AGX_D3D12_CBV_SLOTS, "constant buffer slots of the owner");
static_assert(kSrvSlots == AGX_D3D12_SRV_SLOTS, "SRV slots of the owner");
static_assert(kSamplerSlots == AGX_D3D12_SAMPLER_SLOTS, "sampler slots of the owner");
#endif
/* C14: an ExecuteIndirect argument stream (args NULL = a direct command):
 * max_count records of stride bytes at args + args_offset, limited by the
 * UINT at count + count_offset when count is set */
struct AgxD3d12Indirect {
    AgxD3d12Heap *args;
    UINT64 args_offset;
    UINT stride, max_count;
    AgxD3d12Heap *count;
    UINT64 count_offset;
};
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
    AgxD3d12Indirect indirect; /* C14 (dispatch commands only) */
    /* C47: heap ranges -- the shader-visible CBV/SRV/UAV heap memory and the
     * heap index of each range's first register */
    AgxD3d12Heap *descriptors;
    UINT heap_first[kHeapRanges];
    UINT heap_ranges;
    AgxD3d12Heap *sampler_descriptors; /* C47: the shader-visible sampler heap of heap sampler ranges */
    UINT64 heap_sampler_mask;          /* the heap ranges that are sampler ranges */
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
/* C39: a stream-output view: the target range and its filled-size location */
static constexpr UINT kSoTargets = 4;
struct AgxD3d12SoTarget {
    AgxD3d12Heap *heap, *filled;
    UINT64 offset, size, filled_offset;
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
    AgxD3d12Indirect indirect; /* C14 */
    AgxD3d12SoTarget so[kSoTargets]; /* C39 */
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
/* C10: a recorded ClearUnorderedAccessView{Uint,Float} (the CPU descriptor
 * is read at record time) */
struct AgxD3d12ClearUav {
    AgxD3d12Descriptor view;
    UINT values[4]; /* UINTs, or FLOAT bits when is_float */
    UINT is_float;
    UINT rects;
    RECT rect[kClearRects];
};
/* C11: a query heap (D3D12-C11-queries.md) and a recorded Begin/End/Resolve */
struct AgxD3d12QueryHeap {
    UINT type; /* D3D12DDI_QUERY_HEAP_TYPE: occlusion 0, timestamp 1, pipeline statistics 2 */
    UINT count;
    void *owner; /* AGX_D3D12_QUERY_HEAP */
};
enum : UINT { kQueryBegin = 0, kQueryEnd = 1, kQueryResolve = 2 };
struct AgxD3d12QueryCommand {
    AgxD3d12QueryHeap *heap;
    UINT op, type, index, count;
    AgxD3d12Heap *dst;
    UINT64 dst_offset;
};
/* C8b: a recorded texture-to-texture copy (whole: every common mip, all
 * layers, full extents) */
struct AgxD3d12TextureRegionCopy {
    AgxD3d12Heap *dst, *src;
    UINT dst_level, dst_layer, dst_x, dst_y;
    UINT src_level, src_layer, src_x, src_y;
    UINT width, height, layers;
    UINT whole_levels; /* > 0: ResourceCopy of this many levels */
    UINT volume; /* C21: minify depth per whole-resource mip */
};
enum : UINT { kCommandCopy = 1, kCommandDispatch = 2, kCommandClear = 3, kCommandTextureCopy = 4, kCommandDraw = 5,
              kCommandClearDepth = 6, kCommandTextureRegionCopy = 7, kCommandClearUav = 8,
              kCommandQuery = 9, kCommandGsBind = 10 /* C27: the GS bindings of the next draw */,
              kCommandIndirectExpand = 11 /* C31: ExecuteIndirect with root or IA arguments */,
              kCommandResolve = 12 /* C32: ResolveSubresource */,
              kCommandHsBind = 13, kCommandDsBind = 14 /* C37: the HS and DS bindings of the next draw */ };
/* C32: a recorded ResolveSubresource (subresources decoded at record time) */
struct AgxD3d12Resolve {
    AgxD3d12Heap *dst, *src;
    UINT dst_level, dst_layer, src_layer;
    DXGI_FORMAT format;
};
/* C31: list->indirect_states[state] holds the list state and the command
 * signature at the ExecuteIndirect call */
struct AgxD3d12IndirectExpand {
    UINT state;
    AgxD3d12Indirect indirect;
};
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
        AgxD3d12ClearUav clear_uav;
        AgxD3d12QueryCommand query;
        AgxD3d12IndirectExpand indirect_expand;
        AgxD3d12Resolve resolve;
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
    AgxD3d12SoTarget so[kSoTargets]; /* C39 */
    FLOAT blend_factor[4];
    UINT stencil_ref;
    /* C6d: the index buffer view */
    AgxD3d12Heap *index_heap;
    UINT64 index_offset;
    UINT index_size;
    AgxD3d12Command *command;
    UINT capacity;
    /* C30: a bundle's call log (BundleAppend), replayed by ExecuteBundle */
    unsigned char *bundle;
    SIZE_T bundle_bytes, bundle_capacity;
    /* C31: ExecuteIndirect state snapshots; replay = a scratch list of the
     * queue's replay, which reports no runtime list errors */
    struct AgxD3d12IndirectState *indirect_states;
    UINT indirect_state_count, indirect_state_capacity;
    UINT replay;
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
    if (!list) return;
    if (list->bundle) HeapFree(GetProcessHeap(), 0, list->bundle);
    list->bundle = nullptr;
    list->bundle_bytes = list->bundle_capacity = 0;
    if (list->indirect_states) HeapFree(GetProcessHeap(), 0, list->indirect_states);
    list->indirect_states = nullptr;
    list->indirect_state_count = list->indirect_state_capacity = 0;
    if (!list->command) return;
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
    list->bundle_bytes = 0;
    list->indirect_state_count = 0;
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
    memset(list->so, 0, sizeof(list->so));
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
    if (list && !list->replay && list->device && list->device->callbacks && list->device->callbacks->pfnSetCommandListErrorCb)
        list->device->callbacks->pfnSetCommandListErrorCb(list->runtime, error);
}
static void ReportRejectedListError(D3D12DDI_HCOMMANDLIST handle, HRESULT error) {
    ListError(static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate), error);
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
    /* EXP1374: slots are reused across Reset and grown without zeroing;
     * recorders set only the fields their command uses (a direct draw left
     * the C14 indirect stream with a previous command's bytes), so every
     * slot starts zeroed */
    AgxD3d12Command *slot = &list->command[list->commands++];
    memset(slot, 0, sizeof(*slot));
    return slot;
}
/* C30 (EXP1825: HelloBundles and Bundles stop on ExecuteBundle): a bundle
 * records its calls, not commands. ExecuteBundle replays them through the
 * same DDIs into the calling list, so a bundle draw sees the caller's
 * render targets, viewports, scissors, blend factor, stencil reference and
 * root arguments, and the state the bundle sets stays set in the caller
 * afterwards -- the D3D12 bundle inheritance contract. A record is the
 * replay function and its arguments copied by value, 16-byte aligned. */
typedef void (*AgxD3d12BundleReplay)(D3D12DDI_HCOMMANDLIST, const unsigned char *);
struct AgxD3d12BundleCall {
    AgxD3d12BundleReplay replay;
    SIZE_T bytes; /* the whole record */
};
static constexpr SIZE_T kBundleHeader = (sizeof(AgxD3d12BundleCall) + 15u) & ~static_cast<SIZE_T>(15u);
static bool IsBundle(const AgxD3d12CommandList *list) {
    return list && list->type == static_cast<UINT>(D3D12DDI_COMMAND_LIST_TYPE_BUNDLE);
}
static unsigned char *BundleAppend(AgxD3d12CommandList *list, AgxD3d12BundleReplay replay, SIZE_T payload) {
    if (payload > 0x10000u) {
        ListError(list, E_INVALIDARG);
        return nullptr;
    }
    const SIZE_T record = kBundleHeader + ((payload + 15u) & ~static_cast<SIZE_T>(15u));
    if (list->bundle_capacity - list->bundle_bytes < record) {
        SIZE_T capacity = list->bundle_capacity ? list->bundle_capacity : 4096u;
        while (capacity - list->bundle_bytes < record) capacity *= 2u;
        void *grown = list->bundle ? HeapReAlloc(GetProcessHeap(), 0, list->bundle, capacity)
                                   : HeapAlloc(GetProcessHeap(), 0, capacity);
        if (!grown) {
            ListError(list, E_OUTOFMEMORY);
            return nullptr;
        }
        list->bundle = static_cast<unsigned char *>(grown);
        list->bundle_capacity = capacity;
    }
    unsigned char *at = list->bundle + list->bundle_bytes;
    list->bundle_bytes += record;
    AgxD3d12BundleCall call = {replay, record};
    memcpy(at, &call, sizeof(call));
    return at + kBundleHeader;
}
static VOID APIENTRY CopyBufferRegion(D3D12DDI_HCOMMANDLIST handle, D3D12DDIARG_BUFFER_PLACEMENT dst,
                                      D3D12DDIARG_BUFFER_PLACEMENT src, UINT64 bytes) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    auto *d = static_cast<AgxD3d12Resource *>(dst.BaseAddress.UMD.hResource.pDrvPrivate);
    auto *r = static_cast<AgxD3d12Resource *>(src.BaseAddress.UMD.hResource.pDrvPrivate);
    if (!list) return;
    const UINT64 dst_offset = dst.BaseAddress.UMD.Offset, src_offset = src.BaseAddress.UMD.Offset;
    if (!d || !r || !d->heap || !r->heap || !d->heap->native || !r->heap->native || !bytes ||
        d->type != D3D12DDI_RT_BUFFER || r->type != D3D12DDI_RT_BUFFER || dst_offset > d->bytes || bytes > d->bytes - dst_offset || src_offset > r->bytes ||
        bytes > r->bytes - src_offset || list->commands >= kListCommands) {
        ListError(list, E_INVALIDARG);
        return;
    }
    AgxD3d12Command *slot = AppendCommand(list);
    if (!slot) return;
    AgxD3d12Command &command = *slot;
    command.kind = kCommandCopy;
    /* offsets are resource-relative; a placed resource starts inside its
     * heap (EXP1371 C8C: copying from the heap start read the wrong bytes) */
    command.copy = {d->heap, d->gpuva - d->heap->gpuva + dst_offset, r->heap, r->gpuva - r->heap->gpuva + src_offset, bytes};
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
/* C32: ResolveSubresource of a multisampled 2D subresource into a
 * single-sampled one of the same size; the owner checks sample counts. */
static VOID APIENTRY ResolveSubresource(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HRESOURCE dst_handle, UINT dst_sub,
                                        D3D12DDI_HRESOURCE src_handle, UINT src_sub, DXGI_FORMAT format) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    auto *d = static_cast<AgxD3d12Resource *>(dst_handle.pDrvPrivate);
    auto *s = static_cast<AgxD3d12Resource *>(src_handle.pDrvPrivate);
    const UINT refusal = !d || !s || !d->heap || !s->heap || !d->heap->native || !s->heap->native ? 1u
                       : d->type != D3D12DDI_RT_TEXTURE2D || s->type != D3D12DDI_RT_TEXTURE2D ? 4u
                       : dst_sub >= d->levels * d->layers || src_sub >= s->levels * s->layers ? 5u
                       : list->commands >= kListCommands ? 3u : 0u;
    const UINT receipt[] = {refusal, dst_sub, src_sub, static_cast<UINT>(format)};
    AdmissionUmdDiagnostic("measure-d3d12-resolve-record", refusal ? E_INVALIDARG : S_OK, receipt, 4);
    if (refusal) {
        ListError(list, E_INVALIDARG);
        return;
    }
    AgxD3d12Command *slot = AppendCommand(list);
    if (!slot) return;
    slot->kind = kCommandResolve;
    AgxD3d12Resolve &r = slot->resolve;
    r.dst = d->heap;
    r.src = s->heap;
    r.dst_level = dst_sub % d->levels;
    r.dst_layer = dst_sub / d->levels;
    r.src_layer = src_sub / s->levels;
    r.format = format;
}
static VOID APIENTRY ResourceCopy(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HRESOURCE dst_handle, D3D12DDI_HRESOURCE src_handle) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    auto *d = static_cast<AgxD3d12Resource *>(dst_handle.pDrvPrivate);
    auto *s = static_cast<AgxD3d12Resource *>(src_handle.pDrvPrivate);
    if(d && s && (d->type==D3D12DDI_RT_TEXTURE3D || s->type==D3D12DDI_RT_TEXTURE3D || d->type==D3D12DDI_RT_TEXTURE1D || s->type==D3D12DDI_RT_TEXTURE1D)) {
        if(!d->heap || !s->heap || !d->heap->native || !s->heap->native || d->type!=s->type || d->format!=s->format || d->width!=s->width ||
            d->height!=s->height || d->layers!=s->layers || d->levels!=s->levels || list->commands>=kListCommands) {
            ListError(list,E_INVALIDARG);return;
        }
        AgxD3d12Command *slot=AppendCommand(list);if(!slot)return;
        slot->kind=kCommandTextureRegionCopy;auto &r=slot->region_copy;r.dst=d->heap;r.src=s->heap;
        r.width=s->width;r.height=s->height;r.layers=s->layers;r.whole_levels=s->levels;r.volume=s->type==D3D12DDI_RT_TEXTURE3D;return;
    }
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
/* C7c (D3D12-C7-present.md): DXGI presents a swapchain buffer through the
 * runtime's list; the runtime submits the kernel present with the returned
 * source allocation on the returned context. ExecuteCommandLists retires
 * the owner's work before it returns, so the buffer's rendering is complete
 * here; the owner flushes for present and publishes the buffer's direct map
 * (the D3D11 present order). No GPU work is added and no private data is
 * needed (the D3D11 pfnPresentCb carries none). */
static UINT APIENTRY GetPresentPrivateDriverDataSize(D3D12DDI_HDEVICE, const D3D12DDIARG_PRESENT_0001 *) {
    return 0;
}
static VOID APIENTRY Present(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HCOMMANDQUEUE queue_handle,
                             const D3D12DDIARG_PRESENT_0001 *arg, D3D12DDI_PRESENT_0003 *out) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    auto *queue = static_cast<AgxD3d12CommandQueue *>(queue_handle.pDrvPrivate);
    if (out) memset(out, 0, sizeof(*out));
    const AgxD3d12Resource *source = arg && arg->SurfacesToPresent == 1 && arg->phSurfacesToPresent
                                         ? static_cast<const AgxD3d12Resource *>(arg->phSurfacesToPresent[0].hSurface.pDrvPrivate)
                                         : nullptr;
    UINT reason = !list || !list->device || !queue || !queue->context || !arg || !out ? 1u
                : !source || !source->heap || !source->heap->presentation ? 2u
                : arg->phSurfacesToPresent[0].SubResourceIndex != 0 ? 3u : 0u;
    UINT allocation = 0;
    HRESULT hr = reason ? E_INVALIDARG : E_NOTIMPL;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if (!reason) {
        AGX_D3D12_OWNER *owner = static_cast<AGX_D3D12_OWNER *>(list->device->owner);
        hr = owner ? AgxD3d12OwnerPreparePresent(owner, source->heap->presentation, &allocation) : E_INVALIDARG;
        if (FAILED(hr)) reason = 4u;
    }
#else
    if (!reason) reason = 5u;
#endif
    if (!reason) {
        out->hSrcAllocation = allocation;
        out->hContext = queue->context;
        out->AddedGpuWork = FALSE;
        out->BackBufferMultiplicity = 1;
    }
    const UINT receipt[] = {reason, arg ? arg->SurfacesToPresent : 0u, arg ? arg->Flags.Value : 0u,
                            arg ? static_cast<UINT>(arg->FlipInterval) : 0u, allocation,
                            arg && arg->hDstResource.pDrvPrivate ? 1u : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-present", reason ? hr : S_OK, receipt, 6);
    if (reason && list) ListError(list, hr);
}
/* C10 (D3D12-C10-uav-clears.md): UAV clears record the CPU descriptor
 * (untyped, typed buffer or texture view) with the values and rects. */
static void RecordClearUav(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_CPU_DESCRIPTOR_HANDLE cpu, const void *values,
                           UINT is_float, UINT rects, const D3D12DDI_RECT *rect) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    const auto *view = reinterpret_cast<const AgxD3d12Descriptor *>(cpu.ptr);
    const bool uav = view && (view->kind == kDescriptorUavBuffer || view->kind == kDescriptorUavTypedBuffer ||
                              view->kind == kDescriptorUavTexture);
    const UINT refusal = !uav || !view->heap || !values ? 1u
                       : rects > kClearRects || (rects && !rect) ? 2u
                       : is_float && view->kind == kDescriptorUavBuffer ? 4u /* float clears need a format */
                       : list->commands >= kListCommands ? 3u : 0u;
    const UINT receipt[] = {refusal, view ? view->kind : 0u, is_float, rects};
    AdmissionUmdDiagnostic("measure-d3d12-clear-uav", refusal ? E_INVALIDARG : S_OK, receipt, 4);
    if (refusal) {
        ListError(list, E_INVALIDARG);
        return;
    }
    AgxD3d12Command *slot = AppendCommand(list);
    if (!slot) return;
    slot->kind = kCommandClearUav;
    AgxD3d12ClearUav &clear = slot->clear_uav;
    clear.view = *view;
    memcpy(clear.values, values, sizeof(clear.values));
    clear.is_float = is_float;
    clear.rects = rects;
    for (UINT i = 0; i < rects; ++i) {
        clear.rect[i].left = rect[i].left;
        clear.rect[i].top = rect[i].top;
        clear.rect[i].right = rect[i].right;
        clear.rect[i].bottom = rect[i].bottom;
    }
}
static VOID APIENTRY ClearUnorderedAccessViewUint(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_GPU_DESCRIPTOR_HANDLE,
                                                  D3D12DDI_CPU_DESCRIPTOR_HANDLE cpu, D3D12DDI_HRESOURCE,
                                                  const UINT values[4], UINT rects, const D3D12DDI_RECT *rect) {
    RecordClearUav(handle, cpu, values, 0u, rects, rect);
}
static VOID APIENTRY ClearUnorderedAccessViewFloat(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_GPU_DESCRIPTOR_HANDLE,
                                                   D3D12DDI_CPU_DESCRIPTOR_HANDLE cpu, D3D12DDI_HRESOURCE,
                                                   const FLOAT values[4], UINT rects, const D3D12DDI_RECT *rect) {
    RecordClearUav(handle, cpu, values, 1u, rects, rect);
}
/* C11 (D3D12-C11-queries.md): occlusion (counter and binary), timestamp
 * and pipeline-statistics heaps of owner Gallium queries; Begin/End and
 * Resolve are recorded and replayed in list order. */
static SIZE_T APIENTRY CalcPrivateQueryHeapSize(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_QUERY_HEAP_0001 *) {
    return sizeof(AgxD3d12QueryHeap);
}
static HRESULT APIENTRY CreateQueryHeap(D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATE_QUERY_HEAP_0001 *arg,
                                        D3D12DDI_HQUERYHEAP heap_handle) {
    auto *heap = static_cast<AgxD3d12QueryHeap *>(heap_handle.pDrvPrivate);
    AgxD3d12Device *device = DeviceOf(handle);
    if (!heap || !arg || !device) return E_INVALIDARG;
    memset(heap, 0, sizeof(*heap));
    const UINT type = static_cast<UINT>(arg->Type);
    HRESULT hr = type > static_cast<UINT>(D3D12DDI_QUERY_HEAP_TYPE_PIPELINE_STATISTICS) || !arg->Count ? E_NOTIMPL : S_OK;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    AGX_D3D12_OWNER *owner = SUCCEEDED(hr) ? EnsureOwner(device) : nullptr;
    AGX_D3D12_QUERY_HEAP *queries = nullptr;
    if (SUCCEEDED(hr)) hr = owner ? AgxD3d12OwnerCreateQueryHeap(owner, type, arg->Count, &queries) : E_NOTIMPL;
    heap->owner = queries;
#else
    if (SUCCEEDED(hr)) hr = E_NOTIMPL;
#endif
    heap->type = type;
    heap->count = SUCCEEDED(hr) ? arg->Count : 0u;
    const UINT receipt[] = {type, arg->Count};
    AdmissionUmdDiagnostic("measure-d3d12-query-heap", hr, receipt, 2);
    return hr;
}
static VOID APIENTRY DestroyQueryHeap(D3D12DDI_HDEVICE handle, D3D12DDI_HQUERYHEAP heap_handle) {
    auto *heap = static_cast<AgxD3d12QueryHeap *>(heap_handle.pDrvPrivate);
    AgxD3d12Device *device = DeviceOf(handle);
    if (!heap) return;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if (device && heap->owner)
        AgxD3d12OwnerDestroyQueryHeap(static_cast<AGX_D3D12_OWNER *>(device->owner), static_cast<AGX_D3D12_QUERY_HEAP *>(heap->owner));
#else
    (void)device;
#endif
    memset(heap, 0, sizeof(*heap));
}
static void RecordQuery(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HQUERYHEAP heap_handle, UINT op, D3D12DDI_QUERY_TYPE type,
                        UINT index, UINT count, AgxD3d12Heap *dst, UINT64 dst_offset) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    auto *heap = static_cast<AgxD3d12QueryHeap *>(heap_handle.pDrvPrivate);
    const UINT refusal = !heap || !heap->owner ? 1u
                       : index > heap->count || (op == kQueryResolve ? count > heap->count - index : index >= heap->count) ? 2u
                       : op == kQueryResolve && !dst ? 4u
                       : list->commands >= kListCommands ? 3u : 0u;
    if (refusal) {
        const UINT receipt[] = {refusal, op, static_cast<UINT>(type), index};
        AdmissionUmdDiagnostic("measure-d3d12-query-refused", E_INVALIDARG, receipt, 4);
        ListError(list, E_INVALIDARG);
        return;
    }
    AgxD3d12Command *slot = AppendCommand(list);
    if (!slot) return;
    slot->kind = kCommandQuery;
    slot->query = {heap, op, static_cast<UINT>(type), index, count, dst, dst_offset};
}
static VOID APIENTRY BeginQuery(D3D12DDI_HCOMMANDLIST list, D3D12DDI_HQUERYHEAP heap, D3D12DDI_QUERY_TYPE type, UINT index) {
    RecordQuery(list, heap, kQueryBegin, type, index, 1u, nullptr, 0);
}
static VOID APIENTRY EndQuery(D3D12DDI_HCOMMANDLIST list, D3D12DDI_HQUERYHEAP heap, D3D12DDI_QUERY_TYPE type, UINT index) {
    RecordQuery(list, heap, kQueryEnd, type, index, 1u, nullptr, 0);
}
static VOID APIENTRY ResolveQueryData(D3D12DDI_HCOMMANDLIST list, D3D12DDI_HQUERYHEAP heap, D3D12DDI_QUERY_TYPE type,
                                      UINT start, UINT count, D3D12DDI_HRESOURCE dst_handle, UINT64 offset) {
    auto *dst = static_cast<AgxD3d12Resource *>(dst_handle.pDrvPrivate);
    const bool buffer = dst && dst->heap && dst->type == D3D12DDI_RT_BUFFER;
    RecordQuery(list, heap, kQueryResolve, type, start, count, buffer ? dst->heap : nullptr,
                buffer ? dst->gpuva - dst->heap->gpuva + offset : 0);
}
/* C10: DiscardResource only declares the contents undefined -- keeping
 * them is a valid implementation; SetMarker is for tools (PIX). */
static VOID APIENTRY DiscardResource(D3D12DDI_HCOMMANDLIST, D3D12DDI_HRESOURCE, const D3D12DDIARG_DISCARD_RESOURCE_0003 *) {}
static VOID APIENTRY SetMarker(D3D12DDI_HCOMMANDLIST, UINT64) {}
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
    if (layout.Layout == D3D12DDI_RL_SELECT_SUBRESOURCE) return side->resource->type == D3D12DDI_RT_TEXTURE2D || side->resource->type == D3D12DDI_RT_TEXTURE3D || side->resource->type == D3D12DDI_RT_TEXTURE1D;
    return side->footprint && side->resource->type != D3D12DDI_RT_TEXTURE2D && side->resource->type != D3D12DDI_RT_TEXTURE3D && side->resource->type != D3D12DDI_RT_TEXTURE1D;
}
/* An array subresource is (mip,slice), not a minified volume Z range. */
static bool LineSubresource(const AgxD3d12Resource *resource,UINT64 index,UINT *level,UINT *layer) {
  if(!resource || resource->type!=D3D12DDI_RT_TEXTURE1D || !resource->levels || !resource->layers ||
     index>=static_cast<UINT64>(resource->levels)*resource->layers)return false;
  *level=static_cast<UINT>(index%resource->levels);*layer=static_cast<UINT>(index/resource->levels);return true;
}
static void CopyLineTextureRegion(AgxD3d12CommandList *list,
    const AgxD3d12CopySide &dst,const AgxD3d12CopySide &src,
    UINT dst_x,UINT dst_y,UINT dst_z,const D3D12DDI_BOX *box) {
  auto fail=[&](){ListError(list,E_INVALIDARG);};
  auto width=[](const AgxD3d12Resource *r,UINT l){return r->width>>l?r->width>>l:1u;};
  if(dst_y || dst_z || (box && (box->Left<0 || box->Right<=box->Left ||
      box->Top || box->Bottom!=1 || box->Front || box->Back!=1))) {fail();return;}
  if(!dst.footprint && !src.footprint) {
    UINT dl=0,ds=0,sl=0,ss=0;
    if(!LineSubresource(dst.resource,dst.offset,&dl,&ds) || !LineSubresource(src.resource,src.offset,&sl,&ss) ||
       dst.resource->format!=src.resource->format) {fail();return;}
    const UINT sx=box?static_cast<UINT>(box->Left):0u;
    const UINT count=box?static_cast<UINT>(box->Right-box->Left):width(src.resource,sl);
    const UINT sw=width(src.resource,sl),dw=width(dst.resource,dl);
    if(sx>sw || count>sw-sx || dst_x>dw || count>dw-dst_x) {fail();return;}
    AgxD3d12Command *slot=AppendCommand(list);if(!slot)return;
    slot->kind=kCommandTextureRegionCopy;auto &r=slot->region_copy;
    r.dst=dst.resource->heap;r.src=src.resource->heap;r.dst_level=dl;r.src_level=sl;
    r.dst_layer=ds;r.src_layer=ss;r.dst_x=dst_x;r.src_x=sx;r.width=count;r.height=r.layers=1;return;
  }
  const bool to_buffer=dst.footprint!=nullptr;
  const auto &t=to_buffer?src:dst;const auto &b=to_buffer?dst:src;UINT level=0,layer=0;
  if(!LineSubresource(t.resource,t.offset,&level,&layer) || !b.footprint ||
     b.resource->type!=D3D12DDI_RT_BUFFER || b.footprint->Format!=t.resource->format ||
     !b.footprint->PhysicalWidth || b.footprint->PhysicalHeight!=1 || b.footprint->PhysicalDepth!=1) {fail();return;}
  const UINT tw=width(t.resource,level),fw=b.footprint->PhysicalWidth,block=AgxD3d12FormatBlockBytes(t.resource->format);
  const UINT sx=box?static_cast<UINT>(box->Left):0u;
  const UINT count=box?static_cast<UINT>(box->Right-box->Left):(to_buffer?tw:fw);
  const UINT tx=to_buffer?sx:dst_x,bx=to_buffer?dst_x:sx;
  if(!block || !count || tx>tw || count>tw-tx || bx>fw || count>fw-bx ||
     b.footprint->Pitch<static_cast<UINT64>(fw)*block || b.resource->gpuva<b.resource->heap->gpuva) {fail();return;}
  const UINT64 base=b.resource->gpuva-b.resource->heap->gpuva;
  const UINT64 point=static_cast<UINT64>(bx)*block,span=static_cast<UINT64>(count)*block;
  if(b.offset>b.resource->bytes || point>b.resource->bytes-b.offset || span>b.resource->bytes-b.offset-point ||
     base>b.resource->heap->bytes || b.offset>b.resource->heap->bytes-base ||
     point>b.resource->heap->bytes-base-b.offset || span>b.resource->heap->bytes-base-b.offset-point) {fail();return;}
  AgxD3d12Command *slot=AppendCommand(list);if(!slot)return;slot->kind=kCommandTextureCopy;auto &c=slot->texture_copy;
  c.texture=t.resource->heap;c.buffer=b.resource->heap;c.level=level;c.layer=layer;c.x=tx;
  c.width=count;c.height=c.depth=1;c.offset=base+b.offset+point;
  c.pitch=b.footprint->Pitch;c.slice_pitch=b.footprint->SlicePitch;c.to_buffer=to_buffer;
}

/* C21 LIMITED volume copy: a volume subresource is one entire mip, never a
 * modulo-decoded array slice. Buffer coordinates use real runtime pitches. */
static void CopyVolumeTextureRegion(AgxD3d12CommandList *list,
    const AgxD3d12CopySide &dst,const AgxD3d12CopySide &src,
    UINT dst_x,UINT dst_y,UINT dst_z,const D3D12DDI_BOX *box) {
  const bool to_buffer=dst.footprint!=nullptr;
  const AgxD3d12CopySide &t=to_buffer?src:dst;
  const AgxD3d12CopySide &b=to_buffer?dst:src;
  auto extent=[](UINT base,UINT level){return base>>level ? base>>level:1u;};
  auto fail=[&](){ListError(list,E_INVALIDARG);};
  if(box && (box->Left<0 || box->Top<0 || box->Front<0 || box->Right<=box->Left ||
      box->Bottom<=box->Top || box->Back<=box->Front)) {fail();return;}
  if(!dst.footprint && !src.footprint) {
    if((dst.resource->type!=D3D12DDI_RT_TEXTURE3D && dst.resource->type!=D3D12DDI_RT_TEXTURE1D) || src.resource->type!=dst.resource->type ||
        dst.offset>=dst.resource->levels || src.offset>=src.resource->levels ||
        dst.resource->format!=src.resource->format) {fail();return;}
    const UINT dl=static_cast<UINT>(dst.offset),sl=static_cast<UINT>(src.offset);
    const UINT sx=box?static_cast<UINT>(box->Left):0u,sy=box?static_cast<UINT>(box->Top):0u,sz=box?static_cast<UINT>(box->Front):0u;
    const UINT w=box?static_cast<UINT>(box->Right-box->Left):extent(src.resource->width,sl);
    const UINT h=box?static_cast<UINT>(box->Bottom-box->Top):extent(src.resource->height,sl);
    const UINT d=box?static_cast<UINT>(box->Back-box->Front):extent(src.resource->layers,sl);
    const UINT sw=extent(src.resource->width,sl),sh=extent(src.resource->height,sl),sd=extent(src.resource->layers,sl);
    const UINT dw=extent(dst.resource->width,dl),dh=extent(dst.resource->height,dl),dd=extent(dst.resource->layers,dl);
    if(sx>sw || w>sw-sx || sy>sh || h>sh-sy || sz>sd || d>sd-sz ||
        dst_x>dw || w>dw-dst_x || dst_y>dh || h>dh-dst_y || dst_z>dd || d>dd-dst_z) {fail();return;}
    AgxD3d12Command *slot=AppendCommand(list);if(!slot)return;
    slot->kind=kCommandTextureRegionCopy;
    auto &r=slot->region_copy;r.dst=dst.resource->heap;r.src=src.resource->heap;
    r.dst_level=dl;r.src_level=sl;r.dst_layer=dst_z;r.src_layer=sz;
    r.dst_x=dst_x;r.dst_y=dst_y;r.src_x=sx;r.src_y=sy;r.width=w;r.height=h;r.layers=d;
    return;
  }
  if((t.resource->type!=D3D12DDI_RT_TEXTURE3D && t.resource->type!=D3D12DDI_RT_TEXTURE1D) || !b.footprint ||
      b.resource->type!=D3D12DDI_RT_BUFFER || t.offset>=t.resource->levels ||
      b.footprint->Format!=t.resource->format || !b.footprint->PhysicalWidth ||
      !b.footprint->PhysicalHeight || !b.footprint->PhysicalDepth) {fail();return;}
  const UINT level=static_cast<UINT>(t.offset),block=AgxD3d12FormatBlockBytes(t.resource->format);
  const UINT tw=extent(t.resource->width,level),th=extent(t.resource->height,level),td=extent(t.resource->layers,level);
  const UINT fw=b.footprint->PhysicalWidth,fh=b.footprint->PhysicalHeight,fd=b.footprint->PhysicalDepth;
  const UINT sx=box?static_cast<UINT>(box->Left):0u,sy=box?static_cast<UINT>(box->Top):0u,sz=box?static_cast<UINT>(box->Front):0u;
  const UINT w=box?static_cast<UINT>(box->Right-box->Left):to_buffer?tw:fw;
  const UINT h=box?static_cast<UINT>(box->Bottom-box->Top):to_buffer?th:fh;
  const UINT d=box?static_cast<UINT>(box->Back-box->Front):to_buffer?td:fd;
  const UINT tx=to_buffer?sx:dst_x,ty=to_buffer?sy:dst_y,tz=to_buffer?sz:dst_z;
  const UINT bx=to_buffer?dst_x:sx,by=to_buffer?dst_y:sy,bz=to_buffer?dst_z:sz;
  const UINT pitch=b.footprint->Pitch,slice=b.footprint->SlicePitch;
  if(!block || !w || !h || !d || tx>tw || w>tw-tx || ty>th || h>th-ty || tz>td || d>td-tz ||
      bx>fw || w>fw-bx || by>fh || h>fh-by || bz>fd || d>fd-bz ||
      pitch<static_cast<UINT64>(fw)*block || (fd>1 && slice<static_cast<UINT64>(pitch)*fh) ||
      b.resource->gpuva<b.resource->heap->gpuva) {fail();return;}
  const UINT64 base=b.resource->gpuva-b.resource->heap->gpuva;
  const UINT64 point=static_cast<UINT64>(bz)*slice+static_cast<UINT64>(by)*pitch+static_cast<UINT64>(bx)*block;
  const UINT64 span=static_cast<UINT64>(d-1)*slice+static_cast<UINT64>(h-1)*pitch+static_cast<UINT64>(w)*block;
  if(b.offset>b.resource->bytes || point>b.resource->bytes-b.offset || span>b.resource->bytes-b.offset-point ||
      base>b.resource->heap->bytes || b.offset>b.resource->heap->bytes-base ||
      point>b.resource->heap->bytes-base-b.offset || span>b.resource->heap->bytes-base-b.offset-point) {fail();return;}
  AgxD3d12Command *slot=AppendCommand(list);if(!slot)return;slot->kind=kCommandTextureCopy;
  auto &c=slot->texture_copy;c.texture=t.resource->heap;c.buffer=b.resource->heap;
  c.level=level;c.layer=tz;c.x=tx;c.y=ty;c.width=w;c.height=h;c.depth=d;
  c.offset=base+b.offset+point;c.pitch=pitch;c.slice_pitch=slice;c.to_buffer=to_buffer;
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
    if(!refusal && (dst.resource->type==D3D12DDI_RT_TEXTURE1D || src.resource->type==D3D12DDI_RT_TEXTURE1D)) {
        CopyLineTextureRegion(list,dst,src,dst_x,dst_y,dst_z,box);return;
    }
    if(!refusal && (dst.resource->type==D3D12DDI_RT_TEXTURE3D || src.resource->type==D3D12DDI_RT_TEXTURE3D)) {
        CopyVolumeTextureRegion(list,dst,src,dst_x,dst_y,dst_z,box);return;
    }
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
        /* C29: buffer offsets count block rows and blocks (BC1-BC7) */
        unsigned bw = 0, bh = 0;
        const UINT block = AgxD3d12FormatBlock(texture.resource->format, &bw, &bh);
        if (!bw) bw = bh = 1;
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
                          static_cast<UINT64>(dst_z) * copy.slice_pitch + static_cast<UINT64>(dst_y / bh) * copy.pitch +
                          static_cast<UINT64>(dst_x / bw) * block;
            copy.layer += bz;
        } else {
            copy.x = dst_x;
            copy.y = dst_y;
            copy.layer += dst_z;
            copy.offset = buffer.resource->gpuva - buffer.resource->heap->gpuva + buffer.offset +
                          static_cast<UINT64>(bz) * copy.slice_pitch + static_cast<UINT64>(by / bh) * copy.pitch +
                          static_cast<UINT64>(bx / bw) * block;
        }
        if (!block || (buffer.footprint->Format != texture.resource->format &&
                       (bw > 1 || AgxD3d12FormatBlockBytes(buffer.footprint->Format) != block)) ||
            dst_x % bw || dst_y % bh || bx % bw || by % bh ||
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
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
/* C53 T2D: CopyTiles (Microsoft ID3D12GraphicsCommandList::CopyTiles): the
 * region's 64 KiB tiles, in region order, to or from consecutive 64 KiB of
 * the buffer -- a buffer tile's bytes as they are, a standard texture tile's
 * texels row by row at the full tile's pitch (a partial tile leaves the
 * rest of its 64 KiB alone). Recorded as one buffer or texture copy per
 * tile (consecutive buffer tiles as one). Packed tiles are left out: copies
 * of packed mips through CopyTiles are undefined. Exactly one direction
 * flag is required (the documentation names no default). */
static bool StandardTileElements(UINT block_bytes, UINT *w_el, UINT *h_el) {
    switch (block_bytes) {
    case 1: *w_el = 256; *h_el = 256; return true;
    case 2: *w_el = 256; *h_el = 128; return true;
    case 4: *w_el = 128; *h_el = 128; return true;
    case 8: *w_el = 128; *h_el = 64; return true;
    case 16: *w_el = 64; *h_el = 64; return true;
    default: return false;
    }
}
static VOID APIENTRY CopyTiles(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HRESOURCE tiled_handle,
                               const D3D12DDI_TILED_RESOURCE_COORDINATE *start, const D3D12DDI_TILE_REGION_SIZE *size,
                               D3D12DDI_HRESOURCE buffer_handle, UINT64 buffer_offset, D3D12DDI_TILE_COPY_FLAGS flags) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    auto *tiled = static_cast<AgxD3d12Resource *>(tiled_handle.pDrvPrivate);
    auto *buffer = static_cast<AgxD3d12Resource *>(buffer_handle.pDrvPrivate);
    if (!list) return;
    const bool in = (flags & D3D12DDI_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE) != 0;
    const bool out = (flags & D3D12DDI_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER) != 0;
    const UINT tiles = size ? size->NumTiles : 0u;
    AgxD3d12Heap *reserved = tiled ? tiled->heap : nullptr;
    const UINT64 tile = 0x10000ull;
    UINT refusal = !tiled || !reserved || !reserved->tiles || !reserved->native || !buffer || !buffer->heap ||
                           !buffer->heap->native || buffer->type != D3D12DDI_RT_BUFFER || !start || !tiles ? 1u
                 : in == out ? 2u
                 : buffer_offset > buffer->bytes || static_cast<UINT64>(tiles) * tile > buffer->bytes - buffer_offset ? 3u
                 : list->commands + tiles > kListCommands ? 4u : 0u;
    UINT w_el = 0, h_el = 0, bw = 1, bh = 1, block = 0;
    if (!refusal && reserved->geometry) {
        block = AgxD3d12FormatBlock(tiled->format, &bw, &bh);
        if (!bw) bw = bh = 1;
        if (!StandardTileElements(block, &w_el, &h_el)) refusal = 5u;
    }
    const AGX_TILE_GEOMETRY geometry = !refusal ? ReservedGeometry(reserved) : AGX_TILE_GEOMETRY{};
    const AGX_TILE_COORD at = start ? AGX_TILE_COORD{start->X, start->Y, start->Z, start->Subresource} : AGX_TILE_COORD{};
    const AGX_TILE_REGION region = size ? AGX_TILE_REGION{size->NumTiles, size->UseBox ? 1u : 0u, size->Width,
                                                          size->Height, size->Depth}
                                        : AGX_TILE_REGION{};
    const UINT64 buffer_base = buffer && buffer->heap ? buffer->gpuva - buffer->heap->gpuva + buffer_offset : 0u;
    UINT recorded = 0;
    for (UINT k = 0; !refusal && k < tiles;) {
        unsigned long long index = 0;
        if (!AgxTileRegionTile(&geometry, &at, &region, k, &index)) { refusal = 6u; break; }
        if (!reserved->geometry) {
            /* buffer tiles: runs of consecutive resource tiles in one copy */
            UINT n = 1;
            unsigned long long next = 0;
            while (k + n < tiles && AgxTileRegionTile(&geometry, &at, &region, k + n, &next) && next == index + n) ++n;
            AgxD3d12Command *slot = AppendCommand(list);
            if (!slot) return;
            slot->kind = kCommandCopy;
            const UINT64 at_tile = index * tile, at_buffer = buffer_base + static_cast<UINT64>(k) * tile;
            slot->copy = in ? AgxD3d12Copy{reserved, at_tile, buffer->heap, at_buffer, n * tile}
                            : AgxD3d12Copy{buffer->heap, at_buffer, reserved, at_tile, n * tile};
            ++recorded;
            k += n;
            continue;
        }
        unsigned slice = 0, mip = 0, x = 0, y = 0;
        int packed = 0;
        if (!AgxTileLocate(&geometry, index, &slice, &mip, &x, &y, &packed)) { refusal = 6u; break; }
        if (!packed) {
            const UINT level_w = tiled->width >> mip ? tiled->width >> mip : 1u;
            const UINT level_h = tiled->height >> mip ? tiled->height >> mip : 1u;
            const UINT px = x * w_el * bw, py = y * h_el * bh;
            AgxD3d12TextureCopy copy = {};
            copy.texture = reserved;
            copy.level = mip;
            copy.layer = slice;
            copy.x = px;
            copy.y = py;
            copy.width = w_el * bw < level_w - px ? w_el * bw : level_w - px;
            copy.height = h_el * bh < level_h - py ? h_el * bh : level_h - py;
            copy.depth = 1;
            copy.buffer = buffer->heap;
            copy.offset = buffer_base + static_cast<UINT64>(k) * tile;
            copy.pitch = w_el * block;
            copy.slice_pitch = 0;
            copy.to_buffer = out;
            AgxD3d12Command *slot = AppendCommand(list);
            if (!slot) return;
            slot->kind = kCommandTextureCopy;
            slot->texture_copy = copy;
            ++recorded;
        }
        ++k;
    }
    const UINT receipt[] = {refusal, tiles, recorded, out ? 1u : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-copy-tiles", refusal ? E_INVALIDARG : S_OK, receipt, 4);
    if (refusal) ListError(list, E_INVALIDARG);
}
#endif
/* C6b: graphics state setters (each also records its default-state bit) */
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
#define AGX_VB_SETTER IASetVertexBuffers
#define AGX_IB_SETTER IASetIndexBuffer
#define AGX_SO_SETTER SoSetTargets
#else
#define AGX_VB_SETTER AGX_LIST_STATE(pfnIASetVertexBuffers)
#define AGX_IB_SETTER AGX_LIST_STATE(pfnIASetIndexBuffer)
#define AGX_SO_SETTER AGX_LIST_STATE(pfnSOSetTargets)
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
static void ReportRedundantRoot(const AgxD3d12CommandList *list, UINT bank) {
    const auto *va = bank ? list->graphics_root_va : list->compute_root_va;
    const UINT *dwords = bank ? list->graphics_root_dwords : list->compute_root_dwords;
    UINT receipt[] = {bank, 0u, 0u};
    for (UINT i = 0; i < kRootArguments; ++i) {
        receipt[1] += va[i] != 0;
        receipt[2] += dwords[i] != 0;
    }
    AdmissionUmdDiagnostic("measure-d3d12-root-redundant", S_OK, receipt, 3);
}
static VOID APIENTRY SetGraphicsRootSignature(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HROOTSIGNATURE root) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    const auto *signature = static_cast<const AgxD3d12RootSignature *>(root.pDrvPrivate);
    if (signature && list->graphics_root == signature) {
        ReportRedundantRoot(list, 1u);
        return;
    }
    list->graphics_root = signature;
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
/* C39: stream-output views; a zero BufferLocation unbinds the slot */
static VOID APIENTRY SoSetTargets(D3D12DDI_HCOMMANDLIST handle, UINT start, UINT count,
                                  const D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW *views) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!list) return;
    list->state |= AGX_LIST_BIT(pfnSOSetTargets);
    for (UINT i = 0; i < count && start + i < kSoTargets; ++i) {
        AgxD3d12SoTarget &t = list->so[start + i];
        t = {};
        if (!views || !views[i].BufferLocation) continue;
        t.heap = HeapAt(list->device, views[i].BufferLocation, &t.offset);
        t.filled = views[i].BufferFilledSizeLocation
                       ? HeapAt(list->device, views[i].BufferFilledSizeLocation, &t.filled_offset) : nullptr;
        t.size = views[i].SizeInBytes;
        if (!t.heap || !t.filled) {
            t = {};
            ListError(list, E_INVALIDARG);
        }
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
    AcquireSRWLockExclusive(&pso->compile_lock);
    if (pso->compiled != S_FALSE) {
        const HRESULT result = pso->compiled;
        ReleaseSRWLockExclusive(&pso->compile_lock);
        return result;
    }
    AGX_D3D12_OWNER *owner = EnsureOwner(device);
    AGX_D3D12_GRAPHICS_DESC &desc = pso->owner_desc; /* C65V: kept for the min/max variant */
    memset(&desc, 0, sizeof(desc));
    desc.Vs = pso->vs_dwords ? pso->cs + pso->cs_dwords : nullptr;
    desc.Ps = pso->ps_dwords ? pso->cs + pso->cs_dwords + pso->vs_dwords : nullptr;
    desc.Gs = pso->gs_dwords ? pso->cs + pso->cs_dwords + pso->vs_dwords + pso->ps_dwords : nullptr;
    const UINT *tess = pso->cs + pso->cs_dwords + pso->vs_dwords + pso->ps_dwords + pso->gs_dwords; /* C37 */
    desc.Hs = pso->hs_dwords ? tess : nullptr;
    desc.Ds = pso->ds_dwords ? tess + pso->hs_dwords : nullptr;
    desc.HsPassthroughRegisters = pso->hs_registers;
    desc.HsPassthroughPositions = pso->hs_positions;
    AGX_D3D12_SO_DECL &so = pso->owner_so; /* C39 */
    memset(&so, 0, sizeof(so));
    if (pso->so.declared) {
        so.Entries = pso->so.entries;
        so.Rasterized = pso->so.rasterized;
        so.NumStrides = pso->so.num_strides;
        so.PositionRegisters = pso->so.position_registers;
        for (UINT i = 0; i < 4; ++i) so.Strides[i] = pso->so.strides[i];
        for (UINT i = 0; i < kSoEntries && i < pso->so.entries; ++i)
            so.Entry[i] = {pso->so.entry[i].stream, pso->so.entry[i].slot, pso->so.entry[i].reg, pso->so.entry[i].mask};
        desc.So = &so;
    }
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
    /* C57: Gallium applies one logic op to every render target, so the PSO's
     * render targets must agree (an independent mix is refused). */
    bool logic_agrees = true;
    for (UINT i = 0; i < pso->desc.NumRenderTargets && i < 8; ++i) {
        const D3D12DDI_RENDER_TARGET_BLEND_DESC &rt = pso->blend.RenderTarget[pso->blend.IndependentBlendEnable ? i : 0];
        if (!i) {
            desc.LogicOpEnable = rt.LogicOpEnable;
            desc.LogicOp = rt.LogicOp;
        } else if (!rt.LogicOpEnable != !desc.LogicOpEnable || (rt.LogicOpEnable && static_cast<unsigned>(rt.LogicOp) != desc.LogicOp)) {
            logic_agrees = false;
        }
    }
    if (!logic_agrees) {
        const UINT receipt[] = {pso->desc.NumRenderTargets, static_cast<UINT>(desc.LogicOpEnable), desc.LogicOp};
        AdmissionUmdDiagnostic("measure-d3d12-logic-op", E_NOTIMPL, receipt, 3);
        pso->compiled = E_NOTIMPL;
        ReleaseSRWLockExclusive(&pso->compile_lock);
        return E_NOTIMPL;
    }
    desc.SampleMask = pso->desc.SampleMask;
    desc.SampleCount = pso->desc.SampleDesc.Count; /* C32 */
    desc.FillMode = pso->rasterizer.FillMode;
    desc.CullMode = pso->rasterizer.CullMode;
    desc.ForcedSampleCount = pso->rasterizer.ForcedSampleCount; /* FL 11_1: TIR */
    desc.ConservativeRaster = pso->rasterizer.ConservativeRasterizationMode == D3D12DDI_CONSERVATIVE_RASTERIZATION_MODE_ON;
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
    const HRESULT result = pso->compiled;
    ReleaseSRWLockExclusive(&pso->compile_lock);
    return result;
}
#endif
/* C5 (investigation/analysis/D3D12-C5-compute.md): compute state. Setting
 * a different root signature resets its arguments; redundant selection of
 * the same live signature preserves them. Null retains the reset path. */
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
    const auto *signature = static_cast<const AgxD3d12RootSignature *>(root.pDrvPrivate);
    if (signature && list->compute_root == signature) {
        ReportRedundantRoot(list, 0u);
        return;
    }
    list->compute_root = signature;
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
/* C13: graphics root UAVs (EXP1375: still AGX_REJECT, the C13/C14 probe
 * lists removed the device when they set one) */
static VOID APIENTRY SetGraphicsRootUnorderedAccessView(D3D12DDI_HCOMMANDLIST handle, UINT index,
                                                        D3D12DDI_GPU_VIRTUAL_ADDRESS va) {
    if (AgxD3d12CommandList *list = ListOf(handle)) SetRootBufferView(list, index, va, D3D12DDI_ROOT_PARAMETER_TYPE_UAV, true);
}
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
static HRESULT CompileCompute(AgxD3d12Device *device, AgxD3d12PipelineState *pso) {
    AcquireSRWLockExclusive(&pso->compile_lock);
    if (pso->compiled != S_FALSE) {
        const HRESULT result = pso->compiled;
        ReleaseSRWLockExclusive(&pso->compile_lock);
        return result;
    }
    AGX_D3D12_OWNER *owner = EnsureOwner(device);
#if defined(APPLE_AGX_D3D12_PREPARED_NIR_OWNER) && APPLE_AGX_D3D12_PREPARED_NIR_OWNER
    pso->compiled = E_FAIL;
    if (owner) {
        const SIZE_T bytes = static_cast<SIZE_T>(pso->cs_dwords) * sizeof(UINT);
        if (pso->cs_dwords < 2 || bytes / sizeof(UINT) != pso->cs_dwords) {
            pso->compiled = E_INVALIDARG;
        } else {
            const uint32_t limits[4] = {AGX_D3D12_CBV_SLOTS, AGX_D3D12_SRV_SLOTS,
                                       AGX_D3D12_UAV_SLOTS, AGX_D3D12_SAMPLER_SLOTS};
            AgxOwnedNir *prepared = nullptr;
            const auto status = AgxOwnedNirPrepareLegacyCompute(pso->cs, bytes, limits, &prepared);
            switch (status) {
            case AGX_OWNED_NIR_OK:
                pso->compiled = AgxD3d12OwnerCreateComputePrepared(owner, prepared, &pso->compute);
                break;
            case AGX_OWNED_NIR_INVALID: pso->compiled = E_INVALIDARG; break;
            case AGX_OWNED_NIR_UNSUPPORTED: pso->compiled = E_NOTIMPL; break;
            case AGX_OWNED_NIR_PROJECT_OOM: pso->compiled = E_OUTOFMEMORY; break;
            default: pso->compiled = E_FAIL; break;
            }
            AgxOwnedNirDestroy(&prepared);
        }
    }
#else
    pso->compiled = owner ? AgxD3d12OwnerCreateCompute(owner, pso->cs, &pso->compute) : E_FAIL;
#endif
    const HRESULT result = pso->compiled;
    ReleaseSRWLockExclusive(&pso->compile_lock);
    return result;
}
/* C65V/C65S: the PSO's min/max variants, translated from the same tokens on
 * first use (replay, under the owner lock), keyed by what they emulate: the
 * heap samplers' reduction (run-time map bits) and, per stage and slot, the
 * bound slot or static sampler's dxbc_minmax_bits (compile-time constants).
 * A variant's slots must be the plain compile's: root bindings were resolved
 * against those. NULL = draw with the plain pipeline (standard filtering). */
struct AgxD3d12MinmaxKey {
    UINT heap;
    unsigned char slot[AGX_D3D12_HEAP_STAGES][kSamplerSlots];
};
static constexpr UINT kMinmaxVariants = 4;
struct AgxD3d12MinmaxVariant {
    AgxD3d12MinmaxKey key;
    UINT state; /* 1 ready, 2 refused */
    AGX_D3D12_COMPUTE compute;
    AGX_D3D12_GRAPHICS graphics;
};
template <typename T> static bool SameSlots(const T &a, const T &b) { return !memcmp(&a, &b, sizeof(a)); }
static bool SameGraphicsSlots(const AGX_D3D12_GRAPHICS &a, const AGX_D3D12_GRAPHICS &b) {
    return SameSlots(a.VsBindings, b.VsBindings) && SameSlots(a.PsBindings, b.PsBindings) &&
           SameSlots(a.GsBindings, b.GsBindings) && SameSlots(a.HsBindings, b.HsBindings) &&
           SameSlots(a.DsBindings, b.DsBindings) && SameSlots(a.VsSrvDimensions, b.VsSrvDimensions) &&
           SameSlots(a.PsSrvDimensions, b.PsSrvDimensions) && SameSlots(a.GsSrvDimensions, b.GsSrvDimensions) &&
           SameSlots(a.HsSrvDimensions, b.HsSrvDimensions) && SameSlots(a.DsSrvDimensions, b.DsSrvDimensions) &&
           SameSlots(a.VsUavDimensions, b.VsUavDimensions) && SameSlots(a.PsUavDimensions, b.PsUavDimensions) &&
           SameSlots(a.GsUavDimensions, b.GsUavDimensions) && SameSlots(a.HsUavDimensions, b.HsUavDimensions) &&
           SameSlots(a.DsUavDimensions, b.DsUavDimensions);
}
static void *MinmaxVariant(AGX_D3D12_OWNER *owner, AgxD3d12PipelineState *pso, const AgxD3d12MinmaxKey &key, bool compute) {
    AcquireSRWLockExclusive(&pso->compile_lock);
    void *variant = nullptr;
    if (pso->compiled == S_OK && !pso->minmax)
        pso->minmax = static_cast<AgxD3d12MinmaxVariant *>(
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, kMinmaxVariants * sizeof(AgxD3d12MinmaxVariant)));
    AgxD3d12MinmaxVariant *v = nullptr;
    for (UINT i = 0; pso->minmax && i < pso->minmax_count && !v; ++i)
        if (SameSlots(pso->minmax[i].key, key)) v = &pso->minmax[i];
    if (!v && pso->minmax && pso->minmax_count < kMinmaxVariants) {
        v = &pso->minmax[pso->minmax_count++];
        v->key = key;
        HRESULT hr;
        bool same;
        const unsigned flags = key.heap ? AGX_D3D12_SHADER_MINMAX : 0u;
        if (compute) {
            hr = AgxD3d12OwnerCreateComputeFlags(owner, pso->cs, flags, key.slot[AGX_D3D12_HEAP_CS], &v->compute);
            same = SUCCEEDED(hr) && SameSlots(v->compute.Bindings, pso->compute.Bindings) &&
                   SameSlots(v->compute.SrvDimensions, pso->compute.SrvDimensions) &&
                   SameSlots(v->compute.UavDimensions, pso->compute.UavDimensions);
            if (SUCCEEDED(hr) && !same) AgxD3d12OwnerDestroyCompute(owner, &v->compute);
        } else {
            AGX_D3D12_GRAPHICS_DESC desc = pso->owner_desc;
            desc.Flags = flags;
            static_assert(sizeof(desc.SlotMinmax) == AGX_D3D12_HEAP_CS * sizeof(key.slot[0]), "graphics stages");
            memcpy(desc.SlotMinmax, key.slot, sizeof(desc.SlotMinmax));
            hr = AgxD3d12OwnerCreateGraphics(owner, &desc, &v->graphics);
            same = SUCCEEDED(hr) && SameGraphicsSlots(v->graphics, pso->graphics);
            if (SUCCEEDED(hr) && !same) AgxD3d12OwnerDestroyGraphics(owner, &v->graphics);
        }
        v->state = same ? 1u : 2u;
        const UINT receipt[] = {compute ? 1u : 0u, same ? 1u : 0u, key.heap, pso->minmax_count};
        AdmissionUmdDiagnostic("measure-d3d12-minmax-variant", hr, receipt, 4);
    } else if (!v) {
        const UINT receipt[] = {compute ? 1u : 0u, pso->minmax_count};
        AdmissionUmdDiagnostic("reject-d3d12-minmax-variants", E_OUTOFMEMORY, receipt, 2);
    }
    if (v && v->state == 1u) variant = compute ? static_cast<void *>(&v->compute) : static_cast<void *>(&v->graphics);
    ReleaseSRWLockExclusive(&pso->compile_lock);
    return variant;
}
#endif
static VOID APIENTRY DestroyPipelineState(D3D12DDI_HDEVICE handle, D3D12DDI_HPIPELINESTATE pso_handle) {
    auto *pso = static_cast<AgxD3d12PipelineState *>(pso_handle.pDrvPrivate);
    RetirePsoIdentity(pso);
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    AgxD3d12Device *device = DeviceOf(handle);
    if (device && device->owner && pso && pso->compiled == S_OK) {
        AgxD3d12OwnerDestroyCompute(static_cast<AGX_D3D12_OWNER *>(device->owner), &pso->compute);
        AgxD3d12OwnerDestroyGraphics(static_cast<AGX_D3D12_OWNER *>(device->owner), &pso->graphics);
    }
    if (pso && pso->minmax) {
        for (UINT i = 0; device && device->owner && i < pso->minmax_count; ++i)
            if (pso->minmax[i].state == 1u) {
                AgxD3d12OwnerDestroyCompute(static_cast<AGX_D3D12_OWNER *>(device->owner), &pso->minmax[i].compute);
                AgxD3d12OwnerDestroyGraphics(static_cast<AGX_D3D12_OWNER *>(device->owner), &pso->minmax[i].graphics);
            }
        HeapFree(GetProcessHeap(), 0, pso->minmax);
        pso->minmax = nullptr;
    }
#else
    (void)handle;
    (void)pso_handle;
#endif
}
/* C38 (D3D12-C38-pso-serialization.md; EXP1834: D3D12PipelineStateCache's
 * GetCachedBlob reached the refused pfnSerializeObject and the device was
 * removed): a pipeline state serializes to a position-independent blob of
 * what it keeps after creation -- the strip cut, sample mask and count,
 * the state descriptions, the element layout, and its programs -- and a
 * deserialized pipeline state is rebuilt from that blob alone and compiles
 * at its first use, as a created one. The runtime wraps the blob with its
 * own adapter and driver-version header. */
static constexpr UINT kPsoBlobMagic = 0x50584741u; /* "AGXP" */
static constexpr UINT kPsoBlobVersion = 2u; /* 2: C39 stream output */
struct AgxD3d12PsoBlob {
    UINT magic, version, bytes;
    UINT ib_strip_cut, sample_mask, sample_count, sample_quality;
    D3D12DDI_BLEND_DESC blend;
    D3D12DDI_RASTERIZER_DESC rasterizer;
    D3D12DDI_DEPTH_STENCIL_DESC depth_stencil;
    UINT elements;
    D3D12DDIARG_INPUT_ELEMENT_DESC element[kPipelineElements];
    AgxD3d12StreamOutput so;
    UINT cs_dwords, vs_dwords, ps_dwords, gs_dwords, hs_dwords, ds_dwords, hs_registers, hs_positions;
    UINT tokens[1];
};
static UINT64 PsoDwords(const AgxD3d12PipelineState *pso) {
    return static_cast<UINT64>(pso->cs_dwords) + pso->vs_dwords + pso->ps_dwords + pso->gs_dwords + pso->hs_dwords +
           pso->ds_dwords;
}
static bool LivePso(const void *handle) {
    bool live = false;
    AcquireSRWLockShared(&identity_registry_lock);
    for (auto *pso = live_identity_psos; pso && !live; pso = pso->identity_next) live = pso == handle;
    ReleaseSRWLockShared(&identity_registry_lock);
    return live;
}
/* the blob's own size when it is a well-formed blob of this version, else 0 */
static SIZE_T PsoBlobBytes(SIZE_T size, const UINT *data) {
    const auto *blob = reinterpret_cast<const AgxD3d12PsoBlob *>(data);
    if (!blob || size < offsetof(AgxD3d12PsoBlob, tokens) || blob->magic != kPsoBlobMagic ||
        blob->version != kPsoBlobVersion || blob->bytes != size || blob->elements > kPipelineElements + 1u)
        return 0;
    const UINT64 dwords = static_cast<UINT64>(blob->cs_dwords) + blob->vs_dwords + blob->ps_dwords + blob->gs_dwords +
                          blob->hs_dwords + blob->ds_dwords;
    return offsetof(AgxD3d12PsoBlob, tokens) + dwords * sizeof(UINT) == size ? size : 0;
}
static HRESULT APIENTRY SerializeObject(D3D12DDI_HDEVICE, D3D12DDI_HANDLETYPE type, HANDLE object, SIZE_T *size,
                                        const UINT **data) {
    auto *pso = static_cast<const AgxD3d12PipelineState *>(object);
    const bool valid = type == D3D12DDI_HT_PIPELINE_STATE && pso && size && data && LivePso(pso);
    const UINT64 bytes = valid ? offsetof(AgxD3d12PsoBlob, tokens) + PsoDwords(pso) * sizeof(UINT) : 0;
    auto *blob = valid ? static_cast<AgxD3d12PsoBlob *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, static_cast<SIZE_T>(bytes)))
                       : nullptr;
    const HRESULT hr = !valid ? E_INVALIDARG : blob ? S_OK : E_OUTOFMEMORY;
    const UINT receipt[] = {static_cast<UINT>(type), valid ? 1u : 0u, static_cast<UINT>(bytes)};
    AdmissionUmdDiagnostic("measure-d3d12-serialize", hr, receipt, 3);
    if (FAILED(hr)) return hr;
    blob->magic = kPsoBlobMagic;
    blob->version = kPsoBlobVersion;
    blob->bytes = static_cast<UINT>(bytes);
    blob->ib_strip_cut = static_cast<UINT>(pso->desc.IBStripCutValue);
    blob->sample_mask = pso->desc.SampleMask;
    blob->sample_count = pso->desc.SampleDesc.Count;
    blob->sample_quality = pso->desc.SampleDesc.Quality;
    blob->blend = pso->blend;
    blob->rasterizer = pso->rasterizer;
    blob->depth_stencil = pso->depth_stencil;
    blob->elements = pso->elements;
    memcpy(blob->element, pso->element, sizeof(blob->element));
    blob->so = pso->so;
    blob->cs_dwords = pso->cs_dwords;
    blob->vs_dwords = pso->vs_dwords;
    blob->ps_dwords = pso->ps_dwords;
    blob->gs_dwords = pso->gs_dwords;
    blob->hs_dwords = pso->hs_dwords;
    blob->ds_dwords = pso->ds_dwords;
    blob->hs_registers = pso->hs_registers;
    blob->hs_positions = pso->hs_positions;
    memcpy(blob->tokens, pso->cs, static_cast<SIZE_T>(PsoDwords(pso)) * sizeof(UINT));
    *size = static_cast<SIZE_T>(bytes);
    *data = reinterpret_cast<const UINT *>(blob);
    return S_OK;
}
static VOID APIENTRY DestroyObjectSerialization(D3D12DDI_HDEVICE, SIZE_T, const UINT *data) {
    if (data) HeapFree(GetProcessHeap(), 0, const_cast<UINT *>(data));
}
static SIZE_T APIENTRY CalcPrivateDeserializedObjectSize(D3D12DDI_HDEVICE, D3D12DDI_HANDLETYPE type, SIZE_T size,
                                                         const UINT *data) {
    const SIZE_T bytes = type == D3D12DDI_HT_PIPELINE_STATE ? PsoBlobBytes(size, data) : 0;
    if (!bytes) return sizeof(AgxD3d12PipelineState); /* CreateDeserializedObject refuses the blob */
    const SIZE_T dwords = (bytes - offsetof(AgxD3d12PsoBlob, tokens)) / sizeof(UINT);
    return offsetof(AgxD3d12PipelineState, cs) + (dwords ? dwords : 1) * sizeof(UINT);
}
static HRESULT APIENTRY CreateDeserializedObject(D3D12DDI_HDEVICE handle, D3D12DDI_HANDLETYPE type, SIZE_T size,
                                                 const UINT *data, VOID *object, VOID *) {
    auto *device = static_cast<AgxD3d12Device *>(handle.pDrvPrivate);
    auto *pso = static_cast<AgxD3d12PipelineState *>(object);
    const bool valid = device && pso && type == D3D12DDI_HT_PIPELINE_STATE && PsoBlobBytes(size, data);
    const UINT receipt[] = {static_cast<UINT>(type), valid ? 1u : 0u, static_cast<UINT>(size)};
    AdmissionUmdDiagnostic("measure-d3d12-deserialize", valid ? S_OK : E_INVALIDARG, receipt, 3);
    if (!valid) return E_INVALIDARG;
    const auto *blob = reinterpret_cast<const AgxD3d12PsoBlob *>(data);
    memset(pso, 0, offsetof(AgxD3d12PipelineState, cs));
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    InitializeSRWLock(&pso->compile_lock);
#endif
    pso->desc.hDrvPipelineState.pDrvPrivate = pso;
    pso->desc.IBStripCutValue = static_cast<D3D12DDI_INDEX_BUFFER_STRIP_CUT_VALUE>(blob->ib_strip_cut);
    pso->desc.SampleMask = blob->sample_mask;
    pso->desc.SampleDesc.Count = blob->sample_count;
    pso->desc.SampleDesc.Quality = blob->sample_quality;
    pso->compiled = S_FALSE;
    pso->blend = blob->blend;
    pso->rasterizer = blob->rasterizer;
    pso->depth_stencil = blob->depth_stencil;
    pso->elements = blob->elements;
    memcpy(pso->element, blob->element, sizeof(pso->element));
    pso->so = blob->so;
    pso->cs_dwords = blob->cs_dwords;
    pso->vs_dwords = blob->vs_dwords;
    pso->ps_dwords = blob->ps_dwords;
    pso->gs_dwords = blob->gs_dwords;
    pso->hs_dwords = blob->hs_dwords;
    pso->ds_dwords = blob->ds_dwords;
    pso->hs_registers = blob->hs_registers;
    pso->hs_positions = blob->hs_positions;
    memcpy(pso->cs, blob->tokens, static_cast<SIZE_T>(PsoDwords(pso)) * sizeof(UINT));
    PublishPsoIdentity(pso, device);
    return S_OK;
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
/* C17: namespace-aware root resolution */
static UINT MappedSlotLimit(UINT kind) {
    switch (kind) {
    case DXBC_BINDING_CBV: return kCbSlots;
    case DXBC_BINDING_SRV: return kSrvSlots;
    case DXBC_BINDING_UAV: return kUavSlots;
    case DXBC_BINDING_SAMPLER: return kSamplerSlots;
    default: return 0;
    }
}
static UINT MappedRangeKind(UINT type) {
    switch (type) {
    case D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV: return DXBC_BINDING_CBV;
    case D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV: return DXBC_BINDING_SRV;
    case D3D12DDI_DESCRIPTOR_RANGE_TYPE_UAV: return DXBC_BINDING_UAV;
    case D3D12DDI_DESCRIPTOR_RANGE_TYPE_SAMPLER: return DXBC_BINDING_SAMPLER;
    default: return DXBC_BINDING_KIND_COUNT;
    }
}
static UINT ResolveMappedRoot(AgxD3d12CommandList *list, const AgxD3d12RootSignature *root,
                              const UINT64 *va, const UINT *dwords, UINT stage,
                              const struct dxbc_binding_map &map, AgxD3d12Dispatch *out) {
    if (map.count > DXBC_BINDING_MAP_MAX) return 5u;
    memcpy(out->root_dwords, dwords, kRootArguments * sizeof(UINT));
    out->descriptors = nullptr;
    out->sampler_descriptors = nullptr;
    out->heap_ranges = 0;
    out->heap_sampler_mask = 0;
    for (UINT m = 0; m < map.count; ++m) {
        const struct dxbc_binding &binding = map.bindings[m];
        const UINT slot = binding.slot;
        if (binding.flags & DXBC_BINDING_HEAP) {
            /* C47: a heap range reads the shader-visible heap from the heap
             * index of the table entry its first register names */
            if (binding.kind == DXBC_BINDING_CBV || slot >= kHeapRanges) return 5u;
            const bool sampler = binding.kind == DXBC_BINDING_SAMPLER;
            const UINT stride = DescriptorBytes(sampler ? D3D12DDI_DESCRIPTOR_HEAP_TYPE_SAMPLER
                                                        : D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            bool found = false;
            for (UINT i = 0; root && i < root->parameters; ++i) {
                const AgxD3d12RootParameter &p = root->parameter[i];
                if (stage != kAnyStage && p.visibility != D3D12DDI_SHADER_VISIBILITY_ALL && p.visibility != stage) continue;
                if (i >= kRootArguments) return 5u;
                if (p.type != D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) continue;
                const AgxD3d12Range *ranges = RangesOf(root) + p.first_range;
                for (UINT r = 0; r < p.ranges; ++r) {
                    const AgxD3d12Range &range = ranges[r];
                    if (MappedRangeKind(range.type) != binding.kind || range.space != binding.space ||
                        binding.reg < range.base_register) continue;
                    const UINT delta = binding.reg - range.base_register;
                    if (range.count != UINT_MAX && delta >= range.count) continue;
                    if (found || range.offset == UINT_MAX) return 5u;
                    UINT64 offset = 0;
                    AgxD3d12Heap *heap = HeapAt(list->device, va[i], &offset);
                    AgxD3d12Heap *&bound = sampler ? out->sampler_descriptors : out->descriptors;
                    if (!heap || !heap->cpu || offset % stride || (bound && bound != heap)) return 6u;
                    bound = heap;
                    if (sampler) out->heap_sampler_mask |= 1ull << slot;
                    out->heap_first[slot] = static_cast<UINT>(offset / stride) + range.offset + delta;
                    if (slot >= out->heap_ranges) out->heap_ranges = slot + 1;
                    found = true;
                }
            }
            if (!found) return 6u;
            continue;
        }
        if (slot >= MappedSlotLimit(binding.kind)) return 5u;
        for (UINT prev = 0; prev < m; ++prev) {
            const struct dxbc_binding &other = map.bindings[prev];
            if (other.flags & DXBC_BINDING_HEAP) continue;
            if (other.kind == binding.kind && (other.slot == slot ||
                (other.space == binding.space && other.reg == binding.reg))) return 5u;
        }
        bool matched = false;
        for (UINT i = 0; root && i < root->parameters; ++i) {
            const AgxD3d12RootParameter &p = root->parameter[i];
            if (stage != kAnyStage && p.visibility != D3D12DDI_SHADER_VISIBILITY_ALL && p.visibility != stage) continue;
            if (i >= kRootArguments) return 5u;
            if (p.type == D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
                const AgxD3d12Range *ranges = RangesOf(root) + p.first_range;
                for (UINT r = 0; r < p.ranges; ++r) {
                    const AgxD3d12Range &range = ranges[r];
                    if (MappedRangeKind(range.type) != binding.kind || range.space != binding.space ||
                        binding.reg < range.base_register) continue;
                    const UINT delta = binding.reg - range.base_register;
                    if (range.count != UINT_MAX && delta >= range.count) continue;
                    if (matched || range.offset == UINT_MAX) return 5u;
                    UINT64 offset = 0;
                    AgxD3d12Heap *heap = HeapAt(list->device, va[i], &offset);
                    if (!heap || !heap->cpu) return 6u;
                    const bool sampler = binding.kind == DXBC_BINDING_SAMPLER;
                    const UINT stride = DescriptorBytes(sampler ? D3D12DDI_DESCRIPTOR_HEAP_TYPE_SAMPLER
                                                                : D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                    const UINT64 at = offset + (static_cast<UINT64>(range.offset) + delta) * stride;
                    const UINT64 bytes = sampler ? sizeof(AgxD3d12SamplerDescriptor) : sizeof(AgxD3d12Descriptor);
                    if (at > heap->bytes || bytes > heap->bytes - at) {
                        /* C34: an unbounded range's tail past the heap stays unbound */
                        if (!(binding.flags & DXBC_BINDING_OPTIONAL)) return 6u;
                        matched = true;
                        continue;
                    }
                    if (sampler) {
                        out->sampler[slot] = reinterpret_cast<const AgxD3d12SamplerDescriptor *>(static_cast<const BYTE *>(heap->cpu) + at);
                    } else {
                        const auto *view = reinterpret_cast<const AgxD3d12Descriptor *>(static_cast<const BYTE *>(heap->cpu) + at);
                        (binding.kind == DXBC_BINDING_UAV ? out->uav_descriptor :
                         binding.kind == DXBC_BINDING_SRV ? out->srv_descriptor : out->cb_descriptor)[slot] = view;
                    }
                    matched = true;
                }
                continue;
            }
            if (p.space != binding.space || p.shader_register != binding.reg) continue;
            const bool uav = p.type == D3D12DDI_ROOT_PARAMETER_TYPE_UAV && binding.kind == DXBC_BINDING_UAV;
            const bool srv = p.type == D3D12DDI_ROOT_PARAMETER_TYPE_SRV && binding.kind == DXBC_BINDING_SRV;
            const bool cbv = p.type == D3D12DDI_ROOT_PARAMETER_TYPE_CBV && binding.kind == DXBC_BINDING_CBV;
            const bool constants = p.type == D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS && binding.kind == DXBC_BINDING_CBV;
            if (!uav && !srv && !cbv && !constants) continue;
            if (matched) return 5u;
            if (constants) {
                if (p.offset > kRootArguments || p.dwords > kRootArguments - p.offset) return 5u;
                out->cb_constants[slot] = p.offset + 1u;
                out->cb_count[slot] = p.dwords;
            } else {
                UINT64 *offset = uav ? &out->uav_offset[slot] : srv ? &out->srv_offset[slot] : &out->cb_offset[slot];
                AgxD3d12Heap **heap = uav ? &out->uav_heap[slot] : srv ? &out->srv_heap[slot] : &out->cb_heap[slot];
                if (!(*heap = HeapAt(list->device, va[i], offset))) return 6u;
            }
            matched = true;
        }
        if (binding.kind == DXBC_BINDING_SAMPLER) {
            const D3D12DDI_STATIC_SAMPLER *samplers = root ? StaticSamplersOf(root) : nullptr;
            for (UINT i = 0; root && i < root->static_samplers; ++i) {
                const D3D12DDI_STATIC_SAMPLER &s = samplers[i];
                if (stage != kAnyStage && s.ShaderVisibility != D3D12DDI_SHADER_VISIBILITY_ALL &&
                    static_cast<UINT>(s.ShaderVisibility) != stage) continue;
                if (s.RegisterSpace != binding.space || s.ShaderRegister != binding.reg) continue;
                if (matched) return 5u;
                out->static_sampler[slot] = &s;
                matched = true;
            }
        }
        if (!matched && !(binding.flags & DXBC_BINDING_OPTIONAL)) return 6u;
    }
    return 0u;
}
static UINT ResolveRoot(AgxD3d12CommandList *list, const AgxD3d12RootSignature *root, const UINT64 *va, const UINT *dwords,
                        UINT stage, AgxD3d12Dispatch *out) {
    if (list && list->pso) {
        if (stage == D3D12DDI_SHADER_VISIBILITY_PIXEL && !list->pso->ps_dwords) return 0u;
        if (stage == D3D12DDI_SHADER_VISIBILITY_GEOMETRY && !list->pso->gs_dwords) return 0u;
        if (stage == D3D12DDI_SHADER_VISIBILITY_HULL && !list->pso->hs_dwords) return 0u; /* C37 */
        if (stage == D3D12DDI_SHADER_VISIBILITY_DOMAIN && !list->pso->ds_dwords) return 0u;
        const struct dxbc_binding_map *map = stage == kAnyStage ? &list->pso->compute.Bindings
            : stage == D3D12DDI_SHADER_VISIBILITY_VERTEX ? &list->pso->graphics.VsBindings
            : stage == D3D12DDI_SHADER_VISIBILITY_GEOMETRY ? &list->pso->graphics.GsBindings
            : stage == D3D12DDI_SHADER_VISIBILITY_HULL ? &list->pso->graphics.HsBindings
            : stage == D3D12DDI_SHADER_VISIBILITY_DOMAIN ? &list->pso->graphics.DsBindings
                                                       : &list->pso->graphics.PsBindings;
        if (map->active) return ResolveMappedRoot(list, root, va, dwords, stage, *map, out);
    }
    for (UINT i = 0; root && i < root->parameters; ++i) {
        const AgxD3d12RootParameter &p = root->parameter[i];
        if (stage != kAnyStage && p.visibility != D3D12DDI_SHADER_VISIBILITY_ALL && p.visibility != stage) continue;
        if (p.space || i >= kRootArguments) return 5u;
        /* EXP1835: a root descriptor or table the application never set
         * leaves its slots unbound; D3D12 requires only the arguments the
         * shader reads, which identity (SM5.0) bindings cannot tell */
        if (p.type != D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS && !va[i]) continue;
        if (p.type == D3D12DDI_ROOT_PARAMETER_TYPE_UAV) {
            if (p.shader_register >= kUavSlots) return 5u;
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
        } else if (p.type == D3D12DDI_ROOT_PARAMETER_TYPE_SRV) {
            if (p.shader_register >= kSrvSlots) return 5u;
            if (!(out->srv_heap[p.shader_register] = HeapAt(list->device, va[i], &out->srv_offset[p.shader_register])))
                return 6u;
        } else {
            return 4u;
        }
    }
    /* C6e: static samplers visible to the stage (C9: compute sees all) */
    const D3D12DDI_STATIC_SAMPLER *statics = root ? StaticSamplersOf(root) : nullptr;
    for (UINT i = 0; root && i < root->static_samplers; ++i) {
        const D3D12DDI_STATIC_SAMPLER &s = statics[i];
        if (stage != kAnyStage && s.ShaderVisibility != D3D12DDI_SHADER_VISIBILITY_ALL &&
            static_cast<UINT>(s.ShaderVisibility) != stage)
            continue;
        if (s.RegisterSpace || s.ShaderRegister >= kSamplerSlots) return 5u;
        out->static_sampler[s.ShaderRegister] = &s;
    }
    memcpy(out->root_dwords, dwords, kRootArguments * sizeof(UINT));
    return 0u;
}
#endif
/* Dispatch resolves the compute root signature now: root UAVs (C5), root
 * CBVs and root constants (C5b), CBV/UAV descriptor tables (C5c); root SRVs
 * and SRV/sampler ranges refuse the dispatch with a receipt naming the
 * reason. */
static void RecordDispatch(AgxD3d12CommandList *list, UINT x, UINT y, UINT z, const AgxD3d12Indirect *indirect) {
    if (!list || (!indirect && (!x || !y || !z))) return; /* an empty grid does nothing */
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    AgxD3d12PipelineState *pso = list->pso;
    const AgxD3d12RootSignature *root = list->compute_root;
    AgxD3d12Dispatch dispatch = {};
    UINT refusal = !pso || !pso->cs_dwords ? 1u : !root ? 2u : list->commands >= kListCommands ? 3u : 0u;
    /* C17: compilation produces the immutable namespace map used by ResolveRoot. */
    if (!refusal && FAILED(CompileCompute(list->device, pso))) refusal = 7u;
    if (!refusal) refusal = ResolveRoot(list, root, list->compute_root_va, list->compute_root_dwords, kAnyStage, &dispatch);
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
    if (indirect) dispatch.indirect = *indirect;
    AgxD3d12Command *slot = AppendCommand(list);
    if (!slot) return;
    AgxD3d12Command &command = *slot;
    command.kind = kCommandDispatch;
    command.dispatch = dispatch;
#else
    (void)indirect;
    ListError(list, E_NOTIMPL);
#endif
}
static VOID APIENTRY Dispatch(D3D12DDI_HCOMMANDLIST handle, UINT x, UINT y, UINT z) {
    RecordDispatch(ListOf(handle), x, y, z, nullptr);
}
/* C6b: DrawInstanced snapshots the graphics state; C6c resolves the
 * graphics root signature for the VS and the PS (C27: and the GS, recorded
 * as a kCommandGsBind slot just before the draw so draws without a GS keep
 * their slot size; C37: the HS and DS likewise). A refusal names its
 * reason in receipt measure-d3d12-draw {reason}. */
static void RecordDraw(AgxD3d12CommandList *list, UINT vertices, UINT instances, UINT start_vertex, INT base_vertex,
                       UINT start_instance, bool indexed, const AgxD3d12Indirect *indirect = nullptr) {
    if (!list || (!indirect && (!vertices || !instances))) return;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    AgxD3d12PipelineState *pso = list->pso;
    UINT refusal = !pso || !pso->vs_dwords ? 1u
                 : !pso->hs_dwords != !pso->ds_dwords ? 4u
                 : (list->topology >= 33u && list->topology <= 64u) != (pso->hs_dwords != 0) ? 9u /* C37: patches <=> HS+DS */
                 : pso->so.declared && pso->hs_dwords ? 10u /* C39: stream output after tessellation */
                 : list->rtvs && !list->rtv[0].heap && list->dsv.kind != kDescriptorDsv ? 5u /* EXP1889: 0 RTVs = UAV-only */
                 : indexed && !list->index_heap ? 6u
                 : indirect && indexed && list->index_offset ? 8u /* C14: Gallium takes the start index from the arguments */
                 : list->commands + (pso->gs_dwords ? 1u : 0u) + (pso->hs_dwords ? 2u : 0u) >= kListCommands ? 3u : 0u;
    AgxD3d12Dispatch vs_bind = {}, ps_bind = {};
    if (!refusal && FAILED(CompileGraphics(list->device, pso))) refusal = 7u;
    if (!refusal) refusal = ResolveRoot(list, list->graphics_root, list->graphics_root_va, list->graphics_root_dwords,
                                        D3D12DDI_SHADER_VISIBILITY_VERTEX, &vs_bind);
    if (!refusal) refusal = ResolveRoot(list, list->graphics_root, list->graphics_root_va, list->graphics_root_dwords,
                                        D3D12DDI_SHADER_VISIBILITY_PIXEL, &ps_bind);
    /* C27/C37: the optional stages resolve straight into their slots (five
     * AgxD3d12Dispatch records would take the frame past STACK_FRAMES,
     * EXP1834 build: 21552 bytes); a refusal drops the slots again */
    const UINT first_slot = list->commands;
    const struct { UINT dwords, kind, visibility; } optional[] = {
        {pso ? pso->gs_dwords : 0u, kCommandGsBind, D3D12DDI_SHADER_VISIBILITY_GEOMETRY},
        {pso ? pso->hs_dwords : 0u, kCommandHsBind, D3D12DDI_SHADER_VISIBILITY_HULL},
        {pso ? pso->ds_dwords : 0u, kCommandDsBind, D3D12DDI_SHADER_VISIBILITY_DOMAIN}};
    for (const auto &stage : optional) {
        if (refusal || !stage.dwords) continue;
        AgxD3d12Command *bind_slot = AppendCommand(list);
        if (!bind_slot) return;
        bind_slot->kind = stage.kind;
        refusal = ResolveRoot(list, list->graphics_root, list->graphics_root_va, list->graphics_root_dwords,
                              stage.visibility, &bind_slot->dispatch);
    }
    const UINT receipt[] = {refusal, vertices, instances, list->rtvs, list->topology, indexed ? list->index_size : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-draw", refusal ? E_NOTIMPL : S_OK, receipt, 6);
    if (refusal) {
        list->commands = first_slot;
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
    if (pso->so.declared) memcpy(draw.so, list->so, sizeof(draw.so)); /* C39 */
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
    if (indirect) draw.indirect = *indirect;
#else
    (void)indirect;
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
/* C14 (D3D12-C14-execute-indirect.md): command signatures of one Draw,
 * DrawIndexed or Dispatch argument -- the D3D12 argument records are the
 * Gallium indirect layouts; other argument types refuse. */
/* C31 (EXP1825: the ExecuteIndirect sample's root-CBV + draw signature):
 * up to seven root-constant, root-view, vertex- or index-buffer arguments
 * before the final draw/dispatch, tightly packed in declaration order (D3D12
 * command signature layout). Such a signature replays on the CPU: the
 * argument and count buffers are read through synchronised maps when the
 * list executes, and each record applies its arguments to a copy of the
 * list state through the ordinary setters, then records and replays one
 * draw or dispatch. */
static constexpr UINT kSignatureArguments = 8;
struct AgxD3d12SignatureArgument {
    UINT type, param, dest, count, offset;
};
struct AgxD3d12CommandSignature {
    UINT kind; /* 0 refused, 1 draw, 2 draw indexed, 3 dispatch */
    UINT stride;
    UINT arguments, record_bytes; /* C31: the final draw/dispatch included */
    AgxD3d12SignatureArgument argument[kSignatureArguments];
};
struct AgxD3d12IndirectState {
    AgxD3d12CommandList list;
    AgxD3d12CommandSignature signature;
};
static SIZE_T APIENTRY CalcPrivateCommandSignatureSize(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 *) {
    return sizeof(AgxD3d12CommandSignature);
}
static HRESULT APIENTRY CreateCommandSignature(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 *arg,
                                               D3D12DDI_HCOMMANDSIGNATURE handle) {
    auto *signature = static_cast<AgxD3d12CommandSignature *>(handle.pDrvPrivate);
    if (!signature || !arg) return E_INVALIDARG;
    memset(signature, 0, sizeof(*signature));
    const UINT n = arg->pArgumentDescs ? arg->NumArgumentDescs : 0u;
    const UINT type = n ? static_cast<UINT>(arg->pArgumentDescs[n - 1].Type) : ~0u;
    const UINT kind = type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW ? 1u
                    : type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED ? 2u
                    : type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_DISPATCH ? 3u : 0u;
    static const UINT bytes[4] = {0u, 16u, 20u, 12u};
    UINT offset = 0, refusal = !kind || n > kSignatureArguments ? 1u : 0u;
    for (UINT i = 0; !refusal && i < n; ++i) {
        const D3D12DDI_INDIRECT_ARGUMENT_DESC &d = arg->pArgumentDescs[i];
        AgxD3d12SignatureArgument &a = signature->argument[i];
        a.type = static_cast<UINT>(d.Type);
        a.offset = offset;
        if (i == n - 1) {
            offset += bytes[kind];
        } else if (d.Type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT) {
            a.param = d.Constant.RootParameterIndex;
            a.dest = d.Constant.DestOffsetIn32BitValues;
            a.count = d.Constant.Num32BitValuesToSet;
            if (!a.count || a.count > kRootArguments) refusal = 2u;
            offset += 4u * a.count;
        } else if (d.Type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW ||
                   d.Type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW ||
                   d.Type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW) {
            a.param = d.ConstantBufferView.RootParameterIndex; /* the three views share the layout */
            offset += 8u;
        } else if (d.Type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW) {
            a.param = d.VertexBuffer.Slot;
            if (a.param >= kVertexBuffers || kind == 3u) refusal = 3u;
            offset += 16u;
        } else if (d.Type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW) {
            if (kind == 3u) refusal = 3u;
            offset += 16u;
        } else {
            refusal = 4u; /* a second draw/dispatch, rays, mesh, incrementing constants */
        }
    }
    if (!refusal && arg->ByteStride < offset) refusal = 5u;
    const HRESULT hr = refusal ? E_NOTIMPL : S_OK;
    if (SUCCEEDED(hr)) {
        signature->kind = kind;
        signature->stride = arg->ByteStride;
        signature->arguments = n;
        signature->record_bytes = offset;
    } else {
        memset(signature, 0, sizeof(*signature));
    }
    const UINT receipt[] = {arg->NumArgumentDescs, type, arg->ByteStride, refusal};
    AdmissionUmdDiagnostic("measure-d3d12-command-signature", hr, receipt, 4);
    return hr;
}
static VOID APIENTRY DestroyCommandSignature(D3D12DDI_HDEVICE, D3D12DDI_HCOMMANDSIGNATURE handle) {
    if (handle.pDrvPrivate) memset(handle.pDrvPrivate, 0, sizeof(AgxD3d12CommandSignature));
}
static VOID APIENTRY ExecuteIndirect(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HCOMMANDSIGNATURE signature_handle, UINT max_count,
                                     D3D12DDIARG_BUFFER_PLACEMENT args, D3D12DDIARG_BUFFER_PLACEMENT count) {
    AgxD3d12CommandList *list = ListOf(handle);
    if (!list) return;
    const auto *signature = static_cast<const AgxD3d12CommandSignature *>(signature_handle.pDrvPrivate);
    const auto *a = static_cast<const AgxD3d12Resource *>(args.BaseAddress.UMD.hResource.pDrvPrivate);
    const auto *c = static_cast<const AgxD3d12Resource *>(count.BaseAddress.UMD.hResource.pDrvPrivate);
    const UINT refusal = !signature || !signature->kind ? 1u
                       : !a || !a->heap || a->type != D3D12DDI_RT_BUFFER ? 2u
                       : c && (!c->heap || c->type != D3D12DDI_RT_BUFFER) ? 3u : 0u;
    const UINT receipt[] = {refusal, signature ? signature->kind : 0u, max_count, c ? 1u : 0u};
    AdmissionUmdDiagnostic("measure-d3d12-execute-indirect", refusal ? E_INVALIDARG : S_OK, receipt, 4);
    if (refusal) {
        ListError(list, E_INVALIDARG);
        return;
    }
    if (!max_count) return;
    AgxD3d12Indirect indirect = {};
    indirect.args = a->heap;
    indirect.args_offset = a->gpuva - a->heap->gpuva + args.BaseAddress.UMD.Offset;
    indirect.stride = signature->stride;
    indirect.max_count = max_count;
    if (c) {
        indirect.count = c->heap;
        indirect.count_offset = c->gpuva - c->heap->gpuva + count.BaseAddress.UMD.Offset;
    }
    if (signature->arguments > 1) {
        /* C31: the list state and signature at this call */
        if (list->commands >= kListCommands) {
            ListError(list, E_INVALIDARG);
            return;
        }
        if (list->indirect_state_count == list->indirect_state_capacity) {
            const UINT capacity = list->indirect_state_capacity ? list->indirect_state_capacity * 2u : 4u;
            void *grown = list->indirect_states
                              ? HeapReAlloc(GetProcessHeap(), 0, list->indirect_states, capacity * sizeof(AgxD3d12IndirectState))
                              : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(AgxD3d12IndirectState));
            if (!grown) {
                ListError(list, E_OUTOFMEMORY);
                return;
            }
            list->indirect_states = static_cast<AgxD3d12IndirectState *>(grown);
            list->indirect_state_capacity = capacity;
        }
        AgxD3d12IndirectState &state = list->indirect_states[list->indirect_state_count];
        memcpy(&state.list, list, sizeof(*list));
        state.signature = *signature;
        AgxD3d12Command *slot = AppendCommand(list);
        if (!slot) return;
        slot->kind = kCommandIndirectExpand;
        slot->indirect_expand.state = list->indirect_state_count++;
        slot->indirect_expand.indirect = indirect;
        return;
    }
    if (signature->kind == 3u)
        RecordDispatch(list, 0, 0, 0, &indirect);
    else
        RecordDraw(list, 0, 0, 0, 0, 0, signature->kind == 2u, &indirect);
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
    AGX_D3D12_BUFFER counter_buffers[kUavSlots]; /* C33 */
    AGX_D3D12_UAV uavs[kUavSlots];
    AGX_D3D12_CBV cbvs[kCbSlots];
    AGX_D3D12_SRV srvs[kSrvSlots]; /* EXP1839: only srvs[0, srv_count) are written */
    AGX_D3D12_SAMPLER samplers[kSamplerSlots];
    UINT srv_count;
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
/* C47: a sampler written into a sampler heap also gets its heap form (the
 * packed AGX sampler shaders take from the batch's sampler heap) */
static void FillSamplerForm(D3D12DDI_HDEVICE handle, D3D12DDI_CPU_DESCRIPTOR_HANDLE dest) {
    auto *out = reinterpret_cast<AgxD3d12SamplerDescriptor *>(dest.ptr);
    if (!out) return;
    AGX_D3D12_HEAP_SAMPLER *form = SamplerFormOf(out);
    memset(form, 0, sizeof(*form));
    AgxD3d12Device *device = DeviceOf(handle);
    AGX_D3D12_OWNER *owner = device && out->kind == kDescriptorSampler ? EnsureOwner(device) : nullptr;
    if (!owner) return;
    AGX_D3D12_SAMPLER sampler = {};
    OwnerSampler(out->desc, &sampler);
    if (FAILED(AgxD3d12OwnerSamplerDescriptor(owner, &sampler, form))) memset(form, 0, sizeof(*form));
}
/* C9/C10: the owner UAV of a UAV descriptor -- raw/structured or typed
 * buffer range, or (image u#) a texture level and slice range */
static bool DescriptorUav(const AgxD3d12Descriptor &view, AGX_D3D12_BUFFER *buffer, AGX_D3D12_UAV *uav) {
    memset(uav, 0, sizeof(*uav));
    if (!view.heap || (view.kind != kDescriptorUavBuffer && view.kind != kDescriptorUavTypedBuffer &&
                       view.kind != kDescriptorUavTexture))
        return false;
    *buffer = HeapBuffer(view.heap);
    uav->Buffer = buffer;
    if (view.kind == kDescriptorUavTexture) {
        uav->Format = view.format;
        uav->Texture = 1;
        uav->Level = view.level;
        uav->FirstLayer = view.first_layer;
        uav->Layers = view.layers;
        return true;
    }
    uav->Offset = view.offset;
    uav->Bytes = view.bytes;
    uav->Format = view.kind == kDescriptorUavTypedBuffer ? view.format : DXGI_FORMAT_UNKNOWN;
    return true;
}
/* C47: a UAV's heap form (texture and PBE descriptors of its image) */
static void PackUavHeapForm(AGX_D3D12_OWNER *owner, AgxD3d12Descriptor *view) {
    AgxD3d12HeapForm *form = HeapFormOf(view);
    AGX_D3D12_BUFFER buffer = {};
    AGX_D3D12_UAV uav = {};
    unsigned long long key = 0;
    if (!DescriptorUav(*view, &buffer, &uav)) {
        form->key = 0;
        return;
    }
    UINT counter_offset = 0;
    AGX_D3D12_BUFFER counter = {};
    if (AgxD3d12Heap *heap = UavCounter(*view, &counter_offset)) {
        counter = HeapBuffer(heap);
        uav.Counter = &counter;
        uav.CounterOffset = counter_offset;
    }
    form->key = SUCCEEDED(AgxD3d12OwnerUavDescriptor(owner, &uav, form->texture, form->pbe, form->counter, &key)) ? key : 0u;
}
static void OwnerBindings(const AgxD3d12Dispatch &b, AgxD3d12OwnerBindings *o) {
    /* EXP1839: only the SRV slots up to the last bound one are cleared and
     * handed on (EXP1838: two 14 KB clears per replayed draw, almost all
     * of it the 128-slot SRV table); srv_buffers are read through srvs */
    UINT srv_count = kSrvSlots;
    while (srv_count && !b.srv_descriptor[srv_count - 1] && !b.srv_heap[srv_count - 1]) --srv_count;
    memset(o->uav_buffers, 0, sizeof(o->uav_buffers));
    memset(o->cb_buffers, 0, sizeof(o->cb_buffers));
    memset(o->counter_buffers, 0, sizeof(o->counter_buffers));
    memset(o->uavs, 0, sizeof(o->uavs));
    memset(o->cbvs, 0, sizeof(o->cbvs));
    memset(o->srvs, 0, srv_count * sizeof(o->srvs[0]));
    memset(o->samplers, 0, sizeof(o->samplers));
    o->srv_count = srv_count;
    for (UINT u = 0; u < kUavSlots; ++u) {
        const AgxD3d12Descriptor *view = b.uav_descriptor[u];
        if (view && DescriptorUav(*view, &o->uav_buffers[u], &o->uavs[u])) {
            UINT counter_offset = 0;
            if (AgxD3d12Heap *counter = UavCounter(*view, &counter_offset)) {
                o->counter_buffers[u] = HeapBuffer(counter);
                o->uavs[u].Counter = &o->counter_buffers[u];
                o->uavs[u].CounterOffset = counter_offset;
            }
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
    for (UINT s = 0; s < srv_count; ++s) {
        const AgxD3d12Descriptor *view = b.srv_descriptor[s];
        if (view && view->kind == kDescriptorSrvBuffer && view->heap) {
            o->srv_buffers[s] = HeapBuffer(view->heap);
            o->srvs[s] = {&o->srv_buffers[s], view->offset, view->bytes};
            o->srvs[s].ComponentMapping = view->component_mapping;
            o->srvs[s].Format = view->format; /* C9: typed buffer SRVs; raw/structured UNKNOWN */
        } else if (view && view->kind == kDescriptorSrvTexture && view->heap) {
            o->srv_buffers[s] = HeapBuffer(view->heap);
            o->srvs[s].Buffer = &o->srv_buffers[s];
            o->srvs[s].Texture = 1;
            o->srvs[s].Cube = view->cube;
            o->srvs[s].ComponentMapping = view->component_mapping;
            o->srvs[s].Format = view->format;
            o->srvs[s].FirstLevel = view->level;
            o->srvs[s].Levels = view->levels;
            o->srvs[s].FirstLayer = view->first_layer;
            o->srvs[s].Layers = view->layers;
        } else if (b.srv_heap[s]) {
            o->srv_buffers[s] = HeapBuffer(b.srv_heap[s]);
            o->srvs[s] = {&o->srv_buffers[s], b.srv_offset[s], 0};
            o->srvs[s].ComponentMapping = AGX_D3D12_COMPONENT_MAPPING_IDENTITY;
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
/* C65S: a stage's slot samplers with reduction 2/3 (the rest 0), for the
 * min/max variant key */
static bool SlotMinmax(const AgxD3d12OwnerBindings &b, unsigned char out[kSamplerSlots]) {
    bool any = false;
    for (UINT s = 0; s < kSamplerSlots; ++s) {
        const unsigned bits = b.samplers[s].Valid ? dxbc_minmax_bits(b.samplers[s].Filter) : 0u;
        out[s] = static_cast<unsigned char>((bits & 3u) >= 2u ? bits : 0u);
        any |= out[s] != 0;
    }
    return any;
}
/* Replay of the large commands in their own (non-inlined) frames, so the
 * owner-binding records of one command kind are live at a time
 * (STACK_FRAMES gate: EXP1369 build measured 12944 bytes before this). */
/* C14: the owner record of an indirect argument stream */
static void OwnerIndirect(const AgxD3d12Indirect &in, AGX_D3D12_BUFFER *args, AGX_D3D12_BUFFER *count, AGX_D3D12_INDIRECT *out) {
    memset(out, 0, sizeof(*out));
    if (!in.args) return;
    *args = HeapBuffer(in.args);
    out->Buffer = args;
    out->Offset = in.args_offset;
    out->Stride = in.stride;
    out->MaxCount = in.max_count;
    if (in.count) {
        *count = HeapBuffer(in.count);
        out->CountBuffer = count;
        out->CountOffset = in.count_offset;
    }
}
/* C47: the textures one stage's heap ranges can reach (from the lowest range
 * start to the heap's end) join its batch; a heap reaching more is refused */
static constexpr UINT kHeapTextures = 4096;
struct AgxD3d12HeapScratch {
    AGX_D3D12_BUFFER heap;
    AGX_D3D12_BUFFER textures[kHeapTextures];
    AGX_D3D12_BUFFER writes[kHeapTextures];
    AGX_D3D12_HEAP_SAMPLER samplers[DXBC_HEAP_SAMPLER_MAP_MAX];
    AGX_D3D12_HEAP_BINDING binding;
};
/* EXP1854 receipt-only (measure-d3d12-heap-walk per execution): heap walks,
 * descriptors walked, re-packed descriptors and their time (QPC ticks); the
 * replay runs under the owner lock */
static struct { UINT walks, walked, packs; LONGLONG ticks; } g_heap_walk;
static HRESULT SetStageHeapWalk(AGX_D3D12_OWNER *owner, unsigned stage, const AgxD3d12Dispatch *b, AgxD3d12HeapScratch **scratch,
                                bool *minmax);
/* C65V: *minmax is set when the stage's sampler map holds a MINIMUM or
 * MAXIMUM sampler (the draw then takes the PSO's min/max variant) */
static HRESULT SetStageHeap(AGX_D3D12_OWNER *owner, unsigned stage, const AgxD3d12Dispatch *b, AgxD3d12HeapScratch **scratch,
                            bool *minmax) {
    if (!b || (!b->descriptors && !b->sampler_descriptors) || !b->heap_ranges)
        return AgxD3d12OwnerSetHeap(owner, stage, nullptr);
    LARGE_INTEGER t0 = {}, t1 = {};
    QueryPerformanceCounter(&t0);
    const HRESULT hr = SetStageHeapWalk(owner, stage, b, scratch, minmax);
    QueryPerformanceCounter(&t1);
    ++g_heap_walk.walks;
    g_heap_walk.ticks += t1.QuadPart - t0.QuadPart;
    return hr;
}
static HRESULT SetStageHeapWalk(AGX_D3D12_OWNER *owner, unsigned stage, const AgxD3d12Dispatch *b, AgxD3d12HeapScratch **scratch,
                                bool *minmax) {
    if (!*scratch) *scratch = static_cast<AgxD3d12HeapScratch *>(HeapAlloc(GetProcessHeap(), 0, sizeof(AgxD3d12HeapScratch)));
    if (!*scratch) return E_OUTOFMEMORY;
    AgxD3d12HeapScratch &h = **scratch;
    memset(&h.binding, 0, sizeof(h.binding));
    h.binding.Ranges = b->heap_ranges;
    h.binding.Textures = h.textures;
    h.binding.Writes = h.writes;
    h.binding.Samplers = h.samplers;
    UINT lowest = UINT_MAX, lowest_sampler = UINT_MAX;
    for (UINT r = 0; r < b->heap_ranges; ++r) {
        h.binding.First[r][0] = b->heap_first[r];
        UINT &low = b->heap_sampler_mask & (1ull << r) ? lowest_sampler : lowest;
        if (b->heap_first[r] < low) low = b->heap_first[r];
    }
    if (b->sampler_descriptors && lowest_sampler != UINT_MAX) {
        /* C47: the sampler map covers the heap from the lowest sampler range */
        const UINT samplers = static_cast<UINT>(b->sampler_descriptors->bytes / kSamplerDescriptorBytes);
        for (UINT r = 0; r < b->heap_ranges; ++r)
            if (b->heap_sampler_mask & (1ull << r)) h.binding.First[r][1] = lowest_sampler;
        for (UINT i = lowest_sampler; i < samplers && h.binding.SamplerCount < DXBC_HEAP_SAMPLER_MAP_MAX; ++i) {
            void *d = static_cast<BYTE *>(b->sampler_descriptors->cpu) + static_cast<SIZE_T>(i) * kSamplerDescriptorBytes;
            const bool valid = reinterpret_cast<const AgxD3d12SamplerDescriptor *>(d)->kind == kDescriptorSampler;
            h.samplers[h.binding.SamplerCount] = *SamplerFormOf(d);
            if (!valid) h.samplers[h.binding.SamplerCount].Valid = 0;
            /* the reduction bits of the map word (dxbc_minmax_bits << 8) */
            if (((h.samplers[h.binding.SamplerCount].Valid >> 8) & 3u) >= 2u) *minmax = true;
            ++h.binding.SamplerCount;
        }
    }
    if (!b->descriptors) return AgxD3d12OwnerSetHeap(owner, stage, &h.binding);
    h.heap = HeapBuffer(b->descriptors);
    h.binding.Heap = &h.heap;
    const UINT count = static_cast<UINT>(b->descriptors->bytes / kCbvSrvUavDescriptorBytes);
    for (UINT i = lowest; i < count; ++i) {
        auto *view = reinterpret_cast<AgxD3d12Descriptor *>(static_cast<BYTE *>(b->descriptors->cpu) +
                                                            static_cast<SIZE_T>(i) * kCbvSrvUavDescriptorBytes);
        const bool uav = UavDescriptorKind(view->kind);
        if ((view->kind != kDescriptorSrvTexture && view->kind != kDescriptorSrvBuffer && !uav) || !view->heap) continue;
        const AGX_D3D12_BUFFER texture = HeapBuffer(view->heap);
        ++g_heap_walk.walked;
        /* Asahi re-laid the texture out since it was packed (agx_upload_textures re-packs bound views too) */
        if (HeapFormOf(view)->key != AgxD3d12OwnerLayoutKey(owner, &texture)) {
            PackHeapForm(owner, view);
            MirrorHeapForms(b->descriptors, view, 1u);
            ++g_heap_walk.packs;
        }
        if (uav) {
            /* C47: what heap UAVs write joins the batch as written */
            if (h.binding.WriteCount == kHeapTextures) {
                const UINT receipt[] = {stage, i, count};
                AdmissionUmdDiagnostic("reject-d3d12-heap-textures", E_OUTOFMEMORY, receipt, 3);
                return E_OUTOFMEMORY;
            }
            h.writes[h.binding.WriteCount++] = texture;
            UINT counter_offset = 0;
            if (AgxD3d12Heap *counter = UavCounter(*view, &counter_offset)) {
                if (h.binding.WriteCount == kHeapTextures) return E_OUTOFMEMORY;
                h.writes[h.binding.WriteCount++] = HeapBuffer(counter);
            }
            continue;
        }
        if (h.binding.TextureCount == kHeapTextures) {
            const UINT receipt[] = {stage, i, count};
            AdmissionUmdDiagnostic("reject-d3d12-heap-textures", E_OUTOFMEMORY, receipt, 3);
            return E_OUTOFMEMORY;
        }
        h.textures[h.binding.TextureCount++] = texture;
    }
    return AgxD3d12OwnerSetHeap(owner, stage, &h.binding);
}
static __declspec(noinline) HRESULT ReplayDispatch(AGX_D3D12_OWNER *owner, const AgxD3d12Dispatch &dispatch) {
    AgxD3d12OwnerBindings bind;
    OwnerBindings(dispatch, &bind);
    AgxD3d12HeapScratch *heap = nullptr;
    bool minmax = false;
    const HRESULT heap_hr = SetStageHeap(owner, AGX_D3D12_HEAP_CS, &dispatch, &heap, &minmax);
    if (FAILED(heap_hr)) {
        if (heap) HeapFree(GetProcessHeap(), 0, heap);
        return heap_hr;
    }
    /* C65V/C65S: a MINIMUM/MAXIMUM heap or slot sampler dispatches with a min/max variant */
    AgxD3d12MinmaxKey key = {};
    key.heap = minmax;
    const bool slots = SlotMinmax(bind, key.slot[AGX_D3D12_HEAP_CS]);
    const void *variant = minmax || slots ? MinmaxVariant(owner, dispatch.pso, key, true) : nullptr;
    const AGX_D3D12_COMPUTE *pipeline = variant ? static_cast<const AGX_D3D12_COMPUTE *>(variant) : &dispatch.pso->compute;
    /* AgxD3d12OwnerDispatch reads every SRV slot */
    memset(bind.srvs + bind.srv_count, 0, (kSrvSlots - bind.srv_count) * sizeof(bind.srvs[0]));
    AGX_D3D12_BUFFER args = {}, count = {};
    AGX_D3D12_INDIRECT indirect;
    OwnerIndirect(dispatch.indirect, &args, &count, &indirect);
    const HRESULT hr = AgxD3d12OwnerDispatch(owner, pipeline, bind.uavs, bind.cbvs, bind.srvs, bind.samplers,
                                             dispatch.groups[0], dispatch.groups[1], dispatch.groups[2],
                                             indirect.Buffer ? &indirect : nullptr);
    if (heap) HeapFree(GetProcessHeap(), 0, heap);
    return hr;
}
struct AgxD3d12DrawScratch {
    AGX_D3D12_BUFFER rtv_textures[8], vb_buffers[kVertexBuffers], dsv_texture, index_buffer, indirect_args, indirect_count;
    AGX_D3D12_BUFFER so_buffers[kSoTargets], so_filled[kSoTargets]; /* C39 */
    AGX_D3D12_DRAW draw;
    AgxD3d12OwnerBindings vs_bind, ps_bind, gs_bind, hs_bind, ds_bind;
    AgxD3d12OwnerBindings none; /* zeroed: a pipeline stage without recorded bindings */
    AgxD3d12HeapScratch *heaps[AGX_D3D12_HEAP_CS]; /* C47: allocated by the first heap range */
};
/* EXP1838: one scratch per execution (EXP1837: a ~120 KB allocation per
 * replayed draw), zeroed once for its empty binding set */
static AgxD3d12DrawScratch *NewDrawScratch() {
    auto *scratch = static_cast<AgxD3d12DrawScratch *>(HeapAlloc(GetProcessHeap(), 0, sizeof(AgxD3d12DrawScratch)));
    if (scratch) memset(&scratch->none, 0, sizeof(scratch->none));
    if (scratch) memset(scratch->heaps, 0, sizeof(scratch->heaps));
    return scratch;
}
static void FreeDrawScratch(AgxD3d12DrawScratch *scratch) {
    if (!scratch) return;
    for (AgxD3d12HeapScratch *heap : scratch->heaps)
        if (heap) HeapFree(GetProcessHeap(), 0, heap);
    HeapFree(GetProcessHeap(), 0, scratch);
}
/* C27/C37: the bindings of a draw's optional stages (recorded slots) */
struct AgxD3d12OptionalBinds {
    const AgxD3d12Dispatch *gs, *hs, *ds;
};
static void StageBindings(const AgxD3d12OwnerBindings &bind, decltype(AGX_D3D12_DRAW::Gs) *out) {
    out->Cbvs = bind.cbvs;
    out->Srvs = bind.srvs;
    out->SrvCount = bind.srv_count;
    out->Samplers = bind.samplers;
    out->Uavs = bind.uavs;
}
static void OwnerStageBindings(const AgxD3d12Dispatch &recorded, AgxD3d12OwnerBindings *bind, decltype(AGX_D3D12_DRAW::Gs) *out) {
    OwnerBindings(recorded, bind);
    StageBindings(*bind, out);
}
/* a recorded optional-stage slot fills pending; true when it was one */
static bool TakeOptionalBind(const AgxD3d12Command &command, AgxD3d12OptionalBinds *pending) {
    const AgxD3d12Dispatch **slot = command.kind == kCommandGsBind ? &pending->gs
                                  : command.kind == kCommandHsBind ? &pending->hs
                                  : command.kind == kCommandDsBind ? &pending->ds : nullptr;
    if (slot) *slot = &command.dispatch;
    return slot != nullptr;
}
/* only the stages the draw's pipeline has (a stage bound once must not
 * reach later draws) */
static AgxD3d12OptionalBinds DrawOptionalBinds(const AgxD3d12Draw &draw, const AgxD3d12OptionalBinds &pending) {
    return {draw.pso->gs_dwords ? pending.gs : nullptr, draw.pso->hs_dwords ? pending.hs : nullptr,
            draw.pso->ds_dwords ? pending.ds : nullptr};
}
static __declspec(noinline) HRESULT ReplayDraw(AGX_D3D12_OWNER *owner, const AgxD3d12Draw &d, const AgxD3d12OptionalBinds &optional,
                                                AgxD3d12DrawScratch *scratch) {
    /* the draw's owner records live in the caller's scratch (NewDrawScratch) */
    if (!scratch) return E_OUTOFMEMORY;
    AGX_D3D12_BUFFER(&rtv_textures)[8] = scratch->rtv_textures;
    AGX_D3D12_BUFFER(&vb_buffers)[kVertexBuffers] = scratch->vb_buffers;
    AGX_D3D12_DRAW &draw = scratch->draw;
    memset(&draw, 0, sizeof(draw));
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
    for (UINT t = 0; t < kSoTargets; ++t) { /* C39 */
        if (!d.so[t].heap || !d.so[t].filled) continue;
        scratch->so_buffers[t] = HeapBuffer(d.so[t].heap);
        scratch->so_filled[t] = HeapBuffer(d.so[t].filled);
        draw.So[t].Buffer = &scratch->so_buffers[t];
        draw.So[t].Filled = &scratch->so_filled[t];
        draw.So[t].Offset = d.so[t].offset;
        draw.So[t].Size = d.so[t].size;
        draw.So[t].FilledOffset = d.so[t].filled_offset;
    }
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
    StageBindings(vs_bind, &draw.Vs);
    StageBindings(ps_bind, &draw.Ps); /* C13: graphics UAVs bind per stage too */
    if (optional.gs) OwnerStageBindings(*optional.gs, &scratch->gs_bind, &draw.Gs); /* C27 */
    else StageBindings(scratch->none, &draw.Gs);
    if (optional.hs) OwnerStageBindings(*optional.hs, &scratch->hs_bind, &draw.Hs); /* C37 */
    else StageBindings(scratch->none, &draw.Hs);
    if (optional.ds) OwnerStageBindings(*optional.ds, &scratch->ds_bind, &draw.Ds);
    else StageBindings(scratch->none, &draw.Ds);
    OwnerIndirect(d.indirect, &scratch->indirect_args, &scratch->indirect_count, &draw.Indirect); /* C14 */
    /* C47: every stage's heap binding (or none), consumed by the draw */
    const AgxD3d12Dispatch *heaps[AGX_D3D12_HEAP_CS] = {&d.vs_bind, &d.ps_bind, optional.gs, optional.hs, optional.ds};
    bool minmax = false;
    for (unsigned s = 0; s < AGX_D3D12_HEAP_CS; ++s) {
        const HRESULT hr = SetStageHeap(owner, s, heaps[s], &scratch->heaps[s], &minmax);
        if (FAILED(hr)) return hr;
    }
    /* C65V/C65S: a MINIMUM/MAXIMUM heap or slot sampler draws with a min/max variant */
    AgxD3d12MinmaxKey key = {};
    key.heap = minmax;
    bool slots = SlotMinmax(vs_bind, key.slot[AGX_D3D12_HEAP_VS]);
    slots |= SlotMinmax(ps_bind, key.slot[AGX_D3D12_HEAP_PS]);
    if (optional.gs) slots |= SlotMinmax(scratch->gs_bind, key.slot[AGX_D3D12_HEAP_GS]);
    if (optional.hs) slots |= SlotMinmax(scratch->hs_bind, key.slot[AGX_D3D12_HEAP_HS]);
    if (optional.ds) slots |= SlotMinmax(scratch->ds_bind, key.slot[AGX_D3D12_HEAP_DS]);
    if (minmax || slots)
        if (void *variant = MinmaxVariant(owner, d.pso, key, false)) draw.Pipeline = static_cast<AGX_D3D12_GRAPHICS *>(variant);
    return AgxD3d12OwnerDraw(owner, &draw);
}
#endif
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
/* C31: one CPU-expanded ExecuteIndirect. The scratch list starts as the
 * recorded state; records apply their arguments in order and each one's
 * draw or dispatch is recorded on the scratch list and replayed at once. */
static HRESULT ReplayIndirectExpand(AGX_D3D12_OWNER *owner, const AgxD3d12CommandList *list,
                                    const AgxD3d12IndirectExpand &expand) {
    if (expand.state >= list->indirect_state_count) return E_INVALIDARG;
    const AgxD3d12IndirectState &state = list->indirect_states[expand.state];
    const AgxD3d12CommandSignature &signature = state.signature;
    const AgxD3d12Indirect &in = expand.indirect;
    const bool graphics = signature.kind != 3u;
    HRESULT hr = S_OK;
    UINT count = in.max_count;
    if (in.count) {
        UINT limit = 0;
        AGX_D3D12_BUFFER buffer = HeapBuffer(in.count);
        hr = AgxD3d12OwnerReadBuffer(owner, &buffer, in.count_offset, sizeof(limit), &limit);
        if (FAILED(hr)) return hr;
        if (limit < count) count = limit;
    }
    UINT drawn = 0, skipped = 0;
    AgxD3d12DrawScratch *draw_scratch = nullptr; /* EXP1838: one per expansion */
    if (count) {
        const UINT64 bytes = static_cast<UINT64>(count - 1u) * signature.stride + signature.record_bytes;
        if (bytes > (64ULL << 20)) return E_INVALIDARG;
        auto *args = static_cast<unsigned char *>(HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(bytes)));
        auto *scratch = static_cast<AgxD3d12CommandList *>(HeapAlloc(GetProcessHeap(), 0, sizeof(AgxD3d12CommandList)));
        if (!args || !scratch) hr = E_OUTOFMEMORY;
        if (SUCCEEDED(hr)) {
            AGX_D3D12_BUFFER buffer = HeapBuffer(in.args);
            hr = AgxD3d12OwnerReadBuffer(owner, &buffer, in.args_offset, static_cast<UINT>(bytes), args);
        }
        if (SUCCEEDED(hr)) {
            memcpy(scratch, &state.list, sizeof(*scratch));
            scratch->type = static_cast<UINT>(D3D12DDI_COMMAND_LIST_TYPE_DIRECT);
            scratch->command = nullptr;
            scratch->capacity = scratch->commands = 0;
            scratch->bundle = nullptr;
            scratch->bundle_bytes = scratch->bundle_capacity = 0;
            scratch->indirect_states = nullptr;
            scratch->indirect_state_count = scratch->indirect_state_capacity = 0;
            scratch->replay = 1;
        }
        const D3D12DDI_HCOMMANDLIST handle = {scratch};
        for (UINT i = 0; SUCCEEDED(hr) && i < count; ++i) {
            const unsigned char *record = args + static_cast<SIZE_T>(i) * signature.stride;
            scratch->commands = 0;
            for (UINT a = 0; a + 1u < signature.arguments; ++a) {
                const AgxD3d12SignatureArgument &arg = signature.argument[a];
                const unsigned char *at = record + arg.offset;
                UINT64 va = 0;
                if (arg.type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT) {
                    SetRoot32BitConstants(scratch, graphics, arg.param, arg.count, at, arg.dest);
                    continue;
                }
                if (arg.type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW) {
                    D3D12DDI_VERTEX_BUFFER_VIEW view;
                    memcpy(&view, at, sizeof(view));
                    AGX_VB_SETTER(handle, arg.param, 1, &view);
                    continue;
                }
                if (arg.type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW) {
                    D3D12DDI_INDEX_BUFFER_VIEW view;
                    memcpy(&view, at, sizeof(view));
                    AGX_IB_SETTER(handle, &view);
                    continue;
                }
                memcpy(&va, at, sizeof(va));
                const UINT root_type = arg.type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW ? D3D12DDI_ROOT_PARAMETER_TYPE_CBV
                                     : arg.type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW ? D3D12DDI_ROOT_PARAMETER_TYPE_SRV
                                                                                                        : D3D12DDI_ROOT_PARAMETER_TYPE_UAV;
                SetRootBufferView(scratch, arg.param, va, root_type, graphics);
            }
            const unsigned char *op = record + signature.argument[signature.arguments - 1u].offset;
            UINT v[5] = {};
            memcpy(v, op, signature.kind == 2u ? 20u : signature.kind == 1u ? 16u : 12u);
            if (signature.kind == 3u) RecordDispatch(scratch, v[0], v[1], v[2], nullptr);
            else if (signature.kind == 2u) RecordDraw(scratch, v[0], v[1], v[2], static_cast<INT>(v[3]), v[4], true);
            else RecordDraw(scratch, v[0], v[1], v[2], 0, v[3], false);
            if (!scratch->commands) ++skipped; /* an empty or refused record */
            AgxD3d12OptionalBinds pending = {};
            for (UINT c = 0; SUCCEEDED(hr) && c < scratch->commands; ++c) {
                const AgxD3d12Command &command = scratch->command[c];
                if (TakeOptionalBind(command, &pending)) {
                } else if (command.kind == kCommandDraw) {
                    if (!draw_scratch) draw_scratch = NewDrawScratch();
                    hr = ReplayDraw(owner, command.draw, DrawOptionalBinds(command.draw, pending), draw_scratch);
                    pending = {};
                    ++drawn;
                } else if (command.kind == kCommandDispatch) {
                    hr = ReplayDispatch(owner, command.dispatch);
                    ++drawn;
                }
            }
        }
        if (scratch && scratch->command) HeapFree(GetProcessHeap(), 0, scratch->command);
        if (scratch) HeapFree(GetProcessHeap(), 0, scratch);
        if (args) HeapFree(GetProcessHeap(), 0, args);
        FreeDrawScratch(draw_scratch);
    }
    const UINT receipt[] = {signature.kind, signature.arguments, count, drawn, skipped, static_cast<UINT>(hr)};
    AdmissionUmdDiagnostic("measure-d3d12-indirect-expand", hr, receipt, 6);
    return hr;
}
#endif
static void ReplayAndSubmit(UINT count, const D3D12DDI_HCOMMANDLIST *lists) {
    UINT commands = 0;
    HRESULT hr = S_OK;
    /* the receipt splits the execution into replay and submit+wait time
     * (microseconds): the owner submits synchronously, so an application's
     * ExecuteCommandLists time is both */
    LARGE_INTEGER frequency = {}, start = {}, replayed = {}, submitted = {};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    AgxD3d12Device *device = nullptr;
    AgxD3d12DrawScratch *draw_scratch = nullptr; /* EXP1838: one per execution */
#endif
    for (UINT i = 0; lists && i < count; ++i) {
        auto *list = static_cast<AgxD3d12CommandList *>(lists[i].pDrvPrivate);
        if (!list) continue;
        commands += list->commands;
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
        AgxD3d12OptionalBinds pending = {}; /* C27/C37 */
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
                        hr = AgxD3d12OwnerCopyTextureRegion(owner, &dst, level, 0, 0, 0, &src, level, 0, 0, 0, w, h, rc.volume ? (rc.layers>>level ? rc.layers>>level:1u):rc.layers);
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
            } else if (command.kind == kCommandIndirectExpand) {
                hr = ReplayIndirectExpand(owner, list, command.indirect_expand);
            } else if (command.kind == kCommandResolve) {
                const AgxD3d12Resolve &r = command.resolve;
                AGX_D3D12_BUFFER dst = HeapBuffer(r.dst), src = HeapBuffer(r.src);
                hr = AgxD3d12OwnerResolve(owner, &dst, r.dst_level, r.dst_layer, &src, r.src_layer, r.format);
            } else if (TakeOptionalBind(command, &pending)) { /* C27/C37: consumed by the next draw */
            } else if (command.kind == kCommandDraw) {
                if (!draw_scratch) draw_scratch = NewDrawScratch();
                hr = ReplayDraw(owner, command.draw, DrawOptionalBinds(command.draw, pending), draw_scratch);
                pending = {};
            } else if (command.kind == kCommandQuery) {
                const AgxD3d12QueryCommand &q = command.query;
                auto *queries = static_cast<AGX_D3D12_QUERY_HEAP *>(q.heap->owner);
                if (q.op == kQueryResolve) {
                    AGX_D3D12_BUFFER dst = HeapBuffer(q.dst);
                    hr = AgxD3d12OwnerResolveQueries(owner, queries, q.type, q.index, q.count, &dst, q.dst_offset);
                } else {
                    hr = AgxD3d12OwnerQuery(owner, queries, q.type, q.index, q.op == kQueryBegin);
                }
                const UINT receipt[] = {q.op, q.type, q.index, q.count};
                if (FAILED(hr)) AdmissionUmdDiagnostic("measure-d3d12-query", hr, receipt, 4);
            } else if (command.kind == kCommandClearUav) {
                const AgxD3d12ClearUav &clear = command.clear_uav;
                AGX_D3D12_BUFFER buffer = {};
                AGX_D3D12_UAV uav = {};
                hr = DescriptorUav(clear.view, &buffer, &uav)
                         ? AgxD3d12OwnerClearUav(owner, &uav, clear.values, static_cast<int>(clear.is_float),
                                                 clear.rects ? clear.rect : nullptr, clear.rects)
                         : E_INVALIDARG;
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
    QueryPerformanceCounter(&replayed);
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    FreeDrawScratch(draw_scratch);
    if (SUCCEEDED(hr) && device && device->owner)
        hr = AgxD3d12OwnerSubmitAndWait(static_cast<AGX_D3D12_OWNER *>(device->owner));
    if (FAILED(hr) && device && device->callbacks && device->callbacks->pfnSetErrorCb)
        device->callbacks->pfnSetErrorCb(device->runtime, hr);
#endif
    QueryPerformanceCounter(&submitted);
    const LONGLONG hz = frequency.QuadPart ? frequency.QuadPart : 1;
    const UINT receipt[] = {count, commands, static_cast<UINT>((replayed.QuadPart - start.QuadPart) * 1000000 / hz),
                            static_cast<UINT>((submitted.QuadPart - replayed.QuadPart) * 1000000 / hz)};
    AdmissionUmdDiagnostic("measure-d3d12-execute", hr, receipt, 4);
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    if (g_heap_walk.walks) {
        const UINT walk[] = {g_heap_walk.walks, g_heap_walk.walked, g_heap_walk.packs,
                             static_cast<UINT>(g_heap_walk.ticks * 1000000 / hz)};
        AdmissionUmdDiagnostic("measure-d3d12-heap-walk", S_OK, walk, 4);
        memset(&g_heap_walk, 0, sizeof(g_heap_walk));
    }
#endif
}
/* EXP1822: one replay and its submission run under the device owner's lock,
 * so lists executed from several threads (the nBody async compute thread
 * and its render thread) never interleave on the one pipe_context. */
static VOID APIENTRY ExecuteCommandLists(D3D12DDI_HCOMMANDQUEUE, UINT count, const D3D12DDI_HCOMMANDLIST *lists) {
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    AGX_D3D12_OWNER *owner = nullptr;
    for (UINT i = 0; lists && i < count && !owner; ++i)
        if (const auto *list = static_cast<const AgxD3d12CommandList *>(lists[i].pDrvPrivate))
            owner = list->device ? static_cast<AGX_D3D12_OWNER *>(list->device->owner) : nullptr;
    AgxD3d12OwnerLock(owner);
    ReplayAndSubmit(count, lists);
    AgxD3d12OwnerUnlock(owner);
#else
    ReplayAndSubmit(count, lists);
#endif
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
static VOID APIENTRY CreateSamplerInHeap(D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATE_SAMPLER *arg,
                                         D3D12DDI_CPU_DESCRIPTOR_HANDLE dest) {
    CreateSampler(handle, arg, dest);
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    FillSamplerForm(handle, dest); /* C47 */
#endif
}
static void FillD3D12DDI_DEVICE_FUNCS_CORE_0003(D3D12DDI_DEVICE_FUNCS_CORE_0003 &f) {
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckFormatSupport, CheckFormatSupport);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckMultisampleQualityLevels, CheckMultisampleQualityLevels);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetMipPacking, AGX_TILED_DEVICE(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetMipPacking, GetMipPacking));
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
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateGeometryShaderWithStreamOutput, CalcPrivateGeometryShaderWithStreamOutput); /* C39 */
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateGeometryShaderWithStreamOutput, CreateGeometryShaderWithStreamOutput);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateTessellationShaderSize, CalcPrivateTessellationShaderSize); /* C37 */
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateHullShader, CreateTessellationShader);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDomainShader, CreateTessellationShader);
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
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateShaderResourceView, CreateShaderResourceViewInHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateConstantBufferView, CreateConstantBufferViewInHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateSampler, CreateSamplerInHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateUnorderedAccessView, CreateUnorderedAccessViewInHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRenderTargetView, CreateRenderTargetView);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDepthStencilView, CreateDepthStencilView);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateRootSignatureSize, CalcPrivateRootSignatureSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRootSignature, CreateRootSignature);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyRootSignature, DestroyRootSignature);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnSerializeObject, SerializeObject); /* C38 */
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyObjectSerialization, DestroyObjectSerialization);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDeserializedObjectSize, CalcPrivateDeserializedObjectSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDeserializedObject, CreateDeserializedObject);
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
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateQueryHeapSize, CalcPrivateQueryHeapSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateQueryHeap, CreateQueryHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyQueryHeap, DestroyQueryHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandSignatureSize, CalcPrivateCommandSignatureSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandSignature, CreateCommandSignature);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandSignature, DestroyCommandSignature);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceVirtualAddress, CheckResourceVirtualAddress);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceAllocationInfo, CheckResourceAllocationInfo);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckSubresourceInfo, CheckSubresourceInfo);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckExistingResourceAllocationInfo, CheckExistingResourceAllocationInfo);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnOfferResources, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnOfferResources));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnReclaimResources, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnReclaimResources));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetImplicitPhysicalAdapterMask, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetImplicitPhysicalAdapterMask));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetPresentPrivateDriverDataSize, GetPresentPrivateDriverDataSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnQueryNodeMap, QueryNodeMap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnRetrieveShaderComment, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnRetrieveShaderComment));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceAllocationHandle, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceAllocationHandle));
}
/* C44: the R1 device table (interface 0011): the 0003 table's entries with
 * the 0010 state, shader and pipeline-state adapters, without the object
 * serialization entries, plus the pipeline-library entries */
/* Receipt-only (EXP1862/1863: a CachedPSO creation under R1 ends in a
 * D3D12 heap_failure_invalid_argument freeing the runtime's temporary
 * CLibrary): the runtime's pipeline-library calls, their initial-data size,
 * the private storage address and the result, forwarded unchanged. */
static UINT LibraryAddress(D3D12DDI_HPIPELINELIBRARY handle) {
    return static_cast<UINT>(reinterpret_cast<UINT_PTR>(handle.pDrvPrivate));
}
static SIZE_T APIENTRY ReceiptCalcPrivatePipelineLibrarySize(D3D12DDI_HDEVICE device,
                                                            const D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 *args) {
    const SIZE_T size = AgxD3d12ZeroCacheCalcPrivatePipelineLibrarySize(device, args);
    const UINT receipt[] = {1u, args ? static_cast<UINT>(args->InitialDataSize) : ~0u, static_cast<UINT>(size)};
    AdmissionUmdDiagnostic("measure-d3d12-library", S_OK, receipt, 3);
    return size;
}
static HRESULT APIENTRY ReceiptCreatePipelineLibrary(D3D12DDI_HDEVICE device,
                                                     const D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 *args,
                                                     D3D12DDI_HPIPELINELIBRARY handle) {
    const HRESULT hr = AgxD3d12ZeroCacheCreatePipelineLibrary(device, args, handle);
    const UINT receipt[] = {2u, args ? static_cast<UINT>(args->InitialDataSize) : ~0u, LibraryAddress(handle)};
    AdmissionUmdDiagnostic("measure-d3d12-library", hr, receipt, 3);
    return hr;
}
static VOID APIENTRY ReceiptDestroyPipelineLibrary(D3D12DDI_HDEVICE device, D3D12DDI_HPIPELINELIBRARY handle) {
    const UINT receipt[] = {3u, 0u, LibraryAddress(handle)};
    AdmissionUmdDiagnostic("measure-d3d12-library", S_OK, receipt, 3);
    AgxD3d12ZeroCacheDestroyPipelineLibrary(device, handle);
}
static HRESULT APIENTRY ReceiptAddPipelineStateToLibrary(D3D12DDI_HDEVICE device, D3D12DDI_HPIPELINELIBRARY handle,
                                                         D3D12DDI_HPIPELINESTATE pipeline, UINT index) {
    const HRESULT hr = AgxD3d12ZeroCacheAddPipelineStateToLibrary(device, handle, pipeline, index);
    const UINT receipt[] = {4u, index, LibraryAddress(handle)};
    AdmissionUmdDiagnostic("measure-d3d12-library", hr, receipt, 3);
    return hr;
}
static SIZE_T APIENTRY ReceiptCalcSerializedLibrarySize(D3D12DDI_HDEVICE device, D3D12DDI_HPIPELINELIBRARY handle) {
    const SIZE_T size = AgxD3d12ZeroCacheCalcSerializedLibrarySize(device, handle);
    const UINT receipt[] = {5u, static_cast<UINT>(size), LibraryAddress(handle)};
    AdmissionUmdDiagnostic("measure-d3d12-library", S_OK, receipt, 3);
    return size;
}
static HRESULT APIENTRY ReceiptSerializeLibrary(D3D12DDI_HDEVICE device, D3D12DDI_HPIPELINELIBRARY handle, VOID *output) {
    const HRESULT hr = AgxD3d12ZeroCacheSerializeLibrary(device, handle, output);
    const UINT receipt[] = {6u, 0u, LibraryAddress(handle)};
    AdmissionUmdDiagnostic("measure-d3d12-library", hr, receipt, 3);
    return hr;
}
/* C50 (R3 build 3, D3D12-C50-interface-0033.md): CORE_0033 has CORE_0012's
 * members with newer argument types. Each adapter projects the 0033 argument
 * onto the validated R1/0003 entry; a field without a 0003 equivalent is
 * refused unless it holds the value the 0003 runtime implied. */
static D3D12DDIARG_CREATE_SHADER_0010 ProjectR3Shader(const D3D12DDIARG_CREATE_SHADER_0026 &arg) {
    /* _0026 adds a mesh-signature union member and ShaderCodeHash (the
     * shader-cache hash; no cache session is offered) */
    D3D12DDIARG_CREATE_SHADER_0010 legacy = {};
    static_assert(sizeof(legacy.IOSignatures) == sizeof(arg.IOSignatures));
    legacy.hRootSignature = arg.hRootSignature;
    legacy.pShaderCode = arg.pShaderCode;
    memcpy(&legacy.IOSignatures, &arg.IOSignatures, sizeof(legacy.IOSignatures));
    legacy.Flags = arg.Flags;
    legacy.LibraryReference = arg.LibraryReference;
    return legacy;
}
static SIZE_T APIENTRY CalcPrivateShaderSize0026(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_SHADER_0026 *arg) {
    if (!arg) return CalcPrivateShaderSize0010(device, nullptr);
    const D3D12DDIARG_CREATE_SHADER_0010 legacy = ProjectR3Shader(*arg);
    return CalcPrivateShaderSize0010(device, &legacy);
}
static VOID APIENTRY CreateShader0026(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_SHADER_0026 *arg,
                                      D3D12DDI_HSHADER handle) {
    if (!arg) return CreateShader0010(device, nullptr, handle);
    const D3D12DDIARG_CREATE_SHADER_0010 legacy = ProjectR3Shader(*arg);
    CreateShader0010(device, &legacy, handle);
}
static VOID APIENTRY CreateComputeShader0026(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_SHADER_0026 *arg,
                                             D3D12DDI_HSHADER handle) {
    if (!arg) return CreateComputeShader0010(device, nullptr, handle);
    const D3D12DDIARG_CREATE_SHADER_0010 legacy = ProjectR3Shader(*arg);
    CreateComputeShader0010(device, &legacy, handle);
}
static VOID APIENTRY CreateTessellationShader0026(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_SHADER_0026 *arg,
                                                  D3D12DDI_HSHADER handle) {
    if (!arg) return CreateTessellationShader0010(device, nullptr, handle);
    const D3D12DDIARG_CREATE_SHADER_0010 legacy = ProjectR3Shader(*arg);
    CreateTessellationShader0010(device, &legacy, handle);
}
/* the embedded _0026 shader moves every stream-output field: projected, not cast */
static D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010 ProjectR3StreamOutput(
    const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0026 &arg) {
    D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010 legacy = {};
    legacy.CreateShader = ProjectR3Shader(arg.CreateShader);
    legacy.pOutputStreamDecl = arg.pOutputStreamDecl;
    legacy.NumEntries = arg.NumEntries;
    legacy.BufferStridesInBytes = arg.BufferStridesInBytes;
    legacy.NumStrides = arg.NumStrides;
    legacy.RasterizedStream = arg.RasterizedStream;
    return legacy;
}
static SIZE_T APIENTRY CalcPrivateGeometryShaderWithStreamOutput0026(
    D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0026 *arg) {
    if (!arg) return CalcPrivateGeometryShaderWithStreamOutput0010(device, nullptr);
    const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010 legacy = ProjectR3StreamOutput(*arg);
    return CalcPrivateGeometryShaderWithStreamOutput0010(device, &legacy);
}
static VOID APIENTRY CreateGeometryShaderWithStreamOutput0026(
    D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0026 *arg, D3D12DDI_HSHADER handle) {
    if (!arg) return CreateGeometryShaderWithStreamOutput0010(device, nullptr, handle);
    const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010 legacy = ProjectR3StreamOutput(*arg);
    CreateGeometryShaderWithStreamOutput0010(device, &legacy, handle);
}
static_assert(std::is_same_v<decltype(&CalcPrivateShaderSize0026), PFND3D12DDI_CALC_PRIVATE_SHADER_SIZE_0026>);
static_assert(std::is_same_v<decltype(&CreateShader0026), PFND3D12DDI_CREATE_SHADER_0026>);
static_assert(std::is_same_v<decltype(&CalcPrivateGeometryShaderWithStreamOutput0026),
                             PFND3D12DDI_CALC_PRIVATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0026>);
static_assert(std::is_same_v<decltype(&CreateGeometryShaderWithStreamOutput0026),
                             PFND3D12DDI_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0026>);
/* _0025 = _0010 + DepthBoundsTestEnable; DepthBoundsTestSupported is FALSE */
static bool ProjectR3Depth(const D3D12DDI_DEPTH_STENCIL_DESC_0025 *desc, D3D12DDI_DEPTH_STENCIL_DESC_0010 *legacy) {
    static_assert(offsetof(D3D12DDI_DEPTH_STENCIL_DESC_0025, LibraryReference) ==
                  offsetof(D3D12DDI_DEPTH_STENCIL_DESC_0010, LibraryReference));
    static_assert(offsetof(D3D12DDI_DEPTH_STENCIL_DESC_0025, DepthBoundsTestEnable) >=
                  sizeof(D3D12DDI_DEPTH_STENCIL_DESC_0010));
    if (!desc || desc->DepthBoundsTestEnable) return false;
    memcpy(legacy, desc, sizeof(*legacy));
    return true;
}
static SIZE_T APIENTRY CalcPrivateDepthStencilStateSize0025(D3D12DDI_HDEVICE device,
    const D3D12DDI_DEPTH_STENCIL_DESC_0025 *desc) {
    D3D12DDI_DEPTH_STENCIL_DESC_0010 legacy = {};
    return ProjectR3Depth(desc, &legacy) ? CalcPrivateDepthStencilStateSize0010(device, &legacy) : 0;
}
static VOID APIENTRY CreateDepthStencilState0025(D3D12DDI_HDEVICE device,
    const D3D12DDI_DEPTH_STENCIL_DESC_0025 *desc, D3D12DDI_HDEPTHSTENCILSTATE handle) {
    D3D12DDI_DEPTH_STENCIL_DESC_0010 legacy = {};
    if (!ProjectR3Depth(desc, &legacy)) {
        ReportRejectedDeviceError(device, E_INVALIDARG);
        return;
    }
    CreateDepthStencilState0010(device, &legacy, handle);
}
static_assert(std::is_same_v<decltype(&CalcPrivateDepthStencilStateSize0025),
                             PFND3D12DDI_CALCPRIVATEDEPTHSTENCILSTATESIZE_0025>);
static_assert(std::is_same_v<decltype(&CreateDepthStencilState0025), PFND3D12DDI_CREATEDEPTHSTENCILSTATE_0025>);
/* _0033 = _0010 + ViewInstancingDesc. ViewInstancingTier is NOT_SUPPORTED:
 * the View Instancing spec still requires PSOs declaring 0 or 1 instance
 * (SV_ViewID reads 0); masking and other locations are refused. */
static bool ProjectR3PipelineState(const D3D12DDIARG_CREATE_PIPELINE_STATE_0033 *arg,
                                   D3D12DDIARG_CREATE_PIPELINE_STATE_0010 *legacy) {
    static_assert(offsetof(D3D12DDIARG_CREATE_PIPELINE_STATE_0033, LibraryReference) ==
                  offsetof(D3D12DDIARG_CREATE_PIPELINE_STATE_0010, LibraryReference));
    static_assert(offsetof(D3D12DDIARG_CREATE_PIPELINE_STATE_0033, ViewInstancingDesc) >=
                  sizeof(D3D12DDIARG_CREATE_PIPELINE_STATE_0010));
    if (!arg) return false;
    const D3D12DDI_VIEW_INSTANCING_DESC &view = arg->ViewInstancingDesc;
    if (view.ViewInstanceCount > 1 || view.Flags != D3D12DDI_VIEW_INSTANCING_FLAG_NONE ||
        (view.ViewInstanceCount == 1 && (!view.pViewInstanceLocations ||
                                         view.pViewInstanceLocations[0].ViewportArrayIndex ||
                                         view.pViewInstanceLocations[0].RenderTargetArrayIndex)))
        return false;
    memcpy(legacy, arg, sizeof(*legacy));
    return true;
}
static SIZE_T APIENTRY CalcPrivatePipelineStateSize0033(D3D12DDI_HDEVICE device,
    const D3D12DDIARG_CREATE_PIPELINE_STATE_0033 *arg) {
    D3D12DDIARG_CREATE_PIPELINE_STATE_0010 legacy = {};
    return ProjectR3PipelineState(arg, &legacy) ? CalcPrivatePipelineStateSize0010(device, &legacy) : 0;
}
static HRESULT APIENTRY CreatePipelineState0033(D3D12DDI_HDEVICE device,
    const D3D12DDIARG_CREATE_PIPELINE_STATE_0033 *arg, D3D12DDI_HPIPELINESTATE destination,
    D3D12DDI_HRTPIPELINESTATE) {
    D3D12DDIARG_CREATE_PIPELINE_STATE_0010 legacy = {};
    if (!ProjectR3PipelineState(arg, &legacy)) return E_INVALIDARG;
    return CreatePipelineState0010(device, &legacy, destination);
}
static_assert(std::is_same_v<decltype(&CalcPrivatePipelineStateSize0033),
                             PFND3D12DDI_CALC_PRIVATE_PIPELINE_STATE_SIZE_0033>);
static_assert(std::is_same_v<decltype(&CreatePipelineState0033), PFND3D12DDI_CREATE_PIPELINE_STATE_0033>);
/* Root signature 1.1 (_0013): its range and root-descriptor flags only relax
 * 1.0's rules (1.0: volatile descriptors, CBV/SRV data static while set at
 * execute, UAV data volatile); this driver reads descriptors and data when
 * ExecuteCommandLists replays the list, so the 1.0 projection keeps every
 * 1.1 promise. The projection lives in one heap block freed after the call. */
struct AgxD3d12RootProjection {
    D3D12DDI_ROOT_SIGNATURE desc;
    D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 arg;
    void *storage;
};
static bool ProjectRoot11(const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0013 *in, AgxD3d12RootProjection *out) {
    memset(out, 0, sizeof(*out));
    if (!in || in->Version != D3D12DDI_ROOT_SIGNATURE_VERSION_1_1 || !in->pRootSignature_1_1) return false;
    const D3D12DDI_ROOT_SIGNATURE_0013 &root = *in->pRootSignature_1_1;
    if (root.NumParameters > kCurrentRootLayoutPolicy.max_parameters || (root.NumParameters && !root.pRootParameters))
        return false;
    UINT64 ranges = 0;
    for (UINT i = 0; i < root.NumParameters; ++i) {
        const D3D12DDI_ROOT_PARAMETER_0013 &p = root.pRootParameters[i];
        if (p.ParameterType != D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) continue;
        if (p.DescriptorTable.NumDescriptorRanges && !p.DescriptorTable.pDescriptorRanges) return false;
        ranges += p.DescriptorTable.NumDescriptorRanges;
    }
    const UINT64 parameter_bytes = UINT64{root.NumParameters} * sizeof(D3D12DDI_ROOT_PARAMETER);
    if (ranges > (UINT64{UINT_MAX} - parameter_bytes) / sizeof(D3D12DDI_DESCRIPTOR_RANGE)) return false;
    const SIZE_T bytes = static_cast<SIZE_T>(parameter_bytes + ranges * sizeof(D3D12DDI_DESCRIPTOR_RANGE));
    auto *storage = bytes ? static_cast<unsigned char *>(HeapAlloc(GetProcessHeap(), 0, bytes)) : nullptr;
    if (bytes && !storage) return false;
    auto *parameters = reinterpret_cast<D3D12DDI_ROOT_PARAMETER *>(storage);
    auto *range = reinterpret_cast<D3D12DDI_DESCRIPTOR_RANGE *>(storage + parameter_bytes);
    static_assert(sizeof(D3D12DDI_ROOT_PARAMETER) % alignof(D3D12DDI_DESCRIPTOR_RANGE) == 0);
    for (UINT i = 0; i < root.NumParameters; ++i) {
        const D3D12DDI_ROOT_PARAMETER_0013 &p = root.pRootParameters[i];
        D3D12DDI_ROOT_PARAMETER &q = parameters[i];
        memset(&q, 0, sizeof(q));
        q.ParameterType = p.ParameterType;
        q.ShaderVisibility = p.ShaderVisibility;
        switch (p.ParameterType) {
        case D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
            q.DescriptorTable.NumDescriptorRanges = p.DescriptorTable.NumDescriptorRanges;
            q.DescriptorTable.pDescriptorRanges = range;
            for (UINT r = 0; r < p.DescriptorTable.NumDescriptorRanges; ++r, ++range) {
                const D3D12DDI_DESCRIPTOR_RANGE_0013 &d = p.DescriptorTable.pDescriptorRanges[r];
                range->RangeType = d.RangeType;
                range->NumDescriptors = d.NumDescriptors;
                range->BaseShaderRegister = d.BaseShaderRegister;
                range->RegisterSpace = d.RegisterSpace;
                range->OffsetInDescriptorsFromTableStart = d.OffsetInDescriptorsFromTableStart;
            }
            break;
        case D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            q.Constants = p.Constants;
            break;
        default:
            q.Descriptor.ShaderRegister = p.Descriptor.ShaderRegister;
            q.Descriptor.RegisterSpace = p.Descriptor.RegisterSpace;
            break;
        }
    }
    out->desc.NumParameters = root.NumParameters;
    out->desc.pRootParameters = parameters;
    out->desc.NumStaticSamplers = root.NumStaticSamplers;
    out->desc.pStaticSamplers = root.pStaticSamplers;
    out->desc.Flags = root.Flags;
    out->arg.pRootSignature = &out->desc;
    out->arg.NodeMask = in->NodeMask;
    out->storage = storage;
    return true;
}
static void ReleaseRoot11(AgxD3d12RootProjection *projection) {
    if (projection->storage) HeapFree(GetProcessHeap(), 0, projection->storage);
    projection->storage = nullptr;
}
static SIZE_T APIENTRY CalcPrivateRootSignatureSize0013(D3D12DDI_HDEVICE device,
                                                        const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0013 *arg) {
    AgxD3d12RootProjection projection = {};
    if (!ProjectRoot11(arg, &projection)) return 0;
    const SIZE_T bytes = CalcPrivateRootSignatureSize(device, &projection.arg);
    ReleaseRoot11(&projection);
    return bytes;
}
static HRESULT APIENTRY CreateRootSignature0013(D3D12DDI_HDEVICE device,
                                                const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0013 *arg,
                                                D3D12DDI_HROOTSIGNATURE handle) {
    AgxD3d12RootProjection projection = {};
    if (!ProjectRoot11(arg, &projection)) return E_INVALIDARG;
    const HRESULT hr = CreateRootSignature(device, &projection.arg, handle);
    ReleaseRoot11(&projection);
    return hr;
}
static_assert(std::is_same_v<decltype(&CalcPrivateRootSignatureSize0013),
                             PFND3D12DDI_CALC_PRIVATE_ROOT_SIGNATURE_SIZE_0013>);
static_assert(std::is_same_v<decltype(&CreateRootSignature0013), PFND3D12DDI_CREATE_ROOT_SIGNATURE_0013>);
/* _0023 moves the queue handles to parameters and adds QueueCreationFlags
 * (global realtime priority is not offered: caps type 1062 unanswered) */
static SIZE_T APIENTRY CalcPrivateCommandQueueSize0023(D3D12DDI_HDEVICE device,
                                                       const D3D12DDIARG_CREATECOMMANDQUEUE_0023 *) {
    return CalcPrivateCommandQueueSize(device, nullptr);
}
static HRESULT APIENTRY CreateCommandQueue0023(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATECOMMANDQUEUE_0023 *arg,
                                               D3D12DDI_HCOMMANDQUEUE queue, D3D12DDI_HRTCOMMANDQUEUE runtime) {
    if (!arg || arg->QueueCreationFlags != D3D12DDI_COMMAND_QUEUE_CREATION_FLAG_NONE) return E_INVALIDARG;
    D3D12DDIARG_CREATECOMMANDQUEUE_0001 legacy = {};
    legacy.hDrvCommandQueue = queue;
    legacy.hRTCommandQueue = runtime;
    legacy.QueueFlags = arg->QueueFlags;
    legacy.NodeMask = arg->NodeMask;
    return CreateCommandQueue(device, &legacy);
}
static_assert(std::is_same_v<decltype(&CalcPrivateCommandQueueSize0023), PFND3D12DDI_CALCPRIVATECOMMANDQUEUESIZE_0023>);
static_assert(std::is_same_v<decltype(&CreateCommandQueue0023), PFND3D12DDI_CREATECOMMANDQUEUE_0023>);
/* 0022+ runtimes name UAV resources (D3D12DDI_RESOURCE_FLAG_0022_UNORDERED_ACCESS);
 * the 0003 runtime passed them without the bit, which is the contract these
 * entries validated, so the projection clears it. No protected session is
 * offered (caps type 1057 unanswered). */
static D3D12DDIARG_CREATERESOURCE_0003 ProjectR3Resource(const D3D12DDIARG_CREATERESOURCE_0003 &arg) {
    D3D12DDIARG_CREATERESOURCE_0003 legacy = arg;
    legacy.Flags &= ~D3D12DDI_RESOURCE_FLAG_0022_UNORDERED_ACCESS;
    return legacy;
}
static D3D12DDI_HEAP_AND_RESOURCE_SIZES APIENTRY CalcPrivateHeapAndResourceSizes0030(
    D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATEHEAP_0001 *heap, const D3D12DDIARG_CREATERESOURCE_0003 *resource,
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030) {
    if (!resource) return CalcPrivateHeapAndResourceSizes(device, heap, nullptr);
    const D3D12DDIARG_CREATERESOURCE_0003 legacy = ProjectR3Resource(*resource);
    return CalcPrivateHeapAndResourceSizes(device, heap, &legacy);
}
static HRESULT APIENTRY CreateHeapAndResource0030(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATEHEAP_0001 *heap,
                                                  D3D12DDI_HHEAP heap_handle, D3D12DDI_HRTRESOURCE runtime_resource,
                                                  const D3D12DDIARG_CREATERESOURCE_0003 *resource,
                                                  const D3D12DDI_CLEAR_VALUES *clear,
                                                  D3D12DDI_HPROTECTEDRESOURCESESSION_0030 session,
                                                  D3D12DDI_HRESOURCE resource_handle) {
    if (session.pDrvPrivate) return E_INVALIDARG;
    if (!resource) return CreateHeapAndResource(device, heap, heap_handle, runtime_resource, nullptr, clear, resource_handle);
    const D3D12DDIARG_CREATERESOURCE_0003 legacy = ProjectR3Resource(*resource);
    return CreateHeapAndResource(device, heap, heap_handle, runtime_resource, &legacy, clear, resource_handle);
}
static_assert(std::is_same_v<decltype(&CalcPrivateHeapAndResourceSizes0030),
                             PFND3D12DDI_CALCPRIVATEHEAPANDRESOURCESIZES_0030>);
static_assert(std::is_same_v<decltype(&CreateHeapAndResource0030), PFND3D12DDI_CREATEHEAPANDRESOURCE_0030>);
/* _0022 allocation info: 32-bit alignments and one swizzle transition per
 * pattern slot. Every slot repeats the single 0003 transition (the C21
 * registry has two patterns per layout; 0xff = no transition). */
static void ProjectR3AllocationInfo(const D3D12DDI_RESOURCE_ALLOCATION_INFO &in, D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 *out) {
    memset(out, 0, sizeof(*out));
    out->ResourceDataSize = in.ResourceDataSize;
    out->AdditionalDataHeaderSize = in.AdditionalDataHeaderSize;
    out->AdditionalDataSize = in.AdditionalDataSize;
    out->ResourceDataAlignment = static_cast<UINT32>(in.ResourceDataAlignment);
    out->AdditionalDataHeaderAlignment = static_cast<UINT32>(in.AdditionalDataHeaderAlignment);
    out->AdditionalDataAlignment = static_cast<UINT32>(in.AdditionalDataAlignment);
    out->Layout = in.Layout;
    for (UINT8 &transition : out->MipLevelSwizzleTransition) transition = in.MipLevelSwizzleTransition;
}
static VOID APIENTRY CheckResourceAllocationInfo0022(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATERESOURCE_0003 *arg,
                                                     D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS flags, UINT32 alignment,
                                                     UINT mask, D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 *info) {
    if (!info) return;
    D3D12DDI_RESOURCE_ALLOCATION_INFO legacy_info = {};
    if (arg) {
        const D3D12DDIARG_CREATERESOURCE_0003 legacy = ProjectR3Resource(*arg);
        CheckResourceAllocationInfo(device, &legacy, flags, alignment, mask, &legacy_info);
    } else {
        CheckResourceAllocationInfo(device, nullptr, flags, alignment, mask, &legacy_info);
    }
    ProjectR3AllocationInfo(legacy_info, info);
}
static VOID APIENTRY CheckExistingResourceAllocationInfo0022(D3D12DDI_HDEVICE device, D3D12DDI_HRESOURCE handle,
                                                             D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 *info) {
    if (!info) return;
    D3D12DDI_RESOURCE_ALLOCATION_INFO legacy_info = {};
    CheckExistingResourceAllocationInfo(device, handle, &legacy_info, nullptr);
    ProjectR3AllocationInfo(legacy_info, info);
}
static_assert(std::is_same_v<decltype(&CheckResourceAllocationInfo0022), PFND3D12DDI_CHECKRESOURCEALLOCATIONINFO_0022>);
static_assert(std::is_same_v<decltype(&CheckExistingResourceAllocationInfo0022),
                             PFND3D12DDI_CHECKEXISITINGRESOURCEALLOCATIONINFO_0022>);
/* C50: the 0033 command-list entries. ResourceBarrier_0022 replaces the
 * aliasing member with ranged barriers; like every barrier here it needs no
 * AGX work. Present_0028 adds a sync-interval override, never requested. */
static VOID APIENTRY ResourceBarrier0022(D3D12DDI_HCOMMANDLIST, UINT, const D3D12DDIARG_RESOURCE_BARRIER_0022 *) {}
static VOID APIENTRY Present0028(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HCOMMANDQUEUE queue,
                                 const D3D12DDIARG_PRESENT_0001 *arg, D3D12DDI_PRESENT_0028 *out) {
    D3D12DDI_PRESENT_0003 legacy = {};
    Present(handle, queue, arg, out ? &legacy : nullptr);
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->hSrcAllocation = legacy.hSrcAllocation;
    out->hDstAllocation = legacy.hDstAllocation;
    out->hContext = legacy.hContext;
    out->BroadcastContextCount = legacy.BroadcastContextCount;
    memcpy(out->BroadcastContext, legacy.BroadcastContext, sizeof(out->BroadcastContext));
    memcpy(out->BroadcastSrcAllocation, legacy.BroadcastSrcAllocation, sizeof(out->BroadcastSrcAllocation));
    memcpy(out->BroadcastDstAllocation, legacy.BroadcastDstAllocation, sizeof(out->BroadcastDstAllocation));
    out->AddedGpuWork = legacy.AddedGpuWork;
    out->BackBufferMultiplicity = legacy.BackBufferMultiplicity;
}
/* The added list state DDIs accept only the value their absent capability
 * implies (DepthBoundsTestSupported, ProgrammableSamplePositionsTier and
 * protected sessions are 0): the defaults a list reset may set. */
static VOID APIENTRY OMSetDepthBounds(D3D12DDI_HCOMMANDLIST handle, FLOAT min, FLOAT max) {
    if (min != 0.0f || max != 1.0f) ReportRejectedListError(handle, E_INVALIDARG);
}
static VOID APIENTRY SetSamplePositions(D3D12DDI_HCOMMANDLIST handle, UINT samples, UINT pixels,
                                        D3D12DDI_SAMPLE_POSITION *) {
    if (samples || pixels) ReportRejectedListError(handle, E_INVALIDARG);
}
static VOID APIENTRY SetProtectedResourceSession(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HPROTECTEDRESOURCESESSION_0030 session) {
    if (session.pDrvPrivate) ReportRejectedListError(handle, E_INVALIDARG);
}
/* the mask only applies to PSOs that enable view-instance masking, which
 * CreatePipelineState0033 refuses */
static VOID APIENTRY SetViewInstanceMask(D3D12DDI_HCOMMANDLIST, UINT) {}
static_assert(std::is_same_v<decltype(&ResourceBarrier0022), PFND3D12DDI_RESOURCEBARRIER_0022>);
static_assert(std::is_same_v<decltype(&Present0028), PFND3D12DDI_PRESENT_0028>);
static_assert(std::is_same_v<decltype(&OMSetDepthBounds), PFND3D12DDI_OM_SETDEPTHBOUNDS_0025>);
static_assert(std::is_same_v<decltype(&SetSamplePositions), PFND3D12DDI_SETSAMPLEPOSITIONS_0027>);
static_assert(std::is_same_v<decltype(&SetProtectedResourceSession), PFND3D12DDI_SETPROTECTEDRESOURCESESSION_0030>);
static_assert(std::is_same_v<decltype(&SetViewInstanceMask), PFND3D12DDI_SETVIEWINSTANCEMASK_0033>);
static void FillD3D12DDI_DEVICE_FUNCS_CORE_0010(D3D12DDI_DEVICE_FUNCS_CORE_0010 &f) {
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCheckFormatSupport, CheckFormatSupport);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCheckMultisampleQualityLevels, CheckMultisampleQualityLevels);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnGetMipPacking, AGX_TILED_DEVICE(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnGetMipPacking, GetMipPacking));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateElementLayoutSize, CalcPrivateElementLayoutSize0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateElementLayout, CreateElementLayout0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyElementLayout, DestroyElementLayout);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateBlendStateSize, CalcPrivateBlendStateSize0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateBlendState, CreateBlendState0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyBlendState, BlendState::destroy<D3D12DDI_HBLENDSTATE>);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateDepthStencilStateSize, CalcPrivateDepthStencilStateSize0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateDepthStencilState, CreateDepthStencilState0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyDepthStencilState, DepthStencilState::destroy<D3D12DDI_HDEPTHSTENCILSTATE>);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateRasterizerStateSize, CalcPrivateRasterizerStateSize0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateRasterizerState, CreateRasterizerState0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyRasterizerState, RasterizerState::destroy<D3D12DDI_HRASTERIZERSTATE>);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateShaderSize, CalcPrivateShaderSize0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateVertexShader, CreateShader0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreatePixelShader, CreateShader0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateGeometryShader, CreateShader0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateComputeShader, CreateComputeShader0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateGeometryShaderWithStreamOutput, CalcPrivateGeometryShaderWithStreamOutput0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateGeometryShaderWithStreamOutput, CreateGeometryShaderWithStreamOutput0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateTessellationShaderSize, CalcPrivateShaderSize0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateHullShader, CreateTessellationShader0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateDomainShader, CreateTessellationShader0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyShader, DestroyShader);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateCommandQueueSize, CalcPrivateCommandQueueSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateCommandQueue, CreateCommandQueue);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyCommandQueue, DestroyCommandQueue);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateCommandAllocatorSize, CalcPrivateCommandAllocatorSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateCommandAllocator, CreateCommandAllocator);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyCommandAllocator, DestroyCommandAllocator);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnResetCommandAllocator, ResetCommandAllocator);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivatePipelineStateSize, CalcPrivatePipelineStateSize0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreatePipelineState, CreatePipelineState0010);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyPipelineState, DestroyPipelineState);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateCommandListSize, CalcPrivateCommandListSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateCommandList, CreateCommandList);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyCommandList, DestroyCommandList);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateFenceSize, CalcPrivateFenceSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateFence, CreateFence);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyFence, DestroyFence);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateDescriptorHeapSize, CalcPrivateDescriptorHeapSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateDescriptorHeap, CreateDescriptorHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyDescriptorHeap, DestroyDescriptorHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnGetDescriptorSizeInBytes, GetDescriptorSizeInBytes);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnGetCPUDescriptorHandleForHeapStart, GetCPUDescriptorHandleForHeapStart);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnGetGPUDescriptorHandleForHeapStart, GetGPUDescriptorHandleForHeapStart);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateShaderResourceView, CreateShaderResourceViewInHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateConstantBufferView, CreateConstantBufferViewInHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateSampler, CreateSamplerInHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateUnorderedAccessView, CreateUnorderedAccessViewInHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateRenderTargetView, CreateRenderTargetView);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateDepthStencilView, CreateDepthStencilView);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateRootSignatureSize, CalcPrivateRootSignatureSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateRootSignature, CreateRootSignature);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyRootSignature, DestroyRootSignature);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnMapHeap, MapHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnUnmapHeap, UnmapHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateHeapAndResourceSizes, CalcPrivateHeapAndResourceSizes);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateHeapAndResource, CreateHeapAndResource);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyHeapAndResource, DestroyHeapAndResource);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnMakeResident, MakeResident);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnEvict, Evict);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateOpenedHeapAndResourceSizes, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateOpenedHeapAndResourceSizes));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnOpenHeapAndResource, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnOpenHeapAndResource));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCopyDescriptors, CopyDescriptors);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCopyDescriptorsSimple, CopyDescriptorsSimple);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateQueryHeapSize, CalcPrivateQueryHeapSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateQueryHeap, CreateQueryHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyQueryHeap, DestroyQueryHeap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivateCommandSignatureSize, CalcPrivateCommandSignatureSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreateCommandSignature, CreateCommandSignature);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyCommandSignature, DestroyCommandSignature);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCheckResourceVirtualAddress, CheckResourceVirtualAddress);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCheckResourceAllocationInfo, CheckResourceAllocationInfo);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCheckSubresourceInfo, CheckSubresourceInfo);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCheckExistingResourceAllocationInfo, CheckExistingResourceAllocationInfo);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnOfferResources, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnOfferResources));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnReclaimResources, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnReclaimResources));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnGetImplicitPhysicalAdapterMask, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnGetImplicitPhysicalAdapterMask));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnGetPresentPrivateDriverDataSize, GetPresentPrivateDriverDataSize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnQueryNodeMap, QueryNodeMap);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnRetrieveShaderComment, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnRetrieveShaderComment));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCheckResourceAllocationHandle, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCheckResourceAllocationHandle));
    /* R1 pipeline libraries: the zero-cache library (agx_d3d12_zero_cache_library.cpp) */
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcPrivatePipelineLibrarySize, ReceiptCalcPrivatePipelineLibrarySize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCreatePipelineLibrary, ReceiptCreatePipelineLibrary);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnDestroyPipelineLibrary, ReceiptDestroyPipelineLibrary);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnAddPipelineStateToLibrary, ReceiptAddPipelineStateToLibrary);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnCalcSerializedLibrarySize, ReceiptCalcSerializedLibrarySize);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnSerializeLibrary, ReceiptSerializeLibrary);
}
/* C30: the bundle-legal DDIs (ID3D12GraphicsCommandList bundle
 * restrictions) record into a bundle and run directly on other lists. */
template<auto Fn> struct Bundled;
template<class... A, VOID (APIENTRY *Fn)(D3D12DDI_HCOMMANDLIST, A...)> struct Bundled<Fn> {
    static_assert(((std::is_trivially_copyable_v<A> && !std::is_pointer_v<A>) && ...),
                  "a bundled call copies its arguments by value");
    using Args = std::tuple<A...>;
    static void Replay(D3D12DDI_HCOMMANDLIST parent, const unsigned char *payload) {
        std::apply([parent](A... a) { Fn(parent, a...); }, *reinterpret_cast<const Args *>(payload));
    }
    static VOID APIENTRY call(D3D12DDI_HCOMMANDLIST handle, A... a) {
        auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
        if (!IsBundle(list)) {
            Fn(handle, a...);
            return;
        }
        if (unsigned char *payload = BundleAppend(list, Replay, sizeof(Args))) new (payload) Args(a...);
    }
};
/* the pointer-argument DDIs copy what the pointer names */
template<class T> static const T *BundleArray(const unsigned char *payload, UINT *count) {
    memcpy(count, payload, sizeof(*count));
    return reinterpret_cast<const T *>(payload + 16u);
}
template<class T> static unsigned char *BundleAppendArray(AgxD3d12CommandList *list, AgxD3d12BundleReplay replay,
                                                          UINT head, UINT count, const T *items, SIZE_T extra = 0) {
    unsigned char *payload = BundleAppend(list, replay, 16u + static_cast<SIZE_T>(count) * sizeof(T) + extra);
    if (!payload) return nullptr;
    memset(payload, 0, 16u);
    memcpy(payload, &count, sizeof(count));
    memcpy(payload + 4u, &head, sizeof(head));
    if (count && items) memcpy(payload + 16u, items, static_cast<SIZE_T>(count) * sizeof(T));
    return payload;
}
static VOID APIENTRY BundledVertexBuffers(D3D12DDI_HCOMMANDLIST handle, UINT start, UINT count,
                                          const D3D12DDI_VERTEX_BUFFER_VIEW *views) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!IsBundle(list)) {
        AGX_VB_SETTER(handle, start, count, views);
        return;
    }
    if (count > kVertexBuffers) {
        ListError(list, E_INVALIDARG);
        return;
    }
    unsigned char *payload = BundleAppendArray(
        list,
        [](D3D12DDI_HCOMMANDLIST parent, const unsigned char *p) {
            UINT n = 0, first = 0, present = 0;
            const auto *v = BundleArray<D3D12DDI_VERTEX_BUFFER_VIEW>(p, &n);
            memcpy(&first, p + 4u, sizeof(first));
            memcpy(&present, p + 8u, sizeof(present));
            AGX_VB_SETTER(parent, first, n, present ? v : nullptr);
        },
        start, count, views);
    if (payload) {
        const UINT present = views ? 1u : 0u;
        memcpy(payload + 8u, &present, sizeof(present));
    }
}
static VOID APIENTRY BundledIndexBuffer(D3D12DDI_HCOMMANDLIST handle, const D3D12DDI_INDEX_BUFFER_VIEW *view) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!IsBundle(list)) {
        AGX_IB_SETTER(handle, view);
        return;
    }
    BundleAppendArray(
        list,
        [](D3D12DDI_HCOMMANDLIST parent, const unsigned char *p) {
            UINT n = 0;
            const auto *v = BundleArray<D3D12DDI_INDEX_BUFFER_VIEW>(p, &n);
            AGX_IB_SETTER(parent, n ? v : nullptr);
        },
        0u, view ? 1u : 0u, view);
}
static VOID APIENTRY BundledBlendFactor(D3D12DDI_HCOMMANDLIST handle, const FLOAT factor[4]) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!IsBundle(list)) {
        OmSetBlendFactor(handle, factor);
        return;
    }
    BundleAppendArray(
        list,
        [](D3D12DDI_HCOMMANDLIST parent, const unsigned char *p) {
            UINT n = 0;
            const FLOAT *v = BundleArray<FLOAT>(p, &n);
            OmSetBlendFactor(parent, n ? v : nullptr);
        },
        0u, factor ? 4u : 0u, factor);
}
static VOID APIENTRY BundledDescriptorHeaps(D3D12DDI_HCOMMANDLIST handle, UINT count, D3D12DDI_HDESCRIPTORHEAP *heaps) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!IsBundle(list)) {
        AGX_LIST_STATE(pfnSetDescriptorHeaps)(handle, count, heaps);
        return;
    }
    BundleAppendArray(
        list,
        [](D3D12DDI_HCOMMANDLIST parent, const unsigned char *p) {
            UINT n = 0;
            const auto *v = BundleArray<D3D12DDI_HDESCRIPTORHEAP>(p, &n);
            AGX_LIST_STATE(pfnSetDescriptorHeaps)(parent, n, const_cast<D3D12DDI_HDESCRIPTORHEAP *>(v));
        },
        0u, heaps ? count : 0u, heaps);
}
template<bool Graphics> static VOID APIENTRY BundledRootConstants(D3D12DDI_HCOMMANDLIST handle, UINT index, UINT count,
                                                                  const void *data, UINT offset) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    if (!IsBundle(list)) {
        (Graphics ? SetGraphicsRoot32BitConstants : SetComputeRoot32BitConstants)(handle, index, count, data, offset);
        return;
    }
    if (count > 64u || (count && !data)) {
        ListError(list, E_INVALIDARG);
        return;
    }
    unsigned char *payload = BundleAppendArray(
        list,
        [](D3D12DDI_HCOMMANDLIST parent, const unsigned char *p) {
            UINT n = 0, root = 0, at = 0;
            const UINT *v = BundleArray<UINT>(p, &n);
            memcpy(&root, p + 4u, sizeof(root));
            memcpy(&at, p + 8u, sizeof(at));
            (Graphics ? SetGraphicsRoot32BitConstants : SetComputeRoot32BitConstants)(parent, root, n, v, at);
        },
        index, count, static_cast<const UINT *>(data));
    if (payload) memcpy(payload + 8u, &offset, sizeof(offset));
}
static VOID APIENTRY ExecuteBundle(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HCOMMANDLIST bundle_handle) {
    auto *list = static_cast<AgxD3d12CommandList *>(handle.pDrvPrivate);
    auto *bundle = static_cast<AgxD3d12CommandList *>(bundle_handle.pDrvPrivate);
    if (!list) return;
    const UINT refusal = !bundle || !IsBundle(bundle) ? 1u : IsBundle(list) ? 2u : !bundle->closed ? 3u : 0u;
    UINT calls = 0;
    for (SIZE_T at = 0; !refusal && at < bundle->bundle_bytes;) {
        AgxD3d12BundleCall call;
        memcpy(&call, bundle->bundle + at, sizeof(call));
        call.replay(handle, bundle->bundle + at + kBundleHeader);
        at += call.bytes;
        ++calls;
    }
    const UINT receipt[] = {refusal, calls, list->commands};
    AdmissionUmdDiagnostic("measure-d3d12-execute-bundle", refusal ? E_INVALIDARG : S_OK, receipt, 3);
    if (refusal) ListError(list, E_INVALIDARG);
}
static void FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0003(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 &f) {
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCloseCommandList, CloseCommandList);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResetCommandList, ResetCommandList);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDrawInstanced, Bundled<DrawInstanced>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDrawIndexedInstanced, Bundled<DrawIndexedInstanced>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDispatch, Bundled<Dispatch>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearUnorderedAccessViewUint, ClearUnorderedAccessViewUint);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearUnorderedAccessViewFloat, ClearUnorderedAccessViewFloat);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearRenderTargetView, ClearRenderTargetView);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearDepthStencilView, ClearDepthStencilView);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDiscardResource, DiscardResource);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyTextureRegion, CopyTextureRegion);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceCopy, ResourceCopy);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyTiles, AGX_TILED_DEVICE(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyTiles, CopyTiles));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyBufferRegion, CopyBufferRegion);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceResolveSubresource, ResolveSubresource);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnExecuteBundle, ExecuteBundle);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnExecuteIndirect, Bundled<ExecuteIndirect>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceBarrier, ResourceBarrier);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnBlt, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnBlt));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnPresent, Present);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnBeginQuery, BeginQuery);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnEndQuery, EndQuery);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResolveQueryData, ResolveQueryData);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetPredication, AGX_LIST_STATE(pfnSetPredication));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIaSetTopology, Bundled<IaSetTopology>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnRsSetViewports, RsSetViewports);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnRsSetScissorRects, RsSetScissorRects);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOmSetBlendFactor, BundledBlendFactor);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOmSetStencilRef, Bundled<OmSetStencilRef>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetPipelineState, Bundled<SetPipelineState>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetDescriptorHeaps, BundledDescriptorHeaps);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootSignature, Bundled<SetComputeRootSignature>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootSignature, Bundled<SetGraphicsRootSignature>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootDescriptorTable, Bundled<SetComputeRootDescriptorTable>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootDescriptorTable, Bundled<SetGraphicsRootDescriptorTable>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRoot32BitConstant, Bundled<SetComputeRoot32BitConstant>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRoot32BitConstant, Bundled<SetGraphicsRoot32BitConstant>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRoot32BitConstants, BundledRootConstants<false>);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRoot32BitConstants, BundledRootConstants<true>);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootConstantBufferView, Bundled<SetComputeRootConstantBufferView>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootConstantBufferView, Bundled<SetGraphicsRootConstantBufferView>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootShaderResourceView, Bundled<SetComputeRootShaderResourceView>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootShaderResourceView, Bundled<SetGraphicsRootShaderResourceView>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootUnorderedAccessView, Bundled<SetComputeRootUnorderedAccessView>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootUnorderedAccessView, Bundled<SetGraphicsRootUnorderedAccessView>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIASetIndexBuffer, BundledIndexBuffer);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIASetVertexBuffers, BundledVertexBuffers);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSOSetTargets, AGX_SO_SETTER); /* C39 */
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOMSetRenderTargets, OMSetRenderTargets);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetMarker, Bundled<SetMarker>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearRootArguments, ClearRootArguments);
}
/* C51 (0042, CORE_0040/3D_0040): command allocators become pools and
 * recorders, and lists are created without an allocator. A list records into
 * its own heap buffers (C4), so pools and recorders hold nothing. */
static SIZE_T APIENTRY CalcPrivateCommandPoolSize0040(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_COMMAND_POOL_0040 *) {
    return sizeof(UINT);
}
static HRESULT APIENTRY CreateCommandPool0040(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_COMMAND_POOL_0040 *arg,
                                              D3D12DDI_HCOMMANDPOOL_0040 pool) {
    if (!arg || !pool.pDrvPrivate || arg->PoolFlags != D3D12DDI_COMMAND_POOL_FLAG_NONE) return E_INVALIDARG;
    *static_cast<UINT *>(pool.pDrvPrivate) = 0;
    return S_OK;
}
static VOID APIENTRY DestroyCommandPool0040(D3D12DDI_HDEVICE, D3D12DDI_HCOMMANDPOOL_0040) {}
static VOID APIENTRY ResetCommandPool0040(D3D12DDI_HDEVICE, D3D12DDI_HCOMMANDPOOL_0040) {}
static SIZE_T APIENTRY CalcPrivateCommandRecorderSize0040(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_COMMAND_RECORDER_0040 *) {
    return sizeof(UINT);
}
static HRESULT APIENTRY CreateCommandRecorder0040(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_COMMAND_RECORDER_0040 *arg,
                                                  D3D12DDI_HCOMMANDRECORDER_0040 recorder) {
    if (!arg || !recorder.pDrvPrivate || arg->RecorderFlags != D3D12DDI_COMMAND_RECORDER_FLAG_NONE) return E_INVALIDARG;
    *static_cast<UINT *>(recorder.pDrvPrivate) = 0;
    return S_OK;
}
static VOID APIENTRY DestroyCommandRecorder0040(D3D12DDI_HDEVICE, D3D12DDI_HCOMMANDRECORDER_0040) {}
static VOID APIENTRY CommandRecorderSetCommandPoolAsTarget0040(D3D12DDI_HDEVICE, D3D12DDI_HCOMMANDRECORDER_0040,
                                                              D3D12DDI_HCOMMANDPOOL_0040) {}
static SIZE_T APIENTRY CalcPrivateCommandListSize0040(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_COMMAND_LIST_0040 *) {
    return CalcPrivateCommandListSize(device, nullptr);
}
static HRESULT APIENTRY CreateCommandList0040(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATE_COMMAND_LIST_0040 *arg,
                                              D3D12DDI_HCOMMANDLIST list, D3D12DDI_HRTCOMMANDLIST runtime) {
    if (!arg) return E_INVALIDARG;
    D3D12DDIARG_CREATE_COMMAND_LIST_0001 legacy = {};
    legacy.hDrvCommandList = list;
    legacy.hRTCommandList = runtime;
    legacy.Type = arg->Type;
    legacy.QueueFlags = arg->QueueFlags;
    legacy.ID = arg->ID;
    legacy.CommandListFlags = arg->CommandListFlags;
    legacy.NodeMask = arg->NodeMask;
    return CreateCommandList(device, &legacy);
}
/* the 0040 reset names a recorder instead of an allocator and slot; the
 * reset reads neither */
static VOID APIENTRY ResetCommandList0040(D3D12DDI_HCOMMANDLIST handle, const D3D12DDIARG_RESETCOMMANDLIST_0040 *) {
    ResetCommandList(handle, nullptr);
}
static_assert(std::is_same_v<decltype(&CalcPrivateCommandPoolSize0040), PFND3D12DDI_CALC_PRIVATE_COMMAND_POOL_SIZE_0040>);
static_assert(std::is_same_v<decltype(&CreateCommandPool0040), PFND3D12DDI_CREATE_COMMAND_POOL_0040>);
static_assert(std::is_same_v<decltype(&DestroyCommandPool0040), PFND3D12DDI_DESTROY_COMMAND_POOL_0040>);
static_assert(std::is_same_v<decltype(&ResetCommandPool0040), PFND3D12DDI_RESET_COMMAND_POOL_0040>);
static_assert(std::is_same_v<decltype(&CalcPrivateCommandRecorderSize0040),
                             PFND3D12DDI_CALC_PRIVATE_COMMAND_RECORDER_SIZE_0040>);
static_assert(std::is_same_v<decltype(&CreateCommandRecorder0040), PFND3D12DDI_CREATE_COMMAND_RECORDER_0040>);
static_assert(std::is_same_v<decltype(&DestroyCommandRecorder0040), PFND3D12DDI_DESTROY_COMMAND_RECORDER_0040>);
static_assert(std::is_same_v<decltype(&CommandRecorderSetCommandPoolAsTarget0040),
                             PFND3D12DDI_COMMAND_RECORDER_SET_COMMAND_POOL_AS_TARGET_0040>);
static_assert(std::is_same_v<decltype(&CalcPrivateCommandListSize0040), PFND3D12DDI_CALC_PRIVATE_COMMAND_LIST_SIZE_0040>);
static_assert(std::is_same_v<decltype(&CreateCommandList0040), PFND3D12DDI_CREATE_COMMAND_LIST_0040>);
static_assert(std::is_same_v<decltype(&ResetCommandList0040), PFND3D12DDI_RESETCOMMANDLIST_0040>);
/* C52 (0071, CORE_0070/3D_0062): queues gain a scheduling group, presents
 * report allocations and contexts separately, and the meta-command,
 * raytracing, background-processing and shading-rate entries appear. None
 * of those features is offered (scheduling caps 0, RaytracingTier and
 * VariableShadingRateTier 0, no meta commands), so they answer empty or
 * accept only their defaults. */
static SIZE_T APIENTRY CalcPrivateCommandQueueSize0050(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATECOMMANDQUEUE_0050 *) {
    return CalcPrivateCommandQueueSize0023(device, nullptr);
}
static HRESULT APIENTRY CreateCommandQueue0050(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATECOMMANDQUEUE_0050 *arg,
                                               D3D12DDI_HCOMMANDQUEUE queue, D3D12DDI_HRTCOMMANDQUEUE runtime) {
    if (!arg || arg->SchedulingGroup.pDrvPrivate) return E_INVALIDARG;
    D3D12DDIARG_CREATECOMMANDQUEUE_0023 legacy = {};
    legacy.QueueFlags = arg->QueueFlags;
    legacy.NodeMask = arg->NodeMask;
    legacy.QueueCreationFlags = arg->QueueCreationFlags;
    return CreateCommandQueue0023(device, &legacy, queue, runtime);
}
static HRESULT APIENTRY EnumerateMetaCommands0052(D3D12DDI_HDEVICE, UINT *count, D3D12DDIARG_META_COMMAND_DESC *) {
    if (!count) return E_INVALIDARG;
    *count = 0;
    return S_OK;
}
static VOID APIENTRY GetMetaCommandRequiredParameterInfo0052(D3D12DDI_HMETACOMMAND_0052, D3D12DDI_META_COMMAND_PARAMETER_STAGE,
                                                             UINT, D3D12DDIARG_META_COMMAND_REQUIRED_PARAMETER_INFO *info) {
    if (info) memset(info, 0, sizeof(*info));
}
static void APIENTRY GetRaytracingAccelerationStructurePrebuildInfo0054(
    D3D12DDI_HDEVICE, const D3D12DDI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_0054 *,
    D3D12DDI_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_0054 *info) {
    if (info) memset(info, 0, sizeof(*info));
}
static D3D12DDI_DRIVER_MATCHING_IDENTIFIER_STATUS APIENTRY CheckDriverMatchingIdentifier0054(
    D3D12DDI_HDEVICE, D3D12DDI_SERIALIZED_DATA_TYPE, const D3D12DDI_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER_0054 *) {
    return D3D12DDI_DRIVER_MATCHING_IDENTIFIER_UNSUPPORTED_TYPE;
}
/* state objects are never created (CreateStateObject is fail-closed) */
static void *APIENTRY GetShaderIdentifier0054(D3D12DDI_HSTATEOBJECT_0054, LPCWSTR) { return nullptr; }
static UINT APIENTRY GetShaderStackSize0054(D3D12DDI_HSTATEOBJECT_0054, LPCWSTR) { return 0; }
static UINT APIENTRY GetPipelineStackSize0054(D3D12DDI_HSTATEOBJECT_0054) { return 0; }
static void APIENTRY SetPipelineStackSize0054(D3D12DDI_HSTATEOBJECT_0054, UINT) {}
static void APIENTRY SetBackgroundProcessingMode0063(D3D12DDI_HDEVICE, D3D12DDI_BACKGROUND_PROCESSING_MODE_0062,
                                                     D3D12DDI_MEASUREMENTS_ACTION_0062, BOOL *further) {
    if (further) *further = FALSE;
}
/* _0051 keeps each allocation pair in slot 0 of the broadcast arrays and the
 * context in a separate output; this driver presents through its queue's
 * context, so a runtime asking only for hardware queues is refused */
static VOID APIENTRY Present0051(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HCOMMANDQUEUE queue,
                                 const D3D12DDIARG_PRESENT_0001 *arg, D3D12DDI_PRESENT_0051 *out,
                                 D3D12DDI_PRESENT_CONTEXTS_0051 *contexts, D3D12DDI_PRESENT_HWQUEUES_0051 *queues) {
    if (out) memset(out, 0, sizeof(*out));
    if (contexts) memset(contexts, 0, sizeof(*contexts));
    if (queues) memset(queues, 0, sizeof(*queues));
    D3D12DDI_PRESENT_0003 legacy = {};
    Present(handle, queue, arg, out && contexts ? &legacy : nullptr);
    if (!out || !contexts) return;
    out->BroadcastSrcAllocation[0] = legacy.hSrcAllocation;
    out->BroadcastDstAllocation[0] = legacy.hDstAllocation;
    for (UINT i = 0; i < legacy.BroadcastContextCount && i < D3DDDI_MAX_BROADCAST_CONTEXT; ++i) {
        out->BroadcastSrcAllocation[i + 1] = legacy.BroadcastSrcAllocation[i];
        out->BroadcastDstAllocation[i + 1] = legacy.BroadcastDstAllocation[i];
        contexts->BroadcastContext[i] = legacy.BroadcastContext[i];
    }
    out->AddedGpuWork = legacy.AddedGpuWork;
    out->BackBufferMultiplicity = legacy.BackBufferMultiplicity;
    contexts->hContext = legacy.hContext;
    contexts->BroadcastContextCount = legacy.BroadcastContextCount;
}
static VOID APIENTRY RSSetShadingRate0062(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_SHADING_RATE_0062 rate,
                                          const D3D12DDI_SHADING_RATE_COMBINER_0062 *combiners) {
    if (rate != D3D12DDI_SHADING_RATE_0062_1X1 ||
        (combiners && (combiners[0] != D3D12DDI_SHADING_RATE_COMBINER_0062_PASSTHROUGH ||
                       combiners[1] != D3D12DDI_SHADING_RATE_COMBINER_0062_PASSTHROUGH)))
        ReportRejectedListError(handle, E_INVALIDARG);
}
static VOID APIENTRY RSSetShadingRateImage0062(D3D12DDI_HCOMMANDLIST handle, D3D12DDI_HRESOURCE image) {
    if (image.pDrvPrivate) ReportRejectedListError(handle, E_INVALIDARG);
}
static_assert(std::is_same_v<decltype(&CalcPrivateCommandQueueSize0050), PFND3D12DDI_CALCPRIVATECOMMANDQUEUESIZE_0050>);
static_assert(std::is_same_v<decltype(&CreateCommandQueue0050), PFND3D12DDI_CREATECOMMANDQUEUE_0050>);
static_assert(std::is_same_v<decltype(&EnumerateMetaCommands0052), PFND3D12DDI_ENUMERATE_META_COMMANDS_0052>);
static_assert(std::is_same_v<decltype(&GetMetaCommandRequiredParameterInfo0052),
                             PFND3D12DDI_GET_META_COMMAND_REQUIRED_PARAMETER_INFO_0052>);
static_assert(std::is_same_v<decltype(&GetRaytracingAccelerationStructurePrebuildInfo0054),
                             PFND3D12DDI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_0054>);
static_assert(std::is_same_v<decltype(&CheckDriverMatchingIdentifier0054), PFND3D12DDI_CHECK_DRIVER_MATCHING_IDENTIFIER_0054>);
static_assert(std::is_same_v<decltype(&GetShaderIdentifier0054), PFND3D12DDI_GET_SHADER_IDENTIFIER_0054>);
static_assert(std::is_same_v<decltype(&GetShaderStackSize0054), PFND3D12DDI_GET_SHADER_STACK_SIZE_0054>);
static_assert(std::is_same_v<decltype(&GetPipelineStackSize0054), PFND3D12DDI_GET_PIPELINE_STACK_SIZE_0054>);
static_assert(std::is_same_v<decltype(&SetPipelineStackSize0054), PFND3D12DDI_SET_PIPELINE_STACK_SIZE_0054>);
static_assert(std::is_same_v<decltype(&SetBackgroundProcessingMode0063), PFND3D12DDI_SET_BACKGROUND_PROCESSING_MODE_0063>);
static_assert(std::is_same_v<decltype(&Present0051), PFND3D12DDI_PRESENT_0051>);
static_assert(std::is_same_v<decltype(&RSSetShadingRate0062), PFND3D12DDI_RS_SET_SHADING_RATE_0062>);
static_assert(std::is_same_v<decltype(&RSSetShadingRateImage0062), PFND3D12DDI_RS_SET_SHADING_RATE_IMAGE_0062>);
/* C54 (0076, CORE_0075/3D_0074): pipeline states gain mesh and
 * amplification shaders and resources a sampler-feedback mip region; both
 * features are unsupported (MeshShaderTier, SamplerFeedbackTier 0), so the
 * projections refuse them and otherwise reuse the 0033/0030/0022 entries. */
static bool ProjectR7PipelineState(const D3D12DDIARG_CREATE_PIPELINE_STATE_0075 *arg,
                                   D3D12DDIARG_CREATE_PIPELINE_STATE_0033 *legacy) {
    static_assert(offsetof(D3D12DDIARG_CREATE_PIPELINE_STATE_0075, ViewInstancingDesc) ==
                  offsetof(D3D12DDIARG_CREATE_PIPELINE_STATE_0033, ViewInstancingDesc));
    static_assert(offsetof(D3D12DDIARG_CREATE_PIPELINE_STATE_0075, hMeshShader) >= sizeof(D3D12DDIARG_CREATE_PIPELINE_STATE_0033));
    if (!arg || arg->hMeshShader.pDrvPrivate || arg->hAmplificationShader.pDrvPrivate) return false;
    memcpy(legacy, arg, sizeof(*legacy));
    return true;
}
static SIZE_T APIENTRY CalcPrivatePipelineStateSize0075(D3D12DDI_HDEVICE device,
    const D3D12DDIARG_CREATE_PIPELINE_STATE_0075 *arg) {
    D3D12DDIARG_CREATE_PIPELINE_STATE_0033 legacy = {};
    return ProjectR7PipelineState(arg, &legacy) ? CalcPrivatePipelineStateSize0033(device, &legacy) : 0;
}
static HRESULT APIENTRY CreatePipelineState0075(D3D12DDI_HDEVICE device,
    const D3D12DDIARG_CREATE_PIPELINE_STATE_0075 *arg, D3D12DDI_HPIPELINESTATE destination,
    D3D12DDI_HRTPIPELINESTATE runtime) {
    D3D12DDIARG_CREATE_PIPELINE_STATE_0033 legacy = {};
    if (!ProjectR7PipelineState(arg, &legacy)) return E_INVALIDARG;
    return CreatePipelineState0033(device, &legacy, destination, runtime);
}
static bool ProjectR7Resource(const D3D12DDIARG_CREATERESOURCE_0075 &arg, D3D12DDIARG_CREATERESOURCE_0003 *legacy) {
    static_assert(offsetof(D3D12DDIARG_CREATERESOURCE_0075, pRowMajorLayout) ==
                  offsetof(D3D12DDIARG_CREATERESOURCE_0003, pRowMajorLayout));
    /* x86: the mip region sits in _0003's tail padding, so only the members
     * up to pRowMajorLayout are copied */
    constexpr size_t members = offsetof(D3D12DDIARG_CREATERESOURCE_0003, pRowMajorLayout) + sizeof(arg.pRowMajorLayout);
    static_assert(offsetof(D3D12DDIARG_CREATERESOURCE_0075, SamplerFeedbackMipRegion) >= members);
    if (arg.SamplerFeedbackMipRegion.Width || arg.SamplerFeedbackMipRegion.Height || arg.SamplerFeedbackMipRegion.Depth)
        return false;
    *legacy = {};
    memcpy(legacy, &arg, members);
    return true;
}
static D3D12DDI_HEAP_AND_RESOURCE_SIZES APIENTRY CalcPrivateHeapAndResourceSizes0075(
    D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATEHEAP_0001 *heap, const D3D12DDIARG_CREATERESOURCE_0075 *resource,
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 session) {
    D3D12DDIARG_CREATERESOURCE_0003 legacy = {};
    if (resource && !ProjectR7Resource(*resource, &legacy)) return {};
    return CalcPrivateHeapAndResourceSizes0030(device, heap, resource ? &legacy : nullptr, session);
}
static HRESULT APIENTRY CreateHeapAndResource0075(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATEHEAP_0001 *heap,
                                                  D3D12DDI_HHEAP heap_handle, D3D12DDI_HRTRESOURCE runtime_resource,
                                                  const D3D12DDIARG_CREATERESOURCE_0075 *resource,
                                                  const D3D12DDI_CLEAR_VALUES *clear,
                                                  D3D12DDI_HPROTECTEDRESOURCESESSION_0030 session,
                                                  D3D12DDI_HRESOURCE resource_handle) {
    D3D12DDIARG_CREATERESOURCE_0003 legacy = {};
    if (resource && !ProjectR7Resource(*resource, &legacy)) return E_INVALIDARG;
    return CreateHeapAndResource0030(device, heap, heap_handle, runtime_resource, resource ? &legacy : nullptr, clear,
                                     session, resource_handle);
}
static VOID APIENTRY CheckResourceAllocationInfo0075(D3D12DDI_HDEVICE device, const D3D12DDIARG_CREATERESOURCE_0075 *arg,
                                                     D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS flags, UINT32 alignment,
                                                     UINT mask, D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 *info) {
    D3D12DDIARG_CREATERESOURCE_0003 legacy = {};
    if (arg && !ProjectR7Resource(*arg, &legacy)) {
        if (info) {
            memset(info, 0, sizeof(*info));
            info->ResourceDataSize = ~0ULL;
            info->Layout = D3D12DDI_TL_UNDEFINED;
        }
        return;
    }
    CheckResourceAllocationInfo0022(device, arg ? &legacy : nullptr, flags, alignment, mask, info);
}
static_assert(std::is_same_v<decltype(&CalcPrivatePipelineStateSize0075), PFND3D12DDI_CALC_PRIVATE_PIPELINE_STATE_SIZE_0075>);
static_assert(std::is_same_v<decltype(&CreatePipelineState0075), PFND3D12DDI_CREATE_PIPELINE_STATE_0075>);
static_assert(std::is_same_v<decltype(&CalcPrivateHeapAndResourceSizes0075), PFND3D12DDI_CALCPRIVATEHEAPANDRESOURCESIZES_0075>);
static_assert(std::is_same_v<decltype(&CreateHeapAndResource0075), PFND3D12DDI_CREATEHEAPANDRESOURCE_0075>);
static_assert(std::is_same_v<decltype(&CheckResourceAllocationInfo0075), PFND3D12DDI_CHECKRESOURCEALLOCATIONINFO_0075>);
/* C50: 3D_0033 = 3D_0003 with two retyped members and seven appended */
static void FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0033(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033 &f) {
    static_assert(offsetof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033, pfnAtomicCopyBufferRegion) ==
                  sizeof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003));
    FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0003(reinterpret_cast<D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 &>(f));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033, pfnResourceBarrier, ResourceBarrier0022);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033, pfnPresent, Present0028);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033, pfnAtomicCopyBufferRegion, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033, pfnAtomicCopyBufferRegion));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033, pfnOMSetDepthBounds, OMSetDepthBounds);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033, pfnSetSamplePositions, SetSamplePositions);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033, pfnResourceResolveSubresourceRegion, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033, pfnResourceResolveSubresourceRegion));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033, pfnSetProtectedResourceSession, SetProtectedResourceSession);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033, pfnWriteBufferImmediate, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033, pfnWriteBufferImmediate));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033, pfnSetViewInstanceMask, SetViewInstanceMask);
}
/* C51: 3D_0040 = 3D_0033 with the recorder reset */
static void FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0040(D3D12DDI_COMMAND_LIST_FUNCS_3D_0040 &f) {
    static_assert(sizeof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0040) == sizeof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0033));
    FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0033(reinterpret_cast<D3D12DDI_COMMAND_LIST_FUNCS_3D_0033 &>(f));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0040, pfnResetCommandList, ResetCommandList0040);
}
/* C52: 3D_0062 = 3D_0040 with the 0051 present and nine appended entries */
static void FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0062(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062 &f) {
    static_assert(offsetof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnInitializeMetaCommand) ==
                  sizeof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0040));
    FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0040(reinterpret_cast<D3D12DDI_COMMAND_LIST_FUNCS_3D_0040 &>(f));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnPresent, Present0051);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnInitializeMetaCommand, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnInitializeMetaCommand));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnExecuteMetaCommand, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnExecuteMetaCommand));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnBuildRaytracingAccelerationStructure, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnBuildRaytracingAccelerationStructure));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnEmitRaytracingAccelerationStructurePostbuildInfo, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnEmitRaytracingAccelerationStructurePostbuildInfo));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnCopyRaytracingAccelerationStructure, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnCopyRaytracingAccelerationStructure));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnSetPipelineState1, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnSetPipelineState1));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnDispatchRays, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnDispatchRays));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnRSSetShadingRate, RSSetShadingRate0062);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062, pfnRSSetShadingRateImage, RSSetShadingRateImage0062);
}
/* C54: 3D_0074 = 3D_0062 with DispatchMesh (no mesh shaders) */
static void FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0074(D3D12DDI_COMMAND_LIST_FUNCS_3D_0074 &f) {
    static_assert(offsetof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0074, pfnDispatchMesh) == sizeof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0062));
    FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0062(reinterpret_cast<D3D12DDI_COMMAND_LIST_FUNCS_3D_0062 &>(f));
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0074, pfnDispatchMesh, AGX_REJECT(D3D12DDI_COMMAND_LIST_FUNCS_3D_0074, pfnDispatchMesh));
}
static void FillD3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 &f) {
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnExecuteCommandLists, ExecuteCommandLists);
    f.pfnUnused = nullptr;
    f.pfnUnused2 = nullptr;
    /* C53 B3: wired with the native owner; the runtime calls them only for
     * reserved resources */
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnUpdateTileMappings, AGX_TILED_QUEUE(pfnUpdateTileMappings, UpdateTileMappings));
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnCopyTileMappings, AGX_TILED_QUEUE(pfnCopyTileMappings, CopyTileMappings));
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnSignalFence, AGX_REJECT(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnSignalFence));
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnWaitForFence, AGX_REJECT(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnWaitForFence));
}
/* C1: the core tables the runtime requests for the negotiated 0003 device,
 * every entry fail-closed; other tables are refused. */
/* C48: CORE_0012 is CORE_0010 plus pfnGetDebugAllocationInfo. The debug
 * layer's object-to-allocation map: no allocation is described (resources
 * are owner allocations without D3DKMT handles of their own). */
static VOID APIENTRY GetDebugAllocationInfo(D3D12DDI_HDEVICE, D3D12DDI_HANDLE_AND_TYPE, UINT *num_va,
                                            D3D12DDI_DEBUG_VIRTUAL_ADDRESS_ALLOCATION_INFO_0012 *,
                                            UINT *num_kmt, D3D12DDI_DEBUG_KMT_ALLOCATION_INFO_0012 *) {
    if (num_va) *num_va = 0;
    if (num_kmt) *num_kmt = 0;
}
static void FillD3D12DDI_DEVICE_FUNCS_CORE_0012(D3D12DDI_DEVICE_FUNCS_CORE_0012 &f) {
    static_assert(offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0012, pfnGetDebugAllocationInfo) ==
                  sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0010));
    FillD3D12DDI_DEVICE_FUNCS_CORE_0010(reinterpret_cast<D3D12DDI_DEVICE_FUNCS_CORE_0010 &>(f));
    f.pfnGetDebugAllocationInfo = GetDebugAllocationInfo;
}
/* C50: _0014 adds the allocation's offset and size; none is described */
static VOID APIENTRY GetDebugAllocationInfo0014(D3D12DDI_HDEVICE, D3D12DDI_HANDLE_AND_TYPE, UINT *num_va,
                                                D3D12DDI_DEBUG_VIRTUAL_ADDRESS_ALLOCATION_INFO_0012 *,
                                                UINT *num_kmt, D3D12DDI_DEBUG_KMT_ALLOCATION_INFO_0014 *) {
    if (num_va) *num_va = 0;
    if (num_kmt) *num_kmt = 0;
}
static_assert(std::is_same_v<decltype(&GetDebugAllocationInfo0014), PFND3D12DDI_GET_DEBUG_ALLOCATION_INFO_0014>);
/* C50: CORE_0033 has CORE_0012's members in the same order; the members whose
 * PFN type changed are reassigned (test_d3d12_interface_0033 derives the set
 * from the WDK header). */
static void FillD3D12DDI_DEVICE_FUNCS_CORE_0033(D3D12DDI_DEVICE_FUNCS_CORE_0033 &f) {
    static_assert(sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0033) == sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0012));
    FillD3D12DDI_DEVICE_FUNCS_CORE_0012(reinterpret_cast<D3D12DDI_DEVICE_FUNCS_CORE_0012 &>(f));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCalcPrivateDepthStencilStateSize, CalcPrivateDepthStencilStateSize0025);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCreateDepthStencilState, CreateDepthStencilState0025);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCalcPrivateShaderSize, CalcPrivateShaderSize0026);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCreateVertexShader, CreateShader0026);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCreatePixelShader, CreateShader0026);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCreateGeometryShader, CreateShader0026);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCreateComputeShader, CreateComputeShader0026);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCalcPrivateGeometryShaderWithStreamOutput, CalcPrivateGeometryShaderWithStreamOutput0026);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCreateGeometryShaderWithStreamOutput, CreateGeometryShaderWithStreamOutput0026);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCalcPrivateTessellationShaderSize, CalcPrivateShaderSize0026);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCreateHullShader, CreateTessellationShader0026);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCreateDomainShader, CreateTessellationShader0026);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCalcPrivateCommandQueueSize, CalcPrivateCommandQueueSize0023);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCreateCommandQueue, CreateCommandQueue0023);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCalcPrivatePipelineStateSize, CalcPrivatePipelineStateSize0033);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCreatePipelineState, CreatePipelineState0033);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCalcPrivateRootSignatureSize, CalcPrivateRootSignatureSize0013);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCreateRootSignature, CreateRootSignature0013);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCalcPrivateHeapAndResourceSizes, CalcPrivateHeapAndResourceSizes0030);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCreateHeapAndResource, CreateHeapAndResource0030);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCheckResourceAllocationInfo, CheckResourceAllocationInfo0022);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnCheckExistingResourceAllocationInfo, CheckExistingResourceAllocationInfo0022);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0033, pfnGetDebugAllocationInfo, GetDebugAllocationInfo0014);
}
/* C51: CORE_0040 = CORE_0033 with the pool and list creation retyped and
 * the recorder appended */
static void FillD3D12DDI_DEVICE_FUNCS_CORE_0040(D3D12DDI_DEVICE_FUNCS_CORE_0040 &f) {
    static_assert(offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0040, pfnCalcPrivateCommandRecorderSize) ==
                  sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0033));
    FillD3D12DDI_DEVICE_FUNCS_CORE_0033(reinterpret_cast<D3D12DDI_DEVICE_FUNCS_CORE_0033 &>(f));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0040, pfnCalcPrivateCommandPoolSize, CalcPrivateCommandPoolSize0040);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0040, pfnCreateCommandPool, CreateCommandPool0040);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0040, pfnDestroyCommandPool, DestroyCommandPool0040);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0040, pfnResetCommandPool, ResetCommandPool0040);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0040, pfnCalcPrivateCommandListSize, CalcPrivateCommandListSize0040);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0040, pfnCreateCommandList, CreateCommandList0040);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0040, pfnCalcPrivateCommandRecorderSize, CalcPrivateCommandRecorderSize0040);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0040, pfnCreateCommandRecorder, CreateCommandRecorder0040);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0040, pfnDestroyCommandRecorder, DestroyCommandRecorder0040);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0040, pfnCommandRecorderSetCommandPoolAsTarget, CommandRecorderSetCommandPoolAsTarget0040);
}
/* C52: CORE_0070 = CORE_0040 with the 0050 queue and 0043 opened-resource
 * entries retyped and twenty entries appended */
static void FillD3D12DDI_DEVICE_FUNCS_CORE_0070(D3D12DDI_DEVICE_FUNCS_CORE_0070 &f) {
    static_assert(offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCalcPrivateSchedulingGroupSize) ==
                  sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0040));
    FillD3D12DDI_DEVICE_FUNCS_CORE_0040(reinterpret_cast<D3D12DDI_DEVICE_FUNCS_CORE_0040 &>(f));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCalcPrivateCommandQueueSize, CalcPrivateCommandQueueSize0050);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCreateCommandQueue, CreateCommandQueue0050);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCalcPrivateOpenedHeapAndResourceSizes, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCalcPrivateOpenedHeapAndResourceSizes));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnOpenHeapAndResource, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnOpenHeapAndResource));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCalcPrivateSchedulingGroupSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCalcPrivateSchedulingGroupSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCreateSchedulingGroup, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCreateSchedulingGroup));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnDestroySchedulingGroup, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnDestroySchedulingGroup));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnEnumerateMetaCommands, EnumerateMetaCommands0052);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnEnumerateMetaCommandParameters, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnEnumerateMetaCommandParameters));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCalcPrivateMetaCommandSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCalcPrivateMetaCommandSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCreateMetaCommand, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCreateMetaCommand));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnDestroyMetaCommand, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnDestroyMetaCommand));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnGetMetaCommandRequiredParameterInfo, GetMetaCommandRequiredParameterInfo0052);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCalcPrivateStateObjectSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCalcPrivateStateObjectSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCreateStateObject, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCreateStateObject));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnDestroyStateObject, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnDestroyStateObject));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnGetRaytracingAccelerationStructurePrebuildInfo, GetRaytracingAccelerationStructurePrebuildInfo0054);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnCheckDriverMatchingIdentifier, CheckDriverMatchingIdentifier0054);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnGetShaderIdentifier, GetShaderIdentifier0054);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnGetShaderStackSize, GetShaderStackSize0054);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnGetPipelineStackSize, GetPipelineStackSize0054);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnSetPipelineStackSize, SetPipelineStackSize0054);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnSetBackgroundProcessingMode, SetBackgroundProcessingMode0063);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnAddToStateObject, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnAddToStateObject));
}
/* C54: CORE_0075 = CORE_0070 up to index 114, then the add-to-state-object
 * size entry before AddToStateObject_0072, and the sampler-feedback view and
 * mesh/amplification shader entries; PSO, heap/resource and allocation
 * info take the 0075 argument forms */
static void FillD3D12DDI_DEVICE_FUNCS_CORE_0075(D3D12DDI_DEVICE_FUNCS_CORE_0075 &f) {
    static_assert(offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCalcPrivateAddToStateObjectSize) ==
                  offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0070, pfnAddToStateObject));
    FillD3D12DDI_DEVICE_FUNCS_CORE_0070(reinterpret_cast<D3D12DDI_DEVICE_FUNCS_CORE_0070 &>(f));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCalcPrivatePipelineStateSize, CalcPrivatePipelineStateSize0075);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCreatePipelineState, CreatePipelineState0075);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCalcPrivateHeapAndResourceSizes, CalcPrivateHeapAndResourceSizes0075);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCreateHeapAndResource, CreateHeapAndResource0075);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCheckResourceAllocationInfo, CheckResourceAllocationInfo0075);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCalcPrivateAddToStateObjectSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCalcPrivateAddToStateObjectSize));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnAddToStateObject, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnAddToStateObject));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCreateSamplerFeedbackUnorderedAccessView, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCreateSamplerFeedbackUnorderedAccessView));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCreateAmplificationShader, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCreateAmplificationShader));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCreateMeshShader, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCreateMeshShader));
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCalcPrivateMeshShaderSize, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0075, pfnCalcPrivateMeshShaderSize));
}
/* EXP1868: after the 0033 core and list tables the runtime asked for
 * D3D12DDI_TABLE_TYPE_0020_EXTENDED_FEATURES (0x20 bytes, the _0021 table)
 * although GetOptionalDDITables lists nothing, and failed device creation
 * when it was refused. No extended feature (video, shader caching, content
 * protection, render passes, downlevel support) is offered. */
static HRESULT APIENTRY GetSupportedExtendedFeatures0020(D3D12DDI_HDEVICE, UINT32 *count, D3D12DDI_FEATURE_0020 *) {
    if (!count) return E_INVALIDARG;
    *count = 0;
    return S_OK;
}
static HRESULT APIENTRY GetSupportedExtendedFeatureVersions0020(D3D12DDI_HDEVICE, D3D12DDI_FEATURE_0020, UINT32 *count,
                                                                UINT32 *) {
    if (count) *count = 0;
    return E_INVALIDARG;
}
static HRESULT APIENTRY EnableExtendedFeature0020(D3D12DDI_HDEVICE, D3D12DDI_FEATURE_0020, UINT32) { return E_INVALIDARG; }
/* callbacks belong to enabled features; none is enabled, nothing is kept */
static HRESULT APIENTRY SetExtendedFeatureCallbacks0021(D3D12DDI_HDEVICE, D3D12DDI_TABLE_TYPE, const void *, SIZE_T) {
    return S_OK;
}
static void FillD3D12DDI_EXTENDED_FEATURES_FUNCS_0021(D3D12DDI_EXTENDED_FEATURES_FUNCS_0021 &f) {
    f.pfnGetSupportedExtendedFeatures = GetSupportedExtendedFeatures0020;
    f.pfnGetSupportedExtendedFeatureVersions = GetSupportedExtendedFeatureVersions0020;
    f.pfnEnableExtendedFeature = EnableExtendedFeature0020;
    f.pfnSetExtendedFeatureCallbacks = SetExtendedFeatureCallbacks0021;
}
static_assert(std::is_same_v<decltype(&GetSupportedExtendedFeatures0020), PFND3D12DDI_GET_SUPPORTED_EXTENDED_FEATURES_0020>);
static_assert(std::is_same_v<decltype(&GetSupportedExtendedFeatureVersions0020),
                             PFND3D12DDI_GET_SUPPORTED_EXTENDED_FEATURE_VERSIONS_0020>);
static_assert(std::is_same_v<decltype(&EnableExtendedFeature0020), PFND3D12DDI_ENABLE_EXTENDED_FEATURE_0020>);
static_assert(std::is_same_v<decltype(&SetExtendedFeatureCallbacks0021), PFND3D12DDI_SET_EXTENDED_FEATURE_CALLBACKS_0021>);
template<class T> static HRESULT FillTable(void *table, SIZE_T size, void (*fill)(T &)) {
    if (!table || size < sizeof(T)) return E_INVALIDARG;
    memset(table, 0, size);
    fill(*static_cast<T *>(table));
    return S_OK;
}
static HRESULT APIENTRY FillDDITable(D3D12DDI_HADAPTER, D3D12DDI_TABLE_TYPE type, void *table,
                                     SIZE_T size, UINT table_num, D3D12DDI_HRTTABLE rt_table) {
    HRESULT hr = E_NOTIMPL;
    const bool tabled = !NegotiatedR2() || Negotiated0033() || Negotiated0042() || Negotiated0076();
    /* C4a/0003: a command list names the first successful runtime list table.
     * table_num is an instance index, not a DDI version; R1's second table
     * instance needs a separate selection/lifetime contract before admission. */
    switch (tabled ? type : static_cast<D3D12DDI_TABLE_TYPE>(~0u)) {
    case D3D12DDI_TABLE_TYPE_DEVICE_CORE:
        /* C44: the runtime sizes the table of the interface it negotiated
         * (GetVersions): R1 (0011) takes CORE_0010, 0012 CORE_0012; C50:
         * CORE_0033 has CORE_0012's size, the negotiated version decides */
        hr = Negotiated0076()
                 ? FillTable<D3D12DDI_DEVICE_FUNCS_CORE_0075>(table, size, FillD3D12DDI_DEVICE_FUNCS_CORE_0075)
             : Negotiated0042()
                 ? FillTable<D3D12DDI_DEVICE_FUNCS_CORE_0040>(table, size, FillD3D12DDI_DEVICE_FUNCS_CORE_0040)
             : Negotiated0033()
                 ? FillTable<D3D12DDI_DEVICE_FUNCS_CORE_0033>(table, size, FillD3D12DDI_DEVICE_FUNCS_CORE_0033)
             : size >= sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0012)
                 ? FillTable<D3D12DDI_DEVICE_FUNCS_CORE_0012>(table, size, FillD3D12DDI_DEVICE_FUNCS_CORE_0012)
             : size >= sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0010)
                 ? FillTable<D3D12DDI_DEVICE_FUNCS_CORE_0010>(table, size, FillD3D12DDI_DEVICE_FUNCS_CORE_0010)
                 : FillTable<D3D12DDI_DEVICE_FUNCS_CORE_0003>(table, size, FillD3D12DDI_DEVICE_FUNCS_CORE_0003);
        break;
    case D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D:
        hr = Negotiated0076()
                 ? FillTable<D3D12DDI_COMMAND_LIST_FUNCS_3D_0074>(table, size, FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0074)
             : Negotiated0042()
                 ? FillTable<D3D12DDI_COMMAND_LIST_FUNCS_3D_0040>(table, size, FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0040)
             : Negotiated0033()
                 ? FillTable<D3D12DDI_COMMAND_LIST_FUNCS_3D_0033>(table, size, FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0033)
                 : FillTable<D3D12DDI_COMMAND_LIST_FUNCS_3D_0003>(table, size, FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0003);
        break;
    case D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D:
        hr = FillTable<D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001>(table, size, FillD3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001);
        break;
    case D3D12DDI_TABLE_TYPE_0020_EXTENDED_FEATURES:
        /* R2+ (0021 has the callbacks member); the R1 runtime never asks */
        if (NegotiatedR2())
            hr = FillTable<D3D12DDI_EXTENDED_FEATURES_FUNCS_0021>(table, size, FillD3D12DDI_EXTENDED_FEATURES_FUNCS_0021);
        break;
    default:
        break;
    }
    if (SUCCEEDED(hr) && type == D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D && !list_table.handle)
        list_table = rt_table;
    const UINT receipt[] = {static_cast<UINT>(type), static_cast<UINT>(size), table_num};
    AdmissionUmdDiagnostic("d3d12-fill-table", hr, receipt, 3);
    return hr;
}
} // namespace
extern "C" HRESULT APIENTRY OpenAdapter12(D3D12DDIARG_OPENADAPTER *arg) {
    char enabled[2] = {};
#ifdef APPLE_AGX_D3D12_NATIVE_OWNER
    /* C56: validated on J313 (EXP1873: probes 107/107, the 19 samples and
     * MiniEngine at interface 0076), D3D12 serves every process with the
     * native owner; APPLE_AGX_D3D12=0 opts a process out */
    if (GetEnvironmentVariableA("APPLE_AGX_D3D12", enabled, sizeof(enabled)) == 1 && enabled[0] == '0')
        return E_NOTIMPL;
#else
    /* without the native owner nothing executes: only an opted-in process */
    if (GetEnvironmentVariableA("APPLE_AGX_D3D12", enabled, sizeof(enabled)) != 1 || enabled[0] != '1')
        return E_NOTIMPL;
#endif
    const UINT receipt[] = {arg != nullptr, arg && arg->pAdapterFuncs != nullptr,
                            arg && arg->pAdapterCallbacks != nullptr};
    AdmissionUmdDiagnostic("d3d12-open-adapter", arg && arg->pAdapterFuncs ? S_OK : E_INVALIDARG, receipt, 3);
    if (!arg || !arg->pAdapterFuncs) return E_INVALIDARG;
    adapter_object.runtime = arg->hRTAdapter;
    adapter_object.callbacks = arg->pAdapterCallbacks;
    adapter_object.info_valid = FALSE;
    adapter_object.negotiated = 0;
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
