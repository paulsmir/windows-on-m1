/*
 * D3D-H2a: the D3D11 DDI device function table over the D3D10 frontend
 * (investigation/analysis/D3D-H2-d3d11-ddi.md).
 *
 * The 105 entries whose types the D3D11 table shares with D3D10 use the
 * frontend's 10.0 implementations; the two 10.1-typed blend entries use
 * its 10.1 ones; the D3D11-typed entries with a 10.x meaning are adapters
 * onto the same implementations, refusing only argument fields whose
 * behaviour is not implemented.  D3D-H2b: compute on 10.x hardware (cs_4_x
 * through the DXBC -> NIR translator, raw and structured buffer SRVs and
 * UAVs, Dispatch, buffer UAV clears), reported only under its switch.
 * Typed UAVs, append/consume counters, indirect arguments, tessellation and
 * pixel-stage UAVs fail closed (the capabilities report FL 10_0, so the
 * runtime does not create them; unbinding them with NULL is accepted).
 * Deferred contexts are left NULL: D3D11DDICAPS_THREADING reports no
 * command lists, so the runtime emulates them.
 *
 * SPDX-License-Identifier: MIT
 */
#include "gallium/frontends/d3d10umd/Draw.h"
#include "gallium/frontends/d3d10umd/Device.h"
#include "gallium/frontends/d3d10umd/InputAssembly.h"
#include "gallium/frontends/d3d10umd/OutputMerger.h"
#include "gallium/frontends/d3d10umd/Query.h"
#include "gallium/frontends/d3d10umd/Rasterizer.h"
#include "gallium/frontends/d3d10umd/Resource.h"
#include "gallium/frontends/d3d10umd/Shader.h"
#include "gallium/frontends/d3d10umd/State.h"

#include <stddef.h>

#include "util/bitscan.h"
#include "util/u_inlines.h"

#include "agx_d3d11_ddi.h"
#include "dxbc_to_nir.h"

extern "C" VOID AgxD3d10WindowsDiagnostic(PCSTR Stage, HRESULT Status,
                                          const UINT *Values, UINT Count);
extern "C" BOOL AgxD3d10WindowsDiagnosticSwitch(const char *Name);

BOOL
AgxD3d11DdiEnabled(void)
{
   return AgxD3d10WindowsDiagnosticSwitch("APPLE_AGX_D3D11_DDI");
}

BOOL
AgxD3d11ComputeEnabled(void)
{
   return AgxD3d11DdiEnabled() && AgxD3d10WindowsDiagnosticSwitch("APPLE_AGX_D3D11_COMPUTE");
}

/* The D3D11 binding and misc flags without a behaviour here: unordered
 * access is implemented for buffers only (raw and structured views), raw and
 * structured layouts for buffers only. */
#define AGX_D3D11_UNSUPPORTED_BIND (~(UINT)(D3D10_DDI_BIND_MASK | D3D11_DDI_BIND_UNORDERED_ACCESS))
#define AGX_D3D11_UNSUPPORTED_MISC \
   (D3D11_DDI_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_DDI_RESOURCE_MISC_RESOURCE_CLAMP)
#define AGX_D3D11_BUFFER_MISC \
   (D3D11_DDI_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS | D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED)
/* D3D11_REQ_MULTI_ELEMENT_STRUCTURE_SIZE_IN_BYTES */
#define AGX_D3D11_MAX_STRUCTURE_STRIDE 2048u
/* D3D11_PS_CS_UAV_REGISTER_COUNT (D3D11.0 DDI); the translator's u# range */
#define AGX_D3D11_UAV_SLOTS DXBC_NIR_MAX_UAVS

static void
refuse(D3D10DDI_HDEVICE hDevice, const char *what, UINT a, UINT b)
{
   UINT values[2] = {a, b};
   AgxD3d10WindowsDiagnostic(what, E_NOTIMPL, values, 2u);
   SetError(hDevice, E_NOTIMPL);
}

/* ------------------------------------------------------------------ */
/* Resources and views                                                */

static bool
resource_supported(const D3D11DDIARG_CREATERESOURCE *args)
{
   if (!args || (args->BindFlags & AGX_D3D11_UNSUPPORTED_BIND) ||
       (args->MiscFlags & AGX_D3D11_UNSUPPORTED_MISC))
      return false;
   bool buffer = args->ResourceDimension == D3D10DDIRESOURCE_BUFFER;
   UINT layout = args->MiscFlags & AGX_D3D11_BUFFER_MISC;
   if (!buffer)
      return !(args->BindFlags & D3D11_DDI_BIND_UNORDERED_ACCESS) && !layout && !args->ByteStride;
   if (layout == AGX_D3D11_BUFFER_MISC)
      return false;
   if (layout == D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED)
      return args->ByteStride && !(args->ByteStride & 3u) &&
             args->ByteStride <= AGX_D3D11_MAX_STRUCTURE_STRIDE;
   return !args->ByteStride;
}

/* D3D11DDIARG_CREATERESOURCE begins with every D3D10DDIARG_CREATERESOURCE
 * field in the same order; the D3D11 fields follow. */
static_assert(offsetof(D3D11DDIARG_CREATERESOURCE, pPrimaryDesc) ==
              offsetof(D3D10DDIARG_CREATERESOURCE, pPrimaryDesc),
              "D3D11 resource arguments extend the D3D10 ones");

static SIZE_T APIENTRY
CalcPrivateResourceSize11(D3D10DDI_HDEVICE hDevice, const D3D11DDIARG_CREATERESOURCE *args)
{
   return CalcPrivateResourceSize(hDevice, (const D3D10DDIARG_CREATERESOURCE *)args);
}

