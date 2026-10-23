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
#if defined(APPLE_AGX_D3D12_PREPARED_NIR_OWNER) && APPLE_AGX_D3D12_PREPARED_NIR_OWNER
#include "../dxil/agx_owned_nir_handoff.h"
#endif
#include "agx_d3d12_view_mapping.h"
#include "agx_win32_asahi_layout.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "../dxbc/dxbc_to_nir.h"
#include "../dxbc/dxbc_tir_gs.h"
#ifdef APPLE_AGX_D3D12_DXIL
#include "../dxil/agx_dxil_to_nir.h"
#endif
#include "util/format/u_format.h"
#include "util/u_surface.h"
#include "util/u_simple_shaders.h"

/* The D3D10 frontend's DXGI -> pipe format table (gallium/frontends/d3d10umd
 * Format.cpp, linked in the native runtime archive). */
enum pipe_format FormatTranslate(DXGI_FORMAT Format, BOOL depth);
static_assert(AGX_D3D12_SRV_SLOTS == DXBC_RESOURCE_SLOTS, "NIR SRV dimension metadata capacity");

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
  UINT PendingDraws; /* C6b: draws whose framebuffer/vertex bindings the submit drops */
  /* the framebuffer the last draw bound: Asahi's set_framebuffer_state
   * leaves the batch and dirties every state, so an unchanged framebuffer
   * is not set again (EXP1835 Multithreading: ~2050 draws a frame each
   * re-emitted all state). Clears and the submit bind their own and
   * invalidate it; u_blitter restores it. */
  struct pipe_framebuffer_state DrawFramebuffer;
  bool DrawFramebufferValid;
  /* receipt-only: QPC ticks of the draws' binding, draw_vbo and unbinding
   * and of the submit's flush and retire, reported and reset per submit
   * (measure-d3d12-owner-time) */
  LONGLONG TimeBind, TimeDrawVbo, TimeUnbind;
  UINT TimedDraws;
  /* receipt-only (measure-d3d12-owner-fences): the newest submission fence
   * the previous submit reported */
  unsigned long long ReportedFence;
  /* C47: the heap bindings the next draw or dispatch uses (AgxD3d12OwnerSetHeap)
   * and their constant buffer DXBC_HEAP_TABLE_CB (range table + sampler map) */
  AGX_D3D12_HEAP_BINDING PendingHeaps[AGX_D3D12_HEAP_STAGES];
  /* C61C: + the TIR/CR viewport of a wrapped GS that reads heap ranges */
  uint32_t HeapTables[AGX_D3D12_HEAP_STAGES][4 * AGX_D3D12_HEAP_RANGES + DXBC_HEAP_SAMPLER_MAP_MAX + 4];
  float TirViewport[4];
  bool TirViewportValid;
  /* EXP1829 (Multithreading's shadow pass): a PSO without a pixel shader
   * draws with this empty one -- Asahi always reads the bound FS */
  void *EmptyFs;
  /* EXP1891B: a PSO without an input layout draws with this empty vertex
   * elements state -- Asahi's agx_update_vs always reads the bound one */
  void *EmptyVelems;
  struct AGX_D3D12_PRESENTATION *PendingPresentations; /* C7b: released, kernel allocation still registered */
  /* EXP1822: D3D12 queues and lists are used from several threads (nBody's
   * async compute thread next to its render thread). The pipe_context is not
   * thread-safe, so every entry point holds this recursive lock, and
   * ExecuteCommandLists holds it across a whole replay and submission. */
  CRITICAL_SECTION Lock;
};
namespace {
struct OwnerLock {
  AGX_D3D12_OWNER *owner;
  explicit OwnerLock(AGX_D3D12_OWNER *o) : owner(o) { if (owner) EnterCriticalSection(&owner->Lock); }
  ~OwnerLock() { if (owner) LeaveCriticalSection(&owner->Lock); }
  OwnerLock(const OwnerLock &) = delete;
  OwnerLock &operator=(const OwnerLock &) = delete;
};
} // namespace
extern "C" void AgxD3d12OwnerLock(AGX_D3D12_OWNER *owner) { if (owner) EnterCriticalSection(&owner->Lock); }
extern "C" void AgxD3d12OwnerUnlock(AGX_D3D12_OWNER *owner) { if (owner) LeaveCriticalSection(&owner->Lock); }

/* C7b: a presentation texture (agx_d3d10_windows.cpp
 * AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE) */
struct AGX_D3D12_PRESENTATION {
  ADMISSION_UMD_RESOURCE Resource;
  AGX_WIN32_SCREEN_BUFFER RenderBuffer;
  struct pipe_resource *RenderResource;
  /* C40: a swapchain format the presentation path does not store (10-bit):
   * the application renders this texture, PreparePresent converts it into
   * RenderResource (NULL: RenderResource is the application's texture) */
  struct pipe_resource *Converted;
  struct AGX_D3D12_PRESENTATION *Next;
};

/* Destroys released presentations whose kernel allocation the screen no
 * longer tracks (agx_d3d10_windows.cpp collect_presentations). */
static BOOL collect_presentations(AGX_D3D12_OWNER *owner) {
  struct AGX_D3D12_PRESENTATION **link = &owner->PendingPresentations;
  while (*link) {
    struct AGX_D3D12_PRESENTATION *record = *link;
    (void)AgxWin32AsahiCollect(&owner->Backend);
    if (AdmissionUmdScreenAllocationRegistered(&owner->Runtime, record->Resource.KernelAllocation)) {
      link = &record->Next;
      continue;
    }
    D3D10DDI_HDEVICE device = {};
    D3D10DDI_HRESOURCE resource = {};
    device.pDrvPrivate = &owner->Runtime;
    resource.pDrvPrivate = &record->Resource;
    AdmissionUmdDestroyResource(device, resource);
    *link = record->Next;
    HeapFree(GetProcessHeap(), 0, record);
  }
  return owner->PendingPresentations == NULL;
}

static VOID APIENTRY owner_error(D3D10DDI_HRTCORELAYER handle, HRESULT error) {
  AGX_D3D12_OWNER *owner = static_cast<AGX_D3D12_OWNER *>(handle.handle);
  if (owner && owner->Error) owner->Error(owner->ErrorContext, error);
}

static void receipt(PCSTR stage, HRESULT status, UINT a, UINT b, UINT c) {
  const UINT values[] = {a, b, c};
  AdmissionUmdDiagnostic(stage, status, values, 3u);
}

/* EXP1851: a refused batch faults the backend and Asahi's next BO allocation
 * fails silently; report refusals as the D3D10 owner does (reject-batch
 * {kind, site, detail0, detail1}; kind 4 = backend failure). */
extern "C" void (*AgxWin32BatchRefusalHook)(unsigned kind, unsigned site, unsigned detail0, unsigned detail1);
extern "C" void (*AgxWin32BackendFailHook)(unsigned site);
static void owner_batch_refusal(unsigned kind, unsigned site, unsigned detail0, unsigned detail1) {
  const UINT values[] = {kind, site, detail0, detail1};
  AdmissionUmdDiagnostic("reject-batch", E_FAIL, values, 4u);
}
static void owner_backend_fail(unsigned site) { owner_batch_refusal(4u, site, 0u, 0u); }
/* EXP1875: a D3D9On12 batch reported faulted without a submission and
 * nothing named the Asahi site that marked the context (kind << 16 | line). */
extern "C" void (*AgxWin32FirstFaultHook)(unsigned site, uintptr_t context, unsigned flags, unsigned draws);
static void owner_first_fault(unsigned site, uintptr_t context, unsigned flags, unsigned draws) {
  static volatile LONG records;
  const LONG count = InterlockedIncrement(&records);
  if (count > 64) return;
  const UINT values[] = {site, flags, draws, static_cast<UINT>(context), static_cast<UINT>(count)};
  AdmissionUmdDiagnostic("measure-d3d12-first-fault", E_FAIL, values, 5u);
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
  if (owner->Context && owner->PendingPresentations) {
    owner->Context->flush(owner->Context, NULL, 0);
    (void)AgxWin32AsahiContextRetire(owner->Context, 5000u);
    (void)collect_presentations(owner);
  }
  owner->Backend.Closing = 1;
  if (owner->Context && owner->EmptyFs) {
    owner->Context->bind_fs_state(owner->Context, NULL);
    owner->Context->delete_fs_state(owner->Context, owner->EmptyFs);
    owner->EmptyFs = NULL;
  }
  if (owner->Context && owner->EmptyVelems) {
    owner->Context->bind_vertex_elements_state(owner->Context, NULL);
    owner->Context->delete_vertex_elements_state(owner->Context, owner->EmptyVelems);
    owner->EmptyVelems = NULL;
  }
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
  DeleteCriticalSection(&owner->Lock);
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
  InitializeCriticalSection(&owner->Lock);
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
    /* C32 (EXP1830): without this the winsys sets AGX_DBG_NOMSAA, the screen
     * refuses multisampled formats and u_blitter's resolve hits
     * UNREACHABLE("Unsupported blit") in agx_blit */
    owner->Backend.Multisample = 1;
    owner->Screen = AgxWin32AsahiScreenCreateForWindows(&owner->Backend, &owner->Runtime.Screen,
                                                       &owner->OwnerOperations, &owner->Owner,
                                                       AdmissionUmdAsahiBatchOperations());
    if (!owner->Screen) hr = FAILED(owner->Runtime.LastScreenError) ? owner->Runtime.LastScreenError : E_OUTOFMEMORY;
  }
  if (SUCCEEDED(hr)) {
    owner->Context = AgxWin32AsahiContextCreate(owner->Screen, &owner->Owner);
    if (!owner->Context) hr = E_OUTOFMEMORY;
    AgxWin32BatchRefusalHook = owner_batch_refusal;
    AgxWin32BackendFailHook = owner_backend_fail;
    AgxWin32FirstFaultHook = owner_first_fault;
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

extern "C" HRESULT AgxD3d12OwnerCreateBuffer(AGX_D3D12_OWNER *owner, unsigned long long bytes, BOOL cpu_visible,
                                             AGX_D3D12_BUFFER *buffer) {
  OwnerLock lock(owner);
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
  /* EXP1373: an explicit PIPE_BIND_SHADER_IMAGE made resource_create fail
   * for every heap; Asahi infers the image bind for linear single-layer
   * resources (agx_resource_create), which typed buffer UAVs use. */
  struct pipe_resource *resource = owner->Screen->resource_create(owner->Screen, &templ);
  if (!resource) return E_OUTOFMEMORY;
  /* EXP1847 ModelViewer: a mapped buffer counts as CPU-visible, so the UMD
   * downloaded every GPU write of it after each submission (its 64 MB light
   * grid: ~163 ms a frame). A heap the CPU cannot map stays unmapped; a later
   * Mesa map (ExecuteIndirect arguments) downloads it then. */
  void *cpu = cpu_visible ? AgxWin32AsahiBufferWriteMap(owner->Context, resource) : NULL;
  unsigned long long va = AgxWin32AsahiBufferGpuAddress(resource);
  if ((cpu_visible ? !cpu : !AgxWin32AsahiBufferMarkValid(owner->Context, resource)) || !va) {
    AgxWin32AsahiResourceRelease(&resource);
    return E_OUTOFMEMORY;
  }
  buffer->Resource = resource;
  buffer->GpuVa = va;
  buffer->Cpu = cpu;
  buffer->Bytes = bytes;
  return S_OK;
}

extern "C" HRESULT AgxD3d12OwnerCreateReservedBuffer(AGX_D3D12_OWNER *owner, unsigned long long bytes,
                                                     AGX_D3D12_BUFFER *buffer) {
  OwnerLock lock(owner);
  if (!owner || !owner->Screen || !owner->Context || !buffer || !bytes || bytes > 0xffffffffull ||
      (bytes & 0xffffull))
    return E_INVALIDARG;
  memset(buffer, 0, sizeof(*buffer));
  struct pipe_resource *resource = AgxWin32AsahiCreateReservedBuffer(owner->Screen, bytes);
  if (!resource) return E_OUTOFMEMORY;
  /* GPU-only content: never downloaded, the tiles' heaps own the data */
  unsigned long long va = AgxWin32AsahiBufferGpuAddress(resource);
  if (!va || !AgxWin32AsahiBufferMarkValid(owner->Context, resource)) {
    AgxWin32AsahiResourceRelease(&resource);
    return E_OUTOFMEMORY;
  }
  buffer->Resource = resource;
  buffer->GpuVa = va;
  buffer->Bytes = bytes;
  return S_OK;
}

extern "C" HRESULT AgxD3d12OwnerMapReservedTiles(AGX_D3D12_OWNER *owner, const AGX_D3D12_BUFFER *reserved,
                                                 unsigned first_tile, unsigned tiles,
                                                 const AGX_D3D12_BUFFER *backing, unsigned backing_tile) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !reserved || !reserved->Resource || !tiles ||
      (backing && !backing->Resource))
    return E_INVALIDARG;
  /* recorded, unsubmitted work names the old tiles: submit it first */
  if (owner->PendingDraws || owner->PendingCopies || owner->PendingDispatches) {
    const HRESULT flushed = AgxD3d12OwnerSubmitAndWait(owner);
    if (FAILED(flushed)) return flushed;
  }
  const int mapped = AgxWin32AsahiMapReservedTiles(static_cast<struct pipe_resource *>(reserved->Resource), first_tile,
                                                   tiles, backing ? static_cast<struct pipe_resource *>(backing->Resource)
                                                                  : NULL, backing_tile);
  return mapped ? S_OK : E_FAIL;
}

/* C53 HW2 (D3D12-C53-tiled-textures.md; run by the HW1 self-test): a
 * reserved 256x256 RGBA8 texture with 3 mips -- mip 0 is 2x2 standard tiles,
 * mip 1 one, mip 2 packed -- over a 6-tile heap:
 *   mip 0 (0,0) -> heap 0, (1,0) -> heap 1, (0,1) -> heap 2, (1,1) -> heap 0
 *   (an alias of (0,0)), mip 1 -> heap 3, the packed tile -> heap 4.
 * Known texels go in through CopyTexture (GPU writes through the 16 KiB page
 * maps) except into (1,1); a read back must return them, (1,1) must read
 * (0,0)'s texels, heap tiles 0-4 must hold data and heap tile 5 none. Then
 * (1,1) is remapped onto heap 5 and must read zero. Receipt
 * measure-d3d12-reserved-texture-selftest {stage, bad, first bad texel}. */
static uint32_t selftest_texel(unsigned x, unsigned y, unsigned level) {
  return 0xff000000u | (level << 16) | ((y & 0xffu) << 8) | (x & 0xffu);
}
static HRESULT ReservedTextureSelfTest(AGX_D3D12_OWNER *owner) {
  const unsigned long long tile = 0x10000ull;
  const unsigned size = 256, pitch = size * 4;
  AGX_D3D12_BUFFER heap = {}, texture = {}, staging = {};
  AGX_D3D12_RESERVED_TILING tiling = {};
  UINT stage = 0, bad = 0, first_bad = ~0u;
  HRESULT hr = AgxD3d12OwnerCreateBuffer(owner, 6 * tile, TRUE, &heap);
  if (SUCCEEDED(hr)) {
    stage = 1;
    memset(heap.Cpu, 0, static_cast<size_t>(6 * tile));
    hr = AgxD3d12OwnerCreateReservedTexture2D(owner, DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, 3,
                                              AGX_D3D12_BIND_SAMPLER | AGX_D3D12_BIND_RENDER_TARGET, &texture, &tiling);
  }
  if (SUCCEEDED(hr) && (tiling.StandardMips != 2 || tiling.TilesX[0] != 2 || tiling.TilesY[0] != 2 ||
                        tiling.TilesX[1] != 1 || tiling.PackedTiles != 1)) hr = E_UNEXPECTED;
  if (SUCCEEDED(hr)) { stage = 2; hr = AgxD3d12OwnerCreateBuffer(owner, static_cast<unsigned long long>(pitch) * size, TRUE, &staging); }
  if (SUCCEEDED(hr)) {
    stage = 3;
    const struct { unsigned mip, x, y, heap; BOOL packed; } map[] = {
        {0, 0, 0, 0, FALSE}, {0, 1, 0, 1, FALSE}, {0, 0, 1, 2, FALSE}, {0, 1, 1, 0, FALSE},
        {1, 0, 0, 3, FALSE}, {2, 0, 0, 4, TRUE}};
    for (unsigned i = 0; SUCCEEDED(hr) && i < sizeof(map) / sizeof(map[0]); ++i)
      hr = AgxD3d12OwnerMapReservedTextureTile(owner, &texture, 0, map[i].packed, map[i].mip, map[i].x, map[i].y,
                                               &heap, map[i].heap);
  }
  /* uploads: mip 0 without its (1,1) tile, mips 1 and 2 whole */
  const struct { unsigned level, x, y, w, h; } up[] = {
      {0, 0, 0, 256, 128}, {0, 0, 128, 128, 128}, {1, 0, 0, 128, 128}, {2, 0, 0, 64, 64}};
  for (unsigned i = 0; SUCCEEDED(hr) && i < sizeof(up) / sizeof(up[0]); ++i) {
    stage = 4;
    uint32_t *words = static_cast<uint32_t *>(staging.Cpu);
    for (unsigned y = 0; y < up[i].h; ++y)
      for (unsigned x = 0; x < up[i].w; ++x) words[y * size + x] = selftest_texel(up[i].x + x, up[i].y + y, up[i].level);
    AGX_D3D12_TEXTURE_COPY copy = {&texture, up[i].level, 0, up[i].x, up[i].y, up[i].w, up[i].h, 1, &staging, 0, pitch, 0, 0};
    hr = AgxD3d12OwnerCopyTexture(owner, &copy);
  }
  for (unsigned pass = 0; SUCCEEDED(hr) && pass < 2; ++pass) {
    stage = 5 + pass;
    if (pass) hr = AgxD3d12OwnerMapReservedTextureTile(owner, &texture, 0, FALSE, 0, 1, 1, &heap, 5);
    for (unsigned level = 0; SUCCEEDED(hr) && level < 3; ++level) {
      const unsigned extent = size >> level;
      AGX_D3D12_TEXTURE_COPY copy = {&texture, level, 0, 0, 0, extent, extent, 1, &staging, 0, pitch, 0, 1};
      memset(staging.Cpu, 0xee, static_cast<size_t>(pitch) * size);
      hr = AgxD3d12OwnerCopyTexture(owner, &copy);
      const uint32_t *words = static_cast<const uint32_t *>(staging.Cpu);
      for (unsigned y = 0; SUCCEEDED(hr) && y < extent; y += 3)
        for (unsigned x = 0; x < extent; x += 5) {
          /* (1,1) of mip 0 reads (0,0) through the alias, then zero (heap 5) */
          const bool alias = level == 0 && x >= 128 && y >= 128;
          const uint32_t expect = !alias ? selftest_texel(x, y, level) : pass ? 0u : selftest_texel(x - 128, y - 128, 0);
          if (words[y * size + x] != expect) {
            if (!bad) first_bad = (level << 24) | (y << 12) | x;
            ++bad;
          }
        }
    }
    if (SUCCEEDED(hr) && bad) hr = E_FAIL;
  }
  if (SUCCEEDED(hr)) {
    /* the texels live in the heap: tiles 0-4 written, tile 5 untouched */
    stage = 7;
    const uint32_t *words = static_cast<const uint32_t *>(heap.Cpu);
    for (unsigned t = 0; t < 6; ++t) {
      unsigned nonzero = 0;
      for (unsigned i = 0; i < tile / 4; i += 61) nonzero += words[t * (tile / 4) + i] != 0;
      if ((t < 5) != (nonzero != 0)) { ++bad; if (first_bad == ~0u) first_bad = 0x80000000u | t; }
    }
    if (bad) hr = E_FAIL;
  }
  receipt("measure-d3d12-reserved-texture-selftest", hr, stage, bad, first_bad);
  /* C53 HW3 (Tier 2, measurement only): (1,1) unmapped, texels written to
   * it land in the tile scratch; a read through the sparse descriptor
   * (153fd2ab) returns 0, a read through the image VA returns the scratch.
   * Receipt measure-d3d12-reserved-tier2-selftest {zero texels, nonzero
   * texels, heap tiles changed}. */
  if (SUCCEEDED(hr)) {
    UINT zero = 0, nonzero = 0, heap_changed = 0;
    uint32_t before[6] = {};
    const uint32_t *heap_words = static_cast<const uint32_t *>(heap.Cpu);
    for (unsigned t = 0; t < 6; ++t) before[t] = heap_words[t * (tile / 4) + 4097];
    HRESULT probe = AgxD3d12OwnerMapReservedTextureTile(owner, &texture, 0, FALSE, 0, 1, 1, nullptr, 0);
    uint32_t *words = static_cast<uint32_t *>(staging.Cpu);
    for (unsigned y = 0; SUCCEEDED(probe) && y < 128; ++y)
      for (unsigned x = 0; x < 128; ++x) words[y * size + x] = 0xdeadbeefu;
    AGX_D3D12_TEXTURE_COPY in = {&texture, 0, 0, 128, 128, 128, 128, 1, &staging, 0, pitch, 0, 0};
    if (SUCCEEDED(probe)) probe = AgxD3d12OwnerCopyTexture(owner, &in);
    AGX_D3D12_TEXTURE_COPY out = {&texture, 0, 0, 128, 128, 128, 128, 1, &staging, 0, pitch, 0, 1};
    if (SUCCEEDED(probe)) {
      memset(staging.Cpu, 0xee, static_cast<size_t>(pitch) * size);
      probe = AgxD3d12OwnerCopyTexture(owner, &out);
    }
    for (unsigned y = 0; SUCCEEDED(probe) && y < 128; y += 3)
      for (unsigned x = 0; x < 128; x += 5) (words[y * size + x] ? nonzero : zero) += 1;
    for (unsigned t = 0; SUCCEEDED(probe) && t < 6; ++t) heap_changed += heap_words[t * (tile / 4) + 4097] != before[t];
    receipt("measure-d3d12-reserved-tier2-selftest", probe, zero, nonzero, heap_changed);
  }
  if (texture.Resource) AgxD3d12OwnerDestroyBuffer(owner, &texture);
  if (heap.Resource) AgxD3d12OwnerDestroyBuffer(owner, &heap);
  if (staging.Resource) AgxD3d12OwnerDestroyBuffer(owner, &staging);
  return hr;
}

extern "C" HRESULT AgxD3d12OwnerReservedSelfTest(AGX_D3D12_OWNER *owner) {
  OwnerLock lock(owner);
  const unsigned long long tile = 0x10000ull;
  AGX_D3D12_BUFFER heap = {}, reserved = {}, readback = {};
  UINT stage = 0, bad = 0;
  HRESULT hr = AgxD3d12OwnerCreateBuffer(owner, 4 * tile, TRUE, &heap);
  if (SUCCEEDED(hr)) { stage = 1; hr = AgxD3d12OwnerCreateReservedBuffer(owner, 4 * tile, &reserved); }
  if (SUCCEEDED(hr)) { stage = 2; hr = AgxD3d12OwnerCreateBuffer(owner, 4 * tile, TRUE, &readback); }
  if (SUCCEEDED(hr)) {
    /* heap tile k holds the 32-bit words 0xA0000000 | k << 16 | index */
    stage = 3;
    uint32_t *words = static_cast<uint32_t *>(heap.Cpu);
    for (unsigned k = 0; k < 4; ++k)
      for (unsigned i = 0; i < tile / 4; ++i) words[k * (tile / 4) + i] = 0xa0000000u | (k << 16) | (i & 0xffffu);
    /* reserved tile 0 -> heap tile 2, tile 1 unmapped, tile 2 -> heap tile 0 */
    hr = AgxD3d12OwnerMapReservedTiles(owner, &reserved, 0, 1, &heap, 2);
    if (SUCCEEDED(hr)) hr = AgxD3d12OwnerMapReservedTiles(owner, &reserved, 2, 1, &heap, 0);
  }
  for (unsigned pass = 0; SUCCEEDED(hr) && pass < 2; ++pass) {
    stage = 4 + pass;
    if (pass) hr = AgxD3d12OwnerMapReservedTiles(owner, &reserved, 1, 1, &heap, 1); /* remap tile 1 */
    if (SUCCEEDED(hr)) hr = AgxD3d12OwnerCopy(owner, &readback, 0, &reserved, 0, 3 * tile);
    if (SUCCEEDED(hr)) hr = AgxD3d12OwnerSubmitAndWait(owner);
    if (SUCCEEDED(hr)) {
      const uint32_t *out = static_cast<const uint32_t *>(readback.Cpu);
      const unsigned expect[3] = {2u, pass ? 1u : 0xffffu, 0u};
      for (unsigned t = 0; t < 3; ++t) {
        if (expect[t] == 0xffffu) continue; /* the scratch page: undefined contents */
        for (unsigned i = 0; i < tile / 4; i += 257)
          bad += out[t * (tile / 4) + i] != (0xa0000000u | (expect[t] << 16) | (i & 0xffffu));
      }
      if (bad) hr = E_FAIL;
    }
  }
  if (reserved.Resource) AgxD3d12OwnerDestroyBuffer(owner, &reserved);
  if (heap.Resource) AgxD3d12OwnerDestroyBuffer(owner, &heap);
  if (readback.Resource) AgxD3d12OwnerDestroyBuffer(owner, &readback);
  receipt("measure-d3d12-reserved-selftest", hr, stage, bad, 0u);
  (void)ReservedTextureSelfTest(owner);
  return hr;
}

