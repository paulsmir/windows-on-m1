#ifndef AGX_WIN32_ASAHI_SCENE_H
#define AGX_WIN32_ASAHI_SCENE_H

#include "agx_win32_asahi_batch.h"

struct pipe_screen;
struct pipe_context;
struct pipe_resource;
struct pipe_stream_output_target;
struct pipe_query;

typedef enum {
  AgxAsahiSceneEmpty, AgxAsahiScenePreparing, AgxAsahiScenePrepared,
  AgxAsahiSceneDrawn, AgxAsahiSceneSubmitted, AgxAsahiSceneRetired,
  AgxAsahiSceneRejected, AgxAsahiSceneReleased
} AGX_WIN32_ASAHI_SCENE_PHASE;

/* The byte layout of a linear Windows allocation. Keep this distinct from
 * Windows' A8R8G8B8/A8B8G8R8 names so the import owns the matching Gallium
 * and AIL format rather than relabelling the allocation. */
typedef enum {
  AgxWin32AsahiLinearFormatBgra8Unorm,
  AgxWin32AsahiLinearFormatRgba8Unorm
} AGX_WIN32_ASAHI_LINEAR_FORMAT;

typedef struct {
  AGX_WIN32_RELOC_ALLOCATION TargetIdentity;
  APPLE_AGX_U64 TargetConstructionAddress, TargetBytes, TargetLayerStride;
  APPLE_AGX_U32 Width, Height, Tiling, Format, BootGeneration, PageBytes;
  /* Canonical BGRA8 words (B in low byte), packed by Mesa from the same
   * clear/fragment inputs. Pixel verification may allow at most 1 LSB per
   * channel for hardware float quantization; these are not KMD expectations. */
  APPLE_AGX_U32 ExpectedBackgroundBgra8, ExpectedForegroundBgra8;
  APPLE_AGX_U64 Request, EncoderBytes;
  APPLE_AGX_U32 CommandVersion, References, Relocations;
  APPLE_AGX_WIN32_NATIVE_BATCH_METADATA NativeRoots;
} AGX_WIN32_ASAHI_SCENE_RECEIPT;

/* Caller-owned zeroed storage, at a stable address until Cleanup succeeds.
 * The caller owns Screen/Context and lends the context exclusively to this
 * scene. No other draws/state mutation or caller teardown while it is live.
 * Target remains owned by the scene after Retire, for caller output collection.
 * Receipt is a value snapshot, never a borrowed capture/capsule pointer.
 */
typedef struct {
  AGX_WIN32_ASAHI_SCENE_PHASE Phase;
  struct pipe_screen *Screen;
  struct pipe_context *Context;
  struct pipe_resource *Target, *VertexBuffer;
  struct agx_batch *Batch;
  void *VertexShader, *FragmentShader, *Blend, *Rasterizer, *Depth, *Elements;
  int32_t SubmitStatus, RetireStatus;
  AGX_WIN32_ASAHI_SCENE_RECEIPT Receipt;
} AGX_WIN32_ASAHI_SCENE;

/* Validates the existing KMD device-info contract and boot generation. It is
 * not a live inventory measurement: hardware callers must separately establish
 * the J313/package launch contract. No caller-provided fixture params accepted.
 */
