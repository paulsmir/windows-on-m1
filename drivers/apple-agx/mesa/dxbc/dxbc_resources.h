/* Immutable direct-slot shader resource dimension requirements.
 * SPDX-License-Identifier: MIT
 */
#ifndef DXBC_RESOURCES_H
#define DXBC_RESOURCES_H
#include <stdint.h>
#define DXBC_RESOURCE_SLOTS 128
/* OTHER deliberately preserves legacy non-cube view handling. */
enum dxbc_srv_dimension {
   DXBC_SRV_UNUSED = 0,
   DXBC_SRV_OTHER = 1,
   DXBC_SRV_CUBE = 2,
   DXBC_SRV_CUBE_ARRAY = 3,
   DXBC_SRV_3D = 4,
   DXBC_SRV_1D = 5,
};
#define DXBC_UAV_DIMENSION_SLOTS 8
enum dxbc_uav_dimension {
   DXBC_UAV_UNUSED = 0,
   DXBC_UAV_OTHER = 1,
   DXBC_UAV_1D = 2,
};
#endif