extern "C" void AgxD3d12OwnerDestroyBuffer(AGX_D3D12_OWNER *owner, AGX_D3D12_BUFFER *buffer) {
  OwnerLock lock(owner);
  if (!owner || !buffer || !buffer->Resource) return;
  struct pipe_resource *resource = static_cast<struct pipe_resource *>(buffer->Resource);
  AgxWin32AsahiResourceRelease(&resource);
  memset(buffer, 0, sizeof(*buffer));
}

extern "C" UINT AgxD3d12OwnerBufferAllocation(AGX_D3D12_OWNER *owner, const AGX_D3D12_BUFFER *buffer) {
  OwnerLock lock(owner);
  if (!owner || !buffer || !buffer->Resource) return 0;
  AGX_WIN32_RELOC_ALLOCATION identity = {};
  if (!AgxWin32AsahiResourceIdentity(static_cast<struct pipe_resource *>(buffer->Resource), &identity)) return 0;
  return AdmissionUmdScreenKernelAllocation(&owner->Runtime, identity.Token);
}

extern "C" HRESULT AgxD3d12OwnerReadBuffer(AGX_D3D12_OWNER *owner, const AGX_D3D12_BUFFER *buffer,
                                           unsigned long long offset, unsigned bytes, void *data) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !buffer || !buffer->Resource || !data || !bytes || offset > buffer->Bytes ||
      bytes > buffer->Bytes - offset || buffer->Bytes > 0xffffffffull)
    return E_INVALIDARG;
  struct pipe_context *pipe = owner->Context;
  struct pipe_transfer *transfer = NULL;
  struct pipe_box box = {};
  box.x = static_cast<int>(offset);
  box.width = static_cast<int>(bytes);
  box.height = 1;
  box.depth = 1;
  void *map = pipe->buffer_map(pipe, static_cast<struct pipe_resource *>(buffer->Resource), 0, PIPE_MAP_READ, &box,
                               &transfer);
  if (!map) return E_OUTOFMEMORY;
  memcpy(data, map, bytes);
  pipe->buffer_unmap(pipe, transfer);
  return S_OK;
}

extern "C" HRESULT AgxD3d12OwnerCopy(AGX_D3D12_OWNER *owner, const AGX_D3D12_BUFFER *dst,
                                     unsigned long long dst_offset, const AGX_D3D12_BUFFER *src,
                                     unsigned long long src_offset, unsigned long long bytes) {
  OwnerLock lock(owner);
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
/* C17: independent per-kind capacities; sampler views are not sampler states. */
/* C47: SRV ranges beyond the slots (or unbounded) read the descriptor heap */
static const uint32_t owner_binding_limits[DXBC_BINDING_KIND_COUNT] = {
  AGX_D3D12_CBV_SLOTS, AGX_D3D12_SRV_SLOTS | DXBC_BINDING_HEAP_RANGES, AGX_D3D12_UAV_SLOTS | DXBC_BINDING_HEAP_RANGES,
  AGX_D3D12_SAMPLER_SLOTS | DXBC_BINDING_HEAP_RANGES
};
/* C44: a stored shader is DXBC tokens or (SM 6) a DXIL program part with
 * the same version/size header; both translate to the same NIR conventions
 * and binding map. DXIL stream output is built on the translated NIR
 * (agx_dxil_nir_xfb, the R1 runtime converts every DXBC shader); a DXIL hull shader
 * always has its control point function, so the pass-through registers
 * (a DXBC hull shader without a control point phase) do not apply. */
static struct nir_shader *translate_shader(const UINT *tokens, mesa_shader_stage stage,
                                           const nir_shader_compiler_options *options, struct dxbc_binding_map *bindings,
                                           uint32_t passthrough_registers, uint32_t passthrough_positions,
                                           const struct dxbc_nir_xfb *xfb, struct dxbc_nir_error *error) {
#ifdef APPLE_AGX_D3D12_DXIL
  if (tokens && tokens[1] >= 6 && tokens[1] < 0x4000000u &&
      agx_dxil_is_program(tokens, static_cast<size_t>(tokens[1]) * 4u)) {
    struct agx_dxil_nir_result dxil = {};
    enum agx_dxil_nir_status status =
        agx_dxil_to_nir(tokens, static_cast<size_t>(tokens[1]) * 4u, owner_binding_limits, options, &dxil);
    if (status == AGX_DXIL_NIR_OK && xfb && !agx_dxil_nir_xfb(dxil.nir, xfb)) {
      ralloc_free(dxil.nir);
      dxil.nir = NULL;
      status = AGX_DXIL_NIR_UNSUPPORTED;
    }
    if (status == AGX_DXIL_NIR_OK && dxil.stage != stage) {
      ralloc_free(dxil.nir);
      dxil.nir = NULL;
    }
    /* receipt-only: the DXIL status (opcode slot) and stage (offset slot) */
    error->opcode = 0x10000u | static_cast<uint32_t>(status);
    error->offset = static_cast<uint32_t>(dxil.stage);
    if (status != AGX_DXIL_NIR_OK || !dxil.nir) {
      memset(bindings, 0, sizeof(*bindings));
      return NULL;
    }
    *bindings = dxil.bindings;
    return dxil.nir;
  }
#endif
  (void)stage;
  return dxbc_translate_tokens_bindings_xfb(tokens, options, owner_binding_limits, bindings, passthrough_registers,
                                            passthrough_positions, xfb, error);
}
extern "C" HRESULT AgxD3d12OwnerCreateCompute(AGX_D3D12_OWNER *owner, const UINT *tokens,
                                              AGX_D3D12_COMPUTE *compute) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !tokens || !compute) return E_INVALIDARG;
  memset(compute, 0, sizeof(*compute));
  struct pipe_context *pipe = owner->Context;
  struct dxbc_nir_error error = {};
  struct nir_shader *nir = translate_shader(tokens, MESA_SHADER_COMPUTE, pipe->screen->nir_options[MESA_SHADER_COMPUTE],
                                            &compute->Bindings, 0u, 0u, NULL, &error);
  if (!nir) {
    receipt("measure-d3d12-compute", E_NOTIMPL, 0u, error.opcode, error.offset);
    return E_NOTIMPL;
  }
  if (!dxbc_nir_srv_dimensions(nir, compute->SrvDimensions) ||
      !dxbc_nir_uav_dimensions(nir, compute->UavDimensions)) {
    ralloc_free(nir);
    memset(compute, 0, sizeof(*compute));
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

#if defined(APPLE_AGX_D3D12_PREPARED_NIR_OWNER) && APPLE_AGX_D3D12_PREPARED_NIR_OWNER
static bool compute_prepared_empty(const AGX_D3D12_COMPUTE *v) {
 if(v->State||v->Bindings.active||v->Bindings.count)return false;
 for(unsigned i=0;i<3;i++)if(v->Block[i])return false;
 for(unsigned i=0;i<DXBC_BINDING_MAP_MAX;i++){
  const auto &b=v->Bindings.bindings[i];
  if(b.kind||b.id||b.space||b.reg||b.slot)return false;
 }
 for(unsigned i=0;i<DXBC_RESOURCE_SLOTS;i++)if(v->SrvDimensions[i])return false;
 for(unsigned i=0;i<DXBC_UAV_DIMENSION_SLOTS;i++)if(v->UavDimensions[i])return false;
 return true;
}
extern "C" HRESULT AgxD3d12OwnerCreateComputePrepared(AGX_D3D12_OWNER *owner,
 const AgxOwnedNir *prepared,AGX_D3D12_COMPUTE *compute) {
  OwnerLock lock(owner);
 if(!compute||!compute_prepared_empty(compute))return E_INVALIDARG;
 if(!owner||!owner->Screen||!owner->Context||!prepared)return E_INVALIDARG;
 if(AgxOwnedNirStage(prepared)!=AGX_OWNED_NIR_STAGE_COMPUTE)return E_NOTIMPL;
 struct agx_owned_nir_pipe_state state={};
 switch(AgxOwnedNirCreatePipeState(prepared,owner->Context,&state)){
 case AGX_OWNED_NIR_PIPE_INVALID:return E_INVALIDARG;
 case AGX_OWNED_NIR_PIPE_UNSUPPORTED:return E_NOTIMPL;
 case AGX_OWNED_NIR_PIPE_CLONE_FAILED:return E_OUTOFMEMORY;
 case AGX_OWNED_NIR_PIPE_CREATE_FAILED:return E_FAIL;
 case AGX_OWNED_NIR_PIPE_OK:break;
 default:return E_FAIL;
 }
 const uint32_t *block=AgxOwnedNirWorkgroup(prepared);
 if(!state.state||state.token_stage!=5||!block||!state.block[0]||!state.block[1]||!state.block[2]||
    state.block[0]!=block[0]||state.block[1]!=block[1]||state.block[2]!=block[2]){
  agx_owned_nir_delete_pipe_state(owner->Context,&state);return E_FAIL;
 }
 AGX_D3D12_COMPUTE local={};local.State=state.state;local.Bindings=state.bindings;
 for(unsigned i=0;i<3;i++)local.Block[i]=state.block[i];
 memcpy(local.SrvDimensions,state.srv_dimensions,sizeof(local.SrvDimensions));
 memcpy(local.UavDimensions,state.uav_dimensions,sizeof(local.UavDimensions));
 *compute=local;return S_OK;
}

#endif /* APPLE_AGX_D3D12_PREPARED_NIR_OWNER */

extern "C" void AgxD3d12OwnerDestroyCompute(AGX_D3D12_OWNER *owner, AGX_D3D12_COMPUTE *compute) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !compute || !compute->State) return;
  owner->Context->delete_compute_state(owner->Context, compute->State);
  memset(compute, 0, sizeof(*compute));
}

static unsigned level_extent(unsigned extent,unsigned level);
static unsigned texture_layer_extent(const struct pipe_resource *resource,unsigned level);

/* C6a: textures, render-target clears and footprint copies (the D3D10
 * frontend's RTV/clear path, OutputMerger.cpp, on this owner's context). */
/* Bytes of a 1x1 texel block, 0 for other block shapes; read from the
 * format table directly -- u_format.h's inline helpers carry CRT asserts,
 * which the package's assertion gate (check_no_crt_asserts.py) refuses. */
static unsigned block_bytes(enum pipe_format format) {
  const struct util_format_description *desc = format != PIPE_FORMAT_NONE ? util_format_description(format) : NULL;
  if (!desc || desc->block.width != 1 || desc->block.height != 1 || desc->block.bits % 8) return 0;
  return desc->block.bits / 8;
}

extern "C" unsigned AgxD3d12FormatBlockBytes(DXGI_FORMAT format) {
  return block_bytes(FormatTranslate(format, FALSE));
}

/* C29: a texture's element: a 1x1 texel or a BC1-BC7 4x4 block (S3TC,
 * RGTC and BPTC, which ail lays out twiddled and the AGX samples natively;
 * the texture map detiles whole blocks, agx_pipe.c/ail tiling.cc). */
static unsigned texture_block(enum pipe_format format, unsigned *bw, unsigned *bh) {
  const struct util_format_description *desc = format != PIPE_FORMAT_NONE ? util_format_description(format) : NULL;
  if (!desc || desc->block.bits % 8 || desc->block.depth != 1) return 0;
  if (desc->block.width == 1 && desc->block.height == 1) {
    *bw = *bh = 1;
    return desc->block.bits / 8;
  }
  if ((desc->layout != UTIL_FORMAT_LAYOUT_S3TC && desc->layout != UTIL_FORMAT_LAYOUT_RGTC &&
       desc->layout != UTIL_FORMAT_LAYOUT_BPTC) || desc->block.width != 4 || desc->block.height != 4)
    return 0;
  *bw = *bh = 4;
  return desc->block.bits / 8;
}

extern "C" unsigned AgxD3d12FormatBlock(DXGI_FORMAT format, unsigned *width, unsigned *height) {
  unsigned bw = 0, bh = 0;
  const unsigned bytes = texture_block(FormatTranslate(format, FALSE), &bw, &bh);
  if (width) *width = bw;
  if (height) *height = bh;
  return bytes;
}

extern "C" int AgxD3d12FormatMultisample(DXGI_FORMAT format, unsigned samples) {
  if (samples != 2 && samples != 4) return samples == 1;
  enum pipe_format physical = FormatTranslate(format, FALSE);
  if (physical == PIPE_FORMAT_NONE) physical = FormatTranslate(format, TRUE);
  return block_bytes(physical) != 0;
}

extern "C" HRESULT AgxD3d12OwnerCreateTexture2D(AGX_D3D12_OWNER *owner, DXGI_FORMAT format, unsigned width,
                                                unsigned height, unsigned layers, unsigned levels, unsigned bind,
                                                unsigned long long bytes, AGX_D3D12_BUFFER *texture, unsigned samples) {
  OwnerLock lock(owner);
  unsigned bw = 0, bh = 0;
  if (!owner || !owner->Screen || !texture || !width || !height || !layers || !levels || levels > 16 ||
      !AgxD3d12FormatBlock(format, &bw, &bh) ||
      (bw > 1 && (bind & (AGX_D3D12_BIND_RENDER_TARGET | AGX_D3D12_BIND_DEPTH_STENCIL))) ||
      (samples > 1 && (levels != 1 || bw > 1 || !AgxD3d12FormatMultisample(format, samples))))
    return E_INVALIDARG;
  memset(texture, 0, sizeof(*texture));
  struct pipe_resource templ = {};
  templ.target = layers > 1 ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D;
  templ.format = FormatTranslate(format, (bind & AGX_D3D12_BIND_DEPTH_STENCIL) ? TRUE : FALSE);
  templ.width0 = width;
  templ.height0 = static_cast<uint16_t>(height);
  templ.depth0 = 1;
  templ.array_size = static_cast<uint16_t>(layers);
  templ.last_level = static_cast<uint8_t>(levels - 1);
  if (samples > 1) templ.nr_samples = templ.nr_storage_samples = static_cast<uint8_t>(samples); /* C32 */
  templ.usage = PIPE_USAGE_DEFAULT;
  templ.bind = ((bind & AGX_D3D12_BIND_RENDER_TARGET) ? PIPE_BIND_RENDER_TARGET : 0) |
               ((bind & AGX_D3D12_BIND_SAMPLER) ? PIPE_BIND_SAMPLER_VIEW : 0) |
               ((bind & AGX_D3D12_BIND_DEPTH_STENCIL) ? PIPE_BIND_DEPTH_STENCIL : 0);
  /* no explicit PIPE_BIND_SHADER_IMAGE (EXP1373, see the buffer path):
   * set_shader_images decompresses a written image itself */
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

extern "C" HRESULT AgxD3d12OwnerCreateReservedTexture2D(AGX_D3D12_OWNER *owner, DXGI_FORMAT format, unsigned width,
                                                        unsigned height, unsigned layers, unsigned levels,
                                                        unsigned bind, AGX_D3D12_BUFFER *texture,
                                                        AGX_D3D12_RESERVED_TILING *tiling) {
  OwnerLock lock(owner);
  unsigned bw = 0, bh = 0;
  if (!owner || !owner->Screen || !texture || !tiling || !width || !height || !layers || !levels || levels > 16 ||
      height > 0xffffu || layers > 0xffffu || !AgxD3d12FormatBlock(format, &bw, &bh) ||
      (bind & AGX_D3D12_BIND_DEPTH_STENCIL) || (bw > 1 && (bind & AGX_D3D12_BIND_RENDER_TARGET)))
    return E_INVALIDARG;
  memset(texture, 0, sizeof(*texture));
  memset(tiling, 0, sizeof(*tiling));
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
  struct pipe_resource *resource = templ.format != PIPE_FORMAT_NONE ?
      AgxWin32AsahiCreateReservedTexture(owner->Screen, &templ) : NULL;
  if (resource && !AgxWin32AsahiReservedTextureTiling(resource, &tiling->StandardMips, tiling->TilesX,
                                                      tiling->TilesY, &tiling->PackedTiles))
    AgxWin32AsahiResourceRelease(&resource);
  receipt("measure-d3d12-reserved-texture", resource ? S_OK : E_NOTIMPL, static_cast<UINT>(templ.format),
          width | (height << 16), layers | (levels << 16));
  if (!resource) return E_NOTIMPL;
  texture->Resource = resource;
  texture->GpuVa = AgxWin32AsahiBufferGpuAddress(resource);
  return S_OK;
}

extern "C" HRESULT AgxD3d12OwnerMapReservedTextureTile(AGX_D3D12_OWNER *owner, const AGX_D3D12_BUFFER *reserved,
                                                       unsigned layer, BOOL packed, unsigned mip, unsigned x,
                                                       unsigned y, const AGX_D3D12_BUFFER *backing,
                                                       unsigned backing_tile) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !reserved || !reserved->Resource || (backing && !backing->Resource))
    return E_INVALIDARG;
  /* recorded, unsubmitted work names the old tiles: submit it first */
  if (owner->PendingDraws || owner->PendingCopies || owner->PendingDispatches) {
    const HRESULT flushed = AgxD3d12OwnerSubmitAndWait(owner);
    if (FAILED(flushed)) return flushed;
  }
  const int mapped = AgxWin32AsahiMapReservedTextureTile(
      static_cast<struct pipe_resource *>(reserved->Resource), layer, packed ? 1 : 0, mip, x, y,
      backing ? static_cast<struct pipe_resource *>(backing->Resource) : NULL, backing_tile);
  return mapped ? S_OK : E_FAIL;
}

/* C21 LIMITED volume contract. Same pure template/query for allocation-info
 * and actual creation; never construct a screen/context just to obtain size. */
static bool texture3d_template(DXGI_FORMAT format, unsigned width, unsigned height,
    unsigned depth, unsigned levels, unsigned bind, struct pipe_resource *templ) {
  if (!templ || !width || width>32 || height!=width || (width&(width-1)) || !depth || depth>2048 ||
      !levels || levels>2 || (format!=DXGI_FORMAT_R32_UINT && format!=DXGI_FORMAT_R8G8B8A8_UNORM) ||
      (bind & ~(AGX_D3D12_BIND_RENDER_TARGET | AGX_D3D12_BIND_SAMPLER))) return false;
  unsigned extent=width>height ? width:height;
  if(depth>extent) extent=depth;
  unsigned max_levels=1;
  while(extent>1) {extent>>=1;++max_levels;}
  if(levels>max_levels) return false;
  const enum pipe_format physical=FormatTranslate(format,FALSE);
  const struct util_format_description *desc=physical!=PIPE_FORMAT_NONE ? util_format_description(physical):NULL;
  if(!desc || !block_bytes(physical) || desc->colorspace==UTIL_FORMAT_COLORSPACE_ZS) return false;
  // Native screen-free query owns backend target/format/usage validation.
  memset(templ,0,sizeof(*templ));
  templ->target=PIPE_TEXTURE_3D;templ->format=physical;
  templ->width0=width;templ->height0=static_cast<uint16_t>(height);templ->depth0=static_cast<uint16_t>(depth);
  templ->array_size=1;templ->last_level=static_cast<uint8_t>(levels-1);
  templ->nr_samples=templ->nr_storage_samples=1;templ->usage=PIPE_USAGE_DEFAULT;
  templ->bind=((bind&AGX_D3D12_BIND_RENDER_TARGET)?PIPE_BIND_RENDER_TARGET:0) |
              ((bind&AGX_D3D12_BIND_SAMPLER)?PIPE_BIND_SAMPLER_VIEW:0);
  return true;
}

static bool texture3d_layout(DXGI_FORMAT format,unsigned width,unsigned height,unsigned depth,
    unsigned levels,unsigned bind,struct pipe_resource *templ,
    AGX_WIN32_ASAHI_RESOURCE_LAYOUT *layout,unsigned long long *bytes) {
  *bytes=0;
  if(!texture3d_template(format,width,height,depth,levels,bind,templ) ||
      !AgxWin32AsahiQueryUncompressedResourceLayout(templ,layout) || !layout->SizeBytes ||
      layout->SizeBytes>0xffff0000ULL || (layout->Flags&AGX_WIN32_ASAHI_LAYOUT_COMPRESSED) ||
      layout->MetadataOffsetBytes || layout->CompressionLayerStrideBytes)
    return false;
  *bytes=(layout->SizeBytes+0xffffULL)&~0xffffULL;
  return true;
}

extern "C" HRESULT AgxD3d12QueryTexture3DLayout(DXGI_FORMAT format,unsigned width,unsigned height,
    unsigned depth,unsigned levels,unsigned bind,AGX_D3D12_VOLUME_LAYOUT *result) {
  if(!result) return E_INVALIDARG;
  memset(result,0,sizeof(*result));
  struct pipe_resource templ={};AGX_WIN32_ASAHI_RESOURCE_LAYOUT layout={};
  unsigned long long bytes=0;
  if(!texture3d_layout(format,width,height,depth,levels,bind,&templ,&layout,&bytes) ||
      !layout.LayerStrideBytes || layout.RequestedLevels!=levels || layout.AllocatedLevels<levels ||
      layout.AllocatedLevels>16 || layout.SizeBytes!=layout.LayerStrideBytes*depth) return E_NOTIMPL;
  for(unsigned m=0;m<levels;++m) {
    const unsigned side=(width>>m)?width>>m:1u, d=(depth>>m)?depth>>m:1u;
    const unsigned long long block=4ull*side*side,off=layout.LevelOffsets[m];
    if(off%block || off>layout.LayerStrideBytes || block>layout.LayerStrideBytes-off ||
       off+(d-1)*layout.LayerStrideBytes+block>layout.SizeBytes ||
       (m+1<layout.AllocatedLevels && off+block>layout.LevelOffsets[m+1])) return E_NOTIMPL;
  }
  result->Bytes=bytes;result->NativeBytes=layout.SizeBytes;result->DepthStride=layout.LayerStrideBytes;
  for(unsigned m=0;m<levels;++m) result->MipOffsets[m]=layout.LevelOffsets[m];
  return S_OK;
}

extern "C" HRESULT AgxD3d12QueryTexture3D(DXGI_FORMAT format,unsigned width,unsigned height,
    unsigned depth,unsigned levels,unsigned bind,unsigned long long *bytes,unsigned long long *mipOffsets) {
  if(!bytes) return E_INVALIDARG;
  if(mipOffsets) memset(mipOffsets,0,16*sizeof(*mipOffsets));
  struct pipe_resource templ={};AGX_WIN32_ASAHI_RESOURCE_LAYOUT layout={};
  if(!texture3d_layout(format,width,height,depth,levels,bind,&templ,&layout,bytes)) return E_NOTIMPL;
  if(mipOffsets) for(unsigned i=0;i<levels;++i) mipOffsets[i]=layout.LevelOffsets[i];
  return S_OK;
}

