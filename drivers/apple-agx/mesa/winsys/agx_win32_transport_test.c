#include "agx_win32_transport.h"

#include <assert.h>
#include <string.h>

typedef struct _FAKE_WINSYS {
  APPLE_AGX_U32 Creates;
  APPLE_AGX_U32 Maps;
  APPLE_AGX_U32 Unmaps;
  APPLE_AGX_U32 Destroys;
  APPLE_AGX_U32 Submits;
  APPLE_AGX_U32 Waits;
} FAKE_WINSYS;

static int fake_create(void *context, APPLE_AGX_U64 bytes,
                       APPLE_AGX_U32 flags, APPLE_AGX_U64 *token) {
  FAKE_WINSYS *fake = (FAKE_WINSYS *)context;
  assert(bytes == 0x20000ULL);
  assert(flags == (AppleAgxWin32BufferCpuRead |
                   AppleAgxWin32BufferGpuWrite));
  ++fake->Creates;
  *token = 0x1234ULL;
  return 1;
}

static int fake_map(void *context, APPLE_AGX_U64 token,
                    APPLE_AGX_U64 offset, APPLE_AGX_U64 bytes,
                    APPLE_AGX_U32 access, void **address) {
  FAKE_WINSYS *fake = (FAKE_WINSYS *)context;
  assert(token == 0x1234ULL && offset == 0x1000ULL && bytes == 0x2000ULL);
  assert(access == AppleAgxWin32BufferCpuRead);
  ++fake->Maps;
  *address = (void *)(size_t)0x5678u;
  return 1;
}

static int fake_unmap(void *context, APPLE_AGX_U64 token) {
  FAKE_WINSYS *fake = (FAKE_WINSYS *)context;
  assert(token == 0x1234ULL);
  ++fake->Unmaps;
  return 1;
}

static int fake_destroy(void *context, APPLE_AGX_U64 token) {
  FAKE_WINSYS *fake = (FAKE_WINSYS *)context;
  assert(token == 0x1234ULL);
  ++fake->Destroys;
  return 1;
}

static int fake_submit(void *context, const AGX_WIN32_CLEAR_REQUEST *clear,
                       APPLE_AGX_U32 *fence) {
  FAKE_WINSYS *fake = (FAKE_WINSYS *)context;
  assert(clear->Generation == 7u && clear->Color == 0xffabcdefu);
  ++fake->Submits;
  *fence = 44u;
  return 1;
}

static int fake_wait(void *context, APPLE_AGX_U32 fence,
                     APPLE_AGX_U32 timeout_ms) {
  FAKE_WINSYS *fake = (FAKE_WINSYS *)context;
  assert(fence == 44u && timeout_ms == 1000u);
  ++fake->Waits;
  return 1;
}

static AGX_WIN32_CLEAR_REQUEST request(void) {
  AGX_WIN32_CLEAR_REQUEST value;
  memset(&value, 0, sizeof(value));
  value.Generation = 7u;
  value.AllocationIndex = 3u;
  value.AllocationOffset = 0x20000ULL;
  value.AllocationBytes = 0x100000ULL;
  value.Format = AppleAgxWin32FormatBgra8Unorm;
  value.Color = 0xffabcdefu;
  value.SurfaceWidth = 512u;
  value.SurfaceHeight = 512u;
  value.SurfacePitch = 2048u;
  value.Left = 16u;
  value.Top = 32u;
  value.Right = 496u;
  value.Bottom = 480u;
  return value;
}

static void test_exact_clear_envelope(void) {
  AGX_WIN32_CLEAR_REQUEST input = request();
  unsigned char bytes[APPLE_AGX_WIN32_COMMAND_MAX_BYTES];
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  APPLE_AGX_U32 commandBytes = 0u;
  memset(bytes, 0xa5, sizeof(bytes));
  assert(AgxWin32TransportBuildClear(&input, bytes, sizeof(bytes),
                                     &commandBytes) ==
         AppleAgxWin32AbiSuccess);
  assert(commandBytes == 128u);
  assert(AppleAgxWin32CommandValidate(bytes, commandBytes, 7u, 4u, &view) ==
         AppleAgxWin32AbiSuccess);
  assert(view.Header->TotalBytes == 128u);
  assert(view.Header->ReferenceCount == 1u);
  assert(view.References[0].AllocationIndex == 3u);
  assert(view.References[0].Offset == 0x20000ULL);
  assert(view.References[0].Bytes == 0x100000ULL);
  assert(view.Clear->Color == 0xffabcdefu);
  assert(view.Clear->Left == 16u && view.Clear->Bottom == 480u);
  assert(view.Header->ContentHash ==
         AppleAgxWin32CommandHash(bytes, commandBytes));
}

