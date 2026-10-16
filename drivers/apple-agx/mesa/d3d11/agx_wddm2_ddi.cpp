/*
 * D3D-H7: the WDDM 2.0 D3D11 device function table over the D3D11 table
 * (investigation/analysis/D3D-H7-wddm2-ddi.md).
 *
 * EXP1299: the runtime opens the adapter with Interface 0x000b0011
 * (D3DWDDM2_0_DDI_INTERFACE_VERSION) and refuses a FL 11_0 device when the
 * driver offers only the 10.0 and 11.0 DDIs.  D3DWDDM2_0DDI_DEVICEFUNCS
 * (d3d10umddi.h, D3D11DDI_MINOR_HEADER_VERSION >= 9) keeps the first 152
 * entries of D3D11DDI_DEVICEFUNCS in the same order (checked below): 118
 * keep their type and come from AgxD3d11FillDeviceFuncs, 34 change type
 * (11.1 constant-buffer ranges and copy flags, 11.1 blend and
 * shader-signature structures, the WDDM 2.0 rasterizer, views, queries and
 * flush, the WDDM 1.3 multisample flags, relocation) and are adapters onto
 * the 11.0 entries, and 18 are new.  The 11.1 runtime reports constant-buffer
 * offsets, ClearView, no-overwrite constant-buffer maps and copy flags to
 * applications without asking the driver, so those are implemented; tiled
 * resources, markers, hardware protection, texture layouts and logic ops
 * have caps the driver reports as absent, and their entries fail closed.
 *
 * SPDX-License-Identifier: MIT
 */
#include "gallium/frontends/d3d10umd/Device.h"
#include "gallium/frontends/d3d10umd/OutputMerger.h"
#include "gallium/frontends/d3d10umd/Resource.h"
#include "gallium/frontends/d3d10umd/State.h"

#include <stddef.h>
#include <mutex>
#include <vector>

#include "util/u_inlines.h"
#include "util/format/u_format.h"

#include "agx_d3d11_ddi.h"

extern "C" VOID AgxD3d10WindowsDiagnostic(PCSTR Stage, HRESULT Status,
                                          const UINT *Values, UINT Count);

