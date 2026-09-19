"""Compile pinned native Gallium Asahi state with the proven Windows NIR flags.

Produces an object and first-error packet only. Does not run a renderer, install
a package, or substitute a synthetic state producer. Build-local source retains
the pinned MIT notices. No source change is made to the reference checkout.
"""
import argparse
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--windows-platform-declarations', action='store_true')
parser.add_argument('--native-state-test', action='store_true')
parser.add_argument('--native-batch-lifecycle', action='store_true')
parser.add_argument('--prepare-only', action='store_true')
parser.add_argument('--architecture', choices=('x64','arm64'), default='x64')
parser.add_argument('--project',type=Path)
args = parser.parse_args()
if (args.native_batch_lifecycle or args.prepare_only) and not (args.windows_platform_declarations and args.project):
    parser.error('Native lifecycle/prepare requires --windows-platform-declarations and --project')
out = args.output
out.mkdir(exist_ok=False)
mesa = Path(r'C:\Users\pauls\AD04-d3d10-frontend-build\mesa')
build = Path(r'C:\Users\pauls\AD04-fullcompiler-001') / ('nir-'+args.architecture)
generated = Path(r'C:\Users\pauls\AD04-asahi-windows-compiler\b5\generated')
sdk = Path(r'C:\Program Files (x86)\Windows Kits\10')
vc = Path(r'C:\VS2022Community\VC\Tools\MSVC\14.44.35207')
llvm = Path(r'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin')
source = mesa / 'src/gallium/drivers/asahi/agx_state.c'
digest = hashlib.sha256(source.read_bytes()).hexdigest()
if digest != '5015b75863202a170f8d6015eb82a76a70ecd4b92204894fa3d1886b1baa94ba':
    raise SystemExit('Pinned native state hash mismatch')

