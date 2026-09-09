#include "agx_win32_pipe_screen.h"

#include "pipe/p_state.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define AGX_WIN32_PIPE_SCREEN_MAGIC 0x53505741u /* AWPS */
#define AGX_WIN32_PIPE_CONTEXT_MAGIC 0x43505741u /* AWPC */
#define AGX_WIN32_PIPE_RESOURCE_MAGIC 0x52505741u /* AWPR */

typedef struct _AGX_WIN32_PIPE_SCREEN {
  struct pipe_screen Base;
  AGX_WIN32_SCREEN *Screen;
  uint32_t Magic;
  uint32_t Contexts;
  uint32_t Resources;
  AGX_WIN32_SCREEN_RESULT LastResult;
} AGX_WIN32_PIPE_SCREEN;

typedef struct _AGX_WIN32_PIPE_CONTEXT {
  struct pipe_context Base;
  AGX_WIN32_PIPE_SCREEN *Screen;
  uint32_t Magic;
} AGX_WIN32_PIPE_CONTEXT;

typedef struct _AGX_WIN32_PIPE_RESOURCE {
  struct pipe_resource Base;
  AGX_WIN32_PIPE_SCREEN *Screen;
  AGX_WIN32_SCREEN_BUFFER Buffer;
  uint64_t AllocationBytes;
  uint32_t Pitch;
  uint32_t BytesPerPixel;
  uint32_t Magic;
} AGX_WIN32_PIPE_RESOURCE;

static AGX_WIN32_PIPE_SCREEN *pipe_screen_cast(struct pipe_screen *Base) {
  AGX_WIN32_PIPE_SCREEN *screen = (AGX_WIN32_PIPE_SCREEN *)Base;
  return screen != NULL && screen->Magic == AGX_WIN32_PIPE_SCREEN_MAGIC
             ? screen : NULL;
}

static AGX_WIN32_PIPE_CONTEXT *pipe_context_cast(struct pipe_context *Base) {
  AGX_WIN32_PIPE_CONTEXT *context = (AGX_WIN32_PIPE_CONTEXT *)Base;
  return context != NULL && context->Magic == AGX_WIN32_PIPE_CONTEXT_MAGIC
             ? context : NULL;
}

static AGX_WIN32_PIPE_RESOURCE *pipe_resource_cast(
    struct pipe_resource *Base) {
  AGX_WIN32_PIPE_RESOURCE *resource = (AGX_WIN32_PIPE_RESOURCE *)Base;
  return resource != NULL && resource->Magic == AGX_WIN32_PIPE_RESOURCE_MAGIC
             ? resource : NULL;
}

static const AGX_WIN32_BUFFER_CLASS_INFO *pipe_buffer_class(
    const AGX_WIN32_SCREEN *Screen, APPLE_AGX_U32 ClassId) {
  APPLE_AGX_U32 index;
  if (Screen == NULL)
    return NULL;
  for (index = 0u; index < Screen->Info.ClassCount; ++index)
    if (Screen->Info.Classes[index].ClassId == ClassId)
      return &Screen->Info.Classes[index];
  return NULL;
}

static int checked_align(uint64_t Value, uint64_t Alignment,
                         uint64_t *Aligned) {
  uint64_t mask;
  if (Aligned == NULL || Alignment == 0ULL ||
      (Alignment & (Alignment - 1ULL)) != 0ULL)
    return 0;
  mask = Alignment - 1ULL;
  if (Value == 0ULL || Value > UINT64_MAX - mask)
    return 0;
  *Aligned = (Value + mask) & ~mask;
  return *Aligned != 0ULL;
}

static const char *pipe_get_name(struct pipe_screen *Base) {
  return pipe_screen_cast(Base) != NULL ? "Apple AGX G13G (Windows)" : NULL;
}

static const char *pipe_get_vendor(struct pipe_screen *Base) {
  return pipe_screen_cast(Base) != NULL ? "Mesa" : NULL;
}

static const char *pipe_get_device_vendor(struct pipe_screen *Base) {
  return pipe_screen_cast(Base) != NULL ? "Apple" : NULL;
}

