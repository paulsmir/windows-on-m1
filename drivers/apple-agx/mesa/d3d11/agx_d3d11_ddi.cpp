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
 * D3D-H4/H5 (reachable once FL 11_0 is reported): hull and domain shaders,
 * typed UAVs of buffers and textures, pixel-stage UAVs, append/consume
 * counters and CopyStructureCount, typed UAV clears, indirect draws and
 * dispatch.
 * Deferred contexts are left NULL: D3D11DDICAPS_THREADING reports no
 * command lists, so the runtime emulates them.
 *
 * SPDX-License-Identifier: MIT
 */
#include "gallium/frontends/d3d10umd/Draw.h"
#include "gallium/frontends/d3d10umd/Device.h"
#include "gallium/frontends/d3d10umd/Format.h"
#include "gallium/frontends/d3d10umd/InputAssembly.h"
#include "gallium/frontends/d3d10umd/OutputMerger.h"
#include "gallium/frontends/d3d10umd/Query.h"
#include "gallium/frontends/d3d10umd/Rasterizer.h"
#include "gallium/frontends/d3d10umd/Resource.h"
#include "gallium/frontends/d3d10umd/Shader.h"
#include "gallium/frontends/d3d10umd/State.h"

#include <stddef.h>

#include "util/bitscan.h"
#include "util/format/u_format.h"
#include "util/u_draw.h"
#include "util/u_inlines.h"
#include "util/u_sampler.h"
#include "util/u_surface.h"

#include "agx_d3d11_ddi.h"
#include "dxbc_to_nir.h"
#include "dxbc_tir_gs.h"

/* Draw.cpp (projection): the frontend's draw-time state resolution */
void AgxD3d11ResolveState(Device *pDevice);

extern "C" VOID AgxD3d10WindowsDiagnostic(PCSTR Stage, HRESULT Status,
                                          const UINT *Values, UINT Count);
extern "C" BOOL AgxD3d10WindowsDiagnosticSwitch(const char *Name);

/* EXP1310 receipt-only (APPLE_AGX_D3D11_COUNTER_TRACE=1): CopyStructureCount
 * copied 0 after an initial count of 42 and 256 after 86 compact appends.
 * Records the hidden counter resource and the value the CPU reads from it
 * where the initial count is written and where the count is copied. */
static void
counter_trace(PCSTR stage, struct pipe_context *pipe, struct pipe_resource *counter, UINT a, UINT b)
{
   static volatile LONG records;
   if (!AgxD3d10WindowsDiagnosticSwitch("APPLE_AGX_D3D11_COUNTER_TRACE") ||
       InterlockedIncrement(&records) > 64 || !counter)
      return;
   UINT value = ~0u;
   struct pipe_transfer *transfer = NULL;
   struct pipe_box box;
   u_box_1d(0, 4, &box);
   const void *map = pipe->buffer_map(pipe, counter, 0, PIPE_MAP_READ, &box, &transfer);
   if (map) {
      memcpy(&value, map, 4);
      pipe->buffer_unmap(pipe, transfer);
   }
   UINT values[5] = {(UINT)(uintptr_t)counter, (UINT)((uint64_t)(uintptr_t)counter >> 32), a, b,
                     value};
   AgxD3d10WindowsDiagnostic(stage, S_OK, values, 5);
}

/* D3D-H2a: advertised by default (EXP1295: every D3D smoke passes through
 * the D3D11 interface); APPLE_AGX_D3D11_DDI=0 keeps a process on 10.0. */
BOOL
AgxD3d11DdiEnabled(void)
{
   char value[4];
   DWORD length = GetEnvironmentVariableA("APPLE_AGX_D3D11_DDI", value, sizeof(value));
   return !(length == 1 && value[0] == '0');
}

BOOL
AgxD3d11ComputeEnabled(void)
{
   return AgxD3d11DdiEnabled() && AgxD3d10WindowsDiagnosticSwitch("APPLE_AGX_D3D11_COMPUTE");
}

/* D3D-H5: feature levels 10_1 and 11_0 by default (EXP1318: every FL 11_0
 * smoke case passes and the FL 11_0 corpus completes with pixel_diff 0);
 * APPLE_AGX_D3D11_FL11=0 keeps a process on 10_0. */
BOOL
AgxD3d11Fl11Enabled(void)
{
   char value[4];
   DWORD length = GetEnvironmentVariableA("APPLE_AGX_D3D11_FL11", value, sizeof(value));
   return AgxD3d11DdiEnabled() && !(length == 1 && value[0] == '0');
}

BOOL
AgxD3d11Fl111Enabled(void)
{
   char value[4];
   DWORD length = GetEnvironmentVariableA("APPLE_AGX_D3D11_FL111", value, sizeof(value));
   return AgxD3d11Fl11Enabled() && length == 1 && value[0] == '1';
}

/* D3D-H7: the WDDM 2.0 table goes with the FL 11_0 switch (EXP1299: the
 * runtime refuses FL 11_0 without it); APPLE_AGX_D3D11_WDDM2=0 keeps such a
 * process on the 11.0 table. */
BOOL
AgxWddm2_0DdiEnabled(void)
{
   char value[4];
   DWORD length = GetEnvironmentVariableA("APPLE_AGX_D3D11_WDDM2", value, sizeof(value));
   return AgxD3d11Fl11Enabled() && !(length == 1 && value[0] == '0');
}

/* The D3D11 binding and misc flags without a behaviour here: unordered
 * access is implemented for buffers only (raw and structured views), raw and
 * structured layouts for buffers only. */
#define AGX_D3D11_UNSUPPORTED_BIND (~(UINT)(D3D10_DDI_BIND_MASK | D3D11_DDI_BIND_UNORDERED_ACCESS))
#define AGX_D3D11_UNSUPPORTED_MISC D3D11_DDI_RESOURCE_MISC_RESOURCE_CLAMP
#define AGX_D3D11_BUFFER_MISC \
   (D3D11_DDI_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS | D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED)
/* D3D11_REQ_MULTI_ELEMENT_STRUCTURE_SIZE_IN_BYTES */
#define AGX_D3D11_MAX_STRUCTURE_STRIDE 2048u
/* D3D11_1_UAV_SLOT_COUNT (FL 11_1): the translator's D3D11.1 u# range */
#define AGX_D3D11_UAV_SLOTS DXBC_NIR_UAV11_SLOTS

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
      return !layout && !args->ByteStride &&
             !(args->MiscFlags & D3D11_DDI_RESOURCE_MISC_DRAWINDIRECT_ARGS) &&
             (!(args->BindFlags & D3D11_DDI_BIND_UNORDERED_ACCESS) || args->SampleDesc.Count <= 1);
   if (layout == AGX_D3D11_BUFFER_MISC ||
       (layout == D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED &&
        (args->MiscFlags & D3D11_DDI_RESOURCE_MISC_DRAWINDIRECT_ARGS)))
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
      resource->agx_misc_flags = args->MiscFlags &
         (AGX_D3D11_BUFFER_MISC | D3D11_DDI_RESOURCE_MISC_DRAWINDIRECT_ARGS);
      resource->agx_structure_stride =
         (resource->agx_misc_flags & D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED) ? args->ByteStride : 0;
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
             (resource->agx_misc_flags & D3D11_DDI_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) ? 4u : 0u;
   return format == DXGI_FORMAT_UNKNOWN &&
          (resource->agx_misc_flags & D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED) ?
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

