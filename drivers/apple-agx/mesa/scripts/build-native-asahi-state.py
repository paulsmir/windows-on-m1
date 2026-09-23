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
import re
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
    (out/'src/gallium/auxiliary/nir').mkdir(parents=True,exist_ok=True)
    shutil.copy2(mesa/'src/gallium/auxiliary/nir/tgsi_to_nir.c',
                 out/'src/gallium/auxiliary/nir/tgsi_to_nir.c')
    shutil.copy2(mesa/'src/gallium/auxiliary/nir/tgsi_to_nir.h',
                 out/'src/gallium/auxiliary/nir/tgsi_to_nir.h')
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
    change('src/gallium/frontends/d3d10umd/ShaderTGSI.c',
        '375020e842babdedd9829367522297e4d6ae1288b571328d8f48ef2017b7cf81',[
        ('''   reg = ureg_DECL_fs_input(ureg,
                            translate_system_name(dcl_siv_name),
                            0,
                            TGSI_INTERPOLATE_CONSTANT);''','''   if (dcl_siv_name == D3D10_SB_NAME_POSITION) {
      /* The selected Asahi screen exposes fragment position as a system
       * value; an INPUT declaration violates TGSI-to-NIR's source contract. */
      ureg_property(ureg, TGSI_PROPERTY_FS_COORD_ORIGIN,
                    TGSI_FS_COORD_ORIGIN_UPPER_LEFT);
      ureg_property(ureg, TGSI_PROPERTY_FS_COORD_PIXEL_CENTER,
                    TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER);
      reg = ureg_DECL_system_value(ureg, TGSI_SEMANTIC_POSITION, 0);
   } else {
      reg = ureg_DECL_fs_input(ureg, translate_system_name(dcl_siv_name),
                              0, TGSI_INTERPOLATE_CONSTANT);
   }'''),
        ('''   reg = ureg_DECL_fs_input(ureg,
                            translate_system_name(dcl_siv_name),
                            0,
                            translate_interpolation(dcl_in_ps_interp));''','''   if (dcl_siv_name == D3D10_SB_NAME_POSITION) {
      /* The selected Asahi screen exposes fragment position as a system
       * value; an INPUT declaration violates TGSI-to-NIR's source contract. */
      ureg_property(ureg, TGSI_PROPERTY_FS_COORD_ORIGIN,
                    TGSI_FS_COORD_ORIGIN_UPPER_LEFT);
      ureg_property(ureg, TGSI_PROPERTY_FS_COORD_PIXEL_CENTER,
                    TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER);
      reg = ureg_DECL_system_value(ureg, TGSI_SEMANTIC_POSITION, 0);
   } else {
      reg = ureg_DECL_fs_input(ureg, translate_system_name(dcl_siv_name),
                              0, translate_interpolation(dcl_in_ps_interp));
   }''')])
    change('src/gallium/auxiliary/nir/tgsi_to_nir.c',
        '755f85617fa4f38923c759d1f27029c078c2bc47aead66abbb79130fea3f9b17',[
        ('''   nir_alu_type *samp_types;''','''   nir_alu_type *samp_types;
   unsigned *samp_targets;'''),
        ('''         c->samp_types[decl->Range.First + i] = type;''','''         c->samp_types[decl->Range.First + i] = type;
         c->samp_targets[decl->Range.First + i] = sview->Resource;'''),
        ('''   c->samp_types = rzalloc_array(c, nir_alu_type, c->num_samp_types);''','''   c->samp_types = rzalloc_array(c, nir_alu_type, c->num_samp_types);
   c->samp_targets = rzalloc_array(c, unsigned, c->num_samp_types);'''),
        ('''   BITSET_SET32(s->info.textures_used, scan.samplers_declared);''','''   BITSET_ZERO(s->info.textures_used);'''),
        ('''   BITSET_SET32(s->info.samplers_used, scan.samplers_declared);''','''   BITSET_ZERO(s->info.samplers_used);'''),
        ('''static nir_def *
ttn_tex(''','''/* D3D10 SAMPLE operands name the texture view and sampler independently. */
static nir_def *
ttn_d3d_sample(struct ttn_compile *c, nir_def **src)
{
   nir_builder *b = &c->build;
   struct tgsi_full_instruction *ins = &c->token->FullInstruction;
   unsigned texture = ins->Src[1].Register.Index;
   unsigned sampler = ins->Src[2].Register.Index;
   bool explicit_lod = ins->Instruction.Opcode == TGSI_OPCODE_SAMPLE_L;
   assert(ins->Src[1].Register.File == TGSI_FILE_SAMPLER_VIEW);
   assert(ins->Src[2].Register.File == TGSI_FILE_SAMPLER);
   assert(!ins->Src[1].Register.Indirect && !ins->Src[2].Register.Indirect);
   assert(texture < c->num_samp_types && sampler < PIPE_MAX_SAMPLERS);
   assert(ins->Texture.NumOffsets <= 1);
   nir_tex_instr *tex = nir_tex_instr_create(b->shader,
      1 + explicit_lod + ins->Texture.NumOffsets);
   tex->op = explicit_lod ? nir_texop_txl : nir_texop_tex;
   tex->can_speculate = true;
   get_texture_info(c->samp_targets[texture], &tex->sampler_dim,
                    &tex->is_shadow, &tex->is_array);
   tex->coord_components = glsl_get_sampler_dim_coordinate_components(tex->sampler_dim) + tex->is_array;
   tex->texture_index = texture;
   tex->sampler_index = sampler;
   tex->dest_type = c->samp_types[texture];
   unsigned n = 0;
   tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_coord,
      nir_trim_vector(b, src[0], tex->coord_components));
   if (explicit_lod)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_channel(b, src[3], 0));
   if (ins->Texture.NumOffsets) {
      struct tgsi_texture_offset *offset = &ins->TexOffsets[0];
      nir_src value = ttn_src_for_file_and_index(c, offset->File, offset->Index,
                                                NULL, NULL, NULL, true);
      unsigned count = tex->coord_components - tex->is_array;
      nir_def *components[3] = {
         nir_channel(b, value.ssa, offset->SwizzleX),
         nir_channel(b, value.ssa, offset->SwizzleY),
         nir_channel(b, value.ssa, offset->SwizzleZ)};
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_offset, nir_vec(b, components, count));
   }
   c->num_samplers = MAX2(c->num_samplers, texture + 1);
   BITSET_SET(b->shader->info.textures_used, texture);
   BITSET_SET(b->shader->info.samplers_used, sampler);
   nir_def_init(&tex->instr, &tex->def, nir_tex_instr_dest_size(tex), 32);
   nir_builder_instr_insert(b, &tex->instr);
   return nir_pad_vector_imm_int(b, &tex->def, 0, 4);
}

static nir_def *
ttn_tex('''),
        ('''      src[i] = ttn_get_src(c, &tgsi_inst->Src[i], i);''','''      if ((tgsi_op == TGSI_OPCODE_SAMPLE || tgsi_op == TGSI_OPCODE_SAMPLE_L) &&
          (i == 1 || i == 2))
         src[i] = NULL; /* Descriptor operands are not ALU values. */
      else
         src[i] = ttn_get_src(c, &tgsi_inst->Src[i], i);'''),
        ('''   case TGSI_OPCODE_TEX:
   case TGSI_OPCODE_TXP:''','''   case TGSI_OPCODE_SAMPLE:
   case TGSI_OPCODE_SAMPLE_L:
      dst = ttn_d3d_sample(c, src);
      break;

   case TGSI_OPCODE_TEX:
   case TGSI_OPCODE_TXP:'''),
        ('''            } else {
               assert(!decl->Declaration.Semantic);
               var->data.location = VERT_ATTRIB_GENERIC0 + idx;
            }''','''            } else if (c->scan->processor == MESA_SHADER_GEOMETRY) {
               var->data.location = tgsi_varying_semantic_to_slot(
                  decl->Semantic.Name, decl->Semantic.Index);
            } else {
               assert(!decl->Declaration.Semantic);
               var->data.location = VERT_ATTRIB_GENERIC0 + idx;
            }'''),
        ('''         var->type = glsl_vec4_type();
         if (is_array)
            var->type = glsl_array_type(var->type, array_size, 0);

         switch (file) {''','''         var->type = glsl_vec4_type();
         if (c->scan->processor == MESA_SHADER_GEOMETRY &&
             file == TGSI_FILE_INPUT)
            var->type = glsl_array_type(var->type,
                                        b->shader->info.gs.vertices_in, 0);
         else if (is_array)
            var->type = glsl_array_type(var->type, array_size, 0);

         switch (file) {'''),
        ('''      } else {
         /* Indirection on input arrays isn't supported by TTN. */
         assert(!dim);
         nir_deref_instr *deref = nir_build_deref_var(&c->build,
                                                      c->inputs[index]);
         return nir_src_for_ssa(nir_load_deref(&c->build, deref));
      }''','''      } else {
         nir_deref_instr *deref = nir_build_deref_var(&c->build,
                                                      c->inputs[index]);
         if (dim) {
            assert(c->scan->processor == MESA_SHADER_GEOMETRY &&
                   !dim->Indirect);
            deref = nir_build_deref_array_imm(&c->build, deref, dim->Index);
         }
         return nir_src_for_ssa(nir_load_deref(&c->build, deref));
      }'''),
        ('''   case TGSI_OPCODE_RET:
      /* NIR returns must be at the end of the block, while TGSI returns may not''','''   case TGSI_OPCODE_EMIT:
      nir_emit_vertex(b, 0);
      break;

   case TGSI_OPCODE_ENDPRIM:
      nir_end_primitive(b, 0);
      break;

   case TGSI_OPCODE_RET:
      /* NIR returns must be at the end of the block, while TGSI returns may not'''),
        ('''         if (parser.FullToken.FullInstruction.Instruction.Opcode == TGSI_OPCODE_RET) {
            /* We have to be conservative and add output stores before each return.
             * Hopefully stores will be optimized out later if not actually required */
            ttn_add_output_stores(c);
         }''','''         unsigned opcode = parser.FullToken.FullInstruction.Instruction.Opcode;
         if (opcode == TGSI_OPCODE_EMIT ||
             (opcode == TGSI_OPCODE_RET &&
              c->build.shader->info.stage != MESA_SHADER_GEOMETRY)) {
            /* GS output registers are committed at each EmitVertex. Other
             * stages retain the conservative stores before return. */
            ttn_add_output_stores(c);
         }'''),
        ('''   ttn_parse_tgsi(c, tgsi_tokens);
   ttn_add_output_stores(c);''','''   ttn_parse_tgsi(c, tgsi_tokens);
   if (s->info.stage != MESA_SHADER_GEOMETRY)
      ttn_add_output_stores(c);'''),
        ('''      case TGSI_PROPERTY_FS_COORD_ORIGIN:
         if (s->info.stage == MESA_SHADER_FRAGMENT)''','''      case TGSI_PROPERTY_GS_INPUT_PRIM:
         if (s->info.stage == MESA_SHADER_GEOMETRY) {
            s->info.gs.input_primitive = value;
            s->info.gs.vertices_in = mesa_vertices_per_prim(value);
         }
         break;
      case TGSI_PROPERTY_GS_OUTPUT_PRIM:
         if (s->info.stage == MESA_SHADER_GEOMETRY)
            s->info.gs.output_primitive = value;
         break;
      case TGSI_PROPERTY_GS_MAX_OUTPUT_VERTICES:
         if (s->info.stage == MESA_SHADER_GEOMETRY)
            s->info.gs.vertices_out = value;
         break;
      case TGSI_PROPERTY_GS_INVOCATIONS:
         if (s->info.stage == MESA_SHADER_GEOMETRY)
            s->info.gs.invocations = value;
         break;
      case TGSI_PROPERTY_FS_COORD_ORIGIN:
         if (s->info.stage == MESA_SHADER_FRAGMENT)''')])
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
      ((D3D11DDI_3DPIPELINESUPPORT_CAPS *)pData->pData)->Caps =
         D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(
            D3D11DDI_3DPIPELINELEVEL_10_0);
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
        ('''static inline void
SetError(D3D10DDI_HDEVICE hDevice, HRESULT hr)
{
   if (FAILED(hr)) {
      Device *pDevice = CastDevice(hDevice);
      pDevice->UMCallbacks.pfnSetErrorCb(pDevice->hRTCoreLayer, hr);
   }
}''','''static inline void
AgxSetErrorWithOrigin(D3D10DDI_HDEVICE hDevice, HRESULT hr, const char *origin)
{
   if (FAILED(hr)) {
      if (!AgxD3d10WindowsDiagnosticRefusal(hr))
         AgxD3d10WindowsDiagnostic(origin, hr, NULL, 0);
      Device *pDevice = CastDevice(hDevice);
      pDevice->UMCallbacks.pfnSetErrorCb(pDevice->hRTCoreLayer, hr);
   }
}
#define SetError(device, hr) AgxSetErrorWithOrigin((device), (hr), __func__)'''),
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
   UINT usage;
   UINT bind_flags;
   Device *owner_device;
   ULONGLONG owner_cookie;
   ULONG device_generation;
   DXGI_FORMAT sample_format;
   UINT sample_level;
   UINT sample_layer;
   AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *presentation;
};'''),
        ('''struct SamplerState
{
   void *handle;
};''','''struct SamplerState
{
   void *handle;
   Device *owner_device;
};'''),
        ('''struct ShaderResourceView
{
   struct pipe_sampler_view *handle;
};''','''struct ShaderResourceView
{
   struct pipe_sampler_view *handle;
   Device *owner_device;
   Resource *owner_resource;
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
   if (Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
       Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
       Format == DXGI_FORMAT_B8G8R8X8_UNORM ||
       Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB) {
      *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET |
                     D3D10_DDI_FORMAT_SUPPORT_BLENDABLE |
                     D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE;
   } else if (Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
       Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
       Format == DXGI_FORMAT_R8_UNORM || Format == DXGI_FORMAT_R16_FLOAT ||
       Format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
       Format == DXGI_FORMAT_R10G10B10A2_UNORM ||
       Format == DXGI_FORMAT_R11G11B10_FLOAT ||
       Format == DXGI_FORMAT_B5G6R5_UNORM || Format == DXGI_FORMAT_A8_UNORM) {
      *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET |
                     D3D10_DDI_FORMAT_SUPPORT_BLENDABLE;
   } else if (Format == DXGI_FORMAT_D32_FLOAT) {
      *pFormatCaps = 0; /* Depth support is base-assumed, not an optional DDI bit. */
   } else if (Format == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM) {
      *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED;
   } else {
      *pFormatCaps = 0;
   }'''),
        ('''   /* XXX: Disable MSAA */
   *pNumQualityLevels = 0;''','''   (void)hDevice;
   *pNumQualityLevels =
      (Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
       Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
       Format == DXGI_FORMAT_B8G8R8X8_UNORM ||
       Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB ||
       Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
       Format == DXGI_FORMAT_D32_FLOAT ||
       Format == DXGI_FORMAT_D16_UNORM ||
       Format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
       Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ||
       Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
       Format == DXGI_FORMAT_R8_UNORM ||
       Format == DXGI_FORMAT_R16_FLOAT ||
       Format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
       Format == DXGI_FORMAT_R10G10B10A2_UNORM ||
       Format == DXGI_FORMAT_R11G11B10_FLOAT ||
       Format == DXGI_FORMAT_B5G6R5_UNORM ||
       Format == DXGI_FORMAT_A8_UNORM ||
       Format == DXGI_FORMAT_R16_UINT) && SampleCount == 1 ? 1 : 0;''')])
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
        ('''   LOG_ENTRYPOINT();

   struct pipe_resource *resource = CastPipeResource(pCreateRenderTargetView->hDrvResource);''',
         '''   LOG_ENTRYPOINT();

   Device *windowsDevice=CastDevice(hDevice);
   Resource *windowsResource=CastResource(pCreateRenderTargetView->hDrvResource);
   if(!windowsDevice||!windowsResource||windowsResource->owner_device!=windowsDevice||
      (!windowsResource->presentation&&
       !(windowsResource->bind_flags&D3D10_DDI_BIND_RENDER_TARGET))||
      !AgxD3d10FormatViewCompatible(windowsResource->Format,
          pCreateRenderTargetView->Format,FALSE,
          windowsResource->presentation &&
          (windowsResource->bind_flags & D3D10_DDI_BIND_PRESENT))) {
      SetError(hDevice,E_NOTIMPL);return;
   }
   struct pipe_resource *resource = windowsResource->resource;'''),
        ('''   desc.format = FormatTranslate(pCreateRenderTargetView->Format, false);''',
         '''   desc.format = FormatTranslate(pCreateRenderTargetView->Format, false);
   enum pipe_format lowered=AgxD3d10LoweredTextureFormat(
      pCreateRenderTargetView->Format);
   if(lowered!=PIPE_FORMAT_NONE) desc.format=lowered;'''),
        ('''   pipe->clear_render_target(pipe,
                             surface,
                             &clear_color,
                             0, 0,
                             pipe_surface_width(surface),
                             pipe_surface_height(surface),
                             true);''','''   Device *traceDevice = CastDevice(hDevice);
   if (traceDevice) AgxD3d10WindowsDiagnosticState(traceDevice->windows, "clear-before");
   if (pipe->clear_render_target) {
      pipe->clear_render_target(pipe,
                                surface,
                                &clear_color,
                                0, 0,
                                pipe_surface_width(surface),
                                pipe_surface_height(surface),
                                true);
      if (traceDevice) AgxD3d10WindowsDiagnosticState(traceDevice->windows, "clear-after");
      return;
   }
   Device *pDevice = CastDevice(hDevice);
   struct pipe_resource *resource = surface ? surface->texture : NULL;
   struct pipe_surface *bound = pDevice && pDevice->fb.nr_cbufs == 1 ?
      &pDevice->fb.cbufs[0] : NULL;
   if (!pipe->clear || !surface || !resource ||
       resource->target != PIPE_TEXTURE_2D ||
       (resource->format != PIPE_FORMAT_B8G8R8A8_UNORM &&
        resource->format != PIPE_FORMAT_B8G8R8A8_SRGB &&
        resource->format != PIPE_FORMAT_B8G8R8X8_UNORM &&
        resource->format != PIPE_FORMAT_B8G8R8X8_SRGB &&
       resource->format != PIPE_FORMAT_R8G8B8A8_UNORM &&
        resource->format != PIPE_FORMAT_R16G16B16A16_FLOAT &&
        resource->format != PIPE_FORMAT_R8_UNORM &&
        resource->format != PIPE_FORMAT_R16_FLOAT &&
        resource->format != PIPE_FORMAT_R32G32B32A32_FLOAT &&
        resource->format != PIPE_FORMAT_R10G10B10A2_UNORM &&
        resource->format != PIPE_FORMAT_R11G11B10_FLOAT &&
        resource->format != PIPE_FORMAT_B5G6R5_UNORM) ||
       resource->nr_samples != 1 || resource->array_size != 1 ||
       resource->last_level != 0 ||
       util_format_linear(surface->format) != util_format_linear(resource->format) ||
       surface->level != 0 || surface->first_layer != 0 || surface->last_layer != 0 ||
       !bound || pDevice->fb.zsbuf.texture || bound->texture != resource ||
       bound->format != surface->format || bound->level != surface->level ||
       bound->first_layer != surface->first_layer || bound->last_layer != surface->last_layer ||
       pDevice->fb.width != pipe_surface_width(surface) ||
       pDevice->fb.height != pipe_surface_height(surface)) {
      LOG_UNSUPPORTED("ClearRenderTargetView requires one full admitted color target");
      SetError(hDevice, E_NOTIMPL);
      return;
   }
   pipe->clear(pipe, PIPE_CLEAR_COLOR0, 0xf, 0, NULL,
               &clear_color, 0.0, 0);
   if (traceDevice) AgxD3d10WindowsDiagnosticState(traceDevice->windows, "clear-after");'''),
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
                             true);''','''   Device *pDevice = CastDevice(hDevice);
   struct pipe_context *pipe = CastPipeContext(hDevice);
   struct pipe_surface *surface = CastPipeDepthStencilView(hDepthStencilView);
   struct pipe_resource *resource = surface ? surface->texture : NULL;
   struct pipe_surface *bound = pDevice && pDevice->fb.zsbuf.texture ?
      &pDevice->fb.zsbuf : NULL;
   bool packed_depth_stencil = resource &&
      (resource->format == PIPE_FORMAT_Z24_UNORM_S8_UINT ||
       resource->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT);
   unsigned clear_flags = packed_depth_stencil ?
      PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL : PIPE_CLEAR_DEPTH;
   if (!pipe || !pipe->clear || !surface || !resource || !bound ||
       Flags != (packed_depth_stencil ?
          D3D10_DDI_CLEAR_DEPTH | D3D10_DDI_CLEAR_STENCIL :
          D3D10_DDI_CLEAR_DEPTH) || (!packed_depth_stencil && Stencil != 0) ||
       Depth < 0.0f || Depth > 1.0f ||
       resource->target != PIPE_TEXTURE_2D ||
       (resource->format != PIPE_FORMAT_Z32_FLOAT &&
        resource->format != PIPE_FORMAT_Z16_UNORM &&
        resource->format != PIPE_FORMAT_Z24_UNORM_S8_UINT &&
        resource->format != PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) ||
       resource->nr_samples != 1 || resource->array_size != 1 ||
       resource->last_level != 0 ||
       util_format_linear(surface->format) != util_format_linear(resource->format) ||
       surface->level != 0 || surface->first_layer != 0 ||
       surface->last_layer != 0 || bound->texture != resource ||
       bound->format != surface->format || bound->level != surface->level ||
       bound->first_layer != surface->first_layer ||
       bound->last_layer != surface->last_layer ||
       pDevice->fb.width != pipe_surface_width(surface) ||
       pDevice->fb.height != pipe_surface_height(surface)) {
      LOG_UNSUPPORTED("ClearDepthStencilView requires one bound full admitted depth target");
      SetError(hDevice, E_NOTIMPL);
      return;
   }
   union pipe_color_union color;
   memset(&color, 0, sizeof(color));
   pipe->clear(pipe, clear_flags, 0, 0, NULL, &color, Depth, Stencil);''')])
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
    replace_function_body('src/gallium/frontends/d3d10umd/Query.cpp','SetPredication','''   Device *device = CastDevice(hDevice);
   if (!device || hQuery.pDrvPrivate || PredicateValue) {
      SetError(hDevice, E_NOTIMPL); return;
   }
   device->pPredicate = NULL;
   device->PredicateValue = FALSE;''')
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

   return S_OK;''','''   UINT presentValues[6] = {
      pPresentData ? pPresentData->Flags.Value : ~0u,
      pPresentData ? (UINT)pPresentData->FlipInterval : ~0u,
      pPresentData ? pPresentData->SrcSubResourceIndex : ~0u,
      pPresentData && pPresentData->hDstResource != 0,
      pPresentData && pPresentData->hSurfaceToPresent != 0,
      pPresentData && pPresentData->pDXGIContext != NULL};
   AgxD3d10WindowsDiagnostic("native-present-entry",
      pPresentData ? S_OK : E_INVALIDARG, presentValues, 6u);
   if (!pPresentData || pPresentData->hDstResource != 0 ||
       pPresentData->SrcSubResourceIndex != 0 ||
       (pPresentData->Flags.Value != 0x1u && pPresentData->Flags.Value != 0x2u) ||
       (pPresentData->FlipInterval != DXGI_DDI_FLIP_INTERVAL_IMMEDIATE &&
        pPresentData->FlipInterval != DXGI_DDI_FLIP_INTERVAL_ONE))
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
   if (!GetCaps || !GetCaps->pGammaCapabilities ||
       !CastDevice(GetCaps->hDevice))
      return E_INVALIDARG;
   ZeroMemory(GetCaps->pGammaCapabilities,
              sizeof(*GetCaps->pGammaCapabilities));
   return S_OK;'''),
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
   if (!SetResourcePriority) return E_INVALIDARG;
   Device *device = CastDevice(SetResourcePriority->hDevice);
   Resource *resource = CastResource(SetResourcePriority->hResource);
   if (!device || !resource || resource->owner_device != device)
      return E_INVALIDARG;
   return AgxD3d10WindowsSetResourcePriority(device->windows,
      resource->presentation, resource->resource,
      SetResourcePriority->Priority);'''),
        ('''_QueryResourceResidency( DXGI_DDI_ARG_QUERYRESOURCERESIDENCY *QueryResourceResidency )
{
   LOG_ENTRYPOINT();''','''_QueryResourceResidency( DXGI_DDI_ARG_QUERYRESOURCERESIDENCY *QueryResourceResidency )
{
   if (!QueryResourceResidency || !QueryResourceResidency->Resources ||
       !QueryResourceResidency->pResources || !QueryResourceResidency->pStatus ||
       QueryResourceResidency->Resources > ((SIZE_T)-1) /
          sizeof(DXGI_DDI_RESIDENCY))
      return E_INVALIDARG;
   Device *device = CastDevice(QueryResourceResidency->hDevice);
   if (!device) return E_INVALIDARG;
   DXGI_DDI_RESIDENCY *statuses = (DXGI_DDI_RESIDENCY *)HeapAlloc(
      GetProcessHeap(), HEAP_ZERO_MEMORY,
      QueryResourceResidency->Resources * sizeof(*statuses));
   if (!statuses) return E_OUTOFMEMORY;
   HRESULT result = S_OK;
   for (SIZE_T i = 0; i < QueryResourceResidency->Resources; ++i) {
      Resource *resource = CastResource(QueryResourceResidency->pResources[i]);
      if (!resource || resource->owner_device != device) {
         result = E_INVALIDARG;
         break;
      }
   }
   for (SIZE_T i = 0; SUCCEEDED(result) &&
        i < QueryResourceResidency->Resources; ++i) {
      Resource *resource = CastResource(QueryResourceResidency->pResources[i]);
      HRESULT one = AgxD3d10WindowsQueryResourceResidency(device->windows,
         resource->presentation, resource->resource, &statuses[i]);
      if (FAILED(one)) { result = one; break; }
      if (one == AGX_DXGI_STATUS_NOT_RESIDENT)
         result = AGX_DXGI_STATUS_NOT_RESIDENT;
      else if (one == AGX_DXGI_STATUS_RESIDENT_IN_SHARED_MEMORY &&
               result == S_OK)
         result = AGX_DXGI_STATUS_RESIDENT_IN_SHARED_MEMORY;
   }
   if (SUCCEEDED(result)) memcpy(QueryResourceResidency->pStatus, statuses,
      QueryResourceResidency->Resources * sizeof(*statuses));
   HeapFree(GetProcessHeap(), 0, statuses);
   return result;'''),
        ('''_RotateResourceIdentities( DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES *RotateResourceIdentities )
{
   LOG_ENTRYPOINT();''','''_RotateResourceIdentities( DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES *RotateResourceIdentities )
{
   if (!RotateResourceIdentities || RotateResourceIdentities->Resources < 2 ||
       !RotateResourceIdentities->pResources)
      return E_INVALIDARG;
   Device *device = CastDevice(RotateResourceIdentities->hDevice);
   if (!device) return E_INVALIDARG;
   AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE **records =
      (AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE **)HeapAlloc(
         GetProcessHeap(), HEAP_ZERO_MEMORY,
         sizeof(*records) * RotateResourceIdentities->Resources);
   if (!records) return E_OUTOFMEMORY;
   HRESULT result = S_OK;
   for (UINT i = 0; i < RotateResourceIdentities->Resources; ++i) {
      Resource *resource = CastResource(RotateResourceIdentities->pResources[i]);
      if (!resource || resource->owner_device != device ||
          !resource->presentation) {
         result = E_INVALIDARG;
         break;
      }
      records[i] = resource->presentation;
   }
   if (SUCCEEDED(result)) result = AgxD3d10WindowsPresentationRotate(
      device->windows, records, RotateResourceIdentities->Resources);
   HeapFree(GetProcessHeap(), 0, records);
   return result;'''),
        ('''_Blt(DXGI_DDI_ARG_BLT *Blt)
{
   LOG_UNSUPPORTED_ENTRYPOINT();''','''_Blt(DXGI_DDI_ARG_BLT *Blt)
{
   if (!Blt || Blt->DstSubresource || Blt->SrcSubresource ||
       Blt->DstLeft || Blt->DstTop || Blt->DstRight != 2560 ||
       Blt->DstBottom != 1600 || (Blt->Flags.Value & ~0xfu))
      return E_INVALIDARG;
   Device *device = CastDevice(Blt->hDevice);
   Resource *destination = CastResource(Blt->hDstResource);
   Resource *source = CastResource(Blt->hSrcResource);
   if (!device || !destination || !source || destination == source ||
       destination->owner_device != device || source->owner_device != device ||
       !destination->presentation || !source->presentation)
      return E_INVALIDARG;
   if (Blt->Flags.Value != 0x8u ||
       Blt->Rotate != DXGI_DDI_MODE_ROTATION_IDENTITY)
      return E_NOTIMPL;
   return AgxD3d10WindowsPresentationBlt(device->windows,
      destination->presentation, source->presentation);''')])
    # Trace the existing DXGI boundary as one unit. Return values and argument
    # guards stay unchanged; the process-local diagnostic preserves last-error.
    dxgi_path = 'src/gallium/frontends/d3d10umd/DxgiFns.cpp'
    dxgi_target = out / dxgi_path
    dxgi_text = dxgi_target.read_text()
    dxgi_marker = 'static HRESULT\nUnsupportedDxgi'
    if dxgi_text.count(dxgi_marker) != 1:
        raise SystemExit('Ambiguous DXGI trace helper anchor')
    dxgi_text = dxgi_text.replace(dxgi_marker, '''static HRESULT
AgxDxgiTraceReturn(const char *name, HRESULT result)
{
   AgxD3d10WindowsDiagnostic(name, result, NULL, 0);
   return result;
}

static HRESULT
UnsupportedDxgi''')
    dxgi_target.write_text(dxgi_text)
    for name in ('_Present', '_GetGammaCaps', '_SetDisplayMode',
                 '_SetResourcePriority', '_QueryResourceResidency',
                 '_RotateResourceIdentities', '_Blt'):
        text = dxgi_target.read_text()
        marker = '\n' + name + '('
        if text.count(marker) != 1:
            raise SystemExit('Ambiguous DXGI trace function: ' + name)
        start = text.index('{', text.index(marker)) + 1
        end, depth = start, 1
        while depth and end < len(text):
            if text[end] == '{': depth += 1
            elif text[end] == '}': depth -= 1
            end += 1
        if depth:
            raise SystemExit('Unclosed DXGI trace function: ' + name)
        body = text[start:end - 1]
        body, returns = re.subn(r'\breturn\s+([^;]+);',
            r'return AgxDxgiTraceReturn(__func__, (\1));', body)
        if not returns:
            raise SystemExit('Missing DXGI trace return: ' + name)
        entry = '   AgxD3d10WindowsDiagnostic("' + name + '-entry", S_OK, NULL, 0);\n'
        if name == '_Blt':
            entry += '''   UINT values[11] = {
      Blt ? Blt->DstSubresource : ~0u, Blt ? Blt->SrcSubresource : ~0u,
      Blt ? Blt->DstLeft : ~0u, Blt ? Blt->DstTop : ~0u,
      Blt ? Blt->DstRight : ~0u, Blt ? Blt->DstBottom : ~0u,
      Blt ? Blt->Flags.Value : ~0u, Blt ? (UINT)Blt->Rotate : ~0u,
      Blt && Blt->hDstResource != 0, Blt && Blt->hSrcResource != 0,
      Blt && Blt->hDevice != 0};
   AgxD3d10WindowsDiagnostic("dxgi-blt-args", S_OK, values, 11u);
'''
        elif name == '_RotateResourceIdentities':
            entry += '''   UINT count = RotateResourceIdentities ? RotateResourceIdentities->Resources : ~0u;
   AgxD3d10WindowsDiagnostic("dxgi-rotate-count", S_OK, &count, 1u);
'''
        elif name == '_QueryResourceResidency':
            entry += '''   UINT count = QueryResourceResidency ? QueryResourceResidency->Resources : ~0u;
   AgxD3d10WindowsDiagnostic("dxgi-residency-count", S_OK, &count, 1u);
'''
        if name == '_Blt':
            body = body.replace('AgxDxgiTraceReturn(__func__, (', 'rejectBlt(')
            body = body.replace('));', ');')
            entry = entry[entry.index('   UINT values[11]'):]
            entry = entry.replace('   AgxD3d10WindowsDiagnostic("dxgi-blt-args", S_OK, values, 11u);',
                '   auto rejectBlt = [&](HRESULT status) { if (FAILED(status)) AgxD3d10WindowsDiagnostic("reject-BltDXGI",status,values,11u); return status; };')
        replace_function_body(dxgi_path, name, entry + body)

    change('src/gallium/frontends/d3d10umd/Shader.cpp',
        '48a7de2a42b25abac677cd903c10f21fc91b86aef167266a32a6d7d9da21097c',[
        ('''{
   unsigned i;

   LOG_ENTRYPOINT();

   Device *pDevice = CastDevice(hDevice);
   struct pipe_context *pipe = pDevice->pipe;

   assert(SOTargets + ClearTargets <= PIPE_MAX_SO_BUFFERS);''','''{
   unsigned i;
   Device *windows_device=CastDevice(hDevice);
   if(!windows_device) { SetError(hDevice,E_INVALIDARG); return; }
   Resource *windows_resource=(SOTargets==1u&&ClearTargets==0u&&phResource)?
      CastResource(phResource[0]):NULL;
   if(SOTargets==1u&&ClearTargets==0u) {
      if(!windows_resource||!pOffsets||pOffsets[0]!=0u||
         windows_resource->owner_device!=windows_device||
         windows_resource->bind_flags!=
           (D3D10_DDI_BIND_VERTEX_BUFFER|D3D10_DDI_BIND_STREAM_OUTPUT)||
         !windows_resource->resource) { SetError(hDevice,E_NOTIMPL); return; }
   } else if(!((SOTargets==0u&&ClearTargets==0u)||
               (SOTargets==0u&&ClearTargets==1u))) {
      SetError(hDevice,E_NOTIMPL);return;
   }

   Device *pDevice = windows_device;
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
   Device *windows_device=CastDevice(hDevice);
   if(!windows_device||!pData||!pData->pShaderCode||
      !pData->pOutputStreamDecl||pData->NumEntries!=1u||
      pData->StreamOutputStrideInBytes!=16u||
      pData->pOutputStreamDecl[0].OutputSlot!=0u||
      pData->pOutputStreamDecl[0].RegisterIndex!=0u||
      pData->pOutputStreamDecl[0].RegisterMask!=0xfu) {
      SetError(hDevice,E_NOTIMPL);return;
   }
   LOG_ENTRYPOINT();'''),
        ('''CreateShaderResourceView(
   D3D10DDI_HDEVICE hDevice,                                                     // IN
   __in const D3D10DDIARG_CREATESHADERRESOURCEVIEW *pCreateSRView,   // IN
   D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView,                             // IN
   D3D10DDI_HRTSHADERRESOURCEVIEW hRTShaderResourceView)                         // IN
{
   LOG_ENTRYPOINT();

   struct pipe_context *pipe = CastPipeContext(hDevice);
   ShaderResourceView *pSRView = CastShaderResourceView(hShaderResourceView);
   struct pipe_resource *resource;
   enum pipe_format format;

   struct pipe_sampler_view desc;
   memset(&desc, 0, sizeof desc);
   resource = CastPipeResource(pCreateSRView->hDrvResource);
   format = FormatTranslate(pCreateSRView->Format, false);

   u_sampler_view_default_template(&desc,
                                   resource,
                                   format);''',
         '''CreateShaderResourceView(
   D3D10DDI_HDEVICE hDevice,                                                     // IN
   __in const D3D10DDIARG_CREATESHADERRESOURCEVIEW *pCreateSRView,   // IN
   D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView,                             // IN
   D3D10DDI_HRTSHADERRESOURCEVIEW hRTShaderResourceView)                         // IN
{
   LOG_ENTRYPOINT();

   Device *windowsDevice=CastDevice(hDevice);
   Resource *windowsResource=CastResource(pCreateSRView->hDrvResource);
   if(!windowsDevice||!windowsResource||windowsResource->owner_device!=windowsDevice||
      (!windowsResource->presentation&&
       !(windowsResource->bind_flags&D3D10_DDI_BIND_SHADER_RESOURCE))||
      !AgxD3d10FormatViewCompatible(windowsResource->Format,
          pCreateSRView->Format,FALSE,
          windowsResource->presentation &&
          (windowsResource->bind_flags & D3D10_DDI_BIND_PRESENT))||
      pCreateSRView->ResourceDimension!=D3D10DDIRESOURCE_TEXTURE2D||
      pCreateSRView->Tex2D.MipLevels!=1u||pCreateSRView->Tex2D.ArraySize!=1u||
      pCreateSRView->Tex2D.MostDetailedMip>=windowsResource->MipLevels||
      pCreateSRView->Tex2D.FirstArraySlice>=
         windowsResource->NumSubResources/windowsResource->MipLevels) {
      SetError(hDevice,E_NOTIMPL);return;
   }
   struct pipe_context *pipe = CastPipeContext(hDevice);
   ShaderResourceView *pSRView = CastShaderResourceView(hShaderResourceView);
   struct pipe_resource *resource;
   enum pipe_format format;

   struct pipe_sampler_view desc;
   memset(&desc, 0, sizeof desc);
   resource = CastPipeResource(pCreateSRView->hDrvResource);
   format = FormatTranslate(pCreateSRView->Format, false);
   enum pipe_format lowered=AgxD3d10LoweredTextureFormat(pCreateSRView->Format);
   if(lowered!=PIPE_FORMAT_NONE) format=lowered;
   u_sampler_view_default_template(&desc, resource, format);
   if(pCreateSRView->Format==DXGI_FORMAT_A8_UNORM) {
      desc.swizzle_r=PIPE_SWIZZLE_0;desc.swizzle_g=PIPE_SWIZZLE_0;
      desc.swizzle_b=PIPE_SWIZZLE_0;desc.swizzle_a=PIPE_SWIZZLE_W;
   }'''),
        ('''   pSamplerState->handle = pipe->create_sampler_state(pipe, &state);''',
         '''   pSamplerState->handle = pipe->create_sampler_state(pipe, &state);
   pSamplerState->owner_device = CastDevice(hDevice);'''),
        ('''   pSRView->handle = pipe->create_sampler_view(pipe, resource, &desc);
}


/*
 * ----------------------------------------------------------------------
 *
 * CreateShaderResourceView1 --''',
         '''   pSRView->handle = pipe->create_sampler_view(pipe, resource, &desc);
   pSRView->owner_device = CastDevice(hDevice);
   pSRView->owner_resource = CastResource(pCreateSRView->hDrvResource);
   pSRView->owner_resource->sample_format = pCreateSRView->Format;
   pSRView->owner_resource->sample_level = pCreateSRView->Tex2D.MostDetailedMip;
   pSRView->owner_resource->sample_layer = pCreateSRView->Tex2D.FirstArraySlice;
}


/*
 * ----------------------------------------------------------------------
 *
 * CreateShaderResourceView1 --''')])
    replace_function_body('src/gallium/frontends/d3d10umd/Shader.cpp','SetSamplers','''   Device *pDevice = CastDevice(hDevice);
   const UINT slots = D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT;
   static_assert(PIPE_MAX_SAMPLERS >= D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT,
                 "D3D10 sampler state must fit the native array");
   bool valid = pDevice && pDevice->pipe &&
      (shader_type == MESA_SHADER_VERTEX || shader_type == MESA_SHADER_FRAGMENT ||
       shader_type == MESA_SHADER_GEOMETRY) && Offset <= slots &&
      NumSamplers <= slots - Offset && (NumSamplers == 0 || phSamplers);
   void *states[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
   UINT nonnull = 0;
   for (UINT i = 0; valid && i < NumSamplers; ++i) {
      SamplerState *sampler = CastSamplerState(phSamplers[i]);
      if (sampler) {
         nonnull |= 1u << (Offset + i);
         valid = sampler->owner_device == pDevice && sampler->handle;
         if (valid) states[i] = sampler->handle;
      }
   }
   const UINT receipt[] = {(UINT)shader_type, Offset, NumSamplers, nonnull};
   AgxD3d10WindowsDiagnostic("sampler-range", valid ? S_OK : E_NOTIMPL,
                             receipt, 4);
   if (!valid) { SetError(hDevice, E_NOTIMPL); return; }
   if (NumSamplers == 0) return;
   for (UINT i = 0; i < NumSamplers; ++i)
      pDevice->samplers[shader_type][Offset + i] = states[i];
   pDevice->pipe->bind_sampler_states(pDevice->pipe, shader_type, Offset,
                                     NumSamplers, states);''')
    replace_function_body('src/gallium/frontends/d3d10umd/Shader.cpp','SetShaderResources','''   Device *pDevice = CastDevice(hDevice);
   bool valid = pDevice &&
      (shader_type == MESA_SHADER_VERTEX || shader_type == MESA_SHADER_FRAGMENT ||
       shader_type == MESA_SHADER_GEOMETRY) && Offset == 0 &&
      NumViews <= 1 && (NumViews == 0 || phShaderResourceViews);
   for (UINT i = 0; valid && i < NumViews; ++i) {
      ShaderResourceView *view = CastShaderResourceView(phShaderResourceViews[i]);
      valid = !view || (view->owner_device == pDevice && view->owner_resource &&
         view->owner_resource->owner_device == pDevice && view->handle);
   }
   if (!valid) { SetError(hDevice, E_NOTIMPL); return; }
   struct pipe_sampler_view *view = NumViews ?
      CastPipeShaderResourceView(phShaderResourceViews[0]) : NULL;
   pDevice->sampler_views[shader_type][0] = view;
   pDevice->pipe->set_sampler_views(pDevice->pipe, shader_type, 0, 1, 0, &view);''')
    replace_function_body('src/gallium/frontends/d3d10umd/Shader.cpp','DestroySampler','''   Device *pDevice = CastDevice(hDevice);
   SamplerState *sampler = CastSamplerState(hSampler);
   if (!pDevice || !sampler || sampler->owner_device != pDevice || !sampler->handle) {
      SetError(hDevice, E_INVALIDARG); return;
   }
   pDevice->pipe->delete_sampler_state(pDevice->pipe, sampler->handle);
   sampler->handle = NULL; sampler->owner_device = NULL;''')
    replace_function_body('src/gallium/frontends/d3d10umd/Shader.cpp','DestroyShaderResourceView','''   Device *pDevice = CastDevice(hDevice);
   ShaderResourceView *view = CastShaderResourceView(hShaderResourceView);
   if (!pDevice || !view || view->owner_device != pDevice || !view->handle) {
      SetError(hDevice, E_INVALIDARG); return;
   }
   pDevice->pipe->sampler_view_release(pDevice->pipe, view->handle);
   if (view->owner_resource) {
      view->owner_resource->sample_format = DXGI_FORMAT_UNKNOWN;
      view->owner_resource->sample_level = view->owner_resource->sample_layer = 0;
   }
   view->handle = NULL; view->owner_device = NULL; view->owner_resource = NULL;''')
    replace_function_body('src/gallium/frontends/d3d10umd/Shader.cpp','GenMips','''   Device *device = CastDevice(hDevice);
   ShaderResourceView *view = CastShaderResourceView(hShaderResourceView);
   if (!device || !view || view->owner_device != device || !view->handle ||
       !view->owner_resource || view->owner_resource->owner_device != device ||
       view->owner_resource->MipLevels != 1) {
      SetError(hDevice, E_NOTIMPL); return;
   }''')
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
               shader_type == MESA_SHADER_FRAGMENT ||
               shader_type == MESA_SHADER_GEOMETRY) && slot == 0 &&
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
    change('src/gallium/frontends/d3d10umd/Format.h',
        'e6118737e8a3f3f92e2d30043a648e7b79b31d969aa835d85de3f2b71c7db4dd',[
        ('const char *\nFormatToName(DXGI_FORMAT Format);',
         '''const char *
FormatToName(DXGI_FORMAT Format);

#ifdef __cplusplus
extern "C"
#endif
BOOL AgxD3d10FormatViewCompatible(
   DXGI_FORMAT ResourceFormat, DXGI_FORMAT ViewFormat, BOOL Depth, BOOL BackBuffer);

#ifdef __cplusplus
extern "C"
#endif
enum pipe_format AgxD3d10LoweredTextureFormat(DXGI_FORMAT Format);''')])
    change('src/gallium/frontends/d3d10umd/Format.cpp',
        '26215278ae7e566dc5973fb932daaa9142b7b11bd5dcfff8c574f37991e8fdf0',[
        ('''   case DXGI_FORMAT_B5G6R5_UNORM:
      return PIPE_FORMAT_B5G6R5_UNORM;''',
         '''   case DXGI_FORMAT_B4G4R4A4_UNORM:
      return PIPE_FORMAT_B4G4R4A4_UNORM;
   case DXGI_FORMAT_B5G6R5_UNORM:
      return PIPE_FORMAT_B5G6R5_UNORM;'''),
        ('#include "Format.h"','''#include "Format.h"

extern "C" BOOL AgxD3d10FormatViewCompatible(
   DXGI_FORMAT resource, DXGI_FORMAT view, BOOL depth, BOOL backbuffer)
{
   if (backbuffer && !depth) {
      if (resource == DXGI_FORMAT_B8G8R8A8_UNORM ||
          resource == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
         resource = DXGI_FORMAT_B8G8R8A8_TYPELESS;
      else if (resource == DXGI_FORMAT_R8G8B8A8_UNORM ||
               resource == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
         resource = DXGI_FORMAT_R8G8B8A8_TYPELESS;
   }
   if (resource == view) return FormatTranslate(view, depth) != PIPE_FORMAT_NONE;
   if (resource == DXGI_FORMAT_B8G8R8A8_TYPELESS)
      return !depth && (view == DXGI_FORMAT_B8G8R8A8_UNORM ||
                        view == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
   if (resource == DXGI_FORMAT_B8G8R8X8_TYPELESS)
      return !depth && (view == DXGI_FORMAT_B8G8R8X8_UNORM ||
                        view == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB);
   if (resource == DXGI_FORMAT_R8G8B8A8_TYPELESS)
      return view == DXGI_FORMAT_R8G8B8A8_UNORM ||
             view == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
             view == DXGI_FORMAT_R8G8B8A8_UINT ||
             view == DXGI_FORMAT_R8G8B8A8_SNORM ||
             view == DXGI_FORMAT_R8G8B8A8_SINT;
   if (resource == DXGI_FORMAT_BC1_TYPELESS)
      return view == DXGI_FORMAT_BC1_UNORM ||
             view == DXGI_FORMAT_BC1_UNORM_SRGB;
   if (resource == DXGI_FORMAT_BC2_TYPELESS)
      return view == DXGI_FORMAT_BC2_UNORM ||
             view == DXGI_FORMAT_BC2_UNORM_SRGB;
   if (resource == DXGI_FORMAT_BC3_TYPELESS)
      return view == DXGI_FORMAT_BC3_UNORM ||
             view == DXGI_FORMAT_BC3_UNORM_SRGB;
   if (resource == DXGI_FORMAT_BC4_TYPELESS)
      return view == DXGI_FORMAT_BC4_UNORM || view == DXGI_FORMAT_BC4_SNORM;
   if (resource == DXGI_FORMAT_BC5_TYPELESS)
      return view == DXGI_FORMAT_BC5_UNORM || view == DXGI_FORMAT_BC5_SNORM;
   return FALSE;
}

extern "C" enum pipe_format
AgxD3d10LoweredTextureFormat(DXGI_FORMAT format)
{
   switch(format) {
   case DXGI_FORMAT_R8G8_B8G8_UNORM:
   case DXGI_FORMAT_G8R8_G8B8_UNORM:
   case DXGI_FORMAT_A8_UNORM:
      return PIPE_FORMAT_R8G8B8A8_UNORM;
   case DXGI_FORMAT_R32G32B32_FLOAT:
      return PIPE_FORMAT_R32G32B32A32_FLOAT;
   case DXGI_FORMAT_R32G32B32_UINT:
      return PIPE_FORMAT_R32G32B32A32_UINT;
   case DXGI_FORMAT_R32G32B32_SINT:
      return PIPE_FORMAT_R32G32B32A32_SINT;
   case DXGI_FORMAT_R32G32B32_TYPELESS:
      return PIPE_FORMAT_R32G32B32A32_UNORM;
   default:
      return PIPE_FORMAT_NONE;
   }
}

extern "C" BOOL APIENTRY
MesaD3d10FrontendFormatMappedForTest(DXGI_FORMAT format)
{
   return FormatTranslate(format, false) != PIPE_FORMAT_NONE ? TRUE : FALSE;
}''')])
    change('src/gallium/frontends/d3d10umd/Draw.cpp',
        'da5904f2ac6b8a79373bcc60d2cef0546e8da0bc1ba92d21812aff3ea2338f7e',[
        ('#include "State.h"',
         '#include "State.h"\n#include "agx_win32_asahi_scene.h"'),
        ('''DrawAuto(D3D10DDI_HDEVICE hDevice)  // IN
{
   LOG_ENTRYPOINT();''','''DrawAuto(D3D10DDI_HDEVICE hDevice)  // IN
{
   Device *windows_device=CastDevice(hDevice);
   if(!windows_device||!windows_device->draw_so_target) {
      SetError(hDevice,E_NOTIMPL);return;
   }
   LOG_ENTRYPOINT();''')])
    replace_function_body('src/gallium/frontends/d3d10umd/Draw.cpp','Draw','''   LOG_ENTRYPOINT();
   Device *pDevice = CastDevice(hDevice);
   AgxD3d10WindowsDiagnosticState(pDevice->windows, "resolve-before");
   ResolveState(pDevice);
   assert(pDevice->primitive < MESA_PRIM_COUNT);
   const UINT args[] = {VertexCount, StartVertexLocation, static_cast<UINT>(pDevice->primitive)};
   AgxD3d10WindowsDiagnostic("draw-args", S_OK, args, 3);
   AgxD3d10WindowsDiagnosticState(pDevice->windows, "draw-before");
   util_draw_arrays(pDevice->pipe, pDevice->primitive, StartVertexLocation, VertexCount);
   AgxD3d10WindowsDiagnosticState(pDevice->windows, "draw-after");''')
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
   if (IndexCountPerInstance != 3 || InstanceCount != 1 ||
       StartIndexLocation != 0 || BaseVertexLocation != 0 ||
       StartInstanceLocation != 0) {
      SetError(hDevice, E_NOTIMPL); return;
   }
   DrawIndexed(hDevice,3,0,0);''')
    replace_function_body('src/gallium/frontends/d3d10umd/Draw.cpp','DrawInstanced','''   if (VertexCountPerInstance != 3 || InstanceCount != 1 ||
       StartVertexLocation != 0 || StartInstanceLocation != 0) {
      SetError(hDevice, E_NOTIMPL); return;
   }
   Draw(hDevice,3,0);''')
    draw_path=out/'src/gallium/frontends/d3d10umd/Draw.cpp'
    draw_text=draw_path.read_text()+'''\nextern "C" BOOL APIENTRY