static int pipe_resource_layout(const struct pipe_resource *Template,
                                uint64_t PageBytes, uint32_t *Pitch,
                                uint32_t *BytesPerPixel,
                                uint64_t *AllocationBytes) {
  uint64_t pitch;
  uint64_t bytes;
  if (Template == NULL || Pitch == NULL || BytesPerPixel == NULL ||
      AllocationBytes == NULL || Template->width0 == 0u ||
      Template->last_level != 0u || Template->depth0 != 1u ||
      Template->array_size != 1u || Template->nr_samples > 1u ||
      Template->nr_storage_samples > 1u || Template->next != NULL)
    return 0;
  if (Template->target == PIPE_BUFFER) {
    if (Template->height0 != 1u || Template->format != PIPE_FORMAT_R8_UNORM ||
        (Template->bind & ~(PIPE_BIND_VERTEX_BUFFER |
                            PIPE_BIND_INDEX_BUFFER |
                            PIPE_BIND_CONSTANT_BUFFER |
                            PIPE_BIND_SAMPLER_VIEW |
                            PIPE_BIND_VERTEX_STATE |
                            PIPE_BIND_STREAM_OUTPUT)) != 0u)
      return 0;
    pitch = Template->width0;
    *BytesPerPixel = 1u;
  } else if (Template->target == PIPE_TEXTURE_2D) {
    if (Template->height0 == 0u ||
        Template->format != PIPE_FORMAT_B8G8R8A8_UNORM ||
        (Template->bind & ~(PIPE_BIND_RENDER_TARGET |
                            PIPE_BIND_SAMPLER_VIEW)) != 0u)
      return 0;
    pitch = (uint64_t)Template->width0 * 4ULL;
    if (pitch > UINT32_MAX - 15u)
      return 0;
    pitch = (pitch + 15ULL) & ~15ULL;
    *BytesPerPixel = 4u;
  } else {
    return 0;
  }
  if (pitch > UINT32_MAX || Template->height0 > UINT64_MAX / pitch)
    return 0;
  bytes = pitch * Template->height0;
  if (!checked_align(bytes, PageBytes, AllocationBytes))
    return 0;
  *Pitch = (uint32_t)pitch;
  return 1;
}

static bool pipe_is_format_supported(struct pipe_screen *Base,
                                     enum pipe_format Format,
                                     enum pipe_texture_target Target,
                                     unsigned SampleCount,
                                     unsigned StorageSampleCount,
                                     unsigned Bindings) {
  if (pipe_screen_cast(Base) == NULL || SampleCount > 1u ||
      StorageSampleCount > 1u)
    return false;
  if (Target == PIPE_BUFFER)
    return Format == PIPE_FORMAT_R8_UNORM;
  return Target == PIPE_TEXTURE_2D &&
         Format == PIPE_FORMAT_B8G8R8A8_UNORM &&
         (Bindings & ~(PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW)) == 0u;
}

static bool pipe_can_create_resource(struct pipe_screen *Base,
                                     const struct pipe_resource *Template) {
  AGX_WIN32_PIPE_SCREEN *screen = pipe_screen_cast(Base);
  uint64_t allocationBytes;
  uint32_t pitch;
  uint32_t bpp;
  const AGX_WIN32_BUFFER_CLASS_INFO *classInfo = screen == NULL
      ? NULL : pipe_buffer_class(screen->Screen, AgxWin32BufferClassGeneral);
  return screen != NULL && classInfo != NULL &&
         pipe_resource_layout(Template, screen->Screen->Info.PageBytes,
                              &pitch, &bpp, &allocationBytes) &&
         allocationBytes <= classInfo->MaximumBytes;
}

