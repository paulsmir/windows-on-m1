/* Original CPU-only D3D12 library digest and compatibility gate. SPDX-License-Identifier: MIT
 * Caller supplies an actual SHA-256 backend. This is not a recipe importer,
 * ownership transaction, runtime-framing validator or registered DDI callback.
 */
#ifndef AGX_D3D12_LIBRARY_WIRE_DIGEST_H
#define AGX_D3D12_LIBRARY_WIRE_DIGEST_H
#include "agx_d3d12_library_wire_tables.h"

struct AgxD3d12WireSpan {
    const std::uint8_t *data;
    std::size_t bytes;
};
using AgxD3d12WireDigestCallback = bool (*)(const AgxD3d12WireSpan *,
    std::size_t, std::uint8_t[32], void *);

inline bool AgxD3D12CheckLibraryDigests(const void *data, std::size_t bytes,
    AgxD3d12LibraryInterval *scratch, std::size_t scratch_count,
    AgxD3d12WireDigestCallback sha256, void *sha_context,
    const std::uint8_t compiler_key[32], const std::uint8_t adapter_key[32],
    std::uint64_t ddi_version, std::uint32_t shader_model,
    std::uint32_t semantic_profile, AgxD3d12LibraryEnvelope *out) {
    if (out) *out = {};
    if (!sha256 || !compiler_key || !adapter_key || !out) return false;
    AgxD3d12LibraryEnvelope e{};
    if (!AgxD3D12CheckLibraryTables(data, bytes, scratch, scratch_count, &e))
        return false;
    const auto *p = static_cast<const std::uint8_t *>(data);
    if (std::memcmp(p + 64, compiler_key, 32) != 0 ||
        std::memcmp(p + 96, adapter_key, 32) != 0 ||
        AgxD3d12WireU64(p + 160) != ddi_version ||
        AgxD3d12WireU32(p + 168) != shader_model ||
        AgxD3d12WireU32(p + 172) != semantic_profile)
        return false;
    std::uint8_t digest[32]{};
    for (std::uint32_t i = 0; i < e.component_count; ++i) {
        const auto *record = p + e.component_table + std::uint64_t(i) * 64u;
        const std::uint64_t begin = AgxD3d12WireU64(record + 8);
        const std::uint64_t length = AgxD3d12WireU64(record + 16);
        const AgxD3d12WireSpan span{p + begin, static_cast<std::size_t>(length)};
        if (!sha256(&span, 1, digest, sha_context) ||
            std::memcmp(digest, record + 24, 32) != 0) return false;
    }
    for (std::uint32_t i = 0; i < e.pipeline_count; ++i) {
        const auto *record = p + e.pipeline_table + std::uint64_t(i) * 64u;
        const std::uint32_t component = AgxD3d12WireU32(record + 4);
        const auto *recipe = p + e.component_table + std::uint64_t(component) * 64u;
        if (std::memcmp(record + 8, recipe + 24, 32) != 0) return false;
    }
    static constexpr std::uint8_t zero_digest_field[32]{};
    const AgxD3d12WireSpan arena[3] = {
        {p, 128}, {zero_digest_field, 32}, {p + 160, bytes - 160}
    };
    if (!sha256(arena, 3, digest, sha_context) ||
        std::memcmp(digest, p + 128, 32) != 0) return false;
    *out = e;
    return true;
}
#endif