MesaD3d10FrontendSetSoOffsetForTest(D3D10DDI_HDEVICE hDevice,
                                    D3D10DDI_HRESOURCE hResource,
                                    UINT value)
{
   Device *device=CastDevice(hDevice);
   Resource *resource=CastResource(hResource);
   return device&&resource&&resource->owner_device==device&&resource->so_target&&
      AgxWin32AsahiSetStreamOutputTargetOffsetForTest(
         resource->so_target,value) ? TRUE : FALSE;
}
'''
    draw_path.write_text(draw_text)
    overlays['src/gallium/frontends/d3d10umd/Draw.cpp']['after']=hashlib.sha256(
        draw_path.read_bytes()).hexdigest()
    change('src/gallium/frontends/d3d10umd/Resource.cpp',
        'ae2d60a798ff0d9da6e55171013f133d1d99bc91ef2760875d126aa5b96fcf48',[
        ('''   } else {
      BOOL bindDepthStencil = !!(pCreateResource->BindFlags & D3D10_DDI_BIND_DEPTH_STENCIL);
      templat.format = FormatTranslate(pCreateResource->Format, bindDepthStencil);
   }''',
         '''   } else {
      BOOL bindDepthStencil = !!(pCreateResource->BindFlags & D3D10_DDI_BIND_DEPTH_STENCIL);
      templat.format = FormatTranslate(pCreateResource->Format, bindDepthStencil);
   }
   enum pipe_format upload_format = templat.format;
   enum pipe_format lowered_format=AgxD3d10LoweredTextureFormat(
      pCreateResource->Format);
   if(lowered_format!=PIPE_FORMAT_NONE) templat.format=lowered_format;'''),
        ('''                  util_copy_rect(dst,
                                 templat.format,
                                 transfer->stride,
                                 0, 0, box.width, box.height,
                                 src,
                                 pInitialDataUP->SysMemPitch,
                                 0, 0);''',
         '''                  if (upload_format == templat.format) {
                     util_copy_rect(dst, templat.format, transfer->stride,
                                    0, 0, box.width, box.height, src,
                                    pInitialDataUP->SysMemPitch, 0, 0);
                  } else if (!AgxD3d10ExpandPackedPair(
                                upload_format,dst,transfer->stride,src,
                                pInitialDataUP->SysMemPitch,box.width,box.height) &&
                             !util_format_translate(
                                templat.format, dst, transfer->stride, 0, 0,
                                upload_format, src, pInitialDataUP->SysMemPitch,
                                0, 0, box.width, box.height)) {
                     SetError(hDevice, E_NOTIMPL);
                  }'''),
        ('#include "util/u_surface.h"',
         '''#include "util/u_surface.h"