static void APIENTRY
CreateResource11(D3D10DDI_HDEVICE hDevice, const D3D11DDIARG_CREATERESOURCE *args,
                 D3D10DDI_HRESOURCE hResource, D3D10DDI_HRTRESOURCE hRTResource)
{
   if (!resource_supported(args)) {
      refuse(hDevice, "reject-d3d11-create-resource", args ? args->BindFlags : ~0u,
             args ? args->MiscFlags : ~0u);
      return;
   }
   CreateResource(hDevice, (const D3D10DDIARG_CREATERESOURCE *)args, hResource, hRTResource);
   /* D3D-H2b: the layout raw and structured views read */
   Resource *resource = CastResource(hResource);
   if (resource && resource->resource) {
      resource->agx_misc_flags = args->MiscFlags & AGX_D3D11_BUFFER_MISC;
      resource->agx_structure_stride =
         resource->agx_misc_flags == D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED ? args->ByteStride : 0;
   }
}

/* D3D-H2b: a raw or structured view of a buffer as 32-bit words: a raw view
 * counts 32-bit elements (format R32_TYPELESS, a buffer allowing raw views),
 * a structured view counts structures of the resource's stride (format
 * UNKNOWN, a structured buffer).  Returns the element size, 0 otherwise. */
static UINT
buffer_view_stride(const Resource *resource, DXGI_FORMAT format, bool raw)
{
   if (!resource || !resource->buffer)
      return 0;
   if (raw)
      return format == DXGI_FORMAT_R32_TYPELESS &&
             resource->agx_misc_flags == D3D11_DDI_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS ? 4u : 0u;
   return format == DXGI_FORMAT_UNKNOWN &&
          resource->agx_misc_flags == D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED ?
          resource->agx_structure_stride : 0u;
}

/* An SRV the 10.0 implementation describes: no cube array.  D3D-H2b: raw
 * and structured buffer views become R32_UINT buffer views of the same
 * bytes, which the translator reads one 32-bit word per texel. */
static bool
srv_10(const D3D11DDIARG_CREATESHADERRESOURCEVIEW *args, D3D10DDIARG_CREATESHADERRESOURCEVIEW *out)
{
   memset(out, 0, sizeof(*out));
   out->hDrvResource = args->hDrvResource;
   out->Format = args->Format;
   out->ResourceDimension = args->ResourceDimension;
   switch (args->ResourceDimension) {
   case D3D10DDIRESOURCE_BUFFER:
   case D3D11DDIRESOURCE_BUFFEREX: {
      bool ex = args->ResourceDimension == D3D11DDIRESOURCE_BUFFEREX;
      UINT flags = ex ? args->BufferEx.Flags : 0;
      UINT first = ex ? args->BufferEx.FirstElement : args->Buffer.FirstElement;
      UINT count = ex ? args->BufferEx.NumElements : args->Buffer.NumElements;
      out->ResourceDimension = D3D10DDIRESOURCE_BUFFER;
      if (flags & ~(UINT)D3D11_DDI_BUFFEREX_SRV_FLAG_RAW)
         return false;
      if (flags || args->Format == DXGI_FORMAT_UNKNOWN) {
         UINT stride = buffer_view_stride(CastResource(args->hDrvResource), args->Format, flags != 0);
         if (!stride || (UINT64)first * stride / 4u > UINT_MAX || (UINT64)count * stride / 4u > UINT_MAX)
            return false;
         out->Format = DXGI_FORMAT_R32_UINT;
         first = (UINT)((UINT64)first * stride / 4u);
         count = (UINT)((UINT64)count * stride / 4u);
      }
      out->Buffer.FirstElement = first;
      out->Buffer.NumElements = count;
      return true;
   }
   case D3D10DDIRESOURCE_TEXTURE1D: out->Tex1D = args->Tex1D; return true;
   case D3D10DDIRESOURCE_TEXTURE2D: out->Tex2D = args->Tex2D; return true;
   case D3D10DDIRESOURCE_TEXTURE3D: out->Tex3D = args->Tex3D; return true;
   case D3D10DDIRESOURCE_TEXTURECUBE:
      if (args->TexCube.First2DArrayFace || args->TexCube.NumCubes != 1)
         return false;
      out->TexCube.MostDetailedMip = args->TexCube.MostDetailedMip;
      out->TexCube.MipLevels = args->TexCube.MipLevels;
      return true;
   default:
      return false;
   }
}

static SIZE_T APIENTRY
CalcPrivateShaderResourceViewSize11(D3D10DDI_HDEVICE hDevice,
                                    const D3D11DDIARG_CREATESHADERRESOURCEVIEW *args)
{
   D3D10DDIARG_CREATESHADERRESOURCEVIEW view;
   if (!args || !srv_10(args, &view))
      return sizeof(ShaderResourceView);
   return CalcPrivateShaderResourceViewSize(hDevice, &view);
}

static void APIENTRY
CreateShaderResourceView11(D3D10DDI_HDEVICE hDevice,
                           const D3D11DDIARG_CREATESHADERRESOURCEVIEW *args,
                           D3D10DDI_HSHADERRESOURCEVIEW hView,
                           D3D10DDI_HRTSHADERRESOURCEVIEW hRTView)
{
   D3D10DDIARG_CREATESHADERRESOURCEVIEW view;
   if (!args || !srv_10(args, &view)) {
      refuse(hDevice, "reject-d3d11-create-srv", args ? (UINT)args->ResourceDimension : ~0u,
             args ? (UINT)args->Format : ~0u);
      return;
   }
   CreateShaderResourceView(hDevice, &view, hView, hRTView);
}

/* Read-only depth/stencil flags only promise that the view is not written
 * through; the 10.0 view honours the depth-stencil state that writes. */
static void
dsv_10(const D3D11DDIARG_CREATEDEPTHSTENCILVIEW *args, D3D10DDIARG_CREATEDEPTHSTENCILVIEW *out)
{
   memset(out, 0, sizeof(*out));
   out->hDrvResource = args->hDrvResource;
   out->Format = args->Format;
   out->ResourceDimension = args->ResourceDimension;
   switch (args->ResourceDimension) {
   case D3D10DDIRESOURCE_TEXTURE1D: out->Tex1D = args->Tex1D; break;
   case D3D10DDIRESOURCE_TEXTURECUBE: out->TexCube = args->TexCube; break;
   default: out->Tex2D = args->Tex2D; break;
   }
}

