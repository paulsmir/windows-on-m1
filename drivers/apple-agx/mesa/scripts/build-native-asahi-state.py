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


def project_shader_failure(state, shader, device):
    """Project checked executable allocation through shader/device creation.

    This changes only the copied Windows build inputs. Every anchor is pinned
    to one occurrence so a Mesa revision cannot silently drop a failure edge.
    """
    def replace_once(source, old, new):
        if source.count(old) != 1:
            raise ValueError('Ambiguous shader failure anchor: ' + old[:72])
        return source.replace(old, new, 1)

    state = replace_once(state, '#include "agx_state.h"',
        '#include "agx_state.h"\n#include "agx_win32_asahi_bo.h"')
    state = replace_once(state,
        '   struct agx_compiled_shader *compiled = CALLOC_STRUCT(agx_compiled_shader);\n   compiled->stage = stage;',
        '   struct agx_compiled_shader *compiled = CALLOC_STRUCT(agx_compiled_shader);\n'
        '   if (!compiled)\n      return NULL;\n   compiled->stage = stage;')
    state = replace_once(state, '''      memcpy(agx_bo_map(compiled->bo), compiled->b.binary,
             compiled->b.info.binary_size);''', '''      if (!compiled->bo) {
         free(compiled->b.binary);
         FREE(compiled);
         return NULL;
      }
      void *mapped = agx_bo_map(compiled->bo);
      if (!mapped) {
         agx_bo_unreference(dev, compiled->bo);
         free(compiled->b.binary);
         FREE(compiled);
         return NULL;
      }
      memcpy(mapped, compiled->b.binary, compiled->b.info.binary_size);''')
    state = replace_once(state,
        'static struct agx_compiled_shader *\nagx_compile_variant(',
        'static void agx_delete_compiled_shader(struct agx_device *dev,\n'
        '                                       struct agx_compiled_shader *so);\n\n'
        'static struct agx_compiled_shader *\nagx_compile_variant(')
    state = replace_once(state, '''      false, 0, attrib_components_read);

   if (so->type == MESA_SHADER_FRAGMENT) {''', '''      false, 0, attrib_components_read);
   if (!compiled)
      goto fail;

   if (so->type == MESA_SHADER_FRAGMENT) {''')
    state = replace_once(state, '''      compiled->gs_count->so = so;''', '''      if (!compiled->gs_count)
         goto fail;
      compiled->gs_count->so = so;''')
    state = replace_once(state, '''                         true, false, 0, NULL);
   }

   if (gs_copy) {''', '''                         true, false, 0, NULL);
      if (!compiled->pre_gs)
         goto fail;
   }

   if (gs_copy) {''')
    state = replace_once(state, '''      compiled->gs_copy->so = so;''', '''      if (!compiled->gs_copy)
         goto fail;
      compiled->gs_copy->so = so;''')
    state = replace_once(state, '''   ralloc_free(gs_count);
   return compiled;
}

static struct agx_compiled_shader *
agx_get_shader_variant''', '''   ralloc_free(gs_count);
   return compiled;

fail:
   ralloc_free(nir);
   ralloc_free(pre_gs);
   ralloc_free(gs_count);
   ralloc_free(gs_copy);
   if (compiled)
      agx_delete_compiled_shader(dev, compiled);
   return NULL;
}

static struct agx_compiled_shader *
agx_get_shader_variant''')
    state = replace_once(state, '''      compiled = agx_compile_variant(&screen->dev, pctx, so, key);
      agx_disk_cache_store(screen->disk_cache, so, key, compiled);''', '''      compiled = agx_compile_variant(&screen->dev, pctx, so, key);
      if (!compiled)
         return NULL;
      agx_disk_cache_store(screen->disk_cache, so, key, compiled);''')
    state = replace_once(state, '''   union asahi_shader_key *cloned_key =
      rzalloc(so->variants, union asahi_shader_key);

   if (so->type''', '''   union asahi_shader_key *cloned_key =
      rzalloc(so->variants, union asahi_shader_key);
   if (!cloned_key) {
      agx_delete_compiled_shader(&screen->dev, compiled);
      return NULL;
   }

   if (so->type''')
    state = replace_once(state,
        'static void *\nagx_create_shader_state(',
        'static void agx_delete_uncompiled_shader(struct agx_device *dev,\n'
        '                                         struct agx_uncompiled_shader *so);\n\n'
        'static void *\nagx_create_shader_state(')
    state = replace_once(state, '''      agx_get_shader_variant(agx_screen(pctx->screen), pctx, so, &key);
   } else if (so->type == MESA_SHADER_VERTEX) {''', '''      if (!agx_get_shader_variant(agx_screen(pctx->screen), pctx, so, &key)) {
         agx_delete_uncompiled_shader(dev, so);
         return NULL;
      }
   } else if (so->type == MESA_SHADER_VERTEX) {''')
    state = replace_once(state, '''      agx_get_shader_variant(agx_screen(pctx->screen), pctx, so, &key);

      if (next_stage == MESA_SHADER_NONE) {''', '''      if (!agx_get_shader_variant(agx_screen(pctx->screen), pctx, so, &key)) {
         agx_delete_uncompiled_shader(dev, so);
         return NULL;
      }

      if (next_stage == MESA_SHADER_NONE) {''')
    state = replace_once(state, '''         agx_get_shader_variant(agx_screen(pctx->screen), pctx, so, &key);
      }
   } else if (dev->debug & AGX_DBG_PRECOMPILE)''', '''         if (!agx_get_shader_variant(agx_screen(pctx->screen), pctx, so, &key)) {
            agx_delete_uncompiled_shader(dev, so);
            return NULL;
         }
      }
   } else if (dev->debug & AGX_DBG_PRECOMPILE)''')

    state = replace_once(state, '''      agx_compile_variant(dev, pctx, so, &key);
   }

   return so;''', '''      if (!agx_compile_variant(dev, pctx, so, &key)) {
         agx_delete_uncompiled_shader(dev, so);
         return NULL;
      }
   }

   return so;''')

    state = replace_once(state, 'static void *\nagx_create_compute_state(',
        'static void agx_win32_shader_failed(struct agx_context *ctx);\n\n'
        'static void *\nagx_create_compute_state(')
    state = replace_once(state, '''   agx_get_shader_variant(agx_screen(pctx->screen), pctx, so, &key);

   /* We're done with the NIR, throw it away */''', '''   if (!agx_get_shader_variant(agx_screen(pctx->screen), pctx, so, &key)) {
      ralloc_free(nir);
      agx_delete_uncompiled_shader(dev, so);
      return NULL;
   }

   /* We're done with the NIR, throw it away */''')
    state = replace_once(state, '''      agx_get_shader_variant(agx_screen(pctx->screen), pctx, cso, &key);

   info->max_threads =''', '''      agx_get_shader_variant(agx_screen(pctx->screen), pctx, cso, &key);
   if (!so) {
      memset(info, 0, sizeof(*info));
      agx_win32_shader_failed(agx_context(pctx));
      return;
   }

   info->max_threads =''')

    # A later shader key can compile at draw time. Keep that failure out of
    # the cache and stop the Windows draw before it reads the missing variant.
    state = replace_once(state, '''static bool
agx_update_shader(''', '''static void
agx_win32_shader_failed(struct agx_context *ctx)
{
   ctx->any_faults = true;
   struct agx_device *dev = agx_device(ctx->base.screen);
   AGX_WIN32_ASAHI_BACKEND *backend = dev->windows_private;
   if (backend)
      backend->Failed = 1;
}

static bool
agx_update_shader(''')
    state = replace_once(state, '''   *out = agx_get_shader_variant(screen, &ctx->base, so, key);
   return true;''', '''   *out = agx_get_shader_variant(screen, &ctx->base, so, key);
   if (!*out) {
      agx_win32_shader_failed(ctx);
      return false;
   }
   return true;''')
    state = replace_once(state, '''   agx_update_shader(ctx, &ctx->vs, MESA_SHADER_VERTEX,
                     (union asahi_shader_key *)&key);

   struct agx_device''', '''   agx_update_shader(ctx, &ctx->vs, MESA_SHADER_VERTEX,
                     (union asahi_shader_key *)&key);
   if (!ctx->vs)
      return false;

   struct agx_device''')
    state = replace_once(state, '''   agx_update_shader(ctx, &ctx->fs, MESA_SHADER_FRAGMENT,
                     (union asahi_shader_key *)&key);

   /* Fast link''', '''   agx_update_shader(ctx, &ctx->fs, MESA_SHADER_FRAGMENT,
                     (union asahi_shader_key *)&key);
   if (!ctx->fs)
      return false;

   /* Fast link''')
    state = replace_once(state, '''   ralloc_free(b.shader);

   /* ..and cache it before we return.''', '''   ralloc_free(b.shader);
   if (!shader)
      return NULL;

   /* ..and cache it before we return.''')
    state = replace_once(state, '''   struct agx_linked_shader *linked =
      rzalloc(so->linked_shaders, struct agx_linked_shader);
   agx_fast_link''', '''   if ((so->type == MESA_SHADER_FRAGMENT && (!prolog || !epilog)) ||
       (so->type == MESA_SHADER_VERTEX && !prolog)) {
      agx_win32_shader_failed(ctx);
      return NULL;
   }
   struct agx_linked_shader *linked =
      rzalloc(so->linked_shaders, struct agx_linked_shader);
   if (!linked) {
      agx_win32_shader_failed(ctx);
      return NULL;
   }
   agx_fast_link''')
    state = replace_once(state, '''   ctx->linked.vs =
      asahi_fast_link(ctx, ctx->stage[MESA_SHADER_VERTEX].shader, &link_key);

   agx_batch_add_bo''', '''   ctx->linked.vs =
      asahi_fast_link(ctx, ctx->stage[MESA_SHADER_VERTEX].shader, &link_key);
   if (!ctx->linked.vs) {
      agx_win32_shader_failed(ctx);
      return false;
   }

   agx_batch_add_bo''')
    state = replace_once(state, '''   ctx->linked.fs =
      asahi_fast_link(ctx, ctx->stage[MESA_SHADER_FRAGMENT].shader, &link_key);

   if (ctx->fs->bo)''', '''   ctx->linked.fs =
      asahi_fast_link(ctx, ctx->stage[MESA_SHADER_FRAGMENT].shader, &link_key);
   if (!ctx->linked.fs) {
      agx_win32_shader_failed(ctx);
      return false;
   }

   if (ctx->fs->bo)''')
    state = replace_once(state, '''   agx_update_vs(batch, info->index_size);
   agx_update_tcs(ctx, info);''', '''   agx_update_vs(batch, info->index_size);
   if (ctx->any_faults)
      return;
   agx_update_tcs(ctx, info);''')
    state = replace_once(state, '''   /* This is subtle. But agx_update_vs will be true at least once per batch. */
   assert(agx_batch_uses_bo''', '''   if (ctx->any_faults)
      return;
   /* This is subtle. But agx_update_vs will be true at least once per batch. */
   assert(agx_batch_uses_bo''')
    state = replace_once(state, '''   /* This is subtle. But agx_update_fs will be true at least once per batch. */
   assert(!ctx->fs->bo''', '''   if (ctx->any_faults)
      return;
   /* This is subtle. But agx_update_fs will be true at least once per batch. */
   assert(!ctx->fs->bo''')

    shader = replace_once(shader, '''   if (!tokens)
      return NULL;

   ureg_destroy(ureg);''', '''   if (!tokens) {
      ureg_destroy(ureg);
      return NULL;
   }

   ureg_destroy(ureg);''')
    shader = replace_once(shader, '''   assert(handle);

   ureg_free_tokens(tokens);''', '''   ureg_free_tokens(tokens);''')

    device = replace_once(device, '''   pDevice->empty_vs = CreateEmptyShader(pDevice, MESA_SHADER_VERTEX);
   pDevice->empty_fs = CreateEmptyShader(pDevice, MESA_SHADER_FRAGMENT);

   pipe->bind_vs_state''', '''   pDevice->empty_vs = CreateEmptyShader(pDevice, MESA_SHADER_VERTEX);
   if (!pDevice->empty_vs) {
      cso_destroy_context(pDevice->cso);
      pDevice->cso = NULL;
      BOOL consumed = FALSE;
      pDevice->cleanup_result = AgxD3d10WindowsDestroyDeviceDdi(
         &pDevice->windows, &consumed);
      pDevice->pipe = NULL;
      return E_OUTOFMEMORY;
   }
   pDevice->empty_fs = CreateEmptyShader(pDevice, MESA_SHADER_FRAGMENT);
   if (!pDevice->empty_fs) {
      DeleteEmptyShader(pDevice, MESA_SHADER_VERTEX, pDevice->empty_vs);
      pDevice->empty_vs = NULL;
      cso_destroy_context(pDevice->cso);
      pDevice->cso = NULL;
      BOOL consumed = FALSE;
      pDevice->cleanup_result = AgxD3d10WindowsDestroyDeviceDdi(
         &pDevice->windows, &consumed);
      pDevice->pipe = NULL;
      return E_OUTOFMEMORY;
   }

   pipe->bind_vs_state''')
    return state, shader, device

parser = argparse.ArgumentParser()
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--windows-platform-declarations', action='store_true')
parser.add_argument('--native-state-test', action='store_true')
parser.add_argument('--native-batch-lifecycle', action='store_true')
parser.add_argument('--gpuva', action='store_true')
parser.add_argument('--prepare-only', action='store_true')
parser.add_argument('--architecture', choices=('x64','arm64','x86'), default='x64')
parser.add_argument('--project',type=Path)
# EXP1182: a Mesa build other than nir-<architecture> (e.g. b_ndebug=true).
parser.add_argument('--mesa-build', type=Path)
args = parser.parse_args()
if (args.native_batch_lifecycle or args.prepare_only) and not (args.windows_platform_declarations and args.project):
    parser.error('Native lifecycle/prepare requires --windows-platform-declarations and --project')
if args.gpuva and not args.native_batch_lifecycle:
    parser.error('GPUVA requires native batch lifecycle projection')
out = args.output
out.mkdir(exist_ok=False)
mesa = Path(r'C:\Users\pauls\AD04-d3d10-frontend-build\mesa')
build = args.mesa_build or Path(r'C:\Users\pauls\AD04-fullcompiler-001') / ('nir-'+args.architecture)
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
        ('''      case D3D10_SB_OPCODE_LD_MS:
         /* XXX: We don't support multi-sampling yet, but we need to parse
          * this opcode regardless, so we just ignore sample index operand
          * for now */
      case D3D10_SB_OPCODE_LD:''','''      case D3D10_SB_OPCODE_LD_MS: {
         struct ureg_src srcreg[3];
         srcreg[0]=translate_src_operand(&sx,&opcode.src[0],OF_INT);
         srcreg[1]=translate_src_operand(&sx,&opcode.src[1],OF_INT);
         srcreg[2]=translate_src_operand(&sx,&opcode.src[2],OF_INT);
         sample_ureg_emit(ureg,TGSI_OPCODE_SAMPLE_I_MS,3,&opcode,
            translate_dst_operand(&sx,&opcode.dst[0],opcode.saturate),srcreg);
         break;
      }
      case D3D10_SB_OPCODE_LD:'''),
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
   } else if (dcl_siv_name == D3D10_SB_NAME_IS_FRONT_FACE) {
      /* EXP1298: Asahi's fs_face_is_integer_sysval requires a system value;
       * tgsi_to_nir rejects a FACE input (agx_pipe.c:2125,
       * tgsi_to_nir.c:601-604,645). */
      reg = ureg_DECL_system_value(ureg, TGSI_SEMANTIC_FACE, 0);
   } else {
      reg = ureg_DECL_fs_input(ureg, translate_system_name(dcl_siv_name),
                              0, TGSI_INTERPOLATE_CONSTANT);
   }'''),
        ('''      /* We need to map gallium's front_face to the one expected
       * by D3D10 */
      struct ureg_dst tmp = ureg_DECL_temporary(ureg);

      tmp = ureg_writemask(tmp, TGSI_WRITEMASK_X);

      ureg_CMP(ureg, tmp, reg,
               ureg_imm1i(ureg, 0), ureg_imm1i(ureg, -1));''','''      /* EXP1298: tgsi_to_nir.c:505-521 supplies integer 0xffffffff/0
       * for Asahi's FACE sysval; D3D's SV_IsFrontFace has that encoding. */
      struct ureg_dst tmp = ureg_DECL_temporary(ureg);

      tmp = ureg_writemask(tmp, TGSI_WRITEMASK_X);

      ureg_USNE(ureg, tmp, ureg_scalar(reg, TGSI_SWIZZLE_X),
                ureg_imm1u(ureg, 0));'''),
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
   }'''),
        ('''            for (i = 0; i < opcode.specific.dcl_indexable_temp.count; i++) {
               sx.temps[sx.declared_temps + i] = ureg_DECL_temporary(ureg);
            }
            sx.declared_temps += opcode.specific.dcl_indexable_temp.count;''','''            /* DCL_INDEXABLE_TEMP is one relative-addressable TGSI array.
             * EXP751 reached this with DirectComposition's ClearGuard VS;
             * separate scalar declarations make TTN reject the indirect src. */
            struct ureg_dst array = ureg_DECL_array_temporary(
               ureg, opcode.specific.dcl_indexable_temp.count, false);
            for (i = 0; i < opcode.specific.dcl_indexable_temp.count; i++)
               sx.temps[sx.declared_temps + i] =
                  ureg_dst_array_offset(array, i);
            sx.declared_temps += opcode.specific.dcl_indexable_temp.count;'''),
        # EXP1230: dwm.exe overflowed a worker thread's stack in this UMD
        # (0xC00000FD at __chkstk) when the first redirected GL window
        # appeared; this translator kept 89 KB of state on the stack. Keep it
        # on the heap (the function has one exit).
        ('''   struct Shader_xlate sx;\n''','''   struct Shader_xlate *sx_heap = CALLOC_STRUCT(Shader_xlate);
#define sx (*sx_heap)\n'''),
        ('''   memset(&sx, 0, sizeof sx);\n''','''   if (!sx_heap)
      return NULL;
   memset(&sx, 0, sizeof sx);\n'''),
        ('''   return tokens;\n''','''   FREE(sx_heap);
#undef sx
   return tokens;\n''')])
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
   unsigned op = ins->Instruction.Opcode;
   bool ms = op == TGSI_OPCODE_SAMPLE_I_MS;
   bool fetch = ms || op == TGSI_OPCODE_SAMPLE_I;
   unsigned sampler = fetch ? 0 : ins->Src[2].Register.Index;
   bool explicit_lod = op == TGSI_OPCODE_SAMPLE_L;
   /* D3D-G9: sample_b (bias), sample_c / sample_c_lz (depth comparison,
    * SampleCmp / SampleCmpLevelZero) and sample_d (gradients). */
   bool bias = op == TGSI_OPCODE_SAMPLE_B;
   bool compare = op == TGSI_OPCODE_SAMPLE_C || op == TGSI_OPCODE_SAMPLE_C_LZ;
   bool level_zero = op == TGSI_OPCODE_SAMPLE_C_LZ;
   bool grad = op == TGSI_OPCODE_SAMPLE_D;
   assert(ins->Src[1].Register.File == TGSI_FILE_SAMPLER_VIEW);
   assert(!ins->Src[1].Register.Indirect);
   if(!fetch) {
      assert(ins->Src[2].Register.File == TGSI_FILE_SAMPLER);
      assert(!ins->Src[2].Register.Indirect && sampler < PIPE_MAX_SAMPLERS);
   }
   assert(texture < c->num_samp_types && ins->Texture.NumOffsets <= 1);
   enum glsl_sampler_dim dim;
   bool shadow,array;
   get_texture_info(c->samp_targets[texture],&dim,&shadow,&array);
   bool fetch_lod=fetch && !ms && dim!=GLSL_SAMPLER_DIM_BUF && dim!=GLSL_SAMPLER_DIM_RECT;
   nir_tex_instr *tex = nir_tex_instr_create(b->shader,
      1 + explicit_lod + level_zero + bias + compare + 2 * grad + fetch_lod +
      ms + ins->Texture.NumOffsets);
   tex->op = ms ? nir_texop_txf_ms : fetch ? nir_texop_txf :
      explicit_lod || level_zero ? nir_texop_txl : bias ? nir_texop_txb :
      grad ? nir_texop_txd : nir_texop_tex;
   tex->can_speculate = true;
   tex->sampler_dim=dim;tex->is_shadow=shadow||compare;tex->is_array=array;
   tex->is_new_style_shadow = compare;
   tex->coord_components = glsl_get_sampler_dim_coordinate_components(tex->sampler_dim) + tex->is_array;
   tex->texture_index = texture;
   tex->sampler_index = sampler;
   tex->dest_type = c->samp_types[texture];
   unsigned n = 0;
   tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_coord,
      nir_trim_vector(b, src[0], tex->coord_components));
   if (explicit_lod)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_channel(b, src[3], 0));
   if (level_zero)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_float(b, 0.0f));
   if (bias)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_bias, nir_channel(b, src[3], 0));
   if (compare)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_comparator, nir_channel(b, src[3], 0));
   if (grad) {
      unsigned grad_components = tex->coord_components - tex->is_array;
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_ddx, nir_trim_vector(b, src[3], grad_components));
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_ddy, nir_trim_vector(b, src[4], grad_components));
   }
   if(fetch_lod)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_lod,nir_channel(b,src[0],3));
   if(ms)
      tex->src[n++] = nir_tex_src_for_ssa(nir_tex_src_ms_index,nir_channel(b,src[2],0));
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
   if(fetch) BITSET_SET(b->shader->info.textures_used_by_txf,texture);
   else BITSET_SET(b->shader->info.samplers_used, sampler);
   nir_def_init(&tex->instr, &tex->def, nir_tex_instr_dest_size(tex), 32);
   nir_builder_instr_insert(b, &tex->instr);
   /* A comparison returns one value; D3D writes it to every component. */
   if (compare)
      return nir_replicate(b, &tex->def, 4);
   return nir_pad_vector_imm_int(b, &tex->def, 0, 4);
}

/* D3D-G9: resinfo (SVIEWINFO): the view's size at a mip level in .xyz and
 * its level count in .w, as integers (the frontend converts them). */
static nir_def *
ttn_d3d_sviewinfo(struct ttn_compile *c, nir_def **src)
{
   nir_builder *b = &c->build;
   struct tgsi_full_instruction *ins = &c->token->FullInstruction;
   unsigned texture = ins->Src[1].Register.Index;
   assert(ins->Src[1].Register.File == TGSI_FILE_SAMPLER_VIEW);
   assert(texture < c->num_samp_types);
   enum glsl_sampler_dim dim;
   bool shadow, array;
   get_texture_info(c->samp_targets[texture], &dim, &shadow, &array);
   bool has_lod = dim != GLSL_SAMPLER_DIM_BUF && dim != GLSL_SAMPLER_DIM_MS;
   nir_tex_instr *txs = nir_tex_instr_create(b->shader, has_lod ? 1 : 0);
   txs->op = nir_texop_txs;
   txs->sampler_dim = dim; txs->is_array = array; txs->is_shadow = shadow;
   txs->texture_index = texture; txs->sampler_index = 0;
   txs->dest_type = nir_type_int32;
   if (has_lod)
      txs->src[0] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_channel(b, src[0], 0));
   nir_def_init(&txs->instr, &txs->def, nir_tex_instr_dest_size(txs), 32);
   nir_builder_instr_insert(b, &txs->instr);
   nir_def *levels = nir_imm_int(b, 1);
   if (has_lod) {
      nir_tex_instr *query = nir_tex_instr_create(b->shader, 0);
      query->op = nir_texop_query_levels;
      query->sampler_dim = dim; query->is_array = array; query->is_shadow = shadow;
      query->texture_index = texture; query->sampler_index = 0;
      query->dest_type = nir_type_int32;
      nir_def_init(&query->instr, &query->def, 1, 32);
      nir_builder_instr_insert(b, &query->instr);
      levels = &query->def;
   }
   c->num_samplers = MAX2(c->num_samplers, texture + 1);
   BITSET_SET(b->shader->info.textures_used, texture);
   nir_def *size = nir_pad_vector_imm_int(b, &txs->def, 0, 3);
   return nir_vec4(b, nir_channel(b, size, 0), nir_channel(b, size, 1),
                   nir_channel(b, size, 2), levels);
}

static nir_def *
ttn_tex('''),
        ('''      src[i] = ttn_get_src(c, &tgsi_inst->Src[i], i);''','''      if (((tgsi_op == TGSI_OPCODE_SAMPLE || tgsi_op == TGSI_OPCODE_SAMPLE_L ||
            tgsi_op == TGSI_OPCODE_SAMPLE_B || tgsi_op == TGSI_OPCODE_SAMPLE_C ||
            tgsi_op == TGSI_OPCODE_SAMPLE_C_LZ || tgsi_op == TGSI_OPCODE_SAMPLE_D) &&
           (i == 1 || i == 2)) ||
          ((tgsi_op == TGSI_OPCODE_SAMPLE_I || tgsi_op == TGSI_OPCODE_SAMPLE_I_MS ||
            tgsi_op == TGSI_OPCODE_SVIEWINFO) && i == 1))
         src[i] = NULL; /* Descriptor operands are not ALU values. */
      else
         src[i] = ttn_get_src(c, &tgsi_inst->Src[i], i);'''),
        ('''   case TGSI_OPCODE_TEX:
   case TGSI_OPCODE_TXP:''','''   case TGSI_OPCODE_SAMPLE:
   case TGSI_OPCODE_SAMPLE_L:
   case TGSI_OPCODE_SAMPLE_I:
   case TGSI_OPCODE_SAMPLE_I_MS:
   case TGSI_OPCODE_SAMPLE_B:
   case TGSI_OPCODE_SAMPLE_C:
   case TGSI_OPCODE_SAMPLE_C_LZ:
   case TGSI_OPCODE_SAMPLE_D:
      dst = ttn_d3d_sample(c, src);
      break;

   case TGSI_OPCODE_SVIEWINFO:
      dst = ttn_d3d_sviewinfo(c, src);
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
   D3D10_0_x_DDI_SUPPORTED,
   /* D3D-H2a: advertised only under AgxD3d11DdiEnabled() */
   D3D11_0_DDI_SUPPORTED,
   /* D3D-H7: last, advertised only under AgxWddm2_0DdiEnabled() */
   D3DWDDM2_0_DDI_SUPPORTED,
};'''),
        ('''#include "Device.h"