static void test_capacity_generation_and_geometry_fail_closed(void) {
  AGX_WIN32_CLEAR_REQUEST input = request();
  unsigned char bytes[128];
  unsigned char original[128];
  APPLE_AGX_U32 commandBytes = 0xaaaaaaaau;
  memset(bytes, 0xa5, sizeof(bytes));
  memcpy(original, bytes, sizeof(bytes));
  assert(AgxWin32TransportBuildClear(&input, bytes, 127u, &commandBytes) ==
         AppleAgxWin32AbiArgument);
  assert(commandBytes == 0xaaaaaaaau);
  assert(memcmp(bytes, original, sizeof(bytes)) == 0);
  input.Generation = 0u;
  assert(AgxWin32TransportBuildClear(&input, bytes, sizeof(bytes),
                                     &commandBytes) ==
         AppleAgxWin32AbiStaleGeneration);
  input = request();
  input.AllocationBytes = 0u;
  assert(AgxWin32TransportBuildClear(&input, bytes, sizeof(bytes),
                                     &commandBytes) ==
         AppleAgxWin32AbiRange);
  input = request();
  input.Right = 513u;
  assert(AgxWin32TransportBuildClear(&input, bytes, sizeof(bytes),
                                     &commandBytes) ==
         AppleAgxWin32AbiPayload);
}

static void test_built_bytes_are_independent_of_request_storage(void) {
  AGX_WIN32_CLEAR_REQUEST input = request();
  unsigned char bytes[128];
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  APPLE_AGX_U32 commandBytes;
  assert(AgxWin32TransportBuildClear(&input, bytes, sizeof(bytes),
                                     &commandBytes) ==
         AppleAgxWin32AbiSuccess);
  memset(&input, 0, sizeof(input));
  assert(AppleAgxWin32CommandValidate(bytes, commandBytes, 7u, 4u, &view) ==
         AppleAgxWin32AbiSuccess);
  assert(view.Clear->Color == 0xffabcdefu);
  assert(view.References[0].Offset == 0x20000ULL);
}

static void test_two_data_driven_clears_have_distinct_hashes(void) {
  AGX_WIN32_CLEAR_REQUEST first = request();
  AGX_WIN32_CLEAR_REQUEST second = request();
  unsigned char firstBytes[128];
  unsigned char secondBytes[128];
  APPLE_AGX_WIN32_COMMAND_VIEW firstView;
  APPLE_AGX_WIN32_COMMAND_VIEW secondView;
  APPLE_AGX_U32 firstCount;
  APPLE_AGX_U32 secondCount;
  second.Color = 0xff0000ffu;
  second.Top = 256u;
  second.Bottom = 512u;
  assert(AgxWin32TransportBuildClear(
             &first, firstBytes, sizeof(firstBytes), &firstCount) ==
         AppleAgxWin32AbiSuccess);
  assert(AgxWin32TransportBuildClear(
             &second, secondBytes, sizeof(secondBytes), &secondCount) ==
         AppleAgxWin32AbiSuccess);
  assert(AppleAgxWin32CommandValidate(
             firstBytes, firstCount, 7u, 4u, &firstView) ==
         AppleAgxWin32AbiSuccess);
  assert(AppleAgxWin32CommandValidate(
             secondBytes, secondCount, 7u, 4u, &secondView) ==
         AppleAgxWin32AbiSuccess);
  assert(firstView.Header->ContentHash != secondView.Header->ContentHash);
  assert(firstView.Clear->Color == 0xffabcdefu);
  assert(secondView.Clear->Color == 0xff0000ffu);
  assert(firstView.Clear->Top == 32u && secondView.Clear->Top == 256u);
}

