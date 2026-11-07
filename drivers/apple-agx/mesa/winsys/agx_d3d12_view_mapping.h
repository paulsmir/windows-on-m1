/* D3D12 SRV component-map encoding (WDK26100 d3d12umddi.h).
 * SPDX-License-Identifier: MIT
 */
#ifndef AGX_D3D12_VIEW_MAPPING_H
#define AGX_D3D12_VIEW_MAPPING_H
#include <stdint.h>

#define AGX_D3D12_COMPONENT_MAPPING_IDENTITY UINT32_C(0x1688)

static inline int
AgxD3d12ComponentMappingValid(uint32_t mapping)
{
  if ((mapping & UINT32_C(0xffffe000)) || !(mapping & UINT32_C(0x1000)))
    return 0;
  for (unsigned c = 0; c < 4; ++c)
    if (((mapping >> (3 * c)) & 7u) > 5u)
      return 0;
  return 1;
}
#endif
