#include <assert.h>
#include <string.h>

#include "apple_agx_backend_memory_bridge.h"

static unsigned char pool_token;

typedef struct _SYNC_CAPTURE {
  const APPLE_AGX_MEMORY_OBJECT *Pool;
  const void *Address;
  unsigned int Bytes;
  APPLE_AGX_BACKEND_MEMORY_SYNC_DIRECTION Direction;
  unsigned int Calls;
  unsigned char Result;
} SYNC_CAPTURE;

static unsigned char synchronize(void *context,
                                 const APPLE_AGX_MEMORY_OBJECT *pool,
                                 const void *address, unsigned int bytes,
                                 APPLE_AGX_BACKEND_MEMORY_SYNC_DIRECTION
                                     direction) {
  SYNC_CAPTURE *capture = context;
  capture->Pool = pool;
  capture->Address = address;
  capture->Bytes = bytes;
  capture->Direction = direction;
  ++capture->Calls;
  return capture->Result;
}

static APPLE_AGX_BACKEND_MEMORY_SYNC_IO sync_io(SYNC_CAPTURE *capture) {
  APPLE_AGX_BACKEND_MEMORY_SYNC_IO io;
  memset(&io, 0, sizeof(io));
  io.Context = capture;
  io.Synchronize = synchronize;
  return io;
}

static APPLE_AGX_RENDER_TEMPLATE_ROOTS roots(void) {
  APPLE_AGX_RENDER_TEMPLATE_ROOTS value = {
      {0x1503880000ULL, 0x1503898000ULL},
      {0x1503870000ULL, 0x1503890000ULL}};
  return value;
}

static void initialize_pool(APPLE_AGX_MEMORY_OBJECT *pool) {
  memset(pool, 0, sizeof(*pool));
  pool->CpuAddress = &pool_token;
  pool->GpuVirtualAddress = APPLE_AGX_BACKEND_BRIDGE_GDI_GPU_BASE;
  pool->Length = APPLE_AGX_BACKEND_BRIDGE_GDI_POOL_BYTES;
  pool->State = AppleAgxMemoryGpuMapped;
}

static void test_borrow_is_validation_not_a_second_mapping(void) {
  APPLE_AGX_MEMORY_OBJECT pool;
  APPLE_AGX_BACKEND_MEMORY_BRIDGE bridge;
  APPLE_AGX_RENDER_TEMPLATE_ROOTS prepared = roots();
  void *cpu;
  unsigned long long gpu;
  unsigned int bytes;
  SYNC_CAPTURE capture = {0};
  APPLE_AGX_BACKEND_MEMORY_SYNC_IO sync = sync_io(&capture);

  initialize_pool(&pool);
  capture.Result = 1u;
  assert(AppleAgxBackendMemoryBridgeInitialize(&bridge, &pool, &prepared,
                                               &sync));
  assert(AppleAgxBackendMemoryBridgeBorrow(&bridge, &cpu, &gpu, &bytes));
  assert(cpu == pool.CpuAddress);
  assert(gpu == APPLE_AGX_BACKEND_BRIDGE_GDI_GPU_BASE);
  assert(bytes == APPLE_AGX_BACKEND_BRIDGE_GDI_POOL_BYTES);
  assert(AppleAgxBackendMemoryBridgeFlushForDevice(&bridge, cpu, bytes));
  assert(capture.Calls == 1u);
  assert(capture.Pool == &pool);
  assert(capture.Address == cpu);
  assert(capture.Bytes == bytes);
  assert(capture.Direction == AppleAgxBackendMemorySyncForDevice);
  assert(AppleAgxBackendMemoryBridgeFlushForCpu(&bridge, cpu, bytes));
  assert(capture.Calls == 2u);
  assert(capture.Direction == AppleAgxBackendMemorySyncForCpu);
  assert(AppleAgxBackendMemoryBridgeRelease(&bridge, cpu, bytes));
  assert(pool.State == AppleAgxMemoryGpuMapped);
}

