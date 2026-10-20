/* Actual-source R1 PSO destination adapter; host type model, not Windows ABI. */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

using UINT = std::uint32_t;
using SIZE_T = std::size_t;
using HRESULT = std::int32_t;
#define APIENTRY
#define S_OK static_cast<HRESULT>(0)
#define E_INVALIDARG static_cast<HRESULT>(0x80070057u)
#define FAILED(value) ((value) < 0)
struct Handle { void *pDrvPrivate; };
using D3D12DDI_HDEVICE = Handle;
using D3D12DDI_HPIPELINESTATE = Handle;
using D3D12DDI_HSHADER = Handle;
using D3D12DDI_HROOTSIGNATURE = Handle;
using D3D12DDI_HBLENDSTATE = Handle;
using D3D12DDI_HRASTERIZERSTATE = Handle;
using D3D12DDI_HDEPTHSTENCILSTATE = Handle;
using D3D12DDI_HELEMENTLAYOUT = Handle;
using D3D12DDI_HPIPELINELIBRARY = Handle;
using D3D12DDI_INDEX_BUFFER_STRIP_CUT_VALUE = int;
using D3D12DDI_PRIMITIVE_TOPOLOGY_TYPE = int;
using DXGI_FORMAT = int;
struct DXGI_SAMPLE_DESC { UINT Count, Quality; };
struct D3D12DDI_LIBRARY_REFERENCE_0010 { D3D12DDI_HPIPELINELIBRARY hLibrary; UINT PipelineIndex; };
#define PSO_FIELDS \
    D3D12DDI_HSHADER hComputeShader, hVertexShader, hPixelShader, hDomainShader, hHullShader, hGeometryShader; \
    D3D12DDI_HROOTSIGNATURE hRootSignature; D3D12DDI_HBLENDSTATE hBlendState; UINT SampleMask; \
    D3D12DDI_HRASTERIZERSTATE hRasterizerState; D3D12DDI_HDEPTHSTENCILSTATE hDepthStencilState; \
    D3D12DDI_HELEMENTLAYOUT hElementLayout; D3D12DDI_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue; \
    D3D12DDI_PRIMITIVE_TOPOLOGY_TYPE PrimitiveTopologyType; UINT NumRenderTargets; \
    DXGI_FORMAT RTVFormats[8]; DXGI_FORMAT DSVFormat; DXGI_SAMPLE_DESC SampleDesc; UINT NodeMask;
struct D3D12DDIARG_CREATE_PIPELINE_STATE_0001 { D3D12DDI_HPIPELINESTATE hDrvPipelineState; PSO_FIELDS };
struct D3D12DDIARG_CREATE_PIPELINE_STATE_0010 { PSO_FIELDS D3D12DDI_LIBRARY_REFERENCE_0010 LibraryReference; };
#undef PSO_FIELDS
using PFND3D12DDI_CALC_PRIVATE_PIPELINE_STATE_SIZE_0010 = SIZE_T (APIENTRY *)(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_PIPELINE_STATE_0010 *);
using PFND3D12DDI_CREATE_PIPELINE_STATE_0010 = HRESULT (APIENTRY *)(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_PIPELINE_STATE_0010 *, D3D12DDI_HPIPELINESTATE);

static int live_device, live_library;
static UINT checks, failures, old_calc, old_create;
static D3D12DDIARG_CREATE_PIPELINE_STATE_0001 captured_calc{}, captured_create{};
static void check(bool good, const char *name) {
    ++checks;
    if (!good) { ++failures; std::printf("R1_PSO_FAIL %s\n", name); }
}
static HRESULT APIENTRY AgxD3d12ZeroCacheValidateLibraryReference(
    D3D12DDI_HDEVICE device, const D3D12DDI_LIBRARY_REFERENCE_0010 *reference) {
    if (device.pDrvPrivate != &live_device || !reference) return E_INVALIDARG;
    void *library = reference->hLibrary.pDrvPrivate;
    return !library || library == &live_library ? S_OK : E_INVALIDARG;
}
static SIZE_T APIENTRY CalcPrivatePipelineStateSize(D3D12DDI_HDEVICE,
    const D3D12DDIARG_CREATE_PIPELINE_STATE_0001 *arg) {
    ++old_calc; captured_calc = *arg; return 4096;
}
static HRESULT APIENTRY CreatePipelineState(D3D12DDI_HDEVICE,
    const D3D12DDIARG_CREATE_PIPELINE_STATE_0001 *arg) {
    ++old_create; captured_create = *arg;
    if (!arg->hDrvPipelineState.pDrvPrivate) return E_INVALIDARG;
    *static_cast<int *>(arg->hDrvPipelineState.pDrvPrivate) = 0x5a5a;
    return S_OK;
}