#include "State.h"''','''#include "Device.h"
#include "State.h"
#include "agx_d3d11_ddi.h"''')])
    replace_function_body('src/gallium/frontends/d3d10umd/Adapter.cpp','GetSupportedVersions','''   UINT64 versions[8];
   UINT count = ARRAYSIZE(SupportedDDIInterfaceVersions) - 2u +
      (AgxD3d11DdiEnabled() ? 1u + (AgxWddm2_0DdiEnabled() ? 1u : 0u) : 0u);
   memcpy(versions, SupportedDDIInterfaceVersions, count * sizeof(versions[0]));
   /* EXP1302 diagnostic (FL 11_0 processes only): EXP1301's runtime opened
    * the adapter with 11.17 and refused the list 10.0/10.0x/11.0/11.32 before
    * CreateDevice; APPLE_AGX_D3D11_DDI_PROBE replaces the D3D11 part of the
    * list (letters, in order: 0 = 11.0, a = 11.1, b = WDDM 1.3, c = WDDM 2.0
    * as header minor 5 encodes it (11.17 build 5), w = WDDM 2.0 (header 9+))
    * so g4-create-device-args names the interface the runtime picks. */
   char probe[8];
   DWORD probe_length = AgxD3d11Fl11Enabled() ?
      GetEnvironmentVariableA("APPLE_AGX_D3D11_DDI_PROBE", probe, sizeof(probe)) : 0u;
   if (probe_length && probe_length < sizeof(probe)) {
      count = 2u;
      for (DWORD i = 0; i < probe_length && count < ARRAYSIZE(versions); ++i) {
         switch (probe[i]) {
         case '0': versions[count++] = D3D11_0_DDI_SUPPORTED; break;
         case 'a': versions[count++] = D3D11_1_DDI_SUPPORTED; break;
         case 'b': versions[count++] = D3DWDDM1_3_DDI_SUPPORTED; break;
         case 'c': versions[count++] = (((UINT64)((D3D11_DDI_MAJOR_VERSION << 16) | 17u)) << 32) |
                                       (((UINT64)5u) << 16); break;
         case 'w': versions[count++] = D3DWDDM2_0_DDI_SUPPORTED; break;
         default: break;
         }
      }
   }
   if (!puEntries)
      return E_INVALIDARG;
   if (pSupportedDDIInterfaceVersions && *puEntries < count)
      return E_OUTOFMEMORY;
   *puEntries = count;
   if (pSupportedDDIInterfaceVersions)
      memcpy(pSupportedDDIInterfaceVersions, versions, count * sizeof(versions[0]));
   /* EXP1297: adapter queries before a refused FL 11_0 device (receipt);
    * EXP1302: with each listed interface (high word) and build (low word) */
   UINT values[10] = {count, pSupportedDDIInterfaceVersions ? 1u : 0u};
   for (UINT i = 0; i < count && i < 8u; ++i)
      values[2u + i] = (UINT)(versions[i] >> 32) << 8 | (UINT)((versions[i] >> 16) & 0xffu);
   AgxD3d10WindowsDiagnostic("measure-getversions", S_OK, values, 2u + (count < 8u ? count : 8u));
   return S_OK;''')
    replace_function_body('src/gallium/frontends/d3d10umd/Adapter.cpp','GetCaps','''   Adapter *pAdapter = CastAdapter(hAdapter);
   /* EXP1297: the runtime refused a FL 11_0 device (DXGI_ERROR_DRIVER_
    * INTERNAL_ERROR) after OpenAdapter alone; every caps query is logged
    * with its answer (receipt only). */
   auto caps = [&]() -> HRESULT {
   if (!pAdapter || !pData || !pData->pData)
      return E_INVALIDARG;
   switch (pData->Type) {
   case D3D11DDICAPS_THREADING:
      if (pData->DataSize != sizeof(D3D11DDI_THREADING_CAPS))
         return E_INVALIDARG;
      ((D3D11DDI_THREADING_CAPS *)pData->pData)->Caps = 0;
      return S_OK;
   case D3D11DDICAPS_SHADER:
      /* D3D-H2a: no doubles.  D3D-H2b: compute with raw and structured
       * buffers at SM 4.x (cs_4_0/cs_4_1) once its switch is on. */
      if (pData->DataSize != sizeof(D3D11DDI_SHADER_CAPS))
         return E_INVALIDARG;
      /* EXP1303: the runtime refuses a FL 11_0 adapter whose shader caps
       * deny 4.x compute (0x887a0020 right after this query; with the bit
       * it goes on to CreateDevice); FL 11_0 implies it, and cs_4_x passes
       * on hardware since EXP1299. */
      ((D3D11DDI_SHADER_CAPS *)pData->pData)->Caps =
         (AgxD3d11ComputeEnabled() || AgxD3d11Fl11Enabled()) ?
         D3D11DDICAPS_SHADER_COMPUTE_PLUS_RAW_AND_STRUCTURED_BUFFERS_IN_SHADER_4_X : 0;
      return S_OK;
   case D3D11DDICAPS_3DPIPELINESUPPORT:
      if (pData->DataSize != sizeof(D3D11DDI_3DPIPELINESUPPORT_CAPS))
         return E_INVALIDARG;
      /* D3D-H5: 10_1 and 11_0 per process (APPLE_AGX_D3D11_FL11) while
       * their checkpoints are measured: 8x MSAA, which FL 11_0 lists for
       * most render-target formats, is absent on AGX and reported as 0
       * quality levels */
      ((D3D11DDI_3DPIPELINESUPPORT_CAPS *)pData->pData)->Caps =
         D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(
            D3D11DDI_3DPIPELINELEVEL_10_0) |
         (AgxD3d11Fl11Enabled() ?
          (D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_1) |
           D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_11_0)) : 0u);
      return S_OK;
   /* D3D-H7: the 11.1 to WDDM 2.0 caps (agx_wddm2_ddi.cpp implements or
    * fails closed each entry these caps leave reachable) */
   case D3D11_1DDICAPS_D3D11_OPTIONS:
      if (pData->DataSize != sizeof(D3D11_1DDI_D3D11_OPTIONS_DATA))
         return E_INVALIDARG;
      ((D3D11_1DDI_D3D11_OPTIONS_DATA *)pData->pData)->OutputMergerLogicOp = FALSE;
      ((D3D11_1DDI_D3D11_OPTIONS_DATA *)pData->pData)->AssignDebugBinarySupport = FALSE;
      return S_OK;
   case D3D11_1DDICAPS_ARCHITECTURE_INFO:
      if (pData->DataSize != sizeof(D3D11_1DDI_ARCHITECTURE_INFO_DATA))
         return E_INVALIDARG;
      /* AGX renders tile by tile (deferred) */
      ((D3D11_1DDI_ARCHITECTURE_INFO_DATA *)pData->pData)->TileBasedDeferredRenderer = TRUE;
      return S_OK;
   case D3D11_1DDICAPS_SHADER_MIN_PRECISION_SUPPORT:
      if (pData->DataSize != sizeof(D3D11_DDI_SHADER_MIN_PRECISION_SUPPORT_DATA))
         return E_INVALIDARG;
      /* full precision only: min-precision types run as 32 bits */
      ((D3D11_DDI_SHADER_MIN_PRECISION_SUPPORT_DATA *)pData->pData)->PixelShaderMinPrecision = 0;
      ((D3D11_DDI_SHADER_MIN_PRECISION_SUPPORT_DATA *)pData->pData)->AllOtherStagesMinPrecision = 0;
      return S_OK;
   case D3DWDDM1_3DDICAPS_D3D11_OPTIONS1:
      if (pData->DataSize != sizeof(D3DWDDM1_3DDI_D3D11_OPTIONS_DATA1))
         return E_INVALIDARG;
      ((D3DWDDM1_3DDI_D3D11_OPTIONS_DATA1 *)pData->pData)->TiledResourcesSupportFlags = 0;
      return S_OK;
   case D3DWDDM1_3DDICAPS_MARKER:
      if (pData->DataSize != sizeof(D3DWDDM1_3DDI_MARKER_TYPE))
         return E_INVALIDARG;
      *(D3DWDDM1_3DDI_MARKER_TYPE *)pData->pData = D3DWDDM1_3DDI_MARKER_TYPE_NONE;
      return S_OK;
   case D3DWDDM2_0DDICAPS_D3D11_OPTIONS2:
      if (pData->DataSize != sizeof(D3DWDDM2_0DDI_D3D11_OPTIONS2_DATA) &&
          pData->DataSize != sizeof(D3DWDDM2_0DDI_D3D11_OPTIONS2_DATA_LEGACY_ASTC))
         return E_INVALIDARG;
      memset(pData->pData, 0, pData->DataSize); /* no conservative rasterization, no ASTC */
      return S_OK;
   case D3DWDDM2_0DDICAPS_D3D11_OPTIONS3:
      if (pData->DataSize != sizeof(D3DWDDM2_0DDI_D3D11_OPTIONS3_DATA))
         return E_INVALIDARG;
      ((D3DWDDM2_0DDI_D3D11_OPTIONS3_DATA *)pData->pData)->
         VPAndRTArrayIndexFromAnyShaderFeedingRasterizer = FALSE;
      return S_OK;
   case D3DWDDM2_0DDICAPS_MEMORY_ARCHITECTURE:
      if (pData->DataSize != sizeof(D3DWDDM2_0DDI_MEMORY_ARCHITECTURE_CAPS))
         return E_INVALIDARG;
      /* one memory shared with the CPU; coherence is not promised */
      ((D3DWDDM2_0DDI_MEMORY_ARCHITECTURE_CAPS *)pData->pData)->UMA = TRUE;
      ((D3DWDDM2_0DDI_MEMORY_ARCHITECTURE_CAPS *)pData->pData)->CacheCoherent = FALSE;
      return S_OK;
   case D3DWDDM2_0DDICAPS_TEXTURE_LAYOUT:
      if (pData->DataSize != sizeof(D3DWDDM2_0DDI_TEXTURE_LAYOUT_CAPS))
         return E_INVALIDARG;
      /* no device-dependent layout is exposed (GetResourceLayout fails closed) */
      ((D3DWDDM2_0DDI_TEXTURE_LAYOUT_CAPS *)pData->pData)->DeviceDependentLayoutCount = 0;
      return S_OK;
   case D3DWDDM2_0DDICAPS_GPUVA_CAPS:
      if (pData->DataSize != sizeof(D3DWDDM2_0DDI_GPUVA_CAPS_DATA))
         return E_INVALIDARG;
      /* 39-bit GPU VA (apple_agx_gpuva_g3_caps.h); a resource spans < 4 GiB */
      ((D3DWDDM2_0DDI_GPUVA_CAPS_DATA *)pData->pData)->MaxGPUVirtualAddressBitsPerResource = 32;
      return S_OK;
   default:
      return E_NOTIMPL;
   }
   };
   const HRESULT hr = caps();
   UINT values[2] = {pData ? (UINT)pData->Type : ~0u, pData ? (UINT)pData->DataSize : 0u};
   AgxD3d10WindowsDiagnostic("measure-getcaps", hr, values, 2u);
   return hr;''')
    # EXP1080 diagnostic: DWM is CPU-bound (EXP1078 CSwitch trace) and this
    # guest has no ETW sampling source. Time every d3d10umd DDI entry
    # (LOG_ENTRYPOINT) with a scoped QPC timer; agx_d3d10_windows.cpp sums
    # the time per DDI and reports DWM's share of wall time inside the UMD.
    change('src/gallium/frontends/d3d10umd/Debug.h',
        '5e8e975d8f224db6cf747143cf8d1a667f308e6a4851322fcb4adc6401d38125',[
        ('''#else
#define LOG_ENTRYPOINT() (void)0
#endif''','''#elif defined(_WIN32) && defined(__cplusplus)
extern "C" void AgxD3d10DdiTimerNote(const char *name, long long ticks,
                                     unsigned depth);
inline unsigned &AgxDdiTimerDepth() {
   static thread_local unsigned depth;
   return depth;
}
struct AgxDdiTimer {
   const char *name;
   LARGE_INTEGER start;
   unsigned depth;
   explicit AgxDdiTimer(const char *n) : name(n), depth(AgxDdiTimerDepth()++) {
      QueryPerformanceCounter(&start);
   }
   ~AgxDdiTimer() {
      LARGE_INTEGER end;
      QueryPerformanceCounter(&end);
      --AgxDdiTimerDepth();
      AgxD3d10DdiTimerNote(name, end.QuadPart - start.QuadPart, depth);
   }
   AgxDdiTimer(const AgxDdiTimer &) = delete;
   AgxDdiTimer &operator=(const AgxDdiTimer &) = delete;
};
#define AGX_DDI_CAT2(a, b) a##b
#define AGX_DDI_CAT(a, b) AGX_DDI_CAT2(a, b)
#define LOG_ENTRYPOINT() AgxDdiTimer AGX_DDI_CAT(agx_ddi_timer_, __LINE__)(__func__)
#else
#define LOG_ENTRYPOINT() (void)0
#endif''')])
    change('src/gallium/frontends/d3d10umd/State.h',
        '4280c406ca8c1c199d09a0d062f8b52fb0baaec43a2ca7482e0d1a3acc7c4dd3',[
        ('#include "DriverIncludes.h"',
         '#include "DriverIncludes.h"\n#include "agx_d3d10_windows.h"'),
        ('''   unsigned output_mapping[PIPE_MAX_SHADER_OUTPUTS];
   bool output_resolved;
};''','''   unsigned output_mapping[PIPE_MAX_SHADER_OUTPUTS];
   bool output_resolved;
   /* D3D-G10e: a vertex shader's serial; a stream output without geometry
    * shader code (an "empty" GS) keeps the vertex-shader variant carrying
    * its stream output, made for the VS of serial so_vs_serial. */
   uint64_t agx_serial;
   void *so_vs_handle;
   uint64_t so_vs_serial;
   /* D3D-H2b: a compute shader's thread-group size (dcl_thread_group),
    * the block every Dispatch launches. */
   UINT agx_block[3];
   /* D3D-H5: an SM 5 vertex shader's tokens (it has no TGSI record), the
    * source of its stream-output variant. */
   UINT *agx_code;
};

/* D3D-H5: a vertex or geometry shader state with stream output from the
 * DXBC -> NIR translator (registers are D3D output registers). */
void *AgxD3d10CreateXfbState(struct pipe_context *pipe, Shader *shader,
                             const UINT *code,
                             const struct pipe_stream_output_info *so);'''),
        ('struct pipe_screen *screen;',
         'AGX_D3D10_WINDOWS_ADAPTER *windows;'),
        ('struct pipe_context *pipe;','''struct pipe_context *pipe;
   AGX_D3D10_WINDOWS_DEVICE *windows;
   HRESULT cleanup_result;
   bool frontend_ready;
   /* The 10.0 DDI keeps the legacy MultisampleEnable triangle rule. */
   bool agx_d3d10_0_interface;
   /* EXP994: D3D disables depth/stencil while no DSV is bound. */
   void *app_dsa;
   void *no_depth_dsa;
   /* EXP1064 receipt: last SetRenderTargets binding (slots, NULL views,
    * views without a texture, DSV present). */
   UINT agx_rt_args[4];
   /* EXP1065: vertex elements were bound at least once (see update_velems). */
   BOOL agx_velems_bound;
   /* D3D-G10e: serials of bound vertex shaders (stream-output variants). */
   uint64_t agx_shader_serial;
   /* D3D-H2b: the bound compute shader's thread-group size (0: none). */
   UINT agx_cs_block[3];
   /* D3D-G11: right/bottom pixel extent of the bound viewports. */
   UINT agx_viewport_extent[2];'''),
        ('''static inline void
SetError(D3D10DDI_HDEVICE hDevice, HRESULT hr)
{
   if (FAILED(hr)) {
      Device *pDevice = CastDevice(hDevice);
      pDevice->UMCallbacks.pfnSetErrorCb(pDevice->hRTCoreLayer, hr);
   }
}''','''static inline void
AgxSetErrorWithOrigin(D3D10DDI_HDEVICE hDevice, HRESULT hr,
                      const char *origin, UINT line)
{
   if (FAILED(hr)) {
      AgxD3d10WindowsDiagnosticSetError(origin, line, hr);
      Device *pDevice = CastDevice(hDevice);
      pDevice->UMCallbacks.pfnSetErrorCb(pDevice->hRTCoreLayer, hr);
   }
}
#define SetError(device, hr) \
   AgxSetErrorWithOrigin((device), (hr), __func__, __LINE__)

/* EXP1064 receipt-only: a draw reaching the pipe with neither a color nor a
 * depth target (EXP1063 kind-6 drops: text quads), with the last binding. */
static inline void
AgxD3d10NoTargetReceipt(Device *pDevice, UINT kind, UINT count)
{
   static volatile LONG records;
   if (!pDevice || pDevice->fb.nr_cbufs || pDevice->fb.zsbuf.texture) return;
   if (InterlockedIncrement(&records) > 24) return;
   UINT values[7] = {kind, count, (UINT)pDevice->primitive,
      pDevice->agx_rt_args[0], pDevice->agx_rt_args[1],
      pDevice->agx_rt_args[2], pDevice->agx_rt_args[3]};
   AgxD3d10WindowsDiagnostic("reject-no-target", S_OK, values, 7u);
}

/* D3D-G11: with neither a render target nor a depth-stencil view bound the
 * pass rasterizes within its viewports (stream output, UAV-only pixel
 * shaders). Gallium sizes such a framebuffer by width/height/layers/samples
 * (util_framebuffer_get_num_layers, ARB_framebuffer_no_attachments); the
 * D3D10 frontend left it 0x0. Returns whether the size changed. */
static inline bool
AgxD3d10NoAttachmentSize(Device *pDevice)
{
   if (pDevice->fb.nr_cbufs || pDevice->fb.zsbuf.texture)
      return false;
   const unsigned width = MAX2(pDevice->agx_viewport_extent[0], 1u);
   const unsigned height = MAX2(pDevice->agx_viewport_extent[1], 1u);
   const bool changed = pDevice->fb.width != width ||
      pDevice->fb.height != height || pDevice->fb.layers != 1u ||
      pDevice->fb.samples != 1u;
   pDevice->fb.width = width;
   pDevice->fb.height = height;
   pDevice->fb.layers = 1u;
   pDevice->fb.samples = 1u;
   return changed;
}'''),
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
   QueryCreated, QueryBuilding, QueryIssued, QuerySignaled, QueryFailed,
   QueryDestroyed
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
   /* D3D-G3: occlusion counters and predicates are Mesa pipe queries; an
    * event query has no pipe query and completes on the Windows fence.
    * D3D-G10d: pipeline statistics are eight single-statistic pipe queries
    * and stream-output statistics two (written, generated): handle is the
    * first, extra the rest. EXP1336: the D3D 11 pipeline statistics are
    * eleven (HS, DS and CS invocations added). */
   D3D10DDI_QUERY Type;
   UINT Flags;
   struct pipe_query *handle;
   struct pipe_query *extra[10];
   UINT extra_count;
   /* D3D-H5: a timestamp query's value, taken when its fence completed */
   UINT64 agx_timestamp;
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
   UINT cpu_access;
   void *dynamic_shadow;
   void *active_buffer_map;
   bool direct_buffer_map;
   bool shadow_only_map;
   bool shadow_dirty;
   bool shadow_stale;
   /* EXP1037: client-format CPU views of lowered textures (Resource.cpp). */
   void *client_maps;
   Device *owner_device;
   ULONGLONG owner_cookie;
   ULONG device_generation;
   DXGI_FORMAT sample_format;
   UINT sample_level;
   UINT sample_layer;
   AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *presentation;
   struct ShaderResourceView *first_presentation_srv;
   struct RenderTargetView *first_presentation_rtv;
   /* D3D-H2b: D3D11 buffer misc flags (raw views / structured) and the
    * structure stride (D3D11DDIARG_CREATERESOURCE::ByteStride). */
   UINT agx_misc_flags;
   UINT agx_structure_stride;
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
   ShaderResourceView *next_presentation;
};'''),
        ('''struct RenderTargetView
{
   struct pipe_surface surface;
   D3D10DDI_HRTRENDERTARGETVIEW hRTRenderTargetView;
};''','''struct RenderTargetView
{
   struct pipe_surface surface;
   D3D10DDI_HRTRENDERTARGETVIEW hRTRenderTargetView;
   Resource *owner_resource;
   RenderTargetView *next_presentation;
};''')])
    change('src/gallium/frontends/d3d10umd/Device.cpp',
        'dcf950aec993d40743671e1f208655e151158a9b4647bc3581dcd134962086aa',[
        ('''   return sizeof(Device);
}''', '''   SIZE_T size = sizeof(Device);
   UINT values[4] = {pData->Interface, pData->Version, pData->Flags,
                     (UINT)size};
   AgxD3d10WindowsDiagnostic("measure-calc-private-device-size", S_OK,
                             values, 4u);
   return size;
}'''),
        ('#include "Format.h"', '''#include "Format.h"
/* EXP1331: the DXGI base table has pfnResolveSharedResource (DXGI 1.1) when
 * IS_DXGI1_1_BASE_FUNCTIONS holds and in every DXGI 1.2+ table. The WDDM 2.0
 * runtime passes Version 0x00090000, whose low word fails the 1.1 test, so
 * the 1.2 test must count too: without it the WDDM 2.0 (FL 11_0) devices
 * left the entry NULL and returned DXGI_STATUS_NO_REDIRECTION. */
static bool
AgxDxgiHasResolveSharedResource(UINT interfaceVersion, UINT runtimeVersion)
{
   return IS_DXGI1_1_BASE_FUNCTIONS(interfaceVersion, runtimeVersion) ||
          IS_DXGI1_2_BASE_FUNCTIONS(interfaceVersion, runtimeVersion);
}

static HRESULT
AgxNativeSharedPresentationStatus(UINT interfaceVersion, UINT runtimeVersion)
{
   /* The native GPUVA implementation has GPU-backed shared allocations and
    * implements the extended table's ResolveSharedResource retirement path.
    * Keep CPU-only/legacy profiles on their original presentation policy. */
   if (!AgxDxgiHasResolveSharedResource(interfaceVersion, runtimeVersion))
      return DXGI_STATUS_NO_REDIRECTION;
#ifdef APPLE_AGX_GPUVA_WINSYS
   return S_OK;
#else
   return DXGI_STATUS_NO_REDIRECTION;
#endif
}