overlays = {}
if args.windows_platform_declarations:
    # All copied sources retain upstream MIT/BSD notices. This compile-only
    # projection excludes Linux APIs; no successful substitute API is defined.
    for directory in ('src/asahi', 'src/gallium/drivers/asahi',
                      'src/gallium/frontends/d3d10umd', 'include/drm-uapi'):
        shutil.copytree(mesa/directory, out/directory)
    def change(path, sha, replacements):
        target=out/path
        raw=target.read_bytes()
        if hashlib.sha256(raw).hexdigest()!=sha:
            raise SystemExit('Pinned header mismatch: '+path)
        text=raw.decode()
        for old,new in replacements:
            if text.count(old)!=1: raise SystemExit('Ambiguous header anchor: '+path)
            text=text.replace(old,new)
        target.write_text(text)
        overlays[path]={'before':sha,'after':hashlib.sha256(target.read_bytes()).hexdigest()}
    def replace_function_body(path, name, body):
        target=out/path
        text=target.read_text()
        marker='\n'+name+'('
        if text.count(marker)!=1:
            raise SystemExit('Ambiguous function anchor: '+path+':'+name)
        start=text.index('{',text.index(marker))+1
        depth=1
        end=start
        while depth and end<len(text):
            if text[end]=='{': depth+=1
            elif text[end]=='}': depth-=1
            end+=1
        if depth:
            raise SystemExit('Unclosed function: '+path+':'+name)
        target.write_text(text[:start]+'\n'+body+'\n'+text[end-1:])
        overlays[path]['after']=hashlib.sha256(target.read_bytes()).hexdigest()
    change('src/gallium/drivers/asahi/agx_state.h',
        '6d5e7f85849bce3c3f2e5569373a24f6c0d692217a8e493754298750b755e7ab',[
        ('#include <xf86drm.h>', '#ifndef _WIN32\n#include <xf86drm.h>\n#endif')])
    change('src/gallium/frontends/d3d10umd/Adapter.cpp',
        'e6a8e473d3574ce46970a045bf21136ce30eb206ac97d0cea34028427688f70b',[
        ('EXTERN_C struct pipe_screen *\nd3d10_create_screen(void);\n\n\n',''),
        ('''   pAdaptor->screen = d3d10_create_screen();
   if (!pAdaptor->screen) {
      free(pAdaptor);
      --numAdapters;
      return E_OUTOFMEMORY;
   }''','''   HRESULT result = AgxD3d10WindowsOpenAdapter(pOpenData, &pAdaptor->windows);
   if (FAILED(result)) {
      free(pAdaptor);
      --numAdapters;
      return result;
   }'''),
        ('''   struct pipe_screen *screen = pAdapter->screen;
   screen->destroy(screen);
   free(pAdapter);''','''   HRESULT result = AgxD3d10WindowsCloseAdapter(&pAdapter->windows);
   if (FAILED(result)) return result;
   free(pAdapter);'''),
        ('EXTERN_C HRESULT APIENTRY\nOpenAdapter10(',
         'EXTERN_C HRESULT APIENTRY\nMesaD3d10OpenAdapter10('),
        ('EXTERN_C HRESULT APIENTRY\nMesaD3d10OpenAdapter10(',
         '''EXTERN_C AGX_D3D10_WINDOWS_ADAPTER *APIENTRY
MesaD3d10FrontendAdapterForTest(D3D10DDI_HADAPTER hAdapter)
{
   Adapter *pAdapter = CastAdapter(hAdapter);
   return pAdapter ? pAdapter->windows : NULL;
}

EXTERN_C HRESULT APIENTRY
MesaD3d10OpenAdapter10('''),
        ('EXTERN_C HRESULT APIENTRY\nOpenAdapter10_2(',
         'EXTERN_C HRESULT APIENTRY\nMesaD3d10OpenAdapter10_2('),
        ('''static const UINT64
SupportedDDIInterfaceVersions[] = {
   D3D10_0_DDI_SUPPORTED,
   D3D10_0_x_DDI_SUPPORTED,
   D3D10_0_7_DDI_SUPPORTED,
#if SUPPORT_D3D10_1
   D3D10_1_DDI_SUPPORTED,
   D3D10_1_x_DDI_SUPPORTED,
   D3D10_1_7_DDI_SUPPORTED,
#endif
#if SUPPORT_D3D11
   D3D11_0_DDI_SUPPORTED,
   D3D11_0_7_DDI_SUPPORTED,
#endif
};''','''static const UINT64
SupportedDDIInterfaceVersions[] = {
   D3D10_0_DDI_SUPPORTED,
};''')])
    replace_function_body('src/gallium/frontends/d3d10umd/Adapter.cpp','GetCaps','''   Adapter *pAdapter = CastAdapter(hAdapter);
   if (!pAdapter || !pData || !pData->pData)
      return E_INVALIDARG;
   switch (pData->Type) {
   case D3D11DDICAPS_THREADING:
      if (pData->DataSize != sizeof(D3D11DDI_THREADING_CAPS))
         return E_INVALIDARG;
      ((D3D11DDI_THREADING_CAPS *)pData->pData)->Caps = 0;
      return S_OK;
   case D3D11DDICAPS_3DPIPELINESUPPORT:
      if (pData->DataSize != sizeof(D3D11DDI_3DPIPELINESUPPORT_CAPS))
         return E_INVALIDARG;
      ((D3D11DDI_3DPIPELINESUPPORT_CAPS *)pData->pData)->Caps = 0;
      return S_OK;
   default:
      return E_NOTIMPL;
   }''')
    change('src/gallium/frontends/d3d10umd/State.h',
        '4280c406ca8c1c199d09a0d062f8b52fb0baaec43a2ca7482e0d1a3acc7c4dd3',[
        ('#include "DriverIncludes.h"',
         '#include "DriverIncludes.h"\n#include "agx_d3d10_windows.h"'),
        ('struct pipe_screen *screen;',
         'AGX_D3D10_WINDOWS_ADAPTER *windows;'),
        ('struct pipe_context *pipe;','''struct pipe_context *pipe;
   AGX_D3D10_WINDOWS_DEVICE *windows;
   HRESULT cleanup_result;
   bool frontend_ready;'''),
        ('''struct Query
{
   D3D10DDI_QUERY Type;
   UINT Flags;

   unsigned pipe_type;
   struct pipe_query *handle;
   INT SeqNo;
   UINT GetDataCount;

   D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS Statistics;
};''','''enum QueryPhase {
   QueryCreated, QueryIssued, QuerySignaled, QueryFailed, QueryDestroyed
};

struct Query
{
   UINT Magic;
   Device *OwnerDevice;
   uint64_t OwnerCookie;
   UINT DeviceGeneration;
   UINT IssueSerial;
   UINT FenceToken;
   QueryPhase Phase;
   HRESULT LastError;
};'''),
        ('''struct Resource
{
   DXGI_FORMAT Format;
   UINT MipLevels;
   UINT NumSubResources;
   bool buffer;
   struct pipe_resource *resource;
   struct pipe_transfer **transfers;
   struct pipe_stream_output_target *so_target;
};''','''struct Resource
{
   DXGI_FORMAT Format;
   UINT MipLevels;
   UINT NumSubResources;
   bool buffer;
   struct pipe_resource *resource;
   struct pipe_transfer **transfers;
   struct pipe_stream_output_target *so_target;
   bool constant_buffer;
   bool index_buffer;
   UINT logical_bytes;
   Device *owner_device;
   ULONGLONG owner_cookie;
   ULONG device_generation;
   AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *presentation;
};'''),
        ('''   Query *pQuery = CastQuery(hQuery);
   return pQuery ? pQuery->handle : NULL;''','''   (void)hQuery;
   return NULL;''')])
    change('src/gallium/frontends/d3d10umd/Device.cpp',
        'dcf950aec993d40743671e1f208655e151158a9b4647bc3581dcd134962086aa',[
        ('''   struct pipe_screen *screen = pAdapter->screen;
   struct pipe_context *pipe = screen->context_create(screen, NULL, 0);
   pDevice->pipe = pipe;
   pDevice->cso = cso_create_context(pipe, CSO_NO_VBUF);''','''   HRESULT result = AgxD3d10WindowsCreateDevice(
      pAdapter->windows, pCreateData, &pDevice->windows);
   pDevice->cleanup_result = result;
   if (FAILED(result)) {
      HRESULT initial = result;
      if (pDevice->windows) {
         BOOL consumed = FALSE;
         HRESULT cleanup = AgxD3d10WindowsDestroyDeviceDdi(&pDevice->windows, &consumed);
         if (FAILED(cleanup)) pDevice->cleanup_result = cleanup;
      }
      return initial;
   }
   struct pipe_context *pipe = AgxD3d10WindowsContext(pDevice->windows);
   if (!pipe) {
      BOOL consumed = FALSE;
      pDevice->cleanup_result = AgxD3d10WindowsDestroyDeviceDdi(&pDevice->windows, &consumed);
      return E_FAIL;
   }
   struct pipe_screen *screen = pipe->screen;
   pDevice->pipe = pipe;
   pDevice->cso = cso_create_context(pipe, CSO_NO_VBUF);
   if (!pDevice->cso) {
      BOOL consumed = FALSE;
      pDevice->cleanup_result = AgxD3d10WindowsDestroyDeviceDdi(&pDevice->windows, &consumed);
      pDevice->pipe = NULL;
      return E_OUTOFMEMORY;
   }'''),
        ('''   if (0) {
      return S_OK;''','''   pDevice->frontend_ready = true;
   pDevice->cleanup_result = S_OK;
   if (0) {
      return S_OK;'''),
        ('   pipe->destroy(pipe);','''   pDevice->frontend_ready = false;
   pDevice->pipe = NULL;
   BOOL consumed = FALSE;
   pDevice->cleanup_result = AgxD3d10WindowsDestroyDeviceDdi(
      &pDevice->windows, &consumed);'''),
        ('''void APIENTRY
RelocateDeviceFuncs(''','''EXTERN_C HRESULT APIENTRY
MesaD3d10FrontendCleanupResult(D3D10DDI_HDEVICE hDevice)
{
   Device *pDevice = CastDevice(hDevice);
   return pDevice ? pDevice->cleanup_result : E_INVALIDARG;
}

EXTERN_C struct _ADMISSION_UMD_DEVICE *APIENTRY
MesaD3d10FrontendRuntimeForTest(D3D10DDI_HDEVICE hDevice)
{
   Device *pDevice = CastDevice(hDevice);
   return pDevice ? AgxD3d10WindowsRuntimeForTest(pDevice->windows) : NULL;
}

EXTERN_C void *APIENTRY
MesaD3d10FrontendOwnerForTest(D3D10DDI_HDEVICE hDevice)
{
   Device *pDevice = CastDevice(hDevice);
   return pDevice ? AgxD3d10WindowsOwnerForTest(pDevice->windows) : NULL;
}

EXTERN_C struct pipe_context *APIENTRY
MesaD3d10FrontendContextForTest(D3D10DDI_HDEVICE hDevice)
{
   Device *pDevice = CastDevice(hDevice);
   return pDevice ? pDevice->pipe : NULL;
}

EXTERN_C BOOL APIENTRY
MesaD3d10FrontendShaderValidForTest(D3D10DDI_HSHADER hShader)
{
   return CastPipeShader(hShader) != NULL;
}

void APIENTRY
 RelocateDeviceFuncs('''),
        ('''   struct pipe_context *pipe = CastPipeContext(hDevice);
   struct pipe_screen *screen = pipe->screen;

   *pFormatCaps = 0;

   enum pipe_format format = FormatTranslate(Format, false);
   if (format == PIPE_FORMAT_NONE) {
      *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED;
      return;
   }

   if (Format == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM) {
      /*
       * We only need to support creation.
       * http://msdn.microsoft.com/en-us/library/windows/hardware/ff552818.aspx
       */
      return;
   }

   if (screen->is_format_supported(screen, format, PIPE_TEXTURE_2D, 0, 0,
                                   PIPE_BIND_RENDER_TARGET)) {
      *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET;
      *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_BLENDABLE;

#if SUPPORT_MSAA
      if (screen->is_format_supported(screen, format, PIPE_TEXTURE_2D, 4, 4,
                                      PIPE_BIND_RENDER_TARGET)) {
         *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET;
      }
#endif
   }

   if (screen->is_format_supported(screen, format, PIPE_TEXTURE_2D, 0, 0,
                                   PIPE_BIND_SAMPLER_VIEW)) {
      *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE;

#if SUPPORT_MSAA
      if (screen->is_format_supported(screen, format, PIPE_TEXTURE_2D, 4, 4,
                                      PIPE_BIND_RENDER_TARGET)) {
         *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_LOAD;
      }
#endif
   }''','''   (void)hDevice;
   if (Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
      *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET |
                     D3D10_DDI_FORMAT_SUPPORT_BLENDABLE;
   } else if (Format == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM) {
      *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED;
   } else {
      *pFormatCaps = 0;
   }'''),
        ('''   /* XXX: Disable MSAA */
   *pNumQualityLevels = 0;''','''   (void)hDevice;
   *pNumQualityLevels =
      Format == DXGI_FORMAT_B8G8R8A8_UNORM && SampleCount == 1 ? 1 : 0;''')])
    replace_function_body('src/gallium/frontends/d3d10umd/Device.cpp','Flush','''   Device *pDevice = CastDevice(hDevice);
   HRESULT result = AgxD3d10WindowsQueryCollect(pDevice->windows);
   if (SUCCEEDED(result)) {
      pDevice->pipe->flush(pDevice->pipe, NULL, 0);
      result = AgxD3d10WindowsFlushStatus(pDevice->windows);
   }
   if (SUCCEEDED(result)) result = AgxD3d10WindowsQueryCollect(pDevice->windows);
   if (FAILED(result)) SetError(hDevice, result);''')
    change('src/gallium/frontends/d3d10umd/OutputMerger.cpp',
        'fefcbe8754fd1042b7bf091feab767844cc71a8fe4cbe9f0b41d76dc9dd4fd04',[
        ('''   pipe->clear_render_target(pipe,
                             surface,
                             &clear_color,
                             0, 0,
                             pipe_surface_width(surface),
                             pipe_surface_height(surface),
                             true);''','''   if (pipe->clear_render_target) {
      pipe->clear_render_target(pipe,
                                surface,
                                &clear_color,
                                0, 0,
                                pipe_surface_width(surface),
                                pipe_surface_height(surface),
                                true);
      return;
   }
   Device *pDevice = CastDevice(hDevice);
   struct pipe_resource *resource = surface ? surface->texture : NULL;
   struct pipe_surface *bound = pDevice && pDevice->fb.nr_cbufs == 1 ?
      &pDevice->fb.cbufs[0] : NULL;
   if (!pipe->clear || !surface || !resource ||
       resource->target != PIPE_TEXTURE_2D ||
       resource->format != PIPE_FORMAT_B8G8R8A8_UNORM ||
       resource->nr_samples != 1 || resource->array_size != 1 ||
       resource->last_level != 0 || surface->format != resource->format ||
       surface->level != 0 || surface->first_layer != 0 || surface->last_layer != 0 ||
       !bound || pDevice->fb.zsbuf.texture || bound->texture != resource ||
       bound->format != surface->format || bound->level != surface->level ||
       bound->first_layer != surface->first_layer || bound->last_layer != surface->last_layer ||
       pDevice->fb.width != pipe_surface_width(surface) ||
       pDevice->fb.height != pipe_surface_height(surface)) {
      LOG_UNSUPPORTED("ClearRenderTargetView requires one full BGRA8 target");
      SetError(hDevice, E_NOTIMPL);
      return;
   }
   pipe->clear(pipe, PIPE_CLEAR_COLOR0, 0xf, 0, NULL,
               &clear_color, 0.0, 0);'''),
        ('''   struct pipe_context *pipe = CastPipeContext(hDevice);
   struct pipe_surface *surface = CastPipeDepthStencilView(hDepthStencilView);

   unsigned flags = 0;
   if (Flags & D3D10_DDI_CLEAR_DEPTH) {
      flags |= PIPE_CLEAR_DEPTH;
   }
   if (Flags & D3D10_DDI_CLEAR_STENCIL) {
      flags |= PIPE_CLEAR_STENCIL;
   }

   pipe->clear_depth_stencil(pipe,
                             surface,
                             flags,
                             Depth,
                             Stencil,
                             0, 0,
                             pipe_surface_width(surface),
                             pipe_surface_height(surface),
                             true);''','''   (void)hDepthStencilView;
   (void)Flags;
   (void)Depth;
   (void)Stencil;
   SetError(hDevice, E_NOTIMPL);''')])
    change('src/gallium/frontends/d3d10umd/Query.cpp',
        '8456801b1614e79ad9ce307035a76a5f5f642eb83a40e4c066a8946f44d620b4',[
        ('#include "State.h"', '''#include "State.h"

#define AGX_EVENT_QUERY_MAGIC 0x51564541u

static bool
EventQueryValid(Device *device, Query *query)
{
   ULONGLONG owner = 0;
   ULONG generation = 0;
   return device && query && query->Magic == AGX_EVENT_QUERY_MAGIC &&
      AgxD3d10WindowsIdentity(device->windows, &owner, &generation) &&
      query->OwnerDevice == device && query->OwnerCookie == owner &&
      query->DeviceGeneration == generation && query->Phase != QueryDestroyed;
}

EXTERN_C ULONG APIENTRY
MesaD3d10FrontendEventQuerySetGenerationForTest(D3D10DDI_HQUERY hQuery,
                                                ULONG generation)
{
   Query *query = CastQuery(hQuery);
   ULONG prior = query ? query->DeviceGeneration : 0;
   if (query) query->DeviceGeneration = generation;
   return prior;
}'''),
        ('''   Device *pDevice = CastDevice(hDevice);
   struct pipe_context *pipe = pDevice->pipe;

   Query *pQuery = CastQuery(hQuery);
   memset(pQuery, 0, sizeof *pQuery);

   pQuery->Type = pCreateQuery->Query;
   pQuery->Flags = pCreateQuery->MiscFlags;

   pQuery->pipe_type = TranslateQueryType(pCreateQuery->Query);
   if (pQuery->pipe_type < PIPE_QUERY_TYPES) {
      pQuery->handle = pipe->create_query(pipe, pQuery->pipe_type, 0);
   }''','''   Device *pDevice = CastDevice(hDevice);
   Query *pQuery = CastQuery(hQuery);
   ULONGLONG owner = 0;
   ULONG generation = 0;
   if (!pCreateQuery || pCreateQuery->Query != D3D10DDI_QUERY_EVENT ||
       pCreateQuery->MiscFlags || !pQuery) {
      SetError(hDevice, E_NOTIMPL);
      return;
   }
   if (!pDevice || !AgxD3d10WindowsIdentity(
          pDevice->windows, &owner, &generation)) {
      SetError(hDevice, E_FAIL);
      return;
   }
   Query candidate = {};
   candidate.Magic = AGX_EVENT_QUERY_MAGIC;
   candidate.OwnerDevice = pDevice;
   candidate.OwnerCookie = owner;
   candidate.DeviceGeneration = generation;
   candidate.Phase = QueryCreated;
   candidate.LastError = S_OK;
   *pQuery = candidate;
   (void)hRTQuery;'''),
        ('''DestroyQuery(D3D10DDI_HDEVICE hDevice, // IN
             D3D10DDI_HQUERY hQuery)   // IN
{
   LOG_ENTRYPOINT();''','''DestroyQuery(D3D10DDI_HDEVICE hDevice, // IN
             D3D10DDI_HQUERY hQuery)   // IN
{
   Device *pDevice = CastDevice(hDevice);
   Query *pQuery = CastQuery(hQuery);
   if (!EventQueryValid(pDevice, pQuery)) {
      SetError(hDevice, E_INVALIDARG);
      return;
   }
   pQuery->Magic = 0;
   pQuery->OwnerDevice = NULL;
   pQuery->OwnerCookie = 0;
   pQuery->DeviceGeneration = 0;
   pQuery->FenceToken = 0;
   pQuery->Phase = QueryDestroyed;
   pQuery->LastError = S_OK;
   return;'''),
        ('''QueryBegin(D3D10DDI_HDEVICE hDevice,   // IN
           D3D10DDI_HQUERY hQuery)     // IN
{
   LOG_ENTRYPOINT();''','''QueryBegin(D3D10DDI_HDEVICE hDevice,   // IN
           D3D10DDI_HQUERY hQuery)     // IN
{
   SetError(hDevice, E_NOTIMPL);
   return;'''),
        ('''QueryEnd(D3D10DDI_HDEVICE hDevice,  // IN
         D3D10DDI_HQUERY hQuery)    // IN
{
   LOG_ENTRYPOINT();''','''QueryEnd(D3D10DDI_HDEVICE hDevice,  // IN
         D3D10DDI_HQUERY hQuery)    // IN
{
   SetError(hDevice, E_NOTIMPL);
   return;'''),
        ('''QueryGetData(D3D10DDI_HDEVICE hDevice,                      // IN
             D3D10DDI_HQUERY hQuery,                        // IN
             __out_bcount_full_opt (DataSize) void *pData,  // OUT
             UINT DataSize,                                 // IN
             UINT Flags)                                    // IN
{
   LOG_ENTRYPOINT();''','''QueryGetData(D3D10DDI_HDEVICE hDevice,                      // IN
             D3D10DDI_HQUERY hQuery,                        // IN
             __out_bcount_full_opt (DataSize) void *pData,  // OUT
             UINT DataSize,                                 // IN
             UINT Flags)                                    // IN
{
   SetError(hDevice, E_NOTIMPL);
   return;'''),
        ('''SetPredication(D3D10DDI_HDEVICE hDevice,  // IN
               D3D10DDI_HQUERY hQuery,    // IN
               BOOL PredicateValue)       // IN
{
   LOG_ENTRYPOINT();''','''SetPredication(D3D10DDI_HDEVICE hDevice,  // IN
               D3D10DDI_HQUERY hQuery,    // IN
               BOOL PredicateValue)       // IN
{
   SetError(hDevice, E_NOTIMPL);
   return;''')])
    replace_function_body('src/gallium/frontends/d3d10umd/Query.cpp','DestroyQuery','''   Device *pDevice = CastDevice(hDevice);
   Query *pQuery = CastQuery(hQuery);
   if (!EventQueryValid(pDevice, pQuery)) {
      SetError(hDevice, E_INVALIDARG);
      return;
   }
   HRESULT result = S_OK;
   if (pQuery->FenceToken) {
      result = AgxD3d10WindowsQueryDetach(pDevice->windows,
         pQuery->OwnerCookie, pQuery->DeviceGeneration, pQuery->IssueSerial,
         pQuery->FenceToken);
      if (SUCCEEDED(result))
         result = AgxD3d10WindowsQueryCollect(pDevice->windows);
   }
   pQuery->Magic = 0;
   pQuery->OwnerDevice = NULL;
   pQuery->OwnerCookie = 0;
   pQuery->DeviceGeneration = 0;
   pQuery->FenceToken = 0;
   pQuery->Phase = QueryDestroyed;
   pQuery->LastError = result;
   if (FAILED(result)) SetError(hDevice, result);''')
    replace_function_body('src/gallium/frontends/d3d10umd/Query.cpp','QueryBegin','''   Device *pDevice = CastDevice(hDevice);
   Query *pQuery = CastQuery(hQuery);
   if (!EventQueryValid(pDevice, pQuery)) {
      SetError(hDevice, E_INVALIDARG);
      return;
   }
   if (pQuery->Phase == QueryFailed)
      SetError(hDevice, FAILED(pQuery->LastError) ? pQuery->LastError : E_FAIL);''')
    replace_function_body('src/gallium/frontends/d3d10umd/Query.cpp','QueryEnd','''   Device *pDevice = CastDevice(hDevice);
   Query *pQuery = CastQuery(hQuery);
   if (!EventQueryValid(pDevice, pQuery)) {
      SetError(hDevice, E_INVALIDARG);
      return;
   }
   if (pQuery->Phase == QueryFailed || pQuery->IssueSerial == UINT_MAX) {
      HRESULT error = pQuery->Phase == QueryFailed && FAILED(pQuery->LastError) ?
         pQuery->LastError : E_OUTOFMEMORY;
      SetError(hDevice, error);
      return;
   }
   HRESULT result = S_OK;
   if (pQuery->FenceToken) {
      result = AgxD3d10WindowsQueryDetach(pDevice->windows,
         pQuery->OwnerCookie, pQuery->DeviceGeneration, pQuery->IssueSerial,
         pQuery->FenceToken);
      pQuery->FenceToken = 0;
   }
   if (SUCCEEDED(result)) result = AgxD3d10WindowsQueryCollect(pDevice->windows);
   UINT issue = pQuery->IssueSerial + 1;
   if (SUCCEEDED(result)) {
      pDevice->pipe->flush(pDevice->pipe, NULL, 0);
      result = AgxD3d10WindowsFlushStatus(pDevice->windows);
   }
   if (SUCCEEDED(result)) result = AgxD3d10WindowsQueryCollect(pDevice->windows);
   ULONG fence = 0;
   if (SUCCEEDED(result)) result = AgxD3d10WindowsQuerySignal(pDevice->windows,
      pQuery->OwnerCookie, pQuery->DeviceGeneration, issue, &fence);
   pQuery->IssueSerial = issue;
   pQuery->LastError = result;
   if (FAILED(result) || !fence) {
      pQuery->Phase = QueryFailed;
      SetError(hDevice, FAILED(result) ? result : E_FAIL);
      return;
   }
   pQuery->FenceToken = fence;
   pQuery->Phase = QueryIssued;''')
    replace_function_body('src/gallium/frontends/d3d10umd/Query.cpp','QueryGetData','''   Device *pDevice = CastDevice(hDevice);
   Query *pQuery = CastQuery(hQuery);
   if (!EventQueryValid(pDevice, pQuery) ||
       !((pData == NULL && DataSize == 0) ||
         (pData != NULL && DataSize == sizeof(BOOL))) ||
       (Flags & ~D3D10_DDI_GET_DATA_DO_NOT_FLUSH)) {
      SetError(hDevice, E_INVALIDARG);
      return;
   }
   if (pQuery->Phase == QuerySignaled) {
      if (pData) *(BOOL *)pData = TRUE;
      return;
   }
   if (pQuery->Phase == QueryFailed) {
      SetError(hDevice, FAILED(pQuery->LastError) ? pQuery->LastError : E_FAIL);
      return;
   }
   if (pQuery->Phase != QueryIssued || !pQuery->FenceToken) {
      SetError(hDevice, E_INVALIDARG);
      return;
   }
   BOOL completed = FALSE;
   HRESULT result = AgxD3d10WindowsQueryPoll(pDevice->windows,
      pQuery->OwnerCookie, pQuery->DeviceGeneration, pQuery->IssueSerial,
      pQuery->FenceToken, &completed);
   if (FAILED(result)) {
      pQuery->Phase = QueryFailed;
      pQuery->LastError = result;
      SetError(hDevice, result);
      return;
   }
   if (!completed) {
      SetError(hDevice, DXGI_DDI_ERR_WASSTILLDRAWING);
      return;
   }
   result = AgxD3d10WindowsQueryConsume(pDevice->windows,
      pQuery->OwnerCookie, pQuery->DeviceGeneration, pQuery->IssueSerial,
      pQuery->FenceToken);
   if (FAILED(result)) {
      pQuery->Phase = QueryFailed;
      pQuery->LastError = result;
      SetError(hDevice, result);
      return;
   }
   pQuery->FenceToken = 0;
   pQuery->Phase = QuerySignaled;
   pQuery->LastError = S_OK;
   if (pData) *(BOOL *)pData = TRUE;''')
    replace_function_body('src/gallium/frontends/d3d10umd/Query.cpp','SetPredication','''   (void)hQuery;
   (void)PredicateValue;
   SetError(hDevice, E_NOTIMPL);''')
    replace_function_body('src/gallium/frontends/d3d10umd/Query.cpp','CheckPredicate','''   return pDevice && pDevice->pPredicate == NULL;''')
    change('src/gallium/frontends/d3d10umd/DxgiFns.cpp',
        'ecdfee2a652cab9d0604ff3ddcf0fb196778f41d367398aa4fcd080776a47f40',[
        ('HRESULT APIENTRY\n_Present(', '''static HRESULT
UnsupportedDxgi(DXGI_DDI_HDEVICE dxgiDevice)
{
   D3D10DDI_HDEVICE hDevice = {};
   hDevice.pDrvPrivate = reinterpret_cast<void *>(dxgiDevice);
   SetError(hDevice, E_NOTIMPL);
   return E_NOTIMPL;
}

HRESULT APIENTRY
_Present('''),
        ('''   struct Device *device = CastDevice(pPresentData->hDevice);
   Resource *pSrcResource = CastResource(pPresentData->hSurfaceToPresent);

   device->pipe->flush(device->pipe, NULL, 0);
   device->pipe->screen->flush_frontbuffer(device->pipe->screen, device->pipe,\x20
      pSrcResource->resource, 0, 0, pPresentData->pDXGIContext, 0, NULL);

   return S_OK;''','''   if (!pPresentData || pPresentData->hDstResource != 0 ||
       pPresentData->SrcSubResourceIndex != 0 ||
       pPresentData->Flags.Value != 0x2u ||
       pPresentData->FlipInterval != DXGI_DDI_FLIP_INTERVAL_ONE)
      return E_INVALIDARG;
   struct Device *device = CastDevice(pPresentData->hDevice);
   Resource *resource = CastResource(pPresentData->hSurfaceToPresent);
   if (!device || !resource || resource->owner_device != device ||
       !resource->presentation)
      return E_INVALIDARG;
   return AgxD3d10WindowsPresentationSubmit(
      device->windows, resource->presentation, pPresentData->pDXGIContext);'''),
        ('''_GetGammaCaps( DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS *GetCaps )
{
   LOG_ENTRYPOINT();''','''_GetGammaCaps( DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS *GetCaps )
{
   return UnsupportedDxgi(GetCaps->hDevice);'''),
        ('''_SetDisplayMode( DXGI_DDI_ARG_SETDISPLAYMODE *SetDisplayMode )
{
   LOG_UNSUPPORTED_ENTRYPOINT();''','''_SetDisplayMode( DXGI_DDI_ARG_SETDISPLAYMODE *SetDisplayMode )
{
   if (!SetDisplayMode || SetDisplayMode->SubResourceIndex != 0)
      return E_INVALIDARG;
   struct Device *device = CastDevice(SetDisplayMode->hDevice);
   Resource *resource = CastResource(SetDisplayMode->hResource);
   if (!device || !resource || resource->owner_device != device ||
       !resource->presentation)
      return E_INVALIDARG;
   return AgxD3d10WindowsPresentationSetDisplayMode(
      device->windows, resource->presentation);'''),
        ('''_SetResourcePriority( DXGI_DDI_ARG_SETRESOURCEPRIORITY *SetResourcePriority )
{
   LOG_ENTRYPOINT();''','''_SetResourcePriority( DXGI_DDI_ARG_SETRESOURCEPRIORITY *SetResourcePriority )
{
   return UnsupportedDxgi(SetResourcePriority->hDevice);'''),
        ('''_QueryResourceResidency( DXGI_DDI_ARG_QUERYRESOURCERESIDENCY *QueryResourceResidency )
{
   LOG_ENTRYPOINT();''','''_QueryResourceResidency( DXGI_DDI_ARG_QUERYRESOURCERESIDENCY *QueryResourceResidency )
{
   return UnsupportedDxgi(QueryResourceResidency->hDevice);'''),
        ('''_RotateResourceIdentities( DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES *RotateResourceIdentities )
{
   LOG_ENTRYPOINT();''','''_RotateResourceIdentities( DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES *RotateResourceIdentities )
{
   return UnsupportedDxgi(RotateResourceIdentities->hDevice);'''),
        ('''_Blt(DXGI_DDI_ARG_BLT *Blt)
{
   LOG_UNSUPPORTED_ENTRYPOINT();''','''_Blt(DXGI_DDI_ARG_BLT *Blt)
{
   return UnsupportedDxgi(Blt->hDevice);''')])
    change('src/gallium/frontends/d3d10umd/Shader.cpp',
        '48a7de2a42b25abac677cd903c10f21fc91b86aef167266a32a6d7d9da21097c',[
        ('''{
   unsigned i;

   LOG_ENTRYPOINT();

   Device *pDevice = CastDevice(hDevice);
   struct pipe_context *pipe = pDevice->pipe;

   assert(SOTargets + ClearTargets <= PIPE_MAX_SO_BUFFERS);''','''{
   SetError(hDevice, E_NOTIMPL);
   return;

   unsigned i;
   Device *pDevice = CastDevice(hDevice);
   struct pipe_context *pipe = pDevice->pipe;

   assert(SOTargets + ClearTargets <= PIPE_MAX_SO_BUFFERS);'''),
        ('''GenMips(D3D10DDI_HDEVICE hDevice,                           // IN
        D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView)   // IN
{
   LOG_ENTRYPOINT();''','''GenMips(D3D10DDI_HDEVICE hDevice,                           // IN
        D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView)   // IN
{
   (void)hShaderResourceView;
   SetError(hDevice, E_NOTIMPL);
   return;'''),
        ('''CreateGeometryShaderWithStreamOutput(
   D3D10DDI_HDEVICE hDevice,                                                                             // IN
   __in const D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *pData,   // IN
   D3D10DDI_HSHADER hShader,                                                                             // IN
   D3D10DDI_HRTSHADER hRTShader,                                                                         // IN
   __in const D3D10DDIARG_STAGE_IO_SIGNATURES *pSignatures)                                              // IN
{
   LOG_ENTRYPOINT();''','''CreateGeometryShaderWithStreamOutput(
   D3D10DDI_HDEVICE hDevice,                                                                             // IN
   __in const D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *pData,   // IN
   D3D10DDI_HSHADER hShader,                                                                             // IN
   D3D10DDI_HRTSHADER hRTShader,                                                                         // IN
   __in const D3D10DDIARG_STAGE_IO_SIGNATURES *pSignatures)                                              // IN
{
   SetError(hDevice, E_NOTIMPL);
   return;''')])
    replace_function_body('src/gallium/frontends/d3d10umd/Shader.cpp','SetConstantBuffers','''   Device *pDevice = CastDevice(hDevice);
   ULONGLONG owner = 0;
   ULONG generation = 0;
   bool valid = pDevice && (NumBuffers == 0 || phBuffers) &&
      StartBuffer <= PIPE_MAX_CONSTANT_BUFFERS &&
      NumBuffers <= PIPE_MAX_CONSTANT_BUFFERS - StartBuffer &&
      AgxD3d10WindowsIdentity(pDevice->windows, &owner, &generation);
   for (UINT i = 0; valid && i < NumBuffers; ++i) {
      Resource *resource = CastResource(phBuffers[i]);
      if (!resource) continue;
      unsigned slot = StartBuffer + i;
      valid = (shader_type == MESA_SHADER_VERTEX ||
               shader_type == MESA_SHADER_FRAGMENT) && slot == 0 &&
         resource->constant_buffer && resource->owner_device == pDevice &&
         resource->owner_cookie == owner &&
         resource->device_generation == generation && resource->resource &&
         resource->resource->target == PIPE_BUFFER &&
         (resource->resource->bind & PIPE_BIND_CONSTANT_BUFFER) &&
         !(resource->resource->bind &
           ~(PIPE_BIND_CONSTANT_BUFFER | PIPE_BIND_SHADER_IMAGE)) &&
         resource->logical_bytes >= 16 && resource->logical_bytes <= 65536 &&
         (resource->logical_bytes & 15) == 0 &&
         resource->resource->width0 == resource->logical_bytes;
   }
   if (!valid) {
      SetError(hDevice, E_NOTIMPL);
      return;
   }
   for (UINT i = 0; i < NumBuffers; ++i) {
      Resource *resource = CastResource(phBuffers[i]);
      if (!resource) {
         pDevice->pipe->set_constant_buffer(pDevice->pipe, shader_type,
                                             StartBuffer + i, NULL);
         continue;
      }
      struct pipe_constant_buffer cb = {};
      cb.buffer = resource->resource;
      cb.buffer_offset = 0;
      cb.buffer_size = resource->logical_bytes;
      pDevice->pipe->set_constant_buffer(pDevice->pipe, shader_type,
                                          StartBuffer + i, &cb);
   }''')
    change('src/gallium/frontends/d3d10umd/Draw.cpp',
        'da5904f2ac6b8a79373bcc60d2cef0546e8da0bc1ba92d21812aff3ea2338f7e',[
        ('''DrawAuto(D3D10DDI_HDEVICE hDevice)  // IN
{
   LOG_ENTRYPOINT();''','''DrawAuto(D3D10DDI_HDEVICE hDevice)  // IN
{
   SetError(hDevice, E_NOTIMPL);
   return;''')])
    replace_function_body('src/gallium/frontends/d3d10umd/Draw.cpp','DrawIndexed','''   Device *pDevice = CastDevice(hDevice);
   if (!pDevice || IndexCount != 3 || StartIndexLocation != 0 ||
       BaseVertexLocation != 0 || pDevice->primitive != MESA_PRIM_TRIANGLES ||
       !pDevice->index_buffer || pDevice->index_size != 2 ||
       pDevice->ib_offset != 0) {
      SetError(hDevice, E_NOTIMPL);
      return;
   }
   ResolveState(pDevice);
   struct pipe_draw_info info;
   struct pipe_draw_start_count_bias draw = {};
   util_draw_init_info(&info);
   info.index_size = 2;
   info.mode = MESA_PRIM_TRIANGLES;
   info.index.resource = pDevice->index_buffer;
   info.instance_count = 1;
   info.primitive_restart = false;
   draw.count = 3;
   pDevice->pipe->draw_vbo(pDevice->pipe, &info, 0, NULL, &draw, 1);''')
    replace_function_body('src/gallium/frontends/d3d10umd/Draw.cpp','DrawIndexedInstanced','''   (void)IndexCountPerInstance;
   (void)InstanceCount;
   (void)StartIndexLocation;
   (void)BaseVertexLocation;
   (void)StartInstanceLocation;
   SetError(hDevice, E_NOTIMPL);''')
    change('src/gallium/frontends/d3d10umd/Resource.cpp',
        'ae2d60a798ff0d9da6e55171013f133d1d99bc91ef2760875d126aa5b96fcf48',[
        ('#include "util/u_surface.h"',
         '#include "util/u_surface.h"\n#include "drm-uapi/drm_fourcc.h"'),
        ('''   Resource *pResource = CastResource(hResource);

   memset(pResource, 0, sizeof *pResource);''','''   Resource *pResource = CastResource(hResource);
   Device *pDevice = CastDevice(hDevice);
   ULONGLONG resourceOwner = 0;
   ULONG resourceGeneration = 0;
   const D3D10DDI_MIPINFO *resourceMip = pCreateResource->pMipInfoList;
   bool wantsConstant =
      (pCreateResource->BindFlags & D3D10_DDI_BIND_CONSTANT_BUFFER) != 0;
   bool wantsIndex =
      (pCreateResource->BindFlags & D3D10_DDI_BIND_INDEX_BUFFER) != 0;
   bool wantsPresentation = pCreateResource->pPrimaryDesc != NULL ||
      (pCreateResource->BindFlags & D3D10_DDI_BIND_PRESENT) != 0;
   bool validConstant = wantsConstant && pResource && resourceMip &&
      pCreateResource->ResourceDimension == D3D10DDIRESOURCE_BUFFER &&
      pCreateResource->Format == DXGI_FORMAT_UNKNOWN &&
      pCreateResource->BindFlags == D3D10_DDI_BIND_CONSTANT_BUFFER &&
      pCreateResource->Usage == D3D10_DDI_USAGE_DEFAULT &&
      pCreateResource->MapFlags == 0 && pCreateResource->MiscFlags == 0 &&
      !pCreateResource->pPrimaryDesc && pCreateResource->MipLevels == 1 &&
      pCreateResource->ArraySize == 1 && resourceMip[0].TexelHeight == 1 &&
      resourceMip[0].TexelDepth == 1 && resourceMip[0].TexelWidth >= 16 &&
      resourceMip[0].TexelWidth <= 65536 &&
      (resourceMip[0].TexelWidth & 15) == 0 &&
      pCreateResource->SampleDesc.Count == 1 &&
      pCreateResource->SampleDesc.Quality == 0 &&
      (!pCreateResource->pInitialDataUP ||
       pCreateResource->pInitialDataUP[0].pSysMem) && pDevice &&
      AgxD3d10WindowsIdentity(pDevice->windows,&resourceOwner,
                              &resourceGeneration);
   static const unsigned char expectedIndex[8] = {0,0,1,0,2,0,0,0};
   bool validIndex = wantsIndex && pResource && resourceMip &&
      pCreateResource->ResourceDimension == D3D10DDIRESOURCE_BUFFER &&
      pCreateResource->Format == DXGI_FORMAT_UNKNOWN &&
      pCreateResource->BindFlags == D3D10_DDI_BIND_INDEX_BUFFER &&
      pCreateResource->Usage == D3D10_DDI_USAGE_DEFAULT &&
      pCreateResource->MapFlags == 0 && pCreateResource->MiscFlags == 0 &&
      !pCreateResource->pPrimaryDesc && pCreateResource->MipLevels == 1 &&
      pCreateResource->ArraySize == 1 && resourceMip[0].TexelWidth == 8 &&
      resourceMip[0].TexelHeight == 1 && resourceMip[0].TexelDepth == 1 &&
      pCreateResource->SampleDesc.Count == 1 &&
      pCreateResource->SampleDesc.Quality == 0 &&
      pCreateResource->pInitialDataUP &&
      pCreateResource->pInitialDataUP[0].pSysMem &&
      memcmp(pCreateResource->pInitialDataUP[0].pSysMem,expectedIndex,8)==0 &&
      pDevice && AgxD3d10WindowsIdentity(pDevice->windows,&resourceOwner,
                                         &resourceGeneration);
   if ((wantsConstant && !validConstant) || (wantsIndex && !validIndex)) {
      SetError(hDevice, E_NOTIMPL);
      return;
   }

   if (wantsPresentation) {
      if (!pDevice || !pResource) {
         SetError(hDevice, E_INVALIDARG);
         return;
      }
      memset(pResource, 0, sizeof(*pResource));
      HRESULT result = AgxD3d10WindowsPresentationCreate(
         pDevice->windows, pCreateResource, hRTResource,
         &pResource->presentation);
      if (FAILED(result)) {
         SetError(hDevice, result);
         return;
      }
      pResource->owner_device = pDevice;
      pResource->Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      pResource->MipLevels = 1;
      pResource->NumSubResources = 1;
      return;
   }

   memset(pResource, 0, sizeof *pResource);'''),
        ('''   pResource->resource = screen->resource_create(screen, &templat);
   if (!pResource) {
      DebugPrintf("%s: failed to create resource\\n", __func__);
      SetError(hDevice, E_OUTOFMEMORY);
      return;
   }''','''   if (wantsConstant || wantsIndex) {
      pResource->resource = screen->resource_create(screen, &templat);
   } else if (pCreateResource->BindFlags & D3D10_DDI_BIND_RENDER_TARGET) {
      const D3D10DDI_MIPINFO *mip = pCreateResource->pMipInfoList;
      bool private_rt = pCreateResource->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D &&
         pCreateResource->Format == DXGI_FORMAT_B8G8R8A8_UNORM &&
         pCreateResource->MipLevels == 1 && pCreateResource->ArraySize == 1 && mip &&
         mip[0].TexelWidth > 0 && mip[0].TexelHeight > 0 && mip[0].TexelDepth == 1 &&
         mip[0].TexelWidth <= 4096 && mip[0].TexelHeight <= 4096 &&
         ((uint64_t)mip[0].TexelWidth * mip[0].TexelHeight * 4) <= 0x100000 &&
         pCreateResource->SampleDesc.Count == 1 && pCreateResource->SampleDesc.Quality == 0 &&
         pCreateResource->Usage == D3D10_DDI_USAGE_DEFAULT && pCreateResource->MapFlags == 0 &&
         pCreateResource->BindFlags == D3D10_DDI_BIND_RENDER_TARGET &&
         pCreateResource->MiscFlags == 0 && !pCreateResource->pPrimaryDesc &&
         !pCreateResource->pInitialDataUP;
      if (!private_rt || !screen->resource_create_with_modifiers) {
         LOG_UNSUPPORTED("Only a private uncompressed BGRA8 render target is admitted");
         SetError(hDevice, E_NOTIMPL);
         return;
      }
      const uint64_t modifier = DRM_FORMAT_MOD_APPLE_GPU_TILED;
      pResource->resource = screen->resource_create_with_modifiers(
         screen, &templat, &modifier, 1);
   } else {
      pResource->resource = screen->resource_create(screen, &templat);
   }
   if (!pResource->resource) {
      DebugPrintf("%s: failed to create resource\\n", __func__);
      SetError(hDevice, E_OUTOFMEMORY);
      return;
   }
   if (wantsConstant) {
      pResource->constant_buffer = true;
      pResource->logical_bytes = resourceMip[0].TexelWidth;
      pResource->owner_device = pDevice;
      pResource->owner_cookie = resourceOwner;
      pResource->device_generation = resourceGeneration;
   } else if (wantsIndex) {
      pResource->index_buffer = true;
      pResource->logical_bytes = 8;
      pResource->owner_device = pDevice;
      pResource->owner_cookie = resourceOwner;
      pResource->device_generation = resourceGeneration;
   }'''),
        ('''ResourceCopy(D3D10DDI_HDEVICE hDevice,          // IN
             D3D10DDI_HRESOURCE hDstResource,   // IN
             D3D10DDI_HRESOURCE hSrcResource)   // IN
{
   LOG_ENTRYPOINT();''','''ResourceCopy(D3D10DDI_HDEVICE hDevice,          // IN
             D3D10DDI_HRESOURCE hDstResource,   // IN
             D3D10DDI_HRESOURCE hSrcResource)   // IN
{
   SetError(hDevice, E_NOTIMPL);
   return;'''),
        ('''ResourceCopyRegion(D3D10DDI_HDEVICE hDevice,                // IN
                   D3D10DDI_HRESOURCE hDstResource,         // IN
                   UINT DstSubResource,                     // IN
                   UINT DstX,                               // IN
                   UINT DstY,                               // IN
                   UINT DstZ,                               // IN
                   D3D10DDI_HRESOURCE hSrcResource,         // IN
                   UINT SrcSubResource,                     // IN
                   __in_opt const D3D10_DDI_BOX *pSrcBox)   // IN (optional)
{
   LOG_ENTRYPOINT();''','''ResourceCopyRegion(D3D10DDI_HDEVICE hDevice,                // IN
                   D3D10DDI_HRESOURCE hDstResource,         // IN
                   UINT DstSubResource,                     // IN
                   UINT DstX,                               // IN
                   UINT DstY,                               // IN
                   UINT DstZ,                               // IN
                   D3D10DDI_HRESOURCE hSrcResource,         // IN
                   UINT SrcSubResource,                     // IN
                   __in_opt const D3D10_DDI_BOX *pSrcBox)   // IN (optional)
{
   SetError(hDevice, E_NOTIMPL);
   return;'''),
        ('''ResourceResolveSubResource(D3D10DDI_HDEVICE hDevice,        // IN
                           D3D10DDI_HRESOURCE hDstResource, // IN
                           UINT DstSubResource,             // IN
                           D3D10DDI_HRESOURCE hSrcResource, // IN
                           UINT SrcSubResource,             // IN
                           DXGI_FORMAT ResolveFormat)       // IN
{
   LOG_UNSUPPORTED_ENTRYPOINT();''','''ResourceResolveSubResource(D3D10DDI_HDEVICE hDevice,        // IN
                           D3D10DDI_HRESOURCE hDstResource, // IN
                           UINT DstSubResource,             // IN
                           D3D10DDI_HRESOURCE hSrcResource, // IN
                           UINT SrcSubResource,             // IN
                           DXGI_FORMAT ResolveFormat)       // IN
{
   SetError(hDevice, E_NOTIMPL);
   return;''')])
    replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','OpenResource','''   Device *pDevice = CastDevice(hDevice);
   Resource *pResource = CastResource(hResource);
   if (!pDevice || !pResource || !pOpenResource || !hRTResource.handle) {
      SetError(hDevice, E_INVALIDARG);
      return;
   }
   memset(pResource, 0, sizeof(*pResource));
   HRESULT result = AgxD3d10WindowsPresentationOpen(
      pDevice->windows, pOpenResource, hRTResource, &pResource->presentation);
   if (FAILED(result)) {
      SetError(hDevice, result);
      return;
   }
   pResource->owner_device = pDevice;
   pResource->Format = DXGI_FORMAT_B8G8R8A8_UNORM;
   pResource->MipLevels = 1;
   pResource->NumSubResources = 1;''')
    resource_path=out/'src/gallium/frontends/d3d10umd/Resource.cpp'
    resource_text=resource_path.read_text()
    destroy_anchor='''   Resource *pResource = CastResource(hResource);

   if (pResource->so_target) {'''
    destroy_replacement='''   Device *pDevice = CastDevice(hDevice);
   Resource *pResource = CastResource(hResource);

   if (pResource && pResource->presentation) {
      HRESULT result = AgxD3d10WindowsPresentationDestroy(
         pDevice->windows, &pResource->presentation);
      if (FAILED(result)) SetError(hDevice, result);
      return;
   }

   if (pResource->so_target) {'''
    if resource_text.count(destroy_anchor)!=1:
        raise SystemExit('Ambiguous presentation DestroyResource anchor')
    resource_path.write_text(resource_text.replace(destroy_anchor,destroy_replacement))
    overlays['src/gallium/frontends/d3d10umd/Resource.cpp']['after']=hashlib.sha256(resource_path.read_bytes()).hexdigest()
    replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','ResourceUpdateSubResourceUP','''   Device *pDevice = CastDevice(hDevice);
   Resource *resource = CastResource(hDstResource);
   ULONGLONG owner = 0;
   ULONG generation = 0;
   bool identity = pDevice && AgxD3d10WindowsIdentity(
      pDevice->windows, &owner, &generation);
   bool valid = pDevice && resource && resource->constant_buffer &&
      resource->owner_device == pDevice && DstSubResource == 0 && !pDstBox &&
      pSysMemUP && resource->resource && resource->resource->target == PIPE_BUFFER &&
      (resource->resource->bind & PIPE_BIND_CONSTANT_BUFFER) &&
      !(resource->resource->bind &
        ~(PIPE_BIND_CONSTANT_BUFFER | PIPE_BIND_SHADER_IMAGE)) &&
      resource->logical_bytes >= 16 && resource->logical_bytes <= 65536 &&
      (resource->logical_bytes & 15) == 0 &&
      resource->resource->width0 == resource->logical_bytes &&
      identity &&
      resource->owner_cookie == owner && resource->device_generation == generation;
   if (!valid) {
      SetError(hDevice, E_INVALIDARG);
      return;
   }
   HRESULT result = AgxD3d10WindowsFlushRetire(pDevice->windows);
   if (FAILED(result)) {
      SetError(hDevice, result);
      return;
   }
   struct pipe_box box = {0, 0, 0, (int)resource->logical_bytes, 1, 1};
   struct pipe_transfer *transfer = NULL;
   void *map = pDevice->pipe->buffer_map(pDevice->pipe, resource->resource, 0,
                                          PIPE_MAP_WRITE, &box, &transfer);
   if (!map || !transfer) {
      SetError(hDevice, E_OUTOFMEMORY);
      return;
   }
   memcpy(map, pSysMemUP, resource->logical_bytes);
   pipe_buffer_unmap(pDevice->pipe, transfer);
   (void)RowPitch;
   (void)DepthPitch;''')
    change('src/gallium/frontends/d3d10umd/InputAssembly.cpp',
        '210b330c3327042d230a65ff5a7242df89ddb385d50f61bcacfc996d39c55bbd',[
        ('   static const float dummy[4] = {0.0f, 0.0f, 0.0f, 0.0f};\n\n',''),
        ('''      else {
         pDevice->vertex_strides[StartBuffer + i] = 0;
         vb->buffer_offset = 0;
         if (!vb->is_user_buffer) {
            pipe_resource_reference(&vb->buffer.resource, NULL);
            vb->is_user_buffer = true;
         }
         vb->buffer.user = dummy;
      }''','''      else {
         pDevice->vertex_strides[StartBuffer + i] = 0;
         vb->buffer_offset = 0;
         if (vb->is_user_buffer) {
            vb->buffer.user = NULL;
            vb->is_user_buffer = false;
         } else {
            pipe_resource_reference(&vb->buffer.resource, NULL);
         }
      }'''),
        ('''   for (i = 0; i < PIPE_MAX_ATTRIBS; ++i) {
      struct pipe_vertex_buffer *vb = &pDevice->vertex_buffers[i];

      /* XXX this is odd... */
      if (!vb->is_user_buffer && !vb->buffer.resource) {
         pDevice->vertex_strides[i] = 0;
         vb->buffer_offset = 0;
         vb->is_user_buffer = true;
         vb->buffer.user = dummy;
      }
   }

''','')])
    replace_function_body('src/gallium/frontends/d3d10umd/InputAssembly.cpp','IaSetIndexBuffer','''   Device *pDevice = CastDevice(hDevice);
   Resource *resource = CastResource(hBuffer);
   if (!resource) {
      pipe_resource_reference(&pDevice->index_buffer, NULL);
      pDevice->index_size = 0;
      pDevice->restart_index = 0;
      pDevice->ib_offset = 0;
      return;
   }
   ULONGLONG owner = 0;
   ULONG generation = 0;
   bool valid = pDevice && Format == DXGI_FORMAT_R16_UINT && Offset == 0 &&
      resource->index_buffer && resource->logical_bytes == 8 &&
      resource->owner_device == pDevice && resource->resource &&
      resource->resource->target == PIPE_BUFFER &&
      (resource->resource->bind & PIPE_BIND_INDEX_BUFFER) &&
      !(resource->resource->bind &
        ~(PIPE_BIND_INDEX_BUFFER | PIPE_BIND_SHADER_IMAGE)) &&
      resource->resource->width0 == 8 &&
      AgxD3d10WindowsIdentity(pDevice->windows,&owner,&generation) &&
      resource->owner_cookie == owner &&
      resource->device_generation == generation;
   if (!valid) {
      SetError(hDevice, E_NOTIMPL);
      return;
   }
   pDevice->ib_offset = 0;
   pDevice->index_size = 2;
   pDevice->restart_index = 0;
   pipe_resource_reference(&pDevice->index_buffer, resource->resource);''')
    change('src/asahi/lib/agx_device.h',
        'e6ba76e16b2aace0ebf8b1ff2348cb800ad6cc254cef633d490de5bc203bfda3',[
        ('#include <xf86drm.h>', '#ifndef _WIN32\n#include <xf86drm.h>\n#else\n#include <stddef.h>\n#include "c11/threads.h"\ntypedef ptrdiff_t ssize_t;\n#endif'),
        ('#include "vdrm.h"\n\n#include "asahi_proto.h"',
         '#ifndef _WIN32\n#include "vdrm.h"\n#include "asahi_proto.h"\n#else\nstruct vdrm_device;\nstruct asahi_ccmd_submit_res;\n#endif'),
        ('   pthread_mutex_t bo_map_lock;', '#ifdef _WIN32\n   mtx_t bo_map_lock;\n#else\n   pthread_mutex_t bo_map_lock;\n#endif'),
        ('   struct u_printf_ctx printf;\n};',
         '   struct u_printf_ctx printf;\n#ifdef _WIN32\n   void *windows_private; /* Windows native allocation owner, never a GPU address */\n#endif\n};')])
    change('include/drm-uapi/drm.h',
        '1617ef3ed0c0ceb7c1d37f828d529306478313c4363c0d6d597e11503e025f07',[
        ('#if defined(__GNU__)\n#include <sys/ioctl.h>',
         '#if defined(_WIN32)\n/* Integer declarations only: Windows has no DRM ioctl transport. */\n#elif defined(__GNU__)\n#include <sys/ioctl.h>')])
    change('include/drm-uapi/asahi_drm.h',
        '69fe416b7294dfec4794217bd11379effd53caff4e86010bb803f1b34bdf5e89',[
        ('#define DRM_IOCTL_ASAHI(__access, __id, __type)',
         '#ifndef _WIN32\n#define DRM_IOCTL_ASAHI(__access, __id, __type)'),
        ('#if defined(__cplusplus)\n}\n#endif', '#endif /* !_WIN32: no Linux ioctl numbers in Windows producer */\n#if defined(__cplusplus)\n}\n#endif')])
    linker=(out/'src/asahi/lib/agx_linker.h').read_text()
    start=linker.index('struct agx_fs_epilog_link_info {')
    end=linker.index('\n};',start)+3
    original=linker[start:end]
    # Native byte layout is 3 octets then 8 flag bits, size/alignment 4.
    # Match that layout without weakening the native assertion or bool semantics.
    adapted=original.replace('struct agx_fs_epilog_link_info {',
        'struct __attribute__((aligned(4))) agx_fs_epilog_link_info {').replace('   unsigned ', '   uint8_t ')
    change('src/asahi/lib/agx_linker.h',
        'bd469422d642fdbd136c978446426446d85b80070378e16fece842287136c6ff',[(original,adapted)])
    change('src/asahi/libagx/libagx_dgc.h',
        'ab8db337b1c95bec23aebcbf92521a98f862a4fb7b23a0f2828e20ffd336eecc',[
        ('   uint range_B = d.index_buffer_range_B;',
         '   uint32_t range_B = d.index_buffer_range_B;')])
    if args.project:
        change('src/asahi/lib/pool.c',
            '1420e8cbc883bad80bc014a285a922fffe47da92597f1671c6ebe8f4df8ab4ed',[
            ('   util_dynarray_append(&pool->bos, bo);',
             '   if (!bo) return NULL;\n   util_dynarray_append(&pool->bos, bo);'),
            ('   assert(alignment == util_next_power_of_two(alignment));',
             '   assert(alignment == util_next_power_of_two(alignment));\n   if (out_bo) *out_bo = NULL;'),
            ('   pool->transient_offset = offset + sz;\n\n   struct agx_ptr ret = {\n      .cpu = agx_bo_map(bo) + offset,',
             '   if (!bo) return (struct agx_ptr){0};\n   void *mapped = agx_bo_map(bo);\n   if (!mapped) return (struct agx_ptr){0};\n   pool->transient_offset = offset + sz;\n\n   struct agx_ptr ret = {\n      .cpu = (char *)mapped + offset,'),
            ('   memcpy(transfer.cpu, data, sz);',
             '   if (!transfer.cpu) return 0;\n   memcpy(transfer.cpu, data, sz);')])
        # The initial VDM/CDM allocation is a source-defined encoder intent.
        # Keep the shared Batch pool unchanged: continuation streams remain a
        # separate General-backed/unsupported contract.
        change('src/gallium/drivers/asahi/agx_batch.c',
            'b99fac69d414e0f34420ef38243d31da644745f62d88a50835d734e627997778',[
            ('#include <xf86drm.h>', '#ifndef _WIN32\n#include <xf86drm.h>\n#endif'),
            ('#include "agx_state.h"', '#include "agx_state.h"\n#include "agx_win32_asahi_bo.h"'),
            ('struct agx_bo *bo = agx_bo_create(dev, 0x80000, 0, 0, "Encoder");',
             'struct agx_bo *bo = AgxWin32AsahiEncoderCreate(dev, 0x80000, 0, "Encoder");')])
        batch_source=(out/'src/gallium/drivers/asahi/agx_batch.c').read_text()
        batch_begin=batch_source.index('struct agx_encoder\nagx_encoder_allocate(')
        batch_end=batch_source.index('\n}\n',batch_begin)+3
        batch_body=batch_source[batch_begin:batch_end]
        (out/'native_batch_encoder_contract.c').write_text(
            '#include "gallium/drivers/asahi/agx_state.h"\n'
            '#include "agx_win32_asahi_bo.h"\n'+batch_body+
            '\nstruct agx_encoder AgxWin32NativeEncoderAllocateTest('
            'struct agx_batch *batch, struct agx_device *dev) {\n'
            '  return agx_encoder_allocate(batch, dev);\n}\n')
        overlays['src/gallium/drivers/asahi/agx_batch.c']={
            'before':'b99fac69d414e0f34420ef38243d31da644745f62d88a50835d734e627997778',
            'after':hashlib.sha256((out/'src/gallium/drivers/asahi/agx_batch.c').read_bytes()).hexdigest(),
            'focused_encoder_body_sha256':hashlib.sha256(batch_body.encode()).hexdigest()}
    # Canonical helper has no arguments. Windows cannot represent the GNU
    # zero-sized host record: keep an opaque host placeholder and explicitly
    # dispatch the source-declared zero byte payload. Other generated argument
    # layouts and assertions remain unchanged.
    shutil.copy2(Path(r'C:\Users\pauls\AD04-umd-owner-004-generated\src\asahi\lib\libagx_shaders.h'),
                 out/'src/asahi/lib/libagx_shaders.h')
    change('src/asahi/lib/libagx_shaders.h',
        'b10e6ec36950ecd55878f4665f644969a93ba7bdbeb75f7c6584e8a1da11bc37',[
        ('struct libagx_helper_args {\n} PACKED;',
         'enum { LIBAGX_HELPER_ARGS_BYTES = 0 };\nstruct libagx_helper_args {\n   uint8_t host_placeholder;\n} PACKED;'),
        ('static_assert(sizeof(struct libagx_helper_args) == 0, "");',
         'static_assert(LIBAGX_HELPER_ARGS_BYTES == 0, "zero argument wire payload");')])
    target=out/'src/asahi/lib/libagx_shaders.h'
    text=target.read_text()
    old='MESA_DISPATCH_PRECOMP(_context, _grid, _barrier, LIBAGX_HELPER, &_args, sizeof(_args));'
    if text.count(old)!=2: raise SystemExit('Helper dispatch anchor mismatch')
    text=text.replace(old, 'MESA_DISPATCH_PRECOMP(_context, _grid, _barrier, LIBAGX_HELPER, &_args, LIBAGX_HELPER_ARGS_BYTES);')
    target.write_text(text)
    overlays['src/asahi/lib/libagx_shaders.h']['after']=hashlib.sha256(target.read_bytes()).hexdigest()
    source=out/'src/gallium/drivers/asahi/agx_state.c'
    if args.project:
        # Register edges at the actual native emission points. The immutable
        # source is hash checked; no replacement state producer is introduced.
        state=source.read_text()
        begin=state.index('static uint32_t\nagx_build_pipeline(')
        end=state.index('\nstatic void\nagx_launch_internal(',begin)
        original=state[begin:end]
        pipeline=original
        def emit_replace(old,new):
            global pipeline
            if pipeline.count(old)!=1: raise SystemExit('Ambiguous native pipeline capture anchor')
            pipeline=pipeline.replace(old,new)
        emit_replace('   struct agx_usc_builder b = agx_usc_builder(t.cpu, usc_size);',
            '''   AGX_WIN32_ASAHI_PIPELINE capture;
   if (!AgxWin32AsahiPipelineBegin(dev, t.cpu, t.gpu, usc_size, &capture))
      return 0;
   struct agx_usc_builder b = agx_usc_builder(t.cpu, usc_size);''')
        emit_replace('         cfg.buffer = batch->stage_uniforms[stage].texture_base;\n      }',
            '''         cfg.buffer = batch->stage_uniforms[stage].texture_base;
      }
      AgxWin32AsahiPipelineRecord(&capture, b.head,
         AppleAgxWin32RelocationUscTableAddress39,
         batch->stage_uniforms[stage].texture_base,
         MIN2(batch->texture_count[stage], AGX_NUM_TEXTURE_STATE_REGS) * AGX_TEXTURE_LENGTH,
         AppleAgxWin32RoleDescriptor);''')
        emit_replace('         cfg.buffer = batch->samplers[stage];\n      }',
            '''         cfg.buffer = batch->samplers[stage];
      }
      AgxWin32AsahiPipelineRecord(&capture, b.head,
         AppleAgxWin32RelocationUscTableAddress39, batch->samplers[stage],
         batch->sampler_count[stage] * (AGX_SAMPLER_LENGTH +
            (ctx->stage[stage].custom_borders ? AGX_BORDER_LENGTH : 0)),
         AppleAgxWin32RoleDescriptor);''')
        emit_replace('                      table_ptr + cs->push[i].offset);',
            '''                      table_ptr + cs->push[i].offset);
      AgxWin32AsahiPipelineRecord(&capture, b.head,
         AppleAgxWin32RelocationUscBufferAddress40,
         table_ptr + cs->push[i].offset, cs->push[i].length * 2,
         AppleAgxWin32RoleConstant);''')
        emit_replace('      agx_usc_immediates(&b, &cs->b.info.rodata, cs->bo->va->addr);',
            '''      uint8_t *rodata_begin = b.head;
      agx_usc_immediates(&b, &cs->b.info.rodata, cs->bo->va->addr);
      for (unsigned range = 0; range < constant_push_ranges; ++range) {
         unsigned offset = range * 64;
         AgxWin32AsahiPipelineRecord(&capture,
            rodata_begin + (range + 1) * AGX_USC_UNIFORM_LENGTH,
            AppleAgxWin32RelocationUscBufferAddress40,
            cs->bo->va->addr + cs->b.info.rodata.offset + offset * 2,
            MIN2(64, cs->b.info.rodata.size_16 - offset) * 2,
            AppleAgxWin32RoleShaderRodata);
      }''')
        # Scratch is not a captured graph yet. Reject before allocating or
        # publishing it; do not supply a successful scratch placeholder.
        scratch_start=pipeline.index('   if (max_scratch_size > 0) {')
        scratch_end=pipeline.index('\n   if (stage == MESA_SHADER_FRAGMENT)',scratch_start)
        pipeline=pipeline[:scratch_start]+'''   bool linked_scratch_unsupported = false;
   if (linked) {
      uint32_t packed_regs;
      struct AGX_USC_REGISTERS regs;
      memcpy(&packed_regs, &linked->regs, sizeof(packed_regs));
      linked_scratch_unsupported =
         !AGX_USC_REGISTERS_unpack(NULL, (const uint8_t *)&packed_regs, &regs) ||
         regs.spill_size != 0;
   }
   if (max_scratch_size > 0 || linked_scratch_unsupported) {
      capture.Failed = 1;
      (void)AgxWin32AsahiPipelineFinish(&capture, b.head);
      return 0;
   }
