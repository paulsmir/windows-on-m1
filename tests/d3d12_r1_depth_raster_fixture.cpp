/* Actual-source R1 depth/raster callbacks; host type model, not Windows ABI. */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

using SIZE_T = std::size_t;
using HRESULT = std::int32_t;
using VOID = void;
using BOOL = int;
using UINT8 = std::uint8_t;
using UINT = std::uint32_t;
using INT = int;
using FLOAT = float;
#define APIENTRY
#define S_OK static_cast<HRESULT>(0)
#define E_INVALIDARG static_cast<HRESULT>(0x80070057u)
#define FAILED(value) ((value) < 0)
struct D3D12DDI_HDEVICE { void *pDrvPrivate; };
struct D3D12DDI_HDEPTHSTENCILSTATE { void *pDrvPrivate; };
struct D3D12DDI_HRASTERIZERSTATE { void *pDrvPrivate; };
struct D3D12DDI_HPIPELINELIBRARY { void *pDrvPrivate; };
struct D3D12DDI_LIBRARY_REFERENCE_0010 { D3D12DDI_HPIPELINELIBRARY hLibrary; UINT PipelineIndex; };
using D3D12DDI_DEPTH_WRITE_MASK = int;
using D3D12DDI_COMPARISON_FUNC = int;
using D3D12DDI_STENCIL_OP = int;
using D3D12DDI_FILL_MODE = int;
using D3D12DDI_CULL_MODE = int;
using D3D12DDI_CONSERVATIVE_RASTERIZATION_MODE = int;
struct D3D12DDI_DEPTH_STENCILOP_DESC {
    D3D12DDI_STENCIL_OP StencilFailOp, StencilDepthFailOp, StencilPassOp;
    D3D12DDI_COMPARISON_FUNC StencilFunc;
};
#define DEPTH_FIELDS \
    BOOL DepthEnable; D3D12DDI_DEPTH_WRITE_MASK DepthWriteMask; \
    D3D12DDI_COMPARISON_FUNC DepthFunc; BOOL StencilEnable; \
    BOOL FrontEnable, BackEnable; UINT8 StencilReadMask, StencilWriteMask; \
    D3D12DDI_DEPTH_STENCILOP_DESC FrontFace, BackFace;
struct D3D12DDI_DEPTH_STENCIL_DESC { DEPTH_FIELDS };
struct D3D12DDI_DEPTH_STENCIL_DESC_0010 {
    DEPTH_FIELDS D3D12DDI_LIBRARY_REFERENCE_0010 LibraryReference;
};
#undef DEPTH_FIELDS
#define RASTER_FIELDS \
    D3D12DDI_FILL_MODE FillMode; D3D12DDI_CULL_MODE CullMode; \
    BOOL FrontCounterClockwise; INT DepthBias; FLOAT DepthBiasClamp, SlopeScaledDepthBias; \
    BOOL DepthClipEnable, ScissorEnable, MultisampleEnable, AntialiasedLineEnable; \
    UINT ForcedSampleCount; D3D12DDI_CONSERVATIVE_RASTERIZATION_MODE ConservativeRasterizationMode;
struct D3D12DDI_RASTERIZER_DESC { RASTER_FIELDS };
struct D3D12DDI_RASTERIZER_DESC_0010 {
    RASTER_FIELDS D3D12DDI_LIBRARY_REFERENCE_0010 LibraryReference;
};
#undef RASTER_FIELDS
using PFND3D12DDI_CALCPRIVATEDEPTHSTENCILSTATESIZE_0010 = SIZE_T (APIENTRY *)(D3D12DDI_HDEVICE, const D3D12DDI_DEPTH_STENCIL_DESC_0010 *);
using PFND3D12DDI_CREATEDEPTHSTENCILSTATE_0010 = VOID (APIENTRY *)(D3D12DDI_HDEVICE, const D3D12DDI_DEPTH_STENCIL_DESC_0010 *, D3D12DDI_HDEPTHSTENCILSTATE);
using PFND3D12DDI_CALCPRIVATERASTERIZERSTATESIZE_0010 = SIZE_T (APIENTRY *)(D3D12DDI_HDEVICE, const D3D12DDI_RASTERIZER_DESC_0010 *);
using PFND3D12DDI_CREATERASTERIZERSTATE_0010 = VOID (APIENTRY *)(D3D12DDI_HDEVICE, const D3D12DDI_RASTERIZER_DESC_0010 *, D3D12DDI_HRASTERIZERSTATE);

