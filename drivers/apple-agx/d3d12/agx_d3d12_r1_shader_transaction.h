/* Original internal CPU-only R1 shader lease/copy transaction. SPDX-License-Identifier: MIT
 * No DDI/HRESULT mapping or runtime error callback. Caller proves source spans
 * readable and stable through this synchronous call. */
#ifndef AGX_D3D12_R1_SHADER_TRANSACTION_H
#define AGX_D3D12_R1_SHADER_TRANSACTION_H

#include "agx_shader_input_provider.h"
#include "agx_d3d12_r1_shader_span.h"
#include "agx_d3d12_r1_io_span.h"

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

/* Caller proves the args record, first two PROGRAM words, Standard IO record,
 * and all declared bodies readable and stable for this synchronous call.
 * No runtime callback or DDI error mapping occurs here. */
inline AgxCpuProviderResult AgxD3D12R1CopyStandardShaderPreflight(
    AgxCpuProvider *provider, AgxCpuDevice device, AgxCpuRoot root,
    const D3D12DDIARG_CREATE_SHADER_0010 *args, uint32_t stage,
    SIZE_T max_program_bytes, SIZE_T max_io_entries,
    void *storage, SIZE_T capacity, AgxShaderOwnedLayout *layout,
    AgxShaderOwned **owned) {
    if (layout) *layout = {};
    if (owned) *owned = nullptr;
    if (!layout || !owned || !args)
        return {AgxCpuProviderStatus::Invalid, AgxShaderStorageStatus::Invalid, false};
    AgxD3d12ProgramSpan program{};
    AgxD3d12R1IoSpans io{};
    if (!AgxD3d12AcquireR1ProgramSpan(args->pShaderCode, max_program_bytes, &program) ||
        !AgxD3d12AcquireR1IoSpans(args, max_io_entries, &io))
        return {AgxCpuProviderStatus::Invalid, AgxShaderStorageStatus::Invalid, false};
    return AgxD3d12R1CopyShaderTransaction(
        provider, device, root, args, stage,
        {program.data, program.bytes}, io.input, io.output,
        storage, capacity, layout, owned);
}

#endif
