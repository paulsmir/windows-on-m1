/* Original CPU-only checked root-private extent. SPDX-License-Identifier: MIT
 * Include the pinned WDK and current DDI private root types first. The WDK
 * root record and arrays remain caller-trusted readable inputs. */
#ifndef AGX_D3D12_R1_ROOT_LAYOUT_H
#define AGX_D3D12_R1_ROOT_LAYOUT_H

#include <cstddef>
#include <limits>

struct AgxR1RootLayoutPolicy {
    UINT max_parameters;
    UINT max_ranges;
    UINT max_static_samplers;
    UINT max_dwords;
};

inline bool AgxD3d12MeasureR1RootPrivate(const D3D12DDI_ROOT_SIGNATURE *root,
    AgxR1RootLayoutPolicy policy, SIZE_T *bytes, UINT *range_count) {
    if (bytes) *bytes = 0;
    if (range_count) *range_count = 0;
    if (!bytes || !range_count || !root ||
        root->NumParameters > policy.max_parameters ||
        root->NumStaticSamplers > policy.max_static_samplers ||
        (root->NumParameters && !root->pRootParameters) ||
        (root->NumStaticSamplers && !root->pStaticSamplers))
        return false;
    UINT ranges = 0, dwords = 0;
    for (UINT i = 0; i < root->NumParameters; ++i) {
        const auto &parameter = root->pRootParameters[i];
        UINT cost = 0;
        switch (parameter.ParameterType) {
        case D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
            const UINT count = parameter.DescriptorTable.NumDescriptorRanges;
            if ((count && !parameter.DescriptorTable.pDescriptorRanges) ||
                count > policy.max_ranges - ranges)
                return false;
            UINT next = 0;
            for (UINT r = 0; r < count; ++r) {
                const auto &range = parameter.DescriptorTable.pDescriptorRanges[r];
                const UINT offset = range.OffsetInDescriptorsFromTableStart ==
                    static_cast<UINT>(D3D12DDI_DESCRIPTOR_RANGE_OFFSET_APPEND)
                    ? next : range.OffsetInDescriptorsFromTableStart;
                if ((next == UINT_MAX && range.OffsetInDescriptorsFromTableStart ==
                     static_cast<UINT>(D3D12DDI_DESCRIPTOR_RANGE_OFFSET_APPEND)) ||
                    (range.NumDescriptors != UINT_MAX &&
                     range.NumDescriptors > UINT_MAX - offset))
                    return false;
                next = range.NumDescriptors == UINT_MAX ? UINT_MAX : offset + range.NumDescriptors;
            }
            ranges += count;
            cost = 1;
            break;
        }
        case D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            cost = parameter.Constants.Num32BitValues;
            break;
        case D3D12DDI_ROOT_PARAMETER_TYPE_CBV:
        case D3D12DDI_ROOT_PARAMETER_TYPE_SRV:
        case D3D12DDI_ROOT_PARAMETER_TYPE_UAV:
            cost = 2;
            break;
        default:
            return false;
        }
        if (cost > policy.max_dwords - dwords) return false;
        dwords += cost;
    }
    SIZE_T total = offsetof(AgxD3d12RootSignature, parameter);
    const auto add = [&total](SIZE_T count, SIZE_T stride) {
        const SIZE_T max = (std::numeric_limits<SIZE_T>::max)();
        if (stride && count > (max - total) / stride) return false;
        total += count * stride;
        return true;
    };
    if (!add(root->NumParameters ? root->NumParameters : 1, sizeof(AgxD3d12RootParameter)) ||
        !add(ranges, sizeof(AgxD3d12Range)) ||
        !add(root->NumStaticSamplers, sizeof(D3D12DDI_STATIC_SAMPLER)))
        return false;
    *bytes = total;
    *range_count = ranges;
    return true;
}

#endif