static SIZE_T APIENTRY
CalcPrivateDepthStencilViewSize11(D3D10DDI_HDEVICE hDevice,
                                  const D3D11DDIARG_CREATEDEPTHSTENCILVIEW *args)
{
   D3D10DDIARG_CREATEDEPTHSTENCILVIEW view;
   if (!args)
      return sizeof(DepthStencilView);
   dsv_10(args, &view);
   return CalcPrivateDepthStencilViewSize(hDevice, &view);
}

static void APIENTRY
CreateDepthStencilView11(D3D10DDI_HDEVICE hDevice, const D3D11DDIARG_CREATEDEPTHSTENCILVIEW *args,
                         D3D10DDI_HDEPTHSTENCILVIEW hView, D3D10DDI_HRTDEPTHSTENCILVIEW hRTView)
{
   D3D10DDIARG_CREATEDEPTHSTENCILVIEW view;
   if (!args) {
      SetError(hDevice, E_INVALIDARG);
      return;
   }
   dsv_10(args, &view);
   CreateDepthStencilView(hDevice, &view, hView, hRTView);
}

static void APIENTRY
SetRenderTargets11(D3D10DDI_HDEVICE hDevice, const D3D10DDI_HRENDERTARGETVIEW *phRTVs,
                   UINT NumRTVs, UINT ClearSlots, D3D10DDI_HDEPTHSTENCILVIEW hDSV,
                   const D3D11DDI_HUNORDEREDACCESSVIEW *phUAVs, const UINT *pInitialCounts,
                   UINT UAVStartSlot, UINT NumUAVs, UINT UAVRangeStart, UINT UAVRangeSize)
{
   (void)pInitialCounts; (void)UAVStartSlot; (void)UAVRangeStart; (void)UAVRangeSize;
   for (UINT i = 0; phUAVs && i < NumUAVs; ++i) {
      if (phUAVs[i].pDrvPrivate) {
         refuse(hDevice, "reject-d3d11-pixel-uav", i, NumUAVs);
         return;
      }
   }
   SetRenderTargets(hDevice, phRTVs, NumRTVs, ClearSlots, hDSV);
}

/* ------------------------------------------------------------------ */
/* Stream output                                                      */

#define AGX_D3D11_MAX_SO_ENTRIES 64

/* One stream, rasterized; per-slot strides equal to what the 10.0 path
 * derives (packed components), or a single slot with the given stride. */
static bool
so_10(const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *args,
      D3D10DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY *entries,
      D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *out)
{
   UINT packed[4] = {0, 0, 0, 0};
   if (!args || args->NumEntries > AGX_D3D11_MAX_SO_ENTRIES || args->RasterizedStream != 0)
      return false;
   for (UINT i = 0; i < args->NumEntries; ++i) {
      const D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY *e = &args->pOutputStreamDecl[i];
      if (e->Stream != 0 || e->OutputSlot >= 4)
         return false;
      entries[i].OutputSlot = e->OutputSlot;
      entries[i].RegisterIndex = e->RegisterIndex;
      entries[i].RegisterMask = e->RegisterMask;
      packed[e->OutputSlot] += 4u * (UINT)util_bitcount(e->RegisterMask & 0xfu);
   }
   memset(out, 0, sizeof(*out));
   out->pShaderCode = args->pShaderCode;
   out->pOutputStreamDecl = entries;
   out->NumEntries = args->NumEntries;
   if (args->NumStrides == 1 && args->BufferStridesInBytes) {
      out->StreamOutputStrideInBytes = args->BufferStridesInBytes[0];
      return true;
   }
   for (UINT s = 0; s < args->NumStrides && s < 4; ++s)
      if (args->BufferStridesInBytes && args->BufferStridesInBytes[s] != packed[s])
         return false;
   out->StreamOutputStrideInBytes = 0;
   return true;
}

static SIZE_T APIENTRY
CalcPrivateGeometryShaderWithStreamOutput11(
   D3D10DDI_HDEVICE hDevice, const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *args,
   const D3D10DDIARG_STAGE_IO_SIGNATURES *pSignatures)
{
   D3D10DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY entries[AGX_D3D11_MAX_SO_ENTRIES];
   D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT gs;
   if (!so_10(args, entries, &gs))
      return sizeof(Shader);
   return CalcPrivateGeometryShaderWithStreamOutput(hDevice, &gs, pSignatures);
}

static void APIENTRY
CreateGeometryShaderWithStreamOutput11(
   D3D10DDI_HDEVICE hDevice, const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *args,
   D3D10DDI_HSHADER hShader, D3D10DDI_HRTSHADER hRTShader,
   const D3D10DDIARG_STAGE_IO_SIGNATURES *pSignatures)
{
   D3D10DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY entries[AGX_D3D11_MAX_SO_ENTRIES];
   D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT gs;
   if (!so_10(args, entries, &gs)) {
      refuse(hDevice, "reject-d3d11-stream-output", args ? args->RasterizedStream : ~0u,
             args ? args->NumStrides : ~0u);
      return;
   }
   CreateGeometryShaderWithStreamOutput(hDevice, &gs, hShader, hRTShader, pSignatures);
}

/* ------------------------------------------------------------------ */
/* D3D-H2b: compute (cs_4_x) and buffer unordered-access views        */

/* A raw or structured buffer view bound as shader buffer u# (dxbc_to_nir.h
 * binding conventions).  Typed views and the append/consume counter flags
 * (SM 5) are refused until implemented. */
struct AgxUnorderedAccessView {
   Device *owner_device;
   Resource *owner_resource;
   struct pipe_shader_buffer buffer;
};

static inline AgxUnorderedAccessView *
CastUnorderedAccessView(D3D11DDI_HUNORDEREDACCESSVIEW hView)
{
   return static_cast<AgxUnorderedAccessView *>(hView.pDrvPrivate);
}