/* D3D-H5 (FL 10_1): cube-array views, cube views at any face of a cube-array
 * resource and 2D-array views of cube resources -- the sampler views the
 * 10.0 path does not describe (it takes one cube of a six-slice resource).
 * Returns false with *handled false for every other view. */
static bool
srv_cube_family(D3D10DDI_HDEVICE hDevice, const D3D11DDIARG_CREATESHADERRESOURCEVIEW *args,
                D3D10DDI_HSHADERRESOURCEVIEW hView, bool *handled)
{
   Device *device = CastDevice(hDevice);
   Resource *res = args ? CastResource(args->hDrvResource) : NULL;
   struct pipe_resource *tex = res ? res->resource : NULL;
   *handled = false;
   if (!device || !tex || (tex->target != PIPE_TEXTURE_CUBE && tex->target != PIPE_TEXTURE_CUBE_ARRAY))
      return false;
   bool cube = args->ResourceDimension == D3D10DDIRESOURCE_TEXTURECUBE;
   if (!cube && args->ResourceDimension != D3D10DDIRESOURCE_TEXTURE2D)
      return false;
   if (cube && tex->target == PIPE_TEXTURE_CUBE && args->TexCube.First2DArrayFace == 0 &&
       args->TexCube.NumCubes == 1)
      return false;
   *handled = true;
   ShaderResourceView *view = CastShaderResourceView(hView);
   memset(view, 0, sizeof(*view));
   UINT level = cube ? args->TexCube.MostDetailedMip : args->Tex2D.MostDetailedMip;
   UINT levels = cube ? args->TexCube.MipLevels : args->Tex2D.MipLevels;
   UINT first = cube ? args->TexCube.First2DArrayFace : args->Tex2D.FirstArraySlice;
   UINT count = cube ? 6u * args->TexCube.NumCubes : args->Tex2D.ArraySize;
   if (levels == ~0u && level <= tex->last_level)
      levels = tex->last_level + 1u - level;
   if (res->owner_device != device || !(res->bind_flags & D3D10_DDI_BIND_SHADER_RESOURCE) ||
       !count || !levels || level > tex->last_level || levels > tex->last_level + 1u - level ||
       first >= tex->array_size || count > tex->array_size - first ||
       !AgxD3d10FormatViewCompatible(res->Format, args->Format, FALSE, FALSE))
      return false;
   enum pipe_format format = FormatTranslate(args->Format, FALSE);
   enum pipe_format lowered = AgxD3d10LoweredTextureFormat(args->Format);
   if (lowered != PIPE_FORMAT_NONE)
      format = lowered;
   if (format == PIPE_FORMAT_NONE)
      return false;
   struct pipe_sampler_view desc;
   u_sampler_view_default_template(&desc, tex, format);
   desc.target = !cube ? PIPE_TEXTURE_2D_ARRAY : count == 6u ? PIPE_TEXTURE_CUBE : PIPE_TEXTURE_CUBE_ARRAY;
   desc.u.tex.first_level = level;
   desc.u.tex.last_level = level + levels - 1u;
   desc.u.tex.first_layer = first;
   desc.u.tex.last_layer = first + count - 1u;
   view->handle = device->pipe->create_sampler_view(device->pipe, tex, &desc);
   if (!view->handle)
      return false;
   view->owner_device = device;
   view->owner_resource = res;
   res->sample_format = args->Format;
   res->sample_level = level;
   res->sample_layer = first;
   return true;
}

