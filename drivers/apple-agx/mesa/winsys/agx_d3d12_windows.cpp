#include <windows.h>
#include <string.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)
extern "C" {
#include "umd_internal.h"
#include "umd_asahi_owner.h"
#include "umd_asahi_batch_adapter.h"
#include "render_win32_transport.h"
}
#include "agx_win32_asahi_scene.h"
#include "agx_d3d12_windows.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "../dxbc/dxbc_to_nir.h"
#include "util/format/u_format.h"

/* The D3D10 frontend's DXGI -> pipe format table (gallium/frontends/d3d10umd
 * Format.cpp, linked in the native runtime archive). */
enum pipe_format FormatTranslate(DXGI_FORMAT Format, BOOL depth);

/* D3D12 C4b owner. Construction mirrors agx_d3d10_windows.cpp
 * (AdmissionUmdRuntimeDeviceInitialize -> AgxWin32AsahiScreenCreateForWindows
 * -> AgxWin32AsahiContextCreate) with two differences: the UMD device storage
 * sits on a GPU-VA kernel context this owner creates for the D3D12 device
 * (AdmissionUmdRuntimeDeviceInitializeForQueue borrows it), and errors go to
 * the D3D12 runtime through the caller's callback. */
struct AGX_D3D12_OWNER {
  ADMISSION_UMD_ADAPTER Adapter;
  ADMISSION_UMD_DEVICE Runtime;
  ADMISSION_UMD_ASAHI_OWNER Owner;
  AGX_WIN32_ASAHI_BACKEND Backend;
  AGX_WIN32_ASAHI_OWNER_OPS OwnerOperations;
  struct pipe_screen *Screen;
  struct pipe_context *Context;
  const D3DDDI_DEVICECALLBACKS *Kernel;
  HANDLE RuntimeDevice;
  HANDLE KernelContext;
  AGX_D3D12_ERROR_CALLBACK Error;
  void *ErrorContext;
  UINT PendingCopies;
  UINT PendingDispatches;
};

static VOID APIENTRY owner_error(D3D10DDI_HRTCORELAYER handle, HRESULT error) {
  AGX_D3D12_OWNER *owner = static_cast<AGX_D3D12_OWNER *>(handle.handle);
  if (owner && owner->Error) owner->Error(owner->ErrorContext, error);
}

static void receipt(PCSTR stage, HRESULT status, UINT a, UINT b, UINT c) {
  const UINT values[] = {a, b, c};
  AdmissionUmdDiagnostic(stage, status, values, 3u);
}

static HRESULT destroy_kernel_context(AGX_D3D12_OWNER *owner) {
  if (!owner->KernelContext) return S_OK;
  D3DDDICB_DESTROYCONTEXT destroy = {};
  destroy.hContext = owner->KernelContext;
  HRESULT hr = owner->Kernel->pfnDestroyContextCb(owner->RuntimeDevice, &destroy);
  if (SUCCEEDED(hr)) owner->KernelContext = NULL;
  return hr;
}

extern "C" HRESULT AgxD3d12OwnerDestroy(AGX_D3D12_OWNER *owner) {
  if (!owner) return S_OK;
  BOOL consumed = FALSE;
  HRESULT hr = S_OK;
  owner->Backend.Closing = 1;
  if (owner->Context) {
    if (!AgxWin32AsahiContextDestroy(owner->Context)) return HRESULT_FROM_WIN32(ERROR_BUSY);
    owner->Context = NULL;
  }
  if (owner->Screen) {
    if (!AgxWin32AsahiScreenDestroy(owner->Screen)) return HRESULT_FROM_WIN32(ERROR_BUSY);
    owner->Screen = NULL;
  }
  if (owner->Runtime.Magic == ADMISSION_UMD_DEVICE_MAGIC) {
    hr = AdmissionUmdScreenBeginClose(&owner->Runtime);
    if (FAILED(hr)) return hr;
    hr = AdmissionUmdRuntimeDeviceFinalize(&owner->Runtime, &consumed);
    if (!consumed) return FAILED(hr) ? hr : E_FAIL;
  }
  hr = destroy_kernel_context(owner);
  if (FAILED(hr)) return hr;
  HeapFree(GetProcessHeap(), 0, owner);
  return S_OK;
}