/* C23 ordinary nonarray1D: same complete native query for sizing and creation. */
static bool texture1d_template(DXGI_FORMAT format,unsigned width,unsigned levels,unsigned bind,struct pipe_resource *templ) {
  if(!templ || !width || width>16384 || !levels || levels>15 ||
     (format!=DXGI_FORMAT_R32_UINT && format!=DXGI_FORMAT_R8G8B8A8_UNORM) ||
     (bind&~AGX_D3D12_BIND_SAMPLER)) return false;
  unsigned legal=1;for(unsigned n=width;n>1;n>>=1)++legal;if(levels>legal)return false;
  const enum pipe_format physical=FormatTranslate(format,FALSE);
  if(physical==PIPE_FORMAT_NONE || block_bytes(physical)!=4)return false;
  memset(templ,0,sizeof(*templ));templ->target=PIPE_TEXTURE_1D_ARRAY;templ->format=physical;
  templ->width0=width;templ->height0=templ->depth0=templ->array_size=1;
  templ->last_level=static_cast<uint8_t>(levels-1);templ->nr_samples=templ->nr_storage_samples=1;
  templ->usage=PIPE_USAGE_DEFAULT;templ->bind=(bind&AGX_D3D12_BIND_SAMPLER)?PIPE_BIND_SAMPLER_VIEW:0;
  return true;
}
extern "C" HRESULT AgxD3d12QueryTexture1DLayout(DXGI_FORMAT format,unsigned width,unsigned levels,unsigned bind,
    AGX_D3D12_VOLUME_LAYOUT *result) {
  if(!result)return E_INVALIDARG;memset(result,0,sizeof(*result));
  struct pipe_resource templ={};AGX_WIN32_ASAHI_RESOURCE_LAYOUT layout={};
  if(!texture1d_template(format,width,levels,bind,&templ) ||
     !AgxWin32AsahiQueryUncompressedResourceLayout(&templ,&layout) || !layout.SizeBytes ||
     layout.SizeBytes>0xffff0000ULL || layout.SizeBytes!=layout.LayerStrideBytes ||
     layout.MetadataOffsetBytes || layout.CompressionLayerStrideBytes ||
     (layout.Flags&AGX_WIN32_ASAHI_LAYOUT_COMPRESSED) || layout.RequestedLevels!=levels ||
     layout.AllocatedLevels<levels || layout.AllocatedLevels>16)return E_NOTIMPL;
  for(unsigned m=0;m<levels;m++) {
    const unsigned w=width>>m ? width>>m:1u;
    const unsigned long long off=layout.LevelOffsets[m],span=4ull*w;
    if(off%4 || off>layout.SizeBytes || span>layout.SizeBytes-off ||
       (m+1<layout.AllocatedLevels && (layout.LevelOffsets[m+1]<off || span>layout.LevelOffsets[m+1]-off)))return E_NOTIMPL;
  }
  result->Bytes=(layout.SizeBytes+0xffffULL)&~0xffffULL;result->NativeBytes=layout.SizeBytes;
  result->DepthStride=layout.LayerStrideBytes;
  memcpy(result->MipOffsets,layout.LevelOffsets,levels*sizeof(result->MipOffsets[0]));return S_OK;
}
/* C24 bounds are pinned FL11_0 D3D12_REQ_TEXTURE1D_* / SUBRESOURCES. */
static bool texture1d_array_template(DXGI_FORMAT format,unsigned width,unsigned layers,unsigned levels,
    unsigned bind,struct pipe_resource *templ) {
  if(!layers || layers>2048 || !levels || levels>30720u/layers ||
     !texture1d_template(format,width,levels,bind,templ)) return false;
  templ->array_size=static_cast<uint16_t>(layers);return true;
}
extern "C" HRESULT AgxD3d12QueryTexture1DArrayLayout(DXGI_FORMAT format,unsigned width,unsigned layers,
    unsigned levels,unsigned bind,AGX_D3D12_VOLUME_LAYOUT *result) {
  if(!result)return E_INVALIDARG;memset(result,0,sizeof(*result));
  struct pipe_resource templ={};AGX_WIN32_ASAHI_RESOURCE_LAYOUT layout={};
  if(!texture1d_array_template(format,width,layers,levels,bind,&templ) ||
     !AgxWin32AsahiQueryUncompressedResourceLayout(&templ,&layout) || !layout.SizeBytes ||
     layout.SizeBytes>0xffff0000ULL || !layout.LayerStrideBytes ||
     layout.LayerStrideBytes>layout.SizeBytes/layers || layout.SizeBytes!=layout.LayerStrideBytes*layers ||
     layout.MetadataOffsetBytes || layout.CompressionLayerStrideBytes ||
     (layout.Flags&AGX_WIN32_ASAHI_LAYOUT_COMPRESSED) ||
     !(layout.Flags&AGX_WIN32_ASAHI_LAYOUT_WRITEABLE_IMAGE) ||
     !(layout.Flags&AGX_WIN32_ASAHI_LAYOUT_PAGE_ALIGNED_LAYERS) ||
     layout.RequestedLevels!=levels || layout.AllocatedLevels<levels || layout.AllocatedLevels>16)return E_NOTIMPL;
  for(unsigned m=0;m<levels;++m) {
    const unsigned w=width>>m?width>>m:1u;
    const unsigned long long off=layout.LevelOffsets[m],span=4ull*w;
    if(off%4 || off>layout.LayerStrideBytes || span>layout.LayerStrideBytes-off ||
       (m+1<layout.AllocatedLevels && (layout.LevelOffsets[m+1]<off || span>layout.LevelOffsets[m+1]-off)))return E_NOTIMPL;
  }
  result->Bytes=(layout.SizeBytes+0xffffULL)&~0xffffULL;result->NativeBytes=layout.SizeBytes;
  result->DepthStride=layout.LayerStrideBytes;
  memcpy(result->MipOffsets,layout.LevelOffsets,levels*sizeof(result->MipOffsets[0]));return S_OK;
}
extern "C" HRESULT AgxD3d12OwnerCreateTexture1DArray(AGX_D3D12_OWNER *owner,DXGI_FORMAT format,
    unsigned width,unsigned layers,unsigned levels,unsigned bind,unsigned long long bytes,AGX_D3D12_BUFFER *texture) {
  OwnerLock lock(owner);
  if(!texture)return E_INVALIDARG;memset(texture,0,sizeof(*texture));
  AGX_D3D12_VOLUME_LAYOUT checked={};struct pipe_resource templ={};AGX_WIN32_ASAHI_RESOURCE_LAYOUT layout={};
  if(!owner || !owner->Screen || FAILED(AgxD3d12QueryTexture1DArrayLayout(format,width,layers,levels,bind,&checked)) ||
     bytes!=checked.Bytes || !texture1d_array_template(format,width,layers,levels,bind,&templ) ||
     !AgxWin32AsahiQueryUncompressedResourceLayout(&templ,&layout))return E_NOTIMPL;
  if(!owner->Screen->is_format_supported || !owner->Screen->resource_create_with_modifiers ||
     !owner->Screen->is_format_supported(owner->Screen,templ.format,templ.target,1,1,templ.bind))return E_NOTIMPL;
  struct pipe_resource *resource=owner->Screen->resource_create_with_modifiers(owner->Screen,&templ,&layout.Modifier,1);
  if(!resource)return E_OUTOFMEMORY;
  texture->Resource=resource;texture->GpuVa=AgxWin32AsahiBufferGpuAddress(resource);texture->Bytes=bytes;return S_OK;
}

extern "C" HRESULT AgxD3d12OwnerCreateTexture1D(AGX_D3D12_OWNER *owner,DXGI_FORMAT format,unsigned width,
    unsigned levels,unsigned bind,unsigned long long bytes,AGX_D3D12_BUFFER *texture) {
  OwnerLock lock(owner);
  if(!texture)return E_INVALIDARG;memset(texture,0,sizeof(*texture));
  AGX_D3D12_VOLUME_LAYOUT checked={};struct pipe_resource templ={};AGX_WIN32_ASAHI_RESOURCE_LAYOUT layout={};
  if(!owner || !owner->Screen || FAILED(AgxD3d12QueryTexture1DLayout(format,width,levels,bind,&checked)) ||
     bytes!=checked.Bytes || !texture1d_template(format,width,levels,bind,&templ) ||
     !AgxWin32AsahiQueryUncompressedResourceLayout(&templ,&layout))return E_NOTIMPL;
  if(!owner->Screen->is_format_supported || !owner->Screen->resource_create_with_modifiers ||
     !owner->Screen->is_format_supported(owner->Screen,templ.format,templ.target,1,1,templ.bind))return E_NOTIMPL;
  struct pipe_resource *resource=owner->Screen->resource_create_with_modifiers(owner->Screen,&templ,&layout.Modifier,1);
  if(!resource)return E_OUTOFMEMORY;
  const unsigned long long va=AgxWin32AsahiBufferGpuAddress(resource);
  texture->Resource=resource;texture->GpuVa=va;texture->Bytes=bytes;return S_OK;
}

extern "C" HRESULT AgxD3d12OwnerCreateTexture3D(AGX_D3D12_OWNER *owner,DXGI_FORMAT format,
    unsigned width,unsigned height,unsigned depth,unsigned levels,unsigned bind,
    unsigned long long bytes,AGX_D3D12_BUFFER *texture) {
  OwnerLock lock(owner);
  if(!owner || !owner->Screen || !texture) return E_INVALIDARG;
  memset(texture,0,sizeof(*texture));
  struct pipe_resource templ={};AGX_WIN32_ASAHI_RESOURCE_LAYOUT layout={};
  unsigned long long actual=0;
  if(!texture3d_layout(format,width,height,depth,levels,bind,&templ,&layout,&actual) || actual!=bytes ||
      !owner->Screen->is_format_supported || !owner->Screen->resource_create_with_modifiers ||
      !owner->Screen->is_format_supported(owner->Screen,templ.format,templ.target,1,1,templ.bind)) return E_NOTIMPL;
  struct pipe_resource *resource=owner->Screen->resource_create_with_modifiers(owner->Screen,&templ,&layout.Modifier,1);
  if(!resource) return E_OUTOFMEMORY;
  texture->Resource=resource;texture->GpuVa=AgxWin32AsahiBufferGpuAddress(resource);texture->Bytes=actual;
  return S_OK;
}

static unsigned level_extent(unsigned extent, unsigned level) {
  extent >>= level;
  return extent ? extent : 1u;
}

static unsigned texture_layer_extent(const struct pipe_resource *resource,unsigned level) {
  return resource->target==PIPE_TEXTURE_3D ? (level>resource->last_level ? 0u:level_extent(resource->depth0,level)):resource->array_size;
}

/* Asahi has no clear_render_target/clear_depth_stencil (EXP1368: the
 * C6a clear called a NULL entry); like the D3D10 frontend overlay
 * (build-native-asahi-state.py ClearRenderTargetView, D3D-G10a) the view is
 * bound as a one-layer framebuffer per layer and cleared with pipe->clear,
 * D3D rects as the scissor. The submit drops the framebuffer binding.
 * EXP1882: the Windows agx_clear refuses a scissor (upstream asserts
 * clear_scissored is unsupported) and faults the context -- D3D9On12's
 * first Clear did that. A rect covering the whole surface clears without a
 * scissor; a partial rect is refused (returns false) before pipe->clear. */
static bool clear_view_layers(AGX_D3D12_OWNER *owner, const struct pipe_surface &surface, bool depth, unsigned buffers,
                              const union pipe_color_union *color, double z, unsigned stencil, const RECT *rects,
                              unsigned rect_count) {
  struct pipe_context *pipe = owner->Context;
  struct pipe_resource *resource = surface.texture;
  const unsigned width = level_extent(resource->width0, surface.level), height = level_extent(resource->height0, surface.level);
  bool whole = !rect_count;
  for (unsigned i = 0; i < rect_count; ++i) {
    const RECT &r = rects[i];
    if (r.left <= 0 && r.top <= 0 && r.right >= static_cast<LONG>(width) && r.bottom >= static_cast<LONG>(height))
      whole = true;
    else if (r.right > r.left && r.bottom > r.top && r.left < static_cast<LONG>(width) && r.top < static_cast<LONG>(height) &&
             r.right > 0 && r.bottom > 0) {
      const UINT receipt[] = {i, rect_count, static_cast<UINT>(r.left), static_cast<UINT>(r.top),
                              static_cast<UINT>(r.right), static_cast<UINT>(r.bottom), width, height};
      AdmissionUmdDiagnostic("measure-d3d12-clear-rect", E_NOTIMPL, receipt, 8u);
      return false;
    }
  }
  if (!whole) return true; /* every rect empty or outside: nothing to clear */
  for (unsigned layer = surface.first_layer; layer <= surface.last_layer; ++layer) {
    struct pipe_framebuffer_state fb = {};
    struct pipe_surface slice = surface;
    slice.first_layer = slice.last_layer = static_cast<uint16_t>(layer);
    fb.width = width;
    fb.height = height;
    fb.layers = 1;
    if (depth) {
      fb.zsbuf = slice;
    } else {
      fb.nr_cbufs = 1;
      fb.cbufs[0] = slice;
    }
    pipe->set_framebuffer_state(pipe, &fb);
    owner->DrawFramebufferValid = false;
    pipe->clear(pipe, buffers, 0xf, 0xff, NULL, color, z, stencil);
  }
  ++owner->PendingDraws; /* the submit unbinds the framebuffer */
  return true;
}

/* 1 = unsigned, -1 = signed integer color formats, 0 = float/normalized */
static int clear_integer_kind(DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32_UINT: case DXGI_FORMAT_R16G16B16A16_UINT:
  case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R10G10B10A2_UINT: case DXGI_FORMAT_R8G8B8A8_UINT:
  case DXGI_FORMAT_R16G16_UINT: case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R8G8_UINT: case DXGI_FORMAT_R16_UINT:
  case DXGI_FORMAT_R8_UINT:
    return 1;
  case DXGI_FORMAT_R32G32B32A32_SINT: case DXGI_FORMAT_R32G32B32_SINT: case DXGI_FORMAT_R16G16B16A16_SINT:
  case DXGI_FORMAT_R32G32_SINT: case DXGI_FORMAT_R8G8B8A8_SINT: case DXGI_FORMAT_R16G16_SINT:
  case DXGI_FORMAT_R32_SINT: case DXGI_FORMAT_R8G8_SINT: case DXGI_FORMAT_R16_SINT: case DXGI_FORMAT_R8_SINT:
    return -1;
  default:
    return 0;
  }
}

extern "C" HRESULT AgxD3d12OwnerClearRenderTarget(AGX_D3D12_OWNER *owner, const AGX_D3D12_BUFFER *texture,
                                                  DXGI_FORMAT view_format, unsigned level, unsigned first_layer,
                                                  unsigned layers, const float color[4], const RECT *rects,
                                                  unsigned rect_count) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !texture || !texture->Resource || !color || !layers) return E_INVALIDARG;
  struct pipe_resource *resource = static_cast<struct pipe_resource *>(texture->Resource);
  const enum pipe_format format = FormatTranslate(view_format, FALSE);
  if (format == PIPE_FORMAT_NONE || level > resource->last_level) return E_INVALIDARG;
  if(resource->target==PIPE_TEXTURE_3D) {
    if(layers!=1 || first_layer>=texture_layer_extent(resource,level)) return E_INVALIDARG;
  } else if(first_layer >= resource->array_size || layers > resource->array_size - first_layer)
    return E_INVALIDARG;
  struct pipe_surface surface = {};
  surface.format = format;
  surface.level = level;
  surface.first_layer = static_cast<uint16_t>(first_layer);
  surface.last_layer = static_cast<uint16_t>(first_layer + layers - 1);
  surface.texture = resource;
  /* C57 (EXP1882): Asahi clears integer targets from pipe_color_union.ui/.i;
   * D3D converts the FLOAT[4] clear color to the integer type (truncation,
   * NaN to 0, saturation to the type's range; the format width clamps next). */
  const int integer = clear_integer_kind(view_format);
  union pipe_color_union value = {};
  for (unsigned i = 0; i < 4; ++i) {
    const float f = color[i];
    if (integer > 0)
      value.ui[i] = !(f > 0.0f) ? 0u : f >= 4294967295.0f ? 0xffffffffu : static_cast<uint32_t>(f);
    else if (integer < 0)
      value.i[i] = !(f == f) ? 0 : f <= -2147483648.0f ? INT32_MIN : f >= 2147483647.0f ? INT32_MAX : static_cast<int32_t>(f);
    else
      value.f[i] = f;
  }
  return clear_view_layers(owner, surface, false, PIPE_CLEAR_COLOR0, &value, 0.0, 0, rects, rect_count) ? S_OK : E_NOTIMPL;
}

/* C7b: the D3D10 frontend's presentation resources for D3D12 swapchain
 * buffers (AgxD3d10WindowsPresentationCreate + attach_presentation_render_resource) */
extern "C" HRESULT AgxD3d12OwnerCreatePresentationTexture(AGX_D3D12_OWNER *owner, DXGI_FORMAT format, unsigned width,
                                                          unsigned height, int shader_resource, HANDLE runtime_resource,
                                                          AGX_D3D12_BUFFER *texture, void **presentation, UINT *allocation) {
  OwnerLock lock(owner);
  if (!owner || !owner->Screen || !owner->Context || !runtime_resource || !texture || !presentation || !allocation)
    return E_INVALIDARG;
  memset(texture, 0, sizeof(*texture));
  *presentation = NULL;
  *allocation = 0;
  /* C40 (D3D12-C40-10bit-swapchains.md): the admission UMD, KMD and DWM
   * presentation contracts store BGRA8/RGBA8 only. A 10-bit swapchain
   * buffer keeps an RGBA8 presentation allocation and renders into its own
   * R10G10B10A2 texture, converted into it at each present. */
  AGX_D3D12_BUFFER converted = {};
  if (format == DXGI_FORMAT_R10G10B10A2_UNORM) {
    const HRESULT made = AgxD3d12OwnerCreateTexture2D(owner, format, width, height, 1u, 1u,
                                                      AGX_D3D12_BIND_RENDER_TARGET | (shader_resource ? AGX_D3D12_BIND_SAMPLER : 0u),
                                                      4ull * width * height, &converted, 1u);
    if (FAILED(made)) return made;
    format = DXGI_FORMAT_R8G8B8A8_UNORM;
  }
  (void)collect_presentations(owner);
  auto *record = static_cast<struct AGX_D3D12_PRESENTATION *>(
      HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(struct AGX_D3D12_PRESENTATION)));
  if (!record) {
    if (converted.Resource) {
      struct pipe_resource *resource = static_cast<struct pipe_resource *>(converted.Resource);
      AgxWin32AsahiResourceRelease(&resource);
    }
    return E_OUTOFMEMORY;
  }
  record->Converted = static_cast<struct pipe_resource *>(converted.Resource);
  D3D10DDI_MIPINFO mip = {width, height, 1u, width, height, 1u};
  D3D11DDIARG_CREATERESOURCE create = {};
  create.pMipInfoList = &mip;
  create.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
  create.Usage = D3D10_DDI_USAGE_DEFAULT;
  create.BindFlags = D3D10_DDI_BIND_RENDER_TARGET | D3D10_DDI_BIND_PRESENT |
                     (shader_resource ? D3D10_DDI_BIND_SHADER_RESOURCE : 0u);
  create.Format = format;
  create.SampleDesc.Count = 1;
  create.MipLevels = 1;
  create.ArraySize = 1;
  D3D10DDI_HDEVICE device = {};
  D3D10DDI_HRESOURCE resource = {};
  D3D10DDI_HRTRESOURCE runtime = {};
  device.pDrvPrivate = &owner->Runtime;
  resource.pDrvPrivate = &record->Resource;
  runtime.handle = runtime_resource;
  AdmissionUmdCreateResource(device, &create, resource, runtime);
  HRESULT hr = record->Resource.Magic == ADMISSION_UMD_RESOURCE_MAGIC && record->Resource.Retirement
                   ? S_OK : (FAILED(owner->Runtime.LastScreenError) ? owner->Runtime.LastScreenError : E_INVALIDARG);
  if (SUCCEEDED(hr)) {
    const ADMISSION_ALLOCATION_DESCRIPTION *desc = &record->Resource.DirectFlip.Allocation;
    const AGX_WIN32_ASAHI_LINEAR_FORMAT linear = desc->Format == (UINT)D3DDDIFMT_A8R8G8B8 ? AgxWin32AsahiLinearFormatBgra8Unorm
                                                                                          : AgxWin32AsahiLinearFormatRgba8Unorm;
    const APPLE_AGX_U32 access = AppleAgxWin32BufferCpuRead | AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead |
                                 AppleAgxWin32BufferGpuWrite;
    if (desc->Format != (UINT)D3DDDIFMT_A8R8G8B8 && desc->Format != (UINT)D3DDDIFMT_A8B8G8R8) hr = E_INVALIDARG;
    if (SUCCEEDED(hr))
      hr = AdmissionUmdScreenAdoptAllocation(&owner->Runtime, record->Resource.KernelAllocation, desc->Size, 0x10000ULL,
                                             AgxWin32BufferClassGeneral, access, record->Resource.WrittenPrimary,
                                             desc->CpuVisible == 0u, &record->RenderBuffer);
    if (SUCCEEDED(hr)) {
      record->RenderResource = AgxWin32AsahiImportLinearColor32(owner->Screen, &record->RenderBuffer, desc->Width,
                                                                desc->Height, desc->Pitch, desc->Size, linear);
      if (!record->RenderResource) {
        (void)AgxWin32ScreenDestroyBuffer(&owner->Runtime.Screen, &record->RenderBuffer);
        memset(&record->RenderBuffer, 0, sizeof(record->RenderBuffer));
        hr = FAILED(owner->Runtime.LastScreenError) ? owner->Runtime.LastScreenError : E_FAIL;
      }
    }
    if (FAILED(hr)) AdmissionUmdDestroyResource(device, resource);
  }
  receipt("measure-d3d12-presentation", hr, static_cast<UINT>(format), width | (height << 16),
          record->Resource.KernelAllocation);
  if (FAILED(hr)) {
    if (record->Converted) AgxWin32AsahiResourceRelease(&record->Converted);
    HeapFree(GetProcessHeap(), 0, record);
    return hr;
  }
  struct pipe_resource *rendered = record->Converted ? record->Converted : record->RenderResource;
  texture->Resource = rendered;
  texture->GpuVa = AgxWin32AsahiBufferGpuAddress(rendered);
  texture->Bytes = record->Resource.DirectFlip.Allocation.Size;
  *presentation = record;
  *allocation = record->Resource.KernelAllocation;
  return S_OK;
}

/* The runtime frees its resource when DestroyHeapAndResource returns, so
 * the runtime-owned kernel resource goes now and the record waits until
 * the screen stops tracking the allocation (agx_d3d10_windows.cpp
 * AgxD3d10WindowsPresentationDestroy). */
extern "C" void AgxD3d12OwnerDestroyPresentationTexture(AGX_D3D12_OWNER *owner, void *presentation) {
  OwnerLock lock(owner);
  auto *record = static_cast<struct AGX_D3D12_PRESENTATION *>(presentation);
  if (!owner || !record) return;
  if (record->RenderResource) AgxWin32AsahiResourceRelease(&record->RenderResource);
  if (record->Converted) AgxWin32AsahiResourceRelease(&record->Converted);
  if (owner->Context) owner->Context->flush(owner->Context, NULL, 0);
  D3D10DDI_HDEVICE device = {};
  D3D10DDI_HRESOURCE resource = {};
  device.pDrvPrivate = &owner->Runtime;
  resource.pDrvPrivate = &record->Resource;
  HRESULT released = AdmissionUmdReleaseRuntimeResource(device, resource);
  if (FAILED(released)) AdmissionUmdSetError(&owner->Runtime, released);
  record->Next = owner->PendingPresentations;
  owner->PendingPresentations = record;
  (void)collect_presentations(owner);
}

/* C10: one pixel of a typed UAV's format (agx_d3d11_ddi.cpp pack_uint_bits
 * for Uint; the format's float packer for Float, pure-integer formats
 * taking the values converted to integers) */
static bool clear_pixel(enum pipe_format format, const void *values, int is_float, uint8_t out[16]) {
  const struct util_format_description *desc = util_format_description(format);
  memset(out, 0, 16);
  if (!desc || desc->block.width != 1 || desc->block.height != 1 || desc->block.bits > 128) return false;
  UINT bits32[4] = {};
  const bool integer = util_format_is_pure_uint(format) || util_format_is_pure_sint(format);
  if (is_float && !integer) {
    const struct util_format_pack_description *pack = util_format_pack_description(format);
    if (!pack || !pack->pack_rgba_float) return false;
    pack->pack_rgba_float(out, 0, static_cast<const float *>(values), 0, 1, 1);
    return true;
  }
  for (unsigned c = 0; c < 4; ++c)
    bits32[c] = is_float ? static_cast<UINT>(static_cast<int>(static_cast<const float *>(values)[c]))
                         : static_cast<const UINT *>(values)[c];
  for (unsigned c = 0; c < desc->nr_channels && c < 4; ++c) {
    const unsigned size = desc->channel[c].size, shift = desc->channel[c].shift;
    const uint64_t v = size >= 32 ? bits32[c] : (bits32[c] & ((1u << size) - 1u));
    for (unsigned b = 0; b < size && shift + b < 128; ++b)
      if (v & (1ull << b)) out[(shift + b) / 8] |= static_cast<uint8_t>(1u << ((shift + b) % 8));
  }
  return true;
}