static int live_device, live_library;
static UINT checks, failures, reports, depth_creates, raster_creates;
static void check(bool good, const char *name) {
    ++checks;
    if (!good) { ++failures; std::printf("R1_STATE_FAIL %s\n", name); }
}
static HRESULT APIENTRY AgxD3d12ZeroCacheValidateLibraryReference(
    D3D12DDI_HDEVICE device, const D3D12DDI_LIBRARY_REFERENCE_0010 *reference) {
    if (device.pDrvPrivate != &live_device || !reference) return E_INVALIDARG;
    void *library = reference->hLibrary.pDrvPrivate;
    return !library || library == &live_library ? S_OK : E_INVALIDARG;
}
static void ReportRejectedDeviceError(D3D12DDI_HDEVICE, HRESULT error) {
    if (error == E_INVALIDARG) ++reports;
}
struct DepthStencilState {
    static SIZE_T APIENTRY size(D3D12DDI_HDEVICE, const D3D12DDI_DEPTH_STENCIL_DESC *) { return sizeof(D3D12DDI_DEPTH_STENCIL_DESC); }
    template<class Handle> static VOID APIENTRY create(D3D12DDI_HDEVICE,
        const D3D12DDI_DEPTH_STENCIL_DESC *desc, Handle handle) {
        ++depth_creates; std::memcpy(handle.pDrvPrivate, desc, sizeof(*desc));
    }
};
struct RasterizerState {
    static SIZE_T APIENTRY size(D3D12DDI_HDEVICE, const D3D12DDI_RASTERIZER_DESC *) { return sizeof(D3D12DDI_RASTERIZER_DESC); }
    template<class Handle> static VOID APIENTRY create(D3D12DDI_HDEVICE,
        const D3D12DDI_RASTERIZER_DESC *desc, Handle handle) {
        ++raster_creates; std::memcpy(handle.pDrvPrivate, desc, sizeof(*desc));
    }
};

/* ACTUAL_R1_DEPTH_RASTER_WRAPPERS */