/* The WDDM 2.0 table starts with the D3D11 table's 152 entries. */
#define AGX_SAME_SLOT(field)                                                    \
   static_assert(offsetof(D3D11DDI_DEVICEFUNCS, field) ==                      \
                 offsetof(D3DWDDM2_0DDI_DEVICEFUNCS, field),                   \
                 "WDDM 2.0 table slot " #field " moved")
AGX_SAME_SLOT(pfnDefaultConstantBufferUpdateSubresourceUP);
AGX_SAME_SLOT(pfnFlush);
AGX_SAME_SLOT(pfnRelocateDeviceFuncs);
AGX_SAME_SLOT(pfnCreateRasterizerState);
AGX_SAME_SLOT(pfnCheckMultisampleQualityLevels);
AGX_SAME_SLOT(pfnCreateDomainShader);
AGX_SAME_SLOT(pfnCsSetConstantBuffers);
AGX_SAME_SLOT(pfnDispatchIndirect);
AGX_SAME_SLOT(pfnSetResourceMinLOD);
AGX_SAME_SLOT(pfnCopyStructureCount);
static_assert(D3D11DDI_MINOR_HEADER_VERSION >= 9, "the WDDM 2.0 table layout of header minor 9+");
static_assert(offsetof(D3DWDDM2_0DDI_DEVICEFUNCS, pfnDiscard) ==
              sizeof(D3D11DDI_DEVICEFUNCS),
              "the WDDM 2.0 additions follow the D3D11 table");

namespace {

/* The 11.0 entries the adapters call (identical for every device). */
D3D11DDI_DEVICEFUNCS base11;
std::once_flag base11_once;

void
refuse(D3D10DDI_HDEVICE hDevice, const char *stage, HRESULT hr, UINT detail)
{
   UINT values[1] = {detail};
   AgxD3d10WindowsDiagnostic(stage, hr, values, 1u);
   SetError(hDevice, hr);
}

/* ----- 11.1 constant-buffer ranges, copy and update flags ----- */

#define AGX_CB_RANGE(prefix, stage)                                              \
   void APIENTRY prefix##SetConstantBuffers1(D3D10DDI_HDEVICE hDevice, UINT start, \
      UINT n, const D3D10DDI_HRESOURCE *buffers, const UINT *first,               \
      const UINT *count)                                                           \
   {                                                                               \
      AgxSetConstantBuffersRange(stage, hDevice, start, n, buffers, first, count); \
   }
AGX_CB_RANGE(Vs, MESA_SHADER_VERTEX)
AGX_CB_RANGE(Ps, MESA_SHADER_FRAGMENT)
AGX_CB_RANGE(Gs, MESA_SHADER_GEOMETRY)
AGX_CB_RANGE(Hs, MESA_SHADER_TESS_CTRL)
AGX_CB_RANGE(Ds, MESA_SHADER_TESS_EVAL)
AGX_CB_RANGE(Cs, MESA_SHADER_COMPUTE)

/* D3D11_1_DDI_COPY_FLAGS (NO_OVERWRITE, DISCARD) are hints: the copies
 * are ordered with every earlier command either way. */
void APIENTRY
DefaultConstantBufferUpdateSubresourceUP1(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE hDst,
   UINT sub, const D3D10_DDI_BOX *box, const VOID *data, UINT row, UINT depth, UINT)
{
   base11.pfnDefaultConstantBufferUpdateSubresourceUP(hDevice, hDst, sub, box, data, row, depth);
}

void APIENTRY
ResourceUpdateSubresourceUP1(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE hDst, UINT sub,
   const D3D10_DDI_BOX *box, const VOID *data, UINT row, UINT depth, UINT)
{
   base11.pfnResourceUpdateSubresourceUP(hDevice, hDst, sub, box, data, row, depth);
}

/* CopySubresourceRegion1 may copy a subresource onto itself with
 * overlapping boxes (D3D11.1 CopyWithOverlap); the native copy has no
 * overlap guarantee, so an overlapping copy goes through a temporary. */
bool
boxes_overlap(LONG dx, LONG dy, LONG dz, const D3D10_DDI_BOX &src)
{
   LONG w = src.right - src.left, h = src.bottom - src.top, d = src.back - src.front;
   return dx < src.right && src.left < dx + w && dy < src.bottom && src.top < dy + h &&
          dz < src.back && src.front < dz + d;
}

void
copy_region(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE hDst, UINT dstSub, UINT dx, UINT dy,
            UINT dz, D3D10DDI_HRESOURCE hSrc, UINT srcSub, const D3D10_DDI_BOX *box,
            PFND3D10DDI_RESOURCECOPYREGION copy)
{
   Resource *dst = CastResource(hDst);
   if (hDst.pDrvPrivate != hSrc.pDrvPrivate || dstSub != srcSub || !dst || !dst->resource) {
      copy(hDevice, hDst, dstSub, dx, dy, dz, hSrc, srcSub, box);
      return;
   }
   struct pipe_resource *res = dst->resource;
   const UINT level = res->target == PIPE_BUFFER ? 0u : dstSub % (res->last_level + 1u);
   D3D10_DDI_BOX whole = {0, 0, 0, 1, 1, 1};
   whole.right = u_minify(res->width0, level);
   whole.bottom = res->target == PIPE_BUFFER ? 1u : u_minify(res->height0, level);
   whole.back = res->target == PIPE_TEXTURE_3D ? u_minify(res->depth0, level) : 1u;
   const D3D10_DDI_BOX src = box ? *box : whole;
   if (!boxes_overlap((LONG)dx, (LONG)dy, (LONG)dz, src)) {
      copy(hDevice, hDst, dstSub, dx, dy, dz, hSrc, srcSub, box);
      return;
   }
   Device *device = CastDevice(hDevice);
   struct pipe_context *pipe = device->pipe;
   struct pipe_resource templ = *res;
   templ.width0 = src.right - src.left;
   templ.height0 = (uint16_t)(src.bottom - src.top);
   templ.depth0 = (uint16_t)(src.back - src.front);
   templ.array_size = 1;
   templ.last_level = 0;
   templ.bind = 0;
   templ.usage = PIPE_USAGE_DEFAULT;
   templ.target = res->target == PIPE_BUFFER ? PIPE_BUFFER :
                  res->target == PIPE_TEXTURE_3D ? PIPE_TEXTURE_3D : PIPE_TEXTURE_2D;
   struct pipe_resource *tmp = pipe->screen->resource_create(pipe->screen, &templ);
   if (!tmp) {
      refuse(hDevice, "reject-wddm2-copy-overlap", E_OUTOFMEMORY, dstSub);
      return;
   }
   /* gallium addresses a 1D array layer with y and other array layers
    * and cube faces with z */
   const UINT layer = res->target == PIPE_BUFFER ? 0u : dstSub / (res->last_level + 1u);
   const bool array1d = res->target == PIPE_TEXTURE_1D_ARRAY;
   const bool layered = res->target == PIPE_TEXTURE_2D_ARRAY || res->target == PIPE_TEXTURE_CUBE ||
                        res->target == PIPE_TEXTURE_CUBE_ARRAY;
   const UINT sy = array1d ? layer : src.top, ty = array1d ? layer : dy;
   const UINT sz = layered ? layer : src.front, tz = layered ? layer : dz;
   struct pipe_box in;
   u_box_3d(src.left, sy, sz, templ.width0, templ.height0, templ.depth0, &in);
   pipe->resource_copy_region(pipe, tmp, 0, 0, 0, 0, res, level, &in);
   struct pipe_box out;
   u_box_3d(0, 0, 0, templ.width0, templ.height0, templ.depth0, &out);
   pipe->resource_copy_region(pipe, res, level, dx, ty, tz, tmp, 0, &out);
   pipe_resource_reference(&tmp, NULL);
}

void APIENTRY
ResourceCopyRegion1(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE hDst, UINT dstSub, UINT dx,
                    UINT dy, UINT dz, D3D10DDI_HRESOURCE hSrc, UINT srcSub,
                    const D3D10_DDI_BOX *box, UINT)
{
   copy_region(hDevice, hDst, dstSub, dx, dy, dz, hSrc, srcSub, box, base11.pfnResourceCopyRegion);
}

void APIENTRY
ResourceConvertRegion1(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE hDst, UINT dstSub, UINT dx,
                       UINT dy, UINT dz, D3D10DDI_HRESOURCE hSrc, UINT srcSub,
                       const D3D10_DDI_BOX *box, UINT)
{
   copy_region(hDevice, hDst, dstSub, dx, dy, dz, hSrc, srcSub, box,
               base11.pfnResourceConvertRegion);
}

/* D3D11_1_DDI_FLUSH_FLAGS only describe why the runtime flushes and the
 * device has one 3D context (D3DWDDM2_0DDI_CONTEXTTYPE_FLAG); TRUE reports
 * that the command buffer was submitted. */
BOOL APIENTRY
FlushWDDM2_0(D3D10DDI_HDEVICE hDevice, UINT, UINT)
{
   base11.pfnFlush(hDevice);
   return TRUE;
}

void APIENTRY
RelocateDeviceFuncsWDDM2_0(D3D10DDI_HDEVICE, struct D3DWDDM2_0DDI_DEVICEFUNCS *)
{
   /* the table holds no device data */
}

/* ----- 11.1 blend, WDDM 2.0 rasterizer ----- */

bool
blend10_1(D3D10DDI_HDEVICE hDevice, const D3D11_1_DDI_BLEND_DESC *in, D3D10_1_DDI_BLEND_DESC *out)
{
   if (!in) {
      refuse(hDevice, "reject-wddm2-blend", E_INVALIDARG, 0);
      return false;
   }
   out->AlphaToCoverageEnable = in->AlphaToCoverageEnable;
   out->IndependentBlendEnable = in->IndependentBlendEnable;
   for (UINT i = 0; i < D3D10_DDI_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
      const D3D11_1_DDI_RENDER_TARGET_BLEND_DESC &rt = in->RenderTarget[i];
      /* D3D11_1DDICAPS_D3D11_OPTIONS reports no output-merger logic ops */
      if (rt.LogicOpEnable) {
         refuse(hDevice, "reject-wddm2-logicop", E_NOTIMPL, i);
         return false;
      }
      out->RenderTarget[i].BlendEnable = rt.BlendEnable;
      out->RenderTarget[i].SrcBlend = rt.SrcBlend;
      out->RenderTarget[i].DestBlend = rt.DestBlend;
      out->RenderTarget[i].BlendOp = rt.BlendOp;
      out->RenderTarget[i].SrcBlendAlpha = rt.SrcBlendAlpha;
      out->RenderTarget[i].DestBlendAlpha = rt.DestBlendAlpha;
      out->RenderTarget[i].BlendOpAlpha = rt.BlendOpAlpha;
      out->RenderTarget[i].RenderTargetWriteMask = rt.RenderTargetWriteMask;
   }
   return true;
}

SIZE_T APIENTRY
CalcPrivateBlendStateSize1(D3D10DDI_HDEVICE hDevice, const D3D11_1_DDI_BLEND_DESC *desc)
{
   D3D10_1_DDI_BLEND_DESC blend = {};
   blend10_1(hDevice, desc, &blend);
   return base11.pfnCalcPrivateBlendStateSize(hDevice, &blend);
}

void APIENTRY
CreateBlendState1(D3D10DDI_HDEVICE hDevice, const D3D11_1_DDI_BLEND_DESC *desc,
                  D3D10DDI_HBLENDSTATE hState, D3D10DDI_HRTBLENDSTATE hRTState)
{
   D3D10_1_DDI_BLEND_DESC blend = {};
   if (blend10_1(hDevice, desc, &blend))
      base11.pfnCreateBlendState(hDevice, &blend, hState, hRTState);
}

bool
rasterizer10(D3D10DDI_HDEVICE hDevice, const D3DWDDM2_0DDI_RASTERIZER_DESC *in,
             D3D10_DDI_RASTERIZER_DESC *out)
{
   /* No target-independent rasterization (forced counts above 1) and no
    * conservative rasterization tier are reported. EXP1324: a forced count
    * of 1 rasterizes single-sampled without depth (D3D11_RASTERIZER_DESC1)
    * -- what single-sampled and attachment-less renders do; the FL 11_0
    * desktop creates such states and recreated its devices without end
    * while they were refused. */
   if (!in || in->ForcedSampleCount > 1u ||
       in->ConservativeRasterizationMode != D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_OFF) {
      refuse(hDevice, "reject-wddm2-rasterizer", E_NOTIMPL, in ? in->ForcedSampleCount : 0u);
      return false;
   }
   out->FillMode = in->FillMode;
   out->CullMode = in->CullMode;
   out->FrontCounterClockwise = in->FrontCounterClockwise;
   out->DepthBias = in->DepthBias;
   out->DepthBiasClamp = in->DepthBiasClamp;
   out->SlopeScaledDepthBias = in->SlopeScaledDepthBias;
   out->DepthClipEnable = in->DepthClipEnable;
   out->ScissorEnable = in->ScissorEnable;
   out->MultisampleEnable = in->MultisampleEnable;
   out->AntialiasedLineEnable = in->AntialiasedLineEnable;
   return true;
}

SIZE_T APIENTRY
CalcPrivateRasterizerStateSizeWDDM2_0(D3D10DDI_HDEVICE hDevice,
                                      const D3DWDDM2_0DDI_RASTERIZER_DESC *desc)
{
   D3D10_DDI_RASTERIZER_DESC rs = {};
   rasterizer10(hDevice, desc, &rs);
   return base11.pfnCalcPrivateRasterizerStateSize(hDevice, &rs);
}

void APIENTRY
CreateRasterizerStateWDDM2_0(D3D10DDI_HDEVICE hDevice, const D3DWDDM2_0DDI_RASTERIZER_DESC *desc,
                             D3D10DDI_HRASTERIZERSTATE hState, D3D10DDI_HRTRASTERIZERSTATE hRTState)
{
   D3D10_DDI_RASTERIZER_DESC rs = {};
   if (rasterizer10(hDevice, desc, &rs))
      base11.pfnCreateRasterizerState(hDevice, &rs, hState, hRTState);
}

/* ----- 11.1 shader signatures (entries gain component type, precision
 * and stream; the frontend reads system value, register and mask) ----- */

struct Signatures {
   std::vector<D3D10DDIARG_SIGNATURE_ENTRY> in, out, patch;
   D3D10DDIARG_STAGE_IO_SIGNATURES stage = {};
   D3D11DDIARG_TESSELLATION_IO_SIGNATURES tess = {};

   static void convert(std::vector<D3D10DDIARG_SIGNATURE_ENTRY> &dst,
                       const D3D11_1DDIARG_SIGNATURE_ENTRY2 *src, UINT n)
   {
      dst.resize(src ? n : 0u);
      for (UINT i = 0; src && i < n; ++i) {
         dst[i].SystemValue = src[i].SystemValue;
         dst[i].Register = src[i].Register;
         dst[i].Mask = src[i].Mask;
      }
   }
   const D3D10DDIARG_STAGE_IO_SIGNATURES *from(const D3D11_1DDIARG_STAGE_IO_SIGNATURES *s)
   {
      if (!s)
         return NULL;
      convert(in, s->pInputSignature, s->NumInputSignatureEntries);
      convert(out, s->pOutputSignature, s->NumOutputSignatureEntries);
      stage.pInputSignature = in.empty() ? NULL : in.data();
      stage.NumInputSignatureEntries = (UINT)in.size();
      stage.pOutputSignature = out.empty() ? NULL : out.data();
      stage.NumOutputSignatureEntries = (UINT)out.size();
      return &stage;
   }
   const D3D11DDIARG_TESSELLATION_IO_SIGNATURES *
   from(const D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES *s)
   {
      if (!s)
         return NULL;
      convert(in, s->pInputSignature, s->NumInputSignatureEntries);
      convert(out, s->pOutputSignature, s->NumOutputSignatureEntries);
      convert(patch, s->pPatchConstantSignature, s->NumPatchConstantSignatureEntries);
      tess.pInputSignature = in.empty() ? NULL : in.data();
      tess.NumInputSignatureEntries = (UINT)in.size();
      tess.pOutputSignature = out.empty() ? NULL : out.data();
      tess.NumOutputSignatureEntries = (UINT)out.size();
      tess.pPatchConstantSignature = patch.empty() ? NULL : patch.data();
      tess.NumPatchConstantSignatureEntries = (UINT)patch.size();
      return &tess;
   }
};

SIZE_T APIENTRY
CalcPrivateShaderSize1(D3D10DDI_HDEVICE hDevice, const UINT *code,
                       const D3D11_1DDIARG_STAGE_IO_SIGNATURES *sig)
{
   Signatures s;
   return base11.pfnCalcPrivateShaderSize(hDevice, code, s.from(sig));
}

#define AGX_SHADER1(name)                                                           \
   void APIENTRY name##1(D3D10DDI_HDEVICE hDevice, const UINT *code,                 \
      D3D10DDI_HSHADER hShader, D3D10DDI_HRTSHADER hRTShader,                        \
      const D3D11_1DDIARG_STAGE_IO_SIGNATURES *sig)                                  \
   {                                                                                 \
      Signatures s;                                                                  \
      base11.pfn##name(hDevice, code, hShader, hRTShader, s.from(sig));              \
   }