extern "C" HRESULT AgxD3d12OwnerClearUav(AGX_D3D12_OWNER *owner, const AGX_D3D12_UAV *uav, const void *values,
                                         int is_float, const RECT *rects, unsigned rect_count) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !uav || !uav->Buffer || !uav->Buffer->Resource || !values || (rect_count && !rects))
    return E_INVALIDARG;
  struct pipe_context *pipe = owner->Context;
  struct pipe_resource *resource = static_cast<struct pipe_resource *>(uav->Buffer->Resource);
  const unsigned long long room = uav->Offset < uav->Buffer->Bytes ? uav->Buffer->Bytes - uav->Offset : 0;
  const unsigned long long bytes = uav->Bytes && uav->Bytes < room ? uav->Bytes : room;
  if (!uav->Format) {
    /* raw/structured: Values[0] in every word; a float clear has no
     * defined meaning for untyped views */
    if (is_float || resource->target != PIPE_BUFFER || !bytes || uav->Buffer->Bytes > 0xffffffffull ||
        (uav->Offset & 3u) || (bytes & 3u))
      return E_INVALIDARG;
    for (unsigned r = 0; r < (rect_count ? rect_count : 1u); ++r) {
      unsigned long long first = 0, last = bytes / 4;
      if (rect_count) {
        first = rects[r].left < 0 ? 0 : static_cast<unsigned long long>(rects[r].left);
        last = static_cast<unsigned long long>(rects[r].right) < last ? static_cast<unsigned long long>(rects[r].right) : last;
        if (rects[r].right <= rects[r].left || first >= last) continue;
      }
      pipe->clear_buffer(pipe, resource, static_cast<unsigned>(uav->Offset + first * 4),
                         static_cast<unsigned>((last - first) * 4), values, 4);
    }
    ++owner->PendingDispatches;
    return S_OK;
  }
  const enum pipe_format format = FormatTranslate(uav->Format, FALSE);
  uint8_t pixel[16];
  if (format == PIPE_FORMAT_NONE || !clear_pixel(format, values, is_float, pixel)) return E_INVALIDARG;
  const unsigned block = util_format_description(format)->block.bits / 8;
  if (!uav->Texture) {
    if (resource->target != PIPE_BUFFER || !bytes || !block || uav->Buffer->Bytes > 0xffffffffull) return E_INVALIDARG;
    for (unsigned r = 0; r < (rect_count ? rect_count : 1u); ++r) {
      unsigned long long first = 0, last = bytes / block;
      if (rect_count) {
        first = rects[r].left < 0 ? 0 : static_cast<unsigned long long>(rects[r].left);
        last = static_cast<unsigned long long>(rects[r].right) < last ? static_cast<unsigned long long>(rects[r].right) : last;
        if (rects[r].right <= rects[r].left || first >= last) continue;
      }
      pipe->clear_buffer(pipe, resource, static_cast<unsigned>(uav->Offset + first * block),
                         static_cast<unsigned>((last - first) * block), pixel, static_cast<int>(block));
    }
    ++owner->PendingDispatches;
    return S_OK;
  }
  const unsigned layers=texture_layer_extent(resource,uav->Level);
  if (resource->target == PIPE_BUFFER || uav->Level > resource->last_level || !uav->Layers ||
      uav->FirstLayer >= layers || uav->Layers > layers-uav->FirstLayer ||
      (resource->target==PIPE_TEXTURE_3D && (uav->FirstLayer || uav->Layers!=layers))) return E_INVALIDARG;
  const unsigned width = resource->width0 >> uav->Level ? resource->width0 >> uav->Level : 1u;
  const unsigned height = resource->height0 >> uav->Level ? resource->height0 >> uav->Level : 1u;
  for (unsigned r = 0; r < (rect_count ? rect_count : 1u); ++r) {
    struct pipe_box box = {};
    box.width = static_cast<int>(width);
    box.height = static_cast<int>(height);
    if (rect_count) {
      const LONG l = rects[r].left < 0 ? 0 : rects[r].left, t = rects[r].top < 0 ? 0 : rects[r].top;
      const LONG rr = rects[r].right > static_cast<LONG>(width) ? static_cast<LONG>(width) : rects[r].right;
      const LONG b = rects[r].bottom > static_cast<LONG>(height) ? static_cast<LONG>(height) : rects[r].bottom;
      if (rr <= l || b <= t) continue;
      box.x = static_cast<int>(l);
      box.y = static_cast<int>(t);
      box.width = static_cast<int>(rr - l);
      box.height = static_cast<int>(b - t);
    }
    box.z = static_cast<int16_t>(uav->FirstLayer);
    box.depth = static_cast<int16_t>(uav->Layers);
    /* Asahi implements no clear_texture (EXP1330): Gallium's default
     * clears through a render target or on the CPU */
    if (pipe->clear_texture)
      pipe->clear_texture(pipe, resource, uav->Level, &box, pixel);
    else
      u_default_clear_texture(pipe, resource, uav->Level, &box, pixel);
  }
  ++owner->PendingDispatches;
  return S_OK;
}

/* C11: query heaps (agx_query.c: occlusion counters/predicates, timestamps
 * with the CPU-fallback copy, PIPELINE_STATISTICS_SINGLE per counter) */
struct AGX_D3D12_QUERY_HEAP {
  unsigned Type, Count, Slots;
  struct pipe_query *Query[1]; /* Count * Slots */
};
static const unsigned kQueryStatistics = 11; /* D3D12_QUERY_DATA_PIPELINE_STATISTICS */

extern "C" HRESULT AgxD3d12OwnerCreateQueryHeap(AGX_D3D12_OWNER *owner, unsigned type, unsigned count,
                                                AGX_D3D12_QUERY_HEAP **heap) {
  OwnerLock lock(owner);
  if (heap) *heap = NULL;
  if (!owner || !owner->Context || !heap || !count || type > 2u || count > (1u << 20)) return E_INVALIDARG;
  const unsigned slots = type == 0 ? 2u : type == 1 ? 1u : kQueryStatistics;
  const SIZE_T bytes = sizeof(AGX_D3D12_QUERY_HEAP) + (static_cast<SIZE_T>(count) * slots - 1) * sizeof(struct pipe_query *);
  auto *h = static_cast<AGX_D3D12_QUERY_HEAP *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes));
  if (!h) return E_OUTOFMEMORY;
  h->Type = type;
  h->Count = count;
  h->Slots = slots;
  *heap = h;
  return S_OK;
}

extern "C" void AgxD3d12OwnerDestroyQueryHeap(AGX_D3D12_OWNER *owner, AGX_D3D12_QUERY_HEAP *heap) {
  OwnerLock lock(owner);
  if (!heap) return;
  for (unsigned i = 0; owner && owner->Context && i < heap->Count * heap->Slots; ++i)
    if (heap->Query[i]) owner->Context->destroy_query(owner->Context, heap->Query[i]);
  HeapFree(GetProcessHeap(), 0, heap);
}

/* the Gallium query of slot s of query index under a D3D12 query type */
static struct pipe_query *heap_query(struct pipe_context *pipe, AGX_D3D12_QUERY_HEAP *heap, unsigned index, unsigned s,
                                     unsigned pipe_type, unsigned pipe_index) {
  struct pipe_query **slot = &heap->Query[index * heap->Slots + s];
  if (!*slot) *slot = pipe->create_query(pipe, pipe_type, pipe_index);
  return *slot;
}

extern "C" HRESULT AgxD3d12OwnerQuery(AGX_D3D12_OWNER *owner, AGX_D3D12_QUERY_HEAP *heap, unsigned type, unsigned index,
                                      int begin) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !heap || index >= heap->Count) return E_INVALIDARG;
  struct pipe_context *pipe = owner->Context;
  /* D3D12_QUERY_TYPE: 0 occlusion, 1 binary occlusion, 2 timestamp (end
   * only), 3 pipeline statistics */
  if ((heap->Type == 0 && type > 1u) || (heap->Type == 1 && (type != 2u || begin)) || (heap->Type == 2 && type != 3u))
    return E_INVALIDARG;
  if (heap->Type == 2) {
    for (unsigned s = 0; s < kQueryStatistics; ++s) {
      struct pipe_query *q = heap_query(pipe, heap, index, s, PIPE_QUERY_PIPELINE_STATISTICS_SINGLE, s);
      if (!q || !(begin ? pipe->begin_query(pipe, q) : pipe->end_query(pipe, q))) return E_FAIL;
    }
    return S_OK;
  }
  const unsigned s = type == 1u ? 1u : 0u;
  const unsigned pipe_type = heap->Type == 1 ? PIPE_QUERY_TIMESTAMP
                           : type == 1u     ? PIPE_QUERY_OCCLUSION_PREDICATE : PIPE_QUERY_OCCLUSION_COUNTER;
  struct pipe_query *q = heap_query(pipe, heap, index, s, pipe_type, 0);
  if (!q || !(begin ? pipe->begin_query(pipe, q) : pipe->end_query(pipe, q))) return E_FAIL;
  return S_OK;
}

extern "C" HRESULT AgxD3d12OwnerResolveQueries(AGX_D3D12_OWNER *owner, AGX_D3D12_QUERY_HEAP *heap, unsigned type,
                                               unsigned start, unsigned count, const AGX_D3D12_BUFFER *dst,
                                               unsigned long long offset) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !heap || !dst || !dst->Resource || start > heap->Count ||
      count > heap->Count - start)
    return E_INVALIDARG;
  const unsigned values = heap->Type == 2 ? kQueryStatistics : 1u;
  const unsigned long long bytes = static_cast<unsigned long long>(count) * values * 8u;
  if (offset > dst->Bytes || bytes > dst->Bytes - offset || dst->Bytes > 0xffffffffull || (offset & 7u))
    return E_INVALIDARG;
  struct pipe_context *pipe = owner->Context;
  struct pipe_resource *resource = static_cast<struct pipe_resource *>(dst->Resource);
  static const UINT64 zero = 0;
  for (unsigned i = 0; i < count; ++i) {
    for (unsigned v = 0; v < values; ++v) {
      const unsigned s = heap->Type == 2 ? v : (type == 1u ? 1u : 0u);
      struct pipe_query *q = heap->Query[(start + i) * heap->Slots + s];
      const unsigned at = static_cast<unsigned>(offset + (static_cast<unsigned long long>(i) * values + v) * 8u);
      if (q)
        pipe->get_query_result_resource(pipe, q, PIPE_QUERY_WAIT, PIPE_QUERY_TYPE_U64, 0, resource, at);
      else
        pipe->clear_buffer(pipe, resource, at, 8u, &zero, 8);
    }
  }
  ++owner->PendingDispatches;
  return S_OK;
}

/* C7c: the D3D12 present hands the runtime the allocation; rendering on
 * the owner context must be flushed and the direct map published first
 * (AgxD3d10WindowsPresentationSubmitSurfaces before AdmissionUmdSubmitPresent) */
extern "C" HRESULT AgxD3d12OwnerPreparePresent(AGX_D3D12_OWNER *owner, void *presentation, UINT *allocation) {
  OwnerLock lock(owner);
  auto *record = static_cast<struct AGX_D3D12_PRESENTATION *>(presentation);
  if (allocation) *allocation = 0;
  if (!owner || !owner->Context || !record || !allocation || !record->Resource.KernelAllocation) return E_INVALIDARG;
  if (record->Converted && record->RenderResource) {
    /* C40: the rendered 10-bit texture into the RGBA8 presentation storage */
    struct pipe_blit_info blit = {};
    blit.src.resource = record->Converted;
    blit.src.format = record->Converted->format;
    blit.dst.resource = record->RenderResource;
    blit.dst.format = record->RenderResource->format;
    blit.src.box.width = blit.dst.box.width = static_cast<int>(record->Converted->width0);
    blit.src.box.height = blit.dst.box.height = static_cast<int>(record->Converted->height0);
    blit.src.box.depth = blit.dst.box.depth = 1;
    blit.mask = PIPE_MASK_RGBA;
    blit.filter = PIPE_TEX_FILTER_NEAREST;
    owner->Context->blit(owner->Context, &blit);
  }
  HRESULT hr = AgxWin32AsahiContextFlushForPresent(owner->Context) ? S_OK : E_FAIL;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if (SUCCEEDED(hr) && record->RenderBuffer.Transport.Token &&
      !AdmissionUmdGpuvaPublishDirectMap(&owner->Runtime, record->RenderBuffer.Transport.Token))
    hr = E_FAIL;
#endif
  receipt("measure-d3d12-prepare-present", hr, record->Resource.KernelAllocation,
          static_cast<UINT>(record->RenderBuffer.Transport.Token), 0u);
  if (SUCCEEDED(hr)) *allocation = record->Resource.KernelAllocation;
  return hr;
}

/* C8b: texture-to-texture copies on the GPU */
/* texture-to-texture region copies (D3D12 CopyTextureRegion between
 * subresources): whole elements -- texels, or the BC1-BC7 4x4 blocks C29
 * copies to and from buffers (EXP1838 MiniEngine: BC3 mip copies were
 * refused, block_bytes knows texels only) -- of formats with the same
 * element size and shape, inside both levels and layer ranges; origins on
 * block boundaries, a partial block only at the source level's edge */
static bool texture_region_copy_valid(const struct pipe_resource *d, unsigned dst_level, unsigned dst_layer, unsigned dst_x,
                                      unsigned dst_y, const struct pipe_resource *s, unsigned src_level, unsigned src_layer,
                                      unsigned src_x, unsigned src_y, unsigned width, unsigned height, unsigned layers) {
  unsigned dbw = 0, dbh = 0, sbw = 0, sbh = 0;
  if (d->target == PIPE_BUFFER || s->target == PIPE_BUFFER || dst_level > d->last_level || src_level > s->last_level)
    return false;
  const unsigned dbytes = texture_block(d->format, &dbw, &dbh), sbytes = texture_block(s->format, &sbw, &sbh);
  if (!dbytes || dbytes != sbytes || dbw != sbw || dbh != sbh) return false;
  const unsigned sw = level_extent(s->width0, src_level), sh = level_extent(s->height0, src_level);
  if (dst_x + width > level_extent(d->width0, dst_level) || dst_y + height > level_extent(d->height0, dst_level) ||
      src_x + width > sw || src_y + height > sh ||
      dst_layer + layers > texture_layer_extent(d, dst_level) || src_layer + layers > texture_layer_extent(s, src_level))
    return false;
  if (dst_x % dbw || dst_y % dbh || src_x % sbw || src_y % sbh) return false;
  return (width % sbw == 0 || src_x + width == sw) && (height % sbh == 0 || src_y + height == sh);
}

extern "C" HRESULT AgxD3d12OwnerCopyTextureRegion(AGX_D3D12_OWNER *owner, const AGX_D3D12_BUFFER *dst, unsigned dst_level,
                                                  unsigned dst_layer, unsigned dst_x, unsigned dst_y,
                                                  const AGX_D3D12_BUFFER *src, unsigned src_level, unsigned src_layer,
                                                  unsigned src_x, unsigned src_y, unsigned width, unsigned height,
                                                  unsigned layers) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !dst || !dst->Resource || !src || !src->Resource || !width || !height || !layers)
    return E_INVALIDARG;
  struct pipe_resource *d = static_cast<struct pipe_resource *>(dst->Resource);
  struct pipe_resource *s = static_cast<struct pipe_resource *>(src->Resource);
  if(d->target==PIPE_TEXTURE_3D || s->target==PIPE_TEXTURE_3D) {
    if(d->target!=PIPE_TEXTURE_3D || s->target!=PIPE_TEXTURE_3D || dst_level>d->last_level || src_level>s->last_level ||
        dst_x>level_extent(d->width0,dst_level) || width>level_extent(d->width0,dst_level)-dst_x ||
        dst_y>level_extent(d->height0,dst_level) || height>level_extent(d->height0,dst_level)-dst_y ||
        src_x>level_extent(s->width0,src_level) || width>level_extent(s->width0,src_level)-src_x ||
        src_y>level_extent(s->height0,src_level) || height>level_extent(s->height0,src_level)-src_y ||
        dst_layer>texture_layer_extent(d,dst_level) || layers>texture_layer_extent(d,dst_level)-dst_layer ||
        src_layer>texture_layer_extent(s,src_level) || layers>texture_layer_extent(s,src_level)-src_layer) return E_INVALIDARG;
  }
  if (!texture_region_copy_valid(d, dst_level, dst_layer, dst_x, dst_y, s, src_level, src_layer, src_x, src_y, width,
                                 height, layers)) {
    receipt("measure-d3d12-texture-region-copy", E_INVALIDARG, static_cast<UINT>(d->format) | (static_cast<UINT>(s->format) << 16),
            dst_level | (src_level << 8) | (static_cast<UINT>(d->last_level) << 16) | (static_cast<UINT>(s->last_level) << 24),
            width | (height << 16));
    return E_INVALIDARG;
  }
  struct pipe_box box = {};
  box.x = static_cast<int>(src_x);
  box.y = static_cast<int>(src_y);
  box.z = static_cast<int16_t>(src_layer);
  box.width = static_cast<int>(width);
  box.height = static_cast<int>(height);
  box.depth = static_cast<int16_t>(layers);
  owner->Context->resource_copy_region(owner->Context, d, dst_level, dst_x, dst_y, dst_layer, s, src_level, &box);
  ++owner->PendingCopies;
  return S_OK;
}

/* C32: a whole-level resolve blit; Gallium averages colour samples */
extern "C" HRESULT AgxD3d12OwnerResolve(AGX_D3D12_OWNER *owner, const AGX_D3D12_BUFFER *dst, unsigned dst_level,
                                        unsigned dst_layer, const AGX_D3D12_BUFFER *src, unsigned src_layer,
                                        DXGI_FORMAT format) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !dst || !dst->Resource || !src || !src->Resource) return E_INVALIDARG;
  struct pipe_resource *d = static_cast<struct pipe_resource *>(dst->Resource);
  struct pipe_resource *s = static_cast<struct pipe_resource *>(src->Resource);
  const enum pipe_format view = FormatTranslate(format, FALSE);
  const struct util_format_description *desc = view != PIPE_FORMAT_NONE ? util_format_description(view) : NULL;
  if (!desc || desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS || s->nr_samples <= 1 || d->nr_samples > 1 ||
      d->target == PIPE_BUFFER || dst_level > d->last_level || dst_layer >= d->array_size || src_layer >= s->array_size ||
      level_extent(d->width0, dst_level) != s->width0 || level_extent(d->height0, dst_level) != s->height0)
    return E_INVALIDARG;
  /* the formats u_blitter needs (is_blit_generic_supported): agx_blit
   * traps on an unsupported blit instead of failing */
  struct pipe_screen *screen = owner->Context->screen;
  if (!screen->is_format_supported(screen, view, d->target, d->nr_samples, d->nr_storage_samples, PIPE_BIND_RENDER_TARGET) ||
      !screen->is_format_supported(screen, view, s->target, s->nr_samples, s->nr_storage_samples, PIPE_BIND_SAMPLER_VIEW)) {
    receipt("measure-d3d12-resolve", E_NOTIMPL, s->nr_samples, s->width0 | (s->height0 << 16), static_cast<UINT>(view));
    return E_NOTIMPL;
  }
  struct pipe_blit_info blit = {};
  blit.dst.resource = d;
  blit.dst.level = dst_level;
  blit.dst.format = view;
  blit.dst.box.x = blit.dst.box.y = 0;
  blit.dst.box.z = static_cast<int16_t>(dst_layer);
  blit.dst.box.width = static_cast<int>(s->width0);
  blit.dst.box.height = static_cast<int>(s->height0);
  blit.dst.box.depth = 1;
  blit.src.resource = s;
  blit.src.level = 0;
  blit.src.format = view;
  blit.src.box = blit.dst.box;
  blit.src.box.z = static_cast<int16_t>(src_layer);
  blit.mask = PIPE_MASK_RGBA;
  blit.filter = PIPE_TEX_FILTER_NEAREST;
  owner->Context->blit(owner->Context, &blit);
  ++owner->PendingCopies;
  receipt("measure-d3d12-resolve", S_OK, s->nr_samples, s->width0 | (s->height0 << 16), static_cast<UINT>(view));
  return S_OK;
}

/* C6f: the D3D10 frontend's ClearDepthStencilView (OutputMerger.cpp) */
extern "C" HRESULT AgxD3d12OwnerClearDepthStencil(AGX_D3D12_OWNER *owner, const AGX_D3D12_BUFFER *texture,
                                                  DXGI_FORMAT view_format, unsigned level, unsigned first_layer,
                                                  unsigned layers, unsigned flags, float depth, unsigned stencil,
                                                  const RECT *rects, unsigned rect_count) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !texture || !texture->Resource || !layers || !(flags & 3u)) return E_INVALIDARG;
  struct pipe_resource *resource = static_cast<struct pipe_resource *>(texture->Resource);
  const enum pipe_format format = FormatTranslate(view_format, TRUE);
  const struct util_format_description *desc = format != PIPE_FORMAT_NONE ? util_format_description(format) : NULL;
  if (!desc || desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS || level > resource->last_level ||
      first_layer >= resource->array_size || layers > resource->array_size - first_layer)
    return E_INVALIDARG;
  /* stencil only where the format has it; D3D clamps depth to [0, 1] */
  const bool has_stencil = desc->swizzle[1] != PIPE_SWIZZLE_NONE;
  const unsigned buffers = ((flags & 1u) ? PIPE_CLEAR_DEPTH : 0u) | ((flags & 2u) && has_stencil ? PIPE_CLEAR_STENCIL : 0u);
  if (!buffers) return S_OK;
  const double z = !(depth >= 0.0f) ? 0.0 : depth > 1.0f ? 1.0 : depth;
  struct pipe_surface surface = {};
  surface.format = format;
  surface.level = level;
  surface.first_layer = static_cast<uint16_t>(first_layer);
  surface.last_layer = static_cast<uint16_t>(first_layer + layers - 1);
  surface.texture = resource;
  union pipe_color_union none = {};
  return clear_view_layers(owner, surface, true, buffers, &none, z, stencil, rects, rect_count) ? S_OK : E_NOTIMPL;
}

/* Maps the texture box and the buffer range; the maps synchronise with work
 * already recorded on the context, so the copy keeps replay order. */
static bool reserved_texture(struct pipe_resource *texture) {
  AGX_D3D12_RESERVED_TILING tiling = {};
  return texture->target != PIPE_BUFFER &&
         AgxWin32AsahiReservedTextureTiling(texture, &tiling.StandardMips, tiling.TilesX, tiling.TilesY,
                                            &tiling.PackedTiles);
}

/* C53 T2D: a reserved texture has no CPU mapping. Its region goes through a
 * committed texture of the region's size: GPU copy out, CPU copy, or CPU
 * copy, GPU copy in; each submitted and waited for before the staging
 * texture is released. */