extern "C" HRESULT AgxD3d12OwnerCreate(const AGX_D3D12_OWNER_ARGS *args, AGX_D3D12_OWNER **out) {
  if (!args || !out || !args->AdapterCallbacks || !args->KernelCallbacks || !args->RuntimeDevice ||
      !args->Generation)
    return E_INVALIDARG;
  *out = NULL;
  auto *owner = static_cast<AGX_D3D12_OWNER *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(AGX_D3D12_OWNER)));
  if (!owner) return E_OUTOFMEMORY;
  owner->Kernel = static_cast<const D3DDDI_DEVICECALLBACKS *>(args->KernelCallbacks);
  owner->RuntimeDevice = args->RuntimeDevice;
  owner->Error = args->Error;
  owner->ErrorContext = args->ErrorContext;
  HRESULT hr;
  {
    D3D10DDIARG_OPENADAPTER open = {};
    open.hRTAdapter.handle = args->RuntimeAdapter;
    open.pAdapterCallbacks = static_cast<const D3DDDI_ADAPTERCALLBACKS *>(args->AdapterCallbacks);
    hr = AdmissionUmdRuntimeAdapterInitialize(&owner->Adapter, &open);
  }
  if (SUCCEEDED(hr) && !owner->Kernel->pfnCreateContextVirtualCb) hr = E_INVALIDARG;
  if (SUCCEEDED(hr)) {
    ADMISSION_WIN32_CONTEXT_CREATE contract = {};
    contract.Magic = ADMISSION_WIN32_CONTEXT_MAGIC;
    contract.Version = ADMISSION_WIN32_CONTEXT_VERSION;
    contract.Bytes = sizeof(contract);
    contract.Generation = args->Generation;
    D3DDDICB_CREATECONTEXTVIRTUAL context = {};
    context.NodeOrdinal = 0;
    context.EngineAffinity = 1;
    context.pPrivateDriverData = &contract;
    context.PrivateDriverDataSize = sizeof(contract);
    hr = owner->Kernel->pfnCreateContextVirtualCb(owner->RuntimeDevice, &context);
    owner->KernelContext = SUCCEEDED(hr) ? context.hContext : NULL;
    if (SUCCEEDED(hr) && !owner->KernelContext) hr = E_FAIL;
  }
  if (SUCCEEDED(hr)) {
    D3D10DDI_HRTCORELAYER error = {};
    error.handle = owner;
    hr = AdmissionUmdRuntimeDeviceInitializeForQueue(&owner->Runtime, &owner->Adapter, owner->Kernel,
                                                    owner->RuntimeDevice, error, owner_error,
                                                    owner->KernelContext, args->Generation);
  }
  if (SUCCEEDED(hr)) {
    owner->Owner.Device = &owner->Runtime;
    owner->Owner.Backend = &owner->Backend;
    AdmissionUmdAsahiOwnerOperations(&owner->OwnerOperations);
    owner->Backend.ZlsAttachments = 1;
    owner->Backend.D3dRobustness = 1;
    owner->Screen = AgxWin32AsahiScreenCreateForWindows(&owner->Backend, &owner->Runtime.Screen,
                                                       &owner->OwnerOperations, &owner->Owner,
                                                       AdmissionUmdAsahiBatchOperations());
    if (!owner->Screen) hr = FAILED(owner->Runtime.LastScreenError) ? owner->Runtime.LastScreenError : E_OUTOFMEMORY;
  }
  if (SUCCEEDED(hr)) {
    owner->Context = AgxWin32AsahiContextCreate(owner->Screen, &owner->Owner);
    if (!owner->Context) hr = E_OUTOFMEMORY;
  }
  receipt("measure-d3d12-owner", hr, owner->KernelContext != NULL ? 1u : 0u, owner->Screen != NULL ? 1u : 0u,
          owner->Context != NULL ? 1u : 0u);
  if (FAILED(hr)) {
    (void)AgxD3d12OwnerDestroy(owner);
    return hr;
  }
  *out = owner;
  return S_OK;
}

