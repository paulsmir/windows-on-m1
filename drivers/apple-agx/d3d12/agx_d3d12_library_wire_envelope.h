/* Original CPU-only D3D12 pipeline-library wire envelope. SPDX-License-Identifier: MIT
 * This checks only the outer arena and table extents. It is not an importer:
 * digests, entries, compatibility keys, gaps and recipe graphs remain unchecked.
 */
#ifndef AGX_D3D12_LIBRARY_WIRE_ENVELOPE_H
#define AGX_D3D12_LIBRARY_WIRE_ENVELOPE_H

#include <cstddef>
#include <cstdint>
#include <cstring>

constexpr std::size_t AgxD3d12LibraryWireBytes = 16u * 1024u * 1024u;
struct AgxD3d12LibraryEnvelope {
    std::uint64_t used = 0;
    std::uint64_t pipeline_table = 0;
    std::uint64_t component_table = 0;
    std::uint64_t payload_data = 0;
    std::uint32_t pipeline_count = 0;
    std::uint32_t component_count = 0;
};

inline std::uint32_t AgxD3d12WireU32(const std::uint8_t *p) {
    std::uint32_t v = 0;
    for (unsigned i = 0; i != 4; ++i) v |= std::uint32_t(p[i]) << (8u * i);
    return v;
}
inline std::uint64_t AgxD3d12WireU64(const std::uint8_t *p) {
    std::uint64_t v = 0;
    for (unsigned i = 0; i != 8; ++i) v |= std::uint64_t(p[i]) << (8u * i);
    return v;
}

inline bool AgxD3D12CheckLibraryEnvelope(const void *data, std::size_t bytes,
                                          AgxD3d12LibraryEnvelope *out) {
    if (out) *out = {};
    if (!data || !out || bytes != AgxD3d12LibraryWireBytes) return false;
    const auto *p = static_cast<const std::uint8_t *>(data);
    static constexpr char magic[8] = {'A','G','X','P','L','B','1',0};
    if (std::memcmp(p, magic, 8) != 0 || AgxD3d12WireU32(p + 8) != 1 ||
        AgxD3d12WireU32(p + 12) != 256 ||
        AgxD3d12WireU64(p + 16) != AgxD3d12LibraryWireBytes)
        return false;
    const std::uint64_t used = AgxD3d12WireU64(p + 24);
    const std::uint32_t pipelines = AgxD3d12WireU32(p + 32);
    const std::uint32_t components = AgxD3d12WireU32(p + 36);
    const std::uint64_t pipeline = AgxD3d12WireU64(p + 40);
    const std::uint64_t component = AgxD3d12WireU64(p + 48);
    const std::uint64_t payload = AgxD3d12WireU64(p + 56);
    if (used < 256 || used > bytes || pipelines > 1024 || components > 8192 ||
        (pipeline & 7u) || (component & 7u) || (payload & 7u) ||
        pipeline < 256 || pipeline > used)
        return false;
    /* Count limits precede multiplication. Subtraction avoids unsigned wrap
     * when a malformed offset is close to UINT64_MAX. */
    const std::uint64_t pipeline_bytes = std::uint64_t(pipelines) * 64u;
    if (pipeline_bytes > used - pipeline || component < pipeline + pipeline_bytes ||
        component > used)
        return false;
    const std::uint64_t component_bytes = std::uint64_t(components) * 64u;
    if (component_bytes > used - component || payload < component + component_bytes ||
        payload > used)
        return false;
    for (std::size_t i = 176; i != 256; ++i)
        if (p[i] != 0) return false;
    *out = {used, pipeline, component, payload, pipelines, components};
    return true;
}

#endif
