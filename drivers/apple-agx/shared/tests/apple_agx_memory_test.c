#include "apple_agx_memory.h"

#include <assert.h>
#include <string.h>

#define PAGE_SIZE_16K 0x4000ULL

typedef struct _FAKE_MEMORY {
  unsigned char Storage[0x18000];
  unsigned long long PageList[4];
  unsigned long long DeviceBase;
  unsigned long long LastBytes;
  unsigned int AllocateCount;
  unsigned int FreeCount;
  unsigned char FailAllocation;
  unsigned char FailFree;
} FAKE_MEMORY;

static unsigned char
allocate_contiguous(void *context, unsigned long long bytes, void **cpu_base,
                    unsigned long long *device_base, void **allocation_handle) {
  FAKE_MEMORY *fake = (FAKE_MEMORY *)context;
  ++fake->AllocateCount;
  fake->LastBytes = bytes;
  if (fake->FailAllocation != 0u)
    return 0u;
  *cpu_base = &fake->Storage[0x1000];
  *device_base = fake->DeviceBase;
  *allocation_handle = fake;
  return 1u;
}

static unsigned char free_contiguous(void *context, void *allocation_handle) {
  FAKE_MEMORY *fake = (FAKE_MEMORY *)context;
  assert(allocation_handle == fake);
  ++fake->FreeCount;
  return fake->FailFree == 0u;
}

static unsigned char allocate_page_list(
    void *context, unsigned long long bytes, void **cpu_base,
    const unsigned long long **device_pages, unsigned int *device_page_count,
    void **allocation_handle) {
  FAKE_MEMORY *fake = (FAKE_MEMORY *)context;
  if (bytes != 0x10000ULL)
    return 0u;
  fake->PageList[0] = 0x40000000ULL;
  fake->PageList[1] = 0x44004000ULL;
  fake->PageList[2] = 0x48008000ULL;
  fake->PageList[3] = 0x4c00c000ULL;
  *cpu_base = &fake->Storage[0];
  *device_pages = fake->PageList;
  *device_page_count = 4u;
  *allocation_handle = fake;
  return 1u;
}

static unsigned char free_page_list(void *context, void *allocation_handle) {
  return free_contiguous(context, allocation_handle);
}

static void init_fixture(FAKE_MEMORY *fake, APPLE_AGX_MEMORY_IO *io,
                         APPLE_AGX_MEMORY_OBJECT *object) {
  memset(fake, 0, sizeof(*fake));
  memset(io, 0, sizeof(*io));
  memset(object, 0, sizeof(*object));
  fake->DeviceBase = 0x10001000ULL;
  io->Context = fake;
  io->AllocateContiguous = allocate_contiguous;
  io->FreeContiguous = free_contiguous;
}

static void test_aligned_view_and_release(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_MEMORY_OBJECT object;
  init_fixture(&fake, &io, &object);

  assert(AppleAgxMemoryAllocate(&io, PAGE_SIZE_16K, &object) ==
         AppleAgxMemoryResultOk);
  assert(fake.LastBytes == 0x8000ULL);
  assert(object.AllocationCpuBase == &fake.Storage[0x1000]);
  assert(object.CpuAddress == &fake.Storage[0x4000]);
  assert(object.AllocationDeviceBase == 0x10001000ULL);
  assert(object.DeviceAddress == 0x10004000ULL);
  assert(object.Length == PAGE_SIZE_16K);
  assert(object.AllocationLength == 0x8000ULL);
  assert(object.State == AppleAgxMemoryCpuOwned);

  assert(AppleAgxMemoryRelease(&io, &object) == AppleAgxMemoryResultOk);
  assert(fake.FreeCount == 1u);
  assert(object.State == AppleAgxMemoryEmpty);
  assert(AppleAgxMemoryRelease(&io, &object) == AppleAgxMemoryResultOk);
  assert(fake.FreeCount == 1u);
}

