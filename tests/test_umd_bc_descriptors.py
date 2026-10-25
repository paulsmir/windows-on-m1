"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Raw UINT BC views must address the selected mip/layer, including NPOT tails;
using base pixel dimensions or deriving array stride from block dimensions
silently copies the wrong bytes even when transfer staging tests pass.
"""
import ast
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from test_dxbc_to_nir import ROOT, local_tree, host_flags, HOST_LIBS
from test_d3d12_pbe_depth import selected, projector


def bc_projector():
    script=(ROOT/'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py').read_text()
    tree=ast.parse(script)
    func=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='project_bc_block_views')
    ns={};exec(compile(ast.Module(body=[func],type_ignores=[]),'<projector>','exec'),ns)
    return ns['project_bc_block_views']


class BcDescriptors(unittest.TestCase):
    def test_actual_raw_bc_packing(self):
        mesa=local_tree('reference','mesa');build=local_tree('accelerated-desktop-ad03','mesa-build')
        self.assertIsNotNone(mesa);self.assertIsNotNone(build)
        config=json.loads((ROOT/'tests/d3d12_pbe_depth_build.json').read_text())
        state=projector()(selected(mesa,config,'state'))
        state,blit=bc_projector()(state,(mesa/'src/gallium/drivers/asahi/agx_blit.c').read_text())
        with tempfile.TemporaryDirectory(prefix='bc-descriptor-') as tmp:
            out=Path(tmp)
            (out/'selected-state.inc').write_text(state)
            (out/'d3d12_pbe_depth_boundary.h').write_bytes((ROOT/'tests/d3d12_pbe_depth_boundary.h').read_bytes())
            from test_d3d12_view_mapping import block
            geometry=block(blit,'static void\nasahi_compute_blit(')
            body=geometry[geometry.index('{')+1:geometry.index('   struct pipe_constant_buffer cb =')]
            body=body.replace('      return;', '      return;')
            support=block(blit,'static bool\nasahi_compute_blit_supported(')
            geometry='\n#define asahi_compute_save(c) ((void)0)\n#define agx_context(c) (c)\n#define TILE_WIDTH 32\n#define TILE_HEIGHT 32\nstatic void geometry(const struct pipe_blit_info *info, float *values, unsigned *dims) {\n struct pipe_context *ctx = NULL;\n'+body+'\n memcpy(values,data,4*sizeof(float));dims[0]=width;dims[1]=height;\n}\n'
            (out/'descriptor.c').write_text(FIXTURE.replace('/* GEOMETRY */',support+geometry))
            exe=out/'check'
            flags=config['flags']
            if sys.platform!='darwin':
                # the pinned recipe is the macOS tree's: keep its includes and
                # options, take the configuration defines from this host's tree
                sdk=flags[flags.index('-isysroot')+1]
                flags=[f for f in flags if not f.startswith('-D') and f not in ('-isysroot',sdk)]+\
                      [f for f in host_flags(build) if f.startswith('-D')]
            r=subprocess.run(['clang','-std=c11','-DC24_SELECTED_SOURCE="'+str(out/'selected-state.inc')+'"',
                *flags,str(out/'descriptor.c'),*[str(build/n) for n in config['libs']],
                '-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread',*HOST_LIBS,'-o',str(exe)],cwd=build,capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertIn('BC raw descriptors: PASS',r.stdout)

FIXTURE=r'''
#include "d3d12_pbe_depth_boundary.h"
#include C24_SELECTED_SOURCE
/* GEOMETRY */
int main(void) {
 const enum pipe_format formats[]={PIPE_FORMAT_DXT1_RGBA,PIPE_FORMAT_BPTC_RGBA_UNORM};
 for(unsigned f=0;f<2;f++) for(unsigned width=63;width<=64;width++) for(unsigned layer=0;layer<3;layer++)
 for(unsigned level=0;level<7;level++) {
  struct agx_va va={.addr=0x10000000ull};struct agx_bo bo={.va=&va};struct agx_resource tex={0};tex.bo=&bo;
  tex.base=(struct pipe_resource){.target=PIPE_TEXTURE_2D_ARRAY,.format=formats[f],.width0=width,
      .height0=35,.depth0=1,.array_size=3,.last_level=6};tex.mipmapped=true;
  tex.layout=(struct ail_layout){.format=formats[f],.width_px=width,.height_px=35,
      .depth_px=3,.sample_count_sa=1,.levels=7,.tiling=AIL_TILING_GPU};ail_make_miptree(&tex.layout);
  enum pipe_format raw=f?PIPE_FORMAT_R32G32B32A32_UINT:PIPE_FORMAT_R32G32_UINT;
  struct pipe_sampler_view view={.target=PIPE_TEXTURE_2D,.format=raw,
      .swizzle_r=PIPE_SWIZZLE_X,.swizzle_g=PIPE_SWIZZLE_Y,.swizzle_b=PIPE_SWIZZLE_Z,.swizzle_a=PIPE_SWIZZLE_W};
  view.u.tex.first_level=view.u.tex.last_level=level;view.u.tex.first_layer=view.u.tex.last_layer=layer;
  struct pipe_image_view image={.resource=&tex.base,.format=raw,.access=PIPE_IMAGE_ACCESS_DRIVER_INTERNAL};
  image.u.tex.level=level;image.u.tex.first_layer=image.u.tex.last_layer=layer;image.u.tex.single_layer_view=true;
  struct agx_texture_packed read={0};struct agx_pbe_packed write={0};
  agx_pack_texture(&read,&tex,raw,&view);agx_batch_upload_pbe(NULL,&write,&image,false,false,false,false);
  agx_unpack(NULL,&read,TEXTURE,t);agx_unpack(NULL,&write,PBE,p);
  unsigned w=util_format_get_nblocksx(formats[f],u_minify(width,level));
  unsigned h=util_format_get_nblocksy(formats[f],u_minify(35,level));
  uint64_t address=va.addr+ail_get_layer_level_B(&tex.layout,layer,level);
  assert(t.address==address && p.buffer==address);
  assert(t.width==w && p.width==w && t.height==h && p.height==h);
  assert(t.first_level==0 && p.level==0 && t.depth==1 && p.layers==1);
  assert(!t.compressed && !p.compressed);
  struct pipe_resource staging={.target=PIPE_TEXTURE_2D,.format=raw,.width0=w,.height0=h};
  struct pipe_blit_info b={0};
  b.src.resource=&tex.base;b.src.format=raw;b.src.level=level;
  b.dst.resource=&staging;b.dst.format=raw;b.mask=PIPE_MASK_RGBA;
  b.src.box.width=b.dst.box.width=w;b.src.box.height=b.dst.box.height=h;
  b.src.box.depth=b.dst.box.depth=1;
  float params[4];unsigned dims[2];geometry(&b,params,dims);
  assert(params[0]==0.5f/w && params[1]==0.5f/h && params[2]==1.0f/w && params[3]==1.0f/h);
  assert(dims[0]==w && dims[1]==h);
  b.src.resource=&staging;b.src.level=0;b.dst.resource=&tex.base;b.dst.level=level;
  geometry(&b,params,dims);assert(dims[0]==w && dims[1]==h);
 }
 puts("BC raw descriptors: PASS");
}
'''
