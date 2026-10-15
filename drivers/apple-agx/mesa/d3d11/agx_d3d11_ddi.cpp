/*
 * D3D-H2a: the D3D11 DDI device function table over the D3D10 frontend
 * (investigation/analysis/D3D-H2-d3d11-ddi.md).
 *
 * The 105 entries whose types the D3D11 table shares with D3D10 use the
 * frontend's 10.0 implementations; the two 10.1-typed blend entries use
 * its 10.1 ones; the D3D11-typed entries with a 10.x meaning are adapters
 * onto the same implementations, refusing only argument fields whose
 * behaviour is not implemented; compute, UAVs, tessellation and indirect
 * draws fail closed (the capabilities report FL 10_0 and no compute, so the
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

#include "agx_d3d11_ddi.h"

extern "C" VOID AgxD3d10WindowsDiagnostic(PCSTR Stage, HRESULT Status,
                                          const UINT *Values, UINT Count);
extern "C" BOOL AgxD3d10WindowsDiagnosticSwitch(const char *Name);

BOOL
AgxD3d11DdiEnabled(void)
{
   return AgxD3d10WindowsDiagnosticSwitch("APPLE_AGX_D3D11_DDI");
}

/* The D3D11 binding and misc flags without a 10.x behaviour here. */
#define AGX_D3D11_UNSUPPORTED_BIND (~(UINT)D3D10_DDI_BIND_MASK)
#define AGX_D3D11_UNSUPPORTED_MISC \
   (D3D11_DDI_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_DDI_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS | \
    D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED | D3D11_DDI_RESOURCE_MISC_RESOURCE_CLAMP)

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
resource_10x(const D3D11DDIARG_CREATERESOURCE *args)
{
   return args && !(args->BindFlags & AGX_D3D11_UNSUPPORTED_BIND) &&
          !(args->MiscFlags & AGX_D3D11_UNSUPPORTED_MISC) && !args->ByteStride;
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
   if (!resource_10x(args)) {
      refuse(hDevice, "reject-d3d11-create-resource", args ? args->BindFlags : ~0u,
             args ? args->MiscFlags : ~0u);
      return;
   }
   CreateResource(hDevice, (const D3D10DDIARG_CREATERESOURCE *)args, hResource, hRTResource);
}

