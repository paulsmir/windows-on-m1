/* Original CPU-only typed R1 PSO scalar snapshot. SPDX-License-Identifier: MIT
 * Include pinned d3d12umddi.h first. This is not an owned recipe or DDI.
 */
#ifndef AGX_D3D12_R1_PSO_SCALAR_CANONICAL_H
#define AGX_D3D12_R1_PSO_SCALAR_CANONICAL_H
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

inline void AgxD3d12PsoPut32(unsigned char *p, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(v >> (8u * i));
}
inline bool AgxD3D12CanonicalPsoScalars0010(
    const D3D12DDIARG_CREATE_PIPELINE_STATE_0010 *arg,
    unsigned char *output, std::size_t capacity, std::size_t *written) {
    if (!written) return false;
    *written = 0;
    constexpr std::size_t bytes = 80;
    if (!arg || !output || capacity < bytes || arg->NumRenderTargets > 8 ||
        arg->LibraryReference.hLibrary.pDrvPrivate) return false;
    const auto src = reinterpret_cast<std::uintptr_t>(arg);
    const auto dst = reinterpret_cast<std::uintptr_t>(output);
    const auto max = (std::numeric_limits<std::uintptr_t>::max)();
    if (sizeof(*arg) > max - src || capacity > max - dst ||
        (src < dst + capacity && dst < src + sizeof(*arg))) return false;
    const void *handles[] = {
        arg->hComputeShader.pDrvPrivate, arg->hVertexShader.pDrvPrivate,
        arg->hPixelShader.pDrvPrivate, arg->hDomainShader.pDrvPrivate,
        arg->hHullShader.pDrvPrivate, arg->hGeometryShader.pDrvPrivate,
        arg->hRootSignature.pDrvPrivate, arg->hBlendState.pDrvPrivate,
        arg->hRasterizerState.pDrvPrivate, arg->hDepthStencilState.pDrvPrivate,
        arg->hElementLayout.pDrvPrivate
    };
    std::uint32_t presence = 0;
    for (unsigned i = 0; i < 11; ++i)
        if (handles[i]) presence |= std::uint32_t(1) << i;
    std::memset(output, 0, bytes);
    std::memcpy(output, "AGXPSO1", 7);
    AgxD3d12PsoPut32(output + 8, 1);
    AgxD3d12PsoPut32(output + 12, presence);
    AgxD3d12PsoPut32(output + 16, arg->SampleMask);
    AgxD3d12PsoPut32(output + 20, static_cast<std::uint32_t>(arg->IBStripCutValue));
    AgxD3d12PsoPut32(output + 24, static_cast<std::uint32_t>(arg->PrimitiveTopologyType));
    AgxD3d12PsoPut32(output + 28, arg->NumRenderTargets);
    for (unsigned i = 0; i < arg->NumRenderTargets; ++i)
        AgxD3d12PsoPut32(output + 32 + i * 4,
                         static_cast<std::uint32_t>(arg->RTVFormats[i]));
    AgxD3d12PsoPut32(output + 64, static_cast<std::uint32_t>(arg->DSVFormat));
    AgxD3d12PsoPut32(output + 68, arg->SampleDesc.Count);
    AgxD3d12PsoPut32(output + 72, arg->SampleDesc.Quality);
    AgxD3d12PsoPut32(output + 76, arg->NodeMask);
    *written = bytes;
    return true;
}
#endif