AGX_SHADER1(CreateVertexShader)
AGX_SHADER1(CreateGeometryShader)
AGX_SHADER1(CreatePixelShader)

SIZE_T APIENTRY
CalcPrivateGeometryShaderWithStreamOutput1(D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *args,
   const D3D11_1DDIARG_STAGE_IO_SIGNATURES *sig)
{
   Signatures s;
   return base11.pfnCalcPrivateGeometryShaderWithStreamOutput(hDevice, args, s.from(sig));
}

void APIENTRY
CreateGeometryShaderWithStreamOutput1(D3D10DDI_HDEVICE hDevice,
   const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *args, D3D10DDI_HSHADER hShader,
   D3D10DDI_HRTSHADER hRTShader, const D3D11_1DDIARG_STAGE_IO_SIGNATURES *sig)
{
   Signatures s;
   base11.pfnCreateGeometryShaderWithStreamOutput(hDevice, args, hShader, hRTShader, s.from(sig));
}

SIZE_T APIENTRY
CalcPrivateTessellationShaderSize1(D3D10DDI_HDEVICE hDevice, const UINT *code,
                                   const D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES *sig)
{
   Signatures s;
   return base11.pfnCalcPrivateTessellationShaderSize(hDevice, code, s.from(sig));
}

#define AGX_TESS1(name)                                                             \
   void APIENTRY name##1(D3D10DDI_HDEVICE hDevice, const UINT *code,                 \
      D3D10DDI_HSHADER hShader, D3D10DDI_HRTSHADER hRTShader,                        \
      const D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES *sig)                           \
   {                                                                                 \
      Signatures s;                                                                  \
      base11.pfn##name(hDevice, code, hShader, hRTShader, s.from(sig));              \
   }