static SIZE_T APIENTRY
CalcPrivateUnorderedAccessViewSize11(D3D10DDI_HDEVICE, const D3D11DDIARG_CREATEUNORDEREDACCESSVIEW *)
{
   return sizeof(AgxUnorderedAccessView);
}

static void APIENTRY
CreateUnorderedAccessView11(D3D10DDI_HDEVICE hDevice, const D3D11DDIARG_CREATEUNORDEREDACCESSVIEW *args,
                            D3D11DDI_HUNORDEREDACCESSVIEW hView, D3D11DDI_HRTUNORDEREDACCESSVIEW)
{
   Device *device = CastDevice(hDevice);
   AgxUnorderedAccessView *view = CastUnorderedAccessView(hView);
   memset(view, 0, sizeof(*view));
   Resource *resource = args ? CastResource(args->hDrvResource) : NULL;
   bool buffer = args && (args->ResourceDimension == D3D10DDIRESOURCE_BUFFER ||
                          args->ResourceDimension == D3D11DDIRESOURCE_BUFFEREX);
   UINT flags = buffer ? args->Buffer.Flags : ~0u;
   UINT stride = buffer && !(flags & ~(UINT)D3D11_DDI_BUFFER_UAV_FLAG_RAW) ?
      buffer_view_stride(resource, args->Format, flags != 0) : 0u;
   UINT64 offset = stride ? (UINT64)args->Buffer.FirstElement * stride : 0;
   UINT64 bytes = stride ? (UINT64)args->Buffer.NumElements * stride : 0;
   if (!device || !stride || resource->owner_device != device || !resource->resource ||
       !(resource->bind_flags & D3D11_DDI_BIND_UNORDERED_ACCESS) || !bytes ||
       offset > resource->logical_bytes || bytes > resource->logical_bytes - offset) {
      refuse(hDevice, "reject-d3d11-uav", args ? (UINT)args->ResourceDimension : ~0u,
             args ? (UINT)args->Format : ~0u);
      return;
   }
   view->owner_device = device;
   view->owner_resource = resource;
   pipe_resource_reference(&view->buffer.buffer, resource->resource);
   view->buffer.buffer_offset = (unsigned)offset;
   view->buffer.buffer_size = (unsigned)bytes;
}

static void APIENTRY
DestroyUnorderedAccessView11(D3D10DDI_HDEVICE, D3D11DDI_HUNORDEREDACCESSVIEW hView)
{
   AgxUnorderedAccessView *view = CastUnorderedAccessView(hView);
   if (!view)
      return;
   pipe_resource_reference(&view->buffer.buffer, NULL);
   view->owner_device = NULL;
   view->owner_resource = NULL;
}

/* The views of slots [start, start + n); the initial counts apply to
 * counter views only, which are not created. */
static void APIENTRY
CsSetUnorderedAccessViews11(D3D10DDI_HDEVICE hDevice, UINT start, UINT n,
                            const D3D11DDI_HUNORDEREDACCESSVIEW *views, const UINT *initial_counts)
{
   (void)initial_counts;
   Device *device = CastDevice(hDevice);
   struct pipe_shader_buffer buffers[AGX_D3D11_UAV_SLOTS] = {};
   bool valid = device && start <= AGX_D3D11_UAV_SLOTS && n <= AGX_D3D11_UAV_SLOTS - start &&
                (!n || views);
   for (UINT i = 0; valid && i < n; ++i) {
      AgxUnorderedAccessView *view = CastUnorderedAccessView(views[i]);
      if (!view)
         continue;
      valid = view->owner_device == device && view->buffer.buffer;
      if (valid)
         buffers[i] = view->buffer;
   }
   if (!valid) {
      refuse(hDevice, "reject-d3d11-cs-uav", start, n);
      return;
   }
   if (n)
      device->pipe->set_shader_buffers(device->pipe, MESA_SHADER_COMPUTE, start, n, buffers,
                                       BITFIELD_MASK(n));
}

/* Raw and structured views: every 32-bit word takes values[0]. */
static void APIENTRY
ClearUnorderedAccessViewUint11(D3D10DDI_HDEVICE hDevice, D3D11DDI_HUNORDEREDACCESSVIEW hView,
                               const UINT values[4])
{
   Device *device = CastDevice(hDevice);
   AgxUnorderedAccessView *view = CastUnorderedAccessView(hView);
   if (!device || !view || view->owner_device != device || !view->buffer.buffer || !values) {
      refuse(hDevice, "reject-d3d11-clear-uav", 0, 0);
      return;
   }
   if (!CheckPredicate(device))
      return;
   device->pipe->clear_buffer(device->pipe, view->buffer.buffer, view->buffer.buffer_offset,
                              view->buffer.buffer_size, &values[0], 4);
}

/* Float clears exist for typed views only. */
static void APIENTRY
ClearUnorderedAccessViewFloat11(D3D10DDI_HDEVICE hDevice, D3D11DDI_HUNORDEREDACCESSVIEW, const FLOAT[4])
{
   refuse(hDevice, "reject-d3d11-clear-uav", 1, 0);
}

static void APIENTRY
CreateComputeShader11(D3D10DDI_HDEVICE hDevice, const UINT *code, D3D10DDI_HSHADER hShader,
                      D3D10DDI_HRTSHADER)
{
   Device *device = CastDevice(hDevice);
   Shader *shader = CastShader(hShader);
   memset(shader, 0, sizeof(*shader));
   shader->type = MESA_SHADER_COMPUTE;
   if (!device || !code) {
      SetError(hDevice, E_INVALIDARG);
      return;
   }
   struct pipe_context *pipe = device->pipe;
   struct dxbc_nir_error error = {};
   struct nir_shader *nir = dxbc_translate_tokens(code, pipe->screen->nir_options[MESA_SHADER_COMPUTE],
                                                  &error);
   UINT values[4] = {(UINT)MESA_SHADER_COMPUTE, nir ? 1u : 0u, error.opcode, error.offset};
   AgxD3d10WindowsDiagnostic("measure-shader-nir", nir ? S_OK : E_NOTIMPL, values, 4u);
   if (!nir) {
      SetError(hDevice, E_NOTIMPL);
      return;
   }
   unsigned block[3], shared = 0;
   dxbc_nir_compute_info(nir, block, &shared);
   struct pipe_compute_state state = {};
   state.ir_type = PIPE_SHADER_IR_NIR;
   state.prog = nir;
   state.static_shared_mem = shared;
   shader->handle = pipe->create_compute_state(pipe, &state);
   if (!shader->handle) {
      SetError(hDevice, E_OUTOFMEMORY);
      return;
   }
   for (unsigned i = 0; i < 3; ++i)
      shader->agx_block[i] = block[i];
}

