/* Original CPU-only R1 Standard IO span preflight. SPDX-License-Identifier: MIT
 * Include pinned WDK types and AgxShaderTrustedSpan before this header.
 * Caller owns readable/stable descriptor and signature array lifetimes. */
#ifndef AGX_D3D12_R1_IO_SPAN_H
#define AGX_D3D12_R1_IO_SPAN_H

#include <cstddef>
#include <limits>

struct AgxD3d12R1IoSpans {
    AgxShaderTrustedSpan input;
    AgxShaderTrustedSpan output;
};

inline bool AgxD3d12AcquireR1IoSpans(const D3D12DDIARG_CREATE_SHADER_0010 *args,
                                     SIZE_T max_entries, AgxD3d12R1IoSpans *spans) {
    if (!spans) return false;
    *spans = {};
    if (!args) return false;
    const auto *io = args->IOSignatures.Standard;
    if (!io) return true;
    const SIZE_T input_count = io->NumInputSignatureEntries;
    const SIZE_T output_count = io->NumOutputSignatureEntries;
    const auto *input = io->pInputSignatureDeprecated;
    const auto *output = io->pOutputSignatureDeprecated;
    const SIZE_T max = (std::numeric_limits<SIZE_T>::max)();
    if (input_count > max_entries || output_count > max_entries ||
        (input_count && !input) || (output_count && !output) ||
        input_count > max / sizeof(*input) || output_count > max / sizeof(*output))
        return false;
    spans->input = {input, input_count * sizeof(*input)};
    spans->output = {output, output_count * sizeof(*output)};
    return true;
}

#endif