static HRESULT APIENTRY
AgxResolveSharedResource(DXGI_DDI_ARG_RESOLVESHAREDRESOURCE *args)
{
   Device *device=args?CastDevice(args->hDevice):NULL;
   Resource *resource=args?CastResource(args->hResource):NULL;
   HRESULT result=E_INVALIDARG;
   if(device && resource && resource->owner_device==device && resource->resource)
      result=AgxD3d10WindowsFlushRetire(device->windows);
   if(FAILED(result)) {
      UINT values[3]={args?(UINT)(UINT_PTR)args->hResource:0,
         args?(UINT)(((UINT64)(UINT_PTR)args->hResource)>>32):0,device!=NULL};
      AgxD3d10WindowsDiagnostic("reject-ResolveSharedResource",result,values,3);
   }
   return result;
}
'''),
        ('''   pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions->pfnBlt =
      _Blt;''','''   pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions->pfnBlt =
      _Blt;
   /* Version is supplied by the runtime. Never write the extended slot
    * when it supplied only the base table. EXP748 measures this negotiation. */
   if(AgxDxgiHasResolveSharedResource(pCreateData->Interface,pCreateData->Version))
      pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions2->pfnResolveSharedResource =
         AgxResolveSharedResource;
   /* EXP1328: AgxWddm2_0FillDxgiFuncs keeps the DXGI 1.1 entries for Present1
    * and Blt1, so it runs after they are written. Filled before them, it kept
    * a NULL pfnPresent and pfnBlt and every Present1 on a WDDM 2.0 (FL 11_0)
    * device called address 0 (dwm.exe, M365Copilot, opengl32's redirection:
    * d3d11!NDXGI::CDevice::PresentImpl -> Present1 -> 0). */
   if(pCreateData->Interface == D3DWDDM2_0_DDI_INTERFACE_VERSION &&
      IS_DXGI1_4_BASE_FUNCTIONS(pCreateData->Interface, pCreateData->Version))
      AgxWddm2_0FillDxgiFuncs(pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions5);
   UINT present_contract[3] = { (UINT)pCreateData->Interface,
      (UINT)pCreateData->Version,
      IS_DXGI1_3_BASE_FUNCTIONS(pCreateData->Interface,pCreateData->Version) };
   AgxD3d10WindowsPresentMeasure(AdmissionUmdMeasureNativeDevice,
      AgxNativeSharedPresentationStatus(pCreateData->Interface,
         pCreateData->Version),present_contract,3u);'''),
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
   pDevice->agx_d3d10_0_interface =
      pCreateData->Interface == D3D10_0_DDI_INTERFACE_VERSION ||
      pCreateData->Interface == D3D10_0_x_DDI_INTERFACE_VERSION ||
      pCreateData->Interface == D3D10_0_7_DDI_INTERFACE_VERSION;
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
   if (AgxNativeSharedPresentationStatus(pCreateData->Interface,
          pCreateData->Version) == S_OK) {
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
   }''','''   /* D3D-G10a probe (APPLE_AGX_D3D_MSAA_CAPS bit 0x40): the screen's
    * answers, as the upstream frontend gives them -- render target and
    * blending where the screen renders the format (no blending for pure
    * integers), filtered sampling where it samples it (not pure integers,
    * not the depth-stencil formats); typeless parents answer only the
    * multisample bits below. Without the bit the EXP1284 list. */
   struct pipe_context *agx_pipe = CastPipeContext(hDevice);
   struct pipe_screen *agx_screen = agx_pipe ? agx_pipe->screen : NULL;
   if ((AgxD3d10WindowsMsaaProbe() & 0x40u) && agx_screen) {
      enum pipe_format format = FormatTranslate(Format, false);
      bool depth_target = Format == DXGI_FORMAT_D32_FLOAT ||
         Format == DXGI_FORMAT_D16_UNORM || Format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
         Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
      bool lowered = AgxD3d10LoweredTextureFormat(Format) != PIPE_FORMAT_NONE;
      *pFormatCaps = 0;
      if (Format == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM) {
         *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED;
      } else if (format != PIPE_FORMAT_NONE &&
                 AgxD3d10WindowsCastParent(Format, NULL) != Format) {
         bool integer = util_format_is_pure_integer(format);
         if (!depth_target && !lowered &&
             !util_format_is_depth_or_stencil(format) &&
             agx_screen->is_format_supported(agx_screen, format, PIPE_TEXTURE_2D, 0, 0,
                                             PIPE_BIND_RENDER_TARGET)) {
            *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET;
            if (!integer)
               *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_BLENDABLE;
         }
         if (!depth_target && !integer &&
             agx_screen->is_format_supported(agx_screen,
                lowered ? AgxD3d10LoweredTextureFormat(Format) : format,
                PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_SAMPLER_VIEW))
            *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE;
         if ((*pFormatCaps & D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE) &&
             (Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
              Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
              Format == DXGI_FORMAT_B8G8R8X8_UNORM ||
              Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB))
            *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_LOAD;
      }
   } else
   if (Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
       Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
       Format == DXGI_FORMAT_B8G8R8X8_UNORM ||
       Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB) {
      *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET |
                     D3D10_DDI_FORMAT_SUPPORT_BLENDABLE |
                     D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE |
                     D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_LOAD |
                     D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET;
      /* EXP749: ld2dms preserves the sample index and reaches native txf_ms.
       * D3D-G5: 2x/4x render targets (KMD utile_config/tib_blocks); the
       * runtime derives resolve support, ResolveSubresource blits. */
   } else if (Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
       Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
      /* EXP1285: MULTISAMPLE_LOAD only where the frontend admits a sampled
       * view (the BGRA8 family); these targets are not sampled here. */
      *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET |
                     D3D10_DDI_FORMAT_SUPPORT_BLENDABLE |
                     D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET;
   } else if (Format == DXGI_FORMAT_R8_UNORM || Format == DXGI_FORMAT_R16_FLOAT ||
       Format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
       Format == DXGI_FORMAT_R10G10B10A2_UNORM ||
       Format == DXGI_FORMAT_R11G11B10_FLOAT ||
       Format == DXGI_FORMAT_B5G6R5_UNORM || Format == DXGI_FORMAT_A8_UNORM) {
      *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET |
                     D3D10_DDI_FORMAT_SUPPORT_BLENDABLE;
   } else if (Format == DXGI_FORMAT_D32_FLOAT ||
              Format == DXGI_FORMAT_D16_UNORM ||
              Format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
              Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT) {
      /* Depth support is base-assumed, not an optional DDI bit. EXP1285:
       * multisampled depth is optional at FL 10_0 and must be reported here
       * for every depth format CheckMultisampleQualityLevels gives 2x/4x
       * levels (the runtime refused the device when the two disagreed). */
      *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET;
   } else if (Format == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM) {
      *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED;
   } else {
      *pFormatCaps = 0;
   }
   /* EXP1289 probe: the APPLE_AGX_D3D_MSAA_CAPS policy selects the formats
    * answering MULTISAMPLE_RENDERTARGET (0: the EXP1284 answers, none); with
    * a mask set every answer is receipted. */
   {
      UINT probe = AgxD3d10WindowsMsaaProbe();
      *pFormatCaps &= ~(UINT)D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET;
      if (AgxD3d10WindowsMsaaRenderTarget(Format, probe))
         *pFormatCaps |= D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET;
      if (probe)
         AgxD3d10WindowsMsaaReceipt(1u, (UINT)Format, 0u, *pFormatCaps);
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
       Format == DXGI_FORMAT_R16_UINT ||
       (AgxD3d10WindowsMsaaProbe() & 2u)) && SampleCount == 1 ? 1 :
      /* D3D-G5: 2x and 4x for the multisampled render-target and depth set
       * (EXP1289 probe: the APPLE_AGX_D3D_MSAA_CAPS policy selects it). */
      AgxD3d10WindowsMsaaFormat(Format, AgxD3d10WindowsMsaaProbe()) &&
      (SampleCount == 2 || SampleCount == 4) ? 1 : 0;
   if (AgxD3d10WindowsMsaaProbe())
      AgxD3d10WindowsMsaaReceipt(2u, (UINT)Format, SampleCount, *pNumQualityLevels);'''),
        # D3D-H2a: accept the D3D11 interface and fill its table
        ('''#include "Draw.h"
#include "DxgiFns.h"''','''#include "Draw.h"
#include "DxgiFns.h"
#include "agx_d3d11_ddi.h"'''),
        ('''#if SUPPORT_D3D10_1
   case D3D10_1_DDI_INTERFACE_VERSION:
   case D3D10_1_x_DDI_INTERFACE_VERSION:
   case D3D10_1_7_DDI_INTERFACE_VERSION:
#endif
      break;
   default:
      DebugPrintf("%s: unsupported interface version 0x%08x\\n",''','''#if SUPPORT_D3D10_1
   case D3D10_1_DDI_INTERFACE_VERSION:
   case D3D10_1_x_DDI_INTERFACE_VERSION:
   case D3D10_1_7_DDI_INTERFACE_VERSION:
#endif
   case D3D11_0_DDI_INTERFACE_VERSION:
   case D3D11_0_7_DDI_INTERFACE_VERSION:
   case D3DWDDM2_0_DDI_INTERFACE_VERSION:
      break;
   default: {
      /* EXP1302: the interface the runtime chose from the probe list */
      UINT chosen[2] = {pCreateData->Interface, pCreateData->Version};
      AgxD3d10WindowsDiagnostic("reject-createdevice-interface", E_FAIL, chosen, 2u);
      }
      DebugPrintf("%s: unsupported interface version 0x%08x\\n",'''),
        ('''      p10_1DeviceFuncs->pfnResourceConvert = ResourceCopy;
      p10_1DeviceFuncs->pfnResourceConvertRegion = ResourceCopyRegion;
   }''','''      p10_1DeviceFuncs->pfnResourceConvert = ResourceCopy;
      p10_1DeviceFuncs->pfnResourceConvertRegion = ResourceCopyRegion;
   }
   /* D3D-H2a: the D3D11 table (agx_d3d11_ddi.cpp) replaces the 10.0 entries
    * written above into the same, larger, runtime-owned table. */
   if (pCreateData->Interface == D3D11_0_DDI_INTERFACE_VERSION ||
       pCreateData->Interface == D3D11_0_7_DDI_INTERFACE_VERSION) {
      const D3D10DDI_DEVICEFUNCS base = *pDeviceFuncs;
      AgxD3d11FillDeviceFuncs(pCreateData->p11DeviceFuncs, &base);
   }
   /* D3D-H7: the WDDM 2.0 table (agx_wddm2_ddi.cpp) over the same memory
    * and the (absent) video sub-device; its DXGI 1.2-1.4 entries follow the
    * DXGI 1.1 ones below */
   if (pCreateData->Interface == D3DWDDM2_0_DDI_INTERFACE_VERSION) {
      const D3D10DDI_DEVICEFUNCS base = *pDeviceFuncs;
      AgxWddm2_0FillDeviceFuncs(pCreateData->pWDDM2_0DeviceFuncs, &base);
      if (pCreateData->ppfnRetrieveSubObject)
         *pCreateData->ppfnRetrieveSubObject = AgxWddm2_0RetrieveSubObject;
   }''')])
    replace_function_body('src/gallium/frontends/d3d10umd/Device.cpp','Flush','''   Device *pDevice = CastDevice(hDevice);
   HRESULT result = AgxD3d10WindowsQueryCollect(pDevice->windows);
   UINT stage[1] = {1u};
   AgxD3d10WindowsPresentMeasure(AdmissionUmdMeasureNativeFlushStage,result,stage,1u);
   if (SUCCEEDED(result)) {
      pDevice->pipe->flush(pDevice->pipe, NULL, 0);
      result = AgxD3d10WindowsFlushStatus(pDevice->windows);
      stage[0] = 2u;
      AgxD3d10WindowsPresentMeasure(AdmissionUmdMeasureNativeFlushStage,result,stage,1u);
   }
   if (SUCCEEDED(result)) {
      result = AgxD3d10WindowsFlushDeferredResources(pDevice->windows);
      stage[0] = 3u;
      AgxD3d10WindowsPresentMeasure(AdmissionUmdMeasureNativeFlushStage,result,stage,1u);
   }
   if (SUCCEEDED(result)) {
      result = AgxD3d10WindowsQueryCollect(pDevice->windows);
      stage[0] = 4u;
      AgxD3d10WindowsPresentMeasure(AdmissionUmdMeasureNativeFlushStage,result,stage,1u);
   }
   if (FAILED(result)) SetError(hDevice, result);''')
    change('src/gallium/frontends/d3d10umd/Rasterizer.cpp',
        'ac147983d5e22231be020606d395278a07fd7f8222297102ba5fd5baf237cd0d',[
        ('''   state.line_smooth = (pRasterizerDesc->AntialiasedLineEnable ? 1 : 0);''',
         '''   /* D3D10.1+/11: MultisampleEnable selects the line algorithm.
    * D3D10.0 keeps its legacy all-primitives switch. */
   Device *pDevice = CastDevice(hDevice);
   state.line_smooth = !pDevice->agx_d3d10_0_interface &&
                       !pRasterizerDesc->MultisampleEnable &&
                       pRasterizerDesc->AntialiasedLineEnable;'''),
        ('''   state.multisample = /* pRasterizerDesc->MultisampleEnable */ 0;''',
         '''   state.multisample = !pDevice->agx_d3d10_0_interface ||
                       pRasterizerDesc->MultisampleEnable;'''),
        ('''   state.line_rectangular = 0;''',
         '''   state.line_rectangular = pRasterizerDesc->MultisampleEnable ? 1 : 0;'''),
        # D3D-G11: a framebuffer without attachments follows the viewports.
        ('''   pipe->set_viewport_states(pipe, 0, NumViewports + ClearViewports,
                             states);
}''','''   pipe->set_viewport_states(pipe, 0, NumViewports + ClearViewports,
                             states);
   Device *pDevice = CastDevice(hDevice);
   UINT extent[2] = {0u, 0u};
   for (UINT i = 0; i < NumViewports; ++i) {
      const float right = pViewports[i].TopLeftX + pViewports[i].Width;
      const float bottom = pViewports[i].TopLeftY + pViewports[i].Height;
      /* Rounded up, at most D3D11_REQ_RENDER_TO_BUFFER_WINDOW_WIDTH; NaN
       * and negative edges give 0. */
      const float edge[2] = {right, bottom};
      for (UINT c = 0; c < 2u; ++c) {
         if (!(edge[c] > 0.0f)) continue;
         UINT px = edge[c] >= 16384.0f ? 16384u : (UINT)edge[c];
         if (px < 16384u && (float)px < edge[c]) ++px;
         extent[c] = MAX2(extent[c], px);
      }
   }
   pDevice->agx_viewport_extent[0] = extent[0];
   pDevice->agx_viewport_extent[1] = extent[1];
   if (AgxD3d10NoAttachmentSize(pDevice))
      pipe->set_framebuffer_state(pipe, &pDevice->fb);
}''')])
    change('src/gallium/frontends/d3d10umd/OutputMerger.cpp',
        'fefcbe8754fd1042b7bf091feab767844cc71a8fe4cbe9f0b41d76dc9dd4fd04',[
        # EXP994: D3D10/11 disable depth and stencil tests while no depth-
        # stencil view is bound. Gallium leaves this to the frontend (st/mesa
        # does it for GL); without it the runtime default DepthEnable/LESS
        # state rejected every fragment against background depth 0.0.
        ('''void APIENTRY
SetRenderTargets(D3D10DDI_HDEVICE hDevice,''','''static void
AgxD3d10ApplyDepthStencil(Device *pDevice)
{
   struct pipe_context *pipe = pDevice->pipe;
   void *state = pDevice->app_dsa;
   if (!pDevice->fb.zsbuf.texture) {
      if (!pDevice->no_depth_dsa) {
         struct pipe_depth_stencil_alpha_state disabled;
         memset(&disabled, 0, sizeof disabled);
         pDevice->no_depth_dsa =
            pipe->create_depth_stencil_alpha_state(pipe, &disabled);
      }
      state = pDevice->no_depth_dsa;
   }
   pipe->bind_depth_stencil_alpha_state(pipe, state);
}

void APIENTRY
SetRenderTargets(D3D10DDI_HDEVICE hDevice,'''),
        ('''   pipe->set_framebuffer_state(pipe, &pDevice->fb);
}''','''   pDevice->agx_rt_args[0] = RTargets;
   pDevice->agx_rt_args[1] = 0u;
   pDevice->agx_rt_args[2] = 0u;
   for (unsigned i = 0; i < RTargets; ++i) {
      struct pipe_surface *view = CastPipeRenderTargetView(phRenderTargetView[i]);
      if (!view) ++pDevice->agx_rt_args[1];
      else if (!view->texture) ++pDevice->agx_rt_args[2];
   }
   pDevice->agx_rt_args[3] = CastPipeDepthStencilView(hDepthStencilView) != NULL;
   (void)AgxD3d10NoAttachmentSize(pDevice);
   pipe->set_framebuffer_state(pipe, &pDevice->fb);
   AgxD3d10ApplyDepthStencil(pDevice);
}'''),
        ('''   struct pipe_context *pipe = CastPipeContext(hDevice);
   void *state = CastPipeDepthStencilState(hState);
   struct pipe_stencil_ref psr;

   psr.ref_value[0] = StencilRef;
   psr.ref_value[1] = StencilRef;

   pipe->bind_depth_stencil_alpha_state(pipe, state);
   pipe->set_stencil_ref(pipe, psr);''','''   Device *pDevice = CastDevice(hDevice);
   struct pipe_context *pipe = CastPipeContext(hDevice);
   void *state = CastPipeDepthStencilState(hState);
   struct pipe_stencil_ref psr;

   psr.ref_value[0] = StencilRef;
   psr.ref_value[1] = StencilRef;

   pDevice->app_dsa = state;
   AgxD3d10ApplyDepthStencil(pDevice);
   pipe->set_stencil_ref(pipe, psr);'''),
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
        ('''   pRTView->surface = desc;
}''','''   pRTView->surface = desc;
   pRTView->owner_resource=NULL;
   pRTView->next_presentation=NULL;
   if(windowsResource->presentation && desc.texture) {
      pRTView->owner_resource=windowsResource;
      pRTView->next_presentation=windowsResource->first_presentation_rtv;
      windowsResource->first_presentation_rtv=pRTView;
   }
}'''),
        ('''   pipe_resource_reference(&pRTView->surface.texture, NULL);
}''','''   if(pRTView->owner_resource) {
      RenderTargetView **link=&pRTView->owner_resource->first_presentation_rtv;
      while(*link && *link!=pRTView) link=&(*link)->next_presentation;
      if(*link==pRTView) *link=pRTView->next_presentation;
      pRTView->owner_resource=NULL;
      pRTView->next_presentation=NULL;
   }
   pipe_resource_reference(&pRTView->surface.texture, NULL);
}'''),
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
   /* D3D-G10a: any render-target view -- any format the resource was
    * created in, any mip level, array slice, cube face or 3D slice range.
    * The native batch renders one layer, so a view of several slices is
    * cleared slice by slice. */
   if (!pDevice || !pipe->clear || !pipe->set_framebuffer_state ||
       !surface || !resource || resource->target == PIPE_BUFFER ||
       resource->nr_samples > 4 || surface->last_layer < surface->first_layer) {
      LOG_UNSUPPORTED("ClearRenderTargetView of a view outside the D3D10 contract");
      SetError(hDevice, E_NOTIMPL);
      return;
   }
   /* D3D clears the supplied view even when it is not bound for drawing.
    * Asahi clear uses a framebuffer-keyed batch, so supply that view only
    * for the clear and restore the application's complete binding state. */
   for (unsigned layer = surface->first_layer; layer <= surface->last_layer; ++layer) {
      struct pipe_framebuffer_state clear_fb = {};
      struct pipe_surface slice = *surface;
      slice.first_layer = slice.last_layer = layer;
      clear_fb.width = pipe_surface_width(&slice);
      clear_fb.height = pipe_surface_height(&slice);
      clear_fb.nr_cbufs = 1;
      clear_fb.cbufs[0] = slice;
      pipe->set_framebuffer_state(pipe, &clear_fb);
      pipe->clear(pipe, PIPE_CLEAR_COLOR0, 0xf, 0, NULL,
                  &clear_color, 0.0, 0);
   }
   pipe->set_framebuffer_state(pipe, &pDevice->fb);
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
   /* D3D-G4: D3D clears depth, stencil or both (stencil only where the
    * format has it) and clamps the depth value to [0, 1].
    * D3D-G10a: any depth-stencil view, bound or not (shadow maps are
    * cleared before they are bound): any depth format, mip level and slice
    * range, cleared slice by slice through a depth-only framebuffer. */
   unsigned clear_flags =
      ((Flags & D3D10_DDI_CLEAR_DEPTH) ? PIPE_CLEAR_DEPTH : 0u) |
      (resource && util_format_has_stencil(util_format_description(resource->format)) &&
       (Flags & D3D10_DDI_CLEAR_STENCIL) ? PIPE_CLEAR_STENCIL : 0u);
   if (!(Depth >= 0.0f)) Depth = 0.0f;
   if (Depth > 1.0f) Depth = 1.0f;
   if (!pDevice || !pipe || !pipe->clear || !pipe->set_framebuffer_state ||
       !surface || !resource || resource->target == PIPE_BUFFER ||
       (Flags & ~(UINT)(D3D10_DDI_CLEAR_DEPTH | D3D10_DDI_CLEAR_STENCIL)) ||
       !util_format_is_depth_or_stencil(resource->format) ||
       resource->nr_samples > 4 || surface->last_layer < surface->first_layer) {
      LOG_UNSUPPORTED("ClearDepthStencilView of a view outside the D3D10 contract");
      SetError(hDevice, E_NOTIMPL);
      return;
   }
   if (!clear_flags)
      return;
   union pipe_color_union color;
   memset(&color, 0, sizeof(color));
   /* The view bound whole as the framebuffer's depth target clears in the
    * current batch (no extra render pass, the EXP1284 path). */
   const struct pipe_surface *bound = &pDevice->fb.zsbuf;
   if (bound->texture == resource && bound->format == surface->format &&
       bound->level == surface->level &&
       bound->first_layer == surface->first_layer &&
       bound->last_layer == surface->last_layer &&
       surface->first_layer == surface->last_layer &&
       pDevice->fb.width == pipe_surface_width(surface) &&
       pDevice->fb.height == pipe_surface_height(surface)) {
      pipe->clear(pipe, clear_flags, 0, 0, NULL, &color, Depth, Stencil);
      return;
   }
   for (unsigned layer = surface->first_layer; layer <= surface->last_layer; ++layer) {
      struct pipe_framebuffer_state clear_fb = {};
      struct pipe_surface slice = *surface;
      slice.first_layer = slice.last_layer = layer;
      clear_fb.width = pipe_surface_width(&slice);
      clear_fb.height = pipe_surface_height(&slice);
      clear_fb.zsbuf = slice;
      pipe->set_framebuffer_state(pipe, &clear_fb);
      pipe->clear(pipe, clear_flags, 0, 0, NULL, &color, Depth, Stencil);
   }
   pipe->set_framebuffer_state(pipe, &pDevice->fb);''')])
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
   /* D3D-G3: the ISP counts samples into the occlusion heap the KMD names
    * as isp_oclqry_base; a predicate may carry the PREDICATEHINT flag.
    * D3D-G10d: pipeline statistics, stream-output statistics and the
    * stream-overflow predicate are Asahi queries too (single statistics,
    * primitives emitted/generated, SO overflow).  D3D-H5: timestamp and
    * timestamp-disjoint queries complete on the event fence (GetData). */
   const bool occlusion = pCreateQuery &&
      (pCreateQuery->Query == D3D10DDI_QUERY_OCCLUSION ||
       pCreateQuery->Query == D3D10DDI_QUERY_OCCLUSIONPREDICATE);
   /* EXP1336: a D3D 11 (FL 11_0) device asks for D3D11DDI_QUERY_PIPELINESTATS
    * (the eleven statistics) and the per-stream stream-output statistics and
    * overflow predicates; refused, the runtime removed the device. */
   const bool streamStats = pCreateQuery &&
      pCreateQuery->Query >= D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM0 &&
      pCreateQuery->Query <= D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM3;
   const bool streamOverflow = pCreateQuery &&
      pCreateQuery->Query >= D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM0 &&
      pCreateQuery->Query <= D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM3;
   const bool statistics = pCreateQuery &&
      (pCreateQuery->Query == D3D10DDI_QUERY_PIPELINESTATS ||
       pCreateQuery->Query == D3D11DDI_QUERY_PIPELINESTATS ||
       pCreateQuery->Query == D3D10DDI_QUERY_STREAMOUTPUTSTATS ||
       pCreateQuery->Query == D3D10DDI_QUERY_STREAMOVERFLOWPREDICATE ||
       streamStats || streamOverflow);
   const bool predicate = pCreateQuery &&
      (pCreateQuery->Query == D3D10DDI_QUERY_OCCLUSIONPREDICATE ||
       pCreateQuery->Query == D3D10DDI_QUERY_STREAMOVERFLOWPREDICATE ||
       streamOverflow);
   if (!pCreateQuery || !pQuery ||
       (pCreateQuery->Query != D3D10DDI_QUERY_EVENT && !occlusion && !statistics &&
        pCreateQuery->Query != D3D10DDI_QUERY_TIMESTAMP &&
        pCreateQuery->Query != D3D10DDI_QUERY_TIMESTAMPDISJOINT) ||
       (pCreateQuery->MiscFlags &&
        !(predicate &&
          pCreateQuery->MiscFlags == D3D10DDI_QUERY_MISCFLAG_PREDICATEHINT))) {
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
   candidate.Type = pCreateQuery->Query;
   candidate.Flags = pCreateQuery->MiscFlags;
   if (occlusion) {
      candidate.handle = pDevice->pipe->create_query(pDevice->pipe,
         pCreateQuery->Query == D3D10DDI_QUERY_OCCLUSION ?
            PIPE_QUERY_OCCLUSION_COUNTER : PIPE_QUERY_OCCLUSION_PREDICATE, 0);
      if (!candidate.handle) {
         SetError(hDevice, E_OUTOFMEMORY);
         return;
      }
   } else if (statistics) {
      struct pipe_query *queries[11] = {};
      unsigned count = 0;
      bool made = true;
      if (pCreateQuery->Query == D3D10DDI_QUERY_PIPELINESTATS ||
          pCreateQuery->Query == D3D11DDI_QUERY_PIPELINESTATS) {
         /* PIPE_STAT_QUERY_IA_VERTICES..PS_INVOCATIONS (then HS, DS, CS
          * invocations): the order of D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS
          * and D3D11_DDI_QUERY_DATA_PIPELINE_STATISTICS. */
         const unsigned stats =
            pCreateQuery->Query == D3D11DDI_QUERY_PIPELINESTATS ? 11u : 8u;
         for (unsigned i = 0; made && i < stats; ++i)
            made = (queries[count++] = pDevice->pipe->create_query(pDevice->pipe,
               PIPE_QUERY_PIPELINE_STATISTICS_SINGLE, i)) != NULL;
      } else if (pCreateQuery->Query == D3D10DDI_QUERY_STREAMOUTPUTSTATS ||
                 streamStats) {
         const unsigned stream = streamStats ? (unsigned)(pCreateQuery->Query -
            D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM0) : 0u;
         made = (queries[count++] = pDevice->pipe->create_query(pDevice->pipe,
            PIPE_QUERY_PRIMITIVES_EMITTED, stream)) != NULL &&
            (queries[count++] = pDevice->pipe->create_query(pDevice->pipe,
            PIPE_QUERY_PRIMITIVES_GENERATED, stream)) != NULL;
      } else {
         const unsigned stream = streamOverflow ? (unsigned)(pCreateQuery->Query -
            D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM0) : 0u;
         made = (queries[count++] = pDevice->pipe->create_query(pDevice->pipe,
            PIPE_QUERY_SO_OVERFLOW_PREDICATE, stream)) != NULL;
      }
      if (!made) {
         for (unsigned i = 0; i < count; ++i)
            if (queries[i]) pDevice->pipe->destroy_query(pDevice->pipe, queries[i]);
         SetError(hDevice, E_OUTOFMEMORY);
         return;
      }
      candidate.handle = queries[0];
      for (unsigned i = 1; i < count; ++i)
         candidate.extra[candidate.extra_count++] = queries[i];
   }
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
   if (pQuery->handle) {
      if (pDevice->pPredicate == pQuery) {
         pDevice->pipe->render_condition(pDevice->pipe, NULL, false,
                                         PIPE_RENDER_COND_WAIT);
         pDevice->pPredicate = NULL;
      }
      /* agx_destroy_query waits for the batches writing its heap slot. */
      pDevice->pipe->destroy_query(pDevice->pipe, pQuery->handle);
      pQuery->handle = NULL;
      for (UINT i = 0; i < pQuery->extra_count; ++i)
         pDevice->pipe->destroy_query(pDevice->pipe, pQuery->extra[i]);
      pQuery->extra_count = 0;
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
   if (pQuery->handle) {
      /* agx_begin_query makes it the context's occlusion query and zeroes
       * its heap slot once the batches still writing it completed. */
      bool begun = pDevice->pipe->begin_query(pDevice->pipe, pQuery->handle);
      for (UINT i = 0; begun && i < pQuery->extra_count; ++i)
         begun = pDevice->pipe->begin_query(pDevice->pipe, pQuery->extra[i]);
      if (!begun) {
         pQuery->Phase = QueryFailed;
         pQuery->LastError = E_FAIL;
         SetError(hDevice, E_FAIL);
         return;
      }
      pQuery->Phase = QueryBuilding;
      pQuery->LastError = S_OK;
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
   if (pQuery->handle) {
      /* Only the query this one began; the draws since carry its index. */
      if (pQuery->Phase != QueryBuilding) return;
      bool ended = pDevice->pipe->end_query(pDevice->pipe, pQuery->handle);
      for (UINT i = 0; ended && i < pQuery->extra_count; ++i)
         ended = pDevice->pipe->end_query(pDevice->pipe, pQuery->extra[i]);
      if (!ended) {
         pQuery->Phase = QueryFailed;
         pQuery->LastError = E_FAIL;
         SetError(hDevice, E_FAIL);
         return;
      }
      pQuery->Phase = QueryIssued;
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
   /* D3D-H5: timestamp queries complete on the event fence.  No GPU
    * timestamps reach this UMD yet, so a timestamp is the CPU counter when
    * its fence was seen complete and every disjoint query reports TRUE:
    * D3D documents timestamps of a disjoint interval as unreliable, which
    * applications check before using them. */
   auto event_data = [&](void *out) {
      if (!out) return;
      if (pQuery->Type == D3D10DDI_QUERY_TIMESTAMP) {
         *(UINT64 *)out = pQuery->agx_timestamp;
      } else if (pQuery->Type == D3D10DDI_QUERY_TIMESTAMPDISJOINT) {
         LARGE_INTEGER frequency;
         QueryPerformanceFrequency(&frequency);
         D3D10_DDI_QUERY_DATA_TIMESTAMP_DISJOINT *disjoint =
            (D3D10_DDI_QUERY_DATA_TIMESTAMP_DISJOINT *)out;
         disjoint->Frequency = (UINT64)frequency.QuadPart;
         disjoint->Disjoint = TRUE;
      } else {
         *(BOOL *)out = TRUE;
      }
   };
   const UINT dataBytes = !pQuery ? sizeof(BOOL) :
      !pQuery->handle ?
         (pQuery->Type == D3D10DDI_QUERY_TIMESTAMP ? sizeof(UINT64) :
          pQuery->Type == D3D10DDI_QUERY_TIMESTAMPDISJOINT ?
             sizeof(D3D10_DDI_QUERY_DATA_TIMESTAMP_DISJOINT) : sizeof(BOOL)) :
      pQuery->Type == D3D10DDI_QUERY_OCCLUSION ? sizeof(UINT64) :
      pQuery->Type == D3D10DDI_QUERY_PIPELINESTATS ?
         sizeof(D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS) :
      pQuery->Type == D3D11DDI_QUERY_PIPELINESTATS ?
         sizeof(D3D11_DDI_QUERY_DATA_PIPELINE_STATISTICS) :
      pQuery->Type == D3D10DDI_QUERY_STREAMOUTPUTSTATS ||
      (pQuery->Type >= D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM0 &&
       pQuery->Type <= D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM3) ?
         sizeof(D3D10_DDI_QUERY_DATA_SO_STATISTICS) : sizeof(BOOL);
   if (!EventQueryValid(pDevice, pQuery) ||
       !((pData == NULL && DataSize == 0) ||
         (pData != NULL && DataSize == dataBytes)) ||
       (Flags & ~D3D10_DDI_GET_DATA_DO_NOT_FLUSH)) {
      SetError(hDevice, E_INVALIDARG);
      return;
   }
   if (pQuery->handle) {
      if (pQuery->Phase == QueryFailed) {
         SetError(hDevice, FAILED(pQuery->LastError) ? pQuery->LastError : E_FAIL);
         return;
      }
      if (pQuery->Phase != QueryIssued && pQuery->Phase != QuerySignaled) {
         SetError(hDevice, E_INVALIDARG);
         return;
      }
      /* The native agx_get_query_result submits the query's writers and,
       * without wait, reports not-ready until they completed. Submitting
       * under DO_NOT_FLUSH too keeps a polling caller from never finishing. */
      union pipe_query_result value, values[11];
      memset(&value, 0, sizeof(value));
      memset(values, 0, sizeof(values));
      bool ready = pDevice->pipe->get_query_result(pDevice->pipe,
         pQuery->handle, false, &value);
      values[0] = value;
      /* D3D-G10d: every part of a composite query must be ready. */
      for (UINT i = 0; ready && i < pQuery->extra_count; ++i)
         ready = pDevice->pipe->get_query_result(pDevice->pipe,
            pQuery->extra[i], false, &values[i + 1]);
      HRESULT status = AgxD3d10WindowsFlushStatus(pDevice->windows);
      if (FAILED(status)) {
         pQuery->Phase = QueryFailed;
         pQuery->LastError = status;
         SetError(hDevice, status);
         return;
      }
      if (!ready) {
         SetError(hDevice, DXGI_DDI_ERR_WASSTILLDRAWING);
         return;
      }
      if (pData && pQuery->Type == D3D10DDI_QUERY_OCCLUSION) {
         *(UINT64 *)pData = value.u64;
      } else if (pData && pQuery->Type == D3D10DDI_QUERY_PIPELINESTATS) {
         D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS *stats =
            (D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS *)pData;
         stats->IAVertices = values[0].u64; stats->IAPrimitives = values[1].u64;
         stats->VSInvocations = values[2].u64; stats->GSInvocations = values[3].u64;
         stats->GSPrimitives = values[4].u64; stats->CInvocations = values[5].u64;
         stats->CPrimitives = values[6].u64; stats->PSInvocations = values[7].u64;
      } else if (pData && pQuery->Type == D3D11DDI_QUERY_PIPELINESTATS) {
         D3D11_DDI_QUERY_DATA_PIPELINE_STATISTICS *stats =
            (D3D11_DDI_QUERY_DATA_PIPELINE_STATISTICS *)pData;
         stats->IAVertices = values[0].u64; stats->IAPrimitives = values[1].u64;
         stats->VSInvocations = values[2].u64; stats->GSInvocations = values[3].u64;
         stats->GSPrimitives = values[4].u64; stats->CInvocations = values[5].u64;
         stats->CPrimitives = values[6].u64; stats->PSInvocations = values[7].u64;
         stats->HSInvocations = values[8].u64; stats->DSInvocations = values[9].u64;
         stats->CSInvocations = values[10].u64;
      } else if (pData && (pQuery->Type == D3D10DDI_QUERY_STREAMOUTPUTSTATS ||
                           (pQuery->Type >= D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM0 &&
                            pQuery->Type <= D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM3))) {
         D3D10_DDI_QUERY_DATA_SO_STATISTICS *so =
            (D3D10_DDI_QUERY_DATA_SO_STATISTICS *)pData;
         so->NumPrimitivesWritten = values[0].u64;
         so->PrimitivesStorageNeeded = values[1].u64;
      } else if (pData) {
         *(BOOL *)pData = value.b ? TRUE : FALSE;
      }
      pQuery->Phase = QuerySignaled;
      return;
   }
   if (pQuery->Phase == QuerySignaled) {
      event_data(pData);
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
   {
      LARGE_INTEGER now;
      QueryPerformanceCounter(&now);
      pQuery->agx_timestamp = (UINT64)now.QuadPart;
   }
   event_data(pData);''')
    replace_function_body('src/gallium/frontends/d3d10umd/Query.cpp','SetPredication','''   Device *device = CastDevice(hDevice);
   Query *query = CastQuery(hQuery);
   /* D3D-G3b: an occlusion predicate gates draws and clears through Mesa's
    * render condition (checked on the CPU at each one) and copies, updates
    * and mip generation through CheckPredicate. With PREDICATEHINT an
    * operation runs while the result is not yet available. A NULL predicate
    * ends predication; its value is kept as the runtime's. */
   /* D3D-G10d: the stream-overflow predicate predicates too. */
   if (!device || (query && (!EventQueryValid(device, query) ||
       !query->handle || (query->Type != D3D10DDI_QUERY_OCCLUSIONPREDICATE &&
                          query->Type != D3D10DDI_QUERY_STREAMOVERFLOWPREDICATE &&
                          (query->Type < D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM0 ||
                           query->Type > D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM3))))) {
      SetError(hDevice, E_NOTIMPL); return;
   }
   device->pipe->render_condition(device->pipe, query ? query->handle : NULL,
      PredicateValue ? true : false,
      query && (query->Flags & D3D10DDI_QUERY_MISCFLAG_PREDICATEHINT) ?
         PIPE_RENDER_COND_NO_WAIT : PIPE_RENDER_COND_WAIT);
   device->pPredicate = query;
   device->PredicateValue = PredicateValue;''')
    replace_function_body('src/gallium/frontends/d3d10umd/Query.cpp','CheckPredicate','''   /* D3D: an operation is skipped when the predicate's data equals the
    * predicate value; a hinted predicate that is not ready lets it run. */
   if (!pDevice) return FALSE;
   Query *query = pDevice->pPredicate;
   if (!query || !query->handle) return TRUE;
   union pipe_query_result result;
   memset(&result, 0, sizeof(result));
   bool hint = (query->Flags & D3D10DDI_QUERY_MISCFLAG_PREDICATEHINT) != 0;
   if (!pDevice->pipe->get_query_result(pDevice->pipe, query->handle, !hint,
                                        &result))
      return TRUE;
   return !!result.b != !!pDevice->PredicateValue;''')
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
   if (!device || !device->pipe || !device->windows)
      return E_INVALIDARG;
   AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE **records =
      (AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE **)HeapAlloc(
         GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*records) * count);
   if (!records) return E_OUTOFMEMORY;
   HRESULT result = S_OK;
   UINT viewCount=0;
   for (UINT i = 0; i < count; ++i) {
      Resource *resource = CastResource(RotateResourceIdentities->pResources[i]);
      if (!resource || resource->owner_device != device ||
          !resource->presentation || !resource->resource) {
         result = E_INVALIDARG;
         break;
      }
      for(UINT j=0;j<i;++j)
         if(RotateResourceIdentities->pResources[j]==
            RotateResourceIdentities->pResources[i]) result=E_INVALIDARG;
      if(FAILED(result)) break;
      records[i] = resource->presentation;
      for(RenderTargetView *view=resource->first_presentation_rtv;
          view;view=view->next_presentation)
         if(view->owner_resource!=resource ||
            view->surface.texture!=resource->resource) result=E_INVALIDARG;
      for(ShaderResourceView *view=resource->first_presentation_srv;
          view;view=view->next_presentation) {
         if(view->owner_resource!=resource || view->owner_device!=device ||
            !view->handle || view->handle->texture!=resource->resource ||
            viewCount==~0u) result=E_INVALIDARG;
         else ++viewCount;
      }
      if(FAILED(result)) break;
   }
   struct PreparedView {
      ShaderResourceView *View;
      struct pipe_sampler_view *Old;
      struct pipe_sampler_view *New;
   };
   PreparedView *prepared=NULL;
   UINT preparedCount=0;
   if(SUCCEEDED(result) && viewCount) {
      prepared=(PreparedView *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,
                                        (SIZE_T)viewCount*sizeof(*prepared));
      if(!prepared) result=E_OUTOFMEMORY;
   }
   for(UINT i=0;SUCCEEDED(result) && i<count;++i) {
      Resource *resource=CastResource(RotateResourceIdentities->pResources[i]);
      Resource *next=CastResource(RotateResourceIdentities->pResources[(i+1u)%count]);
      for(ShaderResourceView *view=resource->first_presentation_srv;
          view;view=view->next_presentation) {
         struct pipe_sampler_view *candidate=device->pipe->create_sampler_view(
            device->pipe,next->resource,view->handle);
         if(!candidate) { result=E_OUTOFMEMORY;break; }
         prepared[preparedCount++]={view,view->handle,candidate};
      }
   }
   if(SUCCEEDED(result)) result=AgxD3d10WindowsPresentationRotate(
      device->windows,records,count);
   if (SUCCEEDED(result)) {
      bool framebufferChanged=false;
      for(UINT slot=0;slot<device->fb.nr_cbufs;++slot) {
         struct pipe_resource *old=device->fb.cbufs[slot].texture;
         for(UINT i=0;i<count;++i) {
            Resource *resource=CastResource(RotateResourceIdentities->pResources[i]);
            if(old==resource->resource) {
               pipe_resource_reference(&device->fb.cbufs[slot].texture,
                  AgxD3d10WindowsPresentationPipeResource(records[i]));
               framebufferChanged=true;
               break;
            }
         }
      }
      for(UINT i=0;i<count;++i) {
         Resource *resource=CastResource(RotateResourceIdentities->pResources[i]);
         struct pipe_resource *target=
            AgxD3d10WindowsPresentationPipeResource(records[i]);
         for(RenderTargetView *view=resource->first_presentation_rtv;
             view;view=view->next_presentation)
            pipe_resource_reference(&view->surface.texture,target);
         resource->resource=target;
      }
      if(framebufferChanged)
         device->pipe->set_framebuffer_state(device->pipe,&device->fb);
      bool samplerChanged[MESA_SHADER_STAGES]={};
      for(UINT i=0;i<preparedCount;++i) {
         for(UINT stage=0;stage<MESA_SHADER_STAGES;++stage)
            for(UINT slot=0;slot<PIPE_MAX_SHADER_SAMPLER_VIEWS;++slot)
               if(device->sampler_views[stage][slot]==prepared[i].Old) {
                  device->sampler_views[stage][slot]=prepared[i].New;
                  samplerChanged[stage]=true;
               }
         prepared[i].View->handle=prepared[i].New;
      }
      for(UINT stage=0;stage<MESA_SHADER_STAGES;++stage)
         if(samplerChanged[stage])
            device->pipe->set_sampler_views(device->pipe,
               (enum mesa_shader_stage)stage,0,
               PIPE_MAX_SHADER_SAMPLER_VIEWS,0,device->sampler_views[stage]);
      for(UINT i=0;i<preparedCount;++i)
         device->pipe->sampler_view_release(device->pipe,prepared[i].Old);
   } else {
      for(UINT i=0;i<preparedCount;++i)
         device->pipe->sampler_view_release(device->pipe,prepared[i].New);
   }
   if(prepared) HeapFree(GetProcessHeap(),0,prepared);
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
    dxgi_text = dxgi_text.replace(dxgi_marker, '''#include <string.h>

static HRESULT
AgxDxgiTraceReturn(const char *name, HRESULT result)
{
   if (!strcmp(name,"_Present"))
      AgxD3d10WindowsPresentMeasure(AdmissionUmdMeasureNativePresentReturn,result,NULL,0u);
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
        if name == '_Present':
            entry += '''   UINT presentMeasure[10] = {
      pPresentData ? pPresentData->Flags.Value : ~0u,
      pPresentData ? (UINT)pPresentData->FlipInterval : ~0u,
      pPresentData ? pPresentData->SrcSubResourceIndex : ~0u,
      pPresentData && pPresentData->hDstResource != 0,
      pPresentData ? (UINT)(UINT_PTR)pPresentData->hSurfaceToPresent : 0u,
      pPresentData ? (UINT)((UINT64)(UINT_PTR)pPresentData->hSurfaceToPresent>>32) : 0u,
      pPresentData ? (UINT)(UINT_PTR)pPresentData->hDevice : 0u,
      pPresentData ? (UINT)((UINT64)(UINT_PTR)pPresentData->hDevice>>32) : 0u,
      pPresentData ? (UINT)(UINT_PTR)pPresentData->pDXGIContext : 0u,
      pPresentData ? (UINT)((UINT64)(UINT_PTR)pPresentData->pDXGIContext>>32) : 0u};
   AgxD3d10WindowsPresentMeasure(AdmissionUmdMeasureNativePresentEntry,
      pPresentData?S_OK:E_INVALIDARG,presentMeasure,10u);
'''
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
            entry = '   AgxD3d10WindowsPresentMeasure(AdmissionUmdMeasureNativeBltEntry,S_OK,NULL,0u);\n' + entry
            entry = entry.replace('   AgxD3d10WindowsDiagnostic("dxgi-blt-args", S_OK, values, 11u);',
                '   auto rejectBlt = [&](HRESULT status) { AgxD3d10WindowsPresentMeasure(AdmissionUmdMeasureNativeBltReturn,status,values,11u); if (FAILED(status)) AgxD3d10WindowsDiagnostic("reject-BltDXGI",status,values,11u); return status; };')
        replace_function_body(dxgi_path, name, entry + body)

    change('src/gallium/frontends/d3d10umd/Shader.cpp',
        '48a7de2a42b25abac677cd903c10f21fc91b86aef167266a32a6d7d9da21097c',[
        ('''   if (pShader->handle) {
      switch (pShader->type) {
      case MESA_SHADER_FRAGMENT:''','''   /* D3D-G10e: the stream-output VS variant of a GS without code. */
   if (pShader->so_vs_handle) {
      pipe->delete_vs_state(pipe, pShader->so_vs_handle);
      pShader->so_vs_handle = NULL;
   }
   /* D3D-H5: an SM 5 vertex shader's tokens */
   if (pShader->type == MESA_SHADER_VERTEX) {
      free(pShader->agx_code);
      pShader->agx_code = NULL;
   }
   if (pShader->handle) {
      switch (pShader->type) {
      case MESA_SHADER_FRAGMENT:'''),
        # D3D-H2b: compute shaders (agx_d3d11_ddi.cpp CreateComputeShader11)
        ('''      case MESA_SHADER_GEOMETRY:
         pipe->delete_gs_state(pipe, pShader->handle);
         break;''','''      case MESA_SHADER_GEOMETRY:
         pipe->delete_gs_state(pipe, pShader->handle);
         break;
      case MESA_SHADER_COMPUTE:
         pipe->delete_compute_state(pipe, pShader->handle);
         break;
      case MESA_SHADER_TESS_CTRL:
         pipe->delete_tcs_state(pipe, pShader->handle);
         break;
      case MESA_SHADER_TESS_EVAL:
         pipe->delete_tes_state(pipe, pShader->handle);
         break;'''),
        # D3D-H2b: the compute stage's common-shader setters for the D3D11
        # table (declared in agx_d3d11_ddi.h)
        ('''/*
 * ----------------------------------------------------------------------
 *
 * CalcPrivateShaderSize --''','''/* D3D-H7: constant buffers with optional 11.1 windows (agx_d3d11_ddi.h) */
void
AgxSetConstantBuffersRange(mesa_shader_stage shader_type, D3D10DDI_HDEVICE hDevice,
                           UINT StartBuffer, UINT NumBuffers,
                           const D3D10DDI_HRESOURCE *phBuffers,
                           const UINT *firstConstant, const UINT *numConstants)
{
   Device *pDevice = CastDevice(hDevice);
   ULONGLONG owner = 0;
   ULONG generation = 0;
   const UINT slots=D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
   static_assert(PIPE_MAX_CONSTANT_BUFFERS>=
                 D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
                 "D3D10 constant buffers must fit the native array");
   bool valid = pDevice && pDevice->pipe &&
      (shader_type == MESA_SHADER_VERTEX ||
       shader_type == MESA_SHADER_FRAGMENT ||
       shader_type == MESA_SHADER_GEOMETRY ||
       shader_type == MESA_SHADER_COMPUTE ||
       shader_type == MESA_SHADER_TESS_CTRL || shader_type == MESA_SHADER_TESS_EVAL) &&
      (NumBuffers == 0 || phBuffers) && StartBuffer <= slots &&
      NumBuffers <= slots - StartBuffer &&
      AgxD3d10WindowsIdentity(pDevice->windows, &owner, &generation);
   for (UINT i = 0; valid && i < NumBuffers; ++i) {
      Resource *resource = CastResource(phBuffers[i]);
      if (!resource) continue;
      valid = resource->constant_buffer && resource->owner_device == pDevice &&
         resource->owner_cookie == owner &&
         resource->device_generation == generation && resource->resource &&
         resource->resource->target == PIPE_BUFFER &&
         (resource->resource->bind & PIPE_BIND_CONSTANT_BUFFER) &&
         !(resource->resource->bind &
           ~(PIPE_BIND_CONSTANT_BUFFER | PIPE_BIND_SHADER_IMAGE)) &&
         resource->logical_bytes >= 16 &&
         (firstConstant || resource->logical_bytes <= 65536) &&
         (resource->logical_bytes & 15) == 0 &&
         resource->resource->width0 == resource->logical_bytes;
   }
   for (UINT i = 0; valid && firstConstant && numConstants && i < NumBuffers; ++i)
      valid = (firstConstant[i] & 15u) == 0 && (numConstants[i] & 15u) == 0 &&
              numConstants[i] >= 16u && numConstants[i] <= 4096u;
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
      /* D3D11.1: the window starts at a multiple of 16 constants and
       * holds 16..4096 of them (the runtime validates both); reads past
       * the buffer's end return 0 (robust context), so the bound size is
       * clipped to the buffer and an empty window unbinds the slot. */
      UINT offset = firstConstant ? firstConstant[i] * 16u : 0u;
      UINT window = numConstants ? numConstants[i] * 16u : 65536u;
      if (offset >= resource->logical_bytes) {
         pDevice->pipe->set_constant_buffer(pDevice->pipe, shader_type,
                                             StartBuffer + i, NULL);
         continue;
      }
      struct pipe_constant_buffer cb = {};
      cb.buffer = resource->resource;
      cb.buffer_offset = offset;
      cb.buffer_size = MIN2(window, resource->logical_bytes - offset);
      pDevice->pipe->set_constant_buffer(pDevice->pipe, shader_type,
                                          StartBuffer + i, &cb);
   }
}

void APIENTRY
CsSetShaderResources(D3D10DDI_HDEVICE hDevice, UINT Offset, UINT NumViews,
                     const D3D10DDI_HSHADERRESOURCEVIEW *phShaderResourceViews)
{
   LOG_ENTRYPOINT();
   SetShaderResources(MESA_SHADER_COMPUTE, hDevice, Offset, NumViews, phShaderResourceViews);
}

void APIENTRY
CsSetConstantBuffers(D3D10DDI_HDEVICE hDevice, UINT StartBuffer, UINT NumBuffers,
                     const D3D10DDI_HRESOURCE *phBuffers)
{
   LOG_ENTRYPOINT();
   SetConstantBuffers(MESA_SHADER_COMPUTE, hDevice, StartBuffer, NumBuffers, phBuffers);
}

void APIENTRY
CsSetSamplers(D3D10DDI_HDEVICE hDevice, UINT Offset, UINT NumSamplers,
              const D3D10DDI_HSAMPLER *phSamplers)
{
   LOG_ENTRYPOINT();
   SetSamplers(MESA_SHADER_COMPUTE, hDevice, Offset, NumSamplers, phSamplers);
}

/* D3D-H4: the hull and domain stages' common-shader setters */
#define AGX_D3D11_STAGE_SETTERS(prefix, stage)                                       \
   void APIENTRY prefix##SetShaderResources(D3D10DDI_HDEVICE hDevice, UINT Offset,  \
      UINT NumViews, const D3D10DDI_HSHADERRESOURCEVIEW *phViews)                    \
   {                                                                                 \
      SetShaderResources(stage, hDevice, Offset, NumViews, phViews);                 \
   }                                                                                 \
   void APIENTRY prefix##SetConstantBuffers(D3D10DDI_HDEVICE hDevice, UINT Start,   \
      UINT NumBuffers, const D3D10DDI_HRESOURCE *phBuffers)                          \
   {                                                                                 \
      SetConstantBuffers(stage, hDevice, Start, NumBuffers, phBuffers);              \
   }                                                                                 \
   void APIENTRY prefix##SetSamplers(D3D10DDI_HDEVICE hDevice, UINT Offset,         \
      UINT NumSamplers, const D3D10DDI_HSAMPLER *phSamplers)                         \
   {                                                                                 \
      SetSamplers(stage, hDevice, Offset, NumSamplers, phSamplers);                  \
   }
AGX_D3D11_STAGE_SETTERS(Hs, MESA_SHADER_TESS_CTRL)
AGX_D3D11_STAGE_SETTERS(Ds, MESA_SHADER_TESS_EVAL)


/*
 * ----------------------------------------------------------------------
 *
 * CalcPrivateShaderSize --'''),
        ('''         pShader->state.stream_output.stride[i] = total_components[i];
      }
   }

   pShader->handle = pipe->create_gs_state(pipe, &pShader->state);
}''','''         pShader->state.stream_output.stride[i] = total_components[i];
      }
   }

   /* D3D-G10e: without geometry shader code no GS state exists; the draw
    * binds a vertex-shader variant carrying this stream output instead. */
   pShader->so_vs_handle = NULL;
   pShader->so_vs_serial = 0;
   /* D3D-H5: SM 5 code has no TGSI record; the translator captures. */
   pShader->handle = pShader->state.tokens ?
      pipe->create_gs_state(pipe, &pShader->state) :
      AgxD3d10TranslatorOnly(pData->pShaderCode) ?
      AgxD3d10CreateXfbState(pipe, pShader, pData->pShaderCode,
                             &pShader->state.stream_output) : NULL;
   if (pData->pShaderCode && !pShader->handle)
      SetError(hDevice, E_INVALIDARG);
}'''),
        ('''   if (pShader && !pShader->state.tokens) {
      pDevice->bound_empty_gs = pShader;
   } else {
      pDevice->bound_empty_gs = NULL;
      pipe->bind_gs_state(pipe, state);
   }''','''   /* D3D-G10e: leaving a stream output without GS code rebinds the plain
    * vertex shader (the draw bound its stream-output variant). */
   if (pDevice->bound_empty_gs && pShader != pDevice->bound_empty_gs)
      pipe->bind_vs_state(pipe, pDevice->bound_vs && pDevice->bound_vs->handle ?
         pDevice->bound_vs->handle : pDevice->empty_vs);
   if (pShader && !pShader->state.tokens && !pShader->handle) {
      pDevice->bound_empty_gs = pShader;
   } else {
      pDevice->bound_empty_gs = NULL;
      pipe->bind_gs_state(pipe, state);
   }'''),
        ('''   pDevice->bound_vs = pShader;
   if (!state) {''','''   pDevice->bound_vs = pShader;
   if (pShader && (pShader->state.tokens || pShader->agx_code) &&
       !pShader->agx_serial)
      pShader->agx_serial = ++pDevice->agx_shader_serial;
   if (!state) {'''),
        ('#include "util/u_gen_mipmap.h"', '#include "util/u_gen_mipmap.h"\n#include "drm-uapi/drm_fourcc.h"'),
        ('''{
   unsigned i;

   LOG_ENTRYPOINT();

   Device *pDevice = CastDevice(hDevice);
   struct pipe_context *pipe = pDevice->pipe;

   assert(SOTargets + ClearTargets <= PIPE_MAX_SO_BUFFERS);''','''{
   unsigned i;
   Device *windows_device=CastDevice(hDevice);
   if(!windows_device) { SetError(hDevice,E_INVALIDARG); return; }
   /* EXP1287: the D3D10 DDI contract -- at most four slots
    * (D3D10_SO_BUFFER_SLOT_COUNT); a NULL handle unbinds its slot; every
    * bound buffer belongs to this device and was created with the
    * stream-output bind flag (any other bind flags too); an offset is a
    * 4-byte-aligned byte offset or ~0u, append (Gallium's -1, which Asahi
    * agx_set_stream_output_targets honours). The EXP-era guard admitted
    * only one VERTEX_BUFFER|STREAM_OUTPUT buffer at offset 0. */
   if(SOTargets+ClearTargets>4u||(SOTargets&&(!phResource||!pOffsets))) {
      SetError(hDevice,E_INVALIDARG); return;
   }
   for(i=0;i<SOTargets;++i) {
      Resource *windows_resource=CastResource(phResource[i]);
      if(!windows_resource) continue;
      if(windows_resource->owner_device!=windows_device||
         !(windows_resource->bind_flags&D3D10_DDI_BIND_STREAM_OUTPUT)||
         !windows_resource->resource||
         windows_resource->resource->target!=PIPE_BUFFER||
         (pOffsets[i]!=~0u&&(pOffsets[i]&3u))) {
         SetError(hDevice,E_INVALIDARG); return;
      }
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
   /* D3D-G8: an FL10_0 declaration -- up to 64 components, slots 0..3,
    * contiguous component masks, 0xffffffff register holes; one slot is
    * interleaved at StreamOutputStrideInBytes, several slots are tightly
    * packed (upstream). D3D-G10e: without geometry shader code the
    * declaration names vertex-shader outputs (stream output from the VS). */
   bool soValid=windows_device&&pData&&
      pData->pOutputStreamDecl&&pData->NumEntries>=1u&&
      pData->NumEntries<=64u; /* D3D10_SO_SINGLE_BUFFER_COMPONENT_LIMIT */
   UINT soSlots=0u,soComponents=0u;
   for(UINT i=0;soValid&&i<pData->NumEntries;++i) {
      const D3D10DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY *entry=
         &pData->pOutputStreamDecl[i];
      UINT mask=entry->RegisterMask,low=0u;
      while(mask&&!(mask&1u)){mask>>=1;++low;}
      soValid=entry->OutputSlot<4u&& /* D3D10_SO_BUFFER_SLOT_COUNT */
         entry->RegisterMask&&!(entry->RegisterMask&~0xfu)&&
         !(mask&(mask+1u));
      soSlots|=1u<<entry->OutputSlot;
      UINT bits=entry->RegisterMask;
      while(bits){soComponents+=bits&1u;bits>>=1;}
   }
   if(soValid&&soSlots==1u)
      soValid=!(pData->StreamOutputStrideInBytes&3u)&&
         pData->StreamOutputStrideInBytes>=4u*soComponents;
   if(!soValid) {
      SetError(hDevice,E_NOTIMPL);return;
   }
   LOG_ENTRYPOINT();'''),
        # D3D-H5: SM 5 code never reaches the SM 4 parser.
        ('''   if (pData->pShaderCode) {
      pShader->state.tokens = Shader_tgsi_translate(pData->pShaderCode,
                                                    pShader->output_mapping);
   }''','''   if (pData->pShaderCode && !AgxD3d10TranslatorOnly(pData->pShaderCode)) {
      pShader->state.tokens = Shader_tgsi_translate(pData->pShaderCode,
                                                    pShader->output_mapping);
   }'''),
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
   struct pipe_resource *windowsTexture=windowsResource?windowsResource->resource:NULL;
   UINT windowsLevel=0,windowsMipCount=0,windowsFirstLayer=0,windowsLayers=0;
   UINT windowsLayerLimit=windowsTexture?windowsTexture->array_size:0;
   bool windowsDimOK=false;
   if(windowsTexture) switch(pCreateSRView->ResourceDimension) {
   case D3D10DDIRESOURCE_TEXTURE1D:
      windowsDimOK=windowsTexture->target==PIPE_TEXTURE_1D || windowsTexture->target==PIPE_TEXTURE_1D_ARRAY;
      windowsLevel=pCreateSRView->Tex1D.MostDetailedMip;
      windowsMipCount=pCreateSRView->Tex1D.MipLevels;
      windowsFirstLayer=pCreateSRView->Tex1D.FirstArraySlice;
      windowsLayers=pCreateSRView->Tex1D.ArraySize;break;
   case D3D10DDIRESOURCE_TEXTURE2D:
      windowsDimOK=windowsTexture->target==PIPE_TEXTURE_2D || windowsTexture->target==PIPE_TEXTURE_2D_ARRAY;
      windowsLevel=pCreateSRView->Tex2D.MostDetailedMip;
      windowsMipCount=pCreateSRView->Tex2D.MipLevels;
      windowsFirstLayer=pCreateSRView->Tex2D.FirstArraySlice;
      windowsLayers=pCreateSRView->Tex2D.ArraySize;break;
   case D3D10DDIRESOURCE_TEXTURE3D:
      windowsDimOK=windowsTexture->target==PIPE_TEXTURE_3D;
      windowsLevel=pCreateSRView->Tex3D.MostDetailedMip;
      windowsMipCount=pCreateSRView->Tex3D.MipLevels;
      windowsLayerLimit=windowsTexture->depth0;windowsLayers=windowsLayerLimit;break;
   case D3D10DDIRESOURCE_TEXTURECUBE:
      windowsDimOK=windowsTexture->target==PIPE_TEXTURE_CUBE && windowsTexture->array_size==6;
      windowsLevel=pCreateSRView->TexCube.MostDetailedMip;
      windowsMipCount=pCreateSRView->TexCube.MipLevels;windowsLayers=6;break;
   case D3D10DDIRESOURCE_BUFFER: {
      enum pipe_format bufferFormat=FormatTranslate(pCreateSRView->Format,false);
      UINT element=util_format_get_blocksize(bufferFormat);
      UINT64 first=pCreateSRView->Buffer.FirstElement;
      UINT64 count=pCreateSRView->Buffer.NumElements;
      UINT64 offset=first*element;
      UINT64 bytes=count*element;
      windowsDimOK=windowsTexture->target==PIPE_BUFFER &&
         windowsResource->Format==DXGI_FORMAT_UNKNOWN &&
         (windowsResource->bind_flags&D3D10_DDI_BIND_SHADER_RESOURCE) &&
         bufferFormat!=PIPE_FORMAT_NONE && element && count &&
         offset<=windowsResource->logical_bytes &&
         bytes<=windowsResource->logical_bytes-offset;
      windowsMipCount=windowsLayers=1;windowsLayerLimit=1;
      break;
   }
   default:break;
   }
   if(!windowsDevice||!windowsResource||windowsResource->owner_device!=windowsDevice||
      (!windowsResource->presentation&&
       !(windowsResource->bind_flags&D3D10_DDI_BIND_SHADER_RESOURCE))||
      (pCreateSRView->ResourceDimension!=D3D10DDIRESOURCE_BUFFER &&
       !AgxD3d10FormatViewCompatible(windowsResource->Format,
           pCreateSRView->Format,FALSE,
           windowsResource->presentation &&
           (windowsResource->bind_flags & D3D10_DDI_BIND_PRESENT)))||
      !windowsDimOK || !windowsMipCount || windowsLevel>=windowsResource->MipLevels ||
      windowsMipCount>windowsResource->MipLevels-windowsLevel || !windowsLayers ||
      windowsFirstLayer>=windowsLayerLimit || windowsLayers>windowsLayerLimit-windowsFirstLayer) {
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
   if(pCreateSRView->ResourceDimension==D3D10DDIRESOURCE_BUFFER) {
      UINT element=util_format_get_blocksize(format);
      desc.u.buf.offset=pCreateSRView->Buffer.FirstElement*element;
      desc.u.buf.size=pCreateSRView->Buffer.NumElements*element;
   }
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
   pSRView->owner_resource->sample_level = windowsLevel;
   pSRView->owner_resource->sample_layer = windowsFirstLayer;
   pSRView->next_presentation=NULL;
   if(pSRView->handle && pSRView->owner_resource->presentation) {
      pSRView->next_presentation=pSRView->owner_resource->first_presentation_srv;
      pSRView->owner_resource->first_presentation_srv=pSRView;
   }
}


/*
 * ----------------------------------------------------------------------
 *
 * CreateShaderResourceView1 --'''),
        ('''   pSRView->handle = pipe->create_sampler_view(pipe, resource, &desc);
}


/*
 * ----------------------------------------------------------------------
 *
 * DestroyShaderResourceView --''','''   pSRView->handle = pipe->create_sampler_view(pipe, resource, &desc);
   pSRView->owner_device=CastDevice(hDevice);
   pSRView->owner_resource=CastResource(pCreateSRView->hDrvResource);
   pSRView->next_presentation=NULL;
   if(pSRView->handle && pSRView->owner_resource &&
      pSRView->owner_resource->presentation) {
      pSRView->next_presentation=pSRView->owner_resource->first_presentation_srv;
      pSRView->owner_resource->first_presentation_srv=pSRView;
   }
}


/*
 * ----------------------------------------------------------------------
 *
 * DestroyShaderResourceView --'''),
        ('''   pipe->sampler_view_release(pipe, pSRView->handle);
   pSRView->handle = NULL;
}''','''   if(pSRView->owner_resource && pSRView->owner_resource->presentation) {
      ShaderResourceView **link=&pSRView->owner_resource->first_presentation_srv;
      while(*link && *link!=pSRView) link=&(*link)->next_presentation;
      if(*link==pSRView) *link=pSRView->next_presentation;
   }
   pSRView->owner_resource=NULL;
   pSRView->next_presentation=NULL;
   pipe->sampler_view_release(pipe, pSRView->handle);
   pSRView->handle = NULL;
}'''),
        ('''   pShader->type = MESA_SHADER_VERTEX;
   pShader->output_resolved = true;
''','''   pShader->type = MESA_SHADER_VERTEX;
   pShader->output_resolved = true;
   /* EXP1291: the runtime hands over uninitialized private memory, and
    * DestroyShader/ResolveState read these D3D-G10e fields of every shader. */
   pShader->agx_serial = 0;
   pShader->so_vs_handle = NULL;
   pShader->so_vs_serial = 0;
'''),
        ('''   pShader->type = MESA_SHADER_GEOMETRY;
   pShader->output_resolved = true;
''','''   pShader->type = MESA_SHADER_GEOMETRY;
   pShader->output_resolved = true;
   /* EXP1291: the runtime hands over uninitialized private memory, and
    * DestroyShader/ResolveState read these D3D-G10e fields of every shader. */
   pShader->agx_serial = 0;
   pShader->so_vs_handle = NULL;
   pShader->so_vs_serial = 0;
'''),
        ('''   bool all_slot_zero = true;

   pShader->type = MESA_SHADER_GEOMETRY;
''','''   bool all_slot_zero = true;

   pShader->type = MESA_SHADER_GEOMETRY;
   /* EXP1291: the runtime hands over uninitialized private memory, and
    * DestroyShader/ResolveState read these D3D-G10e fields of every shader. */
   pShader->agx_serial = 0;
   pShader->so_vs_handle = NULL;
   pShader->so_vs_serial = 0;
'''),
        ('''   pShader->type = MESA_SHADER_FRAGMENT;
   pShader->output_resolved = true;
''','''   pShader->type = MESA_SHADER_FRAGMENT;
   pShader->output_resolved = true;
   /* EXP1291: the runtime hands over uninitialized private memory, and
    * DestroyShader/ResolveState read these D3D-G10e fields of every shader. */
   pShader->agx_serial = 0;
   pShader->so_vs_handle = NULL;
   pShader->so_vs_serial = 0;
'''),
        ('''#include "util/u_gen_mipmap.h"
#include "drm-uapi/drm_fourcc.h"
''','''#include "util/u_gen_mipmap.h"
#include "drm-uapi/drm_fourcc.h"
#include "dxbc_to_nir.h"
extern "C" VOID AgxD3d10WindowsDiagnostic(PCSTR Stage, HRESULT Status,
                              const UINT *Values, UINT Count);
extern "C" BOOL AgxD3d10WindowsDiagnosticSwitch(const char *Name);

/* D3D-H3: with APPLE_AGX_D3D_SHADER_NIR=1 in the process environment the
 * pipe shader state comes from the DXBC -> NIR translator
 * (drivers/apple-agx/mesa/dxbc). The TGSI tokens stay the frontend's
 * record (output mapping, stream-output variants) and the state for every
 * program the translator refuses; stream-output shaders keep TGSI, whose
 * xfb rebuild Asahi's state creation owns.  EXP1296: as the default it
 * faulted the GPU from DWM (2435 firmware recoveries), so it stays opt-in
 * until that translation is found. */
/* D3D-H5: Mesa's DXBC parser (ShaderParse.c) knows the SM 4 opcodes only
 * (opcode_info[D3D10_SB_NUM_OPCODES], D3D10_SB_NUM_OPCODES = 0x70) and
 * indexes its opcode table with an assert; an SM 5 program (FL 11_0)
 * never reaches it and always takes the translator.  EXP1314: on an FL 11_0
 * device the runtime also admits 4_x programs using D3D 11 opcodes (raw and
 * structured buffers, D3D11_SB_GLOBAL_FLAG_ENABLE_RAW_AND_STRUCTURED_BUFFERS;
 * d2d1's ps_4_0 with dcl_resource_structured/ld_structured crashed in
 * tgsi_to_nir), so an opcode beyond the table routes the program to the
 * translator as well, and so does a stream the walk cannot follow.
 * EXP1315: so does any D3D 11 global flag (D3D10_SB_GLOBAL_FLAGS_MASK beyond
 * D3D10_SB_GLOBAL_FLAG_REFACTORING_ALLOWED): d2d1's minimum-precision ps_4_0
 * (D3D11_1_SB_GLOBAL_FLAG_ENABLE_MINIMUM_PRECISION) carries extended operand
 * tokens, which the parser asserts away ("Unknown TGSI texture target").
 * EXP1335: so does a switch (D3D10_SB_OPCODE_SWITCH): the SM 4 parser emits
 * TGSI SWITCH/CASE/DEFAULT/ENDSWITCH, which tgsi_to_nir does not translate
 * ("unknown TGSI opcode", abort() -- Windows Terminal's ps_4_0 switches on
 * its shading type); the translator lowers switch to NIR control flow. */
static inline bool
AgxD3d10TranslatorOnly(const UINT *code)
{
   if (!code)
      return false;
   if (((code[0] >> 4) & 0xfu) >= 5u)
      return true;
   const UINT size = code[1];
   for (UINT i = 2u; i < size;) {
      const UINT opcode = code[i] & 0x7ffu;
      UINT length = (code[i] >> 24) & 0x7fu;
      if (opcode == 0x35u) { /* D3D10_SB_OPCODE_CUSTOMDATA: length in the next DWORD */
         if (i + 1u >= size)
            return true;
         length = code[i + 1u];
      }
      if (opcode >= 0x70u || !length || length > size - i)
         return true;
      if (opcode == 0x4cu) /* D3D10_SB_OPCODE_SWITCH */
         return true;
      if (opcode == 0x6au && (code[i] & 0x00fff800u & ~0x800u))
         return true; /* D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS */
      i += length;
   }
   return false;
}

static void *
AgxD3d10CreateShaderState(struct pipe_context *pipe, Shader *shader,
                          const UINT *code)
{
   struct pipe_shader_state state = shader->state;
   const bool translator = AgxD3d10TranslatorOnly(code);
   if (code && !state.stream_output.num_outputs &&
       (translator || AgxD3d10WindowsDiagnosticSwitch("APPLE_AGX_D3D_SHADER_NIR"))) {
      struct dxbc_nir_error error = {};
      struct nir_shader *nir = dxbc_translate_tokens(code,
         pipe->screen->nir_options[shader->type], &error);
      UINT values[4] = {(UINT)shader->type, nir ? 1u : 0u, error.opcode, error.offset};
      AgxD3d10WindowsDiagnostic("measure-shader-nir", nir ? S_OK : E_NOTIMPL, values, 4u);
      if (nir) {
         state.type = PIPE_SHADER_IR_NIR;
         state.tokens = NULL;
         state.ir.nir = nir;
      }
   }
   if (state.type != PIPE_SHADER_IR_NIR && !state.tokens)
      return NULL;
   if (AgxD3d10WindowsDiagnosticSwitch("APPLE_AGX_D3D_SHADER_TRACE")) {
      /* EXP1313: a ps_4_0 corpus shader crashes in tgsi_to_nir on an FL 11_0
       * device only; record the DXBC and TGSI the frontend hands over. */
      UINT code_hash = 2166136261u, tgsi_hash = 2166136261u, tgsi_count = 0u;
      UINT code_count = code ? MIN2(code[1], 65536u) : 0u;
      for (UINT i = 0; i < code_count; ++i) code_hash = (code_hash ^ code[i]) * 16777619u;
      if (state.type != PIPE_SHADER_IR_NIR && state.tokens) {
         const UINT *t = (const UINT *)state.tokens;
         tgsi_count = MIN2((t[0] & 0xffu) + (t[0] >> 8), 65536u);
         for (UINT i = 0; i < tgsi_count; ++i) tgsi_hash = (tgsi_hash ^ t[i]) * 16777619u;
      }
      UINT values[7] = {(UINT)shader->type, (UINT)state.type, code ? code[0] : 0u, code_count,
                        code_hash, tgsi_count, tgsi_hash};
      AgxD3d10WindowsDiagnostic("measure-shader-state", S_OK, values, 7u);
   }
   switch (shader->type) {
   case MESA_SHADER_VERTEX: return pipe->create_vs_state(pipe, &state);
   case MESA_SHADER_GEOMETRY: return pipe->create_gs_state(pipe, &state);
   default: return pipe->create_fs_state(pipe, &state);
   }
}

void *
AgxD3d10CreateXfbState(struct pipe_context *pipe, Shader *shader,
                       const UINT *code, const struct pipe_stream_output_info *so)
{
   struct dxbc_nir_xfb xfb = {};
   struct dxbc_nir_error error = {};
   if (!code || !so || !so->num_outputs || so->num_outputs > DXBC_NIR_MAX_XFB_OUTPUTS ||
       (shader->type != MESA_SHADER_VERTEX && shader->type != MESA_SHADER_GEOMETRY))
      return NULL;
   xfb.num_outputs = so->num_outputs;
   for (unsigned i = 0; i < so->num_outputs; ++i) {
      const struct pipe_stream_output *o = &so->output[i];
      xfb.outputs[i].reg = o->register_index;
      xfb.outputs[i].start_component = (uint8_t)o->start_component;
      xfb.outputs[i].num_components = (uint8_t)o->num_components;
      xfb.outputs[i].buffer = (uint8_t)o->output_buffer;
      xfb.outputs[i].stream = (uint8_t)o->stream;
      xfb.outputs[i].dst_offset = o->dst_offset;
   }
   for (unsigned b = 0; b < DXBC_NIR_MAX_XFB_BUFFERS && b < PIPE_MAX_SO_BUFFERS; ++b)
      xfb.stride[b] = so->stride[b];
   struct nir_shader *nir = dxbc_translate_tokens_xfb(code,
      pipe->screen->nir_options[shader->type], &xfb, &error);
   UINT values[4] = {(UINT)shader->type, nir ? 1u : 0u, error.opcode, error.offset};
   AgxD3d10WindowsDiagnostic("measure-shader-nir-xfb", nir ? S_OK : E_NOTIMPL, values, 4u);
   if (!nir)
      return NULL;
   struct pipe_shader_state state = {};
   state.type = PIPE_SHADER_IR_NIR;
   state.ir.nir = nir;
   return shader->type == MESA_SHADER_VERTEX ? pipe->create_vs_state(pipe, &state) :
                                               pipe->create_gs_state(pipe, &state);
}
'''),
        ('''   pShader->state.tokens = Shader_tgsi_translate(pCode, pShader->output_mapping);

   pShader->handle = pipe->create_vs_state(pipe, &pShader->state);
''','''   pShader->agx_code = NULL;
   pShader->state.tokens = AgxD3d10TranslatorOnly(pCode) ? NULL :
      Shader_tgsi_translate(pCode, pShader->output_mapping);

   pShader->handle = AgxD3d10CreateShaderState(pipe, pShader, pCode);
   if (!pShader->handle) {
      SetError(hDevice, E_INVALIDARG);
   } else if (AgxD3d10TranslatorOnly(pCode)) {
      /* the source of a stream-output variant (an empty GS) */
      pShader->agx_code = (UINT *)malloc((size_t)pCode[1] * sizeof(UINT));
      if (pShader->agx_code) {
         memcpy(pShader->agx_code, pCode, (size_t)pCode[1] * sizeof(UINT));
      } else {
         pipe->delete_vs_state(pipe, pShader->handle);
         pShader->handle = NULL;
         SetError(hDevice, E_OUTOFMEMORY);
      }
   }
'''),
        ('''   pShader->state.tokens = Shader_tgsi_translate(pShaderCode, pShader->output_mapping);

   pShader->handle = pipe->create_gs_state(pipe, &pShader->state);
''','''   pShader->state.tokens = AgxD3d10TranslatorOnly(pShaderCode) ? NULL :
      Shader_tgsi_translate(pShaderCode, pShader->output_mapping);

   pShader->handle = AgxD3d10CreateShaderState(pipe, pShader, pShaderCode);
   if (!pShader->handle)
      SetError(hDevice, E_INVALIDARG);
'''),
        ('''   pShader->state.tokens = Shader_tgsi_translate(pShaderCode,
                                                 pShader->output_mapping);

   pShader->handle = pipe->create_fs_state(pipe, &pShader->state);
''','''   pShader->state.tokens = AgxD3d10TranslatorOnly(pShaderCode) ? NULL :
      Shader_tgsi_translate(pShaderCode, pShader->output_mapping);

   pShader->handle = AgxD3d10CreateShaderState(pipe, pShader, pShaderCode);
   if (!pShader->handle)
      SetError(hDevice, E_INVALIDARG);
''')])
    replace_function_body('src/gallium/frontends/d3d10umd/Shader.cpp','SetSamplers','''   Device *pDevice = CastDevice(hDevice);
   const UINT slots = D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT;
   static_assert(PIPE_MAX_SAMPLERS >= D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT,
                 "D3D10 sampler state must fit the native array");
   bool valid = pDevice && pDevice->pipe &&
      (shader_type == MESA_SHADER_VERTEX || shader_type == MESA_SHADER_FRAGMENT ||
       shader_type == MESA_SHADER_GEOMETRY || shader_type == MESA_SHADER_COMPUTE ||
       shader_type == MESA_SHADER_TESS_CTRL || shader_type == MESA_SHADER_TESS_EVAL) &&
      Offset <= slots &&
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
   const UINT slots=D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
   static_assert(PIPE_MAX_SHADER_SAMPLER_VIEWS>=
                 D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,
                 "D3D10 SRV state must fit the native array");
   bool valid = pDevice && pDevice->pipe &&
      (shader_type == MESA_SHADER_VERTEX || shader_type == MESA_SHADER_FRAGMENT ||
       shader_type == MESA_SHADER_GEOMETRY || shader_type == MESA_SHADER_COMPUTE ||
       shader_type == MESA_SHADER_TESS_CTRL || shader_type == MESA_SHADER_TESS_EVAL) &&
      Offset<=slots &&
      NumViews<=slots-Offset && (NumViews == 0 || phShaderResourceViews);
   struct pipe_sampler_view *views[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]={};
   for (UINT i = 0; valid && i < NumViews; ++i) {
      ShaderResourceView *view = CastShaderResourceView(phShaderResourceViews[i]);
      valid = !view || (view->owner_device == pDevice && view->owner_resource &&
         view->owner_resource->owner_device == pDevice && view->handle);
      if(valid && view) views[i]=view->handle;
   }
   if (!valid) { SetError(hDevice, E_NOTIMPL); return; }
   if(!NumViews) return;
   for(UINT i=0;i<NumViews;++i)
      pDevice->sampler_views[shader_type][Offset+i]=views[i];
   pDevice->pipe->set_sampler_views(
      pDevice->pipe,shader_type,0,PIPE_MAX_SHADER_SAMPLER_VIEWS,0,
      pDevice->sampler_views[shader_type]);''')
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
    replace_function_body('src/gallium/frontends/d3d10umd/Shader.cpp','GenMips','''   Device *device=CastDevice(hDevice);
   if(device && !CheckPredicate(device)) return;
   ShaderResourceView *view=CastShaderResourceView(hShaderResourceView);
   if(!device || !view || view->owner_device!=device || !view->handle ||
      !view->owner_resource || view->owner_resource->owner_device!=device) {
      SetError(hDevice,E_INVALIDARG);return;
   }
   struct pipe_sampler_view *srv=view->handle;
   struct pipe_resource *texture=srv->texture;
   unsigned first=srv->u.tex.first_level,last=srv->u.tex.last_level;
   if(first==last) return;
   bool bgr=srv->format==PIPE_FORMAT_B8G8R8A8_UNORM ||
      srv->format==PIPE_FORMAT_B8G8R8A8_SRGB || srv->format==PIPE_FORMAT_B8G8R8X8_UNORM ||
      srv->format==PIPE_FORMAT_B8G8R8X8_SRGB;
   bool target=texture->target==PIPE_TEXTURE_1D || texture->target==PIPE_TEXTURE_1D_ARRAY ||
      texture->target==PIPE_TEXTURE_2D || texture->target==PIPE_TEXTURE_2D_ARRAY ||
      texture->target==PIPE_TEXTURE_3D || texture->target==PIPE_TEXTURE_CUBE;
   if(!bgr || !target ||
      texture->nr_samples>1 || first>last || last>texture->last_level ||
      !(view->owner_resource->bind_flags&D3D10_DDI_BIND_RENDER_TARGET) ||
      !device->pipe->screen->resource_create_with_modifiers) {
      SetError(hDevice,E_NOTIMPL);return;
   }
   for(unsigned level=first+1;level<=last;++level) {
      HRESULT status=AgxD3d10WindowsFlushRetire(device->windows);
      if(FAILED(status)) {SetError(hDevice,status);return;}
      struct pipe_resource info={};
      info.target=texture->target;info.format=texture->format;
      info.width0=u_minify(texture->width0,level-1);
      info.height0=u_minify(texture->height0,level-1);
      info.depth0=u_minify(texture->depth0,level-1);info.array_size=texture->array_size;
      info.nr_samples=info.nr_storage_samples=1;
      info.bind=PIPE_BIND_SAMPLER_VIEW;info.usage=PIPE_USAGE_STAGING;
      /* Existing capture forbids overlapping texture/output roles. Stage the
       * source LOD with the existing allocator/transfers, not an alias waiver. */
      const uint64_t modifier=DRM_FORMAT_MOD_APPLE_GPU_TILED;
      struct pipe_resource *source=device->pipe->screen->resource_create_with_modifiers(
         device->pipe->screen,&info,&modifier,1);
      if(!source) {SetError(hDevice,E_OUTOFMEMORY);return;}
      struct pipe_box box={};
      box.width=info.width0;box.height=(int16_t)info.height0;
      box.depth=(int16_t)(info.target==PIPE_TEXTURE_3D?info.depth0:info.array_size);
      struct pipe_transfer *read=NULL,*write=NULL;
      void *src=device->pipe->texture_map(device->pipe,texture,level-1,PIPE_MAP_READ,&box,&read);
      void *dst=device->pipe->texture_map(device->pipe,source,0,PIPE_MAP_WRITE,&box,&write);
      bool mapped=src&&dst&&read&&write&&read->stride>=info.width0*4&&write->stride>=info.width0*4;
      if(mapped) for(unsigned layer=0;layer<(unsigned)box.depth;++layer)
         for(unsigned row=0;row<info.height0;++row)
            memcpy((uint8_t *)dst+layer*write->layer_stride+row*write->stride,
                   (const uint8_t *)src+layer*read->layer_stride+row*read->stride,info.width0*4);
      if(write) pipe_texture_unmap(device->pipe,write);
      if(read) pipe_texture_unmap(device->pipe,read);
      if(!mapped) {
         pipe_resource_reference(&source,NULL);SetError(hDevice,E_FAIL);return;
      }
      struct pipe_blit_info blit={};
      blit.src.resource=source;blit.src.format=srv->format;blit.src.box=box;
      blit.dst.resource=texture;blit.dst.format=srv->format;blit.dst.level=level;
      blit.dst.box.width=u_minify(texture->width0,level);
      blit.dst.box.height=(int16_t)u_minify(texture->height0,level);
      if(texture->target==PIPE_TEXTURE_3D) {
         blit.dst.box.depth=(int16_t)u_minify(texture->depth0,level);
      } else {
         blit.src.box.z=blit.dst.box.z=(int16_t)srv->u.tex.first_layer;
         blit.src.box.depth=blit.dst.box.depth=(int16_t)(srv->u.tex.last_layer-srv->u.tex.first_layer+1);
      }
      blit.mask=PIPE_MASK_RGBA;blit.filter=PIPE_TEX_FILTER_LINEAR;
      device->pipe->blit(device->pipe,&blit);
      status=AgxD3d10WindowsFlushStatus(device->windows);
      pipe_resource_reference(&source,NULL);
      if(FAILED(status)) {SetError(hDevice,status);return;}
   }''')
    replace_function_body('src/gallium/frontends/d3d10umd/Shader.cpp','SetConstantBuffers','''   /* D3D-H7: whole buffers; the 11.1 table binds ranges (agx_wddm2_ddi.cpp) */
   void AgxSetConstantBuffersRange(mesa_shader_stage, D3D10DDI_HDEVICE, UINT, UINT,
                                   const D3D10DDI_HRESOURCE *, const UINT *, const UINT *);
   AgxSetConstantBuffersRange(shader_type, hDevice, StartBuffer, NumBuffers, phBuffers,
                              NULL, NULL);''')
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
        # D3D-H5: BC6H/BC7 (FL 11_0), BPTC in Gallium, sampled natively by AGX
        ('''   case DXGI_FORMAT_BC5_SNORM:
      return PIPE_FORMAT_RGTC2_SNORM;
''','''   case DXGI_FORMAT_BC5_SNORM:
      return PIPE_FORMAT_RGTC2_SNORM;
   case DXGI_FORMAT_BC6H_TYPELESS:
   case DXGI_FORMAT_BC6H_UF16:
      return PIPE_FORMAT_BPTC_RGB_UFLOAT;
   case DXGI_FORMAT_BC6H_SF16:
      return PIPE_FORMAT_BPTC_RGB_FLOAT;
   case DXGI_FORMAT_BC7_TYPELESS:
   case DXGI_FORMAT_BC7_UNORM:
      return PIPE_FORMAT_BPTC_RGBA_UNORM;
   case DXGI_FORMAT_BC7_UNORM_SRGB:
      return PIPE_FORMAT_BPTC_SRGBA;
'''),
        ('#include "Format.h"','''#include "Format.h"

extern "C" DXGI_FORMAT AgxD3d10WindowsCastParent(DXGI_FORMAT Format, UINT *ReadOnly);

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
   /* D3D-G10a: a typeless resource admits a view in any format of its DXGI
    * cast family (the winsys cast-family table, which also decides the
    * multisample reporting); a fully typed resource only its own format. */
   if (AgxD3d10WindowsCastParent(resource, NULL) == resource &&
       AgxD3d10WindowsCastParent(view, NULL) == resource)
      return FormatTranslate(view, depth) != PIPE_FORMAT_NONE;
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
        ('''   if (pDevice->bound_empty_gs && pDevice->bound_vs &&
       pDevice->bound_vs->state.tokens) {
      Shader *gs = pDevice->bound_empty_gs;
      Shader *vs = pDevice->bound_vs;
      bool remapped = false;
      struct pipe_context *pipe = pDevice->pipe;
      if (!gs->output_resolved) {
         for (unsigned i = 0; i < gs->state.stream_output.num_outputs; ++i) {
            unsigned mapping =
               ShaderFindOutputMapping(vs, gs->state.stream_output.output[i].register_index);
            if (mapping != gs->state.stream_output.output[i].register_index) {
               gs->state.stream_output.output[i].register_index = mapping;
               remapped = true;
            }
         }
         if (remapped) {
            pipe->delete_gs_state(pipe, gs->handle);
            gs->handle = pipe->create_gs_state(pipe, &gs->state);
         }
         gs->output_resolved = true;
      }
      pipe->bind_gs_state(pipe, gs->handle);
   }''','''   /* D3D-G10e: stream output without geometry shader code is the bound
    * vertex shader's stream output. The variant -- the VS tokens with the
    * declaration's registers mapped to its outputs -- is made once per VS
    * (serial) and bound with no GS; Asahi then captures through its
    * passthrough GS (agx_needs_passthrough_gs). Upstream created a GS state
    * without tokens, which the Asahi driver cannot take. */
   if (pDevice->bound_empty_gs && pDevice->bound_vs &&
       (pDevice->bound_vs->state.tokens || pDevice->bound_vs->agx_code)) {
      Shader *gs = pDevice->bound_empty_gs;
      Shader *vs = pDevice->bound_vs;
      struct pipe_context *pipe = pDevice->pipe;
      if (!gs->so_vs_handle || gs->so_vs_serial != vs->agx_serial) {
         if (gs->so_vs_handle)
            pipe->delete_vs_state(pipe, gs->so_vs_handle);
         if (vs->state.tokens) {
            struct pipe_shader_state variant = vs->state;
            variant.stream_output = gs->state.stream_output;
            for (unsigned i = 0; i < variant.stream_output.num_outputs; ++i)
               variant.stream_output.output[i].register_index = ShaderFindOutputMapping(
                  vs, gs->state.stream_output.output[i].register_index);
            gs->so_vs_handle = pipe->create_vs_state(pipe, &variant);
         } else {
            /* D3D-H5: an SM 5 vertex shader (no TGSI record) captures
             * through the translator; the declaration names D3D registers. */
            gs->so_vs_handle = AgxD3d10CreateXfbState(pipe, vs, vs->agx_code,
                                                      &gs->state.stream_output);
         }
         gs->so_vs_serial = vs->agx_serial;
      }
      pipe->bind_gs_state(pipe, NULL);
      if (gs->so_vs_handle)
         pipe->bind_vs_state(pipe, gs->so_vs_handle);
   }'''),
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
    # EXP1065: Notepad and the atlas probe crashed at agx_update_vs reading a
    # NULL ctx->attributes: D3D draws need no input layout (SV_VertexID only),
    # but upstream update_velems bound nothing for a NULL layout or before the
    # first IaSetInputLayout. A NULL layout means zero vertex elements.
    replace_function_body('src/gallium/frontends/d3d10umd/Draw.cpp','update_velems','''   if (!pDevice->velems_changed && pDevice->agx_velems_bound)
      return;

   if(pDevice->element_layout) {
      struct cso_velems_state *state = &pDevice->element_layout->state;
      for (unsigned i = 0; i < state->count; i++)
         state->velems[i].src_stride = pDevice->vertex_strides[state->velems[i].vertex_buffer_index];
      cso_set_vertex_elements(pDevice->cso, state);
   } else {
      struct cso_velems_state empty;
      memset(&empty, 0, sizeof(empty));
      cso_set_vertex_elements(pDevice->cso, &empty);
   }

   pDevice->agx_velems_bound = TRUE;
   pDevice->velems_changed = false;''')
    replace_function_body('src/gallium/frontends/d3d10umd/Draw.cpp','Draw','''   LOG_ENTRYPOINT();
   Device *pDevice = CastDevice(hDevice);
   /* D3D-G10c: adjacency topologies are drawn natively; Asahi feeds them
    * to the bound geometry shader or a passthrough one (agx_state.c
    * agx_needs_passthrough_gs). The graph-capture era refused them. */
   if (pDevice && AgxWin32AsahiContextDrawReceipt(pDevice->pipe)) {
      HRESULT status=AgxD3d10WindowsFlushRetire(pDevice->windows);
      if(FAILED(status)) { SetError(hDevice,status); return; }
   }
   AgxD3d10WindowsDiagnosticState(pDevice->windows, "resolve-before");
   ResolveState(pDevice);
   assert(pDevice->primitive < MESA_PRIM_COUNT);
   const UINT args[] = {VertexCount, StartVertexLocation, static_cast<UINT>(pDevice->primitive)};
   AgxD3d10WindowsDiagnostic("draw-args", S_OK, args, 3);
   AgxD3d10WindowsDiagnosticState(pDevice->windows, "draw-before");
   AgxD3d10NoTargetReceipt(pDevice, 1u, VertexCount);
   util_draw_arrays(pDevice->pipe, pDevice->primitive, StartVertexLocation, VertexCount);
   AgxD3d10WindowsDiagnosticState(pDevice->windows, "draw-after");''')
    replace_function_body('src/gallium/frontends/d3d10umd/Draw.cpp','DrawIndexed','''   Device *pDevice = CastDevice(hDevice);
   /* EXP1064: DrawIndexed(0) is a D3D no-op, not an error. */
   if (pDevice && !IndexCount) return;
   if (!pDevice || pDevice->primitive >= MESA_PRIM_COUNT ||
       !pDevice->index_buffer ||
       (pDevice->index_size != 2 && pDevice->index_size != 4)) {
      SetError(hDevice, E_NOTIMPL);
      return;
   }
   /* D3D-G10c: adjacency topologies are drawn natively; Asahi feeds them
    * to the bound geometry shader or a passthrough one (agx_state.c
    * agx_needs_passthrough_gs). The graph-capture era refused them. */
   if (AgxWin32AsahiContextDrawReceipt(pDevice->pipe)) {
      HRESULT status=AgxD3d10WindowsFlushRetire(pDevice->windows);
      if(FAILED(status)) { SetError(hDevice,status); return; }
   }
   ResolveState(pDevice);
   struct pipe_draw_info info;
   struct pipe_draw_start_count_bias draw = {};
   util_draw_init_info(&info);
   info.index_size = pDevice->index_size;
   info.mode = pDevice->primitive;
   info.index.resource = pDevice->index_buffer;
   info.instance_count = 1;
   info.primitive_restart = true;
   info.restart_index = pDevice->restart_index;
   draw.start = StartIndexLocation + pDevice->ib_offset/pDevice->index_size;
   draw.count = IndexCount;
   draw.index_bias = BaseVertexLocation;
   AgxD3d10NoTargetReceipt(pDevice, 2u, IndexCount);
   pDevice->pipe->draw_vbo(pDevice->pipe, &info, 0, NULL, &draw, 1);''')
    replace_function_body('src/gallium/frontends/d3d10umd/Draw.cpp','DrawIndexedInstanced','''   Device *pDevice=CastDevice(hDevice);
   if(!pDevice || !pDevice->index_buffer ||
      (pDevice->index_size!=2 && pDevice->index_size!=4) ||
      pDevice->primitive>=MESA_PRIM_COUNT) {
      SetError(hDevice,E_NOTIMPL);return;
   }
   /* D3D-G10c: adjacency topologies are drawn natively; Asahi feeds them
    * to the bound geometry shader or a passthrough one (agx_state.c
    * agx_needs_passthrough_gs). The graph-capture era refused them. */
   if(!InstanceCount || !IndexCountPerInstance) return;
   if(AgxWin32AsahiContextDrawReceipt(pDevice->pipe)) {
      HRESULT status=AgxD3d10WindowsFlushRetire(pDevice->windows);
      if(FAILED(status)) {SetError(hDevice,status);return;}
   }
   ResolveState(pDevice);
   struct pipe_draw_info info;struct pipe_draw_start_count_bias draw={};
   util_draw_init_info(&info);info.index_size=pDevice->index_size;
   info.mode=pDevice->primitive;info.index.resource=pDevice->index_buffer;
   info.instance_count=InstanceCount;info.start_instance=StartInstanceLocation;
   info.primitive_restart=true;info.restart_index=pDevice->restart_index;
   draw.start=StartIndexLocation+pDevice->ib_offset/pDevice->index_size;
   draw.count=IndexCountPerInstance;draw.index_bias=BaseVertexLocation;
   AgxD3d10NoTargetReceipt(pDevice,3u,IndexCountPerInstance);
   pDevice->pipe->draw_vbo(pDevice->pipe,&info,0,NULL,&draw,1);''')
    replace_function_body('src/gallium/frontends/d3d10umd/Draw.cpp','DrawInstanced','''   Device *pDevice=CastDevice(hDevice);
   if(!pDevice || pDevice->primitive>=MESA_PRIM_COUNT) {
      SetError(hDevice,E_NOTIMPL);return;
   }
   /* D3D-G10c: adjacency topologies are drawn natively; Asahi feeds them
    * to the bound geometry shader or a passthrough one (agx_state.c
    * agx_needs_passthrough_gs). The graph-capture era refused them. */
   if(!InstanceCount || !VertexCountPerInstance) return;
   if(AgxWin32AsahiContextDrawReceipt(pDevice->pipe)) {
      HRESULT status=AgxD3d10WindowsFlushRetire(pDevice->windows);
      if(FAILED(status)) {SetError(hDevice,status);return;}
   }
   ResolveState(pDevice);
   AgxD3d10NoTargetReceipt(pDevice,4u,VertexCountPerInstance);
   util_draw_arrays_instanced(pDevice->pipe,pDevice->primitive,
      StartVertexLocation,VertexCountPerInstance,StartInstanceLocation,
      InstanceCount);''')
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

/* D3D-H5: the D3D11 table's indirect draws resolve the same state */
void
AgxD3d11ResolveState(Device *pDevice)
{
   ResolveState(pDevice);
}
'''
    draw_path.write_text(draw_text)
    overlays['src/gallium/frontends/d3d10umd/Draw.cpp']['after']=hashlib.sha256(
        draw_path.read_bytes()).hexdigest()
    change('src/gallium/frontends/d3d10umd/Resource.cpp',
        'ae2d60a798ff0d9da6e55171013f133d1d99bc91ef2760875d126aa5b96fcf48',[
        # D3D-H2b: unordered-access buffers are shader buffers
        ('''   if (flags & D3D10_DDI_BIND_PRESENT)
      bind |= PIPE_BIND_DISPLAY_TARGET;
''','''   if (flags & D3D10_DDI_BIND_PRESENT)
      bind |= PIPE_BIND_DISPLAY_TARGET;

   if (flags & D3D11_DDI_BIND_UNORDERED_ACCESS)
      bind |= PIPE_BIND_SHADER_BUFFER | PIPE_BIND_SHADER_IMAGE;
'''),
        ('''   templat.bind       = translate_resource_flags(pCreateResource->BindFlags);
''','''   templat.bind       = translate_resource_flags(pCreateResource->BindFlags);
   /* EXP1297/EXP1298: Mesa bufferobj.c:162-187 binds buffer images as
    * SHADER_BUFFER resources; SHADER_IMAGE on PIPE_BUFFER blocks linear
    * allocation in Asahi. Texture UAVs retain SHADER_IMAGE. */
   if (pCreateResource->ResourceDimension == D3D10DDIRESOURCE_BUFFER)
      templat.bind &= ~PIPE_BIND_SHADER_IMAGE;
'''),
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
#include "util/format/u_format.h"
#include "drm-uapi/drm_fourcc.h"
#include "agx_win32_asahi_scene.h"

/* EXP1037: A8/RGB32 textures are stored in a lowered physical format
 * (4c7991ab). Every CPU access translates the D3D client format, as the
 * initial-data upload already does; a Map returns a client-format view. */
struct AgxD3d10ClientMap {
   void *client;
   void *target;
   unsigned stride, layer_stride, target_stride, target_layer_stride;
   unsigned width, height, depth;
   bool write;
};

static enum pipe_format
AgxD3d10ClientFormat(const Resource *resource)
{
   if (!resource || resource->buffer || !resource->resource ||
       AgxD3d10LoweredTextureFormat(resource->Format) == PIPE_FORMAT_NONE)
      return PIPE_FORMAT_NONE;
   enum pipe_format client = FormatTranslate(resource->Format, false);
   return client != resource->resource->format ? client : PIPE_FORMAT_NONE;
}

static HRESULT
AgxD3d10ClientMapBegin(Resource *resource, UINT SubResource, void *target,
                       const struct pipe_transfer *transfer, unsigned width,
                       unsigned height, unsigned depth, bool read, bool write,
                       void **client_out, UINT *row_pitch, UINT *depth_pitch)
{
   *client_out = NULL;
   enum pipe_format client = AgxD3d10ClientFormat(resource);
   if (client == PIPE_FORMAT_NONE) return S_OK;
   if (!resource->client_maps)
      resource->client_maps = calloc(resource->NumSubResources,
                                     sizeof(struct AgxD3d10ClientMap));
   struct AgxD3d10ClientMap *maps = (struct AgxD3d10ClientMap *)resource->client_maps;
   if (!maps || maps[SubResource].client || !transfer || !depth) return E_OUTOFMEMORY;
   unsigned stride = align(util_format_get_stride(client, width), 16u);
   unsigned layer = stride * util_format_get_nblocksy(client, height);
   void *memory = calloc(depth, layer);
   if (!memory) return E_OUTOFMEMORY;
   for (unsigned z = 0; read && z < depth; ++z)
      util_format_translate(client, (uint8_t *)memory + z * layer, stride, 0, 0,
         resource->resource->format,
         (const uint8_t *)target + z * transfer->layer_stride, transfer->stride,
         0, 0, width, height);
   struct AgxD3d10ClientMap entry = {memory, target, stride, layer,
      (unsigned)transfer->stride, (unsigned)transfer->layer_stride,
      width, height, depth, write};
   maps[SubResource] = entry;
   *client_out = memory;
   *row_pitch = stride;
   *depth_pitch = layer;
   return S_OK;
}

static void
AgxD3d10ClientMapEnd(Resource *resource, UINT SubResource)
{
   struct AgxD3d10ClientMap *maps = (struct AgxD3d10ClientMap *)resource->client_maps;
   if (!maps || !maps[SubResource].client) return;
   struct AgxD3d10ClientMap *m = &maps[SubResource];
   for (unsigned z = 0; m->write && z < m->depth; ++z)
      util_format_translate(resource->resource->format,
         (uint8_t *)m->target + z * m->target_layer_stride, m->target_stride, 0, 0,
         FormatTranslate(resource->Format, false),
         (const uint8_t *)m->client + z * m->layer_stride, m->stride,
         0, 0, m->width, m->height);
   free(m->client);
   memset(m, 0, sizeof(*m));
}

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

#include "util/box.h"

/* D3D-G10b: the format a ResourceCopy / ResourceCopyRegion blit reads and
 * writes so that the stored bits survive: the bring-up byte families first
 * (BGRA8, BGRX8, RGBA8 as UNORM), then the source format itself (linear;
 * snorm as sint, since snorm does not round-trip -- Asahi agx_blit.c),
 * then any other pair of one DXGI cast family as the unsigned-integer
 * format of their block size. Compressed formats copy through
 * resource_copy_region (PIPE_FORMAT_NONE here); depth formats only to
 * themselves. */
/* D3D copies between subresources of one resource type; a pipe target
 * only records the slice count or cube-ness of a 1D/2D resource (EXP1292:
 * a Texture2DArray slice into a Texture2D staging copy was refused). */
static unsigned
AgxD3d10CopyTargetFamily(enum pipe_texture_target target)
{
   switch (target) {
   case PIPE_TEXTURE_1D: case PIPE_TEXTURE_1D_ARRAY: return 1;
   case PIPE_TEXTURE_2D: case PIPE_TEXTURE_2D_ARRAY: case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_CUBE: case PIPE_TEXTURE_CUBE_ARRAY: return 2;
   case PIPE_TEXTURE_3D: return 3;
   default: return 0;
   }
}

static enum pipe_format
AgxD3d10CopyRawFormat(const Resource *source, const Resource *destination,
                      const struct pipe_resource *src, const struct pipe_resource *dst)
{
   unsigned family = AgxD3d10CopyFamily(source->Format);
   if (family && family == AgxD3d10CopyFamily(destination->Format))
      return family == 3 ? PIPE_FORMAT_R8G8B8A8_UNORM : PIPE_FORMAT_B8G8R8A8_UNORM;
   if (util_format_is_compressed(src->format) || util_format_is_compressed(dst->format))
      return PIPE_FORMAT_NONE;
   if (src->format == dst->format) {
      enum pipe_format linear = util_format_linear(src->format);
      return util_format_is_snorm(linear) ? util_format_snorm_to_sint(linear) : linear;
   }
   DXGI_FORMAT parent = AgxD3d10WindowsCastParent(source->Format, NULL);
   if (parent == DXGI_FORMAT_UNKNOWN ||
       parent != AgxD3d10WindowsCastParent(destination->Format, NULL) ||
       util_format_is_depth_or_stencil(src->format) ||
       util_format_is_depth_or_stencil(dst->format) ||
       util_format_get_blocksize(src->format) != util_format_get_blocksize(dst->format))
      return PIPE_FORMAT_NONE;
   switch (util_format_get_blocksize(src->format)) {
   case 1: return PIPE_FORMAT_R8_UINT;
   case 2: return PIPE_FORMAT_R16_UINT;
   case 4: return PIPE_FORMAT_R32_UINT;
   case 8: return PIPE_FORMAT_R32G32_UINT;
   case 16: return PIPE_FORMAT_R32G32B32A32_UINT;
   default: return PIPE_FORMAT_NONE;
   }
}

/* EXP1030: CPU Map/Unmap of a texture needs only a known texel layout.
 * EXP1029 text probe: Map of a dynamic A8 glyph texture returned
 * E_INVALIDARG, so Direct2D/DirectWrite text never reached the GPU.
 * EXP1290: the bring-up list (ten colour formats and the byte copy families)
 * refused Map of an R32G32B32A32_UINT staging texture and removed the device;
 * every format the frontend translates has a layout (a format the resource
 * stores differently goes through the client map). */
static bool
AgxD3d10MapFormat(DXGI_FORMAT format)
{
   return FormatTranslate(format, false) != PIPE_FORMAT_NONE;
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

/* D3D-H5: the D3D11 limits (d3d11.h D3D11_REQ_*, a superset of D3D10's;
 * the runtime enforces each feature level's own before calling the driver,
 * and the resource-size formula min(max(128, 0.25 x video memory), 2048) MB) */
#define AGX_D3D11_REQ_MIP_LEVELS 15u
#define AGX_D3D11_REQ_RESOURCE_SIZE_IN_MEGABYTES 2048u
#define AGX_D3D11_REQ_TEXTURE1D_U_DIMENSION 16384u
#define AGX_D3D11_REQ_TEXTURE1D_ARRAY_AXIS_DIMENSION 2048u
#define AGX_D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION 16384u
#define AGX_D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION 2048u
#define AGX_D3D11_REQ_TEXTURECUBE_DIMENSION 16384u
#define AGX_D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION 2048u

static bool
AgxD3d10ResourceWithinRequiredLimits(
   const D3D10DDIARG_CREATERESOURCE *resource)
{
   if (!resource || !resource->pMipInfoList || !resource->MipLevels ||
       resource->MipLevels > AGX_D3D11_REQ_MIP_LEVELS || !resource->ArraySize)
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
         width <= (uint64_t)AGX_D3D11_REQ_RESOURCE_SIZE_IN_MEGABYTES * 1024u * 1024u;
   case D3D10DDIRESOURCE_TEXTURE1D:
      if (width > AGX_D3D11_REQ_TEXTURE1D_U_DIMENSION || height != 1 || depth != 1 ||
          array > AGX_D3D11_REQ_TEXTURE1D_ARRAY_AXIS_DIMENSION) return false;
      break;
   case D3D10DDIRESOURCE_TEXTURE2D:
      if (width > AGX_D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
          height > AGX_D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || depth != 1 ||
          array > AGX_D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION) return false;
      break;
   case D3D10DDIRESOURCE_TEXTURE3D:
      if (width > AGX_D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION ||
          height > AGX_D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION ||
          depth > AGX_D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION || array != 1)
         return false;
      break;
   case D3D10DDIRESOURCE_TEXTURECUBE:
      if (width != height || width > AGX_D3D11_REQ_TEXTURECUBE_DIMENSION || depth != 1 ||
          array > AGX_D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION) return false;
      break;
   default:
      return false;
   }

   enum pipe_format format = FormatTranslate(resource->Format, false);
   uint64_t element = util_format_get_blocksize(format);
   uint64_t blockWidth = util_format_get_blockwidth(format);
   uint64_t blockHeight = util_format_get_blockheight(format);
   uint64_t maximum =
      (uint64_t)AGX_D3D11_REQ_RESOURCE_SIZE_IN_MEGABYTES * 1024u * 1024u;
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
   bool bufferResource = pCreateResource->ResourceDimension == D3D10DDIRESOURCE_BUFFER;
   const UINT bufferBindMask = D3D10_DDI_BIND_VERTEX_BUFFER |
      D3D10_DDI_BIND_INDEX_BUFFER | D3D10_DDI_BIND_CONSTANT_BUFFER |
      D3D10_DDI_BIND_SHADER_RESOURCE | D3D10_DDI_BIND_STREAM_OUTPUT |
      D3D10_DDI_BIND_RENDER_TARGET | D3D11_DDI_BIND_UNORDERED_ACCESS;
   /* D3D-H2b: raw-view and structured buffers (D3D11 DDI) */
   const UINT bufferMisc = pCreateResource->MiscFlags;
   bool bufferMiscOK = (bufferMisc & ~D3D11_DDI_RESOURCE_MISC_DRAWINDIRECT_ARGS) == 0 ||
      (bufferMisc & ~D3D11_DDI_RESOURCE_MISC_DRAWINDIRECT_ARGS) ==
         D3D11_DDI_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS ||
      bufferMisc == D3D11_DDI_RESOURCE_MISC_BUFFER_STRUCTURED;
   bool wantsConstant =
      (pCreateResource->BindFlags & D3D10_DDI_BIND_CONSTANT_BUFFER) != 0;
   bool wantsIndex =
      (pCreateResource->BindFlags & D3D10_DDI_BIND_INDEX_BUFFER) != 0;
   bool wantsStream =
      (pCreateResource->BindFlags & D3D10_DDI_BIND_STREAM_OUTPUT) != 0;
   bool wantsRender =
      (pCreateResource->BindFlags & D3D10_DDI_BIND_RENDER_TARGET) != 0;
   bool wantsPresentation = pCreateResource->pPrimaryDesc != NULL ||
      (pCreateResource->BindFlags & D3D10_DDI_BIND_PRESENT) != 0 ||
      (pCreateResource->MiscFlags & D3D10_DDI_RESOURCE_MISC_SHARED) != 0;
   bool bufferShape = bufferResource && pResource && pDevice && resourceMip &&
      pCreateResource->Format == DXGI_FORMAT_UNKNOWN &&
      !(pCreateResource->BindFlags & ~bufferBindMask) &&
      bufferMiscOK && !pCreateResource->pPrimaryDesc &&
      pCreateResource->MipLevels == 1 && pCreateResource->ArraySize == 1 &&
      resourceMip[0].TexelWidth > 0 && resourceMip[0].TexelHeight == 1 &&
      resourceMip[0].TexelDepth == 1 && pCreateResource->SampleDesc.Count == 1 &&
      pCreateResource->SampleDesc.Quality == 0 &&
      (!pCreateResource->pInitialDataUP || pCreateResource->pInitialDataUP[0].pSysMem) &&
      (!wantsConstant || (pCreateResource->BindFlags == D3D10_DDI_BIND_CONSTANT_BUFFER &&
        resourceMip[0].TexelWidth <= 65536 && !(resourceMip[0].TexelWidth & 15))) &&
      AgxD3d10WindowsIdentity(pDevice->windows,&resourceOwner,&resourceGeneration);
   bool validBufferUsage = bufferShape &&
      ((pCreateResource->Usage == D3D10_DDI_USAGE_DEFAULT &&
        pCreateResource->MapFlags == 0) ||
       (pCreateResource->Usage == D3D10_DDI_USAGE_IMMUTABLE &&
        pCreateResource->MapFlags == 0 && !wantsStream && !wantsRender &&
        pCreateResource->pInitialDataUP) ||
       (pCreateResource->Usage == D3D10_DDI_USAGE_DYNAMIC &&
        pCreateResource->MapFlags == D3D10_DDI_CPU_ACCESS_WRITE &&
        pCreateResource->BindFlags != 0 && !wantsStream && !wantsRender) ||
       (pCreateResource->Usage == D3D10_DDI_USAGE_STAGING &&
        pCreateResource->BindFlags == 0 && pCreateResource->MapFlags != 0 &&
        !(pCreateResource->MapFlags & ~D3D10_DDI_CPU_ACCESS_MASK)));
   if (bufferResource && !validBufferUsage) {
      AgxD3d10WindowsDiagnosticBufferUsage(pCreateResource);
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
      if (!AgxD3d10WindowsIdentity(pDevice->windows,&resourceOwner,&resourceGeneration)) {
         AgxD3d10WindowsPresentationDestroy(
            pDevice->windows, &pResource->presentation);
         SetError(hDevice, E_FAIL);
         return;
      }
      pResource->owner_cookie = resourceOwner;
      pResource->device_generation = resourceGeneration;
      pResource->usage = pCreateResource->Usage;
      pResource->bind_flags = pCreateResource->BindFlags;
      pResource->cpu_access = pCreateResource->MapFlags;
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
   }''','''   if (bufferResource) {
      pResource->resource = screen->resource_create(screen, &templat);
   } else if (pCreateResource->BindFlags & D3D10_DDI_BIND_RENDER_TARGET) {
      const D3D10DDI_MIPINFO *mip = pCreateResource->pMipInfoList;
      /* D3D-G10a: the D3D10 render-target contract replaces the bring-up
       * admission (2D, one level, one slice, a format list). The format and
       * bind flags were checked against the screen above; dimensions and
       * byte size against the pinned D3D10 limits
       * (AgxD3d10ResourceWithinRequiredLimits). Any dimension, mip chain and
       * array size; multisampling only for single-level 2D (arrays too);
       * DEFAULT usage without CPU access; RT alone or with SRV;
       * AUTO_GEN_MIP_MAP only with RT|SRV; initial data only single-sampled. */
      bool autogen = pCreateResource->MiscFlags == D3D10_DDI_RESOURCE_AUTO_GEN_MIP_MAP;
      bool private_rt = mip && mip[0].TexelWidth > 0 && mip[0].TexelHeight > 0 &&
         mip[0].TexelDepth > 0 &&
         (pCreateResource->SampleDesc.Count == 1 ||
          ((pCreateResource->SampleDesc.Count == 2 || pCreateResource->SampleDesc.Count == 4) &&
           pCreateResource->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D &&
           pCreateResource->MipLevels == 1)) &&
         pCreateResource->SampleDesc.Quality == 0 &&
         pCreateResource->Usage == D3D10_DDI_USAGE_DEFAULT && pCreateResource->MapFlags == 0 &&
         /* D3D-H5: unordered access with a single-sampled render target */
         ((pCreateResource->BindFlags & ~D3D11_DDI_BIND_UNORDERED_ACCESS) ==
             D3D10_DDI_BIND_RENDER_TARGET ||
          (pCreateResource->BindFlags & ~D3D11_DDI_BIND_UNORDERED_ACCESS) ==
             (D3D10_DDI_BIND_RENDER_TARGET | D3D10_DDI_BIND_SHADER_RESOURCE)) &&
         (!(pCreateResource->BindFlags & D3D11_DDI_BIND_UNORDERED_ACCESS) ||
          pCreateResource->SampleDesc.Count == 1) &&
         (pCreateResource->MiscFlags == 0 ||
          (autogen && pCreateResource->BindFlags == (D3D10_DDI_BIND_RENDER_TARGET |
              D3D10_DDI_BIND_SHADER_RESOURCE))) &&
         !pCreateResource->pPrimaryDesc &&
         /* EXP1284: D3D allows initial data for render targets; the
          * upstream texture-map upload below writes it. Multisampled
          * targets take no initial data (D3D). */
         (!pCreateResource->pInitialDataUP || pCreateResource->SampleDesc.Count == 1);
      if (!private_rt || !screen->resource_create_with_modifiers) {
         LOG_UNSUPPORTED("A render target outside the D3D10 contract");
         SetError(hDevice, E_NOTIMPL);
         return;
      }
      const uint64_t modifier = DRM_FORMAT_MOD_APPLE_GPU_TILED;
      pResource->resource = screen->resource_create_with_modifiers(
         screen, &templat, &modifier, 1);
   } else if (pCreateResource->BindFlags & D3D10_DDI_BIND_DEPTH_STENCIL) {
      const D3D10DDI_MIPINFO *mip = pCreateResource->pMipInfoList;
      /* D3D-G10a: the D3D10 depth-stencil contract: 1D, 2D and cube
       * targets (arrays too) in any format the screen takes as depth/stencil
       * (typeless families included: FormatTranslate with the depth bind),
       * mip chains, multisampling for single-level 2D, DS alone or with SRV
       * (shadow maps), DEFAULT usage without CPU access or initial data. */
      bool private_depth = mip && mip[0].TexelWidth > 0 && mip[0].TexelHeight > 0 &&
         mip[0].TexelDepth == 1 &&
         (pCreateResource->ResourceDimension == D3D10DDIRESOURCE_TEXTURE1D ||
          pCreateResource->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D ||
          pCreateResource->ResourceDimension == D3D10DDIRESOURCE_TEXTURECUBE) &&
         util_format_is_depth_or_stencil(templat.format) &&
         (pCreateResource->SampleDesc.Count == 1 ||
          ((pCreateResource->SampleDesc.Count == 2 || pCreateResource->SampleDesc.Count == 4) &&
           pCreateResource->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D &&
           pCreateResource->MipLevels == 1)) &&
         pCreateResource->SampleDesc.Quality == 0 &&
         pCreateResource->Usage == D3D10_DDI_USAGE_DEFAULT &&
         pCreateResource->MapFlags == 0 &&
         (pCreateResource->BindFlags == D3D10_DDI_BIND_DEPTH_STENCIL ||
          pCreateResource->BindFlags == (D3D10_DDI_BIND_DEPTH_STENCIL |
              D3D10_DDI_BIND_SHADER_RESOURCE)) &&
         pCreateResource->MiscFlags == 0 && !pCreateResource->pPrimaryDesc &&
         !pCreateResource->pInitialDataUP;
      if (!private_depth || !screen->resource_create_with_modifiers) {
         LOG_UNSUPPORTED("A depth-stencil target outside the D3D10 contract");
         SetError(hDevice, E_NOTIMPL);
         return;
      }
      if(util_format_is_depth_and_stencil(templat.format)) {
         pResource->resource=AgxWin32AsahiCreateUncompressedDepthStencil(
            screen,&templat);
      } else {
         const uint64_t modifier = DRM_FORMAT_MOD_APPLE_GPU_TILED;
         pResource->resource = screen->resource_create_with_modifiers(
            screen, &templat, &modifier, 1);
      }
   } else if ((pCreateResource->BindFlags & D3D11_DDI_BIND_UNORDERED_ACCESS) &&
              pCreateResource->ResourceDimension != D3D10DDIRESOURCE_BUFFER) {
      /* D3D-H5: typed unordered-access textures (D3D11 runtime validated:
       * single-sampled, DEFAULT usage) are images: tiled, uncompressed, as
       * the render targets.  Buffers stay linear (EXP1297: a tiled buffer
       * has no Asahi modifier, creation returned NULL). */
      if (pCreateResource->SampleDesc.Count != 1 ||
          pCreateResource->Usage != D3D10_DDI_USAGE_DEFAULT || pCreateResource->MapFlags ||
          !screen->resource_create_with_modifiers) {
         SetError(hDevice, E_NOTIMPL);
         return;
      }
      const uint64_t modifier = DRM_FORMAT_MOD_APPLE_GPU_TILED;
      pResource->resource = screen->resource_create_with_modifiers(
         screen, &templat, &modifier, 1);
   } else if ((pCreateResource->ResourceDimension == D3D10DDIRESOURCE_TEXTURE1D ||
               pCreateResource->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D ||
               pCreateResource->ResourceDimension == D3D10DDIRESOURCE_TEXTURE3D ||
               pCreateResource->ResourceDimension == D3D10DDIRESOURCE_TEXTURECUBE) &&
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
   /* EXP841: textures need the same device identity as buffers so later DDIs
    * (UpdateSubresourceUP) can validate ownership; the buffer path already
    * fetched it while validating the buffer shape. */
   if (!bufferResource &&
       !AgxD3d10WindowsIdentity(pDevice->windows,&resourceOwner,&resourceGeneration)) {
      pipe_resource_reference(&pResource->resource, NULL);
      SetError(hDevice, E_FAIL);
      return;
   }
   pResource->owner_cookie = resourceOwner;
   pResource->device_generation = resourceGeneration;
   pResource->usage = pCreateResource->Usage;
   pResource->bind_flags = pCreateResource->BindFlags;
   pResource->cpu_access = pCreateResource->MapFlags;
   if (bufferResource) {
      pResource->constant_buffer = wantsConstant;
      pResource->index_buffer = wantsIndex;
      pResource->logical_bytes = resourceMip[0].TexelWidth;
      pResource->owner_device = pDevice;
      pResource->owner_cookie = resourceOwner;
      pResource->device_generation = resourceGeneration;
      if (pCreateResource->Usage == D3D10_DDI_USAGE_DYNAMIC) {
         pResource->dynamic_shadow = calloc(1, pResource->logical_bytes);
         if (!pResource->dynamic_shadow) {
            pipe_resource_reference(&pResource->resource, NULL);
            SetError(hDevice, E_OUTOFMEMORY);
            return;
         }
      }
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
    replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','ResourceResolveSubResource','''   Device *device = CastDevice(hDevice);
   if (device && !CheckPredicate(device)) return;
   Resource *destination = CastResource(hDstResource);
   Resource *source = CastResource(hSrcResource);
   struct pipe_resource *dst = destination ? destination->resource : NULL;
   struct pipe_resource *src = source ? source->resource : NULL;
   /* D3D-G5: one multisampled 2D subresource resolves into a single-sample
    * subresource of the same size; u_blitter averages the samples. */
   if (!device || !destination || !source || !dst || !src ||
       destination->owner_device != device || source->owner_device != device ||
       src->target != PIPE_TEXTURE_2D || dst->target != PIPE_TEXTURE_2D ||
       src->nr_samples < 2 || dst->nr_samples > 1 ||
       !source->MipLevels || !destination->MipLevels ||
       SrcSubResource >= source->NumSubResources ||
       DstSubResource >= destination->NumSubResources) {
      SetError(hDevice, E_INVALIDARG); return;
   }
   unsigned dstLevel = DstSubResource % destination->MipLevels;
   unsigned dstLayer = DstSubResource / destination->MipLevels;
   unsigned srcLayer = SrcSubResource / source->MipLevels;
   unsigned width = u_minify(dst->width0, dstLevel);
   unsigned height = u_minify(dst->height0, dstLevel);
   enum pipe_format format = FormatTranslate(ResolveFormat, FALSE);
   if (width != src->width0 || height != src->height0 ||
       format == PIPE_FORMAT_NONE) {
      SetError(hDevice, E_INVALIDARG); return;
   }
   struct pipe_blit_info info;
   memset(&info, 0, sizeof(info));
   info.src.resource = src; info.src.level = 0; info.src.format = format;
   info.src.box.z = (int)srcLayer; info.src.box.width = (int)width;
   info.src.box.height = (int)height; info.src.box.depth = 1;
   info.dst.resource = dst; info.dst.level = dstLevel; info.dst.format = format;
   info.dst.box.z = (int)dstLayer; info.dst.box.width = (int)width;
   info.dst.box.height = (int)height; info.dst.box.depth = 1;
   info.mask = PIPE_MASK_RGBA;
   info.filter = PIPE_TEX_FILTER_NEAREST;
   device->pipe->blit(device->pipe, &info);''')
    replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','ResourceCopy','''   Device *device = CastDevice(hDevice);
   if (device && !CheckPredicate(device)) return;
   Resource *destination = CastResource(hDstResource);
   Resource *source = CastResource(hSrcResource);
   if (!device || !destination || !source || destination == source ||
       destination->owner_device != device || source->owner_device != device ||
       destination->usage == D3D10_DDI_USAGE_IMMUTABLE ||
       destination->MipLevels != source->MipLevels ||
       destination->NumSubResources != source->NumSubResources) {
      SetError(hDevice, E_NOTIMPL); return;
   }
   for (unsigned i = 0; i < destination->NumSubResources; ++i) {
      if ((destination->transfers && destination->transfers[i]) ||
          (source->transfers && source->transfers[i])) {
         SetError(hDevice, E_NOTIMPL); return;
      }
   }
   struct pipe_resource *dst=destination->resource,*src=source->resource;
   if (dst && src && destination->buffer && source->buffer) {
      if (dst->target!=PIPE_BUFFER || src->target!=PIPE_BUFFER ||
          dst->width0!=src->width0) {
         SetError(hDevice,E_INVALIDARG);return;
      }
      HRESULT result=AgxD3d10WindowsFlushStatus(device->windows);
      if(SUCCEEDED(result)) result=AgxD3d10WindowsFlushRetire(device->windows);
      if(SUCCEEDED(result) && !AgxWin32AsahiBufferCopy(
           device->pipe,dst,0,src,0,src->width0)) result=E_FAIL;
      if(FAILED(result)) SetError(hDevice,result);
      return;
   }
   /* D3D10 ResourceCopy: identical type, dimensions, mip count, array size
    * and sample count; formats of one copy group. Copy every subresource. */
   enum pipe_format raw = dst && src ?
      AgxD3d10CopyRawFormat(source, destination, src, dst) : PIPE_FORMAT_NONE;
   bool compressed = dst && src && src->format == dst->format &&
      util_format_is_compressed(src->format);
   if (!dst || !src || (raw==PIPE_FORMAT_NONE && !compressed) ||
       dst->target!=src->target || dst->target==PIPE_BUFFER ||
       dst->width0!=src->width0 || dst->height0!=src->height0 ||
       dst->depth0!=src->depth0 || dst->array_size!=src->array_size ||
       dst->last_level!=src->last_level ||
       /* D3D-G10b: multisampled resources copy at their sample count. */
       MAX2(dst->nr_samples,1)!=MAX2(src->nr_samples,1) ||
       (compressed && src->nr_samples>1)) {
      UINT values[14]={(UINT)source->Format,(UINT)destination->Format,
         src?(UINT)src->target:~0u,dst?(UINT)dst->target:~0u,
         src?src->width0:0u,src?src->height0:0u,dst?dst->width0:0u,dst?dst->height0:0u,
         src?(UINT)src->last_level:0u,src?(UINT)src->array_size:0u,
         src?(UINT)src->nr_samples:0u,dst?(UINT)dst->nr_samples:0u,
         (UINT)(source->presentation!=NULL),(UINT)(destination->presentation!=NULL)};
      AgxD3d10WindowsDiagnostic("reject-resource-copy",E_NOTIMPL,values,14u);
      SetError(hDevice, E_NOTIMPL); return;
   }
   HRESULT result;
   if (destination->presentation && source->presentation) {
      result=AgxD3d10WindowsPresentationBlt(device->windows,
         destination->presentation,source->presentation);
   } else {
      /* Copy stored bits independent of any bound view: linear (non-sRGB)
       * views avoid conversion; BGRA also preserves the X byte. */
      unsigned layers=dst->target==PIPE_TEXTURE_3D ? 1u : dst->array_size;
      for (unsigned layer=0; layer<layers; ++layer) {
         for (unsigned level=0; level<=dst->last_level; ++level) {
            if (compressed) {
               struct pipe_box box;
               u_box_3d(0,0,dst->target==PIPE_TEXTURE_3D ? 0 : (int)layer,
                        u_minify(src->width0,level),u_minify(src->height0,level),
                        dst->target==PIPE_TEXTURE_3D ? u_minify(src->depth0,level) : 1,&box);
               device->pipe->resource_copy_region(device->pipe,dst,level,0,0,
                  dst->target==PIPE_TEXTURE_3D ? 0 : layer,src,level,&box);
               continue;
            }
            struct pipe_blit_info info={};
            info.src.resource=src;info.src.format=raw;info.src.level=level;
            info.dst.resource=dst;info.dst.format=raw;info.dst.level=level;
            info.src.box.z=info.dst.box.z=(int)layer;
            info.src.box.width=info.dst.box.width=u_minify(src->width0,level);
            info.src.box.height=info.dst.box.height=u_minify(src->height0,level);
            info.src.box.depth=info.dst.box.depth=
               dst->target==PIPE_TEXTURE_3D ? u_minify(src->depth0,level) : 1;
            info.mask=util_format_get_mask(raw);info.filter=PIPE_TEX_FILTER_NEAREST;
            device->pipe->blit(device->pipe,&info);
         }
      }
      device->pipe->flush(device->pipe,NULL,0);
      result=AgxD3d10WindowsFlushStatus(device->windows);
   }
   if (FAILED(result)) SetError(hDevice,result);''')
    replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','ResourceCopyRegion','''   Device *device=CastDevice(hDevice);
   if(device && !CheckPredicate(device)) return;
   Resource *source=CastResource(hSrcResource),*destination=CastResource(hDstResource);
   struct pipe_resource *src=source?source->resource:NULL;
   struct pipe_resource *dst=destination?destination->resource:NULL;
   if(device && source && destination && source!=destination &&
      source->owner_device==device && destination->owner_device==device &&
      source->buffer && destination->buffer && src && dst &&
      source->NumSubResources==1 && destination->NumSubResources==1 &&
      destination->usage!=D3D10_DDI_USAGE_IMMUTABLE && !SrcSubResource &&
      !DstSubResource && !DstY && !DstZ) {
      UINT left=pSrcBox?pSrcBox->left:0,right=pSrcBox?pSrcBox->right:src->width0;
      bool valid=left<=right && right<=src->width0 && DstX<=dst->width0 &&
         right-left<=dst->width0-DstX &&
         (!pSrcBox || (!pSrcBox->top && !pSrcBox->front &&
          pSrcBox->bottom==1 && pSrcBox->back==1));
      if(!valid){SetError(hDevice,E_INVALIDARG);return;}
      HRESULT result=AgxD3d10WindowsFlushStatus(device->windows);
      if(SUCCEEDED(result)) result=AgxD3d10WindowsFlushRetire(device->windows);
      UINT bytes=right-left;
      if(SUCCEEDED(result) && bytes && !AgxWin32AsahiBufferCopy(
           device->pipe,dst,DstX,src,left,bytes)) result=E_FAIL;
      if(FAILED(result)) SetError(hDevice,result);
      return;
   }
   /* D3D10 ResourceCopyRegion for textures: copy the source box of one
    * subresource to (DstX,DstY,DstZ) of another; shapes may differ. */
   if (!device || !source || !destination || !src || !dst ||
       source->owner_device != device || destination->owner_device != device ||
       source->buffer || destination->buffer ||
       destination->usage == D3D10_DDI_USAGE_IMMUTABLE ||
       !source->MipLevels || !destination->MipLevels ||
       SrcSubResource >= source->NumSubResources ||
       DstSubResource >= destination->NumSubResources) {
      SetError(hDevice, E_NOTIMPL); return;
   }
   if ((destination->transfers && destination->transfers[DstSubResource]) ||
       (source->transfers && source->transfers[SrcSubResource])) {
      SetError(hDevice, E_NOTIMPL); return;
   }
   unsigned srcLevel = SrcSubResource % source->MipLevels;
   unsigned srcLayer = SrcSubResource / source->MipLevels;
   unsigned dstLevel = DstSubResource % destination->MipLevels;
   unsigned dstLayer = DstSubResource / destination->MipLevels;
   enum pipe_format raw = AgxD3d10CopyRawFormat(source, destination, src, dst);
   bool compressed = src->format == dst->format && util_format_is_compressed(src->format);
   unsigned srcW = u_minify(src->width0, srcLevel), srcH = u_minify(src->height0, srcLevel);
   unsigned srcD = src->target == PIPE_TEXTURE_3D ? u_minify(src->depth0, srcLevel) : 1;
   unsigned dstW = u_minify(dst->width0, dstLevel), dstH = u_minify(dst->height0, dstLevel);
   unsigned dstD = dst->target == PIPE_TEXTURE_3D ? u_minify(dst->depth0, dstLevel) : 1;
   UINT left = pSrcBox ? pSrcBox->left : 0, right = pSrcBox ? pSrcBox->right : srcW;
   UINT top = pSrcBox ? pSrcBox->top : 0, bottom = pSrcBox ? pSrcBox->bottom : srcH;
   UINT front = pSrcBox ? pSrcBox->front : 0, back = pSrcBox ? pSrcBox->back : srcD;
   if (pSrcBox && (right <= left || bottom <= top || back <= front))
      return; /* empty box: D3D no-op */
   bool valid = (raw != PIPE_FORMAT_NONE || compressed) &&
      AgxD3d10CopyTargetFamily(src->target) &&
      AgxD3d10CopyTargetFamily(src->target) == AgxD3d10CopyTargetFamily(dst->target) &&
      src->nr_samples <= 1 && dst->nr_samples <= 1 &&
      right <= srcW && bottom <= srcH && back <= srcD &&
      DstX <= dstW && right - left <= dstW - DstX &&
      DstY <= dstH && bottom - top <= dstH - DstY &&
      DstZ <= dstD && back - front <= dstD - DstZ &&
      srcLayer < src->array_size && dstLayer < dst->array_size;
   if (!valid) {
      UINT values[14]={(UINT)source->Format,(UINT)destination->Format,
         (UINT)src->target,(UINT)dst->target,srcW,srcH,dstW,dstH,
         SrcSubResource,DstSubResource,right-left,bottom-top,DstX,DstY};
      AgxD3d10WindowsDiagnostic("reject-resource-copy-region",E_NOTIMPL,values,14u);
      SetError(hDevice, E_NOTIMPL); return;
   }
   if (compressed) {
      struct pipe_box box;
      u_box_3d((int)left,(int)top,(int)(src->target==PIPE_TEXTURE_3D ? front : srcLayer),
               (int)(right-left),(int)(bottom-top),(int)(back-front),&box);
      device->pipe->resource_copy_region(device->pipe,dst,dstLevel,DstX,DstY,
         dst->target==PIPE_TEXTURE_3D ? DstZ : dstLayer,src,srcLevel,&box);
   } else {
   struct pipe_blit_info info={};
   info.src.resource=src;info.src.format=raw;info.src.level=srcLevel;
   info.dst.resource=dst;info.dst.format=raw;info.dst.level=dstLevel;
   info.src.box.x=(int)left;info.src.box.y=(int)top;
   info.src.box.z=(int)(src->target==PIPE_TEXTURE_3D ? front : srcLayer);
   info.dst.box.x=(int)DstX;info.dst.box.y=(int)DstY;
   info.dst.box.z=(int)(dst->target==PIPE_TEXTURE_3D ? DstZ : dstLayer);
   info.src.box.width=info.dst.box.width=(int)(right-left);
   info.src.box.height=info.dst.box.height=(int)(bottom-top);
   info.src.box.depth=info.dst.box.depth=(int)(back-front);
   info.mask=util_format_get_mask(raw);info.filter=PIPE_TEX_FILTER_NEAREST;
   device->pipe->blit(device->pipe,&info);
   }
   device->pipe->flush(device->pipe,NULL,0);
   HRESULT result=AgxD3d10WindowsFlushStatus(device->windows);
   if (FAILED(result)) SetError(hDevice,result);''')
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
   /* EXP1327: the creator's DXGI format (A8_UNORM shares the RGBA8
    * storage), so views are checked against the format it was created
    * with. */
   pResource->Format = (DXGI_FORMAT)AgxD3d10WindowsPresentationFormat(
      pResource->presentation);
   pResource->usage = D3D10_DDI_USAGE_DEFAULT;
   pResource->bind_flags = D3D10_DDI_BIND_RENDER_TARGET | D3D10_DDI_BIND_SHADER_RESOURCE;
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

   free(pResource->dynamic_shadow);
   pResource->dynamic_shadow = NULL;
   pResource->active_buffer_map = NULL;
   if (pResource->client_maps) {
      for (UINT i = 0; i < pResource->NumSubResources; ++i)
         free(((struct AgxD3d10ClientMap *)pResource->client_maps)[i].client);
      free(pResource->client_maps);
      pResource->client_maps = NULL;
   }
   if (pResource->so_target) {'''
    if resource_text.count(destroy_anchor)!=1:
        raise SystemExit('Ambiguous presentation DestroyResource anchor')
    resource_path.write_text(resource_text.replace(destroy_anchor,destroy_replacement))
    overlays['src/gallium/frontends/d3d10umd/Resource.cpp']['after']=hashlib.sha256(resource_path.read_bytes()).hexdigest()
    replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','ResourceMap','''   Device *device = CastDevice(hDevice);
   Resource *resource = CastResource(hResource);
   bool dynamicBuffer = resource && resource->buffer &&
      resource->usage == D3D10_DDI_USAGE_DYNAMIC;
   bool dynamicTexture = resource && !resource->buffer &&
      resource->usage == D3D10_DDI_USAGE_DYNAMIC &&
      resource->bind_flags == D3D10_DDI_BIND_SHADER_RESOURCE &&
      AgxD3d10MapFormat(resource->Format);
   bool stagingResource = resource && resource->usage == D3D10_DDI_USAGE_STAGING &&
      resource->bind_flags == 0;
   bool stagingAccess = stagingResource &&
      ((DDIMap == D3D10_DDI_MAP_READ &&
        (resource->cpu_access & D3D10_DDI_CPU_ACCESS_READ)) ||
       (DDIMap == D3D10_DDI_MAP_WRITE &&
        (resource->cpu_access & D3D10_DDI_CPU_ACCESS_WRITE)) ||
       (DDIMap == D3D10_DDI_MAP_READWRITE &&
        (resource->cpu_access & D3D10_DDI_CPU_ACCESS_MASK) ==
          D3D10_DDI_CPU_ACCESS_MASK));
   /* D3D11.1 DDI: the runtime reports ConstantBufferOffsetting with
    * MapNoOverwriteOnDynamicConstantBuffer (one grouping alongside ClearView)
    * and MapNoOverwriteOnDynamicBufferSRV with
    * MultisampleRTVWithForcedSampleCountOne, then sends WRITE_NOOVERWRITE for
    * dynamic constant and shader-resource buffers too (EXP1325: 5760 maps
    * refused on the FL 11_0 desktop). Every dynamic buffer is backed the same
    * way, so NO_OVERWRITE maps its current BO whatever it is bound as; the
    * runtime validates the API rules. */
   bool mapMode = dynamicBuffer ?
      (DDIMap == D3D10_DDI_MAP_WRITE_DISCARD ||
       DDIMap == D3D10_DDI_MAP_WRITE_NOOVERWRITE) :
      dynamicTexture ? DDIMap == D3D10_DDI_MAP_WRITE_DISCARD :
      stagingAccess;
   if (!device || !resource || resource->owner_device != device ||
       !resource->resource || !resource->transfers ||
       (!resource->buffer && !AgxD3d10MapFormat(resource->Format)) ||
       SubResource >= resource->NumSubResources ||
       (Flags & ~D3D10_DDI_MAP_FLAG_DONOTWAIT) || !pMappedSubResource || !mapMode ||
       (Flags && (DDIMap == D3D10_DDI_MAP_WRITE_DISCARD ||
                  DDIMap == D3D10_DDI_MAP_WRITE_NOOVERWRITE)) ||
       resource->transfers[SubResource]) {
      /* EXP1325 receipt-only: the FL 11_0 desktop refused 5760 maps here;
       * the resource and request show which rule fails. */
      static volatile LONG mapRefusals;
      if (InterlockedIncrement(&mapRefusals) <= 32) {
         UINT values[8] = {resource ? (UINT)resource->usage : ~0u,
            resource ? (UINT)resource->bind_flags : 0u,
            resource ? (UINT)resource->cpu_access : 0u,
            resource ? (UINT)resource->Format : 0u,
            (UINT)DDIMap | ((UINT)Flags << 8) |
               (resource && resource->buffer ? 0x10000u : 0u) |
               (resource && resource->constant_buffer ? 0x20000u : 0u) |
               (resource && resource->transfers && SubResource < resource->NumSubResources &&
                resource->transfers[SubResource] ? 0x40000u : 0u) |
               (resource && resource->owner_device != device ? 0x80000u : 0u),
            SubResource, resource ? resource->NumSubResources : 0u,
            (dynamicBuffer ? 1u : 0u) | (dynamicTexture ? 2u : 0u) |
               (stagingResource ? 4u : 0u) | (mapMode ? 8u : 0u)};
         AgxD3d10WindowsDiagnostic("reject-map", E_INVALIDARG, values, 8u);
      }
      SetError(hDevice, E_INVALIDARG); return;
   }
   pMappedSubResource->pData=NULL;
   pMappedSubResource->RowPitch=pMappedSubResource->DepthPitch=0;
   HRESULT status = DDIMap == D3D10_DDI_MAP_WRITE_NOOVERWRITE ? S_OK :
      (Flags & D3D10_DDI_MAP_FLAG_DONOTWAIT) ?
       AgxD3d10WindowsTryFlushRetire(device->windows) :
       AgxD3d10WindowsFlushRetire(device->windows);
   if (FAILED(status)) { SetError(hDevice, status); return; }
   struct pipe_box box;
   unsigned level = 0;
   subResourceBox(resource->resource,SubResource,&level,&box);
   if (DDIMap == D3D10_DDI_MAP_WRITE_NOOVERWRITE) {
      if (!resource->dynamic_shadow || resource->logical_bytes != (UINT)box.width) {
         SetError(hDevice, E_FAIL); return;
      }
      void *current = AgxWin32AsahiBufferCurrentMap(
         device->pipe,resource->resource);
      /* EXP1090: map the BO itself (unsynchronized, as NO_OVERWRITE permits)
       * instead of handing out the CPU shadow: keeping that shadow current
       * cost a full-buffer copy at every Unmap (EXP1089: 29 % of DWM's
       * composition thread in memcpy under ResourceUnmap). */
      if (!current)
         current = AgxWin32AsahiBufferWriteMap(device->pipe,resource->resource);
      if (current) {
         resource->active_buffer_map=current;
         resource->direct_buffer_map=true;
         pMappedSubResource->pData=current;
         pMappedSubResource->RowPitch=resource->logical_bytes;
         pMappedSubResource->DepthPitch=resource->logical_bytes;
         return;
      }
      /* The shadow no longer mirrors direct writes; never hand it out stale. */
      if (resource->shadow_stale) { SetError(hDevice, E_OUTOFMEMORY); return; }
      resource->active_buffer_map=resource->dynamic_shadow;
      resource->shadow_only_map=true;
      pMappedSubResource->pData=resource->dynamic_shadow;
      pMappedSubResource->RowPitch=resource->logical_bytes;
      pMappedSubResource->DepthPitch=resource->logical_bytes;
      return;
   }
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
   if (dynamicBuffer) resource->active_buffer_map=map;
   pMappedSubResource->RowPitch=resource->transfers[SubResource]->stride;
   pMappedSubResource->DepthPitch=resource->transfers[SubResource]->layer_stride;
   if (!resource->buffer) {
      void *client=NULL;
      UINT clientRow=0,clientDepth=0;
      HRESULT clientStatus=AgxD3d10ClientMapBegin(resource,SubResource,map,
         resource->transfers[SubResource],box.width,box.height,box.depth,
         (usage&PIPE_MAP_READ)!=0,(usage&PIPE_MAP_WRITE)!=0,
         &client,&clientRow,&clientDepth);
      if (FAILED(clientStatus)) {
         pipe_texture_unmap(device->pipe,resource->transfers[SubResource]);
         resource->transfers[SubResource]=NULL;
         pMappedSubResource->pData=NULL;
         pMappedSubResource->RowPitch=pMappedSubResource->DepthPitch=0;
         SetError(hDevice,clientStatus);return;
      }
      if (client) {
         pMappedSubResource->pData=client;
         pMappedSubResource->RowPitch=clientRow;
         pMappedSubResource->DepthPitch=clientDepth;
      }
   }''')
    replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','ResourceUnmap','''   Device *device = CastDevice(hDevice);
   Resource *resource = CastResource(hResource);
   if (!device || !resource || resource->owner_device != device ||
       !resource->resource || !resource->transfers ||
       (!resource->buffer && !AgxD3d10MapFormat(resource->Format)) ||
       SubResource >= resource->NumSubResources ||
       (!resource->transfers[SubResource] && !resource->direct_buffer_map &&
        !resource->shadow_only_map)) {
      SetError(hDevice, E_INVALIDARG); return;
   }
   if (resource->buffer) {
      /* EXP1090: direct writes are not copied back into the shadow (a full
       * logical_bytes memcpy per Unmap); the shadow is marked stale instead
       * and is only used again as the fallback of an unmappable BO. */
      if (resource->dynamic_shadow && resource->active_buffer_map &&
          resource->active_buffer_map != resource->dynamic_shadow)
         resource->shadow_stale = true;
      resource->active_buffer_map = NULL;
      if (resource->shadow_only_map) {
         /* EXP1033: publish the shadow now. Copies, index-buffer binds and
          * already-bound vertex buffers read the BO, not the shadow. */
         resource->shadow_only_map=false;
         void *map=AgxWin32AsahiBufferCurrentMap(device->pipe,resource->resource);
         if(!map) map=AgxWin32AsahiBufferWriteMap(device->pipe,resource->resource);
         if(map) memcpy(map,resource->dynamic_shadow,resource->logical_bytes);
         resource->shadow_dirty=map==NULL;
         if(!map) SetError(hDevice,E_OUTOFMEMORY);
      } else if (resource->direct_buffer_map) resource->direct_buffer_map = false;
      else pipe_buffer_unmap(device->pipe,resource->transfers[SubResource]);
   } else {
      AgxD3d10ClientMapEnd(resource,SubResource);
      pipe_texture_unmap(device->pipe,resource->transfers[SubResource]);
   }
   resource->transfers[SubResource]=NULL;''')
    replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','ResourceIsStagingBusy','''   Device *device=CastDevice(hDevice);
   Resource *resource=CastResource(hResource);
   if(!device || !resource || resource->owner_device!=device || !resource->resource)
      return TRUE;
   return AgxWin32AsahiResourceBusy(device->pipe,resource->resource) ? TRUE : FALSE;''')
    replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','ResourceUpdateSubResourceUP','''   Device *pDevice = CastDevice(hDevice);
   if (pDevice && !CheckPredicate(pDevice)) return;
   Resource *resource = CastResource(hDstResource);
   ULONGLONG owner = 0;
   ULONG generation = 0;
   /* EXP840: DWM updates textures and boxed buffer ranges, not only whole
    * constant buffers. Follow upstream Mesa (level/layer from the subresource,
    * box or full extent, map and util_copy_rect) with explicit bounds and
    * ownership checks instead of assert. An empty box is a D3D no-op. */
   bool identity = pDevice && AgxD3d10WindowsIdentity(
      pDevice->windows, &owner, &generation);
   struct pipe_resource *dst = resource ? resource->resource : NULL;
   if (!pDevice || !resource || !dst || !pSysMemUP || !identity ||
       resource->owner_device != pDevice ||
       resource->owner_cookie != owner || resource->device_generation != generation) {
      SetError(hDevice, E_INVALIDARG);
      return;
   }
   unsigned mip_levels = dst->last_level + 1;
   unsigned level = DstSubResource % mip_levels;
   unsigned layer = DstSubResource / mip_levels;
   unsigned layers = dst->target == PIPE_TEXTURE_3D ? 1 : dst->array_size;
   if (layer >= layers) {
      SetError(hDevice, E_INVALIDARG);
      return;
   }
   unsigned width = u_minify(dst->width0, level);
   unsigned height = u_minify(dst->height0, level);
   unsigned depth = dst->target == PIPE_TEXTURE_3D ? u_minify(dst->depth0, level) : 1;
   struct pipe_box box;
   if (pDstBox) {
      if (pDstBox->right <= pDstBox->left || pDstBox->bottom <= pDstBox->top ||
          pDstBox->back <= pDstBox->front)
         return;
      if (pDstBox->right > width || pDstBox->bottom > height || pDstBox->back > depth) {
         SetError(hDevice, E_INVALIDARG);
         return;
      }
      box.x = pDstBox->left;
      box.y = pDstBox->top;
      box.z = pDstBox->front + layer;
      box.width = pDstBox->right - pDstBox->left;
      box.height = pDstBox->bottom - pDstBox->top;
      box.depth = pDstBox->back - pDstBox->front;
   } else {
      box.x = 0;
      box.y = 0;
      box.z = layer;
      box.width = width;
      box.height = height;
      box.depth = depth;
   }
   /* EXP1040: Asahi's transfer map synchronises with batches that use the
    * resource (shadow or reader sync); no device-wide flush/retire. */
   struct pipe_transfer *transfer = NULL;
   void *map = dst->target == PIPE_BUFFER ?
      pDevice->pipe->buffer_map(pDevice->pipe, dst, level,
                                PIPE_MAP_WRITE | PIPE_MAP_DISCARD_RANGE, &box, &transfer) :
      pDevice->pipe->texture_map(pDevice->pipe, dst, level,
                                 PIPE_MAP_WRITE | PIPE_MAP_DISCARD_RANGE, &box, &transfer);
   if (!map || !transfer) {
      SetError(hDevice, E_OUTOFMEMORY);
      return;
   }
   enum pipe_format client = AgxD3d10ClientFormat(resource);
   for (int z = 0; client != PIPE_FORMAT_NONE && z < box.depth; ++z) {
      if (!util_format_translate(dst->format,
             (uint8_t *)map + z * transfer->layer_stride, transfer->stride, 0, 0,
             client, (const uint8_t *)pSysMemUP + z * DepthPitch, RowPitch, 0, 0,
             box.width, box.height))
         SetError(hDevice, E_NOTIMPL);
   }
   for (int z = 0; client == PIPE_FORMAT_NONE && z < box.depth; ++z) {
      util_copy_rect((uint8_t *)map + z * transfer->layer_stride, dst->format,
                     transfer->stride, 0, 0, box.width, box.height,
                     (const uint8_t *)pSysMemUP + z * DepthPitch, RowPitch, 0, 0);
   }
   if (dst->target == PIPE_BUFFER)
      pipe_buffer_unmap(pDevice->pipe, transfer);
   else
      pipe_texture_unmap(pDevice->pipe, transfer);''')
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
   AgxD3d10RefusalScope refusal("reject-CreateResource",refusalArgs,16);
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
    # EXP1032 diagnostic: opt-in per-call DDI trace ("ddi-" stages are only
    # written when the process sets APPLE_AGX_UMD_DDI_TRACE=1).
    H='(UINT)(UINT_PTR)'
    ddi_traces = {
        'src/gallium/frontends/d3d10umd/Resource.cpp': {
            'CreateResource': 'UINT v[8]={'+H+'hResource.pDrvPrivate,pCreateResource?(UINT)pCreateResource->Format:~0u,pCreateResource?(UINT)pCreateResource->Usage:~0u,pCreateResource?pCreateResource->BindFlags:~0u,pCreateResource?pCreateResource->MapFlags:~0u,(pCreateResource&&pCreateResource->pMipInfoList&&pCreateResource->MipLevels)?pCreateResource->pMipInfoList[0].TexelWidth:0u,(pCreateResource&&pCreateResource->pMipInfoList&&pCreateResource->MipLevels)?pCreateResource->pMipInfoList[0].TexelHeight:0u,pCreateResource&&pCreateResource->pInitialDataUP}; AgxD3d10WindowsDiagnostic("ddi-CreateResource",S_OK,v,8u);',
            'DestroyResource': 'UINT v[1]={'+H+'hResource.pDrvPrivate}; AgxD3d10WindowsDiagnostic("ddi-DestroyResource",S_OK,v,1u);',
            'ResourceMap': 'UINT v[4]={'+H+'hResource.pDrvPrivate,SubResource,(UINT)DDIMap,Flags}; AgxD3d10WindowsDiagnostic("ddi-ResourceMap",S_OK,v,4u);',
            'ResourceUnmap': 'UINT v[2]={'+H+'hResource.pDrvPrivate,SubResource}; AgxD3d10WindowsDiagnostic("ddi-ResourceUnmap",S_OK,v,2u);',
            'ResourceCopy': 'UINT v[2]={'+H+'hDstResource.pDrvPrivate,'+H+'hSrcResource.pDrvPrivate}; AgxD3d10WindowsDiagnostic("ddi-ResourceCopy",S_OK,v,2u);',
            'ResourceCopyRegion': 'UINT v[11]={'+H+'hDstResource.pDrvPrivate,DstSubResource,DstX,DstY,'+H+'hSrcResource.pDrvPrivate,SrcSubResource,pSrcBox!=NULL,pSrcBox?(UINT)pSrcBox->left:0u,pSrcBox?(UINT)pSrcBox->top:0u,pSrcBox?(UINT)pSrcBox->right:0u,pSrcBox?(UINT)pSrcBox->bottom:0u}; AgxD3d10WindowsDiagnostic("ddi-ResourceCopyRegion",S_OK,v,11u);',
            'ResourceUpdateSubResourceUP': 'UINT v[9]={'+H+'hDstResource.pDrvPrivate,DstSubResource,pDstBox!=NULL,pDstBox?(UINT)pDstBox->left:0u,pDstBox?(UINT)pDstBox->top:0u,pDstBox?(UINT)pDstBox->right:0u,pDstBox?(UINT)pDstBox->bottom:0u,RowPitch,DepthPitch}; AgxD3d10WindowsDiagnostic("ddi-UpdateSubResourceUP",S_OK,v,9u);',
        },
        'src/gallium/frontends/d3d10umd/Draw.cpp': {
            'Draw': 'UINT v[2]={VertexCount,StartVertexLocation}; AgxD3d10WindowsDiagnostic("ddi-Draw",S_OK,v,2u);',
            'DrawIndexed': 'UINT v[3]={IndexCount,StartIndexLocation,(UINT)BaseVertexLocation}; AgxD3d10WindowsDiagnostic("ddi-DrawIndexed",S_OK,v,3u);',
            'DrawInstanced': 'UINT v[4]={VertexCountPerInstance,InstanceCount,StartVertexLocation,StartInstanceLocation}; AgxD3d10WindowsDiagnostic("ddi-DrawInstanced",S_OK,v,4u);',
            'DrawIndexedInstanced': 'UINT v[5]={IndexCountPerInstance,InstanceCount,StartIndexLocation,(UINT)BaseVertexLocation,StartInstanceLocation}; AgxD3d10WindowsDiagnostic("ddi-DrawIndexedInstanced",S_OK,v,5u);',
        },
        'src/gallium/frontends/d3d10umd/Shader.cpp': {
            'SetShaderResources': 'UINT v[6]={(UINT)shader_type,Offset,NumViews,(NumViews>0u&&phShaderResourceViews)?'+H+'phShaderResourceViews[0].pDrvPrivate:0u,(NumViews>1u&&phShaderResourceViews)?'+H+'phShaderResourceViews[1].pDrvPrivate:0u,(NumViews>2u&&phShaderResourceViews)?'+H+'phShaderResourceViews[2].pDrvPrivate:0u}; AgxD3d10WindowsDiagnostic("ddi-SetShaderResources",S_OK,v,6u);',
            'CreateShaderResourceView': 'UINT v[4]={'+H+'hShaderResourceView.pDrvPrivate,pCreateSRView?'+H+'pCreateSRView->hDrvResource.pDrvPrivate:0u,pCreateSRView?(UINT)pCreateSRView->Format:~0u,pCreateSRView?(UINT)pCreateSRView->ResourceDimension:~0u}; AgxD3d10WindowsDiagnostic("ddi-CreateShaderResourceView",S_OK,v,4u);',
        },
    }
    ddi_prototype='\nextern "C" VOID AgxD3d10WindowsDiagnostic(PCSTR Stage, HRESULT Status,\n                              const UINT *Values, UINT Count);\n'
    for ddi_file, ddi_entries in ddi_traces.items():
        ddi_path=out/ddi_file
        text=ddi_path.read_text()
        for name, entry in ddi_entries.items():
            marker='\n'+name+'('
            if text.count(marker)!=1: raise SystemExit('Ambiguous DDI trace: '+name)
            start=text.index('{',text.index(marker))+1
            text=text[:start]+'\n   { '+entry+' }\n'+text[start:]
        last=text.rindex('\n#include')
        last=text.index('\n',last+1)
        text=text[:last]+ddi_prototype+text[last:]
        ddi_path.write_text(text)
        overlays[ddi_file]['after']=hashlib.sha256(ddi_path.read_bytes()).hexdigest()
    change('src/gallium/frontends/d3d10umd/InputAssembly.cpp',
        '210b330c3327042d230a65ff5a7242df89ddb385d50f61bcacfc996d39c55bbd',[
        ('#include "State.h"', '#include "State.h"\n#include "agx_win32_asahi_scene.h"'),
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
    ia_path=out/'src/gallium/frontends/d3d10umd/InputAssembly.cpp'
    ia_text=ia_path.read_text()
    ia_anchor='''      if (resource) {
         pDevice->vertex_strides[StartBuffer + i] = pStrides[i];'''
    ia_replacement='''      if (resource) {
         if (res && res->shadow_dirty) {
            HRESULT status=AgxD3d10WindowsFlushRetire(pDevice->windows);
            void *map=SUCCEEDED(status) ? AgxWin32AsahiBufferCurrentMap(
               pDevice->pipe,res->resource) : NULL;
            if(SUCCEEDED(status) && !map)
               map=AgxWin32AsahiBufferWriteMap(pDevice->pipe,res->resource);
            if(FAILED(status) || !map) {
               SetError(hDevice,FAILED(status)?status:E_FAIL);return;
            }
            memcpy(map,res->dynamic_shadow,res->logical_bytes);
            res->shadow_dirty=false;
         }
         pDevice->vertex_strides[StartBuffer + i] = pStrides[i];'''
    if ia_text.count(ia_anchor)!=1: raise SystemExit('ambiguous dynamic IA upload')
    ia_path.write_text(ia_text.replace(ia_anchor,ia_replacement))
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
   UINT indexSize = Format == DXGI_FORMAT_R16_UINT ? 2u :
      Format == DXGI_FORMAT_R32_UINT ? 4u : 0u;
   bool valid = pDevice && indexSize && !(Offset & (indexSize-1u)) &&
      resource->index_buffer && Offset < resource->logical_bytes &&
      resource->logical_bytes-Offset >= indexSize &&
      resource->owner_device == pDevice && resource->resource &&
      resource->resource->target == PIPE_BUFFER &&
      (resource->resource->bind & PIPE_BIND_INDEX_BUFFER) &&
      !(resource->resource->bind &
        ~(PIPE_BIND_INDEX_BUFFER | PIPE_BIND_SHADER_IMAGE)) &&
      resource->resource->width0 == resource->logical_bytes &&
      AgxD3d10WindowsIdentity(pDevice->windows,&owner,&generation) &&
      resource->owner_cookie == owner &&
      resource->device_generation == generation;
   if (!valid) {
      SetError(hDevice, E_NOTIMPL);
      return;
   }
   pDevice->ib_offset = Offset;
   pDevice->index_size = indexSize;
   /* EXP1031: the cut index is all ones for the bound index size (upstream
    * Mesa d3d10umd). With 0, primitive restart cut every primitive using
    * vertex 0: hardware probe DrawIndexed drew 240/1024 px, BaseVertex 0. */
   pDevice->restart_index = indexSize == 2u ? 0xffffu : 0xffffffffu;
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
             'struct agx_bo *bo = AgxWin32AsahiEncoderCreate(dev, 0x80000, 0, "Encoder");\n   if (!bo) return (struct agx_encoder){0};'),
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
        # EXP1319: on the GPUVA winsys there is no graph to capture -- the
        # scratch buffer reaches the firmware through the render command's
        # fragment helper (agx_flush_render), not through a USC relocation --
        # and the rejection returned pipeline address 0, which hung the GPU
        # for every shader that spills (agx_scratch_smoke n=24,
        # dwmcore.dll_003d8800). Asahi's allocation is kept there; vertex
        # and compute helpers stay refused at the batch gates.
        scratch_start=pipeline.index('   if (max_scratch_size > 0) {')
        scratch_end=pipeline.index('\n   if (stage == MESA_SHADER_FRAGMENT)',scratch_start)
        asahi_scratch=pipeline[scratch_start:scratch_end]
        pipeline=pipeline[:scratch_start]+'#ifdef APPLE_AGX_GPUVA_WINSYS\n'+asahi_scratch+'\n#else\n'+'''   bool linked_scratch_unsupported = false;
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
#endif
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
            '''#ifdef APPLE_AGX_GPUVA_WINSYS
         cfg.spill_size = cs->b.info.scratch_size
                             ? agx_scratch_get_bucket(cs->b.info.scratch_size)
                             : 0;
#else
         cfg.spill_size = 0; /* Nonzero scratch rejected above. */
#endif''')
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
        # EXP1287: tgsi_to_nir stores every output before each RET (its
        # returns become structured control flow), so an early-returning D3D
        # pixel shader exported inside branches; AGX fragment exports must
        # end the shader (agx_validate block form). Honeykrisp lowers shader
        # outputs to temporaries copied once at the end (hk_shader.c).
        state_text=state_text.replace(shader_ir,shader_ir+'''

   if (cso->type == PIPE_SHADER_IR_TGSI &&
       nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries,
               nir_shader_get_entrypoint(nir), nir_var_shader_out);
      NIR_PASS(_, nir, nir_lower_global_vars_to_local);
      NIR_PASS(_, nir, nir_split_var_copies);
      NIR_PASS(_, nir, nir_lower_var_copies);
      NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   }''')
        scan_anchor='#include "gallium/auxiliary/tgsi/tgsi_from_mesa.h"\n'
        create_anchor='static void *\nagx_create_shader_state('
        if state_text.count(scan_anchor)!=1 or state_text.count(create_anchor)!=1:
            raise SystemExit('Ambiguous native stream-output helper anchor')
        state_text=state_text.replace(scan_anchor,scan_anchor+
            '#include "gallium/auxiliary/tgsi/tgsi_scan.h"\n')
        state_text=state_text.replace(create_anchor,'''/* D3D-G8: the NIR slot tgsi_to_nir gives a TGSI output semantic, for the
 * semantics the D3D10 frontend declares. */
static bool
agx_windows_xfb_slot(unsigned name, unsigned index, unsigned *slot)
{
   switch (name) {
   case TGSI_SEMANTIC_POSITION: *slot = VARYING_SLOT_POS; return true;
   case TGSI_SEMANTIC_GENERIC:
      if (index >= 32) return false;
      *slot = VARYING_SLOT_VAR0 + index; return true;
   case TGSI_SEMANTIC_COLOR:
      *slot = index ? VARYING_SLOT_COL1 : VARYING_SLOT_COL0; return true;
   case TGSI_SEMANTIC_PSIZE: *slot = VARYING_SLOT_PSIZ; return true;
   case TGSI_SEMANTIC_CLIPDIST:
      *slot = index ? VARYING_SLOT_CLIP_DIST1 : VARYING_SLOT_CLIP_DIST0; return true;
   case TGSI_SEMANTIC_TEXCOORD:
      if (index >= 8) return false;
      *slot = VARYING_SLOT_TEX0 + index; return true;
   default:
      return false;
   }
}

'''+create_anchor)
        state_text=state_text.replace(shader_ir,shader_ir+'''

   /* The D3D10 frontend carries stream output beside TGSI. D3D-G8: rebuild
    * every declared output as NIR xfb info so Asahi owns both XFB emission
    * and the DrawAuto byte-stride contract. A pipe_stream_output names a
    * TGSI output register; its declaration's semantic gives the NIR slot
    * (the mapping tgsi_to_nir uses). */
   if (cso->stream_output.num_outputs != 0 && !nir->xfb_info) {
      const struct pipe_stream_output_info *pipe_xfb = &cso->stream_output;
      struct tgsi_shader_info scan;
      bool valid = cso->type == PIPE_SHADER_IR_TGSI &&
         pipe_xfb->num_outputs <= PIPE_MAX_SO_OUTPUTS;
      nir_xfb_info *xfb = valid ?
         rzalloc_size(nir, nir_xfb_info_size(pipe_xfb->num_outputs)) : NULL;
      if (xfb)
         tgsi_scan_shader(cso->tokens, &scan);
      for (unsigned i = 0; xfb && valid && i < pipe_xfb->num_outputs; ++i) {
         const struct pipe_stream_output *pipe_out = &pipe_xfb->output[i];
         unsigned reg = pipe_out->register_index, slot = 0;
         valid = reg < scan.num_outputs &&
            pipe_out->output_buffer < NIR_MAX_XFB_BUFFERS &&
            pipe_out->stream == 0 && pipe_out->num_components &&
            pipe_out->start_component + pipe_out->num_components <= 4 &&
            agx_windows_xfb_slot(scan.output_semantic_name[reg],
                                 scan.output_semantic_index[reg], &slot);
         if (!valid)
            break;
         nir_xfb_output_info *xfb_out = &xfb->outputs[i];
         xfb_out->buffer = pipe_out->output_buffer;
         xfb_out->offset = pipe_out->dst_offset * 4;
         xfb_out->location = slot;
         xfb_out->component_offset = pipe_out->start_component;
         xfb_out->component_mask =
            BITFIELD_RANGE(pipe_out->start_component, pipe_out->num_components);
         xfb->buffers_written |= BITFIELD_BIT(pipe_out->output_buffer);
         xfb->buffers[pipe_out->output_buffer].varying_count++;
      }
      if (!xfb || !valid) {
         ralloc_free(nir);
         ralloc_free(so);
         return NULL;
      }
      xfb->streams_written = BITFIELD_BIT(0);
      for (unsigned buffer = 0; buffer < NIR_MAX_XFB_BUFFERS; ++buffer) {
         xfb->buffer_to_stream[buffer] = 0;
         if (xfb->buffers_written & BITFIELD_BIT(buffer)) {
            xfb->buffers[buffer].stride = pipe_xfb->stride[buffer] * 4;
            nir->info.xfb_stride[buffer] = pipe_xfb->stride[buffer];
         }
      }
      xfb->output_count = pipe_xfb->num_outputs;
      nir->xfb_info = xfb;
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
        # EXP843: D3D SV_VertexID excludes the base vertex, so tgsi_to_nir emits
        # load_vertex_id_zero_base.  The AGX compiler accepts that intrinsic only
        # in software (compute) vertex shaders and asserts in a hardware VS
        # (agx_compile.c "only for SW VS").  Lower it for vertex shaders to
        # load_vertex_id - load_first_vertex before sysval lowering; Gallium
        # Asahi already supplies load_first_vertex as a sysval.
        zero_base_pass = """
static bool
agx_win32_lower_vertex_id_zero_base(nir_builder *b, nir_intrinsic_instr *intr,
                                    void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_vertex_id_zero_base)
      return false;
   b->cursor = nir_before_instr(&intr->instr);
   nir_def *id = nir_isub(b, nir_load_vertex_id(b), nir_load_first_vertex(b));
   nir_def_replace(&intr->def, id);
   return true;
}

static void
agx_shader_initialize("""
        if state_text.count('\nstatic void\nagx_shader_initialize(')!=1:
            raise SystemExit('Ambiguous agx_shader_initialize anchor')
        state_text=state_text.replace('\nstatic void\nagx_shader_initialize(',zero_base_pass,1)
        preprocess='\n   agx_preprocess_nir(nir);\n'
        if state_text.count(preprocess)!=1:
            raise SystemExit('Ambiguous agx_preprocess_nir anchor')
        state_text=state_text.replace(preprocess,'''
   if (nir->info.stage == MESA_SHADER_VERTEX) {
      bool lowered_vertex_id = false;
      NIR_PASS(lowered_vertex_id, nir, nir_shader_intrinsics_pass,
               agx_win32_lower_vertex_id_zero_base,
               nir_metadata_control_flow, NULL);
      /* EXP1067: the lowering reads load_first_vertex after the info above
       * was gathered; without SYSTEM_VALUE_FIRST_VERTEX in system_values_read
       * uses_base_param stays false, the draw parameters are never uploaded,
       * and SV_VertexID read an unwritten uniform (k + id). */
      if (lowered_vertex_id)
         nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   }

   agx_preprocess_nir(nir);
''',1)
        # D3D-H4: the tessellated winding of either stage (D3D declares it in
        # the hull shader, GLSL only in the TES).
        winding_front='''            cfg.front_face_ccw ^=
               !ctx->stage[MESA_SHADER_TESS_EVAL].shader->tess.ccw;'''
        winding_params='''      params.ccw = !tes->tess.ccw;'''
        if state_text.count(winding_front)!=1 or state_text.count(winding_params)!=1:
            raise SystemExit('Ambiguous tessellation winding anchors')
        state_text=state_text.replace(winding_front,'''            cfg.front_face_ccw ^=
               !(ctx->stage[MESA_SHADER_TESS_EVAL].shader->tess.ccw ||
                 (ctx->stage[MESA_SHADER_TESS_CTRL].shader &&
                  ctx->stage[MESA_SHADER_TESS_CTRL].shader->tess.ccw));''')
        state_text=state_text.replace(winding_params,
            '''      params.ccw = !(tes->tess.ccw || tcs->tess.ccw);''')
        state_target.write_text(state_text)
        overlays['src/gallium/drivers/asahi/agx_state.c']['after_state_capture']=hashlib.sha256(state_target.read_bytes()).hexdigest()
        # EXP844/845: StartMenuExperienceHost and Explorer faulted in
        # agx2_disassemble_instr.  Debug (non-NDEBUG) builds disassemble every
        # binary to fopen("/dev/null") as a smoke test; on Windows that path
        # does not exist and the decoder reads 16-byte windows past the end of
        # the binary.  The self-test is diagnostics only: disable it on Win32.
        compile_target=out/'src/asahi/compiler/agx_compile.c'
        compile_text=compile_target.read_text()
        selftest='#ifndef NDEBUG\n   bool selftest = !dump_shaders;\n'
        if compile_text.count(selftest)!=1:
            raise SystemExit('Ambiguous agx_compile disassembler self-test anchor')
        compile_text=compile_text.replace(selftest,
            '#if !defined(NDEBUG) && !defined(_WIN32)\n   bool selftest = !dump_shaders;\n',1)
        compile_target.write_text(compile_text)
        overlays.setdefault('src/asahi/compiler/agx_compile.c',{})['after_win32_selftest']=hashlib.sha256(compile_target.read_bytes()).hexdigest()
        (out/'native_pipeline_contract.c').write_text(
            '#include "gallium/drivers/asahi/agx_state.h"\n'
            '#include "agx_usc.h"\n#include "agx_linker.h"\n'
            '#include "gallium/drivers/asahi/agx_win32_pipeline.inc"\n')

        if args.native_batch_lifecycle:
            modules = ('native-asahi-batch-lifecycle',) if args.gpuva else (
                'native-asahi-graph-capture', 'native-asahi-batch-lifecycle')
            for module_name in modules:
                module_path = args.project/'drivers/apple-agx/mesa/scripts'/(module_name+'.py')
                spec = importlib.util.spec_from_file_location(module_name.replace('-', '_'), module_path)
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
                module.project_sources(out, args.project, overlays)
            shader_paths = (
                'src/gallium/drivers/asahi/agx_state.c',
                'src/gallium/frontends/d3d10umd/Shader.cpp',
                'src/gallium/frontends/d3d10umd/Device.cpp')
            projected = project_shader_failure(*((out/path).read_text()
                                                for path in shader_paths))
            for path, content in zip(shader_paths, projected):
                target = out/path
                target.write_text(content)
                overlays.setdefault(path, {})['after_shader_failure'] = (
                    hashlib.sha256(target.read_bytes()).hexdigest())
            for kind, path in enumerate((
                    'src/gallium/drivers/asahi/agx_batch.c',
                    'src/gallium/drivers/asahi/agx_pipe.c',
                    'src/gallium/drivers/asahi/agx_state.c'), 1):
                target = out/path
                content = target.read_text()
                marker = 'ctx->any_faults = true;'
                if content.count(marker) < 2:
                    raise SystemExit('Missing native fault sites: ' + path)
                if kind == 3:
                    declaration = 'static void agx_win32_shader_failed(struct agx_context *ctx);'
                    if content.count(declaration) != 1:
                        raise SystemExit('Missing early native state declaration')
                    content = content.replace(declaration,
                        'void AgxWin32AsahiMarkContextFault(struct agx_context *,unsigned);\n'
                        + declaration, 1)
                content = content.replace(marker,
                    f'AgxWin32AsahiMarkContextFault(ctx, ({kind}u << 16) | __LINE__);')
                if kind == 2:
                    guard = ('if (ctx->any_faults || !batch->vdm.bo || '
                             '!batch->initialized ||\n'
                             '       (!batch->draws && !batch->clear)) {\n'
                             '      AgxWin32AsahiMarkContextFault(ctx, '
                             '(2u << 16) | __LINE__);')
                    if content.count(guard) != 1:
                        raise SystemExit('Missing exact native flush guard')
                    content = content.replace(guard, guard.replace(
                        'MarkContextFault(ctx,', 'MarkBatchFault(ctx, batch,'), 1)
                target.write_text(content)
                overlays.setdefault(path, {})['after_first_fault_receipt'] = (
                    hashlib.sha256(target.read_bytes()).hexdigest())
            # EXP1296: once the device is lost the staging allocation fails and
            # agx_transfer_map dereferenced it (its assert is compiled out):
            # the x86 GL smoke crashed in glReadPixels.  The map fails
            # instead, which the state trackers report to the application.
            pipe_target = out/'src/gallium/drivers/asahi/agx_pipe.c'
            pipe_text = pipe_target.read_text()
            staging_alloc = """      struct agx_resource *staging =
         agx_alloc_staging(pctx->screen, rsrc, level, box);
      assert(staging);
"""
            if pipe_text.count(staging_alloc) != 1:
                raise SystemExit('Ambiguous transfer staging anchor')
            pipe_text = pipe_text.replace(staging_alloc, """      struct agx_resource *staging =
         agx_alloc_staging(pctx->screen, rsrc, level, box);
      if (!staging) {
         *out_transfer = NULL;
         pipe_resource_reference(&transfer->base.resource, NULL);
         FREE(transfer);
         return NULL;
      }
""", 1)
            pipe_target.write_text(pipe_text)
            overlays.setdefault('src/gallium/drivers/asahi/agx_pipe.c', {})[
                'after_staging_failure'] = hashlib.sha256(
                    pipe_target.read_bytes()).hexdigest()
            # The last source transform is authoritative for runtime compilation.
            for key, record in overlays.items():
                path = out/key
                if path.is_file():
                    record['final_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()

if args.prepare_only:
    prepared = {'exit': 0, 'prepared_only': True,
                'gpuva': args.gpuva,
                'native_batch_lifecycle': args.native_batch_lifecycle,
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
if args.architecture=='x86' and not any(a.startswith('--target=') for a in flags):
    flags.append('--target=i686-pc-windows-msvc')
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