static void test_resource_facing_winsys_has_no_fd_or_physical_contract(void) {
  FAKE_WINSYS fake;
  AGX_WIN32_WINSYS_OPERATIONS operations;
  AGX_WIN32_WINSYS winsys;
  AGX_WIN32_BUFFER buffer;
  AGX_WIN32_CLEAR_REQUEST clear = request();
  void *address = NULL;
  APPLE_AGX_U32 fence = 0u;
  memset(&fake, 0, sizeof(fake));
  memset(&operations, 0, sizeof(operations));
  operations.CreateBuffer = fake_create;
  operations.MapBuffer = fake_map;
  operations.UnmapBuffer = fake_unmap;
  operations.DestroyBuffer = fake_destroy;
  operations.SubmitClear = fake_submit;
  operations.WaitFence = fake_wait;
  assert(AgxWin32WinsysInitialize(&winsys, &fake, 7u, &operations) ==
         AgxWin32WinsysSuccess);
  assert(AgxWin32WinsysCreateBuffer(
             &winsys, 0x20000ULL,
             AppleAgxWin32BufferCpuRead | AppleAgxWin32BufferGpuWrite,
             &buffer) == AgxWin32WinsysSuccess);
  assert(buffer.Token == 0x1234ULL && buffer.Generation == 7u);
  assert(AgxWin32WinsysMapBuffer(
             &winsys, &buffer, 0x1000ULL, 0x2000ULL,
             AppleAgxWin32BufferCpuRead, &address) ==
         AgxWin32WinsysSuccess);
  assert(address == (void *)(size_t)0x5678u && buffer.Mapped);
  assert(AgxWin32WinsysUnmapBuffer(&winsys, &buffer) ==
         AgxWin32WinsysSuccess);
  assert(AgxWin32WinsysSubmitClear(&winsys, &clear, &fence) ==
         AgxWin32WinsysSuccess);
  assert(fence == 44u);
  assert(AgxWin32WinsysWaitFence(&winsys, fence, 1000u) ==
         AgxWin32WinsysSuccess);
  assert(AgxWin32WinsysDestroyBuffer(&winsys, &buffer) ==
         AgxWin32WinsysSuccess);
  assert(buffer.Token == 0ULL);
  assert(fake.Creates == 1u && fake.Maps == 1u && fake.Unmaps == 1u &&
         fake.Submits == 1u && fake.Waits == 1u && fake.Destroys == 1u);
}

static void test_winsys_rejects_stale_and_out_of_range_buffers(void) {
  FAKE_WINSYS fake;
  AGX_WIN32_WINSYS_OPERATIONS operations;
  AGX_WIN32_WINSYS winsys;
  AGX_WIN32_BUFFER buffer;
  void *address = (void *)(size_t)0x9999u;
  memset(&fake, 0, sizeof(fake));
  memset(&operations, 0, sizeof(operations));
  operations.CreateBuffer = fake_create;
  operations.MapBuffer = fake_map;
  operations.UnmapBuffer = fake_unmap;
  operations.DestroyBuffer = fake_destroy;
  operations.SubmitClear = fake_submit;
  operations.WaitFence = fake_wait;
  assert(AgxWin32WinsysInitialize(&winsys, &fake, 7u, &operations) ==
         AgxWin32WinsysSuccess);
  assert(AgxWin32WinsysCreateBuffer(
             &winsys, 0x20000ULL,
             AppleAgxWin32BufferCpuRead | AppleAgxWin32BufferGpuWrite,
             &buffer) == AgxWin32WinsysSuccess);
  assert(AgxWin32WinsysMapBuffer(
             &winsys, &buffer, 0x1f000ULL, 0x2000ULL,
             AppleAgxWin32BufferCpuRead, &address) ==
         AgxWin32WinsysRange);
  assert(address == (void *)(size_t)0x9999u && fake.Maps == 0u);
  buffer.Generation = 6u;
  assert(AgxWin32WinsysDestroyBuffer(&winsys, &buffer) ==
         AgxWin32WinsysStaleGeneration);
  assert(fake.Destroys == 0u);
}

int main(void) {
  test_exact_clear_envelope();
  test_capacity_generation_and_geometry_fail_closed();
  test_built_bytes_are_independent_of_request_storage();
  test_two_data_driven_clears_have_distinct_hashes();
  test_resource_facing_winsys_has_no_fd_or_physical_contract();
  test_winsys_rejects_stale_and_out_of_range_buffers();
  return 0;
}