static HRESULT copy_reserved_texture(AGX_D3D12_OWNER *owner, const AGX_D3D12_TEXTURE_COPY *copy) {
  struct pipe_context *pipe = owner->Context;
  struct pipe_resource *texture = static_cast<struct pipe_resource *>(copy->Texture->Resource);
  if (copy->Level > texture->last_level || copy->Layer >= texture->array_size ||
      copy->Depth > texture->array_size - copy->Layer)
    return E_INVALIDARG;
  const unsigned level_w = level_extent(texture->width0, copy->Level), level_h = level_extent(texture->height0, copy->Level);
  if (copy->X >= level_w || copy->Y >= level_h) return E_INVALIDARG;
  struct pipe_resource templ = {};
  templ.target = copy->Depth > 1 ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D;
  templ.format = texture->format;
  templ.width0 = copy->Width < level_w - copy->X ? copy->Width : level_w - copy->X;
  templ.height0 = static_cast<uint16_t>(copy->Height < level_h - copy->Y ? copy->Height : level_h - copy->Y);
  templ.depth0 = 1;
  templ.array_size = static_cast<uint16_t>(copy->Depth);
  templ.usage = PIPE_USAGE_DEFAULT;
  templ.bind = PIPE_BIND_SAMPLER_VIEW;
  struct pipe_resource *staging = owner->Screen->resource_create(owner->Screen, &templ);
  if (!staging) return E_OUTOFMEMORY;
  AGX_D3D12_BUFFER staged = {};
  staged.Resource = staging;
  AGX_D3D12_TEXTURE_COPY cpu = *copy;
  cpu.Texture = &staged;
  cpu.Level = cpu.Layer = cpu.X = cpu.Y = 0;
  struct pipe_box box = {};
  box.width = static_cast<int>(templ.width0);
  box.height = static_cast<int>(templ.height0);
  box.depth = static_cast<int16_t>(copy->Depth);
  HRESULT hr = S_OK;
  if (copy->ToBuffer) {
    box.x = static_cast<int>(copy->X);
    box.y = static_cast<int>(copy->Y);
    box.z = static_cast<int16_t>(copy->Layer);
    pipe->resource_copy_region(pipe, staging, 0, 0, 0, 0, texture, copy->Level, &box);
    ++owner->PendingCopies;
    hr = AgxD3d12OwnerSubmitAndWait(owner);
    if (SUCCEEDED(hr)) hr = AgxD3d12OwnerCopyTexture(owner, &cpu);
  } else {
    hr = AgxD3d12OwnerCopyTexture(owner, &cpu);
    if (SUCCEEDED(hr)) {
      pipe->resource_copy_region(pipe, texture, copy->Level, copy->X, copy->Y, copy->Layer, staging, 0, &box);
      ++owner->PendingCopies;
      hr = AgxD3d12OwnerSubmitAndWait(owner);
    }
  }
  AgxWin32AsahiResourceRelease(&staging);
  return hr;
}

extern "C" HRESULT AgxD3d12OwnerCopyTexture(AGX_D3D12_OWNER *owner, const AGX_D3D12_TEXTURE_COPY *copy) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !copy || !copy->Texture || !copy->Texture->Resource || !copy->Buffer ||
      !copy->Buffer->Resource || !copy->Width || !copy->Height || !copy->Depth)
    return E_INVALIDARG;
  struct pipe_context *pipe = owner->Context;
  struct pipe_resource *texture = static_cast<struct pipe_resource *>(copy->Texture->Resource);
  if (reserved_texture(texture)) return copy_reserved_texture(owner, copy);
  struct pipe_resource *buffer = static_cast<struct pipe_resource *>(copy->Buffer->Resource);
  unsigned bw = 0, bh = 0;
  const unsigned block = texture_block(texture->format, &bw, &bh);
  if (!block || texture->nr_samples > 1) return E_INVALIDARG; /* C32: samples are not CPU-addressable */
  /* buffer rows are block rows; a box may cover a level's partial edge
   * block (block-aligned footprints of small mips) */
  const unsigned rows = (copy->Height + bh - 1) / bh;
  const unsigned long long row = static_cast<unsigned long long>((copy->Width + bw - 1) / bw) * block;
  const unsigned level_w = level_extent(texture->width0, copy->Level), level_h = level_extent(texture->height0, copy->Level);
  const unsigned span_w = (level_w + bw - 1) / bw * bw, span_h = (level_h + bh - 1) / bh * bh;
  if(texture->target==PIPE_TEXTURE_3D && (copy->Level>texture->last_level ||
      copy->X>level_extent(texture->width0,copy->Level) || copy->Width>level_extent(texture->width0,copy->Level)-copy->X ||
      copy->Y>level_extent(texture->height0,copy->Level) || copy->Height>level_extent(texture->height0,copy->Level)-copy->Y)) return E_INVALIDARG;
  const unsigned layers=texture_layer_extent(texture,copy->Level);
  if (copy->Level > texture->last_level || copy->X % bw || copy->Y % bh ||
      copy->X > span_w || copy->Width > span_w - copy->X || copy->Y > span_h || copy->Height > span_h - copy->Y ||
      copy->X >= level_w || copy->Y >= level_h || copy->Layer >= layers ||
      copy->Depth > layers - copy->Layer || copy->Pitch < row ||
      (copy->Depth > 1 && copy->SlicePitch < static_cast<unsigned long long>(copy->Pitch) * rows))
    return E_INVALIDARG;
  const unsigned long long span = static_cast<unsigned long long>(copy->Depth - 1) * copy->SlicePitch +
                                  static_cast<unsigned long long>(rows - 1) * copy->Pitch + row;
  if (copy->Offset > copy->Buffer->Bytes || span > copy->Buffer->Bytes - copy->Offset || copy->Buffer->Bytes > 0xffffffffull)
    return E_INVALIDARG;
  struct pipe_box tbox = {};
  tbox.x = static_cast<int>(copy->X);
  tbox.y = static_cast<int>(copy->Y);
  tbox.z = static_cast<int16_t>(copy->Layer);
  tbox.width = static_cast<int>(copy->Width < level_w - copy->X ? copy->Width : level_w - copy->X);
  tbox.height = static_cast<int>(copy->Height < level_h - copy->Y ? copy->Height : level_h - copy->Y);
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
    for (unsigned y = 0; y < rows; ++y) {
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

/* C6b: graphics pipelines and draws, translated as the D3D10 frontend does
 * (OutputMerger.cpp blend/depth-stencil, Rasterizer.cpp rasterizer and
 * viewports, InputAssembly.cpp elements by input register, Draw.cpp
 * per-draw element strides). */
static unsigned blend_factor(unsigned blend) {
  switch (blend) {
  case D3D10_DDI_BLEND_ZERO: return PIPE_BLENDFACTOR_ZERO;
  case D3D10_DDI_BLEND_ONE: return PIPE_BLENDFACTOR_ONE;
  case D3D10_DDI_BLEND_SRC_COLOR: return PIPE_BLENDFACTOR_SRC_COLOR;
  case D3D10_DDI_BLEND_INV_SRC_COLOR: return PIPE_BLENDFACTOR_INV_SRC_COLOR;
  case D3D10_DDI_BLEND_SRC_ALPHA: return PIPE_BLENDFACTOR_SRC_ALPHA;
  case D3D10_DDI_BLEND_INV_SRC_ALPHA: return PIPE_BLENDFACTOR_INV_SRC_ALPHA;
  case D3D10_DDI_BLEND_DEST_ALPHA: return PIPE_BLENDFACTOR_DST_ALPHA;
  case D3D10_DDI_BLEND_INV_DEST_ALPHA: return PIPE_BLENDFACTOR_INV_DST_ALPHA;
  case D3D10_DDI_BLEND_DEST_COLOR: return PIPE_BLENDFACTOR_DST_COLOR;
  case D3D10_DDI_BLEND_INV_DEST_COLOR: return PIPE_BLENDFACTOR_INV_DST_COLOR;
  case D3D10_DDI_BLEND_SRC_ALPHASAT: return PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE;
  case D3D10_DDI_BLEND_BLEND_FACTOR: return PIPE_BLENDFACTOR_CONST_COLOR;
  case D3D10_DDI_BLEND_INVBLEND_FACTOR: return PIPE_BLENDFACTOR_INV_CONST_COLOR;
  case D3D10_DDI_BLEND_SRC1_COLOR: return PIPE_BLENDFACTOR_SRC1_COLOR;
  case D3D10_DDI_BLEND_INV_SRC1_COLOR: return PIPE_BLENDFACTOR_INV_SRC1_COLOR;
  case D3D10_DDI_BLEND_SRC1_ALPHA: return PIPE_BLENDFACTOR_SRC1_ALPHA;
  case D3D10_DDI_BLEND_INV_SRC1_ALPHA: return PIPE_BLENDFACTOR_INV_SRC1_ALPHA;
  default: return PIPE_BLENDFACTOR_ONE;
  }
}
static unsigned blend_op(unsigned op) {
  switch (op) {
  case D3D10_DDI_BLEND_OP_SUBTRACT: return PIPE_BLEND_SUBTRACT;
  case D3D10_DDI_BLEND_OP_REV_SUBTRACT: return PIPE_BLEND_REVERSE_SUBTRACT;
  case D3D10_DDI_BLEND_OP_MIN: return PIPE_BLEND_MIN;
  case D3D10_DDI_BLEND_OP_MAX: return PIPE_BLEND_MAX;
  default: return PIPE_BLEND_ADD;
  }
}
static unsigned comparison(unsigned func) {
  switch (func) {
  case D3D10_DDI_COMPARISON_NEVER: return PIPE_FUNC_NEVER;
  case D3D10_DDI_COMPARISON_LESS: return PIPE_FUNC_LESS;
  case D3D10_DDI_COMPARISON_EQUAL: return PIPE_FUNC_EQUAL;
  case D3D10_DDI_COMPARISON_LESS_EQUAL: return PIPE_FUNC_LEQUAL;
  case D3D10_DDI_COMPARISON_GREATER: return PIPE_FUNC_GREATER;
  case D3D10_DDI_COMPARISON_NOT_EQUAL: return PIPE_FUNC_NOTEQUAL;
  case D3D10_DDI_COMPARISON_GREATER_EQUAL: return PIPE_FUNC_GEQUAL;
  default: return PIPE_FUNC_ALWAYS;
  }
}
static unsigned stencil_op(unsigned op) {
  switch (op) {
  case D3D10_DDI_STENCIL_OP_ZERO: return PIPE_STENCIL_OP_ZERO;
  case D3D10_DDI_STENCIL_OP_REPLACE: return PIPE_STENCIL_OP_REPLACE;
  case D3D10_DDI_STENCIL_OP_INCR_SAT: return PIPE_STENCIL_OP_INCR;
  case D3D10_DDI_STENCIL_OP_DECR_SAT: return PIPE_STENCIL_OP_DECR;
  case D3D10_DDI_STENCIL_OP_INVERT: return PIPE_STENCIL_OP_INVERT;
  case D3D10_DDI_STENCIL_OP_INCR: return PIPE_STENCIL_OP_INCR_WRAP;
  case D3D10_DDI_STENCIL_OP_DECR: return PIPE_STENCIL_OP_DECR_WRAP;
  default: return PIPE_STENCIL_OP_KEEP;
  }
}
/* EXP1878G: a real application's PSO failed with no receipt naming the
 * cause. APPLE_AGX_D3D12_SHADER_DUMP_DIR set: a shader whose translation or
 * slot gathering fails is written there as <fnv64>-s<stage>-r<reason>.bin
 * (the DXBC/DXIL container as the runtime passed it) for offline replay. */
static void dump_failed_shader(const UINT *tokens, mesa_shader_stage stage, UINT reason) {
  char dir[MAX_PATH];
  const DWORD length = GetEnvironmentVariableA("APPLE_AGX_D3D12_SHADER_DUMP_DIR", dir, sizeof(dir));
  if (!tokens || length == 0 || length >= sizeof(dir) - 48 || tokens[1] < 2 || tokens[1] >= 0x4000000u) return;
  const size_t bytes = static_cast<size_t>(tokens[1]) * 4u;
  unsigned long long hash = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < bytes; ++i) hash = (hash ^ reinterpret_cast<const unsigned char *>(tokens)[i]) * 0x100000001b3ull;
  char path[MAX_PATH];
  wsprintfA(path, "%s\\%08x%08x-s%u-r%u.bin", dir, static_cast<UINT>(hash >> 32), static_cast<UINT>(hash),
            static_cast<UINT>(stage), reason);
  HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  (void)WriteFile(file, tokens, static_cast<DWORD>(bytes), &written, NULL);
  CloseHandle(file);
}
static void *create_shader(struct pipe_context *pipe, const UINT *tokens, mesa_shader_stage stage,
                           struct dxbc_binding_map *bindings, uint8_t dimensions[DXBC_RESOURCE_SLOTS],
                           uint8_t uav_dimensions[DXBC_UAV_DIMENSION_SLOTS], uint32_t passthrough_registers = 0,
                           uint32_t passthrough_positions = 0, const struct dxbc_nir_xfb *xfb = NULL,
                           unsigned tir_samples = 0, struct nir_shader **tir_clone = NULL,
                           const struct nir_shader *tir_wrap_ps = NULL) {
  struct dxbc_nir_error error = {};
  /* C37: a hull shader also takes its output signature's pass-through
   * registers (the D3D11 frontend's create_tessellation_shader); C39: the
   * last vertex stage of a stream-output pipeline captures its outputs */
  struct nir_shader *nir = translate_shader(tokens, stage, pipe->screen->nir_options[stage], bindings, passthrough_registers,
                                            passthrough_positions, xfb, &error);
  receipt("measure-d3d12-graphics-shader", nir ? S_OK : E_NOTIMPL, static_cast<UINT>(stage), error.opcode, error.offset);
  if (!nir) {
    dump_failed_shader(tokens, stage, 1u);
    return NULL;
  }
  /* EXP1878G: name the slot-gathering refusal (2 SRV, 3 UAV dimensions) */
  const bool srv_ok = dxbc_nir_srv_dimensions(nir, dimensions);
  const bool uav_ok = srv_ok && dxbc_nir_uav_dimensions(nir, uav_dimensions);
  if (!srv_ok || !uav_ok) {
    const UINT reason = srv_ok ? 3u : 2u;
    receipt("measure-d3d12-graphics-shader-slots", E_NOTIMPL, static_cast<UINT>(stage), reason, 0u);
    dump_failed_shader(tokens, stage, reason);
    ralloc_free(nir);
    memset(bindings, 0, sizeof(*bindings));
    return NULL;
  }
  /* FL 11_1: the forced-sample-count variant (dxbc_tir_lower_fs); 1 is
   * conservative rasterization (dxbc_cr_lower_fs) */
  if (stage == MESA_SHADER_FRAGMENT && tir_samples &&
      !(tir_samples == 1u ? dxbc_cr_lower_fs(nir) : dxbc_tir_lower_fs(nir, tir_samples))) {
    receipt("measure-d3d12-graphics-shader-slots", E_NOTIMPL, static_cast<UINT>(stage), 5u, tir_samples);
    ralloc_free(nir);
    memset(bindings, 0, sizeof(*bindings));
    return NULL;
  }
  /* Asahi consumes integer pixel_coord. This fresh D3D fragment NIR must
   * acquire the half-pixel center exactly once before native preprocessing. */
  if (stage == MESA_SHADER_FRAGMENT)
    dxbc_nir_lower_fragment_position(nir);
  /* FL 11_1: the TIR expansion GS keys on the vertex and pixel shaders' I/O */
  if (tir_clone)
    *tir_clone = dxbc_tir_clone(nir);
  /* C61C: an application GS before a forced-sample-count or conservative
   * pixel shader variant emits the expanded triangles itself; one that
   * reads descriptor heap ranges takes the viewport from its heap table
   * (constant buffer 15 is then its heap mirror; bind_heaps appends it) */
  if (stage == MESA_SHADER_GEOMETRY && tir_wrap_ps) {
    bool heap = false;
    for (uint32_t i = 0; i < bindings->count && i < DXBC_BINDING_MAP_MAX; ++i)
      heap = heap || (bindings->bindings[i].flags & DXBC_BINDING_HEAP) != 0;
    struct dxbc_tir_gs_key key;
    const bool keyed = dxbc_tir_gs_key_from(nir, tir_wrap_ps, &key);
    key.full_pixel = tir_samples == 1u; /* conservative rasterization */
    if (!keyed || !dxbc_tir_wrap_gs(nir, &key, heap)) {
      receipt("reject-d3d12-tir", E_NOTIMPL, 0u, heap ? 48u : 32u, 0u);
      ralloc_free(nir);
      memset(bindings, 0, sizeof(*bindings));
      return NULL;
    }
  }
  /* receipt-only (EXP1858 C37D/C37E: tessellated winding inverted against
   * D3D for both output topologies): the tessellation info Asahi reads */
  if (stage == MESA_SHADER_TESS_CTRL || stage == MESA_SHADER_TESS_EVAL)
    receipt("measure-d3d12-tess-info", S_OK, static_cast<UINT>(stage), dxbc_nir_tess_info(nir), 0u);
  struct pipe_shader_state state = {};
  state.type = PIPE_SHADER_IR_NIR;
  state.ir.nir = nir;
  if (stage == MESA_SHADER_GEOMETRY) return pipe->create_gs_state(pipe, &state); /* C27 */
  if (stage == MESA_SHADER_TESS_CTRL) return pipe->create_tcs_state(pipe, &state); /* C37 */
  if (stage == MESA_SHADER_TESS_EVAL) return pipe->create_tes_state(pipe, &state);
  void *cso = stage == MESA_SHADER_VERTEX ? pipe->create_vs_state(pipe, &state) : pipe->create_fs_state(pipe, &state);
  if (!cso) {
    receipt("measure-d3d12-graphics-shader-slots", E_NOTIMPL, static_cast<UINT>(stage), 4u, 0u);
    dump_failed_shader(tokens, stage, 4u);
  }
  return cso;
}

/* C39: the D3D10 frontend's CreateGeometryShaderWithStreamOutput rules
 * (Mesa d3d10umd Shader.cpp): each entry's mask names a component run, a
 * hole (register 0xffffffff) only advances its slot, offsets accumulate
 * per slot; the DDI's byte strides win where given. Stream 0 only, and the
 * rasterized stream is 0 or none (the translator captures stream 0). */
static bool so_xfb(const AGX_D3D12_SO_DECL *decl, struct dxbc_nir_xfb *xfb) {
  memset(xfb, 0, sizeof(*xfb));
  xfb->position_registers = decl->PositionRegisters;
  unsigned total[4] = {};
  if (decl->Entries > AGX_D3D12_SO_ENTRIES || decl->Entries > DXBC_NIR_MAX_XFB_OUTPUTS || decl->NumStrides > 4 ||
      (decl->Rasterized != 0 && decl->Rasterized != 0xffffffffu))
    return false;
  for (unsigned i = 0; i < decl->Entries; ++i) {
    const auto &e = decl->Entry[i];
    const unsigned mask = e.Mask & 0xfu;
    unsigned start = 0, count = 0;
    while (mask && !(mask & (1u << start))) ++start;
    while (start + count < 4 && (mask & (1u << (start + count)))) ++count;
    if (e.Stream || e.Slot >= 4 || (mask && ((1u << count) - 1u) << start != mask)) return false;
    if (e.Register != 0xffffffffu) {
      if (!count) return false;
      struct dxbc_nir_xfb_output *o = &xfb->outputs[xfb->num_outputs++];
      o->reg = e.Register;
      o->start_component = static_cast<uint8_t>(start);
      o->num_components = static_cast<uint8_t>(count);
      o->buffer = static_cast<uint8_t>(e.Slot);
      o->dst_offset = total[e.Slot];
    }
    total[e.Slot] += count;
  }
  for (unsigned b = 0; b < 4; ++b) {
    if (b < decl->NumStrides && decl->Strides[b] % 4u) return false;
    xfb->stride[b] = b < decl->NumStrides && decl->Strides[b] ? decl->Strides[b] / 4u : total[b];
    if (xfb->stride[b] < total[b]) return false;
  }
  return xfb->num_outputs != 0;
}

extern "C" void AgxD3d12OwnerDestroyGraphics(AGX_D3D12_OWNER *owner, AGX_D3D12_GRAPHICS *graphics) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !graphics) return;
  struct pipe_context *pipe = owner->Context;
  if (graphics->Vs) pipe->delete_vs_state(pipe, graphics->Vs);
  if (graphics->Fs) pipe->delete_fs_state(pipe, graphics->Fs);
  if (graphics->Gs) pipe->delete_gs_state(pipe, graphics->Gs);
  if (graphics->Hs) pipe->delete_tcs_state(pipe, graphics->Hs);
  if (graphics->Ds) pipe->delete_tes_state(pipe, graphics->Ds);
  if (graphics->Blend) pipe->delete_blend_state(pipe, graphics->Blend);
  if (graphics->DepthStencil) pipe->delete_depth_stencil_alpha_state(pipe, graphics->DepthStencil);
  if (graphics->Rasterizer) pipe->delete_rasterizer_state(pipe, graphics->Rasterizer);
  if (graphics->Velems) pipe->delete_vertex_elements_state(pipe, graphics->Velems);
  memset(graphics, 0, sizeof(*graphics));
}

