/* Original CPU-only canonical root snapshot. SPDX-License-Identifier: MIT
 * Include the current DDI private root types and pinned WDK first. The caller
 * supplies the actual readable private allocation extent. */
#ifndef AGX_D3D12_R1_ROOT_CANONICAL_H
#define AGX_D3D12_R1_ROOT_CANONICAL_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace agx_r1_root {
inline bool add(SIZE_T &total, SIZE_T count, SIZE_T stride) {
    if (stride && count > ((std::numeric_limits<SIZE_T>::max)() - total) / stride)
        return false;
    total += count * stride;
    return true;
}
inline void put32(unsigned char *out, std::uint32_t value) {
    out[0] = static_cast<unsigned char>(value);
    out[1] = static_cast<unsigned char>(value >> 8);
    out[2] = static_cast<unsigned char>(value >> 16);
    out[3] = static_cast<unsigned char>(value >> 24);
}
inline std::uint32_t bits(float value) {
    static_assert(sizeof(value) == sizeof(std::uint32_t));
    std::uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
inline bool overlaps(const void *a, SIZE_T asize, const void *b, SIZE_T bsize) {
    const auto x = reinterpret_cast<std::uintptr_t>(a);
    const auto y = reinterpret_cast<std::uintptr_t>(b);
    const auto max = (std::numeric_limits<std::uintptr_t>::max)();
    if (asize > max - x || bsize > max - y) return true;
    return x < y + bsize && y < x + asize;
}
}

inline bool AgxD3d12MeasureRootCanonical(const AgxD3d12RootSignature *root,
                                         SIZE_T root_bytes, SIZE_T *required) {
    if (!required) return false;
    *required = 0;
    constexpr SIZE_T base = offsetof(AgxD3d12RootSignature, parameter);
    if (!root || root_bytes < base) return false;
    static_assert(sizeof(UINT) == 4);
    static_assert(alignof(AgxD3d12RootParameter) <= alignof(UINT));
    static_assert(alignof(AgxD3d12Range) <= alignof(UINT));
    static_assert(alignof(D3D12DDI_STATIC_SAMPLER) <= alignof(UINT));
    const SIZE_T params = root->parameters;
    const SIZE_T ranges = root->range_count;
    const SIZE_T samplers = root->static_samplers;
    SIZE_T input_bytes = base;
    if (!agx_r1_root::add(input_bytes, params ? params : 1, sizeof(AgxD3d12RootParameter)) ||
        !agx_r1_root::add(input_bytes, ranges, sizeof(AgxD3d12Range)) ||
        !agx_r1_root::add(input_bytes, samplers, sizeof(D3D12DDI_STATIC_SAMPLER)) ||
        input_bytes > root_bytes)
        return false;
    for (SIZE_T i = 0; i < params; ++i) {
        const auto &p = root->parameter[i];
        if (p.first_range > ranges || p.ranges > ranges - p.first_range) return false;
    }
    SIZE_T wire = 32;
    if (!agx_r1_root::add(wire, params, 32) ||
        !agx_r1_root::add(wire, ranges, 20) ||
        !agx_r1_root::add(wire, samplers, 52))
        return false;
    *required = wire;
    return true;
}

inline bool AgxD3d12EncodeRootCanonical(const AgxD3d12RootSignature *root,
    SIZE_T root_bytes, unsigned char *output, SIZE_T capacity, SIZE_T *written) {
    if (!written) return false;
    *written = 0;
    SIZE_T needed = 0;
    if (!AgxD3d12MeasureRootCanonical(root, root_bytes, &needed) ||
        !output || capacity < needed ||
        agx_r1_root::overlaps(root, root_bytes, output, capacity))
        return false;
    std::memset(output, 0, needed);
    std::memcpy(output, "AGXR1RT1", 8);
    unsigned char *slot = output + 8;
    const auto put = [&slot](std::uint32_t value) {
        agx_r1_root::put32(slot, value); slot += 4;
    };
    put(1); put(root->parameters); put(root->static_samplers);
    put(root->flags); put(root->dwords); put(root->range_count);
    for (UINT i = 0; i < root->parameters; ++i) {
        const auto &p = root->parameter[i];
        put(p.type); put(p.visibility); put(p.offset); put(p.dwords);
        put(p.shader_register); put(p.space); put(p.first_range); put(p.ranges);
    }
    const auto *base = reinterpret_cast<const unsigned char *>(root);
    const SIZE_T range_offset = offsetof(AgxD3d12RootSignature, parameter) +
        static_cast<SIZE_T>(root->parameters ? root->parameters : 1) * sizeof(AgxD3d12RootParameter);
    const auto *ranges = reinterpret_cast<const AgxD3d12Range *>(base + range_offset);
    for (UINT i = 0; i < root->range_count; ++i) {
        const auto &r = ranges[i];
        put(r.type); put(r.count); put(r.base_register); put(r.space); put(r.offset);
    }
    const auto *samplers = reinterpret_cast<const D3D12DDI_STATIC_SAMPLER *>(ranges + root->range_count);
    for (UINT i = 0; i < root->static_samplers; ++i) {
        const auto &s = samplers[i];
        put(static_cast<UINT>(s.Filter));
        put(static_cast<UINT>(s.AddressU)); put(static_cast<UINT>(s.AddressV));
        put(static_cast<UINT>(s.AddressW)); put(agx_r1_root::bits(s.MipLODBias));
        put(s.MaxAnisotropy); put(static_cast<UINT>(s.ComparisonFunc));
        put(static_cast<UINT>(s.BorderColor));
        put(agx_r1_root::bits(s.MinLOD)); put(agx_r1_root::bits(s.MaxLOD));
        put(s.ShaderRegister); put(s.RegisterSpace);
        put(static_cast<UINT>(s.ShaderVisibility));
    }
    *written = needed;
    return true;
}

#endif