extern "C" HRESULT AgxD3d12OwnerCreateBuffer(AGX_D3D12_OWNER *owner, unsigned long long bytes,
                                             AGX_D3D12_BUFFER *buffer) {
  if (!owner || !owner->Screen || !owner->Context || !buffer || !bytes || bytes > 0xffffffffull)
    return E_INVALIDARG;
  memset(buffer, 0, sizeof(*buffer));
  struct pipe_resource templ = {};
  templ.target = PIPE_BUFFER;
  templ.format = PIPE_FORMAT_R8_UNORM;
  templ.width0 = static_cast<unsigned>(bytes);
  templ.height0 = 1;
  templ.depth0 = 1;
  templ.array_size = 1;
  templ.usage = PIPE_USAGE_STAGING;
  templ.bind = PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER | PIPE_BIND_CONSTANT_BUFFER |
               PIPE_BIND_SHADER_BUFFER | PIPE_BIND_SAMPLER_VIEW;
  struct pipe_resource *resource = owner->Screen->resource_create(owner->Screen, &templ);
  if (!resource) return E_OUTOFMEMORY;
  void *cpu = AgxWin32AsahiBufferWriteMap(owner->Context, resource);
  unsigned long long va = AgxWin32AsahiBufferGpuAddress(resource);
  if (!cpu || !va) {
    AgxWin32AsahiResourceRelease(&resource);
    return E_OUTOFMEMORY;
  }
  buffer->Resource = resource;
  buffer->GpuVa = va;
  buffer->Cpu = cpu;
  buffer->Bytes = bytes;
  return S_OK;
}

extern "C" void AgxD3d12OwnerDestroyBuffer(AGX_D3D12_OWNER *owner, AGX_D3D12_BUFFER *buffer) {
  if (!owner || !buffer || !buffer->Resource) return;
  struct pipe_resource *resource = static_cast<struct pipe_resource *>(buffer->Resource);
  AgxWin32AsahiResourceRelease(&resource);
  memset(buffer, 0, sizeof(*buffer));
}

extern "C" UINT AgxD3d12OwnerBufferAllocation(AGX_D3D12_OWNER *owner, const AGX_D3D12_BUFFER *buffer) {
  if (!owner || !buffer || !buffer->Resource) return 0;
  AGX_WIN32_RELOC_ALLOCATION identity = {};
  if (!AgxWin32AsahiResourceIdentity(static_cast<struct pipe_resource *>(buffer->Resource), &identity)) return 0;
  return AdmissionUmdScreenKernelAllocation(&owner->Runtime, identity.Token);
}

extern "C" HRESULT AgxD3d12OwnerCopy(AGX_D3D12_OWNER *owner, const AGX_D3D12_BUFFER *dst,
                                     unsigned long long dst_offset, const AGX_D3D12_BUFFER *src,
                                     unsigned long long src_offset, unsigned long long bytes) {
  if (!owner || !owner->Context || !dst || !src || !dst->Resource || !src->Resource || !bytes ||
      dst_offset > dst->Bytes || bytes > dst->Bytes - dst_offset || src_offset > src->Bytes ||
      bytes > src->Bytes - src_offset || dst->Bytes > 0x7fffffffull || src->Bytes > 0x7fffffffull)
    return E_INVALIDARG;
  struct pipe_box box = {}; /* u_box_1d: the UMD Mesa tree has no util/u_box.h */
  box.x = static_cast<int>(src_offset);
  box.width = static_cast<int>(bytes);
  box.height = 1;
  box.depth = 1;
  owner->Context->resource_copy_region(owner->Context, static_cast<struct pipe_resource *>(dst->Resource), 0,
                                       static_cast<unsigned>(dst_offset), 0, 0,
                                       static_cast<struct pipe_resource *>(src->Resource), 0, &box);
  ++owner->PendingCopies;
  return S_OK;
}

/* C5: the D3D11 frontend's compute path (agx_d3d11_ddi.cpp
 * CreateComputeShader11/Dispatch11) on this owner's context. */
extern "C" HRESULT AgxD3d12OwnerCreateCompute(AGX_D3D12_OWNER *owner, const UINT *tokens,
                                              AGX_D3D12_COMPUTE *compute) {
  if (!owner || !owner->Context || !tokens || !compute) return E_INVALIDARG;
  memset(compute, 0, sizeof(*compute));
  struct pipe_context *pipe = owner->Context;
  struct dxbc_nir_error error = {};
  struct nir_shader *nir = dxbc_translate_tokens(tokens, pipe->screen->nir_options[MESA_SHADER_COMPUTE], &error);
  if (!nir) {
    receipt("measure-d3d12-compute", E_NOTIMPL, 0u, error.opcode, error.offset);
    return E_NOTIMPL;
  }
  unsigned block[3] = {}, shared = 0;
  dxbc_nir_compute_info(nir, block, &shared);
  struct pipe_compute_state state = {};
  state.ir_type = PIPE_SHADER_IR_NIR;
  state.prog = nir;
  state.static_shared_mem = shared;
  compute->State = pipe->create_compute_state(pipe, &state);
  const HRESULT hr = compute->State && block[0] && block[1] && block[2] ? S_OK : E_OUTOFMEMORY;
  receipt("measure-d3d12-compute", hr, block[0], block[1], block[2]);
  if (FAILED(hr)) {
    if (compute->State) pipe->delete_compute_state(pipe, compute->State);
    memset(compute, 0, sizeof(*compute));
    return hr;
  }
  for (unsigned i = 0; i < 3; ++i) compute->Block[i] = block[i];
  return S_OK;
}

