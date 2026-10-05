#include "agx_win32_native_device.h"

#include <assert.h>
#include <string.h>

typedef struct _FIXTURE {
  AGX_WIN32_DEVICE_INFO Info;
  unsigned char Bytes[0x4000];
  unsigned Creates;
  unsigned Destroys;
} FIXTURE;

static int query(void *context, AGX_WIN32_DEVICE_INFO *info) {
  *info = ((FIXTURE *)context)->Info;
  return 1;
}
static int create_class(void *context, unsigned klass, unsigned long long bytes,
                        unsigned long long alignment, unsigned flags,
                        unsigned long long *token) {
  FIXTURE *fixture = context;
  assert(klass == AgxWin32BufferClassShader && bytes == 0x4000ULL &&
         alignment == 0x4000ULL &&
         flags == (AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead));
  ++fixture->Creates; *token = 9ULL; return 1;
}
static int create(void *c, unsigned long long b, unsigned f, unsigned long long *t) { (void)c; (void)b; (void)f; (void)t; return 0; }
static int map(void *c, unsigned long long t, unsigned long long o,
               unsigned long long b, unsigned a, void **p) {
  FIXTURE *f = c; assert(t == 9ULL && o == 0ULL && b == 0x4000ULL && a == AppleAgxWin32BufferCpuWrite); *p = f->Bytes; return 1;
}
static int unmap(void *c, unsigned long long t) { (void)c; assert(t == 9ULL); return 1; }
static int destroy(void *c, unsigned long long t) { FIXTURE *f=c; assert(t == 9ULL); ++f->Destroys; return 1; }
static int submit(void *c, const AGX_WIN32_CLEAR_REQUEST *r, unsigned *f) { (void)c; (void)r; (void)f; return 0; }
static int fence(void *c, unsigned f, unsigned t) { (void)c; (void)f; (void)t; return 1; }
static int retire(void *c, unsigned f) { (void)c; (void)f; return 1; }

int main(void) {
  FIXTURE fixture;
  AGX_WIN32_SCREEN screen;
  AGX_WIN32_NATIVE_DEVICE device;
  AGX_WIN32_NATIVE_BO bo = {0};
  AGX_WIN32_NATIVE_BO stale;
  AGX_WIN32_WINSYS_OPERATIONS transport = {
      .CreateBuffer = create, .MapBuffer = map, .UnmapBuffer = unmap,
      .DestroyBuffer = destroy, .SubmitClear = submit, .WaitFence = fence,
      .RetireFence = retire};
  AGX_WIN32_SCREEN_OPERATIONS ops = {query, create_class};
  APPLE_AGX_U64 address = 0ULL;
  memset(&fixture, 0, sizeof(fixture));
  fixture.Info.Magic = AGX_WIN32_DEVICE_INFO_MAGIC;
  fixture.Info.Version = AGX_WIN32_DEVICE_INFO_VERSION;
  fixture.Info.Bytes = sizeof(fixture.Info);
  fixture.Info.BootGeneration = 1u; fixture.Info.GpuGeneration = 13u;
  fixture.Info.GpuVariant = AgxWin32GpuG13G; fixture.Info.PageBytes = 0x4000u;
  fixture.Info.ClassCount = AGX_WIN32_BUFFER_CLASS_COUNT;
  for (unsigned i = 0u; i < AGX_WIN32_BUFFER_CLASS_COUNT; ++i) {
    fixture.Info.Classes[i].ClassId = i + 1u;
    fixture.Info.Classes[i].MinimumAlignment = 0x4000ULL;
    fixture.Info.Classes[i].MaximumBytes = 0x100000ULL;
    fixture.Info.Classes[i].Flags = AppleAgxWin32BufferCpuRead | AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead | AppleAgxWin32BufferGpuWrite;
  }
  assert(AgxWin32ScreenInitialize(&screen, &fixture, 7u, &transport, &ops) == AgxWin32ScreenSuccess);
  assert(AgxWin32NativeDeviceInitialize(&device, &screen, 0x100000000ULL, 7u) == AgxWin32NativeDeviceSuccess);
  assert(AgxWin32NativeDeviceCreateBo(&device, AgxWin32BufferClassShader, 0x4000ULL, 0x4000ULL, AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead, &bo) == AgxWin32NativeDeviceSuccess);
  assert(AgxWin32NativeDeviceResolveBo(&device, &bo, 0ULL, 0x4000ULL, &address) == AgxWin32NativeDeviceSuccess && address == 0x100000000ULL);
  stale = bo;
  assert(AgxWin32NativeDeviceDestroyBo(&device, &bo) == AgxWin32NativeDeviceSuccess);
  assert(AgxWin32NativeDeviceResolveBo(&device, &stale, 0ULL, 1ULL, &address) == AgxWin32NativeDeviceStale);
  assert(AgxWin32NativeDeviceResolveBo(&device, &bo, 0ULL, 1ULL, &address) == AgxWin32NativeDeviceStale);
  assert(fixture.Creates == 1u && fixture.Destroys == 1u);
  return 0;
}