static void test_sync_contract_is_required_and_failure_is_fail_closed(void) {
  APPLE_AGX_MEMORY_OBJECT pool;
  APPLE_AGX_BACKEND_MEMORY_BRIDGE bridge;
  APPLE_AGX_RENDER_TEMPLATE_ROOTS prepared = roots();
  SYNC_CAPTURE capture = {0};
  APPLE_AGX_BACKEND_MEMORY_SYNC_IO sync = sync_io(&capture);
  void *cpu;
  unsigned long long gpu;
  unsigned int bytes;

  initialize_pool(&pool);
  assert(!AppleAgxBackendMemoryBridgeInitialize(&bridge, &pool, &prepared,
                                                NULL));
  sync.Synchronize = NULL;
  assert(!AppleAgxBackendMemoryBridgeInitialize(&bridge, &pool, &prepared,
                                                &sync));

  sync = sync_io(&capture);
  capture.Result = 0u;
  assert(AppleAgxBackendMemoryBridgeInitialize(&bridge, &pool, &prepared,
                                               &sync));
  assert(AppleAgxBackendMemoryBridgeBorrow(&bridge, &cpu, &gpu, &bytes));
  assert(!AppleAgxBackendMemoryBridgeFlushForDevice(&bridge, cpu, bytes));
  assert(capture.Calls == 1u);
  assert(!AppleAgxBackendMemoryBridgeFlushForCpu(&bridge, cpu, bytes));
  assert(capture.Calls == 2u);
}

static void test_real_record_shadow_is_reconstructed_and_fence_is_exact(void) {
  APPLE_AGX_MEMORY_OBJECT pool;
  APPLE_AGX_BACKEND_MEMORY_BRIDGE bridge;
  APPLE_AGX_RENDER_TEMPLATE_ROOTS prepared = roots();
  APPLE_AGX_DMA_SHADOW shadow;
  unsigned char storage[256];
  const unsigned char first[] = {0x11u, 0x22u};
  const unsigned char second[] = {0x33u, 0x44u};
  const unsigned char *resolved;
  unsigned int resolved_bytes;
  void *cpu;
  unsigned long long gpu;
  unsigned int bytes;
  SYNC_CAPTURE capture = {0};
  APPLE_AGX_BACKEND_MEMORY_SYNC_IO sync = sync_io(&capture);

  initialize_pool(&pool);
  capture.Result = 1u;
  AppleAgxDmaShadowInitialize(&shadow, storage, sizeof(storage));
  assert(AppleAgxDmaShadowAppend(&shadow, 8u, first, sizeof(first)));
  assert(AppleAgxDmaShadowAppend(&shadow, 10u, second, sizeof(second)));
  assert(AppleAgxBackendMemoryBridgeInitialize(&bridge, &pool, &prepared,
                                               &sync));
  assert(AppleAgxBackendMemoryBridgeBorrow(&bridge, &cpu, &gpu, &bytes));
  /*
   * WDDM reports the full private-data capacity and a private byte range.
   * That range is independent from the submitted DMA interval.  The bridge
   * must open the shadow using the full capacity, prove that the current
   * private range contains the encoded shadow, and copy records by DMA offset.
   */
  assert(!AppleAgxBackendMemoryBridgeResolve(
      &bridge, storage, sizeof(storage), 0u, shadow.BytesUsed, 8u, 12u,
      &resolved, &resolved_bytes));
  assert(AppleAgxDmaShadowSeal(&shadow, 17u));
  assert(AppleAgxBackendMemoryBridgeResolve(
      &bridge, storage, sizeof(storage), 0u, shadow.BytesUsed, 8u, 12u,
      &resolved, &resolved_bytes));
  assert(resolved_bytes == 4u);
  assert(resolved[0] == 0x11u && resolved[1] == 0x22u &&
         resolved[2] == 0x33u && resolved[3] == 0x44u);
  assert(!AppleAgxBackendMemoryBridgeResolve(
      &bridge, storage, sizeof(storage), 1u, shadow.BytesUsed, 8u, 12u,
      &resolved, &resolved_bytes));
  assert(!AppleAgxBackendMemoryBridgeResolve(
      &bridge, storage, sizeof(storage), 0u, shadow.BytesUsed - 1u, 8u, 12u,
      &resolved, &resolved_bytes));
  assert(AppleAgxBackendMemoryBridgeMarkSubmitted(&bridge, 17u));
  assert(!AppleAgxBackendMemoryBridgeMarkCompleted(&bridge, 16u));
  assert(AppleAgxBackendMemoryBridgeMarkAborted(&bridge, 17u));
  assert(pool.State == AppleAgxMemoryCompleted);
}

int main(void) {
  test_borrow_is_validation_not_a_second_mapping();
  test_sync_contract_is_required_and_failure_is_fail_closed();
  test_real_record_shadow_is_reconstructed_and_fence_is_exact();
  return 0;
}