extern "C" void AgxD3d12OwnerDestroyCompute(AGX_D3D12_OWNER *owner, AGX_D3D12_COMPUTE *compute) {
  if (!owner || !owner->Context || !compute || !compute->State) return;
  owner->Context->delete_compute_state(owner->Context, compute->State);
  memset(compute, 0, sizeof(*compute));
}

extern "C" HRESULT AgxD3d12OwnerDispatch(AGX_D3D12_OWNER *owner, const AGX_D3D12_COMPUTE *compute,
                                         const AGX_D3D12_UAV uavs[AGX_D3D12_UAV_SLOTS],
                                         const AGX_D3D12_CBV cbvs[AGX_D3D12_CBV_SLOTS],
                                         const AGX_D3D12_SRV srvs[AGX_D3D12_SRV_SLOTS], unsigned x,
                                         unsigned y, unsigned z) {
  if (!owner || !owner->Context || !compute || !compute->State || !uavs || !cbvs || !srvs || !x || !y || !z)
    return E_INVALIDARG;
  for (unsigned i = 0; i < AGX_D3D12_SRV_SLOTS; ++i) {
    const AGX_D3D12_BUFFER *buffer = srvs[i].Buffer;
    if (buffer && (!buffer->Resource || srvs[i].Offset >= buffer->Bytes || buffer->Bytes > 0xffffffffull))
      return E_INVALIDARG;
  }
  struct pipe_constant_buffer constants[AGX_D3D12_CBV_SLOTS] = {};
  for (unsigned i = 0; i < AGX_D3D12_CBV_SLOTS; ++i) {
    const AGX_D3D12_BUFFER *buffer = cbvs[i].Buffer;
    if (buffer) {
      if (!buffer->Resource || cbvs[i].Offset >= buffer->Bytes || buffer->Bytes > 0xffffffffull) return E_INVALIDARG;
      unsigned long long room = buffer->Bytes - cbvs[i].Offset;
      if (cbvs[i].Bytes && cbvs[i].Bytes < room) room = cbvs[i].Bytes;
      constants[i].buffer = static_cast<struct pipe_resource *>(buffer->Resource);
      constants[i].buffer_offset = static_cast<unsigned>(cbvs[i].Offset);
      constants[i].buffer_size = static_cast<unsigned>(room < 65536ull ? room : 65536ull);
    } else if (cbvs[i].Data) {
      if (!cbvs[i].Bytes || cbvs[i].Bytes > 65536u) return E_INVALIDARG;
      constants[i].user_buffer = cbvs[i].Data;
      constants[i].buffer_size = cbvs[i].Bytes;
    }
  }
  struct pipe_shader_buffer buffers[AGX_D3D12_UAV_SLOTS] = {};
  for (unsigned i = 0; i < AGX_D3D12_UAV_SLOTS; ++i) {
    const AGX_D3D12_BUFFER *buffer = uavs[i].Buffer;
    if (!buffer) continue;
    if (!buffer->Resource || uavs[i].Offset >= buffer->Bytes || buffer->Bytes > 0xffffffffull)
      return E_INVALIDARG;
    buffers[i].buffer = static_cast<struct pipe_resource *>(buffer->Resource);
    buffers[i].buffer_offset = static_cast<unsigned>(uavs[i].Offset);
    const unsigned long long room = buffer->Bytes - uavs[i].Offset;
    buffers[i].buffer_size = static_cast<unsigned>(uavs[i].Bytes && uavs[i].Bytes < room ? uavs[i].Bytes : room);
  }
  struct pipe_context *pipe = owner->Context;
  /* C5d: texel-buffer views of the SRV ranges (u_sampler_view_default_template
   * for PIPE_BUFFER, which the UMD Mesa tree does not provide) */
  struct pipe_sampler_view *views[AGX_D3D12_SRV_SLOTS] = {};
  unsigned view_count = 0;
  HRESULT hr = S_OK;
  for (unsigned i = 0; i < AGX_D3D12_SRV_SLOTS && SUCCEEDED(hr); ++i) {
    const AGX_D3D12_BUFFER *buffer = srvs[i].Buffer;
    if (!buffer) continue;
    const unsigned long long room = buffer->Bytes - srvs[i].Offset;
    struct pipe_sampler_view templ = {};
    templ.format = PIPE_FORMAT_R32_UINT;
    templ.target = PIPE_BUFFER;
    templ.swizzle_r = PIPE_SWIZZLE_X;
    templ.swizzle_g = PIPE_SWIZZLE_Y;
    templ.swizzle_b = PIPE_SWIZZLE_Z;
    templ.swizzle_a = PIPE_SWIZZLE_W;
    templ.u.buf.offset = static_cast<unsigned>(srvs[i].Offset);
    templ.u.buf.size = static_cast<unsigned>(srvs[i].Bytes && srvs[i].Bytes < room ? srvs[i].Bytes : room) & ~3u;
    views[i] = pipe->create_sampler_view(pipe, static_cast<struct pipe_resource *>(buffer->Resource), &templ);
    if (!views[i]) hr = E_OUTOFMEMORY;
    view_count = i + 1;
  }
  if (FAILED(hr)) {
    for (unsigned i = 0; i < view_count; ++i)
      if (views[i]) pipe->sampler_view_release(pipe, views[i]);
    return hr;
  }
  pipe->bind_compute_state(pipe, compute->State);
  if (view_count) pipe->set_sampler_views(pipe, MESA_SHADER_COMPUTE, 0, view_count, 0, views);
  pipe->set_shader_buffers(pipe, MESA_SHADER_COMPUTE, 0, AGX_D3D12_UAV_SLOTS, buffers,
                           (1u << AGX_D3D12_UAV_SLOTS) - 1u);
  /* Asahi uploads a user buffer when it is set, so root constants are
   * captured here */
  for (unsigned i = 0; i < AGX_D3D12_CBV_SLOTS; ++i)
    pipe->set_constant_buffer(pipe, MESA_SHADER_COMPUTE, i,
                              constants[i].buffer || constants[i].user_buffer ? &constants[i] : NULL);
  struct pipe_grid_info info = {};
  info.work_dim = 3;
  for (unsigned i = 0; i < 3; ++i) info.block[i] = compute->Block[i];
  info.grid[0] = x;
  info.grid[1] = y;
  info.grid[2] = z;
  pipe->launch_grid(pipe, &info);
  pipe->memory_barrier(pipe, PIPE_BARRIER_ALL);
  /* the batch holds its buffers; the binding must not outlive the heaps */
  pipe->set_shader_buffers(pipe, MESA_SHADER_COMPUTE, 0, AGX_D3D12_UAV_SLOTS, NULL, 0);
  for (unsigned i = 0; i < AGX_D3D12_CBV_SLOTS; ++i) pipe->set_constant_buffer(pipe, MESA_SHADER_COMPUTE, i, NULL);
  if (view_count) {
    /* the context holds its own references while bound */
    pipe->set_sampler_views(pipe, MESA_SHADER_COMPUTE, 0, 0, view_count, NULL);
    for (unsigned i = 0; i < view_count; ++i)
      if (views[i]) pipe->sampler_view_release(pipe, views[i]);
  }
  ++owner->PendingDispatches;
  return S_OK;
}