extern "C" HRESULT AgxD3d12OwnerCreateGraphics(AGX_D3D12_OWNER *owner, const AGX_D3D12_GRAPHICS_DESC *desc,
                                               AGX_D3D12_GRAPHICS *graphics) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !desc || !desc->Vs || !graphics || desc->NumElements > AGX_D3D12_MAX_ELEMENTS)
    return E_INVALIDARG;
  memset(graphics, 0, sizeof(*graphics));
  struct pipe_context *pipe = owner->Context;
  /* C39: stream output from the geometry shader, else the vertex shader
   * (a pipeline with a hull/domain shader refuses it at draw recording) */
  struct dxbc_nir_xfb xfb;
  const bool so = desc->So && so_xfb(desc->So, &xfb);
  if (desc->So && !so) return E_NOTIMPL;
  const struct dxbc_nir_xfb *vs_xfb = so && !desc->Gs ? &xfb : NULL, *gs_xfb = so && desc->Gs ? &xfb : NULL;
  graphics->Xfb = so;
  /* FL 11_1 forced sample count (D3D11-FL111-TIR.md): a pixel shader
   * variant behind the expansion GS, fed by the vertex shader or (C61B) the
   * domain shader, or (C61C) the application's GS wrapped to expand; not
   * with stream output (plain 1x rasterization with a receipt) */
  const bool expandable = desc->Ps && !desc->Hs == !desc->Ds && !so;
  const bool forced = desc->ForcedSampleCount >= 4u && expandable;
  /* 12_1 conservative rasterization Tier 1: the same GS, a touched-pixel
   * variant; not together with a forced sample count */
  const bool conservative = desc->ConservativeRaster && expandable && !forced;
  const bool tir = forced || conservative;
  if ((desc->ForcedSampleCount > 1u && !forced) || (desc->ConservativeRaster && !conservative))
    receipt("reject-d3d12-tir", E_NOTIMPL, desc->ForcedSampleCount,
            (desc->Gs ? 1u : 0u) | (desc->Hs ? 2u : 0u) | (so ? 4u : 0u) | (desc->ConservativeRaster ? 8u : 0u), 0u);
  struct nir_shader *vs_io = NULL, *ds_io = NULL, *fs_io = NULL;
  graphics->Vs = create_shader(pipe, desc->Vs, MESA_SHADER_VERTEX, &graphics->VsBindings, graphics->VsSrvDimensions,
                               graphics->VsUavDimensions, 0, 0, vs_xfb, 0, tir && !desc->Ds && !desc->Gs ? &vs_io : NULL);
  graphics->Hs = desc->Hs ? create_shader(pipe, desc->Hs, MESA_SHADER_TESS_CTRL, &graphics->HsBindings, graphics->HsSrvDimensions,
                                          graphics->HsUavDimensions, desc->HsPassthroughRegisters, desc->HsPassthroughPositions) : NULL;
  graphics->Ds = desc->Ds ? create_shader(pipe, desc->Ds, MESA_SHADER_TESS_EVAL, &graphics->DsBindings, graphics->DsSrvDimensions,
                                          graphics->DsUavDimensions, 0, 0, NULL, 0, tir && !desc->Gs ? &ds_io : NULL) : NULL;
  graphics->Fs = desc->Ps ? create_shader(pipe, desc->Ps, MESA_SHADER_FRAGMENT, &graphics->PsBindings, graphics->PsSrvDimensions,
                                          graphics->PsUavDimensions, 0, 0, NULL,
                                          forced ? desc->ForcedSampleCount : conservative ? 1u : 0u,
                                          tir ? &fs_io : NULL) : NULL;
  if (tir) {
    struct dxbc_tir_gs_key key;
    /* C61B: a tessellated pipeline feeds the GS from the domain shader; its
     * domain must produce triangles (isolines and point mode do not) */
    struct nir_shader *last = desc->Ds ? ds_io : vs_io;
    const bool keyed = !desc->Gs && last && fs_io && dxbc_tir_gs_key_from(last, fs_io, &key);
    key.full_pixel = conservative;
    struct nir_shader *gs = keyed ? dxbc_tir_gs(pipe->screen->nir_options[MESA_SHADER_GEOMETRY], &key) : NULL;
    if (desc->Gs && fs_io)
      graphics->Gs = create_shader(pipe, desc->Gs, MESA_SHADER_GEOMETRY, &graphics->GsBindings, graphics->GsSrvDimensions,
                                   graphics->GsUavDimensions, 0, 0, NULL, conservative ? 1u : 0u, NULL, fs_io);
    ralloc_free(vs_io);
    ralloc_free(ds_io);
    ralloc_free(fs_io);
    if (gs) {
      dxbc_nir_finalize(gs);
      struct pipe_shader_state state = {};
      state.type = PIPE_SHADER_IR_NIR;
      state.ir.nir = gs;
      graphics->Gs = pipe->create_gs_state(pipe, &state);
    }
    graphics->TirSamples = graphics->Gs && forced ? desc->ForcedSampleCount : 0u;
    graphics->Conservative = graphics->Gs && conservative;
    receipt("measure-d3d12-tir", graphics->Gs ? S_OK : E_NOTIMPL, desc->ForcedSampleCount, graphics->Fs ? 1u : 0u,
            (graphics->Gs ? 1u : 0u) | (conservative ? 2u : 0u) | (desc->Ds ? 4u : 0u) | (desc->Gs ? 8u : 0u));
  }
  if (!tir)
    graphics->Gs = desc->Gs ? create_shader(pipe, desc->Gs, MESA_SHADER_GEOMETRY, &graphics->GsBindings, graphics->GsSrvDimensions,
                                            graphics->GsUavDimensions, 0, 0, gs_xfb) : NULL;

  struct pipe_blend_state blend = {};
  blend.independent_blend_enable = desc->IndependentBlend ? 1 : 0;
  blend.alpha_to_coverage = desc->AlphaToCoverage ? 1 : 0;
  for (unsigned i = 0; i < 8; ++i) {
    const unsigned s = desc->IndependentBlend ? i : 0;
    blend.rt[i].blend_enable = desc->Rt[s].BlendEnable ? 1 : 0;
    blend.rt[i].colormask = desc->Rt[s].WriteMask & 0xf;
    blend.rt[i].rgb_func = blend_op(desc->Rt[s].Op);
    const bool rgb_minmax = desc->Rt[s].Op == D3D10_DDI_BLEND_OP_MIN || desc->Rt[s].Op == D3D10_DDI_BLEND_OP_MAX;
    blend.rt[i].rgb_src_factor = rgb_minmax ? PIPE_BLENDFACTOR_ONE : blend_factor(desc->Rt[s].Src);
    blend.rt[i].rgb_dst_factor = rgb_minmax ? PIPE_BLENDFACTOR_ONE : blend_factor(desc->Rt[s].Dst);
    blend.rt[i].alpha_func = blend_op(desc->Rt[s].OpAlpha);
    const bool a_minmax = desc->Rt[s].OpAlpha == D3D10_DDI_BLEND_OP_MIN || desc->Rt[s].OpAlpha == D3D10_DDI_BLEND_OP_MAX;
    blend.rt[i].alpha_src_factor = a_minmax ? PIPE_BLENDFACTOR_ONE : blend_factor(desc->Rt[s].SrcAlpha);
    blend.rt[i].alpha_dst_factor = a_minmax ? PIPE_BLENDFACTOR_ONE : blend_factor(desc->Rt[s].DstAlpha);
  }
  /* C57: D3D12DDI_LOGIC_OP order -> PIPE_LOGICOP (same truth tables) */
  static const unsigned char logic_ops[16] = {
      PIPE_LOGICOP_CLEAR, PIPE_LOGICOP_SET, PIPE_LOGICOP_COPY, PIPE_LOGICOP_COPY_INVERTED,
      PIPE_LOGICOP_NOOP, PIPE_LOGICOP_INVERT, PIPE_LOGICOP_AND, PIPE_LOGICOP_NAND,
      PIPE_LOGICOP_OR, PIPE_LOGICOP_NOR, PIPE_LOGICOP_XOR, PIPE_LOGICOP_EQUIV,
      PIPE_LOGICOP_AND_REVERSE, PIPE_LOGICOP_AND_INVERTED, PIPE_LOGICOP_OR_REVERSE, PIPE_LOGICOP_OR_INVERTED};
  if (desc->LogicOpEnable && desc->LogicOp < 16) {
    blend.logicop_enable = 1;
    blend.logicop_func = logic_ops[desc->LogicOp];
  }
  graphics->Blend = pipe->create_blend_state(pipe, &blend);
  struct pipe_depth_stencil_alpha_state dsa = {};
  dsa.depth_enabled = desc->DepthEnable ? 1 : 0;
  dsa.depth_writemask = desc->DepthEnable && desc->DepthWrite ? 1 : 0;
  dsa.depth_func = comparison(desc->DepthFunc);
  if (desc->StencilEnable) {
    for (unsigned f = 0; f < 2; ++f) {
      const auto &face = f ? desc->Back : desc->Front;
      dsa.stencil[f].enabled = 1;
      dsa.stencil[f].func = comparison(face.Func);
      dsa.stencil[f].fail_op = stencil_op(face.Fail);
      dsa.stencil[f].zpass_op = stencil_op(face.Pass);
      dsa.stencil[f].zfail_op = stencil_op(face.DepthFail);
      dsa.stencil[f].valuemask = desc->StencilReadMask & 0xff;
      dsa.stencil[f].writemask = desc->StencilWriteMask & 0xff;
    }
  }
  graphics->DepthStencil = pipe->create_depth_stencil_alpha_state(pipe, &dsa);
  struct pipe_rasterizer_state rast = {};
  rast.flatshade_first = 1;
  rast.front_ccw = desc->FrontCounterClockwise ? 1 : 0;
  rast.cull_face = desc->CullMode == D3D10_DDI_CULL_FRONT ? PIPE_FACE_FRONT
                 : desc->CullMode == D3D10_DDI_CULL_BACK ? PIPE_FACE_BACK : PIPE_FACE_NONE;
  rast.fill_front = rast.fill_back = desc->FillMode == D3D10_DDI_FILL_WIREFRAME ? PIPE_POLYGON_MODE_LINE : PIPE_POLYGON_MODE_FILL;
  rast.scissor = 1; /* D3D12 always scissors */
  rast.offset_units = static_cast<float>(desc->DepthBias);
  rast.offset_scale = desc->SlopeScaledDepthBias;
  rast.offset_clamp = desc->DepthBiasClamp;
  rast.offset_tri = desc->DepthBias || desc->SlopeScaledDepthBias ? 1 : 0;
  rast.half_pixel_center = 1;
  rast.clip_halfz = 1;
  rast.depth_clip_near = rast.depth_clip_far = desc->DepthClipEnable ? 1 : 0;
  rast.depth_clamp = 1;
  rast.point_quad_rasterization = 1;
  rast.point_size = 1.0f;
  rast.point_line_tri_clip = 1;
  rast.line_width = 1.0f;
  /* C32: multisampled rasterization follows the PSO sample count (D3D12
   * MultisampleEnable selects only the line algorithm) */
  rast.multisample = desc->SampleCount > 1 ? 1 : 0;
  rast.rasterizer_discard = so && desc->So->Rasterized == 0xffffffffu ? 1 : 0; /* C39: D3D12_SO_NO_RASTERIZED_STREAM */
  graphics->SampleMask = desc->SampleMask;
  graphics->Rasterizer = pipe->create_rasterizer_state(pipe, &rast);
  HRESULT hr = graphics->Vs && (graphics->Fs || !desc->Ps) && (graphics->Gs || !desc->Gs) && (graphics->Hs || !desc->Hs) &&
                       (!tir || graphics->TirSamples || graphics->Conservative) &&
                       (graphics->Ds || !desc->Ds) && !desc->Hs == !desc->Ds && graphics->Blend && graphics->DepthStencil &&
                       graphics->Rasterizer ? S_OK : E_NOTIMPL;
  graphics->NumElements = desc->NumElements;
  for (unsigned i = 0; SUCCEEDED(hr) && i < desc->NumElements; ++i) {
    const enum pipe_format format = FormatTranslate(desc->Element[i].Format, FALSE);
    if (format == PIPE_FORMAT_NONE || desc->Element[i].Slot >= AGX_D3D12_VB_SLOTS ||
        desc->Element[i].Register >= AGX_D3D12_MAX_ELEMENTS)
      hr = E_NOTIMPL;
    graphics->Element[i].Slot = desc->Element[i].Slot;
    graphics->Element[i].Offset = desc->Element[i].Offset;
    graphics->Element[i].Register = desc->Element[i].Register;
    graphics->Element[i].Divisor = desc->Element[i].PerInstance ? (desc->Element[i].StepRate ? desc->Element[i].StepRate : ~0u) : 0u;
    graphics->Element[i].Format = static_cast<unsigned>(format);
  }
  receipt("measure-d3d12-graphics", hr, graphics->Fs != NULL ? 1u : 0u, desc->NumElements,
          (graphics->Blend != NULL) | ((graphics->DepthStencil != NULL) << 1) | ((graphics->Rasterizer != NULL) << 2));
  if (FAILED(hr)) AgxD3d12OwnerDestroyGraphics(owner, graphics);
  return hr;
}

/* C6c: one graphics stage's constant buffers and buffer SRVs (the compute
 * path's conversions; views are released after the draw). */
/* C6e: D3D sampler state as the D3D10 frontend's CreateSamplerState
 * translates it (filter bits min [5:4], mag [3:2], mip [1:0], anisotropy
 * bit 6, comparison reduction bits [8:7]). */
static unsigned wrap_mode(unsigned address) {
  switch (address) {
  case 2: return PIPE_TEX_WRAP_MIRROR_REPEAT;
  case 3: return PIPE_TEX_WRAP_CLAMP_TO_EDGE;
  case 4: return PIPE_TEX_WRAP_CLAMP_TO_BORDER;
  case 5: return PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE;
  default: return PIPE_TEX_WRAP_REPEAT;
  }
}
static void *create_sampler(struct pipe_context *pipe, const AGX_D3D12_SAMPLER *s) {
  struct pipe_sampler_state state = {};
  const unsigned filter = s->Filter;
  const bool aniso = (filter & 0x40u) != 0;
  state.wrap_s = wrap_mode(s->AddressU);
  state.wrap_t = wrap_mode(s->AddressV);
  state.wrap_r = wrap_mode(s->AddressW);
  state.min_img_filter = aniso || ((filter >> 4) & 3u) ? PIPE_TEX_FILTER_LINEAR : PIPE_TEX_FILTER_NEAREST;
  state.mag_img_filter = aniso || ((filter >> 2) & 3u) ? PIPE_TEX_FILTER_LINEAR : PIPE_TEX_FILTER_NEAREST;
  state.min_mip_filter = aniso || (filter & 3u) ? PIPE_TEX_MIPFILTER_LINEAR : PIPE_TEX_MIPFILTER_NEAREST;
  state.max_anisotropy = aniso ? (s->MaxAnisotropy > 16 ? 16 : s->MaxAnisotropy) : 0;
  if (((filter >> 7) & 3u) == 1u) {
    state.compare_mode = PIPE_TEX_COMPARE_R_TO_TEXTURE;
    state.compare_func = comparison(s->ComparisonFunc);
  }
  state.lod_bias = s->MipLodBias;
  state.min_lod = s->MinLod;
  state.max_lod = s->MaxLod;
  for (unsigned i = 0; i < 4; ++i) state.border_color.f[i] = s->BorderColor[i];
  return pipe->create_sampler_state(pipe, &state);
}

/* C23 only tightens the newly admitted 1D boundary. Unused slots and all
 * pre-existing non-1D combinations retain their previous policy. */
static bool uav_dimension_matches(uint8_t dimension, const AGX_D3D12_UAV *uav) {
  if (dimension == DXBC_UAV_UNUSED) return true;
  const struct pipe_resource *resource = static_cast<const struct pipe_resource *>(uav->Buffer->Resource);
  const bool line = resource->target == PIPE_TEXTURE_1D || resource->target == PIPE_TEXTURE_1D_ARRAY;
  if (dimension == DXBC_UAV_1D) return line && uav->Texture && uav->Format && uav->Layers==1;
  if (dimension == DXBC_UAV_1D_ARRAY) return line && uav->Texture && uav->Format;
  return !line;
}

/* C9: a typed UAV as the D3D11 frontend's uav_image builds it (a buffer
 * range or a texture level and layer range of the view format) */
static bool uav_image(const AGX_D3D12_UAV *uav, struct pipe_image_view *image) {
  struct pipe_resource *resource = static_cast<struct pipe_resource *>(uav->Buffer->Resource);
  const enum pipe_format format = FormatTranslate(uav->Format, FALSE);
  memset(image, 0, sizeof(*image));
  if (!resource || format == PIPE_FORMAT_NONE) return false;
  image->resource = resource;
  image->format = format;
  image->access = image->shader_access = PIPE_IMAGE_ACCESS_READ_WRITE;
  if (!uav->Texture) {
    const unsigned long long room = uav->Offset < uav->Buffer->Bytes ? uav->Buffer->Bytes - uav->Offset : 0;
    const unsigned long long bytes = uav->Bytes && uav->Bytes < room ? uav->Bytes : room;
    if (resource->target != PIPE_BUFFER || !bytes || uav->Buffer->Bytes > 0xffffffffull) return false;
    image->u.buf.offset = static_cast<unsigned>(uav->Offset);
    image->u.buf.size = static_cast<unsigned>(bytes);
    return true;
  }
  if(resource->target==PIPE_TEXTURE_3D) {
    if(resource->nr_samples>1 || uav->Level>resource->last_level || uav->FirstLayer ||
        uav->Layers!=texture_layer_extent(resource,uav->Level)) return false;
    image->u.tex.level=uav->Level;image->u.tex.first_layer=0;image->u.tex.last_layer=uav->Layers-1;
    return true;
  }
  if (resource->target == PIPE_BUFFER || resource->nr_samples > 1 || uav->Level > resource->last_level ||
      !uav->Layers || uav->FirstLayer >= resource->array_size || uav->Layers > resource->array_size - uav->FirstLayer)
    return false;
  image->u.tex.level = uav->Level;
  image->u.tex.first_layer = uav->FirstLayer;
  image->u.tex.last_layer = uav->FirstLayer + uav->Layers - 1;
  return true;
}

/* C47: the descriptor-heap form of a UAV: Asahi's texture and PBE
 * descriptors of its image (typed views by their format, raw/structured
 * buffers as R32_UINT buffer images) and the resource's layout key; an
 * unusable UAV gets null descriptors and E_INVALIDARG. */
extern "C" HRESULT AgxD3d12OwnerUavDescriptor(AGX_D3D12_OWNER *owner, const AGX_D3D12_UAV *uav, void *texture,
                                              void *pbe, void *counter_pbe, unsigned long long *key) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !texture || !pbe || !counter_pbe || !key ||
      AgxWin32AsahiTextureDescriptorBytes() != AGX_D3D12_HEAP_TEXTURE_BYTES ||
      AgxWin32AsahiPbeDescriptorBytes() != AGX_D3D12_HEAP_TEXTURE_BYTES)
    return E_INVALIDARG;
  *key = 0;
  AgxWin32AsahiNullTextureDescriptor(texture);
  AgxWin32AsahiNullPbeDescriptor(pbe);
  AgxWin32AsahiNullPbeDescriptor(counter_pbe);
  if (!uav || !uav->Buffer || !uav->Buffer->Resource) return S_OK;
  if (const AGX_D3D12_BUFFER *counter = uav->Counter) {
    /* the counter word as an R32_UINT buffer image (imm_atomic_alloc/consume) */
    AGX_D3D12_UAV word = {};
    word.Buffer = counter;
    word.Offset = uav->CounterOffset;
    word.Bytes = 4;
    word.Format = DXGI_FORMAT_R32_UINT;
    struct pipe_image_view image;
    unsigned char unused[AGX_D3D12_HEAP_TEXTURE_BYTES];
    APPLE_AGX_U64 counter_key = 0;
    if (!counter->Resource || !uav_image(&word, &image) ||
        !AgxWin32AsahiImageDescriptors(owner->Context, &image, unused, counter_pbe, &counter_key))
      return E_INVALIDARG;
  }
  AGX_D3D12_UAV view = *uav;
  if (!view.Format && !view.Texture) view.Format = DXGI_FORMAT_R32_UINT; /* raw/structured */
  struct pipe_image_view image;
  if (!uav_image(&view, &image)) return E_INVALIDARG;
  APPLE_AGX_U64 packed_key = 0;
  if (!AgxWin32AsahiImageDescriptors(owner->Context, &image, texture, pbe, &packed_key)) {
    AgxWin32AsahiNullTextureDescriptor(texture);
    AgxWin32AsahiNullPbeDescriptor(pbe);
    return E_OUTOFMEMORY;
  }
  *key = packed_key;
  return S_OK;
}

extern "C" HRESULT AgxD3d12OwnerSamplerDescriptor(AGX_D3D12_OWNER *owner, const AGX_D3D12_SAMPLER *sampler,
                                                  AGX_D3D12_HEAP_SAMPLER *out) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !sampler || !out || AgxWin32AsahiSamplerDescriptorBytes() != sizeof(out->Packed))
    return E_INVALIDARG;
  memset(out, 0, sizeof(*out));
  void *state = sampler->Valid ? create_sampler(owner->Context, sampler) : NULL;
  if (!state) return E_INVALIDARG;
  APPLE_AGX_U16 bias = 0;
  int custom = 0;
  const int packed = AgxWin32AsahiSamplerDescriptor(state, out->Packed, &bias, &custom);
  owner->Context->delete_sampler_state(owner->Context, state);
  if (!packed) return E_FAIL;
  out->Bias = bias;
  out->Valid = 1;
  return custom ? S_FALSE : S_OK;
}

/* C18: Asahi composes this view swizzle with the format's own swizzle.
 * PIPE_SWIZZLE_1 retains the image format's integer/float ONE semantics. */
static bool srv_component_mapping(unsigned mapping, struct pipe_sampler_view *view) {
  if (!AgxD3d12ComponentMappingValid(mapping)) return false;
  static const unsigned swizzle[6] = {
    PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z,
    PIPE_SWIZZLE_W, PIPE_SWIZZLE_0, PIPE_SWIZZLE_1
  };
  view->swizzle_r = swizzle[mapping & 7u];
  view->swizzle_g = swizzle[(mapping >> 3) & 7u];
  view->swizzle_b = swizzle[(mapping >> 6) & 7u];
  view->swizzle_a = swizzle[(mapping >> 9) & 7u];
  return true;
}

/* One stage's constant buffers, SRVs and (C13) UAVs: raw/structured UAVs
 * as shader buffers u#, typed UAVs as images u# (the dxbc_to_nir slots in
 * every stage); views are released and UAVs unbound by unbind_stage. */
/* C47: the sampler-view template of a texture SRV for a shader dimension
 * (the slot path takes the dimension from the compiled shader, a heap
 * descriptor from its D3D12 view): 1, 0 to leave the view unbound (a
 * 1D/3D/cube view no shader declaration reads), or -1 when invalid. The
 * component mapping is already in templ. */
static int srv_texture_template(const AGX_D3D12_SRV &srv, uint8_t dimension, struct pipe_resource **out,
                                    struct pipe_sampler_view *templ) {
  struct pipe_resource *texture = static_cast<struct pipe_resource *>(srv.Buffer->Resource);
  const enum pipe_format format = FormatTranslate(srv.Format, FALSE);
  if (!texture || format == PIPE_FORMAT_NONE || !srv.Levels || !srv.Layers ||
      srv.FirstLevel > texture->last_level || srv.Levels - 1u > texture->last_level - srv.FirstLevel ||
      srv.FirstLayer >= texture->array_size || srv.Layers > texture->array_size - srv.FirstLayer)
    return -1;
  templ->format = format;
  templ->target = texture->target;
  if(texture->target==PIPE_TEXTURE_1D || texture->target==PIPE_TEXTURE_1D_ARRAY) {
    if(dimension==DXBC_SRV_UNUSED) return 0;
    if(srv.Cube || (dimension!=DXBC_SRV_1D && dimension!=DXBC_SRV_1D_ARRAY) ||
       (dimension==DXBC_SRV_1D && srv.Layers!=1)) return -1;
    templ->target=dimension==DXBC_SRV_1D_ARRAY?PIPE_TEXTURE_1D_ARRAY:PIPE_TEXTURE_1D;
  } else if(dimension==DXBC_SRV_1D || dimension==DXBC_SRV_1D_ARRAY) return -1;
  if(texture->target==PIPE_TEXTURE_3D) {
    if(dimension==DXBC_SRV_UNUSED) return 0;
    if(dimension!=DXBC_SRV_3D || srv.Cube || srv.FirstLayer || srv.Layers!=1) return -1;
  } else if(dimension==DXBC_SRV_3D) return -1;
  if (srv.Cube) {
    if (dimension == DXBC_SRV_UNUSED) return 0;
    if ((texture->target != PIPE_TEXTURE_2D_ARRAY && texture->target != PIPE_TEXTURE_2D) ||
        texture->width0 != texture->height0 || texture->nr_samples > 1 || srv.Layers % 6u ||
        (dimension != DXBC_SRV_CUBE && dimension != DXBC_SRV_CUBE_ARRAY) ||
        (dimension == DXBC_SRV_CUBE && srv.Layers != 6u)) return -1;
    templ->target = dimension == DXBC_SRV_CUBE ? PIPE_TEXTURE_CUBE : PIPE_TEXTURE_CUBE_ARRAY;
  } else if (dimension == DXBC_SRV_CUBE || dimension == DXBC_SRV_CUBE_ARRAY) {
    return -1;
  }
  templ->u.tex.first_level = static_cast<uint8_t>(srv.FirstLevel);
  templ->u.tex.last_level = static_cast<uint8_t>(srv.FirstLevel + srv.Levels - 1);
  templ->u.tex.first_layer = static_cast<uint16_t>(srv.FirstLayer);
  templ->u.tex.last_layer = static_cast<uint16_t>(srv.FirstLayer + srv.Layers - 1);
  *out = texture;
  return 1;
}
/* C47: the descriptor-heap form of an SRV: Asahi's packed texture
 * descriptor of the view a texture SRV of this dimension binds, and the
 * texture's layout key. Other SRVs (and invalid ones, E_INVALIDARG) get
 * Asahi's null texture descriptor and key 0. */
extern "C" HRESULT AgxD3d12OwnerSrvDescriptor(AGX_D3D12_OWNER *owner, const AGX_D3D12_SRV *srv, uint8_t dimension,
                                              void *out, unsigned long long *key) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !out || !key || AgxWin32AsahiTextureDescriptorBytes() != AGX_D3D12_HEAP_TEXTURE_BYTES)
    return E_INVALIDARG;
  *key = 0;
  AgxWin32AsahiNullTextureDescriptor(out);
  if (!srv || !srv->Buffer || !srv->Buffer->Resource) return S_OK;
  struct pipe_sampler_view templ = {};
  struct pipe_resource *texture = NULL;
  if (!srv_component_mapping(srv->ComponentMapping, &templ)) return E_INVALIDARG;
  if (srv->Texture) {
    if (srv_texture_template(*srv, dimension, &texture, &templ) <= 0) return E_INVALIDARG;
  } else {
    /* a buffer SRV as bind_stage binds it: typed texels of its format, raw
     * and structured buffers as R32_UINT dwords */
    texture = static_cast<struct pipe_resource *>(srv->Buffer->Resource);
    if (srv->Offset >= srv->Buffer->Bytes || srv->Buffer->Bytes > 0xffffffffull) return E_INVALIDARG;
    const unsigned long long room = srv->Buffer->Bytes - srv->Offset;
    templ.format = srv->Format ? FormatTranslate(srv->Format, FALSE) : PIPE_FORMAT_R32_UINT;
    if (templ.format == PIPE_FORMAT_NONE) return E_INVALIDARG;
    templ.target = PIPE_BUFFER;
    templ.u.buf.offset = static_cast<unsigned>(srv->Offset);
    templ.u.buf.size = static_cast<unsigned>(srv->Bytes && srv->Bytes < room ? srv->Bytes : room);
    if (!srv->Format) templ.u.buf.size &= ~3u;
  }
  APPLE_AGX_U64 packed_key = 0;
  if (!AgxWin32AsahiTextureDescriptor(owner->Context, texture, &templ, out, &packed_key)) {
    AgxWin32AsahiNullTextureDescriptor(out);
    return E_OUTOFMEMORY;
  }
  *key = packed_key;
  return S_OK;
}
extern "C" void AgxD3d12NullTextureDescriptor(void *out) { AgxWin32AsahiNullTextureDescriptor(out); }
extern "C" unsigned long long AgxD3d12OwnerLayoutKey(AGX_D3D12_OWNER *owner, const AGX_D3D12_BUFFER *buffer) {
  OwnerLock lock(owner);
  return owner && buffer ? AgxWin32AsahiResourceLayoutKey(static_cast<struct pipe_resource *>(buffer->Resource)) : 0u;
}
extern "C" HRESULT AgxD3d12OwnerSetHeap(AGX_D3D12_OWNER *owner, unsigned stage, const AGX_D3D12_HEAP_BINDING *binding) {
  OwnerLock lock(owner);
  if (!owner || stage >= AGX_D3D12_HEAP_STAGES) return E_INVALIDARG;
  if (binding && ((binding->Heap && !binding->Heap->Resource) || binding->Ranges > AGX_D3D12_HEAP_RANGES ||
                  (binding->TextureCount && !binding->Textures) || (binding->WriteCount && !binding->Writes) ||
                  (binding->SamplerCount && !binding->Samplers) || binding->SamplerCount > DXBC_HEAP_SAMPLER_MAP_MAX))
    return E_INVALIDARG;
  if (binding) owner->PendingHeaps[stage] = *binding;
  else memset(&owner->PendingHeaps[stage], 0, sizeof(owner->PendingHeaps[stage]));
  return S_OK;
}
static const mesa_shader_stage heap_stages[AGX_D3D12_HEAP_STAGES] = {
  MESA_SHADER_VERTEX, MESA_SHADER_FRAGMENT, MESA_SHADER_GEOMETRY, MESA_SHADER_TESS_CTRL, MESA_SHADER_TESS_EVAL,
  MESA_SHADER_COMPUTE};