AGX_TESS1(CreateHullShader)
AGX_TESS1(CreateDomainShader)

/* ----- WDDM 1.3 multisample flags ----- */

void APIENTRY
CheckMultisampleQualityLevelsWDDM1_3(D3D10DDI_HDEVICE hDevice, DXGI_FORMAT format, UINT samples,
                                     UINT flags, UINT *levels)
{
   /* tiled resources are not supported: no quality levels for them */
   if (flags & D3DWDDM1_3DDI_CHECK_MULTISAMPLE_QUALITY_LEVELS_TILED_RESOURCE) {
      if (levels)
         *levels = 0;
      return;
   }
   base11.pfnCheckMultisampleQualityLevels(hDevice, format, samples, levels);
}

/* ----- WDDM 2.0 views and queries (plane slices only on planar formats,
 * which are not supported) ----- */

bool
srv11(D3D10DDI_HDEVICE hDevice, const D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW *in,
      D3D11DDIARG_CREATESHADERRESOURCEVIEW *out)
{
   if (!in) {
      refuse(hDevice, "reject-wddm2-srv", E_INVALIDARG, 0);
      return false;
   }
   out->hDrvResource = in->hDrvResource;
   out->Format = in->Format;
   out->ResourceDimension = in->ResourceDimension;
   switch (in->ResourceDimension) {
   case D3D10DDIRESOURCE_BUFFER: out->Buffer = in->Buffer; break;
   case D3D10DDIRESOURCE_TEXTURE1D: out->Tex1D = in->Tex1D; break;
   case D3D10DDIRESOURCE_TEXTURE2D:
      if (in->Tex2D.PlaneSlice) {
         refuse(hDevice, "reject-wddm2-srv-plane", E_NOTIMPL, in->Tex2D.PlaneSlice);
         return false;
      }
      out->Tex2D.MostDetailedMip = in->Tex2D.MostDetailedMip;
      out->Tex2D.FirstArraySlice = in->Tex2D.FirstArraySlice;
      out->Tex2D.MipLevels = in->Tex2D.MipLevels;
      out->Tex2D.ArraySize = in->Tex2D.ArraySize;
      break;
   case D3D10DDIRESOURCE_TEXTURE3D: out->Tex3D = in->Tex3D; break;
   case D3D10DDIRESOURCE_TEXTURECUBE: out->TexCube = in->TexCube; break;
   case D3D11DDIRESOURCE_BUFFEREX: out->BufferEx = in->BufferEx; break;
   default:
      refuse(hDevice, "reject-wddm2-srv", E_INVALIDARG, in->ResourceDimension);
      return false;
   }
   return true;
}

SIZE_T APIENTRY
CalcPrivateShaderResourceViewSizeWDDM2_0(D3D10DDI_HDEVICE hDevice,
                                         const D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW *in)
{
   D3D11DDIARG_CREATESHADERRESOURCEVIEW view = {};
   srv11(hDevice, in, &view);
   return base11.pfnCalcPrivateShaderResourceViewSize(hDevice, &view);
}

void APIENTRY
CreateShaderResourceViewWDDM2_0(D3D10DDI_HDEVICE hDevice,
                                const D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW *in,
                                D3D10DDI_HSHADERRESOURCEVIEW hView,
                                D3D10DDI_HRTSHADERRESOURCEVIEW hRTView)
{
   D3D11DDIARG_CREATESHADERRESOURCEVIEW view = {};
   if (srv11(hDevice, in, &view))
      base11.pfnCreateShaderResourceView(hDevice, &view, hView, hRTView);
}

bool
rtv10(D3D10DDI_HDEVICE hDevice, const D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW *in,
      D3D10DDIARG_CREATERENDERTARGETVIEW *out)
{
   if (!in) {
      refuse(hDevice, "reject-wddm2-rtv", E_INVALIDARG, 0);
      return false;
   }
   out->hDrvResource = in->hDrvResource;
   out->Format = in->Format;
   out->ResourceDimension = in->ResourceDimension;
   switch (in->ResourceDimension) {
   case D3D10DDIRESOURCE_BUFFER: out->Buffer = in->Buffer; break;
   case D3D10DDIRESOURCE_TEXTURE1D: out->Tex1D = in->Tex1D; break;
   case D3D10DDIRESOURCE_TEXTURE2D:
      if (in->Tex2D.PlaneSlice) {
         refuse(hDevice, "reject-wddm2-rtv-plane", E_NOTIMPL, in->Tex2D.PlaneSlice);
         return false;
      }
      out->Tex2D.MipSlice = in->Tex2D.MipSlice;
      out->Tex2D.FirstArraySlice = in->Tex2D.FirstArraySlice;
      out->Tex2D.ArraySize = in->Tex2D.ArraySize;
      break;
   case D3D10DDIRESOURCE_TEXTURE3D: out->Tex3D = in->Tex3D; break;
   case D3D10DDIRESOURCE_TEXTURECUBE: out->TexCube = in->TexCube; break;
   default:
      refuse(hDevice, "reject-wddm2-rtv", E_INVALIDARG, in->ResourceDimension);
      return false;
   }
   return true;
}