/* C6a: textures, render-target clears and footprint copies (the D3D10
 * frontend's RTV/clear path, OutputMerger.cpp, on this owner's context). */
extern "C" unsigned AgxD3d12FormatBlockBytes(DXGI_FORMAT format) {
  const enum pipe_format pformat = FormatTranslate(format, FALSE);
  if (pformat == PIPE_FORMAT_NONE || util_format_get_blockwidth(pformat) != 1 ||
      util_format_get_blockheight(pformat) != 1)
    return 0;
  return util_format_get_blocksize(pformat);
}

extern "C" HRESULT AgxD3d12OwnerCreateTexture2D(AGX_D3D12_OWNER *owner, DXGI_FORMAT format, unsigned width,
                                                unsigned height, unsigned layers, unsigned levels, unsigned bind,
                                                unsigned long long bytes, AGX_D3D12_BUFFER *texture) {
  if (!owner || !owner->Screen || !texture || !width || !height || !layers || !levels || levels > 16 ||
      !AgxD3d12FormatBlockBytes(format))
    return E_INVALIDARG;
  memset(texture, 0, sizeof(*texture));
  struct pipe_resource templ = {};
  templ.target = layers > 1 ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D;
  templ.format = FormatTranslate(format, FALSE);
  templ.width0 = width;
  templ.height0 = static_cast<uint16_t>(height);
  templ.depth0 = 1;
  templ.array_size = static_cast<uint16_t>(layers);
  templ.last_level = static_cast<uint8_t>(levels - 1);
  templ.usage = PIPE_USAGE_DEFAULT;
  templ.bind = ((bind & AGX_D3D12_BIND_RENDER_TARGET) ? PIPE_BIND_RENDER_TARGET : 0) |
               ((bind & AGX_D3D12_BIND_SAMPLER) ? PIPE_BIND_SAMPLER_VIEW : 0);
  if (height > 0xffffu || layers > 0xffffu) return E_INVALIDARG;
  struct pipe_resource *resource = owner->Screen->resource_create(owner->Screen, &templ);
  receipt("measure-d3d12-texture", resource ? S_OK : E_OUTOFMEMORY, static_cast<UINT>(templ.format), width | (height << 16),
          layers | (levels << 16));
  if (!resource) return E_OUTOFMEMORY;
  texture->Resource = resource;
  texture->GpuVa = AgxWin32AsahiBufferGpuAddress(resource);
  texture->Bytes = bytes;
  return S_OK;
}

