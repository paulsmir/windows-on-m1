/* Original internal CPU-only R1 shader lease/copy transaction. SPDX-License-Identifier: MIT
 * No DDI/HRESULT mapping or runtime error callback. Caller proves source spans
 * readable and stable through this synchronous call. */
#ifndef AGX_D3D12_R1_SHADER_TRANSACTION_H
#define AGX_D3D12_R1_SHADER_TRANSACTION_H

#include "agx_shader_input_provider.h"

inline AgxCpuProviderResult AgxD3d12R1CopyShaderTransaction(
    AgxCpuProvider *provider, AgxCpuDevice device, AgxCpuRoot root,
    const D3D12DDIARG_CREATE_SHADER_0010 *args, uint32_t stage,
    AgxShaderTrustedSpan program, AgxShaderTrustedSpan input_signature,
    AgxShaderTrustedSpan output_signature, void *storage, SIZE_T capacity,
    AgxShaderOwnedLayout *layout, AgxShaderOwned **owned) {
    if (layout) *layout = {};
    if (owned) *owned = nullptr;
    if (!layout || !owned)
        return {AgxCpuProviderStatus::Invalid, AgxShaderStorageStatus::Invalid, false};
    AgxCpuInputLease lease{};
    auto result = AgxCpuAcquireInput(provider, device, root, args, stage,
                                     program, input_signature, output_signature, &lease);
    if (result.status != AgxCpuProviderStatus::Ok) return result;
    /* Each provider wrapper owns a distinct one-use pinned operation. */
    result = AgxCpuCalculate(provider, lease, layout);
    if (result.status == AgxCpuProviderStatus::Ok)
        result = AgxCpuCopyOwned(provider, lease, storage, capacity, owned);
    const auto released = AgxCpuReleaseInput(provider, lease);
    if (released != AgxCpuProviderStatus::Ok) {
        *layout = {};
        *owned = nullptr;
        return {released, result.core_status, result.core_called};
    }
    if (result.status != AgxCpuProviderStatus::Ok) {
        *layout = {};
        *owned = nullptr;
    }
    return result;
}

#endif
