#include "agx_win32_pipe_screen.h"

#include "pipe/p_state.h"
#include "util/box.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct _FAKE_PIPE {
  AGX_WIN32_DEVICE_INFO Info;
  unsigned Creates, Maps, Unmaps, Destroys;
  unsigned char Storage[2][0x8000];
  unsigned long long Tokens[2];
} FAKE_PIPE;

static int query(void *Context, AGX_WIN32_DEVICE_INFO *Info) {
  FAKE_PIPE *fake = Context;
  *Info = fake->Info;
  return 1;
}

static int create_class(void *Context, unsigned ClassId,
                        unsigned long long Bytes,
                        unsigned long long Alignment, unsigned Flags,
                        unsigned long long *Token) {
  FAKE_PIPE *fake = Context;
  unsigned index = fake->Creates;
  assert(index < 2u && ClassId == AgxWin32BufferClassGeneral);
  assert(Bytes != 0ULL && Bytes <= 0x8000ULL && Alignment == 0x4000ULL);
  assert(Flags == (AppleAgxWin32BufferCpuRead |
                   AppleAgxWin32BufferCpuWrite |
                   AppleAgxWin32BufferGpuRead |
                   AppleAgxWin32BufferGpuWrite));
  fake->Tokens[index] = 0x7001ULL + index;
  *Token = fake->Tokens[index];
  ++fake->Creates;
  return 1;
}

static int create_general(void *Context, unsigned long long Bytes,
                          unsigned Flags, unsigned long long *Token) {
  return create_class(Context, AgxWin32BufferClassGeneral, Bytes, 0x4000ULL,
                      Flags, Token);
}

static int map(void *Context, unsigned long long Token,
               unsigned long long Offset, unsigned long long Bytes,
               unsigned Access, void **Address) {
  FAKE_PIPE *fake = Context;
  unsigned index = Token == fake->Tokens[0] ? 0u : 1u;
  assert(Token == fake->Tokens[index] && Offset + Bytes <= 0x8000ULL);
  assert((Access & ~(AppleAgxWin32BufferCpuRead |
                     AppleAgxWin32BufferCpuWrite)) == 0u);
  *Address = fake->Storage[index] + (size_t)Offset;
  ++fake->Maps;
  return 1;
}

static int unmap(void *Context, unsigned long long Token) {
  FAKE_PIPE *fake = Context;
  assert(Token == fake->Tokens[0] || Token == fake->Tokens[1]);
  ++fake->Unmaps;
  return 1;
}

static int destroy(void *Context, unsigned long long Token) {
  FAKE_PIPE *fake = Context;
  assert(Token == fake->Tokens[0] || Token == fake->Tokens[1]);
  ++fake->Destroys;
  return 1;
}

static int fail_submit(void *Context, const AGX_WIN32_CLEAR_REQUEST *Request,
                       unsigned *Fence) {
  (void)Context; (void)Request; (void)Fence; return 0;
}
static int fail_wait(void *Context, unsigned Fence, unsigned TimeoutMs) {
  (void)Context; (void)Fence; (void)TimeoutMs; return 0;
}
static int retire(void *Context, unsigned Fence) {
  (void)Context; (void)Fence; return 0;
}

static AGX_WIN32_DEVICE_INFO device_info(void) {
  AGX_WIN32_DEVICE_INFO info;
  memset(&info, 0, sizeof(info));
  info.Magic = AGX_WIN32_DEVICE_INFO_MAGIC;
  info.Version = AGX_WIN32_DEVICE_INFO_VERSION;
  info.Bytes = sizeof(info);
  info.BootGeneration = 5u;
  info.GpuGeneration = 13u;
  info.GpuVariant = AgxWin32GpuG13G;
  info.PageBytes = 0x4000u;
  info.ClassCount = 3u;
  info.Classes[0] = (AGX_WIN32_BUFFER_CLASS_INFO){
      AgxWin32BufferClassGeneral, 0x4000u, 0x1000000ULL, 0xfu};
  info.Classes[1] = (AGX_WIN32_BUFFER_CLASS_INFO){
      AgxWin32BufferClassShader, 0x4000u, 0x1000000ULL,
      AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead};
  info.Classes[2] = (AGX_WIN32_BUFFER_CLASS_INFO){
      AgxWin32BufferClassEncoder, 0x4000u, 0x1000000ULL,
      AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead};
  return info;
}

