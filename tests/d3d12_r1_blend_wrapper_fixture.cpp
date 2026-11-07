/* Actual-source R1 blend callback fixture. Host types are a model, not WDK ABI. */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

using BOOL = int;
using UINT8 = std::uint8_t;
using SIZE_T = std::size_t;
using HRESULT = std::int32_t;
using VOID = void;
constexpr unsigned D3D12DDI_SIMULTANEOUS_RENDER_TARGET_COUNT = 8;
#define APIENTRY
#define S_OK static_cast<HRESULT>(0)
#define E_INVALIDARG static_cast<HRESULT>(0x80070057u)
#define FAILED(value) ((value) < 0)
struct D3D12DDI_HDEVICE { void *pDrvPrivate; };
struct D3D12DDI_HBLENDSTATE { void *pDrvPrivate; };
struct D3D12DDI_HPIPELINELIBRARY { void *pDrvPrivate; };
struct D3D12DDI_LIBRARY_REFERENCE_0010 { D3D12DDI_HPIPELINELIBRARY hLibrary; unsigned PipelineIndex; };
struct D3D12DDI_RENDER_TARGET_BLEND_DESC {
    BOOL BlendEnable, LogicOpEnable;
    int SrcBlend, DestBlend, BlendOp, SrcBlendAlpha, DestBlendAlpha, BlendOpAlpha, LogicOp;
    UINT8 RenderTargetWriteMask;
};
struct D3D12DDI_BLEND_DESC {
    BOOL AlphaToCoverageEnable, IndependentBlendEnable;
    D3D12DDI_RENDER_TARGET_BLEND_DESC RenderTarget[8];
};
struct D3D12DDI_BLEND_DESC_0010 {
    BOOL AlphaToCoverageEnable, IndependentBlendEnable;
    D3D12DDI_RENDER_TARGET_BLEND_DESC RenderTarget[8];
    D3D12DDI_LIBRARY_REFERENCE_0010 LibraryReference;
};
using PFND3D12DDI_CALCPRIVATEBLENDSTATESIZE_0010 = SIZE_T (APIENTRY *)(D3D12DDI_HDEVICE, const D3D12DDI_BLEND_DESC_0010 *);
using PFND3D12DDI_CREATEBLENDSTATE_0010 = VOID (APIENTRY *)(D3D12DDI_HDEVICE, const D3D12DDI_BLEND_DESC_0010 *, D3D12DDI_HBLENDSTATE);
#include "agx_d3d12_r1_blend_projection.h"

static int live_device, live_library;
static unsigned checks, failures, reports, creates, validations;
static void check(bool good, const char *name) {
    ++checks;
    if (!good) { ++failures; std::printf("R1_BLEND_WRAPPER_FAIL %s\n", name); }
}
static HRESULT APIENTRY AgxD3d12ZeroCacheValidateLibraryReference(
    D3D12DDI_HDEVICE device, const D3D12DDI_LIBRARY_REFERENCE_0010 *reference) {
    ++validations;
    if (device.pDrvPrivate != &live_device || !reference) return E_INVALIDARG;
    void *library = reference->hLibrary.pDrvPrivate;
    return !library || library == &live_library ? S_OK : E_INVALIDARG;
}
static void ReportRejectedDeviceError(D3D12DDI_HDEVICE, HRESULT error) {
    if (error == E_INVALIDARG) ++reports;
}
struct BlendState {
    template<class Handle> static VOID APIENTRY create(D3D12DDI_HDEVICE,
        const D3D12DDI_BLEND_DESC *desc, Handle handle) {
        ++creates;
        std::memcpy(handle.pDrvPrivate, desc, sizeof(*desc));
    }
};

/* ACTUAL_R1_BLEND_WRAPPERS */

int main() {
    static_assert(std::is_same_v<decltype(&CalcPrivateBlendStateSize0010), PFND3D12DDI_CALCPRIVATEBLENDSTATESIZE_0010>);
    static_assert(std::is_same_v<decltype(&CreateBlendState0010), PFND3D12DDI_CREATEBLENDSTATE_0010>);
    D3D12DDI_HDEVICE live{&live_device}, dead{};
    D3D12DDI_BLEND_DESC_0010 input{};
    input.AlphaToCoverageEnable = 1;
    input.IndependentBlendEnable = 1;
    for (unsigned i = 0; i < 8; ++i) {
        input.RenderTarget[i].SrcBlend = 100 + static_cast<int>(i);
        input.RenderTarget[i].RenderTargetWriteMask = static_cast<UINT8>(i + 1);
    }
    struct Guarded { std::uint64_t pre; D3D12DDI_BLEND_DESC state; std::uint64_t post; };
    Guarded output{0x1122334455667788ull, {}, 0x8877665544332211ull};
    D3D12DDI_HBLENDSTATE destination{&output.state};
    check(CalcPrivateBlendStateSize0010(live, &input) == sizeof(output.state), "ordinary size");
    CreateBlendState0010(live, &input, destination);
    check(creates == 1 && reports == 0, "ordinary create");
    check(output.pre == 0x1122334455667788ull && output.post == 0x8877665544332211ull,
          "destination canaries");
    for (unsigned i = 0; i < 8; ++i)
        check(output.state.RenderTarget[i].SrcBlend == static_cast<int>(100 + i) &&
              output.state.RenderTarget[i].RenderTargetWriteMask == i + 1, "eight target fields");
    const D3D12DDI_BLEND_DESC ordinary = output.state;
    input.LibraryReference = {{&live_library}, 0xffffffffu};
    check(CalcPrivateBlendStateSize0010(live, &input) == sizeof(output.state), "live library size");
    CreateBlendState0010(live, &input, destination);
    check(creates == 2 && std::memcmp(&ordinary, &output.state, sizeof(ordinary)) == 0,
          "library metadata not stored");
    int foreign;
    input.LibraryReference.hLibrary = {&foreign};
    const Guarded before = output;
    check(CalcPrivateBlendStateSize0010(live, &input) == 0, "foreign library size refused");
    CreateBlendState0010(live, &input, destination);
    check(creates == 2 && reports == 1 && std::memcmp(&before, &output, sizeof(output)) == 0,
          "foreign library create refused without write");
    input.LibraryReference = {{}, 0};
    check(CalcPrivateBlendStateSize0010(dead, &input) == 0, "dead device size refused");
    CreateBlendState0010(dead, &input, destination);
    check(creates == 2 && reports == 2, "dead device create refused");
    check(CalcPrivateBlendStateSize0010(live, nullptr) == 0, "null desc size refused");
    CreateBlendState0010(live, nullptr, destination);
    check(creates == 2 && reports == 3, "null desc create refused");
    CreateBlendState0010(live, &input, {});
    check(creates == 2 && reports == 4, "null destination refused");
    check(validations == 8, "validator called only for readable descriptions");
    std::printf("R1_BLEND_WRAPPERS checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