static void APIENTRY
CsSetShader11(D3D10DDI_HDEVICE hDevice, D3D10DDI_HSHADER hShader)
{
   Device *device = CastDevice(hDevice);
   Shader *shader = CastShader(hShader);
   if (!device || (shader && (shader->type != MESA_SHADER_COMPUTE || !shader->handle))) {
      refuse(hDevice, "reject-d3d11-cs-shader", shader ? shader->type : 0u, 0);
      return;
   }
   device->pipe->bind_compute_state(device->pipe, shader ? shader->handle : NULL);
   for (unsigned i = 0; i < 3; ++i)
      device->agx_cs_block[i] = shader ? shader->agx_block[i] : 0u;
}

/* D3D11 orders a Dispatch's unordered-access writes before every later
 * command (no API barrier exists): the barrier ends the compute batch. */
static void APIENTRY
Dispatch11(D3D10DDI_HDEVICE hDevice, UINT x, UINT y, UINT z)
{
   Device *device = CastDevice(hDevice);
   if (!device || !CheckPredicate(device) || !x || !y || !z)
      return;
   if (!device->agx_cs_block[0]) {
      UINT values[3] = {x, y, z};
      AgxD3d10WindowsDiagnostic("dispatch-without-shader", S_OK, values, 3u);
      return;
   }
   struct pipe_grid_info info = {};
   info.work_dim = 3;
   for (unsigned i = 0; i < 3; ++i)
      info.block[i] = device->agx_cs_block[i];
   info.grid[0] = x;
   info.grid[1] = y;
   info.grid[2] = z;
   device->pipe->launch_grid(device->pipe, &info);
   device->pipe->memory_barrier(device->pipe, PIPE_BARRIER_ALL);
}

/* ------------------------------------------------------------------ */
/* D3D-H4: hull and domain shaders (reachable once FL 11_0 is reported)  */

/* The control point registers of a hull shader's output signature: what a
 * pass-through control point phase copies, and which hold SV_Position. */
static void
hs_passthrough(const D3D11DDIARG_TESSELLATION_IO_SIGNATURES *sig, uint32_t *registers,
               uint32_t *positions)
{
   *registers = *positions = 0;
   for (UINT i = 0; sig && sig->pOutputSignature && i < sig->NumOutputSignatureEntries; ++i) {
      UINT reg = sig->pOutputSignature[i].Register;
      if (reg >= 32)
         continue;
      *registers |= 1u << reg;
      if (sig->pOutputSignature[i].SystemValue == D3D10_SB_NAME_POSITION)
         *positions |= 1u << reg;
   }
}

static void
create_tessellation_shader(D3D10DDI_HDEVICE hDevice, const UINT *code, D3D10DDI_HSHADER hShader,
                           const D3D11DDIARG_TESSELLATION_IO_SIGNATURES *sig, mesa_shader_stage stage)
{
   Device *device = CastDevice(hDevice);
   Shader *shader = CastShader(hShader);
   uint32_t registers = 0, positions = 0;
   memset(shader, 0, sizeof(*shader));
   shader->type = stage;
   if (!device || !code) {
      SetError(hDevice, E_INVALIDARG);
      return;
   }
   struct pipe_context *pipe = device->pipe;
   struct dxbc_nir_error error = {};
   if (stage == MESA_SHADER_TESS_CTRL)
      hs_passthrough(sig, &registers, &positions);
   struct nir_shader *nir = dxbc_translate_tokens_hs(code, pipe->screen->nir_options[stage],
                                                     registers, positions, &error);
   UINT values[4] = {(UINT)stage, nir ? 1u : 0u, error.opcode, error.offset};
   AgxD3d10WindowsDiagnostic("measure-shader-nir", nir ? S_OK : E_NOTIMPL, values, 4u);
   if (!nir) {
      SetError(hDevice, E_NOTIMPL);
      return;
   }
   struct pipe_shader_state state = {};
   state.type = PIPE_SHADER_IR_NIR;
   state.ir.nir = nir;
   shader->handle = stage == MESA_SHADER_TESS_CTRL ? pipe->create_tcs_state(pipe, &state) :
                                                     pipe->create_tes_state(pipe, &state);
   if (!shader->handle)
      SetError(hDevice, E_OUTOFMEMORY);
}

static void APIENTRY
CreateHullShader11(D3D10DDI_HDEVICE hDevice, const UINT *code, D3D10DDI_HSHADER hShader,
                   D3D10DDI_HRTSHADER, const D3D11DDIARG_TESSELLATION_IO_SIGNATURES *sig)
{
   create_tessellation_shader(hDevice, code, hShader, sig, MESA_SHADER_TESS_CTRL);
}

static void APIENTRY
CreateDomainShader11(D3D10DDI_HDEVICE hDevice, const UINT *code, D3D10DDI_HSHADER hShader,
                     D3D10DDI_HRTSHADER, const D3D11DDIARG_TESSELLATION_IO_SIGNATURES *sig)
{
   create_tessellation_shader(hDevice, code, hShader, sig, MESA_SHADER_TESS_EVAL);
}

