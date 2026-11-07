/* Original CPU-only R1 blend projection. SPDX-License-Identifier: MIT
 * Include the pinned d3d12umddi.h before this header. This is not a DDI
 * callback and does not validate or retain LibraryReference. */
#ifndef AGX_D3D12_R1_BLEND_PROJECTION_H
#define AGX_D3D12_R1_BLEND_PROJECTION_H

#include <cstring>

inline bool AgxD3d12ProjectR1Blend(const D3D12DDI_BLEND_DESC_0010 *input,
                                   D3D12DDI_BLEND_DESC *output) {
    if (!input || !output) return false;
    D3D12DDI_BLEND_DESC projected;
    std::memset(&projected, 0, sizeof(projected));
    projected.AlphaToCoverageEnable = input->AlphaToCoverageEnable;
    projected.IndependentBlendEnable = input->IndependentBlendEnable;
    for (unsigned i = 0; i < D3D12DDI_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
        const D3D12DDI_RENDER_TARGET_BLEND_DESC &source = input->RenderTarget[i];
        D3D12DDI_RENDER_TARGET_BLEND_DESC &target = projected.RenderTarget[i];
        target.BlendEnable = source.BlendEnable;
        target.LogicOpEnable = source.LogicOpEnable;
        target.SrcBlend = source.SrcBlend;
        target.DestBlend = source.DestBlend;
        target.BlendOp = source.BlendOp;
        target.SrcBlendAlpha = source.SrcBlendAlpha;
        target.DestBlendAlpha = source.DestBlendAlpha;
        target.BlendOpAlpha = source.BlendOpAlpha;
        target.LogicOp = source.LogicOp;
        target.RenderTargetWriteMask = source.RenderTargetWriteMask;
    }
    *output = projected;
    return true;
}

#endif