#include "drm-uapi/drm_fourcc.h"
#include "agx_win32_asahi_scene.h"

static unsigned
AgxD3d10CopyFamily(DXGI_FORMAT format)
{
   switch (format) {
   case DXGI_FORMAT_B8G8R8A8_TYPELESS:
   case DXGI_FORMAT_B8G8R8A8_UNORM:
   case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return 1;
   case DXGI_FORMAT_B8G8R8X8_TYPELESS:
   case DXGI_FORMAT_B8G8R8X8_UNORM:
   case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return 2;
   case DXGI_FORMAT_R8G8B8A8_TYPELESS:
   case DXGI_FORMAT_R8G8B8A8_UNORM:
   case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
   case DXGI_FORMAT_R8G8B8A8_UINT:
   case DXGI_FORMAT_R8G8B8A8_SNORM:
   case DXGI_FORMAT_R8G8B8A8_SINT: return 3;
   default: return 0;
   }
}

static unsigned
AgxD3d10ColorBytes(DXGI_FORMAT format)
{
   switch (format) {
   case DXGI_FORMAT_R8_UNORM: return 1;
   case DXGI_FORMAT_R16_FLOAT:
   case DXGI_FORMAT_B5G6R5_UNORM: return 2;
   case DXGI_FORMAT_B8G8R8A8_TYPELESS:
   case DXGI_FORMAT_B8G8R8X8_TYPELESS:
   case DXGI_FORMAT_B8G8R8A8_UNORM:
   case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
   case DXGI_FORMAT_B8G8R8X8_UNORM:
   case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
   case DXGI_FORMAT_R8G8B8A8_UNORM:
   case DXGI_FORMAT_A8_UNORM:
   case DXGI_FORMAT_R10G10B10A2_UNORM:
   case DXGI_FORMAT_R11G11B10_FLOAT: return 4;
   case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
   case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
   default: return 0;
   }
}