static void test_explicit_64k_alignment_for_wddm_segment_backing(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_MEMORY_OBJECT object;
  init_fixture(&fake, &io, &object);

  assert(AppleAgxMemoryAllocateAligned(&io, 0x10000ULL, 0x10000ULL,
                                       &object) ==
         AppleAgxMemoryResultOk);
  assert(fake.LastBytes == 0x20000ULL);
  assert(object.AllocationCpuBase == &fake.Storage[0x1000]);
  assert(object.CpuAddress == &fake.Storage[0x10000]);
  assert(object.DeviceAddress == 0x10010000ULL);
  assert((object.DeviceAddress & 0xffffULL) == 0ULL);
  assert(object.Length == 0x10000ULL);
  assert(object.AllocationLength == 0x20000ULL);
  assert(AppleAgxMemoryRelease(&io, &object) == AppleAgxMemoryResultOk);
}

static void test_page_list_backing_preserves_each_agx_leaf(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_MEMORY_OBJECT object;
  init_fixture(&fake, &io, &object);
  io.AllocatePageList = allocate_page_list;
  io.FreePageList = free_page_list;

  assert(AppleAgxMemoryAllocateAligned(&io, 0x10000ULL, 0x10000ULL,
                                       &object) == AppleAgxMemoryResultOk);
  assert(object.CpuAddress == &fake.Storage[0]);
  assert(object.DevicePages == fake.PageList);
  assert(object.DevicePageCount == 4u);
  assert(object.DeviceAddress == fake.PageList[0]);
  assert(object.AllocationLength == 0x10000ULL);
  assert(AppleAgxMemoryRelease(&io, &object) == AppleAgxMemoryResultOk);
  assert(fake.FreeCount == 1u);
}

static void test_explicit_alignment_rejects_invalid_contracts(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_MEMORY_OBJECT object;
  init_fixture(&fake, &io, &object);

  assert(AppleAgxMemoryAllocateAligned(&io, 0x10000ULL, 0x2000ULL,
                                       &object) ==
         AppleAgxMemoryResultInvalidArgument);
  assert(AppleAgxMemoryAllocateAligned(&io, 0x10000ULL, 0x6000ULL,
                                       &object) ==
         AppleAgxMemoryResultInvalidArgument);
  assert(AppleAgxMemoryAllocateAligned(&io, 0x10000ULL, 0x18000ULL,
                                       &object) ==
         AppleAgxMemoryResultInvalidArgument);
  assert(fake.AllocateCount == 0u);
}

static void test_lifecycle_is_fail_closed(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_MEMORY_OBJECT object;
  init_fixture(&fake, &io, &object);

  assert(AppleAgxMemoryAllocate(&io, 0x8000ULL, &object) ==
         AppleAgxMemoryResultOk);
  assert(AppleAgxMemoryMarkGpuMapped(&object, 3u, 0x1500000000ULL) ==
         AppleAgxMemoryResultNotPrepared);
  assert(AppleAgxMemoryMarkPrepared(&object) == AppleAgxMemoryResultOk);
  assert(AppleAgxMemoryMarkGpuMapped(&object, 3u, 0x1500000000ULL) ==
         AppleAgxMemoryResultOk);
  assert(AppleAgxMemoryRelease(&io, &object) == AppleAgxMemoryResultBusy);
  assert(AppleAgxMemoryMarkSubmitted(&object, 7ULL) == AppleAgxMemoryResultOk);
  assert(AppleAgxMemoryMarkGpuUnmapped(&object) == AppleAgxMemoryResultBusy);
  assert(AppleAgxMemoryMarkCompleted(&object, 6ULL) ==
         AppleAgxMemoryResultStaleFence);
  assert(AppleAgxMemoryMarkCompleted(&object, 7ULL) == AppleAgxMemoryResultOk);
  assert(AppleAgxMemoryMarkSubmitted(&object, 7ULL) ==
         AppleAgxMemoryResultStaleFence);
  assert(AppleAgxMemoryMarkSubmitted(&object, 8ULL) ==
         AppleAgxMemoryResultOk);
  assert(AppleAgxMemoryMarkAborted(&object, 7ULL) ==
         AppleAgxMemoryResultStaleFence);
  assert(AppleAgxMemoryMarkAborted(&object, 8ULL) ==
         AppleAgxMemoryResultOk);
  assert(object.State == AppleAgxMemoryCompleted);
  assert(AppleAgxMemoryMarkSubmitted(&object, 9ULL) ==
         AppleAgxMemoryResultOk);
  assert(AppleAgxMemoryMarkCompleted(&object, 9ULL) ==
         AppleAgxMemoryResultOk);
  assert(AppleAgxMemoryMarkGpuUnmapped(&object) == AppleAgxMemoryResultOk);
  assert(AppleAgxMemoryRelease(&io, &object) == AppleAgxMemoryResultOk);
}