static struct pipe_resource *pipe_resource_create(
    struct pipe_screen *Base, const struct pipe_resource *Template) {
  AGX_WIN32_PIPE_SCREEN *screen = pipe_screen_cast(Base);
  AGX_WIN32_PIPE_RESOURCE *resource;
  uint64_t allocationBytes;
  uint32_t pitch;
  uint32_t bpp;
  AGX_WIN32_SCREEN_RESULT result;
  const AGX_WIN32_BUFFER_CLASS_INFO *classInfo = screen == NULL
      ? NULL : pipe_buffer_class(screen->Screen, AgxWin32BufferClassGeneral);
  if (screen == NULL || classInfo == NULL ||
      !pipe_resource_layout(Template, screen->Screen->Info.PageBytes,
                            &pitch, &bpp, &allocationBytes))
    return NULL;
  resource = (AGX_WIN32_PIPE_RESOURCE *)calloc(1u, sizeof(*resource));
  if (resource == NULL)
    return NULL;
  result = AgxWin32ScreenCreateBuffer(
      screen->Screen, AgxWin32BufferClassGeneral, allocationBytes,
      classInfo->MinimumAlignment,
      AppleAgxWin32BufferCpuRead | AppleAgxWin32BufferCpuWrite |
          AppleAgxWin32BufferGpuRead | AppleAgxWin32BufferGpuWrite,
      &resource->Buffer);
  if (result != AgxWin32ScreenSuccess) {
    screen->LastResult = result;
    free(resource);
    return NULL;
  }
  resource->Base = *Template;
  resource->Base.reference.count = 1;
  resource->Base.screen = Base;
  resource->Screen = screen;
  resource->AllocationBytes = allocationBytes;
  resource->Pitch = pitch;
  resource->BytesPerPixel = bpp;
  resource->Magic = AGX_WIN32_PIPE_RESOURCE_MAGIC;
  ++screen->Resources;
  screen->LastResult = AgxWin32ScreenSuccess;
  return &resource->Base;
}

static void pipe_resource_destroy(struct pipe_screen *Base,
                                  struct pipe_resource *Resource) {
  AGX_WIN32_PIPE_SCREEN *screen = pipe_screen_cast(Base);
  AGX_WIN32_PIPE_RESOURCE *resource = pipe_resource_cast(Resource);
  AGX_WIN32_SCREEN_RESULT result;
  if (screen == NULL || resource == NULL || resource->Screen != screen)
    return;
  result = AgxWin32ScreenDestroyBuffer(screen->Screen, &resource->Buffer);
  screen->LastResult = result;
  if (result != AgxWin32ScreenSuccess)
    return;
  resource->Magic = 0u;
  if (screen->Resources != 0u)
    --screen->Resources;
  free(resource);
}

static int pipe_transfer_range(AGX_WIN32_PIPE_RESOURCE *Resource,
                               const struct pipe_box *Box,
                               uint64_t *Offset, uint64_t *Bytes) {
  uint64_t offset;
  uint64_t bytes;
  if (Resource == NULL || Box == NULL || Offset == NULL || Bytes == NULL ||
      Box->x < 0 || Box->y < 0 || Box->z != 0 || Box->width <= 0 ||
      Box->height <= 0 || Box->depth != 1)
    return 0;
  if (Resource->Base.target == PIPE_BUFFER) {
    if (Box->y != 0 || Box->height != 1 ||
        (uint32_t)Box->x > Resource->Base.width0 ||
        (uint32_t)Box->width > Resource->Base.width0 - (uint32_t)Box->x)
      return 0;
    offset = (uint32_t)Box->x;
    bytes = (uint32_t)Box->width;
  } else {
    if ((uint32_t)Box->x > Resource->Base.width0 ||
        (uint32_t)Box->width > Resource->Base.width0 - (uint32_t)Box->x ||
        (uint32_t)Box->y > Resource->Base.height0 ||
        (uint32_t)Box->height > Resource->Base.height0 - (uint32_t)Box->y)
      return 0;
    offset = (uint64_t)(uint32_t)Box->y * Resource->Pitch +
             (uint64_t)(uint32_t)Box->x * Resource->BytesPerPixel;
    bytes = (uint64_t)((uint32_t)Box->height - 1u) * Resource->Pitch +
            (uint64_t)(uint32_t)Box->width * Resource->BytesPerPixel;
  }
  if (offset > Resource->AllocationBytes ||
      bytes > Resource->AllocationBytes - offset)
    return 0;
  *Offset = offset;
  *Bytes = bytes;
  return 1;
}

