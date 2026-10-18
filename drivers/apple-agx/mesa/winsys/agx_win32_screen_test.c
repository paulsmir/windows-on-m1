#include "agx_win32_screen.h"

#include <assert.h>
#include <string.h>

typedef struct _FAKE_SCREEN {
  AGX_WIN32_DEVICE_INFO Info;
  unsigned int Queries, Creates, Destroys, RetiredFences;
} FAKE_SCREEN;

static int query(void *context, AGX_WIN32_DEVICE_INFO *info) {
  FAKE_SCREEN *fake = context;
  ++fake->Queries;
  *info = fake->Info;
  return 1;
}

static int create_class(void *context, unsigned int class_id,
                        unsigned long long bytes,
                        unsigned long long alignment, unsigned int flags,
                        unsigned long long *token) {
  FAKE_SCREEN *fake = context;
  assert(class_id == AgxWin32BufferClassShader);
  assert(bytes == 0x8000ULL && alignment == 0x4000ULL);
  assert(flags == (AppleAgxWin32BufferCpuWrite |
                   AppleAgxWin32BufferGpuRead));
  ++fake->Creates;
  *token = 0x5001ULL;
  return 1;
}

static int map(void *context, unsigned long long token,
               unsigned long long offset, unsigned long long bytes,
               unsigned int access, void **address) {
  (void)context;
  assert(token == 0x5001ULL && offset == 0ULL && bytes == 0x4000ULL);
  assert(access == AppleAgxWin32BufferCpuWrite);
  *address = (void *)0x10000u;
  return 1;
}
static int unmap(void *context, unsigned long long token) {
  (void)context; return token == 0x5001ULL;
}
static int destroy(void *context, unsigned long long token) {
  FAKE_SCREEN *fake = context;
  if (token != 0x5001ULL) return 0;
  ++fake->Destroys; return 1;
}
static int unused_create(void *context, unsigned long long bytes,
                         unsigned int flags, unsigned long long *token) {
  (void)context; (void)bytes; (void)flags; (void)token; return 0;
}
static int unused_submit(void *context, const AGX_WIN32_CLEAR_REQUEST *request,
                         unsigned int *fence) {
  (void)context; (void)request; (void)fence; return 0;
}
static int wait(void *context, unsigned int fence, unsigned int timeout_ms) {
  (void)context; return fence == 9u && timeout_ms == 100u;
}
static int retire_fence(void *context, unsigned int fence) {
  FAKE_SCREEN *fake = context;
  if (fence != 9u) return 0;
  ++fake->RetiredFences; return 1;
}

static AGX_WIN32_DEVICE_INFO make_info(void) {
  AGX_WIN32_DEVICE_INFO info;
  memset(&info, 0, sizeof(info));
  info.Magic = AGX_WIN32_DEVICE_INFO_MAGIC;
  info.Version = AGX_WIN32_DEVICE_INFO_VERSION;
  info.Bytes = sizeof(info);
  info.BootGeneration = 17u;
  info.GpuGeneration = 13u;
  info.GpuVariant = AgxWin32GpuG13G;
  info.PageBytes = 0x4000u;
  info.ClassCount = 3u;
  info.Classes[0] = (AGX_WIN32_BUFFER_CLASS_INFO){
      AgxWin32BufferClassGeneral, 0x4000u, 0x40000000ULL,
      AppleAgxWin32BufferCpuRead | AppleAgxWin32BufferCpuWrite |
      AppleAgxWin32BufferGpuRead | AppleAgxWin32BufferGpuWrite};
  info.Classes[1] = (AGX_WIN32_BUFFER_CLASS_INFO){
      AgxWin32BufferClassShader, 0x4000u, 0x10000000ULL,
      AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead};
  info.Classes[2] = (AGX_WIN32_BUFFER_CLASS_INFO){
      AgxWin32BufferClassEncoder, 0x4000u, 0x10000000ULL,
      AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead};
  return info;
}

int main(void) {
  FAKE_SCREEN fake = {.Info = make_info()};
  AGX_WIN32_WINSYS_OPERATIONS transport_ops = {
      unused_create, map, unmap, destroy, unused_submit, wait, retire_fence};
  AGX_WIN32_SCREEN_OPERATIONS screen_ops = {query, create_class};
  AGX_WIN32_SCREEN screen;
  AGX_WIN32_SCREEN_BUFFER buffer;
  void *address = NULL;
  assert(AgxWin32ScreenInitialize(&screen, &fake, 23u, &transport_ops,
                                  &screen_ops) == AgxWin32ScreenSuccess);
  assert(fake.Queries == 1u && screen.Info.BootGeneration == 17u &&
         screen.Generation == 23u);
  assert(AgxWin32ScreenCreateBuffer(
      &screen, AgxWin32BufferClassShader, 0x8000ULL, 0x4000ULL,
      AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead,
      &buffer) == AgxWin32ScreenSuccess);
  assert(buffer.Transport.Token == 0x5001ULL &&
         buffer.Transport.Generation == 23u &&
         buffer.ClassId == AgxWin32BufferClassShader);
  assert(AgxWin32ScreenMapBuffer(
      &screen, &buffer, 0ULL, 0x4000ULL, AppleAgxWin32BufferCpuWrite,
      &address) == AgxWin32ScreenSuccess);
  assert(address == (void *)0x10000u);
  assert(AgxWin32ScreenUnmapBuffer(&screen, &buffer) ==
         AgxWin32ScreenSuccess);
  assert(AgxWin32ScreenWaitFence(&screen, 9u, 100u) ==
         AgxWin32ScreenSuccess);
  assert(AgxWin32ScreenRetireFence(&screen, 9u) ==
         AgxWin32ScreenSuccess);
  assert(fake.RetiredFences == 1u);
  assert(AgxWin32ScreenInvalidate(&screen, 18u) ==
         AgxWin32ScreenSuccess);
  assert(AgxWin32ScreenDestroyBuffer(&screen, &buffer) ==
         AgxWin32ScreenStaleGeneration);
  assert(fake.Destroys == 0u);

  fake.Info = make_info();
  fake.Info.Classes[1].Flags = AppleAgxWin32BufferGpuRead;
  assert(AgxWin32DeviceInfoValid(&fake.Info));
  assert(AgxWin32ScreenInitialize(&screen, &fake, 24u, &transport_ops,
                                  &screen_ops) == AgxWin32ScreenSuccess);
  memset(&buffer, 0xa5, sizeof(buffer));
  assert(AgxWin32ScreenCreateBuffer(
      &screen, AgxWin32BufferClassShader, 0x8000ULL, 0x4000ULL,
      AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead,
      &buffer) == AgxWin32ScreenAccess);
  assert(fake.Creates == 1u);

  fake.Info.Classes[2].ClassId = AgxWin32BufferClassShader;
  assert(!AgxWin32DeviceInfoValid(&fake.Info));
  fake.Info = make_info();
  fake.Info.PageBytes = 0x1000u;
  assert(!AgxWin32DeviceInfoValid(&fake.Info));
  fake.Info = make_info();
  fake.Info.BootGeneration = 0u;
  assert(!AgxWin32DeviceInfoValid(&fake.Info));
  return 0;
}