static void test_firmware_and_last_render_contexts_are_valid(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_MEMORY_OBJECT object;
  init_fixture(&fake, &io, &object);

  assert(AppleAgxMemoryAllocate(&io, 0x8000ULL, &object) ==
         AppleAgxMemoryResultOk);
  assert(AppleAgxMemoryMarkPrepared(&object) == AppleAgxMemoryResultOk);
  assert(AppleAgxMemoryMarkGpuMapped(&object, 0u, 0xffffffa000000000ULL) ==
         AppleAgxMemoryResultOk);
  assert(object.Context == 0u && object.State == AppleAgxMemoryGpuMapped);
  assert(AppleAgxMemoryMarkGpuUnmapped(&object) == AppleAgxMemoryResultOk);
  assert(AppleAgxMemoryMarkGpuMapped(&object, 63u, 0x1500000000ULL) ==
         AppleAgxMemoryResultOk);
  assert(object.Context == 63u && object.State == AppleAgxMemoryGpuMapped);
  assert(AppleAgxMemoryMarkGpuUnmapped(&object) == AppleAgxMemoryResultOk);
  assert(AppleAgxMemoryRelease(&io, &object) == AppleAgxMemoryResultOk);
}

static void test_validation_and_allocation_rollback(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_MEMORY_OBJECT object;
  init_fixture(&fake, &io, &object);

  assert(AppleAgxMemoryAllocate(0, PAGE_SIZE_16K, &object) ==
         AppleAgxMemoryResultInvalidArgument);
  assert(AppleAgxMemoryAllocate(&io, 0ULL, &object) ==
         AppleAgxMemoryResultInvalidArgument);
  assert(AppleAgxMemoryAllocate(&io, PAGE_SIZE_16K + 1ULL, &object) ==
         AppleAgxMemoryResultInvalidArgument);
  fake.FailAllocation = 1u;
  assert(AppleAgxMemoryAllocate(&io, PAGE_SIZE_16K, &object) ==
         AppleAgxMemoryResultAllocationFailed);
  assert(object.State == AppleAgxMemoryEmpty);
  assert(fake.FreeCount == 0u);

  fake.FailAllocation = 0u;
  fake.DeviceBase = (1ULL << 40) - 0x1000ULL;
  assert(AppleAgxMemoryAllocate(&io, PAGE_SIZE_16K, &object) ==
         AppleAgxMemoryResultOutOfRange);
  assert(fake.FreeCount == 1u);
  assert(object.State == AppleAgxMemoryEmpty);
}

static void test_release_failure_preserves_ownership_for_retry(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_MEMORY_OBJECT object;
  init_fixture(&fake, &io, &object);

  assert(AppleAgxMemoryAllocate(&io, PAGE_SIZE_16K, &object) ==
         AppleAgxMemoryResultOk);
  fake.FailFree = 1u;
  assert(AppleAgxMemoryRelease(&io, &object) ==
         AppleAgxMemoryResultAllocationFailed);
  assert(object.State == AppleAgxMemoryCpuOwned);
  assert(object.AllocationHandle == &fake);
  fake.FailFree = 0u;
  assert(AppleAgxMemoryRelease(&io, &object) == AppleAgxMemoryResultOk);
  assert(object.State == AppleAgxMemoryEmpty);
}

int main(void) {
  test_aligned_view_and_release();
  test_explicit_64k_alignment_for_wddm_segment_backing();
  test_page_list_backing_preserves_each_agx_leaf();
  test_explicit_alignment_rejects_invalid_contracts();
  test_lifecycle_is_fail_closed();
  test_firmware_and_last_render_contexts_are_valid();
  test_validation_and_allocation_rollback();
  test_release_failure_preserves_ownership_for_retry();
  return 0;
}
