/* Native Asahi resource-layout query; no backing BO is allocated.
 * SPDX-License-Identifier: MIT
 */
#ifndef AGX_WIN32_ASAHI_LAYOUT_H
#define AGX_WIN32_ASAHI_LAYOUT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define AGX_WIN32_ASAHI_LAYOUT_LEVELS 16
#define AGX_WIN32_ASAHI_LAYOUT_MIPMAPPED_Z 1u
#define AGX_WIN32_ASAHI_LAYOUT_COMPRESSED 2u
#define AGX_WIN32_ASAHI_LAYOUT_PAGE_ALIGNED_LAYERS 4u
#define AGX_WIN32_ASAHI_LAYOUT_WRITEABLE_IMAGE 8u
#define AGX_WIN32_ASAHI_LAYOUT_RENDERABLE 16u
struct pipe_screen;
struct pipe_resource;
typedef struct {
   uint64_t SizeBytes, Modifier, LayerStrideBytes, MetadataOffsetBytes;
   uint64_t CompressionLayerStrideBytes, SparseTableSizeBytes;
   uint64_t LevelOffsets[AGX_WIN32_ASAHI_LAYOUT_LEVELS];
   uint64_t CompressedLevelOffsets[AGX_WIN32_ASAHI_LAYOUT_LEVELS];
   uint32_t RequestedLevels, AllocatedLevels, Tiling, Flags, EffectiveBind;
} AGX_WIN32_ASAHI_RESOURCE_LAYOUT;
/* The template must satisfy the native constructor's format/dimension
 * preconditions.  This is a layout query, not resource/capability admission.
 * SizeBytes is the exact AIL extent passed to agx_bo_create, before backend
 * BO rounding; it is not a D3D12 heap size/alignment.
 * Returns 0 and clears result for invalid pointers/modifier selection or the
 * native >=4GiB rejection.  No pointer in result borrows template/resource. */
int AgxWin32AsahiQueryResourceLayout(struct pipe_screen *screen,
   const struct pipe_resource *templ, const uint64_t *modifiers, int count,
   AGX_WIN32_ASAHI_RESOURCE_LAYOUT *result);
#ifdef __cplusplus
}
#endif
#endif