static void *pipe_map_common(struct pipe_context *Base,
                             struct pipe_resource *Resource, unsigned Level,
                             unsigned Usage, const struct pipe_box *Box,
                             struct pipe_transfer **OutTransfer) {
  AGX_WIN32_PIPE_CONTEXT *context = pipe_context_cast(Base);
  AGX_WIN32_PIPE_RESOURCE *resource = pipe_resource_cast(Resource);
  struct pipe_transfer *transfer;
  AGX_WIN32_SCREEN_RESULT result;
  APPLE_AGX_U32 access = 0u;
  uint64_t offset;
  uint64_t bytes;
  void *address = NULL;
  if (OutTransfer == NULL)
    return NULL;
  *OutTransfer = NULL;
  if (context == NULL || resource == NULL ||
      resource->Screen != context->Screen || Level != 0u ||
      (Usage & ~(PIPE_MAP_READ | PIPE_MAP_WRITE)) != 0u ||
      (Usage & (PIPE_MAP_READ | PIPE_MAP_WRITE)) == 0u ||
      !pipe_transfer_range(resource, Box, &offset, &bytes))
    return NULL;
  if ((Usage & PIPE_MAP_READ) != 0u)
    access |= AppleAgxWin32BufferCpuRead;
  if ((Usage & PIPE_MAP_WRITE) != 0u)
    access |= AppleAgxWin32BufferCpuWrite;
  transfer = (struct pipe_transfer *)calloc(1u, sizeof(*transfer));
  if (transfer == NULL)
    return NULL;
  result = AgxWin32ScreenMapBuffer(context->Screen->Screen,
                                   &resource->Buffer, offset, bytes,
                                   access, &address);
  context->Screen->LastResult = result;
  if (result != AgxWin32ScreenSuccess) {
    free(transfer);
    return NULL;
  }
  transfer->resource = Resource;
  transfer->usage = Usage;
  transfer->level = 0u;
  transfer->box = *Box;
  transfer->stride = resource->Pitch;
  transfer->layer_stride = (uintptr_t)resource->Pitch * Resource->height0;
  transfer->offset = (unsigned)offset;
  *OutTransfer = transfer;
  return address;
}

static void pipe_unmap_common(struct pipe_context *Base,
                              struct pipe_transfer *Transfer) {
  AGX_WIN32_PIPE_CONTEXT *context = pipe_context_cast(Base);
  AGX_WIN32_PIPE_RESOURCE *resource = Transfer == NULL
      ? NULL : pipe_resource_cast(Transfer->resource);
  if (context == NULL || resource == NULL ||
      resource->Screen != context->Screen)
    return;
  context->Screen->LastResult = AgxWin32ScreenUnmapBuffer(
      context->Screen->Screen, &resource->Buffer);
  if (context->Screen->LastResult == AgxWin32ScreenSuccess)
    free(Transfer);
}

static void pipe_context_destroy(struct pipe_context *Base) {
  AGX_WIN32_PIPE_CONTEXT *context = pipe_context_cast(Base);
  if (context == NULL)
    return;
  if (context->Screen->Contexts != 0u)
    --context->Screen->Contexts;
  context->Magic = 0u;
  free(context);
}

static struct pipe_context *pipe_context_create(struct pipe_screen *Base,
                                                void *Priv,
                                                unsigned Flags) {
  AGX_WIN32_PIPE_SCREEN *screen = pipe_screen_cast(Base);
  AGX_WIN32_PIPE_CONTEXT *context;
  if (screen == NULL || Flags != 0u)
    return NULL;
  context = (AGX_WIN32_PIPE_CONTEXT *)calloc(1u, sizeof(*context));
  if (context == NULL)
    return NULL;
  context->Base.screen = Base;
  context->Base.priv = Priv;
  context->Base.destroy = pipe_context_destroy;
  context->Base.buffer_map = pipe_map_common;
  context->Base.buffer_unmap = pipe_unmap_common;
  context->Base.texture_map = pipe_map_common;
  context->Base.texture_unmap = pipe_unmap_common;
  context->Screen = screen;
  context->Magic = AGX_WIN32_PIPE_CONTEXT_MAGIC;
  ++screen->Contexts;
  return &context->Base;
}

static void pipe_screen_destroy(struct pipe_screen *Base) {
  AGX_WIN32_PIPE_SCREEN *screen = pipe_screen_cast(Base);
  if (screen == NULL || screen->Contexts != 0u || screen->Resources != 0u)
    return;
  screen->Magic = 0u;
  free(screen);
}

