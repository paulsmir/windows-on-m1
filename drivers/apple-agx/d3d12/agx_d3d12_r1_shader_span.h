/* Original CPU-only early-R1 PROGRAM span gate. SPDX-License-Identifier: MIT
 * The caller must establish that the first eight bytes are readable. Success
 * reports the self-declared extent; it does not prove the body is readable. */
#ifndef AGX_D3D12_R1_SHADER_SPAN_H
#define AGX_D3D12_R1_SHADER_SPAN_H

#include <cstring>

static_assert(sizeof(UINT) == 4, "D3D12 shader PROGRAM words are 32-bit");
struct AgxD3d12ProgramSpan {
    const UINT *data;
    SIZE_T bytes;
    UINT words;
};

inline bool AgxD3d12AcquireR1ProgramSpan(const UINT *code, SIZE_T max_bytes,
                                         AgxD3d12ProgramSpan *output) {
    if (!output) return false;
    *output = {};
    if (!code || max_bytes < 2 * sizeof(UINT)) return false;
    UINT words = 0;
    const auto *bytes = static_cast<const unsigned char *>(static_cast<const void *>(code));
    std::memcpy(&words, bytes + sizeof(UINT), sizeof(words));
    if (words < 2 || static_cast<SIZE_T>(words) > max_bytes / sizeof(UINT)) return false;
    *output = {code, static_cast<SIZE_T>(words) * sizeof(UINT), words};
    return true;
}

#endif