SIZE_T APIENTRY
CalcPrivateRenderTargetViewSizeWDDM2_0(D3D10DDI_HDEVICE hDevice,
                                       const D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW *in)
{
   D3D10DDIARG_CREATERENDERTARGETVIEW view = {};
   rtv10(hDevice, in, &view);
   return base11.pfnCalcPrivateRenderTargetViewSize(hDevice, &view);
}

void APIENTRY
CreateRenderTargetViewWDDM2_0(D3D10DDI_HDEVICE hDevice,
                              const D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW *in,
                              D3D10DDI_HRENDERTARGETVIEW hView, D3D10DDI_HRTRENDERTARGETVIEW hRTView)
{
   D3D10DDIARG_CREATERENDERTARGETVIEW view = {};
   if (rtv10(hDevice, in, &view))
      base11.pfnCreateRenderTargetView(hDevice, &view, hView, hRTView);
}

bool
uav11(D3D10DDI_HDEVICE hDevice, const D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW *in,
      D3D11DDIARG_CREATEUNORDEREDACCESSVIEW *out)
{
   if (!in) {
      refuse(hDevice, "reject-wddm2-uav", E_INVALIDARG, 0);
      return false;
   }
   out->hDrvResource = in->hDrvResource;
   out->Format = in->Format;
   out->ResourceDimension = in->ResourceDimension;
   switch (in->ResourceDimension) {
   case D3D10DDIRESOURCE_BUFFER: out->Buffer = in->Buffer; break;
   case D3D10DDIRESOURCE_TEXTURE1D: out->Tex1D = in->Tex1D; break;
   case D3D10DDIRESOURCE_TEXTURE2D:
      if (in->Tex2D.PlaneSlice) {
         refuse(hDevice, "reject-wddm2-uav-plane", E_NOTIMPL, in->Tex2D.PlaneSlice);
         return false;
      }
      out->Tex2D.MipSlice = in->Tex2D.MipSlice;
      out->Tex2D.FirstArraySlice = in->Tex2D.FirstArraySlice;
      out->Tex2D.ArraySize = in->Tex2D.ArraySize;
      break;
   case D3D10DDIRESOURCE_TEXTURE3D: out->Tex3D = in->Tex3D; break;
   default:
      refuse(hDevice, "reject-wddm2-uav", E_INVALIDARG, in->ResourceDimension);
      return false;
   }
   return true;
}

SIZE_T APIENTRY
CalcPrivateUnorderedAccessViewSizeWDDM2_0(D3D10DDI_HDEVICE hDevice,
                                          const D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW *in)
{
   D3D11DDIARG_CREATEUNORDEREDACCESSVIEW view = {};
   uav11(hDevice, in, &view);
   return base11.pfnCalcPrivateUnorderedAccessViewSize(hDevice, &view);
}

void APIENTRY
CreateUnorderedAccessViewWDDM2_0(D3D10DDI_HDEVICE hDevice,
                                 const D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW *in,
                                 D3D11DDI_HUNORDEREDACCESSVIEW hView,
                                 D3D11DDI_HRTUNORDEREDACCESSVIEW hRTView)
{
   D3D11DDIARG_CREATEUNORDEREDACCESSVIEW view = {};
   if (uav11(hDevice, in, &view))
      base11.pfnCreateUnorderedAccessView(hDevice, &view, hView, hRTView);
}

SIZE_T APIENTRY
CalcPrivateQuerySizeWDDM2_0(D3D10DDI_HDEVICE hDevice, const D3DWDDM2_0DDIARG_CREATEQUERY *in)
{
   D3D10DDIARG_CREATEQUERY query = {};
   if (in) {
      query.Query = in->Query;
      query.MiscFlags = in->MiscFlags;
   }
   return base11.pfnCalcPrivateQuerySize(hDevice, &query);
}

void APIENTRY
CreateQueryWDDM2_0(D3D10DDI_HDEVICE hDevice, const D3DWDDM2_0DDIARG_CREATEQUERY *in,
                   D3D10DDI_HQUERY hQuery, D3D10DDI_HRTQUERY hRTQuery)
{
   if (!in) {
      refuse(hDevice, "reject-wddm2-query", E_INVALIDARG, 0);
      return;
   }
   D3D10DDIARG_CREATEQUERY query = {in->Query, in->MiscFlags};
   base11.pfnCreateQuery(hDevice, &query, hQuery, hRTQuery);
}

/* ----- 11.1 additions ----- */

/* DiscardResource/DiscardView only allow the contents to become undefined */
void APIENTRY
Discard(D3D10DDI_HDEVICE, D3D11DDI_HANDLETYPE, VOID *, const D3D10_DDI_RECT *, UINT)
{
}

void APIENTRY
AssignDebugBinary(D3D10DDI_HDEVICE, D3D10DDI_HSHADER, UINT, const VOID *)
{
   /* D3D11_1DDICAPS_D3D11_OPTIONS reports no AssignDebugBinarySupport */
}

void APIENTRY
DynamicConstantBufferMapNoOverwrite(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE hResource,
                                    UINT sub, D3D10_DDI_MAP map, UINT flags,
                                    D3D10DDI_MAPPED_SUBRESOURCE *mapped)
{
   base11.pfnResourceMap(hDevice, hResource, sub, map, flags, mapped);
}

void APIENTRY
CheckDirectFlipSupport(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, D3D10DDI_HRESOURCE, UINT,
                       BOOL *supported)
{
   if (supported)
      *supported = FALSE;
}

/* ClearView: render-target and unordered-access views, whole or by
 * rectangles (clipped to the view by the runtime). */