static void
bind_tessellation_shader(D3D10DDI_HDEVICE hDevice, D3D10DDI_HSHADER hShader, mesa_shader_stage stage)
{
   Device *device = CastDevice(hDevice);
   Shader *shader = CastShader(hShader);
   if (!device || (shader && (shader->type != stage || !shader->handle))) {
      refuse(hDevice, "reject-d3d11-tess-shader", shader ? shader->type : 0u, stage);
      return;
   }
   if (stage == MESA_SHADER_TESS_CTRL)
      device->pipe->bind_tcs_state(device->pipe, shader ? shader->handle : NULL);
   else
      device->pipe->bind_tes_state(device->pipe, shader ? shader->handle : NULL);
}

static void APIENTRY
HsSetShader11(D3D10DDI_HDEVICE hDevice, D3D10DDI_HSHADER hShader)
{
   bind_tessellation_shader(hDevice, hShader, MESA_SHADER_TESS_CTRL);
}

static void APIENTRY
DsSetShader11(D3D10DDI_HDEVICE hDevice, D3D10DDI_HSHADER hShader)
{
   bind_tessellation_shader(hDevice, hShader, MESA_SHADER_TESS_EVAL);
}

/* D3D11 adds 1..32 control-point patch lists (33..64) to the 10.0
 * topologies, which the 10.0 entry keeps. */
static PFND3D10DDI_IA_SETTOPOLOGY ia_set_topology_10;

static void APIENTRY
IaSetTopology11(D3D10DDI_HDEVICE hDevice, D3D10_DDI_PRIMITIVE_TOPOLOGY topology)
{
   Device *device = CastDevice(hDevice);
   if (device && topology >= D3D11_DDI_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST &&
       topology <= D3D11_DDI_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST) {
      device->primitive = MESA_PRIM_PATCHES;
      device->pipe->set_patch_vertices(device->pipe,
         (uint8_t)(topology - D3D11_DDI_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1));
      return;
   }
   ia_set_topology_10(hDevice, topology);
}

/* ------------------------------------------------------------------ */
/* Stages and features not provided yet: fail closed, accept unbinds  */

static void APIENTRY
RelocateDeviceFuncs11(D3D10DDI_HDEVICE hDevice, struct D3D11DDI_DEVICEFUNCS *pDeviceFunctions)
{
   (void)hDevice; (void)pDeviceFunctions; /* the table holds no device data */
}

static void APIENTRY
SetShaderNone(D3D10DDI_HDEVICE hDevice, D3D10DDI_HSHADER hShader)
{
   if (hShader.pDrvPrivate)
      refuse(hDevice, "reject-d3d11-stage-shader", 0, 0);
}

static void APIENTRY
SetShaderWithIfacesNone(D3D10DDI_HDEVICE hDevice, D3D10DDI_HSHADER hShader, UINT n,
                        const UINT *, const D3D11DDIARG_POINTERDATA *)
{
   if (hShader.pDrvPrivate || n)
      refuse(hDevice, "reject-d3d11-stage-shader", n, 1);
}

#define AGX_D3D11_WITH_IFACES(name, plain)                                         \
   static void APIENTRY name(D3D10DDI_HDEVICE hDevice, D3D10DDI_HSHADER hShader,   \
                             UINT n, const UINT *ids, const D3D11DDIARG_POINTERDATA *data) \
   {                                                                               \
      (void)ids; (void)data;                                                       \
      if (n) {                                                                     \
         refuse(hDevice, "reject-d3d11-class-instances", n, 0);                    \
         return;                                                                   \
      }                                                                            \
      plain(hDevice, hShader);                                                     \
   }
AGX_D3D11_WITH_IFACES(VsSetShaderWithIfaces11, VsSetShader)
AGX_D3D11_WITH_IFACES(GsSetShaderWithIfaces11, GsSetShader)
AGX_D3D11_WITH_IFACES(PsSetShaderWithIfaces11, PsSetShader)
AGX_D3D11_WITH_IFACES(CsSetShaderWithIfaces11, CsSetShader11)
AGX_D3D11_WITH_IFACES(HsSetShaderWithIfaces11, HsSetShader11)
AGX_D3D11_WITH_IFACES(DsSetShaderWithIfaces11, DsSetShader11)

static void APIENTRY
SetShaderResourcesNone(D3D10DDI_HDEVICE hDevice, UINT, UINT n, const D3D10DDI_HSHADERRESOURCEVIEW *views)
{
   for (UINT i = 0; views && i < n; ++i)
      if (views[i].pDrvPrivate) {
         refuse(hDevice, "reject-d3d11-stage-srv", i, n);
         return;
      }
}

static void APIENTRY
SetSamplersNone(D3D10DDI_HDEVICE hDevice, UINT, UINT n, const D3D10DDI_HSAMPLER *samplers)
{
   for (UINT i = 0; samplers && i < n; ++i)
      if (samplers[i].pDrvPrivate) {
         refuse(hDevice, "reject-d3d11-stage-sampler", i, n);
         return;
      }
}

static void APIENTRY
SetConstantBuffersNone(D3D10DDI_HDEVICE hDevice, UINT, UINT n, const D3D10DDI_HRESOURCE *buffers)
{
   for (UINT i = 0; buffers && i < n; ++i)
      if (buffers[i].pDrvPrivate) {
         refuse(hDevice, "reject-d3d11-stage-cb", i, n);
         return;
      }
}

static SIZE_T APIENTRY
CalcPrivateTessellationShaderSize11(D3D10DDI_HDEVICE, const UINT *, const D3D11DDIARG_TESSELLATION_IO_SIGNATURES *)
{
   return sizeof(Shader);
}

static void APIENTRY
DrawIndirectNone(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE, UINT)
{
   refuse(hDevice, "reject-d3d11-draw-indirect", 0, 0);
}

static void APIENTRY
DispatchIndirectNone(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE, UINT)
{
   refuse(hDevice, "reject-d3d11-dispatch", 1, 0);
}

static void APIENTRY
CopyStructureCountNone(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE, UINT, D3D11DDI_HUNORDEREDACCESSVIEW)
{
   refuse(hDevice, "reject-d3d11-copy-structure-count", 0, 0);
}

/* A minimum LOD clamp is a hint for residency management; sampling stays
 * correct without it (no tiled resources here). */