'''+pipeline[scratch_end:]
        emit_replace('      agx_usc_push_packed(&b, SHADER, linked->shader);',
            '''      agx_usc_push_packed(&b, SHADER, linked->shader);
      AgxWin32AsahiPipelineRecord(&capture, b.head,
         AppleAgxWin32RelocationUscShaderOffset32, linked->bo->va->addr,
         linked->bo->size, AppleAgxWin32RoleShader);''')
        emit_replace('         cfg.unk_2 = 3;\n      }',
            '''         cfg.unk_2 = 3;
      }
      AgxWin32AsahiPipelineRecord(&capture, b.head,
         AppleAgxWin32RelocationUscShaderOffset32,
         cs->bo->va->addr + cs->b.info.main_offset,
         cs->b.info.main_size,
         AppleAgxWin32RoleShader);''')
        emit_replace('         cfg.spill_size = cs->b.info.scratch_size\n                             ? agx_scratch_get_bucket(cs->b.info.scratch_size)\n                             : 0;',
            '         cfg.spill_size = 0; /* Nonzero scratch rejected above. */')
        emit_replace('            agx_usc_addr(dev, cs->bo->va->addr + cs->b.info.preamble_offset);\n      }',
            '''            agx_usc_addr(dev, cs->bo->va->addr + cs->b.info.preamble_offset);
      }
      unsigned preamble_end = cs->b.info.binary_size;
      if (cs->b.info.main_offset > cs->b.info.preamble_offset)
         preamble_end = MIN2(preamble_end, cs->b.info.main_offset);
      if (cs->b.info.rodata.size_16 &&
          cs->b.info.rodata.offset > cs->b.info.preamble_offset)
         preamble_end = MIN2(preamble_end, cs->b.info.rodata.offset);
      AgxWin32AsahiPipelineRecord(&capture, b.head,
         AppleAgxWin32RelocationUscPreshaderOffset32,
         cs->bo->va->addr + cs->b.info.preamble_offset,
         cs->b.info.preamble_offset < preamble_end ?
            preamble_end - cs->b.info.preamble_offset : 0,
         AppleAgxWin32RoleShader);''')
        emit_replace('   return agx_usc_addr(dev, t.gpu);',
            '''   if (!AgxWin32AsahiPipelineFinish(&capture, b.head))
      return 0;
   return agx_usc_addr(dev, t.gpu);''')
        # Only this original function is exported to the controlled contract
        # test. No replacement function body or renderer is compiled there.
        pipeline+='''