#ifdef __cplusplus
extern "C" {
#endif
struct pipe_screen *AgxWin32AsahiScreenCreateForWindows(
    AGX_WIN32_ASAHI_BACKEND *, AGX_WIN32_SCREEN *,
    const AGX_WIN32_ASAHI_OWNER_OPS *, void *, const AGX_WIN32_ASAHI_BATCH_OPS *);
/* Returns a constructor-owned screen retained solely for retryable cleanup
 * after ScreenCreate returned NULL. It is never a usable factory result. */
struct pipe_screen *AgxWin32AsahiScreenRecover(AGX_WIN32_ASAHI_BACKEND *);
struct pipe_context *AgxWin32AsahiContextCreate(struct pipe_screen *, void *);
/* Exclusive context owner scopes this callback to draw_vbo. Native draws
 * invoke it after hazard legalization and before taking their batch; internal
 * legalization draws suppress it. Clear it before releasing caller storage. */
void AgxWin32AsahiSetDrawBindings(struct pipe_context *, int (*)(void *), void *);
/* Exclusive owner scopes the IB view base to draw_vbo, then clears it.
 * Resource identity keeps native replacement index buffers unmodified. */
void AgxWin32AsahiSetIndirectIndexBuffer(struct pipe_context *, struct pipe_resource *, unsigned);
void AgxWin32AsahiIndirectMetrics(struct pipe_context *, unsigned out[3]);
struct pipe_resource *AgxWin32AsahiImportLinearColor32(
    struct pipe_screen *, const AGX_WIN32_SCREEN_BUFFER *,
    APPLE_AGX_U32 Width, APPLE_AGX_U32 Height, APPLE_AGX_U32 Pitch,
    APPLE_AGX_U64 Bytes, AGX_WIN32_ASAHI_LINEAR_FORMAT);
void AgxWin32AsahiResourceRelease(struct pipe_resource **);
struct pipe_surface;
union pipe_color_union;
int AgxWin32AsahiClearRenderTargetRect(
    struct pipe_context *, struct pipe_surface *,
    const union pipe_color_union *, unsigned x, unsigned y,
    unsigned width, unsigned height);
int AgxWin32AsahiResourceIdentity(
    struct pipe_resource *, AGX_WIN32_RELOC_ALLOCATION *);
struct pipe_resource *AgxWin32AsahiCreateUncompressedDepthStencil(
    struct pipe_screen *,const struct pipe_resource *);
/* False retains caller storage; retry only after pending ownership is resolved.
 * All contexts must be destroyed before their screen is destroyed. */
int AgxWin32AsahiContextDestroy(struct pipe_context *);
int AgxWin32AsahiContextRetire(struct pipe_context *, APPLE_AGX_U32 TimeoutMs);
int AgxWin32AsahiResourceBusy(struct pipe_context *, struct pipe_resource *);
int AgxWin32AsahiBufferCopy(struct pipe_context *, struct pipe_resource *,
    APPLE_AGX_U64, struct pipe_resource *, APPLE_AGX_U64, APPLE_AGX_U64);
void *AgxWin32AsahiBufferCurrentMap(struct pipe_context *, struct pipe_resource *);
APPLE_AGX_U64 AgxWin32AsahiBufferGpuAddress(struct pipe_resource *);
#ifdef APPLE_AGX_GPUVA_WINSYS
/* C53 B1: reserved buffers over virtual BOs (agx_win32_asahi_bo.h) */
struct pipe_resource *AgxWin32AsahiCreateReservedBuffer(struct pipe_screen *, APPLE_AGX_U64 Bytes);
int AgxWin32AsahiMapReservedTiles(struct pipe_resource *Reserved, unsigned FirstTile, unsigned Tiles,
                                  struct pipe_resource *Backing, unsigned BackingTile);
/* C53 T2D: a reserved Texture2D (array) of Template (NULL if it cannot be
 * reserved), its D3D12 tiling (standard mips, standard tiles per mip, packed
 * tiles per layer), and the bind of one tile -- standard tile (X, Y) of Mip,
 * or packed tile X -- of Layer onto 64 KiB tile HeapTile of a buffer
 * resource (NULL: unmapped). */
struct pipe_resource *AgxWin32AsahiCreateReservedTexture(struct pipe_screen *,
                                                         const struct pipe_resource *Template);
int AgxWin32AsahiReservedTextureTiling(struct pipe_resource *Reserved, unsigned *StandardMips,
                                       unsigned *TilesX, unsigned *TilesY, unsigned *PackedTiles);
int AgxWin32AsahiMapReservedTextureTile(struct pipe_resource *Reserved, unsigned Layer, int Packed,
                                        unsigned Mip, unsigned X, unsigned Y,
                                        struct pipe_resource *Heap, unsigned HeapTile);
#endif
/* rc76: whether pipe->clear(Buffers) on the bound framebuffer is a fast
 * (load-op) clear of every buffer: its batch has neither drawn nor loaded
 * them. Takes the batch as agx_clear does. */
int AgxWin32AsahiClearIsFast(struct pipe_context *, unsigned Buffers);
int AgxWin32AsahiBufferMarkValid(struct pipe_context *, struct pipe_resource *);
void *AgxWin32AsahiBufferWriteMap(struct pipe_context *, struct pipe_resource *);
void AgxWin32AsahiContextDiagnostic(struct pipe_context *,
    APPLE_AGX_U32 State[16], APPLE_AGX_U32 Bindings[16]);
/* EXP1083 receipt-only: per batch slot capsule flags | active << 8 |
 * submitted << 9 and fence (8 values), then any_faults, backend Failed,
 * held | terminal << 1 | last refused line << 8, held render fence. */
APPLE_AGX_U64 AgxWin32AsahiContextNewestFence(struct pipe_context *);
/* D3D12 C47: a texture view's packed descriptor for a descriptor heap */
APPLE_AGX_U64 AgxWin32AsahiResourceLayoutKey(struct pipe_resource *);
int AgxWin32AsahiTextureDescriptor(struct pipe_context *, struct pipe_resource *,
    const struct pipe_sampler_view *, void *Out, APPLE_AGX_U64 *Key);
void AgxWin32AsahiNullTextureDescriptor(void *Out);
unsigned AgxWin32AsahiTextureDescriptorBytes(void);
void AgxWin32AsahiBatchReadResources(struct pipe_context *, struct pipe_resource *const *Resources,
    unsigned Count, int Compute);
void AgxWin32AsahiBatchWriteResources(struct pipe_context *, struct pipe_resource *const *Resources,
    unsigned Count, int Compute);
int AgxWin32AsahiImageDescriptors(struct pipe_context *, const struct pipe_image_view *, void *Texture,
    void *Pbe, APPLE_AGX_U64 *Key);
unsigned AgxWin32AsahiPbeDescriptorBytes(void);
unsigned AgxWin32AsahiSamplerDescriptorBytes(void);
int AgxWin32AsahiSamplerDescriptor(void *Cso, void *Out, APPLE_AGX_U16 *Bias, int *CustomBorder);
int AgxWin32AsahiBatchSamplerHandles(struct pipe_context *, int Compute, const void *Packed, unsigned Stride,
    unsigned Count, APPLE_AGX_U16 *Handles);
void AgxWin32AsahiNullPbeDescriptor(void *Out);
void AgxWin32AsahiContextRetireDiagnostic(struct pipe_context *,
    APPLE_AGX_U32 State[12]);
int AgxWin32AsahiContextDrawReceipt(struct pipe_context *);
int AgxWin32AsahiSetStreamOutputTargetOffsetForTest(
    struct pipe_stream_output_target *,APPLE_AGX_U32);
/* C39: the 4-byte buffer holding a stream-output target's write offset
 * (bytes from its buffer_offset), which the GPU advances as it appends. */
struct pipe_resource *AgxWin32AsahiStreamOutputOffset(struct pipe_stream_output_target *);
int AgxWin32AsahiContextFaulted(struct pipe_context *);
/* RC67 (GPUVA): before Begin, give the Count queries of one D3D12 query
 * index one shared BO (query i = PIPE query Type, index i), creating them if
 * all are null and moving the whole group to fresh storage if any member is
 * busy. 0 leaves the queries unchanged (agx_win32_query_group.h). */
int AgxWin32AsahiQueryGroupBegin(struct pipe_context *, unsigned Type,
    struct pipe_query **Group, unsigned Count);
int AgxWin32AsahiContextFlushForPresent(struct pipe_context *);
int AgxWin32AsahiScreenDestroy(struct pipe_screen *);

int AgxWin32AsahiSceneInit(AGX_WIN32_ASAHI_SCENE *, struct pipe_screen *, struct pipe_context *);
int AgxWin32AsahiSceneDraw(AGX_WIN32_ASAHI_SCENE *);
int AgxWin32AsahiSceneSubmit(AGX_WIN32_ASAHI_SCENE *);
/* A false return after Submit may still mean Render was entered: inspect Phase
 * and keep storage. Retire never infers success merely from a missing capsule. */
int AgxWin32AsahiSceneRetire(AGX_WIN32_ASAHI_SCENE *, APPLE_AGX_U32 TimeoutMs);
/* Abort only pre-submit work. False preserves resources and caller ownership;
 * no event signalling, mock callbacks, replay or force-release occurs here. */
int AgxWin32AsahiSceneCleanup(AGX_WIN32_ASAHI_SCENE *, APPLE_AGX_U32 TimeoutMs);

#ifdef __cplusplus
}
#endif

#endif