int main(void) {
  static FAKE_PIPE fake;
  AGX_WIN32_SCREEN screen;
  AGX_WIN32_SCREEN_OPERATIONS screenOps = {query, create_class};
  AGX_WIN32_WINSYS_OPERATIONS transportOps = {
      create_general, map, unmap, destroy, fail_submit, fail_wait, retire};
  struct pipe_screen *pipe;
  struct pipe_context *context;
  struct pipe_resource bufferTemplate;
  struct pipe_resource textureTemplate;
  struct pipe_resource invalidTemplate;
  struct pipe_resource *buffer;
  struct pipe_resource *texture;
  struct pipe_transfer *transfer = NULL;
  struct pipe_box box;
  unsigned char *address;

  memset(&fake, 0, sizeof(fake));
  fake.Info = device_info();
  {
    AGX_WIN32_BUFFER_CLASS_INFO swap = fake.Info.Classes[0];
    fake.Info.Classes[0] = fake.Info.Classes[2];
    fake.Info.Classes[2] = swap;
  }
  assert(AgxWin32ScreenInitialize(&screen, &fake, 7u, &transportOps,
                                  &screenOps) == AgxWin32ScreenSuccess);
  pipe = AgxWin32PipeScreenCreate(&screen);
  assert(pipe != NULL && pipe->get_screen_fd == NULL);
  assert(strcmp(pipe->get_name(pipe), "Apple AGX G13G (Windows)") == 0);
  assert(strcmp(pipe->get_vendor(pipe), "Mesa") == 0);
  assert(strcmp(pipe->get_device_vendor(pipe), "Apple") == 0);
  assert(pipe->is_format_supported(
      pipe, PIPE_FORMAT_B8G8R8A8_UNORM, PIPE_TEXTURE_2D, 1u, 1u,
      PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW));
  context = pipe->context_create(pipe, &fake, 0u);
  assert(context != NULL && context->priv == &fake);

  memset(&bufferTemplate, 0, sizeof(bufferTemplate));
  bufferTemplate.target = PIPE_BUFFER;
  bufferTemplate.format = PIPE_FORMAT_R8_UNORM;
  bufferTemplate.width0 = 0x8000u;
  bufferTemplate.height0 = 1u;
  bufferTemplate.depth0 = 1u;
  bufferTemplate.array_size = 1u;
  bufferTemplate.bind = PIPE_BIND_VERTEX_BUFFER;
  assert(pipe->can_create_resource(pipe, &bufferTemplate));
  buffer = pipe->resource_create(pipe, &bufferTemplate);
  assert(buffer != NULL && AgxWin32PipeResourceBuffer(buffer) != NULL);
  /* Device teardown must retain both objects and their runtime owner while
   * a resource still depends on this screen. */
  assert(!AgxWin32PipeScreenReleaseDevice(pipe, context));
  assert(context->screen == pipe && screen.Active);
  u_box_1d(0x1000u, 0x2000u, &box);
  address = context->buffer_map(
      context, buffer, 0u, PIPE_MAP_WRITE, &box, &transfer);
  assert(address == fake.Storage[0] + 0x1000u && transfer != NULL);
  memset(address, 0x5a, 0x2000u);
  context->buffer_unmap(context, transfer);

  memset(&textureTemplate, 0, sizeof(textureTemplate));
  textureTemplate.target = PIPE_TEXTURE_2D;
  textureTemplate.format = PIPE_FORMAT_B8G8R8A8_UNORM;
  textureTemplate.width0 = 64u;
  textureTemplate.height0 = 64u;
  textureTemplate.depth0 = 1u;
  textureTemplate.array_size = 1u;
  textureTemplate.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
  texture = pipe->resource_create(pipe, &textureTemplate);
  assert(texture != NULL);
  u_box_2d(4u, 8u, 16u, 4u, &box);
  address = context->texture_map(
      context, texture, 0u, PIPE_MAP_READ_WRITE, &box, &transfer);
  assert(address == fake.Storage[1] + 8u * 256u + 4u * 4u);
  assert(transfer != NULL && transfer->stride == 256u);
  context->texture_unmap(context, transfer);

  invalidTemplate = textureTemplate;
  invalidTemplate.target = PIPE_TEXTURE_3D;
  assert(!pipe->can_create_resource(pipe, &invalidTemplate));
  assert(pipe->resource_create(pipe, &invalidTemplate) == NULL);
  pipe->resource_destroy(pipe, texture);
  pipe->resource_destroy(pipe, buffer);
  {
    struct pipe_context *extra = pipe->context_create(pipe, &fake, 0u);
    struct pipe_screen *other = AgxWin32PipeScreenCreate(&screen);
    assert(other != NULL);
    struct pipe_context *foreign = other->context_create(other, &fake, 0u);
    assert(extra != NULL && foreign != NULL);
    assert(!AgxWin32PipeScreenReleaseDevice(pipe, foreign));
    assert(!AgxWin32PipeScreenReleaseDevice(pipe, context));
    assert(context->screen == pipe && extra->screen == pipe);
    assert(AgxWin32PipeScreenReleaseDevice(other, foreign));
    extra->destroy(extra);
  }
  p_atomic_inc(&pipe->refcnt);
  assert(!AgxWin32PipeScreenReleaseDevice(pipe, context));
  p_atomic_dec(&pipe->refcnt);
  assert(AgxWin32PipeScreenReleaseDevice(pipe, context));
  assert(screen.Active); /* Windows runtime lifetime belongs to the caller. */
  assert(fake.Creates == 2u && fake.Maps == 2u && fake.Unmaps == 2u &&
         fake.Destroys == 2u);
  return 0;
}
