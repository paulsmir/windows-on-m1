#ifndef AGX_WIN32_ASAHI_SCENE_H
#define AGX_WIN32_ASAHI_SCENE_H

#include "agx_win32_asahi_batch.h"

struct pipe_screen;
struct pipe_context;
struct pipe_resource;
struct pipe_stream_output_target;

typedef enum {
  AgxAsahiSceneEmpty, AgxAsahiScenePreparing, AgxAsahiScenePrepared,
  AgxAsahiSceneDrawn, AgxAsahiSceneSubmitted, AgxAsahiSceneRetired,
  AgxAsahiSceneRejected, AgxAsahiSceneReleased
} AGX_WIN32_ASAHI_SCENE_PHASE;

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
struct pipe_resource *AgxWin32AsahiImportLinearBgra8(
    struct pipe_screen *, const AGX_WIN32_SCREEN_BUFFER *,
    APPLE_AGX_U32 Width, APPLE_AGX_U32 Height, APPLE_AGX_U32 Pitch,
    APPLE_AGX_U64 Bytes);
void AgxWin32AsahiResourceRelease(struct pipe_resource **);
int AgxWin32AsahiResourceIdentity(
    struct pipe_resource *, AGX_WIN32_RELOC_ALLOCATION *);
/* False retains caller storage; retry only after pending ownership is resolved.
 * All contexts must be destroyed before their screen is destroyed. */
int AgxWin32AsahiContextDestroy(struct pipe_context *);
int AgxWin32AsahiContextRetire(struct pipe_context *, APPLE_AGX_U32 TimeoutMs);
int AgxWin32AsahiContextDrawReceipt(struct pipe_context *);
int AgxWin32AsahiSetStreamOutputTargetOffsetForTest(
    struct pipe_stream_output_target *,APPLE_AGX_U32);
int AgxWin32AsahiContextFaulted(struct pipe_context *);
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