#ifdef AGX_WIN32_NATIVE_PIPELINE_TEST
uint32_t AgxWin32NativeBuildPipelineTest(struct agx_batch *batch,
   struct agx_compiled_shader *cs, struct agx_linked_shader *linked,
   mesa_shader_stage stage) {
   return agx_build_pipeline(batch, cs, linked, stage, 0, 0);
}
#endif
'''
        # One shared body is compiled both by the native state unit and the
        # focused contract unit. This avoids resolving unrelated Gallium/NIR
        # functions just to execute this function with MSVC's linker.
        inc=out/'src/gallium/drivers/asahi/agx_win32_pipeline.inc'
        inc.write_text(state[:state.index('#include')]+
            '#include "agx_win32_asahi_pipeline.h"\n'+pipeline)
        overlays['src/gallium/drivers/asahi/agx_win32_pipeline.inc']={
            'source_function_sha256':hashlib.sha256(original.encode()).hexdigest(),
            'after':hashlib.sha256(inc.read_bytes()).hexdigest()}
        change('src/gallium/drivers/asahi/agx_state.c',digest,
            [(original,'#include "agx_win32_pipeline.inc"\n')])
        # State emission is captured only in an active Windows request. The
        # original no-capture path remains byte-for-byte behaviorally native.
        state_target=out/'src/gallium/drivers/asahi/agx_state.c'
        state_text=state_target.read_text()
        state_begin=state_text.index('static uint8_t *\nagx_encode_state(')
        state_end=state_text.index('\n}\n\nstatic enum agx_primitive',state_begin)+3
        state_body=state_text[state_begin:state_end]
        state_body=state_body.replace(
            '   if (!ctx->dirty)\n      return out;',
            '''   if (!ctx->dirty)
      return out;

   AGX_WIN32_ASAHI_PIPELINE state_capture = {0};
   AGX_WIN32_ASAHI_BACKEND *windows_backend = dev->windows_private;
   bool capture_active = windows_backend && windows_backend->ActiveCapture;
   if (capture_active && !AgxWin32AsahiEncoderEmissionBeginCpu(
           dev, out, (APPLE_AGX_U32)(batch->vdm.end - out),
           &state_capture)) {
      windows_backend->Failed = 1;
      return out;
   }''',1)
        state_body=state_body.replace(
            '   assert(ppp_updates <= MAX_PPP_UPDATES);\n   return out;',
            '''   assert(ppp_updates <= MAX_PPP_UPDATES);
   if (capture_active && !AgxWin32AsahiPipelineFinish(&state_capture, out)) {
      windows_backend->Failed = 1;
      return out;
   }
   return out;''',1)
        def state_replace(old,new):
            global state_body
            if state_body.count(old)!=1:
                raise SystemExit('Ambiguous native PPP/state capture anchor')
            state_body=state_body.replace(old,new,1)
        state_replace('      agx_push(out, VDM_STATE_VERTEX_SHADER_WORD_1, cfg) {\n         cfg.pipeline =',
            """      uint32_t windows_vs_pipeline = 0;
      agx_push(out, VDM_STATE_VERTEX_SHADER_WORD_1, cfg) {
         cfg.pipeline = windows_vs_pipeline =""")
        state_replace('                               MESA_SHADER_VERTEX, 0, 0);\n      }',
            """                               MESA_SHADER_VERTEX, 0, 0);
      }
      if (capture_active)
         AgxWin32AsahiPipelineRecordCaptured(&state_capture, out,
            AppleAgxWin32RelocationVdmPipelineOffset32,
            dev->shader_base + windows_vs_pipeline, AppleAgxWin32RoleUscPipeline);""")
        state_replace('   struct agx_ppp_update ppp = agx_new_ppp_update(T, size, &dirty);',
            """   struct agx_ppp_update ppp = agx_new_ppp_update(T, size, &dirty);
   AGX_WIN32_ASAHI_PIPELINE ppp_capture = {0};
   if (capture_active && !AgxWin32AsahiEmissionBegin(dev, T.cpu, T.gpu,
           (APPLE_AGX_U32)size, AppleAgxWin32RolePppState, &ppp_capture)) {
      state_capture.Failed = 1;
      (void)AgxWin32AsahiPipelineFinish(&state_capture, out);
      windows_backend->Failed = 1;
      return out;
   }""")
        state_replace('      agx_ppp_push(&ppp, FRAGMENT_SHADER_WORD_1, cfg) {\n         cfg.pipeline = agx_build_pipeline',
            """      uint32_t windows_fs_pipeline = 0;
      agx_ppp_push(&ppp, FRAGMENT_SHADER_WORD_1, cfg) {
         cfg.pipeline = windows_fs_pipeline = agx_build_pipeline""")
        state_replace('                                           MESA_SHADER_FRAGMENT, 0, 0);\n      }',
            """                                           MESA_SHADER_FRAGMENT, 0, 0);
      }
      if (capture_active)
         AgxWin32AsahiPipelineRecordCaptured(&ppp_capture, ppp.head,
            AppleAgxWin32RelocationPppPipelineOffset32,
            dev->shader_base + windows_fs_pipeline, AppleAgxWin32RoleUscPipeline);""")
        state_replace('         cfg.cf_bindings = batch->varyings;\n      }',
            """         cfg.cf_bindings = batch->varyings;
      }
      if (capture_active && ctx->linked.fs->cf.nr_bindings)
         AgxWin32AsahiPipelineRecord(&ppp_capture, ppp.head,
            AppleAgxWin32RelocationPppCfBindingsOffset32,
            dev->shader_base + batch->varyings,
            AGX_CF_BINDING_HEADER_LENGTH +
               ctx->linked.fs->cf.nr_bindings * AGX_CF_BINDING_LENGTH,
            AppleAgxWin32RoleDescriptor);
      else if (capture_active && batch->varyings)
         ppp_capture.Failed = 1;""")
        state_replace('   agx_ppp_fini(&out, &ppp);',
            """   if (capture_active && !AgxWin32AsahiPipelineFinish(&ppp_capture, ppp.head))
      state_capture.Failed = 1;
   agx_ppp_fini(&out, &ppp);
   if (capture_active)
      AgxWin32AsahiPipelineRecordCaptured(&state_capture, out,
         AppleAgxWin32RelocationPppStateAddress40, T.gpu, AppleAgxWin32RolePppState);""")
        if state_body==state_text[state_begin:state_end]:
            raise SystemExit('Native state emission capture anchors missing')
        # Export only a test wrapper around the exact transformed native body.
        # The fixture does not reproduce or reinterpret state emission.
        state_wrapper='''\n#ifdef AGX_WIN32_NATIVE_PIPELINE_TEST