static_assert(DXBC_TIR_GS_HEAP_TABLE_CB == DXBC_HEAP_TABLE_CB &&
                  DXBC_TIR_GS_HEAP_VIEWPORT_BYTES == 4u * (4u * AGX_D3D12_HEAP_RANGES + DXBC_HEAP_SAMPLER_MAP_MAX),
              "the wrapped GS's heap-table viewport (dxbc_tir_gs.h)");
static_assert(AGX_D3D12_HEAP_RANGES == DXBC_HEAP_RANGE_MAX && AGX_D3D12_CBV_SLOTS <= DXBC_HEAP_TABLE_CB &&
                  sizeof(AGX_D3D12_HEAP_BINDING::First) == DXBC_HEAP_SAMPLER_MAP_OFFSET &&
                  AGX_D3D12_SAMPLER_SLOTS <= DXBC_HEAP_SAMPLER_INDEX,
              "heap ranges and their constant buffers (dxbc_bindings.h)");
/* C47: the pending heaps of the graphics stages (or compute) become constant
 * buffers 14/15 and reads of the batch the draw (dispatch) uses: call after
 * the framebuffer and stage bindings, right before draw_vbo/launch_grid */
static void bind_heaps(AGX_D3D12_OWNER *owner, bool compute) {
  struct pipe_context *pipe = owner->Context;
  const unsigned first = compute ? unsigned(AGX_D3D12_HEAP_CS) : 0u;
  const unsigned end = compute ? unsigned(AGX_D3D12_HEAP_STAGES) : unsigned(AGX_D3D12_HEAP_CS);
  for (unsigned s = first; s < end; ++s) {
    const AGX_D3D12_HEAP_BINDING &h = owner->PendingHeaps[s];
    if (!h.Heap && !h.SamplerCount) continue;
    /* the samplers first: entering them may flush the batch the reads and
     * writes below join */
    uint32_t *words = owner->HeapTables[s];
    memcpy(words, h.First, sizeof(h.First));
    unsigned bytes = sizeof(h.First);
    if (h.SamplerCount) {
      APPLE_AGX_U16 handles[DXBC_HEAP_SAMPLER_MAP_MAX];
      if (!AgxWin32AsahiBatchSamplerHandles(pipe, compute ? 1 : 0, h.Samplers, sizeof(h.Samplers[0]), h.SamplerCount, handles)) {
        const UINT receipt[] = {s, h.SamplerCount};
        AdmissionUmdDiagnostic("reject-d3d12-heap-samplers", E_OUTOFMEMORY, receipt, 2);
        memset(handles, 0, h.SamplerCount * sizeof(handles[0]));
      }
      for (unsigned i = 0; i < h.SamplerCount; ++i)
        words[4 * AGX_D3D12_HEAP_RANGES + i] = h.Samplers[i].Valid ? handles[i] | (uint32_t(h.Samplers[i].Bias) << 16) : 0u;
      bytes += 4u * h.SamplerCount;
    }
    if (heap_stages[s] == MESA_SHADER_GEOMETRY && owner->TirViewportValid) {
      /* C61C: a wrapped GS reading heap ranges reads the viewport here */
      memcpy(words + DXBC_TIR_GS_HEAP_VIEWPORT_BYTES / 4u, owner->TirViewport, sizeof(owner->TirViewport));
      bytes = DXBC_TIR_GS_HEAP_VIEWPORT_BYTES + sizeof(owner->TirViewport);
    }
    struct pipe_constant_buffer table = {};
    table.user_buffer = words;
    table.buffer_size = bytes;
    pipe->set_constant_buffer(pipe, heap_stages[s], DXBC_HEAP_TABLE_CB, &table);
    if (h.Heap) {
      struct pipe_constant_buffer heap = {};
      heap.buffer = static_cast<struct pipe_resource *>(h.Heap->Resource);
      heap.buffer_size = static_cast<unsigned>(h.Heap->Bytes < 65536ull ? h.Heap->Bytes : 65536ull);
      pipe->set_constant_buffer(pipe, heap_stages[s], DXBC_HEAP_MIRROR_CB, &heap);
    }
    struct pipe_resource *resources[64];
    for (unsigned i = 0; i < h.TextureCount;) {
      unsigned n = 0;
      for (; n < 64u && i < h.TextureCount; ++i)
        resources[n++] = static_cast<struct pipe_resource *>(h.Textures[i].Resource);
      AgxWin32AsahiBatchReadResources(pipe, resources, n, compute ? 1 : 0);
    }
    for (unsigned i = 0; i < h.WriteCount;) {
      unsigned n = 0;
      for (; n < 64u && i < h.WriteCount; ++i)
        resources[n++] = static_cast<struct pipe_resource *>(h.Writes[i].Resource);
      AgxWin32AsahiBatchWriteResources(pipe, resources, n, compute ? 1 : 0);
    }
  }
}
static void unbind_heaps(AGX_D3D12_OWNER *owner, bool compute) {
  struct pipe_context *pipe = owner->Context;
  const unsigned first = compute ? unsigned(AGX_D3D12_HEAP_CS) : 0u;
  const unsigned end = compute ? unsigned(AGX_D3D12_HEAP_STAGES) : unsigned(AGX_D3D12_HEAP_CS);
  for (unsigned s = first; s < end; ++s) {
    if (owner->PendingHeaps[s].Heap || owner->PendingHeaps[s].SamplerCount) {
      pipe->set_constant_buffer(pipe, heap_stages[s], DXBC_HEAP_TABLE_CB, NULL);
      pipe->set_constant_buffer(pipe, heap_stages[s], DXBC_HEAP_MIRROR_CB, NULL);
    }
    memset(&owner->PendingHeaps[s], 0, sizeof(owner->PendingHeaps[s]));
  }
}
static HRESULT bind_stage(struct pipe_context *pipe, mesa_shader_stage stage, const AGX_D3D12_CBV *cbvs,
                          const AGX_D3D12_SRV *srvs, const AGX_D3D12_UAV *uavs, struct pipe_sampler_view **views,
                          unsigned *view_count, const uint8_t *dimensions, const uint8_t *uav_dimensions,
                          unsigned srv_count = AGX_D3D12_SRV_SLOTS) {
  *view_count = 0;
  for (unsigned i = 0; i < AGX_D3D12_CBV_SLOTS; ++i) {
    struct pipe_constant_buffer cb = {};
    if (const AGX_D3D12_BUFFER *buffer = cbvs[i].Buffer) {
      if (!buffer->Resource || cbvs[i].Offset >= buffer->Bytes) return E_INVALIDARG;
      unsigned long long room = buffer->Bytes - cbvs[i].Offset;
      if (cbvs[i].Bytes && cbvs[i].Bytes < room) room = cbvs[i].Bytes;
      cb.buffer = static_cast<struct pipe_resource *>(buffer->Resource);
      cb.buffer_offset = static_cast<unsigned>(cbvs[i].Offset);
      cb.buffer_size = static_cast<unsigned>(room < 65536ull ? room : 65536ull);
    } else if (cbvs[i].Data) {
      if (!cbvs[i].Bytes || cbvs[i].Bytes > 65536u) return E_INVALIDARG;
      cb.user_buffer = cbvs[i].Data;
      cb.buffer_size = cbvs[i].Bytes;
    }
    pipe->set_constant_buffer(pipe, stage, i, cb.buffer || cb.user_buffer ? &cb : NULL);
  }
  for (unsigned i = 0; i < srv_count && i < AGX_D3D12_SRV_SLOTS; ++i) {
    const AGX_D3D12_BUFFER *buffer = srvs[i].Buffer;
    if (!buffer) continue;
    struct pipe_sampler_view templ = {};
    if (!srv_component_mapping(srvs[i].ComponentMapping, &templ)) return E_INVALIDARG;
    /* Raw/structured SRVs are reconstructed from scalar dword fetches,
     * so they cannot use a post-vector-fetch mapping in this owner yet. */
    if (!srvs[i].Texture && !srvs[i].Format &&
        srvs[i].ComponentMapping != AGX_D3D12_COMPONENT_MAPPING_IDENTITY) return E_INVALIDARG;
    if (srvs[i].Texture) {
      /* C6e: a texture view (gallium/frontends/d3d10umd CreateShaderResourceView) */
      struct pipe_resource *texture = NULL;
      const int shaped = srv_texture_template(srvs[i], dimensions ? dimensions[i] : DXBC_SRV_UNUSED, &texture, &templ);
      if (shaped < 0) return E_INVALIDARG;
      if (!shaped) continue;
      views[i] = pipe->create_sampler_view(pipe, texture, &templ);
      if (!views[i]) return E_OUTOFMEMORY;
      *view_count = i + 1;
      continue;
    }
    if (dimensions && (dimensions[i] == DXBC_SRV_CUBE || dimensions[i] == DXBC_SRV_CUBE_ARRAY || dimensions[i] == DXBC_SRV_3D || dimensions[i] == DXBC_SRV_1D || dimensions[i] == DXBC_SRV_1D_ARRAY)) return E_INVALIDARG;
    if (!buffer->Resource || srvs[i].Offset >= buffer->Bytes) return E_INVALIDARG;
    const unsigned long long room = buffer->Bytes - srvs[i].Offset;
    /* C9: a typed buffer SRV reads texels of its format */
    templ.format = srvs[i].Format ? FormatTranslate(srvs[i].Format, FALSE) : PIPE_FORMAT_R32_UINT;
    if (templ.format == PIPE_FORMAT_NONE) return E_INVALIDARG;
    templ.target = PIPE_BUFFER;
    templ.u.buf.offset = static_cast<unsigned>(srvs[i].Offset);
    templ.u.buf.size = static_cast<unsigned>(srvs[i].Bytes && srvs[i].Bytes < room ? srvs[i].Bytes : room);
    /* C20: typed elements may be1/2 bytes. Only raw/structured dword
     * fetches round their byte span; typed views retain every element. */
    if (!srvs[i].Format) templ.u.buf.size &= ~3u;
    views[i] = pipe->create_sampler_view(pipe, static_cast<struct pipe_resource *>(buffer->Resource), &templ);
    if (!views[i]) return E_OUTOFMEMORY;
    *view_count = i + 1;
  }
  if (*view_count) pipe->set_sampler_views(pipe, stage, 0, *view_count, 0, views);
  /* C33: UAV buffers at 0..7, their append/consume counters at 8..15 */
  static_assert(DXBC_NIR_UAV_COUNTER_SLOT == AGX_D3D12_UAV_SLOTS, "counter slots follow the UAV slots");
  struct pipe_shader_buffer buffers[2 * AGX_D3D12_UAV_SLOTS] = {};
  struct pipe_image_view images[AGX_D3D12_UAV_SLOTS] = {};
  for (unsigned i = 0; i < AGX_D3D12_UAV_SLOTS; ++i) {
    const AGX_D3D12_BUFFER *buffer = uavs[i].Buffer;
    if (!buffer) continue;
    if (!buffer->Resource) return E_INVALIDARG;
    if (const AGX_D3D12_BUFFER *counter = uavs[i].Counter) {
      if (!counter->Resource || counter->Bytes > 0xffffffffull || uavs[i].CounterOffset > counter->Bytes - 4u)
        return E_INVALIDARG;
      buffers[DXBC_NIR_UAV_COUNTER_SLOT + i].buffer = static_cast<struct pipe_resource *>(counter->Resource);
      buffers[DXBC_NIR_UAV_COUNTER_SLOT + i].buffer_offset = static_cast<unsigned>(uavs[i].CounterOffset);
      buffers[DXBC_NIR_UAV_COUNTER_SLOT + i].buffer_size = 4u;
    }
    if (uav_dimensions && uav_dimensions[i] == DXBC_UAV_UNUSED &&
        (static_cast<const struct pipe_resource *>(buffer->Resource)->target == PIPE_TEXTURE_1D ||
         static_cast<const struct pipe_resource *>(buffer->Resource)->target == PIPE_TEXTURE_1D_ARRAY)) continue;
    if (uav_dimensions && !uav_dimension_matches(uav_dimensions[i], &uavs[i])) return E_INVALIDARG;
    if (uavs[i].Format) {
      if (!uav_image(&uavs[i], &images[i])) return E_INVALIDARG;
      const auto target=static_cast<const struct pipe_resource *>(buffer->Resource)->target;
      if(target==PIPE_TEXTURE_1D || target==PIPE_TEXTURE_1D_ARRAY)
        images[i].u.tex.single_layer_view=uav_dimensions && uav_dimensions[i]==DXBC_UAV_1D;
      continue;
    }
    if (uavs[i].Offset >= buffer->Bytes || buffer->Bytes > 0xffffffffull) return E_INVALIDARG;
    buffers[i].buffer = static_cast<struct pipe_resource *>(buffer->Resource);
    buffers[i].buffer_offset = static_cast<unsigned>(uavs[i].Offset);
    const unsigned long long room = buffer->Bytes - uavs[i].Offset;
    buffers[i].buffer_size = static_cast<unsigned>(uavs[i].Bytes && uavs[i].Bytes < room ? uavs[i].Bytes : room);
  }
  pipe->set_shader_buffers(pipe, stage, 0, 2 * AGX_D3D12_UAV_SLOTS, buffers, (1u << (2 * AGX_D3D12_UAV_SLOTS)) - 1u);
  pipe->set_shader_images(pipe, stage, 0, AGX_D3D12_UAV_SLOTS, 0, images);
  return S_OK;
}
static void unbind_stage(struct pipe_context *pipe, mesa_shader_stage stage, struct pipe_sampler_view **views,
                         unsigned view_count) {
  for (unsigned i = 0; i < AGX_D3D12_CBV_SLOTS; ++i) pipe->set_constant_buffer(pipe, stage, i, NULL);
  /* the batch holds its resources; UAV bindings must not outlive the heaps */
  pipe->set_shader_buffers(pipe, stage, 0, 2 * AGX_D3D12_UAV_SLOTS, NULL, 0);
  pipe->set_shader_images(pipe, stage, 0, 0, AGX_D3D12_UAV_SLOTS, NULL);
  if (!view_count) return;
  pipe->set_sampler_views(pipe, stage, 0, 0, view_count, NULL);
  for (unsigned i = 0; i < view_count; ++i)
    if (views[i]) pipe->sampler_view_release(pipe, views[i]);
}

static void release_samplers(struct pipe_context *pipe, void **vs, unsigned vs_count, void **ps, unsigned ps_count) {
  void *none[AGX_D3D12_SAMPLER_SLOTS] = {};
  if (vs_count) pipe->bind_sampler_states(pipe, MESA_SHADER_VERTEX, 0, vs_count, none);
  if (ps_count) pipe->bind_sampler_states(pipe, MESA_SHADER_FRAGMENT, 0, ps_count, none);
  for (unsigned i = 0; i < AGX_D3D12_SAMPLER_SLOTS; ++i) {
    if (vs[i]) pipe->delete_sampler_state(pipe, vs[i]);
    if (ps[i]) pipe->delete_sampler_state(pipe, ps[i]);
  }
}


/* C27/C37: an optional stage (GS, HS, DS) of one draw: its views and
 * samplers. The draw keeps them on the heap (three stages of views would
 * add ~3.5 KB to its frame). */
struct OptionalStage {
  mesa_shader_stage stage;
  bool bound;
  struct pipe_sampler_view *views[AGX_D3D12_SRV_SLOTS];
  void *samplers[AGX_D3D12_SAMPLER_SLOTS];
  unsigned view_count, sampler_count;
};
enum { kOptionalStages = 3 };
/* all_slots: a failed bind_stage may have bound views past view_count */
static void release_optional_stages(struct pipe_context *pipe, OptionalStage *stages, bool all_slots) {
  void *none[AGX_D3D12_SAMPLER_SLOTS] = {};
  for (unsigned s = 0; stages && s < kOptionalStages; ++s) {
    OptionalStage &o = stages[s];
    if (!o.bound) continue;
    unbind_stage(pipe, o.stage, o.views, all_slots ? AGX_D3D12_SRV_SLOTS : o.view_count);
    if (o.sampler_count) pipe->bind_sampler_states(pipe, o.stage, 0, o.sampler_count, none);
    for (unsigned i = 0; i < AGX_D3D12_SAMPLER_SLOTS; ++i)
      if (o.samplers[i]) pipe->delete_sampler_state(pipe, o.samplers[i]);
  }
  if (stages) HeapFree(GetProcessHeap(), 0, stages);
}

extern "C" HRESULT AgxD3d12OwnerDispatch(AGX_D3D12_OWNER *owner, const AGX_D3D12_COMPUTE *compute,
                                         const AGX_D3D12_UAV uavs[AGX_D3D12_UAV_SLOTS],
                                         const AGX_D3D12_CBV cbvs[AGX_D3D12_CBV_SLOTS],
                                         const AGX_D3D12_SRV srvs[AGX_D3D12_SRV_SLOTS],
                                         const AGX_D3D12_SAMPLER samplers[AGX_D3D12_SAMPLER_SLOTS], unsigned x,
                                         unsigned y, unsigned z, const AGX_D3D12_INDIRECT *indirect) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context || !compute || !compute->State || !uavs || !cbvs || !srvs || !samplers ||
      (!indirect && (!x || !y || !z)))
    return E_INVALIDARG;
  /* C14: indirect grids (D3D12_DISPATCH_ARGUMENTS = Gallium's 3 x uint) */
  if (indirect && (!indirect->Buffer || !indirect->Buffer->Resource || !indirect->MaxCount || indirect->CountBuffer ||
                   indirect->Stride < 12u || indirect->Buffer->Bytes > 0xffffffffull ||
                   indirect->Offset + static_cast<unsigned long long>(indirect->Stride) * (indirect->MaxCount - 1u) + 12u >
                       indirect->Buffer->Bytes))
    return E_INVALIDARG;
  struct pipe_context *pipe = owner->Context;
  /* C9: sampler states for this dispatch (released after the launch) */
  void *states[AGX_D3D12_SAMPLER_SLOTS] = {};
  unsigned state_count = 0;
  for (unsigned i = 0; i < AGX_D3D12_SAMPLER_SLOTS; ++i)
    if (samplers[i].Valid && (states[i] = create_sampler(pipe, &samplers[i]))) state_count = i + 1;
  struct pipe_sampler_view *views[AGX_D3D12_SRV_SLOTS] = {};
  unsigned view_count = 0;
  /* C5-C5d via the draw stages' binding (C9: texture and typed SRVs, typed
   * UAVs as images);
   * Asahi uploads a user buffer when it is set, so root constants are
   * captured here */
  HRESULT hr = bind_stage(pipe, MESA_SHADER_COMPUTE, cbvs, srvs, uavs, views, &view_count, compute->SrvDimensions, compute->UavDimensions);
  void *none[AGX_D3D12_SAMPLER_SLOTS] = {};
  if (FAILED(hr)) {
    unbind_stage(pipe, MESA_SHADER_COMPUTE, views, AGX_D3D12_SRV_SLOTS);
    for (unsigned i = 0; i < state_count; ++i)
      if (states[i]) pipe->delete_sampler_state(pipe, states[i]);
    return hr;
  }
  pipe->bind_compute_state(pipe, compute->State);
  if (state_count) pipe->bind_sampler_states(pipe, MESA_SHADER_COMPUTE, 0, state_count, states);
  struct pipe_grid_info info = {};
  info.work_dim = 3;
  for (unsigned i = 0; i < 3; ++i) info.block[i] = compute->Block[i];
  info.grid[0] = x;
  info.grid[1] = y;
  info.grid[2] = z;
  bind_heaps(owner, true); /* C47 */
  if (indirect) {
    info.indirect = static_cast<struct pipe_resource *>(indirect->Buffer->Resource);
    for (unsigned i = 0; i < indirect->MaxCount; ++i) {
      info.indirect_offset = static_cast<unsigned>(indirect->Offset + static_cast<unsigned long long>(indirect->Stride) * i);
      pipe->launch_grid(pipe, &info);
    }
  } else {
    pipe->launch_grid(pipe, &info);
  }
  unbind_heaps(owner, true);
  /* EXP1850 ModelViewer: memory_barrier after every launch flushed each
   * UAV-writing dispatch into its own submission (~70 a frame, ~40 ms).
   * Asahi orders the launches of one compute batch by a CDM barrier that
   * flushes and invalidates the caches (agx_launch_internal), and flushes a
   * resource's writer batch before another batch or a CPU map uses it. */
  unbind_stage(pipe, MESA_SHADER_COMPUTE, views, view_count);
  if (state_count) {
    pipe->bind_sampler_states(pipe, MESA_SHADER_COMPUTE, 0, state_count, none);
    for (unsigned i = 0; i < state_count; ++i)
      if (states[i]) pipe->delete_sampler_state(pipe, states[i]);
  }
  ++owner->PendingDispatches;
  return S_OK;
}

static enum mesa_prim primitive(unsigned topology) {
  switch (topology) {
  case 1: return MESA_PRIM_POINTS;          /* D3D_PRIMITIVE_TOPOLOGY_POINTLIST */
  case 2: return MESA_PRIM_LINES;           /* LINELIST */
  case 3: return MESA_PRIM_LINE_STRIP;      /* LINESTRIP */
  case 4: return MESA_PRIM_TRIANGLES;       /* TRIANGLELIST */
  case 5: return MESA_PRIM_TRIANGLE_STRIP;  /* TRIANGLESTRIP */
  case 10: return MESA_PRIM_LINES_ADJACENCY;
  case 11: return MESA_PRIM_LINE_STRIP_ADJACENCY;
  case 12: return MESA_PRIM_TRIANGLES_ADJACENCY;
  case 13: return MESA_PRIM_TRIANGLE_STRIP_ADJACENCY;
  default: return topology >= 33 && topology <= 64 ? MESA_PRIM_PATCHES /* C37: 1..32 control points */ : MESA_PRIM_COUNT;
  }
}

static LONGLONG qpc_now() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}
static UINT qpc_us(LONGLONG ticks) {
  static LONGLONG frequency;
  if (!frequency) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    frequency = f.QuadPart ? f.QuadPart : 1;
  }
  return static_cast<UINT>(ticks * 1000000 / frequency);
}

/* UAV-only rendering (EXP1889 C60A): with neither an RTV nor a DSV the pass
 * rasterizes within the viewport; Gallium sizes such a framebuffer by
 * width/height (ARB_framebuffer_no_attachments, as the D3D10 frontend's
 * AgxD3d10NoAttachmentSize): the viewport's far edge rounded up, within the
 * D3D12 16384-pixel render window. */
