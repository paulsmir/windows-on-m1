/* Original CPU-only D3D12 pipeline-library table gate. SPDX-License-Identifier: MIT
 * Structural checks only: no digest/compatibility or recipe-graph validation.
 * The caller owns the scratch array; this function never allocates or publishes.
 */
#ifndef AGX_D3D12_LIBRARY_WIRE_TABLES_H
#define AGX_D3D12_LIBRARY_WIRE_TABLES_H

#include "agx_d3d12_library_wire_envelope.h"
#include <algorithm>

struct AgxD3d12LibraryInterval {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
};
inline bool AgxD3d12LibraryZero(const std::uint8_t *p, std::uint64_t begin,
                                 std::uint64_t end) {
    for (std::uint64_t i = begin; i < end; ++i)
        if (p[i]) return false;
    return true;
}
inline bool AgxD3D12CheckLibraryTables(const void *data, std::size_t bytes,
    AgxD3d12LibraryInterval *scratch, std::size_t scratch_count,
    AgxD3d12LibraryEnvelope *out) {
    if (out) *out = {};
    AgxD3d12LibraryEnvelope e{};
    if (!AgxD3D12CheckLibraryEnvelope(data, bytes, &e) || !out ||
        (e.component_count && (!scratch || scratch_count < e.component_count)))
        return false;
    const auto *p = static_cast<const std::uint8_t *>(data);
    const std::uint64_t pipeline_end = e.pipeline_table + std::uint64_t(e.pipeline_count) * 64u;
    const std::uint64_t component_end = e.component_table + std::uint64_t(e.component_count) * 64u;
    if (!AgxD3d12LibraryZero(p, 256, e.pipeline_table) ||
        !AgxD3d12LibraryZero(p, pipeline_end, e.component_table) ||
        !AgxD3d12LibraryZero(p, component_end, e.payload_data))
        return false;
    std::uint32_t previous_index = 0;
    for (std::uint32_t i = 0; i < e.pipeline_count; ++i) {
        const auto *r = p + e.pipeline_table + std::uint64_t(i) * 64u;
        const std::uint32_t index = AgxD3d12WireU32(r);
        const std::uint32_t component = AgxD3d12WireU32(r + 4);
        if ((i && index <= previous_index) || component >= e.component_count ||
            !AgxD3d12LibraryZero(r, 40, 64)) return false;
        previous_index = index;
    }
    for (std::uint32_t i = 0; i < e.component_count; ++i) {
        const auto *r = p + e.component_table + std::uint64_t(i) * 64u;
        const std::uint32_t kind = AgxD3d12WireU32(r);
        const std::uint32_t schema = AgxD3d12WireU32(r + 4);
        const std::uint64_t begin = AgxD3d12WireU64(r + 8);
        const std::uint64_t length = AgxD3d12WireU64(r + 16);
        if (!((kind >= 1 && kind <= 8) || kind == 0x100) || schema != 1 ||
            (begin & 7u) || begin < e.payload_data || begin > e.used ||
            !length || length > e.used - begin ||
            !AgxD3d12LibraryZero(r, 56, 64)) return false;
        scratch[i] = {begin, begin + length};
        if (i) {
            const auto *prior = r - 64;
            const std::uint32_t prior_kind = AgxD3d12WireU32(prior);
            const std::uint32_t prior_schema = AgxD3d12WireU32(prior + 4);
            const int digest_order = std::memcmp(prior + 24, r + 24, 32);
            const std::uint64_t prior_length = AgxD3d12WireU64(prior + 16);
            const std::uint64_t prior_begin = AgxD3d12WireU64(prior + 8);
            if (kind < prior_kind ||
                (kind == prior_kind && schema < prior_schema) ||
                (kind == prior_kind && schema == prior_schema && digest_order > 0) ||
                (kind == prior_kind && schema == prior_schema && digest_order == 0 && length < prior_length) ||
                (kind == prior_kind && schema == prior_schema && digest_order == 0 && length == prior_length &&
                 std::memcmp(p + prior_begin, p + begin, static_cast<std::size_t>(length)) >= 0))
                return false;
        }
    }
    for (std::uint32_t i = 0; i < e.pipeline_count; ++i) {
        const auto *r = p + e.pipeline_table + std::uint64_t(i) * 64u;
        const std::uint32_t component = AgxD3d12WireU32(r + 4);
        const auto *target = p + e.component_table + std::uint64_t(component) * 64u;
        if (AgxD3d12WireU32(target) != 0x100) return false;
    }
    if (e.component_count) std::sort(scratch, scratch + e.component_count,
        [](const AgxD3d12LibraryInterval &a, const AgxD3d12LibraryInterval &b) {
            return a.begin < b.begin;
        });
    std::uint64_t next = e.payload_data;
    for (std::uint32_t i = 0; i < e.component_count; ++i) {
        if (scratch[i].begin < next ||
            !AgxD3d12LibraryZero(p, next, scratch[i].begin)) return false;
        next = scratch[i].end;
    }
    if (!AgxD3d12LibraryZero(p, next, bytes)) return false;
    *out = e;
    return true;
}
#endif
