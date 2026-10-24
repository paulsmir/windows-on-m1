"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Run the production collectors against projected Mesa types: a bound resource
missing from bo_list must still have an expected range after unbind, with root
and descriptor provenance. Compile the entire native submit translation unit.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body
from test_d3d10_shader_fields_init import project
from test_dxbc_to_nir import host_flags, local_tree

ROOT=Path(__file__).resolve().parents[1]
W=ROOT/'drivers/apple-agx/mesa/winsys'

class ReferenceCollectors(unittest.TestCase):
    def test_actual_collectors_and_translation_unit(self):
        build=local_tree('accelerated-desktop-ad03','mesa-build')
        mesa=local_tree('reference','mesa')
        if not build or not mesa:self.skipTest('pinned Mesa host build unavailable')
        with tempfile.TemporaryDirectory(prefix='ref-collectors-') as td:
            p=Path(td);out=project(p/'out')
            # Platform shim: expose only the Windows owner field on this host;
            # no change to production functions or Mesa resource/batch layouts.
            h=out/'src/asahi/lib/agx_device.h';s=h.read_text()
            s=s.replace('#ifdef _WIN32\n   void *windows_private; /* Windows native allocation owner, never a GPU address */\n#endif',
                        '   void *windows_private; /* host platform field */')
            h.write_text(s)
            (p/'xf86drm.h').write_text('/* OS interface unused by this translation unit. */\n')
            includes=[p,out/'src',out/'include',out/'include/drm-uapi',
                      out/'src/asahi/lib',out/'src/asahi/libagx',out/'src/asahi/layout',
                      out/'src/asahi/compiler',out/'src/asahi/genxml',
                      out/'src/gallium/drivers/asahi',out/'src/gallium/include',
                      build/'src/asahi',build/'src/asahi/genxml',build/'src/compiler/nir',
                      mesa/'src/gallium/auxiliary',mesa/'src/gallium/include',mesa/'src/virtio/vdrm',
                      W,ROOT/'drivers/apple-agx/shared/include']
            flags=['-std=c11','-DAPPLE_AGX_GPUVA_WINSYS','-Wno-c23-extensions',
                   *['-I'+str(d) for d in includes],*host_flags(build)]
            z=subprocess.run(['clang',*flags,'-fsyntax-only',str(W/'agx_win32_gpuva_batch.c')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            source=re.sub(r'/\*.*?\*/','',(W/'agx_win32_gpuva_batch.c').read_text(),flags=re.S)
            names=['backend','capsule','expect_bo','AgxWin32AsahiExpectBo','expect_resource',
                   'AgxWin32AsahiExpectResource','capture_stage_refs','capture_draw_refs',
                   'expect_address_origin','expect_address','AgxWin32AsahiExpectDescriptors','capture_compute_addresses','capture_render_addresses','capture_native_refs']
            code=r'''
#include "gallium/drivers/asahi/agx_state.h"
#include "apple_agx_g4_submit.h"
#include <assert.h>
typedef struct {struct agx_bo *Command,*Preemption;AGX_WIN32_REF_LIST Expected;} AGX_G4_BATCH;
static AGX_WIN32_GPUVA_BO maps[8];
static struct agx_batch *selected;
struct agx_batch *agx_get_batch(struct agx_context *c){(void)c;return selected;}
struct agx_batch *agx_get_compute_batch(struct agx_context *c){(void)c;return selected;}
const AGX_WIN32_GPUVA_BO *AgxWin32AsahiGpuvaBo(AGX_WIN32_ASAHI_BACKEND *b,struct agx_bo *bo){(void)b;return &maps[bo->handle];}
struct windows_bo {struct agx_bo Base;int Virtual;AGX_WIN32_GPUVA_BO Reserved,ReadShadow;};
@@VIRTUAL_CONTAINS@@
unsigned AgxWin32AsahiVirtualBackingCount(struct agx_bo *bo){(void)bo;return 0;}
struct agx_bo *AgxWin32AsahiVirtualBacking(struct agx_bo *bo,unsigned i){(void)bo;(void)i;return NULL;}
struct agx_bo *AgxWin32AsahiVirtualTable(struct agx_bo *bo){(void)bo;return NULL;}
@@FUNCTIONS@@
int main(void){
 struct agx_screen screen={0};AGX_WIN32_ASAHI_BACKEND b={0};screen.dev.windows_private=&b;
 struct agx_context ctx={0};ctx.base.screen=&screen.pscreen;
 struct agx_batch batch={0};AGX_G4_BATCH g={0};batch.ctx=&ctx;batch.windows_batch=&g;selected=&batch;
 struct agx_bo bo={0};struct agx_va va={0};va.addr=0x100000;bo.va=&va;bo.handle=1;bo.size=65536;
 maps[1]=(AGX_WIN32_GPUVA_BO){17,va.addr,65536,1};
 struct agx_resource resource={0};resource.bo=&bo;resource.base.target=PIPE_BUFFER;
 resource.base.screen=&screen.pscreen;resource.layout.level_offsets_B[0]=32;
 struct agx_uncompiled_shader shader={0};ctx.stage[MESA_SHADER_COMPUTE].shader=&shader;
 ctx.stage[MESA_SHADER_COMPUTE].cb_mask=1;ctx.stage[MESA_SHADER_COMPUTE].cb[0].buffer=&resource.base;
 ctx.stage[MESA_SHADER_COMPUTE].cb[0].buffer_offset=16;ctx.stage[MESA_SHADER_COMPUTE].cb[0].buffer_size=64;
 capture_stage_refs(&batch,MESA_SHADER_COMPUTE);assert(g.Expected.Count==0); /* default off */
 b.ValidateRefs=1;b.RefOrigin.Execute=19;b.RefOrigin.Command=7;b.RefRoots[MESA_SHADER_COMPUTE][0][0]=4;
 capture_stage_refs(&batch,MESA_SHADER_COMPUTE);
 assert(g.Expected.Count==1 && g.Expected.Items[0].Root==3);
 assert(g.Expected.Items[0].Va==va.addr+48 && g.Expected.Items[0].Bytes==64);
 assert(g.Expected.Items[0].Execute==19 && g.Expected.Items[0].Command==7);
 assert(AgxWin32RefCheck(&g.Expected.Items[0],NULL,0)==AgxRefMissing);
 const AGX_WIN32_GPUVA_BO *held[]={&maps[1]};assert(!AgxWin32RefCheck(&g.Expected.Items[0],held,1));
 ctx.stage[MESA_SHADER_COMPUTE].cb[0].buffer=NULL;resource.bo=NULL;
 assert(g.Expected.Items[0].Va==va.addr+48); /* unbind can't erase snapshot */
 resource.bo=&bo;
 AgxWin32AsahiExpectResource(&ctx.base,1,&resource.base,AgxRefDescriptor,5,123,0xabcdef);
 assert(g.Expected.Items[1].Slot==123 && g.Expected.Items[1].Object==0xabcdef);
 assert(!AgxWin32RefCheck(&g.Expected.Items[1],held,1)); /* placed whole-buffer remainder */
 struct pipe_draw_info draw={0};draw.index_size=4;draw.index.resource=&resource.base;
 struct pipe_draw_indirect_info indirect={0};indirect.buffer=&resource.base;indirect.indirect_draw_count=&resource.base;
 capture_draw_refs(&batch,&draw,&indirect);
 assert(g.Expected.Items[2].Kind==AgxRefIndex && g.Expected.Items[3].Kind==AgxRefIndirect && g.Expected.Items[4].Kind==AgxRefCount);
 batch.sampler_heap.bo=&bo;batch.cs_scratch=true;ctx.scratch_cs.buf=&bo;
 capture_native_refs(&batch);
 assert(g.Expected.Items[5].Kind==AgxRefSampler && g.Expected.Items[6].Kind==AgxRefScratch);
 struct drm_asahi_cmd_compute compute={0};compute.cdm_ctrl_stream_base=va.addr+128;compute.cdm_ctrl_stream_end=va.addr+256;
 capture_compute_addresses(&batch,&compute);assert(g.Expected.Items[7].Bytes==128);
 /* A changed/reused owner may cover the VA but must not satisfy old identity. */
 maps[1].Allocation=99;assert(AgxWin32RefCheck(&g.Expected.Items[0],held,1)==AgxRefOwner);
 AgxWin32RefCleanup(&g.Expected);
 /* Bound but unreachable descriptors are not GPU references. */
 struct agx_sampler_view unused={0};unused.rsrc=&resource;
 ctx.stage[MESA_SHADER_COMPUTE].textures[7]=&unused;
 ctx.stage[MESA_SHADER_COMPUTE].texture_count=8;
 shader.info.nr_bindful_textures=1;
 capture_stage_refs(&batch,MESA_SHADER_COMPUTE);assert(g.Expected.Count==0);
 /* Large sparse buffer: a root binding beyond its 64 KiB canonical anchor. */
 va.addr=0x800000;bo.size=0x40000;maps[1].Va=0x100000;
 b.RefOrigin.List=3;
 expect_resource(&batch,&resource.base,AgxRefCbv,5,0,0x20000,64,0);
 assert(g.Expected.Count==1 && g.Expected.Items[0].List==3);
 assert(g.Expected.Items[0].Va==maps[1].Va && g.Expected.Items[0].Bytes==maps[1].Bytes);
 assert(!AgxWin32RefCheck(&g.Expected.Items[0],held,1));
 AgxWin32RefCleanup(&g.Expected);
 /* A held intended resource must not hide a stale packed heap address. */
 struct windows_bo virtual_bo={0};virtual_bo.Virtual=1;
 virtual_bo.Reserved=(AGX_WIN32_GPUVA_BO){17,0x800000,0x40000,1};
 virtual_bo.ReadShadow=(AGX_WIN32_GPUVA_BO){17,0xa00000,0x40000,1};
 assert(AgxWin32AsahiVirtualContains(&virtual_bo.Base,0x820000,64));
 assert(AgxWin32AsahiVirtualContains(&virtual_bo.Base,0xa20000,64));
 assert(!AgxWin32AsahiVirtualContains(&virtual_bo.Base,0xa3fff0,32));
 assert(!AgxWin32AsahiVirtualContains(&virtual_bo.Base,UINT64_MAX-4,16));
 uint32_t packed[18]={0};
 agx_pack(packed,TEXTURE,t){t.address=0xdead0000;}
 AgxWin32AsahiExpectDescriptors(&ctx.base,1,packed,5,71,0xabcdef);
 assert(g.Expected.Count==1 && g.Expected.Items[0].Slot==71 && g.Expected.Items[0].Object==0xabcdef);
 assert(g.Expected.Items[0].Va==0xdead0000);
 assert(AgxWin32RefCheck(&g.Expected.Items[0],held,1)==AgxRefMissing);
 AgxWin32RefCleanup(&g.Expected);return 0;
}
'''.replace('@@FUNCTIONS@@','\n'.join(body(source,n) for n in names)).replace('@@VIRTUAL_CONTAINS@@',body(re.sub(r'/\*.*?\*/','',(W/'agx_win32_asahi_bo.c').read_text(),flags=re.S),'AgxWin32AsahiVirtualContains'))
            (p/'collect.c').write_text(code)
            z=subprocess.run(['clang',*flags,'-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p/'collect.c'),
                              str(W/'agx_win32_gpuva.c'),'-o',str(p/'collect')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            z=subprocess.run([str(p/'collect')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