/* ACTUAL_R1_PSO_WRAPPERS */

int main() {
    static_assert(std::is_same_v<decltype(&CalcPrivatePipelineStateSize0010), PFND3D12DDI_CALC_PRIVATE_PIPELINE_STATE_SIZE_0010>);
    static_assert(std::is_same_v<decltype(&CreatePipelineState0010), PFND3D12DDI_CREATE_PIPELINE_STATE_0010>);
    D3D12DDI_HDEVICE live{&live_device}, dead{};
    D3D12DDIARG_CREATE_PIPELINE_STATE_0010 input;
    std::memset(&input,0,sizeof(input));
    int handles[11]{};
    input.hComputeShader={&handles[0]}; input.hVertexShader={&handles[1]};
    input.hPixelShader={&handles[2]}; input.hDomainShader={&handles[3]};
    input.hHullShader={&handles[4]}; input.hGeometryShader={&handles[5]};
    input.hRootSignature={&handles[6]}; input.hBlendState={&handles[7]};
    input.hRasterizerState={&handles[8]}; input.hDepthStencilState={&handles[9]};
    input.hElementLayout={&handles[10]}; input.SampleMask=0xa5a5a5a5u;
    input.IBStripCutValue=3; input.PrimitiveTopologyType=4; input.NumRenderTargets=8;
    for (unsigned i=0;i<8;++i) input.RTVFormats[i]=100+static_cast<int>(i);
    input.DSVFormat=200; input.SampleDesc={4,7}; input.NodeMask=0x55aa;
    struct Guarded { std::uint64_t pre; int value; std::uint64_t post; };
    Guarded output{0x1234567812345678ull,0,0x8765432187654321ull};
    D3D12DDI_HPIPELINESTATE destination{&output.value};
    D3D12DDIARG_CREATE_PIPELINE_STATE_0001 expected;
    std::memset(&expected,0,sizeof(expected));
    expected.hDrvPipelineState=destination;
    std::memcpy(&expected.hComputeShader,&input.hComputeShader,
                offsetof(D3D12DDIARG_CREATE_PIPELINE_STATE_0001,NodeMask)+sizeof(expected.NodeMask)-
                offsetof(D3D12DDIARG_CREATE_PIPELINE_STATE_0001,hComputeShader));
    check(CalcPrivatePipelineStateSize0010(live,&input)==4096,"ordinary calc size");
    check(old_calc==1&&captured_calc.hDrvPipelineState.pDrvPrivate==nullptr,"calc omits destination");
    expected.hDrvPipelineState={};
    check(std::memcmp(&captured_calc,&expected,sizeof(expected))==0,"calc semantic fields");
    expected.hDrvPipelineState=destination;
    check(CreatePipelineState0010(live,&input,destination)==S_OK,"ordinary create");
    check(old_create==1&&std::memcmp(&captured_create,&expected,sizeof(expected))==0,"separate destination and all fields");
    check(output.value==0x5a5a&&output.pre==0x1234567812345678ull&&
          output.post==0x8765432187654321ull,"destination canaries");
    input.LibraryReference={{&live_library},0xffffffffu};
    check(CalcPrivatePipelineStateSize0010(live,&input)==4096,"live library calc");
    check(CreatePipelineState0010(live,&input,destination)==S_OK&&old_create==2,"live library create");
    check(std::memcmp(&captured_create,&expected,sizeof(expected))==0,"library metadata excluded");
    int foreign;
    input.LibraryReference.hLibrary={&foreign};
    const Guarded before=output;
    check(CalcPrivatePipelineStateSize0010(live,&input)==0&&old_calc==2,"foreign calc refused");
    check(CreatePipelineState0010(live,&input,destination)==E_INVALIDARG&&old_create==2,"foreign create refused");
    check(std::memcmp(&before,&output,sizeof(output))==0,"foreign destination untouched");
    input.LibraryReference={{},0};
    check(CalcPrivatePipelineStateSize0010(dead,&input)==0,"dead device calc refused");
    check(CreatePipelineState0010(dead,&input,destination)==E_INVALIDARG&&old_create==2,"dead device create refused");
    check(CalcPrivatePipelineStateSize0010(live,nullptr)==0,"null record calc refused");
    check(CreatePipelineState0010(live,nullptr,destination)==E_INVALIDARG,"null record create refused");
    check(CreatePipelineState0010(live,&input,{})==E_INVALIDARG&&old_create==2,"null destination refused");
    std::printf("R1_PSO_DEST_PASS checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