uint8_t *AgxWin32NativeEncodeStateTest(struct agx_batch *batch, uint8_t *out) {
   return agx_encode_state(batch, out);
}
#endif
'''
        state_text=state_text[:state_begin]+state_body+state_wrapper+state_text[state_end:]
        # Guard the actual void draw caller, not the void reserve helper: a
        # failed helper must never fall through to state/draw writes. Preserve
        # the source-defined draw estimate and check before alloc/pool jump.
        reserve_start=state_text.index('   agx_ensure_cmdbuf_has_space(\n      batch, &batch->vdm,')
        reserve_end=state_text.index(');',reserve_start)+2
        reserve=state_text[reserve_start:reserve_end]
        estimate=reserve.split('batch, &batch->vdm,\n',1)[1][:-2].strip()
        guard='''   if (!AgxWin32AsahiEncoderDrawPreflight(
          agx_device(ctx->base.screen), batch->vdm.bo,
          batch->vdm.current, batch->vdm.end,
          %s))
      return;

''' % estimate
        state_text=state_text[:reserve_start]+guard+state_text[reserve_start:]
        encode_call='   uint8_t *out = agx_encode_state(batch, batch->vdm.current);'
        if state_text.count(encode_call)!=1:
            raise SystemExit('Ambiguous native draw state failure propagation anchor')
        state_text=state_text.replace(encode_call,'''   AGX_WIN32_ASAHI_BACKEND *windows_draw_backend =
      agx_device(ctx->base.screen)->windows_private;
   AGX_WIN32_ASAHI_CAPTURE *windows_draw_capture = windows_draw_backend ?
      windows_draw_backend->ActiveCapture : NULL;
   uint8_t *out = agx_encode_state(batch, batch->vdm.current);
   if (windows_draw_capture &&
       (windows_draw_backend->ActiveCapture != windows_draw_capture ||
        windows_draw_backend->Failed || windows_draw_capture->Capture.State != 1u)) {
      windows_draw_backend->Failed = 1;
      return;
   }''',1)
        state_target.write_text(state_text)
        overlays['src/gallium/drivers/asahi/agx_state.c']['after_state_capture']=hashlib.sha256(state_target.read_bytes()).hexdigest()
        (out/'native_pipeline_contract.c').write_text(
            '#include "gallium/drivers/asahi/agx_state.h"\n'
            '#include "agx_usc.h"\n#include "agx_linker.h"\n'
            '#include "gallium/drivers/asahi/agx_win32_pipeline.inc"\n')

        if args.native_batch_lifecycle:
            for module_name in ('native-asahi-graph-capture', 'native-asahi-batch-lifecycle'):
                module_path = args.project/'drivers/apple-agx/mesa/scripts'/(module_name+'.py')
                spec = importlib.util.spec_from_file_location(module_name.replace('-', '_'), module_path)
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
                module.project_sources(out, args.project, overlays)
            # The last source transform is authoritative for runtime compilation.
            for key, record in overlays.items():
                path = out/key
                if path.is_file():
                    record['final_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()

if args.prepare_only:
    prepared = {'exit': 0, 'prepared_only': True, 'native_batch_lifecycle': args.native_batch_lifecycle,
                'native_draw_executed': False, 'architecture': args.architecture,
                'native_source': str(out), 'overlays': overlays}
    (out/'inputs.json').write_text(json.dumps(prepared, indent=2)+'\n')
    (out/'result.json').write_text(json.dumps(prepared, indent=2)+'\n')
    print(json.dumps(prepared))
    raise SystemExit(0)

entry = next(e for e in json.loads((build/'compile_commands.json').read_text())
             if e['file'].replace('\\','/').endswith('/nir.c'))
argc = ctypes.c_int()
split = ctypes.windll.shell32.CommandLineToArgvW
split.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
split.restype = ctypes.POINTER(ctypes.c_wchar_p)
argv = split(entry['command'], ctypes.byref(argc))
try:
    flags = [argv[i] for i in range(1, argc.value)]
finally:
    ctypes.windll.kernel32.LocalFree(ctypes.cast(argv, ctypes.c_void_p))
flags = [a for a in flags if not a.startswith(('-I','/I','/Fo','/Fd'))
         and a not in ('/c','/showIncludes','/Zi',entry['file'])]
# Meson command comes from the MSVC NIR graph. This invocation uses clang-cl,
# whose packed attribute is required by native shared GPU parameter structs.
flags.append('/DHAVE_FUNC_ATTRIBUTE_PACKED=1')
if args.project:
    flags += ['/Gy','/DAGX_WIN32_NATIVE_PIPELINE_TEST=1']
if args.native_state_test: flags.append('/DAGX_WIN32_NATIVE_STATE_TEST=1')
if args.architecture=='arm64': flags.append('--target=aarch64-pc-windows-msvc')
includes = [build/'src', build/'include', mesa/'include', mesa/'src',
    mesa/'src/gallium/include', mesa/'src/gallium/auxiliary',
    mesa/'src/asahi/lib', mesa/'src/asahi/layout', mesa/'src/asahi/compiler',
    mesa/'src/compiler', mesa/'src/compiler/nir', build/'src/compiler',
    build/'src/compiler/nir', generated/'src/asahi/compiler',
    generated/'src/asahi/libagx', mesa/'src/asahi/libagx',
    Path(r'C:\Users\pauls\AD04-umd-owner-004-generated\src'),
    Path(r'C:\Users\pauls\AD04-umd-owner-004-generated\src\asahi\genxml'),
    Path(r'C:\Users\pauls\AD04-umd-owner-004-generated\src\asahi\lib'),
    mesa/'src/virtio/vdrm', mesa/'src/virtio/virtio-gpu']
if args.windows_platform_declarations:
    includes=[out/'include',out/'src',out/'src/asahi/lib',out/'src/asahi/layout',
              out/'src/asahi/compiler',out/'src/asahi/libagx',*includes]
if args.project:
    includes += [args.project/'drivers/apple-agx/mesa/winsys',
                 args.project/'drivers/apple-agx/shared/include']
env = os.environ.copy()
env['INCLUDE'] = ';'.join(str(p) for p in [vc/'include',
    sdk/'Include/10.0.26100.0/ucrt',sdk/'Include/10.0.26100.0/shared',
    sdk/'Include/10.0.26100.0/um'])
env['LIB']=';'.join(str(p) for p in [vc/'lib'/args.architecture,sdk/'Lib/10.0.26100.0/ucrt'/args.architecture,sdk/'Lib/10.0.26100.0/um'/args.architecture])
env['PATH']=str(vc/'bin/HostX64/x64')+';'+env['PATH']
if args.windows_platform_declarations:
    helper=out/'helper_args_test.c'
    helper.write_text('''
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "util/macros.h"
#include "libagx_shaders.h"
static unsigned calls, evaluations, failed;
static struct libagx_helper_args argument(void) {
  struct libagx_helper_args v={}; ++evaluations; return v;
}
static void capture(unsigned program, const void *data, size_t bytes) {
  (void)data; ++calls;
  if(program!=LIBAGX_HELPER || bytes!=0) ++failed;
}
#define MESA_DISPATCH_PRECOMP(c,g,b,p,a,n) capture(p,a,n)
int main(void) {
  libagx_helper(0,0,0);
  libagx_helper_struct(0,0,0,argument());
  if(calls!=2 || evaluations!=1 || failed) return 1;
  puts("LIBAGX_HELPER: both actual generated wrappers transmit zero bytes PASS");
  return 0;
}
''')
    helper_command=[str(llvm/'clang-cl.exe'),*flags,*('/I'+str(p) for p in includes),
                    str(helper),'/Fo'+str(out/'helper_args.obj'),'/Fe'+str(out/'helper_args.exe')]
    run=subprocess.run(helper_command,cwd=build,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/'helper-build.log').write_bytes(run.stdout)
    if run.returncode:
        print(run.stdout.decode(errors='replace')); raise SystemExit(run.returncode)
    if args.architecture=='x64':
        run=subprocess.run([str(out/'helper_args.exe')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (out/'helper-test.log').write_bytes(run.stdout)
        if run.returncode: raise SystemExit('Zero-payload wrapper proof failed')
command = [str(llvm/'clang-cl.exe'),*flags,*('/I'+str(p) for p in includes),
           '/c',str(source),'/Fo'+str(out/'agx_state.obj')]
(out/'inputs.json').write_text(json.dumps({'source':str(source),
    'sha256':digest,'command':command,'overlays':overlays,
    'scope':'Native agx_state compile only; OS-private mutex changes require backend implementation'},indent=2))
run = subprocess.run(command,cwd=build,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'build.log').write_bytes(run.stdout)
lines = run.stdout.decode(errors='replace').splitlines()
errors = [i for i,s in enumerate(lines) if 'error:' in s or 'fatal error' in s]
first = '\n'.join(lines[max(0,errors[0]-3):errors[0]+5]) if errors else ''
result = {'exit':run.returncode,'first_error':first,'native_draw_executed':False,
          'architecture':args.architecture}
if run.returncode==0 and args.windows_platform_declarations:
    pool=out/'src/asahi/lib/pool.c'
    pool_command=[str(llvm/'clang-cl.exe'),*flags,*('/I'+str(p) for p in includes),
                  '/c',str(pool),'/Fo'+str(out/'pool.obj')]
    pool_run=subprocess.run(pool_command,cwd=build,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/'pool-build.log').write_bytes(pool_run.stdout)
    result['pool_exit']=pool_run.returncode
    if pool_run.returncode:
        result['exit']=pool_run.returncode
        result['first_error']=pool_run.stdout.decode(errors='replace')[-3000:]
    else:
        result['objects']={name:hashlib.sha256((out/name).read_bytes()).hexdigest()
                           for name in ('agx_state.obj','pool.obj')}
        if args.project:
            batch=out/'src/gallium/drivers/asahi/agx_batch.c'
            batch_command=[str(llvm/'clang-cl.exe'),*flags,*('/I'+str(p) for p in includes),
                           '/c',str(batch),'/Fo'+str(out/'agx_batch.obj')]
            batch_run=subprocess.run(batch_command,cwd=build,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            (out/'batch-build.log').write_bytes(batch_run.stdout)
            result['batch_exit']=batch_run.returncode
            if batch_run.returncode:
                # Linux DRM/virtio tail is intentionally outside this focused
                # Windows encoder-allocation unit. Preserve its first error as
                # evidence without suppressing the independently executable
                # original agx_encoder_allocate contract below.
                result['batch_first_error']=batch_run.stdout.decode(errors='replace')[-3000:]
            else:
                result['objects']['agx_batch.obj']=hashlib.sha256((out/'agx_batch.obj').read_bytes()).hexdigest()
        if args.project and result['exit']==0:
            for name in ('agx_win32_asahi_bo','agx_win32_asahi_capture','agx_win32_asahi_pipeline','agx_win32_asahi_pool_test','agx_win32_asahi_pipeline_test','native_pipeline_contract','native_batch_encoder_contract'):
                src=(out/(name+'.c')) if name in ('native_pipeline_contract','native_batch_encoder_contract') else args.project/'drivers/apple-agx/mesa/winsys'/(name+'.c')
                command=[str(llvm/'clang-cl.exe'),*flags,*('/I'+str(p) for p in includes),
                         '/c',str(src),'/Fo'+str(out/(name+'.obj'))]
                built=subprocess.run(command,cwd=build,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                (out/(name+'.log')).write_bytes(built.stdout)
                if built.returncode:
                    result['exit']=built.returncode
                    result['first_error']=built.stdout.decode(errors='replace')[-3000:]
                    break
                result['objects'][name+'.obj']=hashlib.sha256((out/(name+'.obj')).read_bytes()).hexdigest()
            if result['exit']==0:
                # Same source-level ABI/native-pack/materializer proof on the
                # Windows target. Assertions must remain enabled in this test.
                winsys=args.project/'drivers/apple-agx/mesa/winsys'
                shared=args.project/'drivers/apple-agx/shared'
                kmd=args.project/'drivers/apple-agx/render-admission'
                sources=[winsys/(n+'.c') for n in ('agx_win32_reloc_capture_test',
                    'agx_win32_reloc_capture','agx_win32_native_pool_bridge','agx_win32_transport')]
                sources += [shared/'src/apple_agx_win32_abi.c',kmd/'src/apple_agx_dynamic_job.c']
                command=[str(llvm/'clang-cl.exe'),*flags,'/UNDEBUG',
                    *('/I'+str(p) for p in includes),'/I'+str(kmd/'include'),
                    *(str(s) for s in sources),'/Fe'+str(out/'reloc_capture_test.exe')]
                (out/'reloc-command.json').write_text(json.dumps(command,indent=2))
                built=subprocess.run(command,cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                (out/'reloc-build.log').write_bytes(built.stdout)
                result['reloc_build_exit']=built.returncode
                if built.returncode:
                    result['exit']=built.returncode
                    result['first_error']=built.stdout.decode(errors='replace')[-3000:]
                else:
                    result['reloc_exe_sha256']=hashlib.sha256((out/'reloc_capture_test.exe').read_bytes()).hexdigest()
                    result['reloc_execution']='NOT_RUN'
                    if args.architecture=='x64':
                        tested=subprocess.run([str(out/'reloc_capture_test.exe')],
                            cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                        (out/'reloc-test.log').write_bytes(tested.stdout)
                        result['reloc_execution']=tested.returncode
                        result['exit']=tested.returncode
(out/'result.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
raise SystemExit(result['exit'])