int main() {
    static_assert(std::is_same_v<decltype(&CalcPrivateDepthStencilStateSize0010), PFND3D12DDI_CALCPRIVATEDEPTHSTENCILSTATESIZE_0010>);
    static_assert(std::is_same_v<decltype(&CreateDepthStencilState0010), PFND3D12DDI_CREATEDEPTHSTENCILSTATE_0010>);
    static_assert(std::is_same_v<decltype(&CalcPrivateRasterizerStateSize0010), PFND3D12DDI_CALCPRIVATERASTERIZERSTATESIZE_0010>);
    static_assert(std::is_same_v<decltype(&CreateRasterizerState0010), PFND3D12DDI_CREATERASTERIZERSTATE_0010>);
    D3D12DDI_HDEVICE live{&live_device}, dead{};
    D3D12DDI_DEPTH_STENCIL_DESC_0010 depth{};
    depth.DepthEnable=1; depth.DepthWriteMask=2; depth.DepthFunc=3;
    depth.StencilEnable=1; depth.FrontEnable=1; depth.BackEnable=0;
    depth.StencilReadMask=0x53; depth.StencilWriteMask=0xa7;
    depth.FrontFace={4,5,6,7}; depth.BackFace={8,9,10,11};
    D3D12DDI_RASTERIZER_DESC_0010 raster{};
    raster.FillMode=2; raster.CullMode=3; raster.FrontCounterClockwise=1;
    raster.DepthBias=-17; raster.DepthBiasClamp=1.25f; raster.SlopeScaledDepthBias=-2.5f;
    raster.DepthClipEnable=1; raster.ScissorEnable=1; raster.MultisampleEnable=0;
    raster.AntialiasedLineEnable=1; raster.ForcedSampleCount=4;
    raster.ConservativeRasterizationMode=5;
    struct DGuard { std::uint64_t pre; D3D12DDI_DEPTH_STENCIL_DESC state; std::uint64_t post; };
    struct RGuard { std::uint64_t pre; D3D12DDI_RASTERIZER_DESC state; std::uint64_t post; };
    DGuard d{0x1234567812345678ull, {}, 0x8765432187654321ull};
    RGuard r{0x1234567812345678ull, {}, 0x8765432187654321ull};
    D3D12DDI_HDEPTHSTENCILSTATE dh{&d.state};
    D3D12DDI_HRASTERIZERSTATE rh{&r.state};
    D3D12DDI_DEPTH_STENCIL_DESC expected_depth{};
    D3D12DDI_RASTERIZER_DESC expected_raster{};
    std::memcpy(&expected_depth, &depth, sizeof(expected_depth));
    std::memcpy(&expected_raster, &raster, sizeof(expected_raster));
    check(CalcPrivateDepthStencilStateSize0010(live,&depth)==sizeof(expected_depth),"depth size");
    check(CalcPrivateRasterizerStateSize0010(live,&raster)==sizeof(expected_raster),"raster size");
    CreateDepthStencilState0010(live,&depth,dh);
    CreateRasterizerState0010(live,&raster,rh);
    check(depth_creates==1&&std::memcmp(&d.state,&expected_depth,sizeof(expected_depth))==0,"all depth fields");
    check(raster_creates==1&&std::memcmp(&r.state,&expected_raster,sizeof(expected_raster))==0,"all raster fields");
    check(d.pre==0x1234567812345678ull&&d.post==0x8765432187654321ull&&
          r.pre==0x1234567812345678ull&&r.post==0x8765432187654321ull,"canaries");
    depth.LibraryReference={{&live_library},0xffffffffu};
    raster.LibraryReference={{&live_library},0xffffffffu};
    CreateDepthStencilState0010(live,&depth,dh);
    CreateRasterizerState0010(live,&raster,rh);
    check(depth_creates==2&&std::memcmp(&d.state,&expected_depth,sizeof(expected_depth))==0,"depth library excluded");
    check(raster_creates==2&&std::memcmp(&r.state,&expected_raster,sizeof(expected_raster))==0,"raster library excluded");
    int foreign;
    depth.LibraryReference.hLibrary={&foreign}; raster.LibraryReference.hLibrary={&foreign};
    const DGuard d_before=d; const RGuard r_before=r;
    check(CalcPrivateDepthStencilStateSize0010(live,&depth)==0,"foreign depth size");
    check(CalcPrivateRasterizerStateSize0010(live,&raster)==0,"foreign raster size");
    CreateDepthStencilState0010(live,&depth,dh);
    CreateRasterizerState0010(live,&raster,rh);
    check(depth_creates==2&&std::memcmp(&d,&d_before,sizeof(d))==0,"foreign depth no write");
    check(raster_creates==2&&std::memcmp(&r,&r_before,sizeof(r))==0,"foreign raster no write");
    depth.LibraryReference={{},0}; raster.LibraryReference={{},0};
    check(CalcPrivateDepthStencilStateSize0010(dead,&depth)==0,"dead depth size");
    check(CalcPrivateRasterizerStateSize0010(dead,&raster)==0,"dead raster size");
    CreateDepthStencilState0010(dead,&depth,dh);
    CreateRasterizerState0010(dead,&raster,rh);
    check(depth_creates==2&&raster_creates==2,"dead creates refused");
    CreateDepthStencilState0010(live,nullptr,dh);
    CreateRasterizerState0010(live,nullptr,rh);
    CreateDepthStencilState0010(live,&depth,{});
    CreateRasterizerState0010(live,&raster,{});
    check(depth_creates==2&&raster_creates==2&&reports==8,"null creates refused");
    std::printf("R1_DEPTH_RASTER_PASS checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