/* An SRV the 10.0 implementation describes: no raw buffer, no cube array. */
static bool
srv_10(const D3D11DDIARG_CREATESHADERRESOURCEVIEW *args, D3D10DDIARG_CREATESHADERRESOURCEVIEW *out)
{
   memset(out, 0, sizeof(*out));
   out->hDrvResource = args->hDrvResource;
   out->Format = args->Format;
   out->ResourceDimension = args->ResourceDimension;
   switch (args->ResourceDimension) {
   case D3D10DDIRESOURCE_BUFFER: out->Buffer = args->Buffer; return true;
   case D3D11DDIRESOURCE_BUFFEREX:
      if (args->BufferEx.Flags)
         return false;
      out->ResourceDimension = D3D10DDIRESOURCE_BUFFER;
      out->Buffer.FirstElement = args->BufferEx.FirstElement;
      out->Buffer.NumElements = args->BufferEx.NumElements;
      return true;
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

static void APIENTRY
SetUnorderedAccessViewsNone(D3D10DDI_HDEVICE hDevice, UINT, UINT n,
                            const D3D11DDI_HUNORDEREDACCESSVIEW *views, const UINT *)
{
   for (UINT i = 0; views && i < n; ++i)
      if (views[i].pDrvPrivate) {
         refuse(hDevice, "reject-d3d11-cs-uav", i, n);
         return;
      }
}

static SIZE_T APIENTRY
CalcPrivateTessellationShaderSize11(D3D10DDI_HDEVICE, const UINT *, const D3D11DDIARG_TESSELLATION_IO_SIGNATURES *)
{
   return sizeof(Shader);
}

static void APIENTRY
CreateTessellationShaderNone(D3D10DDI_HDEVICE hDevice, const UINT *, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER,
                             const D3D11DDIARG_TESSELLATION_IO_SIGNATURES *)
{
   refuse(hDevice, "reject-d3d11-tessellation", 0, 0);
}

static void APIENTRY
CreateComputeShaderNone(D3D10DDI_HDEVICE hDevice, const UINT *, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER)
{
   refuse(hDevice, "reject-d3d11-compute", 0, 0);
}

static SIZE_T APIENTRY
CalcPrivateUnorderedAccessViewSize11(D3D10DDI_HDEVICE, const D3D11DDIARG_CREATEUNORDEREDACCESSVIEW *)
{
   return sizeof(void *);
}

static void APIENTRY
CreateUnorderedAccessViewNone(D3D10DDI_HDEVICE hDevice, const D3D11DDIARG_CREATEUNORDEREDACCESSVIEW *,
                              D3D11DDI_HUNORDEREDACCESSVIEW, D3D11DDI_HRTUNORDEREDACCESSVIEW)
{
   refuse(hDevice, "reject-d3d11-uav", 0, 0);
}

static void APIENTRY
DestroyUnorderedAccessViewNone(D3D10DDI_HDEVICE, D3D11DDI_HUNORDEREDACCESSVIEW)
{
}

static void APIENTRY
ClearUnorderedAccessViewUintNone(D3D10DDI_HDEVICE hDevice, D3D11DDI_HUNORDEREDACCESSVIEW, const UINT[4])
{
   refuse(hDevice, "reject-d3d11-clear-uav", 0, 0);
}

static void APIENTRY
ClearUnorderedAccessViewFloatNone(D3D10DDI_HDEVICE hDevice, D3D11DDI_HUNORDEREDACCESSVIEW, const FLOAT[4])
{
   refuse(hDevice, "reject-d3d11-clear-uav", 1, 0);
}

static void APIENTRY
DrawIndirectNone(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE, UINT)
{
   refuse(hDevice, "reject-d3d11-draw-indirect", 0, 0);
}

static void APIENTRY
DispatchNone(D3D10DDI_HDEVICE hDevice, UINT, UINT, UINT)
{
   refuse(hDevice, "reject-d3d11-dispatch", 0, 0);
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
AgxD3d11FillDeviceFuncs(D3D11DDI_DEVICEFUNCS *f)
{
   memset(f, 0, sizeof(*f));
   /* D3D10-typed entries: the 10.0 implementations */
   f->pfnDefaultConstantBufferUpdateSubresourceUP = ResourceUpdateSubResourceUP;
   f->pfnVsSetConstantBuffers = VsSetConstantBuffers;
   f->pfnPsSetShaderResources = PsSetShaderResources;
   f->pfnPsSetShader = PsSetShader;
   f->pfnPsSetSamplers = PsSetSamplers;
   f->pfnVsSetShader = VsSetShader;
   f->pfnDrawIndexed = DrawIndexed;
   f->pfnDraw = Draw;
   f->pfnDynamicIABufferMapNoOverwrite = ResourceMap;
   f->pfnDynamicIABufferUnmap = ResourceUnmap;
   f->pfnDynamicConstantBufferMapDiscard = ResourceMap;
   f->pfnDynamicIABufferMapDiscard = ResourceMap;
   f->pfnDynamicConstantBufferUnmap = ResourceUnmap;
   f->pfnPsSetConstantBuffers = PsSetConstantBuffers;
   f->pfnIaSetInputLayout = IaSetInputLayout;
   f->pfnIaSetVertexBuffers = IaSetVertexBuffers;
   f->pfnIaSetIndexBuffer = IaSetIndexBuffer;
   f->pfnDrawIndexedInstanced = DrawIndexedInstanced;
   f->pfnDrawInstanced = DrawInstanced;
   f->pfnDynamicResourceMapDiscard = ResourceMap;
   f->pfnDynamicResourceUnmap = ResourceUnmap;
   f->pfnGsSetConstantBuffers = GsSetConstantBuffers;
   f->pfnGsSetShader = GsSetShader;
   f->pfnIaSetTopology = IaSetTopology;
   f->pfnStagingResourceMap = ResourceMap;
   f->pfnStagingResourceUnmap = ResourceUnmap;
   f->pfnVsSetShaderResources = VsSetShaderResources;
   f->pfnVsSetSamplers = VsSetSamplers;
   f->pfnGsSetShaderResources = GsSetShaderResources;
   f->pfnGsSetSamplers = GsSetSamplers;
   f->pfnShaderResourceViewReadAfterWriteHazard = ShaderResourceViewReadAfterWriteHazard;
   f->pfnResourceReadAfterWriteHazard = ResourceReadAfterWriteHazard;
   f->pfnSetBlendState = SetBlendState;
   f->pfnSetDepthStencilState = SetDepthStencilState;
   f->pfnSetRasterizerState = SetRasterizerState;
   f->pfnQueryEnd = QueryEnd;
   f->pfnQueryBegin = QueryBegin;
   f->pfnResourceCopyRegion = ResourceCopyRegion;
   f->pfnResourceUpdateSubresourceUP = ResourceUpdateSubResourceUP;
   f->pfnSoSetTargets = SoSetTargets;
   f->pfnDrawAuto = DrawAuto;
   f->pfnSetViewports = SetViewports;
   f->pfnSetScissorRects = SetScissorRects;
   f->pfnClearRenderTargetView = ClearRenderTargetView;
   f->pfnClearDepthStencilView = ClearDepthStencilView;
   f->pfnSetPredication = SetPredication;
   f->pfnQueryGetData = QueryGetData;
   f->pfnFlush = Flush;
   f->pfnGenMips = GenMips;
   f->pfnResourceCopy = ResourceCopy;
   f->pfnResourceResolveSubresource = ResourceResolveSubResource;
   f->pfnResourceMap = ResourceMap;
   f->pfnResourceUnmap = ResourceUnmap;
   f->pfnResourceIsStagingBusy = ResourceIsStagingBusy;
   f->pfnCalcPrivateOpenedResourceSize = CalcPrivateOpenedResourceSize;
   f->pfnOpenResource = OpenResource;
   f->pfnDestroyResource = DestroyResource;
   f->pfnDestroyShaderResourceView = DestroyShaderResourceView;
   f->pfnCalcPrivateRenderTargetViewSize = CalcPrivateRenderTargetViewSize;
   f->pfnCreateRenderTargetView = CreateRenderTargetView;
   f->pfnDestroyRenderTargetView = DestroyRenderTargetView;
   f->pfnDestroyDepthStencilView = DestroyDepthStencilView;
   f->pfnCalcPrivateElementLayoutSize = CalcPrivateElementLayoutSize;
   f->pfnCreateElementLayout = CreateElementLayout;
   f->pfnDestroyElementLayout = DestroyElementLayout;
   f->pfnDestroyBlendState = DestroyBlendState;
   f->pfnCalcPrivateDepthStencilStateSize = CalcPrivateDepthStencilStateSize;
   f->pfnCreateDepthStencilState = CreateDepthStencilState;
   f->pfnDestroyDepthStencilState = DestroyDepthStencilState;
   f->pfnCalcPrivateRasterizerStateSize = CalcPrivateRasterizerStateSize;
   f->pfnCreateRasterizerState = CreateRasterizerState;
   f->pfnDestroyRasterizerState = DestroyRasterizerState;
   f->pfnCalcPrivateShaderSize = CalcPrivateShaderSize;
   f->pfnCreateVertexShader = CreateVertexShader;
   f->pfnCreateGeometryShader = CreateGeometryShader;
   f->pfnCreatePixelShader = CreatePixelShader;
   f->pfnDestroyShader = DestroyShader;
   f->pfnCalcPrivateSamplerSize = CalcPrivateSamplerSize;
   f->pfnCreateSampler = CreateSampler;
   f->pfnDestroySampler = DestroySampler;
   f->pfnCalcPrivateQuerySize = CalcPrivateQuerySize;
   f->pfnCreateQuery = CreateQuery;
   f->pfnDestroyQuery = DestroyQuery;
   f->pfnCheckFormatSupport = CheckFormatSupport;
   f->pfnCheckMultisampleQualityLevels = CheckMultisampleQualityLevels;
   f->pfnCheckCounterInfo = CheckCounterInfo;
   f->pfnCheckCounter = CheckCounter;
   f->pfnDestroyDevice = DestroyDevice;
   f->pfnSetTextFilterSize = SetTextFilterSize;
   f->pfnResourceConvert = ResourceCopy;
   f->pfnResourceConvertRegion = ResourceCopyRegion;
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
   /* not provided yet: fail closed, unbinding accepted */
   f->pfnHsSetShaderResources = SetShaderResourcesNone;
   f->pfnHsSetShader = SetShaderNone;
   f->pfnHsSetSamplers = SetSamplersNone;
   f->pfnHsSetConstantBuffers = SetConstantBuffersNone;
   f->pfnDsSetShaderResources = SetShaderResourcesNone;
   f->pfnDsSetShader = SetShaderNone;
   f->pfnDsSetSamplers = SetSamplersNone;
   f->pfnDsSetConstantBuffers = SetConstantBuffersNone;
   f->pfnCsSetShaderResources = SetShaderResourcesNone;
   f->pfnCsSetShader = SetShaderNone;
   f->pfnCsSetSamplers = SetSamplersNone;
   f->pfnCsSetConstantBuffers = SetConstantBuffersNone;
   f->pfnHsSetShaderWithIfaces = SetShaderWithIfacesNone;
   f->pfnDsSetShaderWithIfaces = SetShaderWithIfacesNone;
   f->pfnCsSetShaderWithIfaces = SetShaderWithIfacesNone;
   f->pfnCalcPrivateTessellationShaderSize = CalcPrivateTessellationShaderSize11;
   f->pfnCreateHullShader = CreateTessellationShaderNone;
   f->pfnCreateDomainShader = CreateTessellationShaderNone;
   f->pfnCreateComputeShader = CreateComputeShaderNone;
   f->pfnCalcPrivateUnorderedAccessViewSize = CalcPrivateUnorderedAccessViewSize11;
   f->pfnCreateUnorderedAccessView = CreateUnorderedAccessViewNone;
   f->pfnDestroyUnorderedAccessView = DestroyUnorderedAccessViewNone;
   f->pfnClearUnorderedAccessViewUint = ClearUnorderedAccessViewUintNone;
   f->pfnClearUnorderedAccessViewFloat = ClearUnorderedAccessViewFloatNone;
   f->pfnCsSetUnorderedAccessViews = SetUnorderedAccessViewsNone;
   f->pfnDrawIndexedInstancedIndirect = DrawIndirectNone;
   f->pfnDrawInstancedIndirect = DrawIndirectNone;
   f->pfnDispatch = DispatchNone;
   f->pfnDispatchIndirect = DispatchIndirectNone;
   f->pfnCopyStructureCount = CopyStructureCountNone;
   /* deferred contexts and command lists stay NULL (THREADING caps 0) */
}