int AgxWin32PipeScreenReleaseDevice(struct pipe_screen *Base,
                                   struct pipe_context *OwnedContext) {
  AGX_WIN32_PIPE_SCREEN *screen = pipe_screen_cast(Base);
  AGX_WIN32_PIPE_CONTEXT *context = pipe_context_cast(OwnedContext);
  if (screen == NULL || context == NULL || context->Screen != screen ||
      screen->Contexts != 1u || screen->Resources != 0u ||
      screen->Base.refcnt != 1)
    return 0;
  pipe_context_destroy(OwnedContext);
  pipe_screen_destroy(Base);
  return 1;
}

struct pipe_screen *AgxWin32PipeScreenCreate(AGX_WIN32_SCREEN *Screen) {
  AGX_WIN32_PIPE_SCREEN *screen;
  if (Screen == NULL || !Screen->Active ||
      !AgxWin32DeviceInfoValid(&Screen->Info))
    return NULL;
  screen = (AGX_WIN32_PIPE_SCREEN *)calloc(1u, sizeof(*screen));
  if (screen == NULL)
    return NULL;
  screen->Base.refcnt = 1;
  screen->Base.winsys_priv = Screen;
  screen->Base.destroy = pipe_screen_destroy;
  screen->Base.get_name = pipe_get_name;
  screen->Base.get_vendor = pipe_get_vendor;
  screen->Base.get_device_vendor = pipe_get_device_vendor;
  screen->Base.context_create = pipe_context_create;
  screen->Base.is_format_supported = pipe_is_format_supported;
  screen->Base.can_create_resource = pipe_can_create_resource;
  screen->Base.resource_create = pipe_resource_create;
  screen->Base.resource_destroy = pipe_resource_destroy;
  screen->Screen = Screen;
  screen->Magic = AGX_WIN32_PIPE_SCREEN_MAGIC;
  screen->LastResult = AgxWin32ScreenSuccess;
  return &screen->Base;
}

int AgxWin32PipeDeviceInitialize(AGX_WIN32_PIPE_DEVICE *Device,
                                AGX_WIN32_SCREEN *Runtime) {
  AGX_WIN32_PIPE_DEVICE candidate;
  if (Device == NULL || Runtime == NULL || Runtime->Context == NULL ||
      Runtime->Generation == 0u ||
      Device->Runtime != NULL || Device->Screen != NULL ||
      Device->Context != NULL || Device->Generation != 0u)
    return 0;
  memset(&candidate, 0, sizeof(candidate));
  candidate.Screen = AgxWin32PipeScreenCreate(Runtime);
  if (candidate.Screen == NULL)
    return 0;
  candidate.Context = candidate.Screen->context_create(
      candidate.Screen, Runtime->Context, 0u);
  if (candidate.Context == NULL) {
    candidate.Screen->destroy(candidate.Screen);
    return 0;
  }
  candidate.Runtime = Runtime;
  candidate.Generation = Runtime->Generation;
  *Device = candidate;
  return 1;
}

int AgxWin32PipeDeviceClose(AGX_WIN32_PIPE_DEVICE *Device) {
  if (Device == NULL)
    return 0;
  if (Device->Runtime == NULL && Device->Screen == NULL &&
      Device->Context == NULL && Device->Generation == 0u)
    return 1;
  if (Device->Runtime == NULL || Device->Screen == NULL ||
      Device->Context == NULL ||
      Device->Generation != Device->Runtime->Generation ||
      Device->Screen->winsys_priv != Device->Runtime ||
      Device->Context->priv != Device->Runtime->Context ||
      !AgxWin32PipeScreenReleaseDevice(Device->Screen, Device->Context))
    return 0;
  memset(Device, 0, sizeof(*Device));
  return 1;
}

AGX_WIN32_SCREEN_BUFFER *AgxWin32PipeResourceBuffer(
    struct pipe_resource *Resource) {
  AGX_WIN32_PIPE_RESOURCE *resource = pipe_resource_cast(Resource);
  return resource != NULL ? &resource->Buffer : NULL;
}