void APIENTRY
ClearView(D3D10DDI_HDEVICE hDevice, D3D11DDI_HANDLETYPE type, VOID *hView, const FLOAT color[4],
          const D3D10_DDI_RECT *rects, UINT nRects)
{
   if (!hView || !color) {
      refuse(hDevice, "reject-wddm2-clearview", E_INVALIDARG, (UINT)type);
      return;
   }
   if (type == D3D10DDI_HT_RENDERTARGETVIEW) {
      D3D10DDI_HRENDERTARGETVIEW rtv = {hView};
      if (!nRects) {
         base11.pfnClearRenderTargetView(hDevice, rtv, (FLOAT *)color);
         return;
      }
      Device *device = CastDevice(hDevice);
      struct pipe_surface *surface = &CastRenderTargetView(rtv)->surface;
      union pipe_color_union value;
      const bool integer = util_format_is_pure_integer(surface->format);
      const bool sint = util_format_is_pure_sint(surface->format);
      for (UINT i = 0; i < 4; ++i) {
         const float c = color[i];
         if (!integer)
            value.f[i] = c;
         else if (sint)
            value.i[i] = c != c ? 0 : c <= -2147483648.0f ? INT32_MIN :
                         c >= 2147483647.0f ? INT32_MAX : (int)c;
         else
            value.ui[i] = !(c > 0.0f) ? 0u : c >= 4294967295.0f ? UINT32_MAX : (unsigned)c;
      }
      for (UINT i = 0; rects && i < nRects; ++i) {
         const D3D10_DDI_RECT &r = rects[i];
         if (r.right <= r.left || r.bottom <= r.top)
            continue;
         device->pipe->clear_render_target(device->pipe, surface, &value, r.left, r.top,
                                           r.right - r.left, r.bottom - r.top, false);
      }
      return;
   }
   if (type == D3D11DDI_HT_UNORDEREDACCESSVIEW && !nRects) {
      D3D11DDI_HUNORDEREDACCESSVIEW uav = {hView};
      base11.pfnClearUnorderedAccessViewFloat(hDevice, uav, color);
      return;
   }
   /* UAV rectangles and video views are not implemented */
   refuse(hDevice, "reject-wddm2-clearview", E_NOTIMPL, (UINT)type);
}

/* ----- entries whose caps are reported absent: fail closed ----- */

void APIENTRY
UpdateTileMappings(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE, UINT,
                   const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE *,
                   const D3DWDDM1_3DDI_TILE_REGION_SIZE *, D3D10DDI_HRESOURCE, UINT, const UINT *,
                   const UINT *, const UINT *, UINT)
{
   refuse(hDevice, "reject-wddm2-tiled", E_NOTIMPL, 1);
}

void APIENTRY
CopyTileMappings(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE,
                 const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE *, D3D10DDI_HRESOURCE,
                 const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE *,
                 const D3DWDDM1_3DDI_TILE_REGION_SIZE *, UINT)
{
   refuse(hDevice, "reject-wddm2-tiled", E_NOTIMPL, 2);
}

void APIENTRY
CopyTiles(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE,
          const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE *, const D3DWDDM1_3DDI_TILE_REGION_SIZE *,
          D3D10DDI_HRESOURCE, UINT64, UINT)
{
   refuse(hDevice, "reject-wddm2-tiled", E_NOTIMPL, 3);
}

void APIENTRY
UpdateTiles(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE,
            const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE *,
            const D3DWDDM1_3DDI_TILE_REGION_SIZE *, const VOID *, UINT)
{
   refuse(hDevice, "reject-wddm2-tiled", E_NOTIMPL, 4);
}

void APIENTRY
TiledResourceBarrier(D3D10DDI_HDEVICE hDevice, D3D11DDI_HANDLETYPE, VOID *, D3D11DDI_HANDLETYPE,
                     VOID *)
{
   refuse(hDevice, "reject-wddm2-tiled", E_NOTIMPL, 5);
}

void APIENTRY
GetMipPacking(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE, UINT *packed, UINT *tiles)
{
   if (packed)
      *packed = 0;
   if (tiles)
      *tiles = 0;
   refuse(hDevice, "reject-wddm2-tiled", E_NOTIMPL, 6);
}

void APIENTRY
ResizeTilePool(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE, UINT64)
{
   refuse(hDevice, "reject-wddm2-tiled", E_NOTIMPL, 7);
}

/* D3DWDDM1_3DDICAPS_MARKER reports D3DWDDM1_3DDI_MARKER_TYPE_NONE */
void APIENTRY
SetMarker(D3D10DDI_HDEVICE)
{
}

void APIENTRY
SetMarkerMode(D3D10DDI_HDEVICE hDevice, D3DWDDM1_3DDI_MARKER_TYPE type, UINT)
{
   if (type != D3DWDDM1_3DDI_MARKER_TYPE_NONE)
      refuse(hDevice, "reject-wddm2-marker", E_NOTIMPL, (UINT)type);
}

void APIENTRY
SetHardwareProtection(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE, BOOL enable)
{
   if (enable)
      refuse(hDevice, "reject-wddm2-protection", E_NOTIMPL, 1);
}

void APIENTRY
GetResourceLayout(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE, UINT, D3DKMT_HANDLE *,
                  D3DWDDM2_0DDI_TEXTURE_LAYOUT *,
#if D3D11DDI_MINOR_HEADER_VERSION >= 6
                  UINT *,
#endif
                  D3DWDDM2_0DDI_SUBRESOURCE_LAYOUT *)
{
   /* D3DWDDM2_0DDICAPS_TEXTURE_LAYOUT reports no device-dependent layout */
   refuse(hDevice, "reject-wddm2-layout", E_NOTIMPL, 0);
}

/* no shader comments are kept (shader debugging is not reported) */
HRESULT APIENTRY
RetrieveShaderComment(D3D10DDI_HDEVICE, D3D10DDI_HSHADER, WCHAR *buffer, SIZE_T *count)
{
   if (buffer && count && *count)
      buffer[0] = 0;
   if (count)
      *count = 1;
   return S_OK;
}

void APIENTRY
SetHardwareProtectionState(D3D10DDI_HDEVICE hDevice, BOOL enable)
{
   if (enable)
      refuse(hDevice, "reject-wddm2-protection", E_NOTIMPL, 2);
}

} /* namespace */