static void APIENTRY
CreateShaderResourceView11(D3D10DDI_HDEVICE hDevice,
                           const D3D11DDIARG_CREATESHADERRESOURCEVIEW *args,
                           D3D10DDI_HSHADERRESOURCEVIEW hView,
                           D3D10DDI_HRTSHADERRESOURCEVIEW hRTView)
{
   D3D10DDIARG_CREATESHADERRESOURCEVIEW view;
   bool handled = false;
   if (srv_cube_family(hDevice, args, hView, &handled) || handled) {
      if (!CastShaderResourceView(hView)->handle)
         refuse(hDevice, "reject-d3d11-create-srv", (UINT)args->ResourceDimension, (UINT)args->Format);
      return;
   }
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

/* An unordered-access view: a typed view (buffer or texture) is image u#; a
 * raw or structured buffer view and an append/consume or counter view's
 * hidden counter (one 32-bit word) are the shader buffers the bound shader's
 * UAV map names (dxbc_nir_uav_slots; apply_uavs). */
struct AgxUnorderedAccessView {
   Device *owner_device;
   Resource *owner_resource;
   bool typed;
   struct pipe_shader_buffer buffer;
   struct pipe_image_view image;
   struct pipe_resource *counter;
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

/* A typed view of a texture or a buffer: the pipe image of its format,
 * level and slices (D3D11DDIARG_TEX*_UNORDEREDACCESSVIEW). */
static bool
uav_image(const D3D11DDIARG_CREATEUNORDEREDACCESSVIEW *args, Resource *resource,
          struct pipe_image_view *image)
{
   struct pipe_resource *res = resource->resource;
   enum pipe_format format = FormatTranslate(args->Format, FALSE);
   unsigned level = 0, first = 0, count = 1, layers = 1;
   memset(image, 0, sizeof(*image));
   if (format == PIPE_FORMAT_NONE)
      return false;
   image->resource = res;
   image->format = format;
   image->access = image->shader_access = PIPE_IMAGE_ACCESS_READ_WRITE;
   switch (args->ResourceDimension) {
   case D3D10DDIRESOURCE_BUFFER:
   case D3D11DDIRESOURCE_BUFFEREX: {
      UINT64 element = util_format_get_blocksize(format);
      UINT64 offset = (UINT64)args->Buffer.FirstElement * element;
      UINT64 bytes = (UINT64)args->Buffer.NumElements * element;
      if (res->target != PIPE_BUFFER || args->Buffer.Flags || !bytes ||
          offset > resource->logical_bytes || bytes > resource->logical_bytes - offset)
         return false;
      image->u.buf.offset = (unsigned)offset;
      image->u.buf.size = (unsigned)bytes;
      return true;
   }
   case D3D10DDIRESOURCE_TEXTURE1D:
      level = args->Tex1D.MipSlice; first = args->Tex1D.FirstArraySlice;
      count = args->Tex1D.ArraySize; layers = res->array_size;
      break;
   case D3D10DDIRESOURCE_TEXTURE2D:
      level = args->Tex2D.MipSlice; first = args->Tex2D.FirstArraySlice;
      count = args->Tex2D.ArraySize; layers = res->array_size;
      break;
   case D3D10DDIRESOURCE_TEXTURE3D:
      level = args->Tex3D.MipSlice; first = args->Tex3D.FirstW;
      count = args->Tex3D.WSize;
      layers = level <= res->last_level ? u_minify(res->depth0, level) : 0;
      if (count == ~0u && first < layers)
         count = layers - first;
      break;
   default:
      return false;
   }
   if (res->target == PIPE_BUFFER || res->nr_samples > 1 || level > res->last_level || !count ||
       first >= layers || count > layers - first)
      return false;
   image->u.tex.level = level;
   image->u.tex.first_layer = first;
   image->u.tex.last_layer = first + count - 1;
   return true;
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
   UINT flags = buffer ? args->Buffer.Flags : 0;
   bool raw = flags & D3D11_DDI_BUFFER_UAV_FLAG_RAW;
   bool counter = flags & (D3D11_DDI_BUFFER_UAV_FLAG_APPEND | D3D11_DDI_BUFFER_UAV_FLAG_COUNTER);
   bool valid = device && resource && resource->owner_device == device && resource->resource &&
                (resource->bind_flags & D3D11_DDI_BIND_UNORDERED_ACCESS);
   if (valid && buffer && (raw || args->Format == DXGI_FORMAT_UNKNOWN)) {
      /* raw (32-bit elements) or structured (the resource's stride) */
      UINT stride = !(flags & ~(UINT)(D3D11_DDI_BUFFER_UAV_FLAG_RAW | D3D11_DDI_BUFFER_UAV_FLAG_APPEND |
                                      D3D11_DDI_BUFFER_UAV_FLAG_COUNTER)) && !(raw && counter) ?
         buffer_view_stride(resource, args->Format, raw) : 0u;
      UINT64 offset = (UINT64)args->Buffer.FirstElement * stride;
      UINT64 bytes = (UINT64)args->Buffer.NumElements * stride;
      valid = stride && bytes && offset <= resource->logical_bytes &&
              bytes <= resource->logical_bytes - offset;
      if (valid) {
         pipe_resource_reference(&view->buffer.buffer, resource->resource);
         view->buffer.buffer_offset = (unsigned)offset;
         view->buffer.buffer_size = (unsigned)bytes;
      }
      if (valid && counter) {
         static const UINT zero = 0;
         view->counter = pipe_buffer_create(device->pipe->screen, PIPE_BIND_SHADER_BUFFER,
                                            PIPE_USAGE_DEFAULT, 4);
         valid = view->counter != NULL;
         if (valid)
            device->pipe->buffer_subdata(device->pipe, view->counter, PIPE_MAP_WRITE, 0, 4, &zero);
      }
   } else if (valid) {
      struct pipe_image_view image;
      valid = uav_image(args, resource, &image);
      if (valid) {
         view->typed = true;
         view->image = image;
         view->image.resource = NULL;
         pipe_resource_reference(&view->image.resource, resource->resource);
      }
   }
   if (!valid) {
      pipe_resource_reference(&view->buffer.buffer, NULL);
      pipe_resource_reference(&view->counter, NULL);
      refuse(hDevice, "reject-d3d11-uav", args ? (UINT)args->ResourceDimension : ~0u,
             args ? (UINT)args->Format : ~0u);
      return;
   }
   view->owner_device = device;
   view->owner_resource = resource;
}

static void APIENTRY
DestroyUnorderedAccessView11(D3D10DDI_HDEVICE hDevice, D3D11DDI_HUNORDEREDACCESSVIEW hView)
{
   AgxUnorderedAccessView *view = CastUnorderedAccessView(hView);
   Device *device = CastDevice(hDevice);
   if (!view)
      return;
   /* the runtime unbinds first; never keep a pointer to a dead view */
   for (unsigned p = 0; device && p < 2; ++p)
      for (unsigned u = 0; u < AGX_D3D11_UAV_SLOTS; ++u)
         if (device->agx_uavs[p][u] == view)
            device->agx_uavs[p][u] = NULL;
   pipe_resource_reference(&view->buffer.buffer, NULL);
   pipe_resource_reference(&view->image.resource, NULL);
   pipe_resource_reference(&view->counter, NULL);
   view->owner_device = NULL;
   view->owner_resource = NULL;
}

/* FL 11_1: binds a stage's UAVs from its pipeline's views (graphics: every
 * graphics stage, D3D11.1; compute) through the bound shader's map -- typed
 * views at the images, buffer views and counters at the shader buffers the
 * map names. Asahi binds 32 images (its 32-bit image_mask: EXP1887 found
 * slot u + 32 unbinding slot u). A shader without a map (the TGSI path) has
 * u0..u7 at their own slots and counters at DXBC_NIR_UAV_COUNTER_SLOT + u#. */
static void
apply_uavs(Device *device, mesa_shader_stage stage)
{
   struct pipe_shader_buffer buffers[PIPE_MAX_SHADER_BUFFERS] = {};
   struct pipe_image_view images[DXBC_NIR_UAV11_IMAGES] = {};
   const Shader *shader = static_cast<const Shader *>(device->agx_stage_shader[stage]);
   const struct dxbc_nir_uav_slots *map =
      shader ? static_cast<const struct dxbc_nir_uav_slots *>(shader->agx_uav64) : NULL;
   void *const *views = device->agx_uavs[stage == MESA_SHADER_COMPUTE];
   bool any = false;
   for (unsigned u = 0; u < AGX_D3D11_UAV_SLOTS && !any; ++u)
      any = views[u] != NULL;
   /* a shader bind without views on either side changes nothing (DWM) */
   if (!any && !(device->agx_uav_stages & (1u << stage)))
      return;
   device->agx_uav_stages = any ? device->agx_uav_stages | (1u << stage) :
                                  device->agx_uav_stages & ~(1u << stage);
   for (unsigned u = 0; u < AGX_D3D11_UAV_SLOTS; ++u) {
      const AgxUnorderedAccessView *view = static_cast<const AgxUnorderedAccessView *>(views[u]);
      if (!view)
         continue;
      if (view->typed) {
         const unsigned image = map ? map->image[u] : u < DXBC_NIR_MAX_UAVS ? u : DXBC_NIR_UAV_UNUSED;
         if (image < DXBC_NIR_UAV11_IMAGES)
            images[image] = view->image;
         continue;
      }
      const unsigned buffer = map ? map->buffer[u] : u < DXBC_NIR_MAX_UAVS ? u : DXBC_NIR_UAV_UNUSED;
      const unsigned counter = map ? map->counter[u] :
         u < DXBC_NIR_MAX_UAVS ? DXBC_NIR_UAV_COUNTER_SLOT + u : DXBC_NIR_UAV_UNUSED;
      if (buffer < PIPE_MAX_SHADER_BUFFERS)
         buffers[buffer] = view->buffer;
      if (counter < PIPE_MAX_SHADER_BUFFERS && view->counter) {
         buffers[counter].buffer = view->counter;
         buffers[counter].buffer_size = 4;
      }
   }
   device->pipe->set_shader_buffers(device->pipe, stage, 0, PIPE_MAX_SHADER_BUFFERS, buffers,
                                    BITFIELD_MASK(PIPE_MAX_SHADER_BUFFERS));
   device->pipe->set_shader_images(device->pipe, stage, 0, DXBC_NIR_UAV11_IMAGES, 0, images);
}

/* The stages a pipeline's UAVs reach (D3D11.1: every graphics stage). */
static void
apply_pipeline_uavs(Device *device, bool compute)
{
   static const mesa_shader_stage graphics[] = {MESA_SHADER_VERTEX, MESA_SHADER_TESS_CTRL,
                                                MESA_SHADER_TESS_EVAL, MESA_SHADER_GEOMETRY,
                                                MESA_SHADER_FRAGMENT};
   if (compute) {
      apply_uavs(device, MESA_SHADER_COMPUTE);
      return;
   }
   for (mesa_shader_stage stage : graphics)
      apply_uavs(device, stage);
}

/* A stage's shader changed: its map places the pipeline's views. */
static void
note_stage_shader(Device *device, mesa_shader_stage stage, Shader *shader)
{
   device->agx_stage_shader[stage] = shader;
   apply_uavs(device, stage);
}

/* Binds slots [first, first + count) of a pipeline: views[i - start] for the
 * slots in [start, start + n), nothing for the others; an initial count other
 * than ~0 sets a counter view's hidden counter. */
static bool
bind_uavs(Device *device, bool compute, UINT first, UINT count, UINT start, UINT n,
          const D3D11DDI_HUNORDEREDACCESSVIEW *views, const UINT *initial_counts)
{
   struct pipe_context *pipe = device->pipe;
   if (first > AGX_D3D11_UAV_SLOTS || count > AGX_D3D11_UAV_SLOTS - first ||
       start < first || n > count - (start - first) || (n && !views))
      return false;
   for (UINT i = 0; i < n; ++i) {
      AgxUnorderedAccessView *view = CastUnorderedAccessView(views[i]);
      if (view && (view->owner_device != device || (!view->typed && !view->buffer.buffer)))
         return false;
   }
   for (UINT slot = first; slot < first + count; ++slot)
      device->agx_uavs[compute][slot] = NULL;
   for (UINT i = 0; i < n; ++i) {
      AgxUnorderedAccessView *view = CastUnorderedAccessView(views[i]);
      device->agx_uavs[compute][start + i] = view;
      if (view && view->counter && initial_counts && initial_counts[i] != ~0u) {
         pipe->buffer_subdata(pipe, view->counter, PIPE_MAP_WRITE, 0, 4, &initial_counts[i]);
         counter_trace("measure-uav-counter-initial", pipe, view->counter, start + i,
                       initial_counts[i]);
      }
   }
   if (count)
      apply_pipeline_uavs(device, compute);
   return true;
}

static void APIENTRY
CsSetUnorderedAccessViews11(D3D10DDI_HDEVICE hDevice, UINT start, UINT n,
                            const D3D11DDI_HUNORDEREDACCESSVIEW *views, const UINT *initial_counts)
{
   Device *device = CastDevice(hDevice);
   if (!device || !bind_uavs(device, true, start, n, start, n, views, initial_counts))
      refuse(hDevice, "reject-d3d11-cs-uav", start, n);
}

/* The bits a uint clear writes: the low bits of values[c] in channel c. */
static void
pack_uint_bits(enum pipe_format format, const UINT values[4], uint8_t out[16])
{
   const struct util_format_description *desc = util_format_description(format);
   memset(out, 0, 16);
   for (unsigned c = 0; c < desc->nr_channels && c < 4; ++c) {
      unsigned size = desc->channel[c].size, shift = desc->channel[c].shift;
      uint64_t bits = size >= 32 ? values[c] : (values[c] & ((1u << size) - 1u));
      for (unsigned b = 0; b < size && shift + b < 128; ++b)
         if (bits & (1ull << b))
            out[(shift + b) / 8] |= (uint8_t)(1u << ((shift + b) % 8));
   }
}

/* Typed views: one pixel of the view's format over the view's level and
 * slices (buffers: every element). */
static void
clear_typed(Device *device, AgxUnorderedAccessView *view, const uint8_t pixel[16])
{
   struct pipe_context *pipe = device->pipe;
   struct pipe_resource *res = view->image.resource;
   if (res->target == PIPE_BUFFER) {
      pipe->clear_buffer(pipe, res, view->image.u.buf.offset, view->image.u.buf.size, pixel,
                         util_format_get_blocksize(view->image.format));
      return;
   }
   unsigned level = view->image.u.tex.level;
   struct pipe_box box;
   u_box_3d(0, 0, 0, u_minify(res->width0, level), u_minify(res->height0, level), 1, &box);
   if (res->target == PIPE_TEXTURE_3D || res->array_size > 1) {
      box.z = view->image.u.tex.first_layer;
      box.depth = view->image.u.tex.last_layer - view->image.u.tex.first_layer + 1;
   }
   /* Asahi implements no clear_texture (EXP1330: a NULL hook is a call to
    * address 0); Gallium's default clears through a render target or, where
    * the driver has none, on the CPU. */
   if (pipe->clear_texture)
      pipe->clear_texture(pipe, res, level, &box, pixel);
   else
      u_default_clear_texture(pipe, res, level, &box, pixel);
}

/* Raw and structured views: every 32-bit word takes values[0]; typed views:
 * the low bits of each value in its channel. */
static void APIENTRY
ClearUnorderedAccessViewUint11(D3D10DDI_HDEVICE hDevice, D3D11DDI_HUNORDEREDACCESSVIEW hView,
                               const UINT values[4])
{
   Device *device = CastDevice(hDevice);
   AgxUnorderedAccessView *view = CastUnorderedAccessView(hView);
   if (!device || !view || view->owner_device != device || !values ||
       (!view->typed && !view->buffer.buffer)) {
      refuse(hDevice, "reject-d3d11-clear-uav", 0, 0);
      return;
   }
   if (!CheckPredicate(device))
      return;
   if (view->typed) {
      uint8_t pixel[16];
      pack_uint_bits(view->image.format, values, pixel);
      clear_typed(device, view, pixel);
      return;
   }
   device->pipe->clear_buffer(device->pipe, view->buffer.buffer, view->buffer.buffer_offset,
                              view->buffer.buffer_size, &values[0], 4);
}

/* Typed views only: the values converted to the view's format. */
static void APIENTRY
ClearUnorderedAccessViewFloat11(D3D10DDI_HDEVICE hDevice, D3D11DDI_HUNORDEREDACCESSVIEW hView,
                                const FLOAT values[4])
{
   Device *device = CastDevice(hDevice);
   AgxUnorderedAccessView *view = CastUnorderedAccessView(hView);
   uint8_t pixel[16] = {};
   if (!device || !view || view->owner_device != device || !values || !view->typed) {
      refuse(hDevice, "reject-d3d11-clear-uav", 1, 0);
      return;
   }
   if (!CheckPredicate(device))
      return;
   util_format_pack_rgba(view->image.format, pixel, values, 1);
   clear_typed(device, view, pixel);
}

/* The hidden counter of an append/consume or counter view, as 32 bits at
 * an aligned offset of a buffer. */
static void APIENTRY
CopyStructureCount11(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE hDst, UINT offset,
                     D3D11DDI_HUNORDEREDACCESSVIEW hView)
{
   Device *device = CastDevice(hDevice);
   Resource *dst = CastResource(hDst);
   AgxUnorderedAccessView *view = CastUnorderedAccessView(hView);
   struct pipe_box box;
   if (!device || !dst || !dst->resource || dst->owner_device != device || !dst->buffer ||
       !view || view->owner_device != device || !view->counter || (offset & 3u) ||
       offset > dst->logical_bytes || dst->logical_bytes - offset < 4) {
      refuse(hDevice, "reject-d3d11-copy-structure-count", offset, 0);
      return;
   }
   counter_trace("measure-uav-counter-copy", device->pipe, view->counter, offset,
                 (UINT)(uintptr_t)dst->resource);
   u_box_1d(0, 4, &box);
   device->pipe->resource_copy_region(device->pipe, dst->resource, 0, offset, 0, 0, view->counter,
                                      0, &box);
   if (AgxD3d10WindowsDiagnosticSwitch("APPLE_AGX_D3D11_COUNTER_TRACE")) {
      /* EXP1311: the counter held 86 (CPU read) while the copy delivered
       * 256. Copy the counter on the GPU into a fresh buffer and read both
       * it and the destination back after the copy. */
      struct pipe_context *pipe = device->pipe;
      struct pipe_resource *temp = pipe_buffer_create(pipe->screen, PIPE_BIND_SHADER_BUFFER,
                                                      PIPE_USAGE_DEFAULT, 4);
      if (temp) {
         pipe->resource_copy_region(pipe, temp, 0, 0, 0, 0, view->counter, 0, &box);
         counter_trace("measure-uav-counter-gpu-copy", pipe, temp, offset, 0);
         pipe_resource_reference(&temp, NULL);
      }
      counter_trace("measure-uav-counter-dst", pipe, dst->resource, offset,
                    (UINT)(uintptr_t)view->counter);
   }
}

static void APIENTRY
SetRenderTargets11(D3D10DDI_HDEVICE hDevice, const D3D10DDI_HRENDERTARGETVIEW *phRTVs,
                   UINT NumRTVs, UINT ClearSlots, D3D10DDI_HDEPTHSTENCILVIEW hDSV,
                   const D3D11DDI_HUNORDEREDACCESSVIEW *phUAVs, const UINT *pInitialCounts,
                   UINT UAVStartSlot, UINT NumUAVs, UINT UAVRangeStart, UINT UAVRangeSize)
{
   /* D3D-H5: graphics UAVs share the slot space with the render targets
    * (UAVStartSlot >= NumRTVs); the range updated includes unbinds. FL 11_1:
    * every graphics stage sees them. */
   SetRenderTargets(hDevice, phRTVs, NumRTVs, ClearSlots, hDSV);
   Device *device = CastDevice(hDevice);
   UINT first = MIN2(UAVRangeStart, UAVStartSlot);
   UINT end = MAX2(UAVRangeStart + UAVRangeSize, UAVStartSlot + NumUAVs);
   if (!NumUAVs && !UAVRangeSize)
      return;
   if (!device || !bind_uavs(device, false, first, end - first, UAVStartSlot,
                             NumUAVs, phUAVs, pInitialCounts))
      refuse(hDevice, "reject-d3d11-pixel-uav", UAVStartSlot, NumUAVs);
}

/* FL 11_1: translates with the D3D11.1 UAV registers and keeps the map in
 * the shader (DestroyShader frees it). */
static struct nir_shader *
translate_uav64(Shader *shader, const UINT *code, const struct nir_shader_compiler_options *options,
                uint32_t registers, uint32_t positions, struct dxbc_nir_error *error)
{
   struct dxbc_nir_uav_slots *slots =
      static_cast<struct dxbc_nir_uav_slots *>(malloc(sizeof(struct dxbc_nir_uav_slots)));
   struct nir_shader *nir =
      slots ? dxbc_translate_tokens_uav64(code, options, registers, positions, slots, error) : NULL;
   if (nir)
      shader->agx_uav64 = slots;
   else
      free(slots);
   return nir;
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
   struct nir_shader *nir = translate_uav64(shader, code, pipe->screen->nir_options[MESA_SHADER_COMPUTE],
                                            0, 0, &error);
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
   note_stage_shader(device, MESA_SHADER_COMPUTE, shader);
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

/* D3D-H5: indirect arguments: a DRAWINDIRECT_ARGS buffer, a 4-byte aligned
 * offset and room for the argument structure. */
static Resource *
indirect_args(Device *device, D3D10DDI_HRESOURCE hArgs, UINT offset, UINT bytes)
{
   Resource *args = CastResource(hArgs);
   if (!device || !args || args->owner_device != device || !args->resource || !args->buffer ||
       !(args->agx_misc_flags & D3D11_DDI_RESOURCE_MISC_DRAWINDIRECT_ARGS) || (offset & 3u) ||
       offset > args->logical_bytes || args->logical_bytes - offset < bytes)
      return NULL;
   return args;
}

/* ------------------------------------------------------------------ */
/* FL 11_1 forced sample count (investigation/analysis/D3D11-FL111-TIR.md)
 *
 * A triangle draw whose rasterizer state forces 4, 8 or 16 samples (TIR and
 * UAV-only rendering) runs in one pass with the bound pixel shader's
 * variant (dxbc_translate_tokens_tir) behind the expansion GS (dxbc_tir_gs),
 * made once per vertex shader (serial) and sample count from the stages'
 * tokens. Not covered yet, drawn as plain 1x rasterization with a
 * reject-d3d11-tir receipt: application geometry or tessellation shaders,
 * stream output, non-triangle topologies, shaders without tokens. */
#define AGX_TIR_VARIANTS 8u
struct AgxTirVariant {
   uint64_t vs_serial;
   UINT samples;
   void *fs, *gs;
};
struct AgxTirCache {
   UINT count;
   AgxTirVariant v[AGX_TIR_VARIANTS];
};

static void
tir_release(struct pipe_context *pipe, Shader *ps)
{
   AgxTirCache *cache = static_cast<AgxTirCache *>(ps->agx_tir);
   for (UINT i = 0; cache && i < cache->count; ++i) {
      if (cache->v[i].fs)
         pipe->delete_fs_state(pipe, cache->v[i].fs);
      if (cache->v[i].gs)
         pipe->delete_gs_state(pipe, cache->v[i].gs);
   }
   free(cache);
   ps->agx_tir = NULL;
}

static void
tir_refuse(D3D10DDI_HDEVICE hDevice, Device *device, UINT why, UINT detail)
{
   if (!device->agx_tir_refused)
      refuse(hDevice, "reject-d3d11-tir", why, detail);
   device->agx_tir_refused = TRUE;
}

static const AgxTirVariant *
tir_variant(Device *device, Shader *vs, Shader *ps, UINT samples)
{
   AgxTirCache *cache = static_cast<AgxTirCache *>(ps->agx_tir);
   for (UINT i = 0; cache && i < cache->count; ++i)
      if (cache->v[i].vs_serial == vs->agx_serial && cache->v[i].samples == samples)
         return &cache->v[i];
   if (!cache && !(cache = static_cast<AgxTirCache *>(calloc(1, sizeof(AgxTirCache)))))
      return NULL;
   ps->agx_tir = cache;
   struct pipe_context *pipe = device->pipe;
   struct dxbc_nir_uav_slots slots, vs_slots;
   struct dxbc_nir_error error = {};
   struct dxbc_tir_gs_key key;
   struct nir_shader *fs = dxbc_translate_tokens_tir(ps->agx_code, pipe->screen->nir_options[MESA_SHADER_FRAGMENT],
                                                     &slots, samples, &error);
   struct nir_shader *vsn = fs ? dxbc_translate_tokens_uav64(vs->agx_code,
      pipe->screen->nir_options[MESA_SHADER_VERTEX], 0, 0, &vs_slots, &error) : NULL;
   const bool keyed = vsn && dxbc_tir_gs_key_from(vsn, fs, &key);
   ralloc_free(vsn);
   struct nir_shader *gs = keyed ? dxbc_tir_gs(pipe->screen->nir_options[MESA_SHADER_GEOMETRY], &key) : NULL;
   UINT values[4] = {samples, fs ? 1u : 0u, keyed ? key.count : 0u, gs ? 1u : 0u};
   AgxD3d10WindowsDiagnostic("measure-d3d11-tir-variant", gs ? S_OK : E_NOTIMPL, values, 4u);
   if (!gs) {
      ralloc_free(fs);
      return NULL;
   }
   dxbc_nir_finalize(gs);
   struct pipe_shader_state state = {};
   state.type = PIPE_SHADER_IR_NIR;
   state.ir.nir = fs;
   void *fs_handle = pipe->create_fs_state(pipe, &state);
   state.ir.nir = gs;
   void *gs_handle = pipe->create_gs_state(pipe, &state);
   if (!fs_handle || !gs_handle) {
      if (fs_handle)
         pipe->delete_fs_state(pipe, fs_handle);
      if (gs_handle)
         pipe->delete_gs_state(pipe, gs_handle);
      return NULL;
   }
   AgxTirVariant *v = &cache->v[cache->count < AGX_TIR_VARIANTS ? cache->count++ : AGX_TIR_VARIANTS - 1u];
   if (v->fs)
      pipe->delete_fs_state(pipe, v->fs);
   if (v->gs)
      pipe->delete_gs_state(pipe, v->gs);
   *v = {vs->agx_serial, samples, fs_handle, gs_handle};
   return v;
}

/* Binds a draw's TIR variant for the scope of the draw. */
struct AgxTirScope {
   Device *device = NULL;
   Shader *ps = NULL;
   explicit AgxTirScope(D3D10DDI_HDEVICE hDevice)
   {
      Device *d = CastDevice(hDevice);
      if (!d || d->agx_forced_samples < 4u)
         return;
      Shader *vs = static_cast<Shader *>(d->agx_stage_shader[MESA_SHADER_VERTEX]);
      Shader *fs = static_cast<Shader *>(d->agx_stage_shader[MESA_SHADER_FRAGMENT]);
      if (d->primitive != MESA_PRIM_TRIANGLES && d->primitive != MESA_PRIM_TRIANGLE_STRIP) {
         tir_refuse(hDevice, d, 1u, (UINT)d->primitive);
         return;
      }
      if (d->agx_stage_shader[MESA_SHADER_GEOMETRY] || d->agx_stage_shader[MESA_SHADER_TESS_CTRL] ||
          d->agx_stage_shader[MESA_SHADER_TESS_EVAL] || d->bound_empty_gs) {
         tir_refuse(hDevice, d, 2u, 0u);
         return;
      }
      if (!vs || !fs || !vs->agx_code || !fs->agx_code || !fs->handle) {
         tir_refuse(hDevice, d, 3u, 0u);
         return;
      }
      const AgxTirVariant *v = tir_variant(d, vs, fs, d->agx_forced_samples);
      if (!v) {
         tir_refuse(hDevice, d, 4u, d->agx_forced_samples);
         return;
      }
      /* viewport 0: viewport = ndc * scale + offset (D3D: y down) */
      const float vp[4] = {d->agx_viewport0[2] * 0.5f, d->agx_viewport0[3] * -0.5f,
                           d->agx_viewport0[0] + d->agx_viewport0[2] * 0.5f,
                           d->agx_viewport0[1] + d->agx_viewport0[3] * 0.5f};
      struct pipe_constant_buffer cb = {};
      cb.buffer_size = sizeof(vp);
      cb.user_buffer = vp;
      d->pipe->set_constant_buffer(d->pipe, MESA_SHADER_GEOMETRY, DXBC_TIR_GS_CB, false, &cb);
      d->pipe->bind_gs_state(d->pipe, v->gs);
      d->pipe->bind_fs_state(d->pipe, v->fs);
      device = d;
      ps = fs;
   }
   ~AgxTirScope()
   {
      if (!device)
         return;
      device->pipe->bind_fs_state(device->pipe, ps->handle);
      device->pipe->bind_gs_state(device->pipe, NULL);
      device->pipe->set_constant_buffer(device->pipe, MESA_SHADER_GEOMETRY, DXBC_TIR_GS_CB, false, NULL);
   }
};

static PFND3D10DDI_DRAW draw_10;
static PFND3D10DDI_DRAWINDEXED draw_indexed_10;
static PFND3D10DDI_DRAWINSTANCED draw_instanced_10;
static PFND3D10DDI_DRAWINDEXEDINSTANCED draw_indexed_instanced_10;
static PFND3D10DDI_DRAWAUTO draw_auto_10;

static void APIENTRY
Draw11(D3D10DDI_HDEVICE hDevice, UINT count, UINT start)
{
   AgxTirScope tir(hDevice);
   draw_10(hDevice, count, start);
}

static void APIENTRY
DrawIndexed11(D3D10DDI_HDEVICE hDevice, UINT count, UINT start, INT base)
{
   AgxTirScope tir(hDevice);
   draw_indexed_10(hDevice, count, start, base);
}

static void APIENTRY
DrawInstanced11(D3D10DDI_HDEVICE hDevice, UINT count, UINT instances, UINT start, UINT first)
{
   AgxTirScope tir(hDevice);
   draw_instanced_10(hDevice, count, instances, start, first);
}

static void APIENTRY
DrawIndexedInstanced11(D3D10DDI_HDEVICE hDevice, UINT count, UINT instances, UINT start, INT base,
                       UINT first)
{
   AgxTirScope tir(hDevice);
   draw_indexed_instanced_10(hDevice, count, instances, start, base, first);
}

static void APIENTRY
DrawAuto11(D3D10DDI_HDEVICE hDevice)
{
   AgxTirScope tir(hDevice);
   draw_auto_10(hDevice);
}

static PFND3D10DDI_SETRASTERIZERSTATE set_rasterizer_state_10;
static PFND3D10DDI_SETVIEWPORTS set_viewports_10;

static void APIENTRY
SetRasterizerState11(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRASTERIZERSTATE hState)
{
   set_rasterizer_state_10(hDevice, hState);
   if (Device *device = CastDevice(hDevice)) {
      const RasterizerState *state = static_cast<const RasterizerState *>(hState.pDrvPrivate);
      device->agx_forced_samples = state ? state->agx_forced_samples : 0u;
   }
}

static void APIENTRY
SetViewports11(D3D10DDI_HDEVICE hDevice, UINT count, UINT clear, const D3D10_DDI_VIEWPORT *viewports)
{
   set_viewports_10(hDevice, count, clear, viewports);
   Device *device = CastDevice(hDevice);
   if (device && count && viewports) {
      device->agx_viewport0[0] = viewports[0].TopLeftX;
      device->agx_viewport0[1] = viewports[0].TopLeftY;
      device->agx_viewport0[2] = viewports[0].Width;
      device->agx_viewport0[3] = viewports[0].Height;
   }
}

/* FL 11_1: the tokens of a vertex or pixel shader, the source of its TIR
 * variants (an SM 5 vertex shader keeps them already, D3D-H5) */
static PFND3D10DDI_CREATEVERTEXSHADER create_vertex_shader_10;
static PFND3D10DDI_CREATEPIXELSHADER create_pixel_shader_10;

static UINT *
copy_tokens(const UINT *code)
{
   UINT *copy = code ? static_cast<UINT *>(malloc((size_t)code[1] * sizeof(UINT))) : NULL;
   if (copy)
      memcpy(copy, code, (size_t)code[1] * sizeof(UINT));
   return copy;
}

static void APIENTRY
CreateVertexShader11(D3D10DDI_HDEVICE hDevice, const UINT *code, D3D10DDI_HSHADER hShader,
                     D3D10DDI_HRTSHADER hRTShader, const D3D10DDIARG_STAGE_IO_SIGNATURES *sig)
{
   create_vertex_shader_10(hDevice, code, hShader, hRTShader, sig);
   Shader *shader = CastShader(hShader);
   if (shader && shader->handle && !shader->agx_code)
      shader->agx_code = copy_tokens(code); /* the frontend frees a VS's tokens */
}

static void APIENTRY
CreatePixelShader11(D3D10DDI_HDEVICE hDevice, const UINT *code, D3D10DDI_HSHADER hShader,
                    D3D10DDI_HRTSHADER hRTShader, const D3D10DDIARG_STAGE_IO_SIGNATURES *sig)
{
   create_pixel_shader_10(hDevice, code, hShader, hRTShader, sig);
   if (Shader *shader = CastShader(hShader))
      shader->agx_code = shader->handle ? copy_tokens(code) : NULL; /* DestroyShader11 frees */
}

static void
draw_indirect(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE hArgs, UINT offset, bool indexed)
{
   AgxTirScope tir(hDevice);
   Device *device = CastDevice(hDevice);
   Resource *args = indirect_args(device, hArgs, offset, indexed ? 20u : 16u);
   struct pipe_resource *indices = NULL;
   if (!args || device->primitive >= MESA_PRIM_COUNT ||
       (indexed && (!device->index_buffer || (device->index_size != 2 && device->index_size != 4)))) {
      refuse(hDevice, "reject-d3d11-draw-indirect", indexed, offset);
      return;
   }
   if (!CheckPredicate(device))
      return;
   struct pipe_context *pipe = device->pipe;
   struct pipe_draw_info info;
   struct pipe_draw_indirect_info indirect = {};
   struct pipe_draw_start_count_bias draw = {};
   util_draw_init_info(&info);
   info.mode = device->primitive;
   indirect.buffer = args->resource;
   indirect.offset = offset;
   indirect.stride = indexed ? 20u : 16u;
   indirect.draw_count = 1;
   if (indexed) {
      /* indirect index fetch starts at the buffer (StartIndexLocation is in
       * the arguments): an IASetIndexBuffer offset takes a copy */
      pipe_resource_reference(&indices, device->index_buffer);
      if (device->ib_offset) {
         struct pipe_box box;
         unsigned bytes = device->index_buffer->width0 > device->ib_offset ?
                          device->index_buffer->width0 - device->ib_offset : 0;
         pipe_resource_reference(&indices, NULL);
         if (!bytes)
            return;
         indices = pipe_buffer_create(pipe->screen, PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, bytes);
         if (!indices) {
            SetError(hDevice, E_OUTOFMEMORY);
            return;
         }
         u_box_1d(device->ib_offset, bytes, &box);
         pipe->resource_copy_region(pipe, indices, 0, 0, 0, 0, device->index_buffer, 0, &box);
      }
      info.index_size = device->index_size;
      info.index.resource = indices;
      info.primitive_restart = true;
      info.restart_index = device->restart_index;
   }
   AgxD3d11ResolveState(device);
   pipe->draw_vbo(pipe, &info, 0, &indirect, &draw, 1);
   pipe_resource_reference(&indices, NULL);
}

static void APIENTRY
DrawInstancedIndirect11(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE hArgs, UINT offset)
{
   draw_indirect(hDevice, hArgs, offset, false);
}

static void APIENTRY
DrawIndexedInstancedIndirect11(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE hArgs, UINT offset)
{
   draw_indirect(hDevice, hArgs, offset, true);
}

static void APIENTRY
DispatchIndirect11(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE hArgs, UINT offset)
{
   Device *device = CastDevice(hDevice);
   Resource *args = indirect_args(device, hArgs, offset, 12u);
   if (!args) {
      refuse(hDevice, "reject-d3d11-dispatch", 1, offset);
      return;
   }
   if (!CheckPredicate(device) || !device->agx_cs_block[0])
      return;
   struct pipe_grid_info info = {};
   info.work_dim = 3;
   for (unsigned i = 0; i < 3; ++i)
      info.block[i] = device->agx_cs_block[i];
   info.indirect = args->resource;
   info.indirect_offset = offset;
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
   struct nir_shader *nir = translate_uav64(shader, code, pipe->screen->nir_options[stage],
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
   note_stage_shader(device, stage, shader);
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

/* FL 11_1: the frontend binds VS, GS and PS; their UAV maps follow. */
#define AGX_D3D11_SET_SHADER(name, frontend, stage)                                \
   static void APIENTRY name(D3D10DDI_HDEVICE hDevice, D3D10DDI_HSHADER hShader)   \
   {                                                                               \
      frontend(hDevice, hShader);                                                  \
      if (Device *device = CastDevice(hDevice))                                    \
         note_stage_shader(device, stage, CastShader(hShader));                    \
   }
AGX_D3D11_SET_SHADER(VsSetShader11, VsSetShader, MESA_SHADER_VERTEX)
AGX_D3D11_SET_SHADER(GsSetShader11, GsSetShader, MESA_SHADER_GEOMETRY)
AGX_D3D11_SET_SHADER(PsSetShader11, PsSetShader, MESA_SHADER_FRAGMENT)

static PFND3D10DDI_DESTROYSHADER destroy_shader_10;

/* the runtime unbinds first; never keep a pointer to a dead shader */
static void APIENTRY
DestroyShader11(D3D10DDI_HDEVICE hDevice, D3D10DDI_HSHADER hShader)
{
   Device *device = CastDevice(hDevice);
   Shader *shader = CastShader(hShader);
   if (device)
      for (void *&bound : device->agx_stage_shader)
         if (bound == hShader.pDrvPrivate)
            bound = NULL;
   if (device && shader && shader->type == MESA_SHADER_FRAGMENT) {
      tir_release(device->pipe, shader);
      free(shader->agx_code);
      shader->agx_code = NULL;
   }
   destroy_shader_10(hDevice, hShader);
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
AGX_D3D11_WITH_IFACES(VsSetShaderWithIfaces11, VsSetShader11)
AGX_D3D11_WITH_IFACES(GsSetShaderWithIfaces11, GsSetShader11)
AGX_D3D11_WITH_IFACES(PsSetShaderWithIfaces11, PsSetShader11)
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
   f->pfnPsSetShader = PsSetShader11; /* FL 11_1: UAV maps */
   f->pfnPsSetSamplers = base->pfnPsSetSamplers;
   f->pfnVsSetShader = VsSetShader11;
   draw_indexed_10 = base->pfnDrawIndexed;
   f->pfnDrawIndexed = DrawIndexed11; /* FL 11_1 TIR */
   draw_10 = base->pfnDraw;
   f->pfnDraw = Draw11;
   f->pfnDynamicIABufferMapNoOverwrite = base->pfnDynamicIABufferMapNoOverwrite;
   f->pfnDynamicIABufferUnmap = base->pfnDynamicIABufferUnmap;
   f->pfnDynamicConstantBufferMapDiscard = base->pfnDynamicConstantBufferMapDiscard;
   f->pfnDynamicIABufferMapDiscard = base->pfnDynamicIABufferMapDiscard;
   f->pfnDynamicConstantBufferUnmap = base->pfnDynamicConstantBufferUnmap;
   f->pfnPsSetConstantBuffers = base->pfnPsSetConstantBuffers;
   f->pfnIaSetInputLayout = base->pfnIaSetInputLayout;
   f->pfnIaSetVertexBuffers = base->pfnIaSetVertexBuffers;
   f->pfnIaSetIndexBuffer = base->pfnIaSetIndexBuffer;
   draw_indexed_instanced_10 = base->pfnDrawIndexedInstanced;
   f->pfnDrawIndexedInstanced = DrawIndexedInstanced11;
   draw_instanced_10 = base->pfnDrawInstanced;
   f->pfnDrawInstanced = DrawInstanced11;
   f->pfnDynamicResourceMapDiscard = base->pfnDynamicResourceMapDiscard;
   f->pfnDynamicResourceUnmap = base->pfnDynamicResourceUnmap;
   f->pfnGsSetConstantBuffers = base->pfnGsSetConstantBuffers;
   f->pfnGsSetShader = GsSetShader11;
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
   set_rasterizer_state_10 = base->pfnSetRasterizerState;
   f->pfnSetRasterizerState = SetRasterizerState11;
   f->pfnQueryEnd = base->pfnQueryEnd;
   f->pfnQueryBegin = base->pfnQueryBegin;
   f->pfnResourceCopyRegion = base->pfnResourceCopyRegion;
   f->pfnResourceUpdateSubresourceUP = base->pfnResourceUpdateSubresourceUP;
   f->pfnSoSetTargets = base->pfnSoSetTargets;
   draw_auto_10 = base->pfnDrawAuto;
   f->pfnDrawAuto = DrawAuto11;
   set_viewports_10 = base->pfnSetViewports;
   f->pfnSetViewports = SetViewports11;
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
   create_vertex_shader_10 = base->pfnCreateVertexShader;
   f->pfnCreateVertexShader = CreateVertexShader11;
   f->pfnCreateGeometryShader = base->pfnCreateGeometryShader;
   create_pixel_shader_10 = base->pfnCreatePixelShader;
   f->pfnCreatePixelShader = CreatePixelShader11;
   destroy_shader_10 = base->pfnDestroyShader;
   f->pfnDestroyShader = DestroyShader11;
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
   f->pfnCopyStructureCount = CopyStructureCount11;
   f->pfnDrawInstancedIndirect = DrawInstancedIndirect11;
   f->pfnDrawIndexedInstancedIndirect = DrawIndexedInstancedIndirect11;
   f->pfnDispatchIndirect = DispatchIndirect11;
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
   /* deferred contexts and command lists stay NULL (THREADING caps 0) */
}