static unsigned level_extent(unsigned extent, unsigned level) {
  extent >>= level;
  return extent ? extent : 1u;
}

extern "C" HRESULT AgxD3d12OwnerClearRenderTarget(AGX_D3D12_OWNER *owner, const AGX_D3D12_BUFFER *texture,
                                                  DXGI_FORMAT view_format, unsigned level, unsigned first_layer,
                                                  unsigned layers, const float color[4], const RECT *rects,
                                                  unsigned rect_count) {
  if (!owner || !owner->Context || !texture || !texture->Resource || !color || !layers) return E_INVALIDARG;
  struct pipe_resource *resource = static_cast<struct pipe_resource *>(texture->Resource);
  const enum pipe_format format = FormatTranslate(view_format, FALSE);
  if (format == PIPE_FORMAT_NONE || level > resource->last_level || first_layer >= resource->array_size ||
      layers > resource->array_size - first_layer)
    return E_INVALIDARG;
  struct pipe_surface surface = {};
  surface.format = format;
  surface.level = level;
  surface.first_layer = static_cast<uint16_t>(first_layer);
  surface.last_layer = static_cast<uint16_t>(first_layer + layers - 1);
  surface.texture = resource;
  union pipe_color_union value = {};
  for (unsigned i = 0; i < 4; ++i) value.f[i] = color[i];
  const unsigned width = level_extent(resource->width0, level), height = level_extent(resource->height0, level);
  if (!rect_count) {
    owner->Context->clear_render_target(owner->Context, &surface, &value, 0, 0, width, height, false);
    return S_OK;
  }
  for (unsigned i = 0; i < rect_count; ++i) {
    const LONG left = rects[i].left < 0 ? 0 : rects[i].left, top = rects[i].top < 0 ? 0 : rects[i].top;
    const LONG right = rects[i].right > static_cast<LONG>(width) ? static_cast<LONG>(width) : rects[i].right;
    const LONG bottom = rects[i].bottom > static_cast<LONG>(height) ? static_cast<LONG>(height) : rects[i].bottom;
    if (right > left && bottom > top)
      owner->Context->clear_render_target(owner->Context, &surface, &value, static_cast<unsigned>(left),
                                          static_cast<unsigned>(top), static_cast<unsigned>(right - left),
                                          static_cast<unsigned>(bottom - top), false);
  }
  return S_OK;
}

/* Maps the texture box and the buffer range; the maps synchronise with work
 * already recorded on the context, so the copy keeps replay order. */
