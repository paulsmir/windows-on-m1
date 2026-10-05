/* Host resource shell for the real Mesa serialization functions. Wire layout
 * mirrors the documented Asahi UAPI; no serializer is reimplemented here. */
#include <stdint.h>
#define PIPE_MAX_COLOR_BUFS 8
#define DRM_ASAHI_SET_FRAGMENT_ATTACHMENTS 3u
#define DRM_ASAHI_BARRIER_NONE 0xffffu
struct drm_asahi_attachment { uint64_t pointer,size; uint32_t pad,flags; };
struct drm_asahi_cmd_header { uint16_t cmd_type,size,vdm_barrier,cdm_barrier; };
struct agx_resource {
  void *bo;
  uint64_t va;
  struct { uint64_t size_B,level_offsets_B[1]; } layout;
  struct agx_resource *separate_stencil;
};
struct agx_batch {
  struct {
    unsigned nr_cbufs;
    struct { struct agx_resource *texture; } cbufs[PIPE_MAX_COLOR_BUFS],zsbuf;
  } key;
};
static struct agx_resource *agx_resource(struct agx_resource *r) { return r; }
static uint64_t agx_map_gpu(struct agx_resource *r) { return r->va; }
typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V3 Header;
  unsigned char Native[APPLE_AGX_G4_NATIVE_MAX_BYTES];
} AGX_G4_PRIVATE;
_Static_assert(sizeof(struct drm_asahi_attachment)==sizeof(APPLE_AGX_G4_ATTACHMENT),"attachment ABI");
_Static_assert(sizeof(struct drm_asahi_cmd_header)==sizeof(APPLE_AGX_G4_NATIVE_HEADER),"command ABI");
