/* CPU-only projection invariant; host structs model the pinned 0010 fields,
 * while native WDK compilation remains a separate ABI gate. */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

using BOOL = int;
using UINT8 = std::uint8_t;
constexpr unsigned D3D12DDI_SIMULTANEOUS_RENDER_TARGET_COUNT = 8;
struct D3D12DDI_RENDER_TARGET_BLEND_DESC {
    BOOL BlendEnable, LogicOpEnable;
    int SrcBlend, DestBlend, BlendOp, SrcBlendAlpha, DestBlendAlpha, BlendOpAlpha, LogicOp;
    UINT8 RenderTargetWriteMask;
};
struct D3D12DDI_BLEND_DESC {
    BOOL AlphaToCoverageEnable, IndependentBlendEnable;
    D3D12DDI_RENDER_TARGET_BLEND_DESC RenderTarget[8];
};
struct D3D12DDI_LIBRARY_REFERENCE_0010 { void *hLibrary; unsigned PipelineIndex; };
struct D3D12DDI_BLEND_DESC_0010 {
    BOOL AlphaToCoverageEnable, IndependentBlendEnable;
    D3D12DDI_RENDER_TARGET_BLEND_DESC RenderTarget[8];
    D3D12DDI_LIBRARY_REFERENCE_0010 LibraryReference;
};

#include "../drivers/apple-agx/d3d12/agx_d3d12_r1_blend_projection.h"

static unsigned checks, failures;
static void check(bool good, const char *name) {
    ++checks;
    if (!good) { ++failures; std::printf("R1_BLEND_FAIL %s\n", name); }
}
struct Guarded { std::uint64_t before; D3D12DDI_BLEND_DESC value; std::uint64_t after; };

int main() {
    D3D12DDI_BLEND_DESC_0010 input{};
    input.AlphaToCoverageEnable = 1;
    input.IndependentBlendEnable = 1;
    for (unsigned i = 0; i < 8; ++i) {
        auto &r = input.RenderTarget[i];
        r.BlendEnable = i & 1u; r.LogicOpEnable = !(i & 1u);
        r.SrcBlend = 100 + i; r.DestBlend = 200 + i; r.BlendOp = 300 + i;
        r.SrcBlendAlpha = 400 + i; r.DestBlendAlpha = 500 + i;
        r.BlendOpAlpha = 600 + i; r.LogicOp = 700 + i;
        r.RenderTargetWriteMask = static_cast<UINT8>(i + 1);
    }
    input.LibraryReference = {reinterpret_cast<void *>(0x1234), 0xffffffffu};
    Guarded result{0x1234567812345678ull, {}, 0x8765432187654321ull};
    check(AgxD3d12ProjectR1Blend(&input, &result.value), "valid projection");
    check(result.before == 0x1234567812345678ull && result.after == 0x8765432187654321ull,
          "destination canaries");
    check(result.value.AlphaToCoverageEnable == 1 && result.value.IndependentBlendEnable == 1,
          "top-level fields");
    for (unsigned i = 0; i < 8; ++i) {
        const auto &r = result.value.RenderTarget[i];
        check(r.BlendEnable == static_cast<int>(i & 1u) &&
              r.LogicOpEnable == static_cast<int>(!(i & 1u)) &&
              r.SrcBlend == static_cast<int>(100 + i) &&
              r.DestBlend == static_cast<int>(200 + i) &&
              r.BlendOp == static_cast<int>(300 + i) &&
              r.SrcBlendAlpha == static_cast<int>(400 + i) &&
              r.DestBlendAlpha == static_cast<int>(500 + i) &&
              r.BlendOpAlpha == static_cast<int>(600 + i) &&
              r.LogicOp == static_cast<int>(700 + i) &&
              r.RenderTargetWriteMask == i + 1, "all render-target fields");
    }
    const D3D12DDI_BLEND_DESC first = result.value;
    input.LibraryReference = {nullptr, 0};
    check(AgxD3d12ProjectR1Blend(&input, &result.value), "ordinary projection");
    check(std::memcmp(&first, &result.value, sizeof(first)) == 0,
          "library metadata excluded from stored state");
    const Guarded snapshot = result;
    check(!AgxD3d12ProjectR1Blend(nullptr, &result.value), "null source refused");
    check(std::memcmp(&snapshot, &result, sizeof(result)) == 0, "null source writes nothing");
    check(!AgxD3d12ProjectR1Blend(&input, nullptr), "null destination refused");
    std::printf("R1_BLEND_PROJECTION checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