extern "C" HRESULT AgxD3d12OwnerCopyTexture(AGX_D3D12_OWNER *owner, const AGX_D3D12_TEXTURE_COPY *copy) {
  if (!owner || !owner->Context || !copy || !copy->Texture || !copy->Texture->Resource || !copy->Buffer ||
      !copy->Buffer->Resource || !copy->Width || !copy->Height || !copy->Depth)
    return E_INVALIDARG;
  struct pipe_context *pipe = owner->Context;
  struct pipe_resource *texture = static_cast<struct pipe_resource *>(copy->Texture->Resource);
  struct pipe_resource *buffer = static_cast<struct pipe_resource *>(copy->Buffer->Resource);
  const unsigned block = util_format_get_blocksize(texture->format);
  const unsigned long long row = static_cast<unsigned long long>(copy->Width) * block;
  if (!block || util_format_get_blockwidth(texture->format) != 1 || copy->Level > texture->last_level ||
      copy->X + copy->Width > level_extent(texture->width0, copy->Level) ||
      copy->Y + copy->Height > level_extent(texture->height0, copy->Level) || copy->Layer >= texture->array_size ||
      copy->Depth > texture->array_size - copy->Layer || copy->Pitch < row ||
      (copy->Depth > 1 && copy->SlicePitch < static_cast<unsigned long long>(copy->Pitch) * copy->Height))
    return E_INVALIDARG;
  const unsigned long long span = static_cast<unsigned long long>(copy->Depth - 1) * copy->SlicePitch +
                                  static_cast<unsigned long long>(copy->Height - 1) * copy->Pitch + row;
  if (copy->Offset > copy->Buffer->Bytes || span > copy->Buffer->Bytes - copy->Offset || copy->Buffer->Bytes > 0xffffffffull)
    return E_INVALIDARG;
  struct pipe_box tbox = {};
  tbox.x = static_cast<int>(copy->X);
  tbox.y = static_cast<int>(copy->Y);
  tbox.z = static_cast<int16_t>(copy->Layer);
  tbox.width = static_cast<int>(copy->Width);
  tbox.height = static_cast<int>(copy->Height);
  tbox.depth = static_cast<int16_t>(copy->Depth);
  struct pipe_box bbox = {};
  bbox.x = static_cast<int>(copy->Offset);
  bbox.width = static_cast<int>(span);
  bbox.height = 1;
  bbox.depth = 1;
  struct pipe_transfer *ttransfer = NULL, *btransfer = NULL;
  void *tmap = pipe->texture_map(pipe, texture, copy->Level, copy->ToBuffer ? PIPE_MAP_READ : PIPE_MAP_WRITE, &tbox,
                                 &ttransfer);
  void *bmap = tmap ? pipe->buffer_map(pipe, buffer, 0, copy->ToBuffer ? PIPE_MAP_WRITE : PIPE_MAP_READ, &bbox,
                                       &btransfer)
                    : NULL;
  HRESULT hr = tmap && bmap ? S_OK : E_OUTOFMEMORY;
  for (unsigned z = 0; SUCCEEDED(hr) && z < copy->Depth; ++z)
    for (unsigned y = 0; y < copy->Height; ++y) {
      unsigned char *t = static_cast<unsigned char *>(tmap) + z * ttransfer->layer_stride + y * ttransfer->stride;
      unsigned char *b = static_cast<unsigned char *>(bmap) + static_cast<size_t>(z) * copy->SlicePitch +
                         static_cast<size_t>(y) * copy->Pitch;
      if (copy->ToBuffer) memcpy(b, t, static_cast<size_t>(row));
      else memcpy(t, b, static_cast<size_t>(row));
    }
  if (bmap) pipe->buffer_unmap(pipe, btransfer);
  if (tmap) pipe->texture_unmap(pipe, ttransfer);
  receipt("measure-d3d12-texture-copy", hr, copy->ToBuffer ? 1u : 0u, copy->Width | (copy->Height << 16),
          copy->Pitch);
  return hr;
}

extern "C" HRESULT AgxD3d12OwnerSubmitAndWait(AGX_D3D12_OWNER *owner) {
  if (!owner || !owner->Context) return E_INVALIDARG;
  const UINT copies = owner->PendingCopies | (owner->PendingDispatches << 16);
  owner->PendingCopies = owner->PendingDispatches = 0;
  owner->Context->flush(owner->Context, NULL, 0);
  const int retired = AgxWin32AsahiContextRetire(owner->Context, 5000u);
  const int faulted = AgxWin32AsahiContextFaulted(owner->Context);
  const HRESULT hr = retired && !faulted ? S_OK : DXGI_ERROR_DEVICE_HUNG;
  receipt("measure-d3d12-owner-submit", hr, copies, static_cast<UINT>(retired), static_cast<UINT>(faulted));
  return hr;
}