static unsigned no_attachment_extent(float origin, float size) {
  const float edge = ceilf(origin + size);
  return edge >= 16384.0f ? 16384u : edge >= 1.0f ? static_cast<unsigned>(edge) : 1u;
}

extern "C" HRESULT AgxD3d12OwnerDraw(AGX_D3D12_OWNER *owner, const AGX_D3D12_DRAW *draw) {
  OwnerLock lock(owner);
  const LONGLONG entered = qpc_now();
  if (!owner || !owner->Context || !draw || !draw->Pipeline || !draw->Pipeline->Vs || draw->NumRtvs > 8)
    return E_INVALIDARG;
  const enum mesa_prim mode = primitive(draw->Topology);
  if (mode == MESA_PRIM_COUNT) return E_NOTIMPL;
  /* C37: patch lists draw exactly with a hull+domain pipeline */
  if ((mode == MESA_PRIM_PATCHES) != (draw->Pipeline->Hs && draw->Pipeline->Ds)) return E_INVALIDARG;
  /* Indirect counts live in the argument buffer, not these direct fields. */
  if (!draw->Indirect.Buffer && (!draw->VertexCount || !draw->InstanceCount)) return S_OK;
  struct pipe_context *pipe = owner->Context;
  AGX_D3D12_GRAPHICS *graphics = draw->Pipeline;
  /* framebuffer from the RTVs: the size is the smallest bound level */
  struct pipe_framebuffer_state fb = {};
  fb.nr_cbufs = static_cast<uint8_t>(draw->NumRtvs);
  unsigned width = 0, height = 0;
  for (unsigned i = 0; i < draw->NumRtvs; ++i) {
    if (!draw->Rtv[i].Texture || !draw->Rtv[i].Texture->Resource) continue;
    struct pipe_resource *texture = static_cast<struct pipe_resource *>(draw->Rtv[i].Texture->Resource);
    const enum pipe_format format = FormatTranslate(draw->Rtv[i].Format, FALSE);
    if (format == PIPE_FORMAT_NONE || draw->Rtv[i].Level > texture->last_level || !draw->Rtv[i].Layers) return E_INVALIDARG;
    if(texture->target==PIPE_TEXTURE_3D && (draw->Rtv[i].Layers!=1 ||
        draw->Rtv[i].FirstLayer>=texture_layer_extent(texture,draw->Rtv[i].Level))) return E_INVALIDARG;
    fb.cbufs[i].format = format;
    fb.cbufs[i].level = draw->Rtv[i].Level;
    fb.cbufs[i].first_layer = static_cast<uint16_t>(draw->Rtv[i].FirstLayer);
    fb.cbufs[i].last_layer = static_cast<uint16_t>(draw->Rtv[i].FirstLayer + draw->Rtv[i].Layers - 1);
    fb.cbufs[i].texture = texture;
    const unsigned w = level_extent(texture->width0, draw->Rtv[i].Level), h = level_extent(texture->height0, draw->Rtv[i].Level);
    width = width && width < w ? width : w;
    height = height && height < h ? height : h;
  }
  if (const AGX_D3D12_BUFFER *dsv = draw->Dsv.Texture) {
    /* C6f: the depth-stencil view joins the framebuffer and its size */
    struct pipe_resource *texture = static_cast<struct pipe_resource *>(dsv->Resource);
    const enum pipe_format format = FormatTranslate(draw->Dsv.Format, TRUE);
    if (!texture || format == PIPE_FORMAT_NONE || draw->Dsv.Level > texture->last_level || !draw->Dsv.Layers)
      return E_INVALIDARG;
    fb.zsbuf.format = format;
    fb.zsbuf.level = draw->Dsv.Level;
    fb.zsbuf.first_layer = static_cast<uint16_t>(draw->Dsv.FirstLayer);
    fb.zsbuf.last_layer = static_cast<uint16_t>(draw->Dsv.FirstLayer + draw->Dsv.Layers - 1);
    fb.zsbuf.texture = texture;
    const unsigned w = level_extent(texture->width0, draw->Dsv.Level), h = level_extent(texture->height0, draw->Dsv.Level);
    width = width && width < w ? width : w;
    height = height && height < h ? height : h;
  }
  if (!draw->NumRtvs && !draw->Dsv.Texture) {
    width = no_attachment_extent(draw->Viewport[0], draw->Viewport[2]);
    height = no_attachment_extent(draw->Viewport[1], draw->Viewport[3]);
    fb.samples = 1;
  }
  if (!width || !height) return E_INVALIDARG;
  fb.width = width;
  fb.height = height;
  fb.layers = 1;
  /* vertex elements with this draw's strides (cached per pipeline) */
  struct pipe_vertex_element elements[AGX_D3D12_MAX_ELEMENTS] = {};
  unsigned count = 0, vb_count = 0;
  bool strides_match = graphics->Velems != NULL;
  for (unsigned i = 0; i < graphics->NumElements; ++i) {
    const unsigned r = graphics->Element[i].Register, slot = graphics->Element[i].Slot;
    elements[r].src_offset = static_cast<uint16_t>(graphics->Element[i].Offset);
    elements[r].vertex_buffer_index = static_cast<uint8_t>(slot);
    elements[r].src_format = static_cast<uint8_t>(graphics->Element[i].Format);
    elements[r].src_stride = draw->Vb[slot].Stride;
    elements[r].instance_divisor = graphics->Element[i].Divisor;
    count = count > r + 1 ? count : r + 1;
    vb_count = vb_count > slot + 1 ? vb_count : slot + 1;
    strides_match = strides_match && graphics->VelemsStrides[slot] == draw->Vb[slot].Stride;
  }
  for (unsigned r = 0; r < count; ++r)
    if (!elements[r].src_format) elements[r].src_format = PIPE_FORMAT_R32G32B32A32_FLOAT; /* a hole reads vertex 0 of slot 0 */
  if (count && !strides_match) {
    if (graphics->Velems) pipe->delete_vertex_elements_state(pipe, graphics->Velems);
    graphics->Velems = pipe->create_vertex_elements_state(pipe, count, elements);
    for (unsigned s = 0; s < AGX_D3D12_VB_SLOTS; ++s) graphics->VelemsStrides[s] = draw->Vb[s].Stride;
    if (!graphics->Velems) return E_OUTOFMEMORY;
  }
  struct pipe_vertex_buffer buffers[AGX_D3D12_VB_SLOTS] = {};
  for (unsigned s = 0; s < vb_count; ++s) {
    const AGX_D3D12_BUFFER *buffer = draw->Vb[s].Buffer;
    if (!buffer) continue;
    if (!buffer->Resource || draw->Vb[s].Offset >= buffer->Bytes) return E_INVALIDARG;
    buffers[s].buffer.resource = static_cast<struct pipe_resource *>(buffer->Resource);
    buffers[s].buffer_offset = static_cast<unsigned>(draw->Vb[s].Offset);
  }
  if (!owner->DrawFramebufferValid || memcmp(&owner->DrawFramebuffer, &fb, sizeof(fb))) {
    pipe->set_framebuffer_state(pipe, &fb);
    owner->DrawFramebuffer = fb;
    owner->DrawFramebufferValid = true;
  }
  pipe->bind_vs_state(pipe, graphics->Vs);
  if (!graphics->Fs && !owner->EmptyFs) owner->EmptyFs = util_make_empty_fragment_shader(pipe);
  if (!graphics->Fs && !owner->EmptyFs) return E_OUTOFMEMORY;
  pipe->bind_fs_state(pipe, graphics->Fs ? graphics->Fs : owner->EmptyFs);
  pipe->bind_gs_state(pipe, graphics->Gs); /* C27: NULL unbinds a previous draw's GS */
  pipe->bind_tcs_state(pipe, graphics->Hs); /* C37: likewise the tessellation stages */
  pipe->bind_tes_state(pipe, graphics->Ds);
  if (mode == MESA_PRIM_PATCHES) pipe->set_patch_vertices(pipe, static_cast<uint8_t>(draw->Topology - 32));
  pipe->bind_blend_state(pipe, graphics->Blend);
  pipe->bind_depth_stencil_alpha_state(pipe, graphics->DepthStencil);
  pipe->bind_rasterizer_state(pipe, graphics->Rasterizer);
  if (!count && !owner->EmptyVelems) owner->EmptyVelems = pipe->create_vertex_elements_state(pipe, 0, NULL);
  if (!count && !owner->EmptyVelems) return E_OUTOFMEMORY;
  pipe->bind_vertex_elements_state(pipe, count ? graphics->Velems : owner->EmptyVelems);
  pipe->set_vertex_buffers(pipe, vb_count, buffers);
  struct pipe_viewport_state viewport = {};
  const float hw = draw->Viewport[2] / 2.0f, hh = draw->Viewport[3] / 2.0f;
  viewport.scale[0] = hw;
  viewport.scale[1] = -hh;
  viewport.scale[2] = draw->Viewport[5] - draw->Viewport[4];
  viewport.translate[0] = hw + draw->Viewport[0];
  viewport.translate[1] = hh + draw->Viewport[1];
  viewport.translate[2] = draw->Viewport[4];
  viewport.swizzle_x = PIPE_VIEWPORT_SWIZZLE_POSITIVE_X;
  viewport.swizzle_y = PIPE_VIEWPORT_SWIZZLE_POSITIVE_Y;
  viewport.swizzle_z = PIPE_VIEWPORT_SWIZZLE_POSITIVE_Z;
  viewport.swizzle_w = PIPE_VIEWPORT_SWIZZLE_POSITIVE_W;
  pipe->set_viewport_states(pipe, 0, 1, &viewport);
  struct pipe_scissor_state scissor = {};
  scissor.minx = static_cast<uint16_t>(draw->Scissor.left < 0 ? 0 : draw->Scissor.left);
  scissor.miny = static_cast<uint16_t>(draw->Scissor.top < 0 ? 0 : draw->Scissor.top);
  scissor.maxx = static_cast<uint16_t>(draw->Scissor.right < 0 ? 0 : draw->Scissor.right);
  scissor.maxy = static_cast<uint16_t>(draw->Scissor.bottom < 0 ? 0 : draw->Scissor.bottom);
  pipe->set_scissor_states(pipe, 0, 1, &scissor);
  struct pipe_blend_color blend_color = {};
  for (unsigned i = 0; i < 4; ++i) blend_color.color[i] = draw->BlendFactor[i];
  pipe->set_blend_color(pipe, &blend_color);
  struct pipe_stencil_ref ref = {};
  ref.ref_value[0] = ref.ref_value[1] = static_cast<uint8_t>(draw->StencilRef);
  pipe->set_stencil_ref(pipe, ref);
  pipe->set_sample_mask(pipe, graphics->SampleMask); /* C32 */
  /* C6e: sampler states of each stage for this draw */
  void *vs_samplers[AGX_D3D12_SAMPLER_SLOTS] = {}, *ps_samplers[AGX_D3D12_SAMPLER_SLOTS] = {};
  unsigned vs_sampler_count = 0, ps_sampler_count = 0;
  for (unsigned i = 0; i < AGX_D3D12_SAMPLER_SLOTS; ++i) {
    if (draw->Vs.Samplers[i].Valid && (vs_samplers[i] = create_sampler(pipe, &draw->Vs.Samplers[i]))) vs_sampler_count = i + 1;
    if (draw->Ps.Samplers[i].Valid && (ps_samplers[i] = create_sampler(pipe, &draw->Ps.Samplers[i]))) ps_sampler_count = i + 1;
  }
  if (vs_sampler_count) pipe->bind_sampler_states(pipe, MESA_SHADER_VERTEX, 0, vs_sampler_count, vs_samplers);
  if (ps_sampler_count) pipe->bind_sampler_states(pipe, MESA_SHADER_FRAGMENT, 0, ps_sampler_count, ps_samplers);
  struct pipe_sampler_view *vs_views[AGX_D3D12_SRV_SLOTS] = {}, *ps_views[AGX_D3D12_SRV_SLOTS] = {};
  unsigned vs_view_count = 0, ps_view_count = 0;
  HRESULT hr = bind_stage(pipe, MESA_SHADER_VERTEX, draw->Vs.Cbvs, draw->Vs.Srvs, draw->Vs.Uavs, vs_views, &vs_view_count, draw->Pipeline->VsSrvDimensions, draw->Pipeline->VsUavDimensions, draw->Vs.SrvCount);
  if (SUCCEEDED(hr))
    hr = bind_stage(pipe, MESA_SHADER_FRAGMENT, draw->Ps.Cbvs, draw->Ps.Srvs, draw->Ps.Uavs, ps_views, &ps_view_count, draw->Pipeline->PsSrvDimensions, draw->Pipeline->PsUavDimensions, draw->Ps.SrvCount);
  /* C27/C37: the optional stages' bindings and samplers */
  const struct {
    mesa_shader_stage stage;
    const void *shader;
    const decltype(draw->Gs) *bind;
    const uint8_t *srv_dimensions, *uav_dimensions;
  } optional[kOptionalStages] = {
      {MESA_SHADER_GEOMETRY, graphics->Gs, &draw->Gs, graphics->GsSrvDimensions, graphics->GsUavDimensions},
      {MESA_SHADER_TESS_CTRL, graphics->Hs, &draw->Hs, graphics->HsSrvDimensions, graphics->HsUavDimensions},
      {MESA_SHADER_TESS_EVAL, graphics->Ds, &draw->Ds, graphics->DsSrvDimensions, graphics->DsUavDimensions}};
  OptionalStage *stages = NULL;
  if (graphics->Gs || graphics->Hs || graphics->Ds) {
    stages = static_cast<OptionalStage *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, kOptionalStages * sizeof(OptionalStage)));
    if (!stages && SUCCEEDED(hr)) hr = E_OUTOFMEMORY;
  }
  for (unsigned s = 0; stages && s < kOptionalStages; ++s) {
    OptionalStage &o = stages[s];
    o.stage = optional[s].stage;
    if (!optional[s].shader) continue;
    o.bound = true;
    for (unsigned i = 0; i < AGX_D3D12_SAMPLER_SLOTS; ++i)
      if (optional[s].bind->Samplers[i].Valid && (o.samplers[i] = create_sampler(pipe, &optional[s].bind->Samplers[i])))
        o.sampler_count = i + 1;
    if (o.sampler_count) pipe->bind_sampler_states(pipe, o.stage, 0, o.sampler_count, o.samplers);
    if (SUCCEEDED(hr))
      hr = bind_stage(pipe, o.stage, optional[s].bind->Cbvs, optional[s].bind->Srvs, optional[s].bind->Uavs, o.views,
                      &o.view_count, optional[s].srv_dimensions, optional[s].uav_dimensions, optional[s].bind->SrvCount);
  }
  /* FL 11_1: viewport 0 for the TIR expansion GS (after the GS stage's
   * own constant buffers): viewport = ndc * scale + offset */
  owner->TirViewportValid = SUCCEEDED(hr) && (graphics->TirSamples || graphics->Conservative);
  if (owner->TirViewportValid) {
    const float vp[4] = {hw, -hh, hw + draw->Viewport[0], hh + draw->Viewport[1]};
    memcpy(owner->TirViewport, vp, sizeof(vp));
    struct pipe_constant_buffer cb = {};
    cb.buffer_size = sizeof(vp);
    cb.user_buffer = vp;
    pipe->set_constant_buffer(pipe, MESA_SHADER_GEOMETRY, DXBC_TIR_GS_CB, &cb);
  }
  if (FAILED(hr)) {
    unbind_stage(pipe, MESA_SHADER_VERTEX, vs_views, AGX_D3D12_SRV_SLOTS);
    unbind_stage(pipe, MESA_SHADER_FRAGMENT, ps_views, AGX_D3D12_SRV_SLOTS);
    release_samplers(pipe, vs_samplers, vs_sampler_count, ps_samplers, ps_sampler_count);
    release_optional_stages(pipe, stages, true);
    return hr;
  }
  struct pipe_draw_info info = {};
  info.mode = static_cast<uint8_t>(mode);
  info.start_instance = draw->StartInstance;
  info.instance_count = draw->InstanceCount;
  struct pipe_draw_start_count_bias range = {};
  range.start = draw->StartVertex;
  range.count = draw->VertexCount;
  if (draw->IndexSize) {
    /* the index buffer's byte offset becomes whole indices of the draw */
    const AGX_D3D12_BUFFER *ib = draw->IndexBuffer;
    if (!ib || !ib->Resource || (draw->IndexSize != 2 && draw->IndexSize != 4) || draw->IndexOffset % draw->IndexSize ||
        draw->IndexOffset >= ib->Bytes) {
      unbind_stage(pipe, MESA_SHADER_VERTEX, vs_views, vs_view_count);
      unbind_stage(pipe, MESA_SHADER_FRAGMENT, ps_views, ps_view_count);
      release_samplers(pipe, vs_samplers, vs_sampler_count, ps_samplers, ps_sampler_count);
      release_optional_stages(pipe, stages, false);
      return E_INVALIDARG;
    }
    info.index_size = static_cast<uint16_t>(draw->IndexSize);
    info.index.resource = static_cast<struct pipe_resource *>(ib->Resource);
    info.primitive_restart = draw->RestartIndex != 0;
    info.restart_index = draw->RestartIndex;
    range.start += static_cast<unsigned>(draw->IndexOffset / draw->IndexSize);
    range.index_bias = draw->BaseVertex;
  }
  /* C14: indirect draws read count/instances/start from the argument buffer */
  struct pipe_draw_indirect_info indirect = {};
  const AGX_D3D12_INDIRECT *in = &draw->Indirect;
  if (in->Buffer) {
    const unsigned args = draw->IndexSize ? 20u : 16u;
    const bool bad = !in->Buffer->Resource || !in->MaxCount || in->Stride < args || in->Buffer->Bytes > 0xffffffffull ||
                     in->Offset + static_cast<unsigned long long>(in->Stride) * (in->MaxCount - 1u) + args > in->Buffer->Bytes ||
                     (draw->IndexSize && draw->IndexOffset) ||
                     (in->CountBuffer && (!in->CountBuffer->Resource || in->CountOffset + 4u > in->CountBuffer->Bytes));
    if (bad) {
      unbind_stage(pipe, MESA_SHADER_VERTEX, vs_views, vs_view_count);
      unbind_stage(pipe, MESA_SHADER_FRAGMENT, ps_views, ps_view_count);
      release_samplers(pipe, vs_samplers, vs_sampler_count, ps_samplers, ps_sampler_count);
      release_optional_stages(pipe, stages, false);
      return E_INVALIDARG;
    }
    indirect.buffer = static_cast<struct pipe_resource *>(in->Buffer->Resource);
    indirect.offset = static_cast<unsigned>(in->Offset);
    indirect.stride = in->Stride;
    indirect.draw_count = in->MaxCount;
    if (in->CountBuffer) {
      indirect.indirect_draw_count = static_cast<struct pipe_resource *>(in->CountBuffer->Resource);
      indirect.indirect_draw_count_offset = static_cast<unsigned>(in->CountOffset);
    }
  }
  /* C39: stream-output targets at the views' buffer ranges; the D3D12
   * filled size (bytes written from BufferLocation) is the target's append
   * offset: copied in before the draw and back out after it, on the GPU in
   * submission order */
  struct pipe_stream_output_target *so[4] = {};
  unsigned so_count = 0;
  for (unsigned t = 0; graphics->Xfb && t < 4; ++t) {
    const auto &v = draw->So[t];
    if (!v.Buffer || !v.Buffer->Resource || !v.Filled || !v.Filled->Resource || v.Offset > v.Buffer->Bytes ||
        v.Size > v.Buffer->Bytes - v.Offset || v.Buffer->Bytes > 0xffffffffull || v.FilledOffset > 0x7ffffffbull ||
        v.FilledOffset + 4u > v.Filled->Bytes)
      continue;
    so[t] = pipe->create_stream_output_target(pipe, static_cast<struct pipe_resource *>(v.Buffer->Resource),
                                              static_cast<unsigned>(v.Offset), static_cast<unsigned>(v.Size));
    struct pipe_resource *offset = so[t] ? AgxWin32AsahiStreamOutputOffset(so[t]) : NULL;
    if (offset) {
      struct pipe_box box = {}; /* R8_UNORM buffers: bytes */
      box.x = static_cast<int>(v.FilledOffset);
      box.width = 4;
      box.height = box.depth = 1;
      pipe->resource_copy_region(pipe, offset, 0, 0, 0, 0, static_cast<struct pipe_resource *>(v.Filled->Resource), 0, &box);
    }
    so_count = t + 1;
  }
  if (so_count) {
    const unsigned append[4] = {~0u, ~0u, ~0u, ~0u};
    pipe->set_stream_output_targets(pipe, so_count, so, append, mode);
  }
  bind_heaps(owner, false); /* C47 */
  const LONGLONG bound = qpc_now();
  pipe->draw_vbo(pipe, &info, 0, in->Buffer ? &indirect : NULL, &range, 1);
  const LONGLONG drawn = qpc_now();
  unbind_heaps(owner, false);
  if (so_count) {
    pipe->set_stream_output_targets(pipe, 0, NULL, NULL, mode);
    for (unsigned t = 0; t < so_count; ++t) {
      if (!so[t]) continue;
      if (struct pipe_resource *offset = AgxWin32AsahiStreamOutputOffset(so[t])) {
        struct pipe_box box = {};
        box.width = 4;
        box.height = box.depth = 1;
        pipe->resource_copy_region(pipe, static_cast<struct pipe_resource *>(draw->So[t].Filled->Resource), 0,
                                   static_cast<unsigned>(draw->So[t].FilledOffset), 0, 0, offset, 0, &box);
      }
      pipe->stream_output_target_destroy(pipe, so[t]);
    }
  }
  /* the batch holds what the draw read; drop the stage bindings */
  unbind_stage(pipe, MESA_SHADER_VERTEX, vs_views, vs_view_count);
  unbind_stage(pipe, MESA_SHADER_FRAGMENT, ps_views, ps_view_count);
  release_samplers(pipe, vs_samplers, vs_sampler_count, ps_samplers, ps_sampler_count);
  release_optional_stages(pipe, stages, false);
  ++owner->PendingDraws;
  owner->TimeBind += bound - entered;
  owner->TimeDrawVbo += drawn - bound;
  owner->TimeUnbind += qpc_now() - drawn;
  ++owner->TimedDraws;
  return S_OK;
}

extern "C" HRESULT AgxD3d12OwnerSubmitAndWait(AGX_D3D12_OWNER *owner) {
  OwnerLock lock(owner);
  if (!owner || !owner->Context) return E_INVALIDARG;
  if (owner->PendingDraws) {
    /* drop the draw bindings so they never outlive the heaps */
    struct pipe_framebuffer_state empty = {};
    owner->Context->set_framebuffer_state(owner->Context, &empty);
    owner->DrawFramebufferValid = false;
    owner->Context->set_vertex_buffers(owner->Context, 0, NULL);
    owner->PendingDraws = 0;
  }
  const UINT copies = owner->PendingCopies | (owner->PendingDispatches << 16);
  owner->PendingCopies = owner->PendingDispatches = 0;
  const LONGLONG flush_start = qpc_now();
  owner->Context->flush(owner->Context, NULL, 0);
  const LONGLONG flushed = qpc_now();
  const int retired = AgxWin32AsahiContextRetire(owner->Context, 5000u);
  const UINT times[] = {owner->TimedDraws, qpc_us(owner->TimeBind), qpc_us(owner->TimeDrawVbo), qpc_us(owner->TimeUnbind),
                        qpc_us(flushed - flush_start), qpc_us(qpc_now() - flushed)};
  AdmissionUmdDiagnostic("measure-d3d12-owner-time", S_OK, times, 6u);
  const unsigned long long fence = AgxWin32AsahiContextNewestFence(owner->Context);
  const UINT fences[] = {static_cast<UINT>(fence - owner->ReportedFence), static_cast<UINT>(fence)};
  AdmissionUmdDiagnostic("measure-d3d12-owner-fences", S_OK, fences, 2u);
  owner->ReportedFence = fence;
  owner->TimeBind = owner->TimeDrawVbo = owner->TimeUnbind = 0;
  owner->TimedDraws = 0;
  const int faulted = AgxWin32AsahiContextFaulted(owner->Context);
  const HRESULT hr = retired && !faulted ? S_OK : DXGI_ERROR_DEVICE_HUNG;
  receipt("measure-d3d12-owner-submit", hr, copies, static_cast<UINT>(retired), static_cast<UINT>(faulted));
  return hr;
}