void
AgxWddm2_0FillDeviceFuncs(D3DWDDM2_0DDI_DEVICEFUNCS *f, const D3D10DDI_DEVICEFUNCS *base)
{
   /* the 152-entry prefix is the D3D11 table (slot asserts above) */
   AgxD3d11FillDeviceFuncs(reinterpret_cast<D3D11DDI_DEVICEFUNCS *>(f), base);
   std::call_once(base11_once, [f]() {
      base11 = *reinterpret_cast<const D3D11DDI_DEVICEFUNCS *>(f);
   });

   f->pfnDefaultConstantBufferUpdateSubresourceUP = DefaultConstantBufferUpdateSubresourceUP1;
   f->pfnVsSetConstantBuffers = VsSetConstantBuffers1;
   f->pfnPsSetConstantBuffers = PsSetConstantBuffers1;
   f->pfnGsSetConstantBuffers = GsSetConstantBuffers1;
   f->pfnHsSetConstantBuffers = HsSetConstantBuffers1;
   f->pfnDsSetConstantBuffers = DsSetConstantBuffers1;
   f->pfnCsSetConstantBuffers = CsSetConstantBuffers1;
   f->pfnResourceCopyRegion = ResourceCopyRegion1;
   f->pfnResourceConvertRegion = ResourceConvertRegion1;
   f->pfnResourceUpdateSubresourceUP = ResourceUpdateSubresourceUP1;
   f->pfnFlush = FlushWDDM2_0;
   f->pfnRelocateDeviceFuncs = RelocateDeviceFuncsWDDM2_0;
   f->pfnCalcPrivateBlendStateSize = CalcPrivateBlendStateSize1;
   f->pfnCreateBlendState = CreateBlendState1;
   f->pfnCalcPrivateRasterizerStateSize = CalcPrivateRasterizerStateSizeWDDM2_0;
   f->pfnCreateRasterizerState = CreateRasterizerStateWDDM2_0;
   f->pfnCalcPrivateShaderSize = CalcPrivateShaderSize1;
   f->pfnCreateVertexShader = CreateVertexShader1;
   f->pfnCreateGeometryShader = CreateGeometryShader1;
   f->pfnCreatePixelShader = CreatePixelShader1;
   f->pfnCalcPrivateGeometryShaderWithStreamOutput = CalcPrivateGeometryShaderWithStreamOutput1;
   f->pfnCreateGeometryShaderWithStreamOutput = CreateGeometryShaderWithStreamOutput1;
   f->pfnCheckMultisampleQualityLevels = CheckMultisampleQualityLevelsWDDM1_3;
   f->pfnCalcPrivateShaderResourceViewSize = CalcPrivateShaderResourceViewSizeWDDM2_0;
   f->pfnCreateShaderResourceView = CreateShaderResourceViewWDDM2_0;
   f->pfnCalcPrivateRenderTargetViewSize = CalcPrivateRenderTargetViewSizeWDDM2_0;
   f->pfnCreateRenderTargetView = CreateRenderTargetViewWDDM2_0;
   f->pfnCalcPrivateQuerySize = CalcPrivateQuerySizeWDDM2_0;
   f->pfnCreateQuery = CreateQueryWDDM2_0;
   f->pfnCalcPrivateUnorderedAccessViewSize = CalcPrivateUnorderedAccessViewSizeWDDM2_0;
   f->pfnCreateUnorderedAccessView = CreateUnorderedAccessViewWDDM2_0;
   f->pfnCreateHullShader = CreateHullShader1;
   f->pfnCreateDomainShader = CreateDomainShader1;
   f->pfnCalcPrivateTessellationShaderSize = CalcPrivateTessellationShaderSize1;

   f->pfnDiscard = Discard;
   f->pfnAssignDebugBinary = AssignDebugBinary;
   f->pfnDynamicConstantBufferMapNoOverwrite = DynamicConstantBufferMapNoOverwrite;
   f->pfnCheckDirectFlipSupport = CheckDirectFlipSupport;
   f->pfnClearView = ClearView;
   f->pfnUpdateTileMappings = UpdateTileMappings;
   f->pfnCopyTileMappings = CopyTileMappings;
   f->pfnCopyTiles = CopyTiles;
   f->pfnUpdateTiles = UpdateTiles;
   f->pfnTiledResourceBarrier = TiledResourceBarrier;
   f->pfnGetMipPacking = GetMipPacking;
   f->pfnResizeTilePool = ResizeTilePool;
   f->pfnSetMarker = SetMarker;
   f->pfnSetMarkerMode = SetMarkerMode;
   f->pfnSetHardwareProtection = SetHardwareProtection;
   f->pfnGetResourceLayout = GetResourceLayout;
   f->pfnRetrieveShaderComment = RetrieveShaderComment;
   f->pfnSetHardwareProtectionState = SetHardwareProtectionState;
}

/* ----- DXGI 1.2 to 1.4 base functions (IS_DXGI1_4_BASE_FUNCTIONS holds for
 * the WDDM 2.0 interface; the first eight entries are the DXGI 1.1 ones
 * the frontend already wrote into the same table) ----- */

