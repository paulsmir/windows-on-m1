#include "apple_agx_submission_coordinator.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static APPLE_AGX_BACKEND_BOOL ReadU32(
    void *Context, const volatile APPLE_AGX_U32 *Address,
    APPLE_AGX_U32 *Value) {
  (void)Context;
  if (Address == NULL || Value == NULL)
    return APPLE_AGX_BACKEND_FALSE;
  *Value = *Address;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_U32 MakeSubmissionBytes(APPLE_AGX_U32 Opcode,
                                         unsigned char *Bytes) {
  APPLE_AGX_GDI_COMMAND_DESCRIPTION description;
  APPLE_AGX_GDI_RECT sub_rect = {1u, 1u, 15u, 15u};
  APPLE_AGX_U32 written = 0u;

  memset(&description, 0, sizeof(description));
  description.Command.Opcode = Opcode;
  description.Command.SubRectCount = 1u;
  description.Command.Source = (APPLE_AGX_GDI_RECT){0u, 0u, 16u, 16u};
  description.Command.Destination =
      (APPLE_AGX_GDI_RECT){0u, 0u, 16u, 16u};
  description.Command.SourceGpuAddress = 0x10000ULL;
  description.Command.DestinationGpuAddress = 0x20000ULL;
  description.Command.SourceAllocationIndex = 1u;
  description.Command.DestinationAllocationIndex = 2u;
  description.Command.TemporaryAllocationIndex = 3u;
  description.Command.GammaAllocationIndex = 4u;
  description.Command.AlphaAllocationIndex = 5u;
  description.Command.TemporaryGpuAddress = 0x30000ULL;
  description.Command.GammaGpuAddress = 0x40000ULL;
  description.Command.AlphaGpuAddress = 0x50000ULL;
  description.Command.SourceHasAlpha = 1u;
  description.Command.SourceConstantAlpha = 0xffu;
  if (Opcode == (APPLE_AGX_U32)AppleAgxGdiBitBlt)
    description.Command.Rop = AppleAgxGdiBitBltSrcCopy;
  else if (Opcode == (APPLE_AGX_U32)AppleAgxGdiColorFill)
    description.Command.Rop = AppleAgxGdiColorFillPatCopy;
  else if (Opcode == (APPLE_AGX_U32)AppleAgxGdiStretchBlt)
    description.Command.Flags = 3u;
  description.SubRects = &sub_rect;
  assert(AppleAgxGdiEncodeDmaCommand(
      &description, Bytes,
      sizeof(APPLE_AGX_GDI_DMA_COMMAND) + sizeof(APPLE_AGX_GDI_RECT),
      &written));
  return written;
}

int main(void) {
  APPLE_AGX_SUBMISSION_COORDINATOR coordinator;
  APPLE_AGX_SUBMISSION_COORDINATOR_CONFIG config;
  APPLE_AGX_RENDER_PROVIDER render;
  APPLE_AGX_INITDATA_MEMORY_GRAPH initdata;
  APPLE_AGX_G13_QUEUE_PROVIDER queue;
  APPLE_AGX_MEMORY_OBJECT pool;
  APPLE_AGX_BACKEND_SUBMISSION submission;
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  volatile APPLE_AGX_U32 ta_write = 0u;
  volatile APPLE_AGX_U32 d3_write = 0u;
  unsigned char submission_bytes[sizeof(APPLE_AGX_GDI_DMA_COMMAND) +
                                 sizeof(APPLE_AGX_GDI_RECT)];
  unsigned char *pool_bytes =
      (unsigned char *)calloc(1u, APPLE_AGX_BACKEND_BRIDGE_GDI_POOL_BYTES);
  unsigned char *arena;
  unsigned char *arena_before = (unsigned char *)malloc(
      APPLE_AGX_EXP208_ARENA_BYTES);
  unsigned char *shared_bytes = (unsigned char *)calloc(
      APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT, 0x8000u);
  APPLE_AGX_U32 shared_index;

  assert(pool_bytes != NULL && shared_bytes != NULL && arena_before != NULL);
  arena = pool_bytes + APPLE_AGX_RENDER_PROVIDER_ARENA_OFFSET;
  assert(AppleAgxRenderTemplateMaterialize(
      arena, APPLE_AGX_EXP208_ARENA_BYTES, &roots));
  memset(&pool, 0, sizeof(pool));
  pool.CpuAddress = pool_bytes;
  pool.DeviceAddress = 0x900000000ULL;
  pool.GpuVirtualAddress = APPLE_AGX_BACKEND_BRIDGE_GDI_GPU_BASE;
  pool.Length = APPLE_AGX_BACKEND_BRIDGE_GDI_POOL_BYTES;
  pool.Context = APPLE_AGX_RENDER_PROVIDER_CONTEXT;
  pool.State = AppleAgxMemoryGpuMapped;

  memset(&render, 0, sizeof(render));
  memset(&initdata, 0, sizeof(initdata));
  initdata.Initialized = 1u;
  initdata.Built = 1u;
  initdata.RenderSharedMemory.Initialized = APPLE_AGX_TRUE;
  initdata.RenderSharedMemory.Built = APPLE_AGX_TRUE;
  initdata.RenderSharedMemory.ObjectCount =
      APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
  for (shared_index = 0u;
       shared_index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++shared_index) {
    initdata.RenderSharedMemory.Objects[shared_index].CpuAddress =
        shared_bytes + shared_index * 0x8000u;
    initdata.RenderSharedMemory.Objects[shared_index].DeviceAddress =
        0x30000000ULL + shared_index * 0x8000ULL;
    initdata.RenderSharedMemory.Objects[shared_index].Length = 0x8000u;
    initdata.RenderSharedMemory.Objects[shared_index].State =
        AppleAgxMemoryGpuMapped;
    initdata.RenderSharedMemory.VirtualAddresses[shared_index] =
        0xffffffa010000000ULL + shared_index * 0x8000ULL;
  }
  render.PreparedPool = &pool;
  render.PreparedRoots = roots;
  render.Initdata = &initdata;
  render.Initialized = APPLE_AGX_TRUE;
  render.ContextPublication.Active = 1u;

  memset(&queue, 0, sizeof(queue));
  queue.Phase = AppleAgxG13QueueProviderCreated;
  queue.Config.Ta.RingCapacity = APPLE_AGX_EXP208_QUEUE_CAPACITY;
  queue.Config.Ta.CpuWritePointer = &ta_write;
  queue.Config.D3.RingCapacity = APPLE_AGX_EXP208_QUEUE_CAPACITY;
  queue.Config.D3.CpuWritePointer = &d3_write;
  queue.RuntimeIo.ReadU32 = ReadU32;

  memset(&config, 0, sizeof(config));
  config.RenderProvider = &render;
  config.QueueProvider = &queue;
  config.EventPair.Ta = 7u;
  config.EventPair.D3 = 9u;
  config.EventPair.Lease = 1u;
  assert(AppleAgxSubmissionCoordinatorInitialize(&coordinator, &config));

  memset(&submission, 0, sizeof(submission));
  submission.ContextIdentity = APPLE_AGX_RENDER_PROVIDER_CONTEXT;
  submission.Submission.Kind = AppleAgxSubmissionGdi;
  memcpy(arena_before, arena, APPLE_AGX_EXP208_ARENA_BYTES);
  {
    static const APPLE_AGX_U32 opcodes[] = {
        AppleAgxGdiBitBlt,         AppleAgxGdiColorFill,
        AppleAgxGdiAlphaBlend,     AppleAgxGdiStretchBlt,
        AppleAgxGdiTransparentBlt, AppleAgxGdiClearTypeBlend,
    };
    APPLE_AGX_SUBMISSION_COORDINATOR coordinator_before = coordinator;
    APPLE_AGX_RENDER_PROVIDER render_before = render;
    APPLE_AGX_G13_QUEUE_PROVIDER queue_before = queue;
    APPLE_AGX_U32 index;

    assert(AppleAgxExp208SupportedGdiPrimitiveMask() == 0u);
    for (index = 0u; index < sizeof(opcodes) / sizeof(opcodes[0]); ++index) {
      APPLE_AGX_U32 submission_byte_count =
          MakeSubmissionBytes(opcodes[index], submission_bytes);
      submission.Submission.Fence = 41u + index;
      assert(!AppleAgxSubmissionCoordinatorStage(
          &coordinator, &submission, submission_bytes,
          submission_byte_count));
      assert(memcmp(&coordinator, &coordinator_before,
                    sizeof(coordinator)) == 0);
      assert(memcmp(&render, &render_before, sizeof(render)) == 0);
      assert(memcmp(&queue, &queue_before, sizeof(queue)) == 0);
      assert(memcmp(arena, arena_before,
                    APPLE_AGX_EXP208_ARENA_BYTES) == 0);
      assert(ta_write == 0u && d3_write == 0u);
    }
  }
  free(pool_bytes);
  free(shared_bytes);
  free(arena_before);
  return 0;
}