static void APIENTRY
SetResourceMinLOD11(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, FLOAT)
{
}

void
AgxD3d11FillDeviceFuncs(D3D11DDI_DEVICEFUNCS *f, const D3D10DDI_DEVICEFUNCS *base)
{
   memset(f, 0, sizeof(*f));
   /* D3D10-typed entries: exactly the 10.0 table CreateDevice filled */
   f->pfnDefaultConstantBufferUpdateSubresourceUP = base->pfnDefaultConstantBufferUpdateSubresourceUP;
   f->pfnVsSetConstantBuffers = base->pfnVsSetConstantBuffers;
   f->pfnPsSetShaderResources = base->pfnPsSetShaderResources;
   f->pfnPsSetShader = base->pfnPsSetShader;
   f->pfnPsSetSamplers = base->pfnPsSetSamplers;
   f->pfnVsSetShader = base->pfnVsSetShader;
   f->pfnDrawIndexed = base->pfnDrawIndexed;
   f->pfnDraw = base->pfnDraw;
   f->pfnDynamicIABufferMapNoOverwrite = base->pfnDynamicIABufferMapNoOverwrite;
   f->pfnDynamicIABufferUnmap = base->pfnDynamicIABufferUnmap;
   f->pfnDynamicConstantBufferMapDiscard = base->pfnDynamicConstantBufferMapDiscard;
   f->pfnDynamicIABufferMapDiscard = base->pfnDynamicIABufferMapDiscard;
   f->pfnDynamicConstantBufferUnmap = base->pfnDynamicConstantBufferUnmap;
   f->pfnPsSetConstantBuffers = base->pfnPsSetConstantBuffers;
   f->pfnIaSetInputLayout = base->pfnIaSetInputLayout;
   f->pfnIaSetVertexBuffers = base->pfnIaSetVertexBuffers;
   f->pfnIaSetIndexBuffer = base->pfnIaSetIndexBuffer;
   f->pfnDrawIndexedInstanced = base->pfnDrawIndexedInstanced;
   f->pfnDrawInstanced = base->pfnDrawInstanced;
   f->pfnDynamicResourceMapDiscard = base->pfnDynamicResourceMapDiscard;
   f->pfnDynamicResourceUnmap = base->pfnDynamicResourceUnmap;
   f->pfnGsSetConstantBuffers = base->pfnGsSetConstantBuffers;
   f->pfnGsSetShader = base->pfnGsSetShader;
   ia_set_topology_10 = base->pfnIaSetTopology;
   f->pfnIaSetTopology = IaSetTopology11;
   f->pfnStagingResourceMap = base->pfnStagingResourceMap;
   f->pfnStagingResourceUnmap = base->pfnStagingResourceUnmap;
   f->pfnVsSetShaderResources = base->pfnVsSetShaderResources;
   f->pfnVsSetSamplers = base->pfnVsSetSamplers;
   f->pfnGsSetShaderResources = base->pfnGsSetShaderResources;
   f->pfnGsSetSamplers = base->pfnGsSetSamplers;
   f->pfnShaderResourceViewReadAfterWriteHazard = base->pfnShaderResourceViewReadAfterWriteHazard;
   f->pfnResourceReadAfterWriteHazard = base->pfnResourceReadAfterWriteHazard;
   f->pfnSetBlendState = base->pfnSetBlendState;
   f->pfnSetDepthStencilState = base->pfnSetDepthStencilState;
   f->pfnSetRasterizerState = base->pfnSetRasterizerState;
   f->pfnQueryEnd = base->pfnQueryEnd;
   f->pfnQueryBegin = base->pfnQueryBegin;
   f->pfnResourceCopyRegion = base->pfnResourceCopyRegion;
   f->pfnResourceUpdateSubresourceUP = base->pfnResourceUpdateSubresourceUP;
   f->pfnSoSetTargets = base->pfnSoSetTargets;
   f->pfnDrawAuto = base->pfnDrawAuto;
   f->pfnSetViewports = base->pfnSetViewports;
   f->pfnSetScissorRects = base->pfnSetScissorRects;
   f->pfnClearRenderTargetView = base->pfnClearRenderTargetView;
   f->pfnClearDepthStencilView = base->pfnClearDepthStencilView;
   f->pfnSetPredication = base->pfnSetPredication;
   f->pfnQueryGetData = base->pfnQueryGetData;
   f->pfnFlush = base->pfnFlush;
   f->pfnGenMips = base->pfnGenMips;
   f->pfnResourceCopy = base->pfnResourceCopy;
   f->pfnResourceResolveSubresource = base->pfnResourceResolveSubresource;
   f->pfnResourceMap = base->pfnResourceMap;
   f->pfnResourceUnmap = base->pfnResourceUnmap;
   f->pfnResourceIsStagingBusy = base->pfnResourceIsStagingBusy;
   f->pfnCalcPrivateOpenedResourceSize = base->pfnCalcPrivateOpenedResourceSize;
   f->pfnOpenResource = base->pfnOpenResource;
   f->pfnDestroyResource = base->pfnDestroyResource;
   f->pfnDestroyShaderResourceView = base->pfnDestroyShaderResourceView;
   f->pfnCalcPrivateRenderTargetViewSize = base->pfnCalcPrivateRenderTargetViewSize;
   f->pfnCreateRenderTargetView = base->pfnCreateRenderTargetView;
   f->pfnDestroyRenderTargetView = base->pfnDestroyRenderTargetView;
   f->pfnDestroyDepthStencilView = base->pfnDestroyDepthStencilView;
   f->pfnCalcPrivateElementLayoutSize = base->pfnCalcPrivateElementLayoutSize;
   f->pfnCreateElementLayout = base->pfnCreateElementLayout;
   f->pfnDestroyElementLayout = base->pfnDestroyElementLayout;
   f->pfnDestroyBlendState = base->pfnDestroyBlendState;
   f->pfnCalcPrivateDepthStencilStateSize = base->pfnCalcPrivateDepthStencilStateSize;
   f->pfnCreateDepthStencilState = base->pfnCreateDepthStencilState;
   f->pfnDestroyDepthStencilState = base->pfnDestroyDepthStencilState;
   f->pfnCalcPrivateRasterizerStateSize = base->pfnCalcPrivateRasterizerStateSize;
   f->pfnCreateRasterizerState = base->pfnCreateRasterizerState;
   f->pfnDestroyRasterizerState = base->pfnDestroyRasterizerState;
   f->pfnCalcPrivateShaderSize = base->pfnCalcPrivateShaderSize;
   f->pfnCreateVertexShader = base->pfnCreateVertexShader;
   f->pfnCreateGeometryShader = base->pfnCreateGeometryShader;
   f->pfnCreatePixelShader = base->pfnCreatePixelShader;
   f->pfnDestroyShader = base->pfnDestroyShader;
   f->pfnCalcPrivateSamplerSize = base->pfnCalcPrivateSamplerSize;
   f->pfnCreateSampler = base->pfnCreateSampler;
   f->pfnDestroySampler = base->pfnDestroySampler;
   f->pfnCalcPrivateQuerySize = base->pfnCalcPrivateQuerySize;
   f->pfnCreateQuery = base->pfnCreateQuery;
   f->pfnDestroyQuery = base->pfnDestroyQuery;
   f->pfnCheckFormatSupport = base->pfnCheckFormatSupport;
   f->pfnCheckMultisampleQualityLevels = base->pfnCheckMultisampleQualityLevels;
   f->pfnCheckCounterInfo = base->pfnCheckCounterInfo;
   f->pfnCheckCounter = base->pfnCheckCounter;
   f->pfnDestroyDevice = base->pfnDestroyDevice;
   f->pfnSetTextFilterSize = base->pfnSetTextFilterSize;
   f->pfnResourceConvert = base->pfnResourceCopy;
   f->pfnResourceConvertRegion = base->pfnResourceCopyRegion;
   /* D3D10.1-typed blend entries */
   f->pfnCalcPrivateBlendStateSize = CalcPrivateBlendStateSize1;
   f->pfnCreateBlendState = CreateBlendState1;
   /* D3D11-typed adapters onto the 10.x behaviour */
   f->pfnSetRenderTargets = SetRenderTargets11;
   f->pfnRelocateDeviceFuncs = RelocateDeviceFuncs11;
   f->pfnCalcPrivateResourceSize = CalcPrivateResourceSize11;
   f->pfnCreateResource = CreateResource11;
   f->pfnCalcPrivateShaderResourceViewSize = CalcPrivateShaderResourceViewSize11;
   f->pfnCreateShaderResourceView = CreateShaderResourceView11;
   f->pfnCalcPrivateDepthStencilViewSize = CalcPrivateDepthStencilViewSize11;
   f->pfnCreateDepthStencilView = CreateDepthStencilView11;
   f->pfnCalcPrivateGeometryShaderWithStreamOutput = CalcPrivateGeometryShaderWithStreamOutput11;
   f->pfnCreateGeometryShaderWithStreamOutput = CreateGeometryShaderWithStreamOutput11;
   f->pfnVsSetShaderWithIfaces = VsSetShaderWithIfaces11;
   f->pfnGsSetShaderWithIfaces = GsSetShaderWithIfaces11;
   f->pfnPsSetShaderWithIfaces = PsSetShaderWithIfaces11;
   f->pfnSetResourceMinLOD = SetResourceMinLOD11;
   /* D3D-H2b: compute on 10.x hardware, buffer unordered-access views */
   f->pfnCreateComputeShader = CreateComputeShader11;
   f->pfnCsSetShader = CsSetShader11;
   f->pfnCsSetShaderWithIfaces = CsSetShaderWithIfaces11;
   f->pfnCsSetShaderResources = CsSetShaderResources;
   f->pfnCsSetSamplers = CsSetSamplers;
   f->pfnCsSetConstantBuffers = CsSetConstantBuffers;
   f->pfnCalcPrivateUnorderedAccessViewSize = CalcPrivateUnorderedAccessViewSize11;
   f->pfnCreateUnorderedAccessView = CreateUnorderedAccessView11;
   f->pfnDestroyUnorderedAccessView = DestroyUnorderedAccessView11;
   f->pfnCsSetUnorderedAccessViews = CsSetUnorderedAccessViews11;
   f->pfnClearUnorderedAccessViewUint = ClearUnorderedAccessViewUint11;
   f->pfnClearUnorderedAccessViewFloat = ClearUnorderedAccessViewFloat11;
   f->pfnDispatch = Dispatch11;
   /* D3D-H4: hull and domain shaders, patch topologies */
   f->pfnCreateHullShader = CreateHullShader11;
   f->pfnCreateDomainShader = CreateDomainShader11;
   f->pfnHsSetShader = HsSetShader11;
   f->pfnDsSetShader = DsSetShader11;
   f->pfnHsSetShaderWithIfaces = HsSetShaderWithIfaces11;
   f->pfnDsSetShaderWithIfaces = DsSetShaderWithIfaces11;
   f->pfnHsSetShaderResources = HsSetShaderResources;
   f->pfnHsSetSamplers = HsSetSamplers;
   f->pfnHsSetConstantBuffers = HsSetConstantBuffers;
   f->pfnDsSetShaderResources = DsSetShaderResources;
   f->pfnDsSetSamplers = DsSetSamplers;
   f->pfnDsSetConstantBuffers = DsSetConstantBuffers;
   /* not provided yet: fail closed, unbinding accepted */
   f->pfnCalcPrivateTessellationShaderSize = CalcPrivateTessellationShaderSize11;
   f->pfnDrawIndexedInstancedIndirect = DrawIndirectNone;
   f->pfnDrawInstancedIndirect = DrawIndirectNone;
   f->pfnDispatchIndirect = DispatchIndirectNone;
   f->pfnCopyStructureCount = CopyStructureCountNone;
   /* deferred contexts and command lists stay NULL (THREADING caps 0) */
}