namespace {

DXGI1_4_DDI_BASE_FUNCTIONS dxgi11;
std::once_flag dxgi11_once;

void
dxgi_receipt(const char *stage, HRESULT hr, UINT a, UINT b)
{
   UINT values[2] = {a, b};
   AgxD3d10WindowsDiagnostic(stage, hr, values, 2u);
}

/* Blt1 adds a source rectangle; the whole source subresource is Blt. */
HRESULT APIENTRY
Blt1(DXGI_DDI_ARG_BLT1 *args)
{
   if (!args)
      return E_INVALIDARG;
   D3D10DDI_HRESOURCE hSrc = {(void *)args->hSrcResource};
   Resource *src = CastResource(hSrc);
   struct pipe_resource *res = src ? src->resource : NULL;
   const UINT level = res ? args->SrcSubresource % (res->last_level + 1u) : 0u;
   const bool whole = res && args->SrcLeft == 0 && args->SrcTop == 0 &&
                      args->SrcRight == u_minify(res->width0, level) &&
                      args->SrcBottom == u_minify(res->height0, level);
   if (!whole) {
      dxgi_receipt("reject-dxgi-blt1-subrect", E_NOTIMPL, args->SrcRight - args->SrcLeft,
                   args->SrcBottom - args->SrcTop);
      return E_NOTIMPL;
   }
   DXGI_DDI_ARG_BLT blt = {};
   blt.hDevice = args->hDevice;
   blt.hDstResource = args->hDstResource;
   blt.DstSubresource = args->DstSubresource;
   blt.DstLeft = args->DstLeft;
   blt.DstTop = args->DstTop;
   blt.DstRight = args->DstRight;
   blt.DstBottom = args->DstBottom;
   blt.hSrcResource = args->hSrcResource;
   blt.SrcSubresource = args->SrcSubresource;
   blt.Flags = args->Flags;
   blt.Rotate = args->Rotate;
   return dxgi11.pfnBlt(&blt);
}

/* Present1 presents one or more surfaces; one surface is Present. EXP1328:
 * the FL 11_0 desktop (LogonUI, explorer, StartMenuExperienceHost) presents
 * two to four composition surfaces at once. DXGI_DDI_ARG_PRESENT1: with
 * several surfaces hDstResource is NULL and the driver passes only the last
 * source to pfnPresentCb; the others are made visible with it. The checks
 * are Present's for every surface. */
HRESULT APIENTRY
Present1(DXGI_DDI_ARG_PRESENT1 *args)
{
   if (!args)
      return E_INVALIDARG;
   if (args->SurfacesToPresent && !args->phSurfacesToPresent) {
      dxgi_receipt("reject-dxgi-present1", E_INVALIDARG, args->SurfacesToPresent, 0);
      return E_INVALIDARG;
   }
   if (args->SurfacesToPresent > 1) {
      Device *device = CastDevice(args->hDevice);
      bool valid = device && args->hDstResource == 0 &&
         (args->Flags.Value == 0x1u || args->Flags.Value == 0x2u) &&
         (args->FlipInterval == DXGI_DDI_FLIP_INTERVAL_IMMEDIATE ||
          args->FlipInterval == DXGI_DDI_FLIP_INTERVAL_ONE);
      std::vector<AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *> surfaces;
      for (UINT i = 0; valid && i < args->SurfacesToPresent; ++i) {
         Resource *resource = CastResource(args->phSurfacesToPresent[i].hSurface);
         valid = resource && resource->owner_device == device &&
            resource->presentation && args->phSurfacesToPresent[i].SubResourceIndex == 0;
         if (valid)
            surfaces.push_back(resource->presentation);
      }
      if (!valid) {
         dxgi_receipt("reject-dxgi-present1", E_INVALIDARG, args->SurfacesToPresent,
                      args->Flags.Value);
         return E_INVALIDARG;
      }
      return AgxD3d10WindowsPresentationSubmitSurfaces(
         device->windows, surfaces.data(), (UINT)surfaces.size(), args->pDXGIContext);
   }
   DXGI_DDI_ARG_PRESENT present = {};
   present.hDevice = args->hDevice;
   if (args->SurfacesToPresent) {
      present.hSurfaceToPresent = args->phSurfacesToPresent[0].hSurface;
      present.SrcSubResourceIndex = args->phSurfacesToPresent[0].SubResourceIndex;
   }
   present.hDstResource = args->hDstResource;
   present.DstSubResourceIndex = args->DstSubResourceIndex;
   present.pDXGIContext = args->pDXGIContext;
   present.Flags = args->Flags;
   present.FlipInterval = args->FlipInterval;
   return dxgi11.pfnPresent(&present);
}

/* Offered resources are kept: a reclaim never reports them discarded. */
HRESULT APIENTRY
OfferResources(DXGI_DDI_ARG_OFFERRESOURCES *args)
{
   return args ? S_OK : E_INVALIDARG;
}

HRESULT APIENTRY
ReclaimResources(DXGI_DDI_ARG_RECLAIMRESOURCES *args)
{
   if (!args)
      return E_INVALIDARG;
   for (UINT i = 0; args->pDiscarded && i < args->Resources; ++i)
      args->pDiscarded[i] = FALSE;
   return S_OK;
}

/* No multiplane overlays: the primary plane only. */
HRESULT APIENTRY
GetMultiplaneOverlayCaps(DXGI_DDI_ARG_GETMULTIPLANEOVERLAYCAPS *args)
{
   if (!args)
      return E_INVALIDARG;
   args->MultiplaneOverlayCaps.MaxPlanes = 1;
   args->MultiplaneOverlayCaps.NumCapabilityGroups = 0;
   return S_OK;
}

HRESULT APIENTRY
GetMultiplaneOverlayGroupCaps(DXGI_DDI_ARG_GETMULTIPLANEOVERLAYGROUPCAPS *args)
{
   dxgi_receipt("reject-dxgi-mpo-group", E_INVALIDARG, args ? args->GroupIndex : ~0u, 0);
   return E_INVALIDARG;
}

HRESULT APIENTRY
PresentMultiplaneOverlay(DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY *args)
{
   dxgi_receipt("reject-dxgi-mpo-present", E_NOTIMPL, args ? args->PresentPlaneCount : ~0u, 0);
   return E_NOTIMPL;
}

HRESULT APIENTRY
PresentMultiplaneOverlay1(DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY1 *args)
{
   dxgi_receipt("reject-dxgi-mpo-present1", E_NOTIMPL, args != NULL, 1);
   return E_NOTIMPL;
}

HRESULT APIENTRY
CheckMultiplaneOverlayColorSpaceSupport(DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYCOLORSPACESUPPORT *args)
{
   if (!args)
      return E_INVALIDARG;
   args->Supported = FALSE;
   return S_OK;
}

/* No custom present durations: both neighbours 0 (none). */
HRESULT APIENTRY
CheckPresentDurationSupport(DXGI_DDI_ARG_CHECKPRESENTDURATIONSUPPORT *args)
{
   if (!args)
      return E_INVALIDARG;
   args->ClosestSmallerDuration = 0;
   args->ClosestLargerDuration = 0;
   return S_OK;
}

/* Residency trimming is a request to free memory; the native residency
 * keeps its own budget (MakeResident/Evict), nothing is trimmed here. */
HRESULT APIENTRY
TrimResidencySet(DXGI_DDI_ARG_TRIMRESIDENCYSET *args)
{
   return args ? S_OK : E_INVALIDARG;
}

} /* namespace */

void
AgxWddm2_0FillDxgiFuncs(DXGI1_4_DDI_BASE_FUNCTIONS *f)
{
   std::call_once(dxgi11_once, [f]() { dxgi11 = *f; });
   f->pfnBlt1 = Blt1;
   f->pfnOfferResources = OfferResources;
   f->pfnReclaimResources = ReclaimResources;
   f->pfnGetMultiplaneOverlayCaps = GetMultiplaneOverlayCaps;
   f->pfnGetMultiplaneOverlayGroupCaps = GetMultiplaneOverlayGroupCaps;
   f->pfnReserved1 = NULL;
   f->pfnPresentMultiplaneOverlay = PresentMultiplaneOverlay;
   f->pfnReserved2 = NULL;
   f->pfnPresent1 = Present1;
   f->pfnCheckPresentDurationSupport = CheckPresentDurationSupport;
   f->pfnTrimResidencySet = TrimResidencySet;
   f->pfnCheckMultiplaneOverlayColorSpaceSupport = CheckMultiplaneOverlayColorSpaceSupport;
   f->pfnPresentMultiplaneOverlay1 = PresentMultiplaneOverlay1;
}

/* PFND3D10DDI_RETRIEVESUBOBJECT: no video device (D3D11_1DDI_VIDEO_FUNCTIONS) */
HRESULT APIENTRY
AgxWddm2_0RetrieveSubObject(D3D10DDI_HDEVICE, UINT32 id, SIZE_T, void *, SIZE_T, void *)
{
   UINT values[1] = {id};
   AgxD3d10WindowsDiagnostic("measure-wddm2-retrieve-subobject", E_NOTIMPL, values, 1u);
   return E_NOTIMPL;
}