static bool
AgxD3d10ExpandPackedPair(enum pipe_format format, void *destination,
                         unsigned destinationStride, const void *source,
                         unsigned sourceStride, unsigned width,
                         unsigned height)
{
   if ((format != PIPE_FORMAT_R8G8_B8G8_UNORM &&
        format != PIPE_FORMAT_G8R8_G8B8_UNORM) || (width & 1u))
      return false;
   for (unsigned y = 0; y < height; ++y) {
      const uint8_t *src = (const uint8_t *)source + y * sourceStride;
      uint8_t *dst = (uint8_t *)destination + y * destinationStride;
      for (unsigned x = 0; x < width; x += 2, src += 4, dst += 8) {
         uint8_t r, g0, b, g1;
         if (format == PIPE_FORMAT_R8G8_B8G8_UNORM) {
            r=src[0];g0=src[1];b=src[2];g1=src[3];
         } else {
            g0=src[0];r=src[1];g1=src[2];b=src[3];
         }
         dst[0]=r;dst[1]=g0;dst[2]=b;dst[3]=255;
         dst[4]=r;dst[5]=g1;dst[6]=b;dst[7]=255;
      }
   }
   return true;
}

static bool
AgxD3d10ResourceWithinRequiredLimits(
   const D3D10DDIARG_CREATERESOURCE *resource)
{
   if (!resource || !resource->pMipInfoList || !resource->MipLevels ||
       resource->MipLevels > D3D10_REQ_MIP_LEVELS || !resource->ArraySize)
      return false;

   const D3D10DDI_MIPINFO *mip = resource->pMipInfoList;
   uint64_t width = mip[0].TexelWidth;
   uint64_t height = mip[0].TexelHeight;
   uint64_t depth = mip[0].TexelDepth;
   uint64_t array = resource->ArraySize;
   if (!width || !height || !depth)
      return false;

   switch (resource->ResourceDimension) {
   case D3D10DDIRESOURCE_BUFFER:
      return resource->MipLevels == 1 && array == 1 && height == 1 && depth == 1 &&
         width <= (1ULL << D3D10_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP) &&
         width <= (uint64_t)D3D10_REQ_RESOURCE_SIZE_IN_MEGABYTES * 1024u * 1024u;
   case D3D10DDIRESOURCE_TEXTURE1D:
      if (width > D3D10_REQ_TEXTURE1D_U_DIMENSION || height != 1 || depth != 1 ||
          array > D3D10_REQ_TEXTURE1D_ARRAY_AXIS_DIMENSION) return false;
      break;
   case D3D10DDIRESOURCE_TEXTURE2D:
      if (width > D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
          height > D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION || depth != 1 ||
          array > D3D10_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION) return false;
      break;
   case D3D10DDIRESOURCE_TEXTURE3D:
      if (width > D3D10_REQ_TEXTURE3D_U_V_OR_W_DIMENSION ||
          height > D3D10_REQ_TEXTURE3D_U_V_OR_W_DIMENSION ||
          depth > D3D10_REQ_TEXTURE3D_U_V_OR_W_DIMENSION || array != 1)
         return false;
      break;
   case D3D10DDIRESOURCE_TEXTURECUBE:
      if (width != height || width > D3D10_REQ_TEXTURECUBE_DIMENSION || depth != 1 ||
          array > D3D10_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION) return false;
      break;
   default:
      return false;
   }

   enum pipe_format format = FormatTranslate(resource->Format, false);
   uint64_t element = util_format_get_blocksize(format);
   uint64_t blockWidth = util_format_get_blockwidth(format);
   uint64_t blockHeight = util_format_get_blockheight(format);
   uint64_t maximum =
      (uint64_t)D3D10_REQ_RESOURCE_SIZE_IN_MEGABYTES * 1024u * 1024u;
   if (!element || !blockWidth || !blockHeight) return false;
   uint64_t total = 0;
   for (unsigned level = 0; level < resource->MipLevels; ++level) {
      uint64_t levelWidth = MAX2(1ULL, width >> level);
      uint64_t levelHeight = MAX2(1ULL, height >> level);
      uint64_t levelDepth = MAX2(1ULL, depth >> level);
      uint64_t blocksWide = (levelWidth + blockWidth - 1) / blockWidth;
      uint64_t blocksHigh = (levelHeight + blockHeight - 1) / blockHeight;
      if (blocksWide > maximum / blocksHigh ||
          blocksWide * blocksHigh > maximum / levelDepth ||
          blocksWide * blocksHigh * levelDepth > maximum / array ||
          blocksWide * blocksHigh * levelDepth * array >
             (maximum - total) / element)
         return false;
      total += blocksWide * blocksHigh * levelDepth * array * element;
   }
   return true;
}'''),
        ('''   Resource *pResource = CastResource(hResource);

   memset(pResource, 0, sizeof *pResource);''','''   Resource *pResource = CastResource(hResource);
   AgxD3d10WindowsDiagnosticResource("frontend-create", pCreateResource);
   if (!AgxD3d10ResourceWithinRequiredLimits(pCreateResource)) {
      SetError(hDevice, E_NOTIMPL);
      return;
   }

   Device *pDevice = CastDevice(hDevice);
   ULONGLONG resourceOwner = 0;
   ULONG resourceGeneration = 0;
   const D3D10DDI_MIPINFO *resourceMip = pCreateResource->pMipInfoList;
   bool wantsConstant =
      (pCreateResource->BindFlags & D3D10_DDI_BIND_CONSTANT_BUFFER) != 0;
   bool wantsIndex =
      (pCreateResource->BindFlags & D3D10_DDI_BIND_INDEX_BUFFER) != 0;
   bool wantsStream =
      (pCreateResource->BindFlags & D3D10_DDI_BIND_STREAM_OUTPUT) != 0;
   bool wantsPresentation = pCreateResource->pPrimaryDesc != NULL ||
      (pCreateResource->BindFlags & D3D10_DDI_BIND_PRESENT) != 0;
   bool validConstant = wantsConstant && pResource && resourceMip &&
      pCreateResource->ResourceDimension == D3D10DDIRESOURCE_BUFFER &&
      pCreateResource->Format == DXGI_FORMAT_UNKNOWN &&
      pCreateResource->BindFlags == D3D10_DDI_BIND_CONSTANT_BUFFER &&
      (pCreateResource->Usage == D3D10_DDI_USAGE_DEFAULT ||
       pCreateResource->Usage == D3D10_DDI_USAGE_DYNAMIC) &&
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
   bool validStream=wantsStream&&pResource&&resourceMip&&
      pCreateResource->ResourceDimension==D3D10DDIRESOURCE_BUFFER&&
      pCreateResource->Format==DXGI_FORMAT_UNKNOWN&&
      pCreateResource->BindFlags==
        (D3D10_DDI_BIND_VERTEX_BUFFER|D3D10_DDI_BIND_STREAM_OUTPUT)&&
      pCreateResource->Usage==D3D10_DDI_USAGE_DEFAULT&&
      pCreateResource->MapFlags==0&&pCreateResource->MiscFlags==0&&
      !pCreateResource->pPrimaryDesc&&pCreateResource->MipLevels==1&&
      pCreateResource->ArraySize==1&&resourceMip[0].TexelWidth==256&&
      resourceMip[0].TexelHeight==1&&resourceMip[0].TexelDepth==1&&
      pCreateResource->SampleDesc.Count==1&&
      pCreateResource->SampleDesc.Quality==0&&!pCreateResource->pInitialDataUP&&
      pDevice&&AgxD3d10WindowsIdentity(pDevice->windows,&resourceOwner,
                                       &resourceGeneration);
   if ((wantsConstant && !validConstant) || (wantsIndex && !validIndex) ||
       (wantsStream && !validStream)) {
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
      pResource->usage = pCreateResource->Usage;
      pResource->bind_flags = pCreateResource->BindFlags;
      pResource->resource = AgxD3d10WindowsPresentationPipeResource(
         pResource->presentation);
      if (!pResource->resource) {
         AgxD3d10WindowsPresentationDestroy(
            pDevice->windows, &pResource->presentation);
         SetError(hDevice, E_FAIL);
         return;
      }
      pResource->Format = pCreateResource->Format;
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
   }''','''   if (wantsConstant || wantsIndex || wantsStream) {
      pResource->resource = screen->resource_create(screen, &templat);
   } else if (pCreateResource->BindFlags & D3D10_DDI_BIND_RENDER_TARGET) {
      const D3D10DDI_MIPINFO *mip = pCreateResource->pMipInfoList;
      bool private_rt = pCreateResource->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D &&
         (AgxD3d10ColorBytes(pCreateResource->Format) != 0 ||
          pCreateResource->Format == DXGI_FORMAT_R8G8B8A8_TYPELESS) &&
         pCreateResource->MipLevels == 1 && pCreateResource->ArraySize == 1 && mip &&
         mip[0].TexelWidth > 0 && mip[0].TexelHeight > 0 && mip[0].TexelDepth == 1 &&
         /* AgxD3d10ResourceWithinRequiredLimits already checked dimensions,
          * complete byte size and overflow against the pinned D3D10 limits. */
         pCreateResource->SampleDesc.Count == 1 && pCreateResource->SampleDesc.Quality == 0 &&
         pCreateResource->Usage == D3D10_DDI_USAGE_DEFAULT && pCreateResource->MapFlags == 0 &&
         (pCreateResource->BindFlags == D3D10_DDI_BIND_RENDER_TARGET ||
          pCreateResource->BindFlags == (D3D10_DDI_BIND_RENDER_TARGET |
              D3D10_DDI_BIND_SHADER_RESOURCE)) &&
         pCreateResource->MiscFlags == 0 && !pCreateResource->pPrimaryDesc &&
         !pCreateResource->pInitialDataUP;
      if (!private_rt || !screen->resource_create_with_modifiers) {
         LOG_UNSUPPORTED("Only a private uncompressed admitted render target is supported");
         SetError(hDevice, E_NOTIMPL);
         return;
      }
      const uint64_t modifier = DRM_FORMAT_MOD_APPLE_GPU_TILED;
      pResource->resource = screen->resource_create_with_modifiers(
         screen, &templat, &modifier, 1);
   } else if (pCreateResource->BindFlags & D3D10_DDI_BIND_DEPTH_STENCIL) {
      const D3D10DDI_MIPINFO *mip = pCreateResource->pMipInfoList;
      bool private_depth =
         pCreateResource->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D &&
         (pCreateResource->Format == DXGI_FORMAT_D32_FLOAT ||
          pCreateResource->Format == DXGI_FORMAT_D16_UNORM ||
          pCreateResource->Format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
          pCreateResource->Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT) &&
         pCreateResource->MipLevels == 1 && pCreateResource->ArraySize == 1 && mip &&
         mip[0].TexelWidth > 0 && mip[0].TexelHeight > 0 && mip[0].TexelDepth == 1 &&
         mip[0].TexelWidth <= 4096 && mip[0].TexelHeight <= 4096 &&
         ((uint64_t)mip[0].TexelWidth * mip[0].TexelHeight *
          (pCreateResource->Format == DXGI_FORMAT_D16_UNORM ? 2u :
           pCreateResource->Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ? 8u : 4u)) <=
            0x100000 &&
         pCreateResource->SampleDesc.Count == 1 &&
         pCreateResource->SampleDesc.Quality == 0 &&
         pCreateResource->Usage == D3D10_DDI_USAGE_DEFAULT &&
         pCreateResource->MapFlags == 0 &&
         pCreateResource->BindFlags == D3D10_DDI_BIND_DEPTH_STENCIL &&
         pCreateResource->MiscFlags == 0 && !pCreateResource->pPrimaryDesc &&
         !pCreateResource->pInitialDataUP;
      if (!private_depth || !screen->resource_create_with_modifiers) {
         LOG_UNSUPPORTED("Only a private uncompressed D16/D32 depth target is admitted");
         SetError(hDevice, E_NOTIMPL);
         return;
      }
      if(pCreateResource->Format==DXGI_FORMAT_D24_UNORM_S8_UINT ||
         pCreateResource->Format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT) {
         pResource->resource=AgxWin32AsahiCreateUncompressedDepthStencil(
            screen,&templat);
      } else {
         const uint64_t modifier = DRM_FORMAT_MOD_APPLE_GPU_TILED;
         pResource->resource = screen->resource_create_with_modifiers(
            screen, &templat, &modifier, 1);
      }
   } else if (pCreateResource->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D &&
              (pCreateResource->BindFlags & D3D10_DDI_BIND_SHADER_RESOURCE) &&
              (pCreateResource->Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ||
               pCreateResource->Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
               pCreateResource->Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
               pCreateResource->Format == DXGI_FORMAT_B8G8R8X8_TYPELESS ||
               pCreateResource->Format == DXGI_FORMAT_B8G8R8X8_UNORM ||
               pCreateResource->Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)) {
      /* The Windows capture contract admits tiled, uncompressed resources.
       * Default Asahi allocation prefers compression; initial upload would
       * then emit a compression draw outside that contract. Keep allocation
       * and upload in Asahi, selecting the already-supported native modifier. */
      if (!screen->resource_create_with_modifiers) {
         SetError(hDevice, E_NOTIMPL); return;
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
   pResource->owner_device = pDevice;
   pResource->usage = pCreateResource->Usage;
   pResource->bind_flags = pCreateResource->BindFlags;
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
   } else if (wantsStream) {
      pResource->logical_bytes = 256;
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
    replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','ResourceCopy','''   Device *device = CastDevice(hDevice);
   Resource *destination = CastResource(hDstResource);
   Resource *source = CastResource(hSrcResource);
   if (!device || !destination || !source || destination == source ||
       destination->owner_device != device || source->owner_device != device ||
       destination->usage == D3D10_DDI_USAGE_IMMUTABLE ||
       destination->MipLevels != 1 || source->MipLevels != 1 ||
       destination->NumSubResources != 1 || source->NumSubResources != 1 ||
       (destination->transfers && destination->transfers[0]) ||
       (source->transfers && source->transfers[0])) {
      SetError(hDevice, E_NOTIMPL); return;
   }
   struct pipe_resource *dst=destination->resource,*src=source->resource;
   unsigned family=AgxD3d10CopyFamily(source->Format);
   if (!dst || !src || !family || family!=AgxD3d10CopyFamily(destination->Format) ||
       dst->target!=PIPE_TEXTURE_2D || src->target!=PIPE_TEXTURE_2D ||
       dst->width0!=src->width0 || dst->height0!=src->height0 ||
       dst->depth0!=1 || src->depth0!=1 || dst->array_size!=1 || src->array_size!=1 ||
       dst->last_level || src->last_level || dst->nr_samples>1 || src->nr_samples>1) {
      SetError(hDevice, E_NOTIMPL); return;
   }
   HRESULT result;
   if (destination->presentation && source->presentation) {
      result=AgxD3d10WindowsPresentationBlt(device->windows,
         destination->presentation,source->presentation);
   } else {
      /* Copy the stored four bytes, independent of any bound sampling view.
       * UNORM views avoid sRGB conversion; BGRA also preserves the X byte. */
      enum pipe_format raw=family==3 ? PIPE_FORMAT_R8G8B8A8_UNORM : PIPE_FORMAT_B8G8R8A8_UNORM;
      struct pipe_blit_info info={};
      info.src.resource=src;info.src.format=raw;
      info.src.box.width=src->width0;info.src.box.height=src->height0;info.src.box.depth=1;
      info.dst.resource=dst;info.dst.format=raw;
      info.dst.box.width=dst->width0;info.dst.box.height=dst->height0;info.dst.box.depth=1;
      info.mask=PIPE_MASK_RGBA;info.filter=PIPE_TEX_FILTER_NEAREST;
      device->pipe->blit(device->pipe,&info);
      device->pipe->flush(device->pipe,NULL,0);
      result=AgxD3d10WindowsFlushStatus(device->windows);
   }
   if (FAILED(result)) SetError(hDevice,result);''')
    replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','ResourceCopyRegion','''   Resource *source = CastResource(hSrcResource);
   struct pipe_resource *src = source ? source->resource : NULL;
   bool whole = src && (!pSrcBox || (pSrcBox->left == 0 && pSrcBox->top == 0 &&
      pSrcBox->front == 0 && pSrcBox->right == src->width0 &&
      pSrcBox->bottom == src->height0 && pSrcBox->back == src->depth0));
   if (DstSubResource || SrcSubResource || DstX || DstY || DstZ || !whole) {
      SetError(hDevice, E_NOTIMPL); return;
   }
   ResourceCopy(hDevice,hDstResource,hSrcResource);''')
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
   pResource->resource = AgxD3d10WindowsPresentationPipeResource(
      pResource->presentation);
   if (!pResource->resource) {
      AgxD3d10WindowsPresentationDestroy(
         pDevice->windows, &pResource->presentation);
      SetError(hDevice, E_FAIL);
      return;
   }
   pResource->Format = pResource->resource->format == PIPE_FORMAT_R8G8B8A8_UNORM
      ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
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
    replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','ResourceMap','''   Device *device = CastDevice(hDevice);
   Resource *resource = CastResource(hResource);
   bool dynamicBuffer = resource && resource->usage == D3D10_DDI_USAGE_DYNAMIC &&
      (resource->bind_flags == D3D10_DDI_BIND_VERTEX_BUFFER ||
       resource->bind_flags == D3D10_DDI_BIND_INDEX_BUFFER ||
       resource->bind_flags == D3D10_DDI_BIND_CONSTANT_BUFFER);
   bool dynamicTexture = resource && !resource->buffer &&
      resource->usage == D3D10_DDI_USAGE_DYNAMIC &&
      resource->bind_flags == D3D10_DDI_BIND_SHADER_RESOURCE &&
      AgxD3d10CopyFamily(resource->Format) != 0;
   bool stagingResource = resource && resource->usage == D3D10_DDI_USAGE_STAGING &&
      resource->bind_flags == 0;
   bool mapMode = dynamicBuffer ?
      (DDIMap == D3D10_DDI_MAP_WRITE_DISCARD ||
       DDIMap == D3D10_DDI_MAP_WRITE_NOOVERWRITE) :
      dynamicTexture ? DDIMap == D3D10_DDI_MAP_WRITE_DISCARD :
      stagingResource && (DDIMap == D3D10_DDI_MAP_READ ||
                        DDIMap == D3D10_DDI_MAP_WRITE ||
                        DDIMap == D3D10_DDI_MAP_READWRITE);
   if (!device || !resource || resource->owner_device != device ||
       !resource->resource || !resource->transfers ||
       (!resource->buffer && !AgxD3d10CopyFamily(resource->Format)) ||
       SubResource >= resource->NumSubResources ||
       (Flags & ~D3D10_DDI_MAP_FLAG_DONOTWAIT) || !pMappedSubResource || !mapMode ||
       (Flags && (DDIMap == D3D10_DDI_MAP_WRITE_DISCARD ||
                  DDIMap == D3D10_DDI_MAP_WRITE_NOOVERWRITE)) ||
       resource->transfers[SubResource] ||
       (resource->bind_flags == D3D10_DDI_BIND_CONSTANT_BUFFER &&
        DDIMap != D3D10_DDI_MAP_WRITE_DISCARD)) {
      SetError(hDevice, E_INVALIDARG); return;
   }
   pMappedSubResource->pData=NULL;
   pMappedSubResource->RowPitch=pMappedSubResource->DepthPitch=0;
   HRESULT status = (Flags & D3D10_DDI_MAP_FLAG_DONOTWAIT) ?
      AgxD3d10WindowsTryFlushRetire(device->windows) :
      AgxD3d10WindowsFlushRetire(device->windows);
   if (FAILED(status)) { SetError(hDevice, status); return; }
   struct pipe_box box;
   unsigned level = 0;
   subResourceBox(resource->resource,SubResource,&level,&box);
   unsigned usage = DDIMap == D3D10_DDI_MAP_READ ? PIPE_MAP_READ :
      DDIMap == D3D10_DDI_MAP_WRITE ? PIPE_MAP_WRITE :
      DDIMap == D3D10_DDI_MAP_READWRITE ? PIPE_MAP_READ|PIPE_MAP_WRITE :
      PIPE_MAP_WRITE | (DDIMap == D3D10_DDI_MAP_WRITE_DISCARD ?
       (resource->NumSubResources == 1 ? PIPE_MAP_DISCARD_WHOLE_RESOURCE :
        PIPE_MAP_DISCARD_RANGE) : PIPE_MAP_UNSYNCHRONIZED);
   void *map = resource->buffer ?
      device->pipe->buffer_map(device->pipe,resource->resource,level,usage,
                               &box,&resource->transfers[SubResource]) :
      device->pipe->texture_map(device->pipe,resource->resource,level,usage,
                                &box,&resource->transfers[SubResource]);
   if (!map || !resource->transfers[SubResource]) { SetError(hDevice,E_FAIL); return; }
   pMappedSubResource->pData=map;
   pMappedSubResource->RowPitch=resource->transfers[SubResource]->stride;
   pMappedSubResource->DepthPitch=resource->transfers[SubResource]->layer_stride;''')
    replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','ResourceUnmap','''   Device *device = CastDevice(hDevice);
   Resource *resource = CastResource(hResource);
   if (!device || !resource || resource->owner_device != device ||
       !resource->resource || !resource->transfers ||
       (!resource->buffer && !AgxD3d10CopyFamily(resource->Format)) ||
       SubResource >= resource->NumSubResources || !resource->transfers[SubResource]) {
      SetError(hDevice, E_INVALIDARG); return;
   }
   if (resource->buffer)
      pipe_buffer_unmap(device->pipe,resource->transfers[SubResource]);
   else
      pipe_texture_unmap(device->pipe,resource->transfers[SubResource]);
   resource->transfers[SubResource]=NULL;''')
    replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','ResourceIsStagingBusy','''   Device *device=CastDevice(hDevice);
   Resource *resource=CastResource(hResource);
   if(!device || !resource || resource->owner_device!=device || !resource->resource)
      return TRUE;
   return AgxWin32AsahiResourceBusy(device->pipe,resource->resource) ? TRUE : FALSE;''')
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
    # Observe exactly one rejection with its original arguments, including
    # ResourceCopyRegion errors reported by the delegated ResourceCopy helper.
    resource_scopes = {
        'CreateResource': """   const D3D10DDIARG_CREATERESOURCE *r=pCreateResource;
   UINT refusalArgs[16]={r?(UINT)r->Format:~0u,r?(UINT)r->ResourceDimension:~0u,
      r?r->Usage:~0u,r?r->BindFlags:~0u,r?r->MapFlags:~0u,r?r->MiscFlags:~0u,
      r?r->MipLevels:0,r?r->ArraySize:0,r?r->SampleDesc.Count:0,r?r->SampleDesc.Quality:0,
      r&&r->pPrimaryDesc,r&&r->pInitialDataUP,0,0,0,r&&r->pMipInfoList};
   if(r&&r->pMipInfoList&&r->MipLevels) {
      refusalArgs[12]=r->pMipInfoList[0].TexelWidth;
      refusalArgs[13]=r->pMipInfoList[0].TexelHeight;
      refusalArgs[14]=r->pMipInfoList[0].TexelDepth;
   }
   AgxD3d10RefusalScope refusal(r&&r->MiscFlags?"reject-CreateResource":NULL,refusalArgs,16);
""",
        'OpenResource': """   UINT refusalArgs[8]={pOpenResource?pOpenResource->NumAllocations:0,
      pOpenResource?pOpenResource->PrivateDriverDataSize:0,
      pOpenResource&&pOpenResource->pPrivateDriverData,
      pOpenResource&&pOpenResource->pOpenAllocationInfo,0,0,0,0};
   if(pOpenResource&&pOpenResource->NumAllocations&&pOpenResource->pOpenAllocationInfo) {
      refusalArgs[4]=pOpenResource->pOpenAllocationInfo[0].PrivateDriverDataSize;
      refusalArgs[5]=pOpenResource->pOpenAllocationInfo[0].hAllocation;
   }
   refusalArgs[6]=(UINT)(UINT_PTR)hRTResource.handle;
   refusalArgs[7]=(UINT)(((UINT64)(UINT_PTR)hRTResource.handle)>>32);
   AgxD3d10RefusalScope refusal("reject-OpenResource",refusalArgs,8);
""",
        'ResourceMap': """   Resource *traceResource=CastResource(hResource);
   UINT refusalArgs[8]={SubResource,(UINT)DDIMap,Flags,
      (UINT)(UINT_PTR)hResource.pDrvPrivate,(UINT)(((UINT64)(UINT_PTR)hResource.pDrvPrivate)>>32),
      traceResource?(UINT)traceResource->Format:~0u,
      traceResource?traceResource->usage:~0u,traceResource?traceResource->bind_flags:~0u};
   AgxD3d10RefusalScope refusal("reject-ResourceMap",refusalArgs,8);
""",
        'ResourceCopyRegion': """   UINT refusalArgs[16]={DstSubResource,DstX,DstY,DstZ,SrcSubResource,
      pSrcBox!=NULL,pSrcBox?(UINT)pSrcBox->left:0,pSrcBox?(UINT)pSrcBox->top:0,
      pSrcBox?(UINT)pSrcBox->front:0,pSrcBox?(UINT)pSrcBox->right:0,
      pSrcBox?(UINT)pSrcBox->bottom:0,pSrcBox?(UINT)pSrcBox->back:0,
      (UINT)(UINT_PTR)hDstResource.pDrvPrivate,(UINT)(((UINT64)(UINT_PTR)hDstResource.pDrvPrivate)>>32),
      (UINT)(UINT_PTR)hSrcResource.pDrvPrivate,(UINT)(((UINT64)(UINT_PTR)hSrcResource.pDrvPrivate)>>32)};
   AgxD3d10RefusalScope refusal("reject-ResourceCopyRegion",refusalArgs,16);
"""}
    for name, entry in resource_scopes.items():
        text=resource_path.read_text()
        marker='\n'+name+'('
        if text.count(marker)!=1: raise SystemExit('Ambiguous refusal scope: '+name)
        start=text.index('{',text.index(marker))+1
        resource_path.write_text(text[:start]+'\n'+entry+text[start:])
    overlays['src/gallium/frontends/d3d10umd/Resource.cpp']['after']=hashlib.sha256(resource_path.read_bytes()).hexdigest()
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
             'struct agx_bo *bo = AgxWin32AsahiEncoderCreate(dev, 0x80000, 0, "Encoder");'),
            ('   batch->uniforms.tables[AGX_SYSVAL_TABLE_PARAMS] = 0;',
             '   memset(batch->uniforms.tables, 0, sizeof(batch->uniforms.tables));')])
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
        shader_ir='''   nir_shader *nir = cso->type == PIPE_SHADER_IR_NIR
                        ? cso->ir.nir
                        : tgsi_to_nir(cso->tokens, pctx->screen, false);'''
        if state_text.count(shader_ir)!=1:
            raise SystemExit('Ambiguous native TGSI stream-output anchor')
        state_text=state_text.replace(shader_ir,shader_ir+'''

   /* The D3D10 frontend carries stream output beside TGSI.  Preserve the
    * exact admitted FL10_0 declaration when converting TGSI to NIR so Asahi
    * owns both XFB emission and the DrawAuto byte-stride contract. */
   if (cso->stream_output.num_outputs != 0 && !nir->xfb_info) {
      const struct pipe_stream_output_info *pipe_xfb = &cso->stream_output;
      const struct pipe_stream_output *pipe_out = &pipe_xfb->output[0];
      bool exact_windows_xfb =
         pipe_xfb->num_outputs == 1 && pipe_xfb->stride[0] == 4 &&
         pipe_out->output_buffer == 0 && pipe_out->register_index == 0 &&
         pipe_out->start_component == 0 && pipe_out->num_components == 4 &&
         pipe_out->dst_offset == 0 && pipe_out->stream == 0;
      if (!exact_windows_xfb) {
         ralloc_free(nir);
         ralloc_free(so);
         return NULL;
      }

      nir_xfb_info *xfb = rzalloc_size(nir, nir_xfb_info_size(1));
      if (!xfb) {
         ralloc_free(nir);
         ralloc_free(so);
         return NULL;
      }
      xfb->buffers_written = BITFIELD_BIT(0);
      xfb->streams_written = BITFIELD_BIT(0);
      xfb->buffers[0].stride = pipe_xfb->stride[0] * 4;
      xfb->buffers[0].varying_count = 1;
      xfb->buffer_to_stream[0] = 0;
      xfb->output_count = 1;
      xfb->outputs[0].buffer = 0;
      xfb->outputs[0].offset = 0;
      xfb->outputs[0].location = pipe_out->register_index;
      xfb->outputs[0].component_mask = BITFIELD_MASK(4);
      xfb->outputs[0].component_offset = 0;
      nir->xfb_info = xfb;
      nir->info.xfb_stride[0] = pipe_xfb->stride[0];
   }''',1)
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
