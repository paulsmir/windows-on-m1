"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

GPU-only resources must map through staging, including buffers consumed by
ExecuteIndirect. Actual projected transfer_map/unmap must preserve bytes and
order a GPU copy before CPU reads; direct/persistent maps fail closed.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_umd_buffer_map_failure import HARNESS
from test_d3d10_shader_fields_init import function_body, project


class GpuOnlyTransfer(unittest.TestCase):
    def test_staged_read_write_and_failure_cleanup(self):
        with tempfile.TemporaryDirectory(prefix='t7h-transfer-') as tmp:
            out=project(Path(tmp)/'out')
            self.assertIsNotNone(out, 'Pinned Mesa and libagx are required')
            pipe=(out/'src/gallium/drivers/asahi/agx_pipe.c').read_text()
            harness=HARNESS[:HARNESS.index('static void *pipe_buffer_map_range')]
            harness=harness.replace('static unsigned maps, fail_map, freed, draws, detiles;',
                'static unsigned maps, fail_map, freed, draws, detiles, copies, syncs;\n#define PIPE_RESOURCE_FLAG_UNMAPPABLE 256u')
            harness=harness.replace('static void *agx_bo_map(void *bo) { return ++maps==fail_map ? NULL:bo; }',
                'static void *forbidden;\nstatic void *agx_bo_map(void *bo) { assert(bo!=forbidden); return ++maps==fail_map ? NULL:bo; }')
            harness=harness.replace('static void agx_blit_to_staging(struct pipe_context *p,struct agx_transfer *t) { (void)p;(void)t; }', '''
static void agx_blit_to_staging(struct pipe_context *p,struct agx_transfer *t) {
 (void)p;++copies;memcpy(agx_resource(t->staging.rsrc)->bo,(char *)agx_resource(t->base.resource)->bo+t->base.box.x,t->base.box.width);
}
static void agx_blit_from_staging(struct pipe_context *p,struct agx_transfer *t) {
 (void)p;++copies;memcpy((char *)agx_resource(t->base.resource)->bo+t->base.box.x,agx_resource(t->staging.rsrc)->bo,t->base.box.width);
}
''')
            harness=harness.replace('(void)p;(void)r;(void)s; }', '(void)p;(void)r;(void)s;++syncs; }')
            harness=harness.replace('@@MAP@@',function_body(pipe,'agx_transfer_map'))
            harness+='''
#define agx_transfer(t) ((struct agx_transfer *)(t))
static void agx_flush_readers(struct pipe_context *p,struct agx_resource *r,const char *s) {(void)p;(void)r;(void)s;}
static void ail_tile(void *m,void *s,const struct ail_layout *l,unsigned level,unsigned stride,int x,int y,int w,int h) {(void)m;(void)s;(void)l;(void)level;(void)stride;(void)x;(void)y;(void)w;(void)h;assert(0);}
static void agx_transfer_unmap(struct pipe_context *pctx,struct pipe_transfer *transfer)
'''+function_body(pipe,'agx_transfer_unmap')+r'''
int main(void) {
 uint32_t content[16]={0,0,7,2}, copy[16]={0};
 struct agx_resource r={0};r.base.target=PIPE_BUFFER;r.base.flags=PIPE_RESOURCE_FLAG_UNMAPPABLE;
 r.layout.levels=1;r.bo=content;forbidden=content;staging.bo=copy;
 struct pipe_context ctx={0};struct pipe_box box={8,0,0,8,1,1};struct pipe_transfer *t=NULL;
 void *map=agx_transfer_map(&ctx,&r.base,0,PIPE_MAP_READ,&box,&t);
 assert(map==copy && t && copies==1 && syncs==1 && maps==1 && copy[0]==7 && copy[1]==2);
 agx_transfer_unmap(&ctx,t);assert(!r.base.refs && !staging.base.refs);
 copies=syncs=maps=0;
 map=agx_transfer_map(&ctx,&r.base,0,PIPE_MAP_WRITE,&box,&t);
 assert(map==copy && t && !copies && !syncs);copy[0]=91;copy[1]=37;
 agx_transfer_unmap(&ctx,t);assert(copies==1 && content[2]==91 && content[3]==37);
 maps=0;fail_map=1;t=(void *)1;
 assert(!agx_transfer_map(&ctx,&r.base,0,PIPE_MAP_READ,&box,&t));
 assert(!t && !r.base.refs && !staging.base.refs && ctx.faults==1);
 fail_map=0;maps=copies=0;t=(void *)1;
 assert(!agx_transfer_map(&ctx,&r.base,0,PIPE_MAP_READ|PIPE_MAP_DIRECTLY,&box,&t));
 assert(!t && !maps && !copies);
 /* GPU-only linear/twiddled textures also take the staging path. */
 for(unsigned tiled=0;tiled<2;++tiled) {
   r.base.target=2;twiddled=tiled;maps=copies=syncs=0;
   map=agx_transfer_map(&ctx,&r.base,0,PIPE_MAP_READ,&box,&t);
   assert(map==copy && copies==1 && syncs==1 && maps==1 && !detiles);
   agx_transfer_unmap(&ctx,t);
 }
}
'''
            src,exe=Path(tmp)/'test.c',Path(tmp)/'test';src.write_text(harness)
            built=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_actual_staging_constructor_drops_gpu_only_and_sizes_array_box(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Recursive GPU-only staging or full 1D-array staging for a single-layer transfer."""
        with tempfile.TemporaryDirectory(prefix='t7h-staging-') as tmp:
            out=project(Path(tmp)/'out');self.assertIsNotNone(out)
            pipe=(out/'src/gallium/drivers/asahi/agx_pipe.c').read_text()
            harness=r'''
#include <assert.h>
#include <stddef.h>
#define PIPE_RESOURCE_FLAG_UNMAPPABLE 256u
#define util_format_is_compressed(f) 0
#define util_format_get_nblocksx(f,w) (w)
#define util_format_get_nblocksy(f,h) (h)
#define PIPE_USAGE_STAGING 1u
#define PIPE_BIND_LINEAR 1u
#define PIPE_BIND_RENDER_TARGET 2u
#define PIPE_BIND_SAMPLER_VIEW 4u
#define PIPE_BIND_SHADER_BUFFER 8u
enum {PIPE_BUFFER,PIPE_TEXTURE_1D,PIPE_TEXTURE_1D_ARRAY,PIPE_TEXTURE_2D,PIPE_TEXTURE_2D_ARRAY,PIPE_TEXTURE_CUBE,PIPE_TEXTURE_CUBE_ARRAY,PIPE_TEXTURE_3D};
struct pipe_resource {unsigned target,usage,flags,width0,height0,depth0,array_size,last_level,format,bind;};
struct agx_resource {struct pipe_resource base;struct {unsigned format;} layout;};
struct pipe_box {int x,y,z,width,height,depth;};
struct pipe_screen {struct pipe_resource *(*resource_create)(struct pipe_screen *,const struct pipe_resource *);};
#define agx_resource(p) ((struct agx_resource *)(p))
static unsigned agx_staging_format(unsigned f) {return f;}
static struct agx_resource result;
static struct pipe_resource *create(struct pipe_screen *s,const struct pipe_resource *t) {(void)s;result.base=*t;return &result.base;}
static struct agx_resource *agx_alloc_staging(struct pipe_screen *screen,struct agx_resource *rsc,unsigned level,const struct pipe_box *box)
@@BODY@@
int main(void) {
 struct pipe_screen s={create};struct agx_resource r={0};
 r.base.flags=PIPE_RESOURCE_FLAG_UNMAPPABLE;r.base.target=PIPE_TEXTURE_1D_ARRAY;r.base.array_size=12;
 struct pipe_box box={3,0,7,5,1,1};
 for(unsigned depth=1;depth<=2;++depth) {
  box.depth=depth;
  assert(agx_alloc_staging(&s,&r,0,&box)==&result);
  assert(!result.base.flags && result.base.array_size==depth && result.base.width0==5 && result.base.last_level==0);
 }
 r.base.target=PIPE_BUFFER;r.base.array_size=1;box.depth=1;
 assert(agx_alloc_staging(&s,&r,0,&box)==&result);
 assert(!result.base.flags && result.base.bind==PIPE_BIND_SHADER_BUFFER && result.base.target==PIPE_BUFFER);
}
'''.replace('@@BODY@@',function_body(pipe,'agx_alloc_staging'))
            src,exe=Path(tmp)/'test.c',Path(tmp)/'test';src.write_text(harness)
            b=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(b.returncode,0,b.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_actual_buffer_copy_helpers_use_subrange_offsets(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Staging must copy the requested source/destination range, not offset zero or the entire heap."""
        with tempfile.TemporaryDirectory(prefix='t7h-copy-') as tmp:
            out=project(Path(tmp)/'out');self.assertIsNotNone(out)
            pipe=(out/'src/gallium/drivers/asahi/agx_pipe.c').read_text()
            h=r'''
#include <assert.h>
#include <string.h>
#include <stdint.h>
#define PIPE_BUFFER 1
#define PIPE_TEX_FILTER_NEAREST 0
#define util_format_is_compressed(f) 0
#define util_format_get_blockwidth(f) 1
#define util_format_get_blockheight(f) 1
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
struct pipe_resource {unsigned target;uint8_t bytes[32];unsigned format;};
struct agx_resource {struct pipe_resource base;struct {unsigned format;} layout;};
struct pipe_box {int x,y,z,width,height,depth;};
struct agx_transfer {struct {struct pipe_resource *resource;unsigned level;struct pipe_box box;} base;struct {struct pipe_resource *rsrc;struct pipe_box box;} staging;};
struct pipe_context {void (*resource_copy_region)(struct pipe_context *,struct pipe_resource *,unsigned,unsigned,unsigned,unsigned,struct pipe_resource *,unsigned,const struct pipe_box *);};
struct pipe_blit_info {struct {struct pipe_resource *resource;unsigned format,level;struct pipe_box box;} src,dst;unsigned mask,filter;};
#define agx_resource(p) ((struct agx_resource *)(p))
static unsigned agx_staging_format(unsigned f) {return f;}
static unsigned util_format_get_mask(unsigned f) {return f;}
static void agx_blit(struct pipe_context *p,struct pipe_blit_info *b) {(void)p;(void)b;assert(0);}
static unsigned copies;
static void copy(struct pipe_context *p,struct pipe_resource *dst,unsigned dl,unsigned x,unsigned y,unsigned z,struct pipe_resource *src,unsigned sl,const struct pipe_box *box) {
 (void)p;assert(!dl && !sl && !y && !z && box->width==7 && box->height==1 && box->depth==1);
 assert(x+(unsigned)box->width<=32 && box->x+box->width<=32);
 memcpy(dst->bytes+x,src->bytes+box->x,box->width);++copies;
}
static void agx_blit_to_staging(struct pipe_context *pctx,struct agx_transfer *trans)
@@TO@@
static void agx_blit_from_staging(struct pipe_context *pctx,struct agx_transfer *trans)
@@FROM@@
int main(void) {
 struct pipe_resource gpu={PIPE_BUFFER,{0}},staging={PIPE_BUFFER,{0}};
 for(unsigned i=0;i<32;++i)gpu.bytes[i]=i;
 struct pipe_context p={copy};struct agx_transfer t={{&gpu,0,{11,0,0,7,1,1}},{&staging,{0,0,0,7,1,1}}};
 agx_blit_to_staging(&p,&t);assert(copies==1);
 for(unsigned i=0;i<7;++i)assert(staging.bytes[i]==11+i);
 memset(staging.bytes,0xab,7);agx_blit_from_staging(&p,&t);assert(copies==2);
 for(unsigned i=0;i<32;++i)assert(gpu.bytes[i]==(i>=11 && i<18?0xab:i));
}
'''.replace('@@TO@@',function_body(pipe,'agx_blit_to_staging')).replace('@@FROM@@',function_body(pipe,'agx_blit_from_staging'))
            src,exe=Path(tmp)/'test.c',Path(tmp)/'test';src.write_text(h)
            b=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(b.returncode,0,b.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_depth_staging_round_trip_and_cleanup(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? GPU-only Z16/Z32 must stage through R16/R32, preserve array/mip boxes and bytes in both directions, synchronize reads and clean up failed staging without mapping the depth BO, including uncompressed mip tails."""
        from test_dxbc_to_nir import ROOT, local_tree, host_flags
        from test_native_resource_layout import function
        mesa=local_tree('reference','mesa')
        build=local_tree('accelerated-desktop-ad03','mesa-build')
        self.assertIsNotNone(mesa, 'Pinned Mesa is required')
        self.assertIsNotNone(build, 'Pinned host AIL is required')
        with tempfile.TemporaryDirectory(prefix='t7h-depth-') as tmp:
            out=project(Path(tmp)/'out');self.assertIsNotNone(out)
            pipe=(out/'src/gallium/drivers/asahi/agx_pipe.c').read_text()
            declarations=[]
            for name,ret in (('agx_staging_format','enum pipe_format'),
                             ('agx_alloc_staging','struct agx_resource *'),
                             ('agx_blit_from_staging','void'),
                             ('agx_blit_to_staging','void'),
                             ('agx_transfer_map','void *'),
                             ('agx_transfer_unmap','void')):
                declarations.append('static '+ret+'\n'+function(pipe,name))
            fixture=(ROOT/'tests/umd_gpu_only_depth_fixture.c').read_text()
            source=fixture.replace('/* ACTUAL_TRANSFER_SOURCE */','\n'.join(declarations))
            # Keep the pinned source notice on the extracted Mesa functions.
            source=pipe[:pipe.index('*/')+2]+'\n'+source
            src,exe=Path(tmp)/'depth.c',Path(tmp)/'depth';src.write_text(source)
            libs=[build/p for p in ('src/asahi/layout/libasahi_layout.a','src/util/libmesa_util.a',
                 'src/util/libmesa_util_simd.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a')]
            b=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',*host_flags(build),
                '-I',str(mesa/'src'),'-I',str(mesa/'src/gallium/include'),
                '-I',str(mesa/'src/gallium/auxiliary'),str(src),'-o',str(exe),*map(str,libs),
                '-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread'],capture_output=True,text=True)
            self.assertEqual(b.returncode,0,b.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertIn('gpu-only Z16/Z32 depth staging: PASS',r.stdout)

    def test_bc_staging_round_trip_and_cleanup(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? GPU-only BC must stage through raw UINT blocks, preserve array/mip boxes and bytes in both directions, synchronize reads and clean up failed staging without mapping the BC BO, including sub-block mip tails."""
        from test_dxbc_to_nir import ROOT, local_tree, host_flags
        from test_native_resource_layout import function
        mesa=local_tree('reference','mesa')
        build=local_tree('accelerated-desktop-ad03','mesa-build')
        self.assertIsNotNone(mesa, 'Pinned Mesa is required')
        self.assertIsNotNone(build, 'Pinned host AIL is required')
        with tempfile.TemporaryDirectory(prefix='t7h-depth-') as tmp:
            out=project(Path(tmp)/'out');self.assertIsNotNone(out)
            pipe=(out/'src/gallium/drivers/asahi/agx_pipe.c').read_text()
            declarations=[]
            for name,ret in (('agx_staging_format','enum pipe_format'),
                             ('agx_alloc_staging','struct agx_resource *'),
                             ('agx_blit_from_staging','void'),
                             ('agx_blit_to_staging','void'),
                             ('agx_transfer_map','void *'),
                             ('agx_transfer_unmap','void')):
                declarations.append('static '+ret+'\n'+function(pipe,name))
            fixture=(ROOT/'tests/umd_gpu_only_bc_fixture.c').read_text()
            source=fixture.replace('/* ACTUAL_TRANSFER_SOURCE */','\n'.join(declarations))
            # Keep the pinned source notice on the extracted Mesa functions.
            source=pipe[:pipe.index('*/')+2]+'\n'+source
            src,exe=Path(tmp)/'depth.c',Path(tmp)/'depth';src.write_text(source)
            libs=[build/p for p in ('src/asahi/layout/libasahi_layout.a','src/util/libmesa_util.a',
                 'src/util/libmesa_util_simd.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a')]
            b=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',*host_flags(build),
                '-I',str(mesa/'src'),'-I',str(mesa/'src/gallium/include'),
                '-I',str(mesa/'src/gallium/auxiliary'),str(src),'-o',str(exe),*map(str,libs),
                '-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread'],capture_output=True,text=True)
            self.assertEqual(b.returncode,0,b.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertIn('gpu-only BC staging: PASS',r.stdout)

    def test_resource_intent_reaches_bo_flags(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Losing UNMAPPABLE between the owner resource template and agx_bo_create silently restores CPU-visible aperture backing."""
        with tempfile.TemporaryDirectory(prefix='t7h-resource-') as tmp:
            out=project(Path(tmp)/'out');self.assertIsNotNone(out)
            pipe=(out/'src/gallium/drivers/asahi/agx_pipe.c').read_text()
            a=pipe.index('   uint32_t create_flags = 0;');b=pipe.index('   nresource->bo =',a)
            h=r'''
#include <stdint.h>
#include <assert.h>
#define PIPE_RESOURCE_FLAG_UNMAPPABLE 256u
#define PIPE_RESOURCE_FLAG_MAP_COHERENT 2u
#define PIPE_USAGE_STAGING 1u
#define PIPE_BIND_SCANOUT 1u
#define PIPE_BIND_DISPLAY_TARGET 2u
#define PIPE_BIND_SHARED 4u
#define AGX_BO_WRITEBACK 4u
#define AGX_BO_SHAREABLE 16u
#define AGX_WIN32_BO_GPU_ONLY (1u << 29)
#define AGX_DBG_NOWC 1u
static unsigned flags_for(unsigned flags) {
 struct resource {unsigned usage,flags;} resource={0,flags},*templ=&resource;
 struct {struct resource base;} r={resource},*nresource=&r;
 struct {unsigned debug;} device={0},*dev=&device;unsigned bind=0;
@@BODY@@
 return create_flags;
}
int main(void) {assert(flags_for(0)==0);assert(flags_for(256)==(1u<<29));assert(flags_for(2)==4);}
'''.replace('@@BODY@@',pipe[a:b])
            src,exe=Path(tmp)/'test.c',Path(tmp)/'test';src.write_text(h)
            b=subprocess.run(['clang','-std=c11',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(b.returncode,0,b.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
