"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

EXP1910: util_draw_indirect checks the transfer, then dereferences a NULL count
map. Execute the actual projected map producer and indirect reader: no failed
BO map may publish a transfer, add an offset to NULL, or issue a draw. A failure
must latch the context fault and release references. Success must still draw.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d10_shader_fields_init import function_body, project, local_tree

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
enum { PIPE_BUFFER=1, PIPE_MAP_READ=1, PIPE_MAP_WRITE=2, PIPE_MAP_DIRECTLY=4,
       PIPE_MAP_PERSISTENT=8, PIPE_MAP_COHERENT=16, PIPE_MAP_DISCARD_WHOLE_RESOURCE=32,
       DRM_FORMAT_MOD_LINEAR=0 };
struct pipe_resource { int target, refs; };
struct pipe_box { int x,y,z,width,height,depth; };
struct pipe_transfer { struct pipe_resource *resource; unsigned level,usage,stride,layer_stride; struct pipe_box box; };
struct agx_transfer { struct pipe_transfer base; struct { struct pipe_resource *rsrc; struct pipe_box box; } staging; void *map; };
struct ail_layout { unsigned levels,layer_stride_B,format; };
struct agx_resource { struct pipe_resource base; struct ail_layout layout; int modifier; void *bo; unsigned data_valid,valid_buffer_range; };
struct pipe_draw_info { unsigned index_size,instance_count,start_instance; };
struct pipe_draw_start_count_bias { unsigned start,count; int index_bias; };
struct pipe_draw_indirect_info { struct pipe_resource *buffer,*indirect_draw_count; unsigned indirect_draw_count_offset,offset,stride,draw_count; void *count_from_stream_output; };
struct pipe_context { void *screen; void (*draw_vbo)(struct pipe_context *, const struct pipe_draw_info *, unsigned, const void *, const struct pipe_draw_start_count_bias *, unsigned); unsigned faults; };
#define agx_context(p) (p)
#define agx_resource(p) ((struct agx_resource *)(p))
static unsigned maps, fail_map, freed, draws, detiles;
static bool twiddled, compressed;
static struct agx_resource staging;
#define CALLOC_STRUCT(t) calloc(1,sizeof(struct t))
#define FREE(p) do { ++freed; free(p); } while (0)
#define BITSET_SET(v,n) ((v) |= 1u << (n))
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define debug_printf(...) ((void)0)
static void AgxWin32AsahiMarkContextFault(struct pipe_context *p,unsigned site) { assert(site); ++p->faults; }
static void pipe_resource_reference(struct pipe_resource **p,struct pipe_resource *v) { if (*p) --(*p)->refs; *p=v; if(v) ++v->refs; }
static bool ail_is_level_logically_compressed(const struct ail_layout *l,unsigned n) { (void)l;(void)n;return compressed; }
static bool ail_is_level_twiddled_uncompressed(const struct ail_layout *l,unsigned n) { (void)l;(void)n;return twiddled; }
static void agx_prepare_for_map(struct pipe_context *p,struct agx_resource *r,unsigned l,unsigned u,const struct pipe_box *b,bool s) { (void)p;(void)r;(void)l;(void)u;(void)b;(void)s; }
static void util_range_set_empty(unsigned *r) { *r=0; }
static void util_range_add(struct pipe_resource *r,unsigned *v,int x,int end) { (void)r;(void)x;*v=end; }
static struct agx_resource *agx_alloc_staging(void *s,struct agx_resource *r,unsigned l,const struct pipe_box *b) { (void)s;(void)r;(void)l;(void)b;staging.base.refs=1;return &staging; }
static unsigned ail_get_linear_stride_B(const struct ail_layout *l,unsigned n) { (void)l;(void)n;return 16; }
static bool agx_resource_valid(struct agx_resource *r,unsigned l) { (void)r;(void)l;return true; }
static void agx_blit_to_staging(struct pipe_context *p,struct agx_transfer *t) { (void)p;(void)t; }
static void agx_sync_writer(struct pipe_context *p,struct agx_resource *r,const char *s) { (void)p;(void)r;(void)s; }
static void *agx_bo_map(void *bo) { return ++maps==fail_map ? NULL:bo; }
static unsigned util_format_get_stride(unsigned f,int w) { (void)f;return w; }
static unsigned util_format_get_2d_size(unsigned f,unsigned s,int h) { (void)f;return s*h; }
static void *agx_map_texture_cpu(struct agx_resource *r,unsigned l,unsigned z) { (void)l;(void)z;return agx_bo_map(r->bo); }
static void ail_detile(void *m,void *d,const struct ail_layout *l,unsigned level,unsigned stride,int x,int y,int w,int h) { (void)l;(void)level;(void)stride;(void)x;(void)y;(void)w;(void)h;assert(m && d);++detiles; }
static unsigned ail_get_linear_pixel_B(const struct ail_layout *l,unsigned level,int x,int y,int z) { (void)l;(void)level;(void)y;(void)z;return x; }
static void *agx_transfer_map(struct pipe_context *pctx,struct pipe_resource *resource,unsigned level,unsigned usage,const struct pipe_box *box,struct pipe_transfer **out_transfer)
@@MAP@@
static void *pipe_buffer_map_range(struct pipe_context *p,struct pipe_resource *r,unsigned x,unsigned n,unsigned u,struct pipe_transfer **t) { struct pipe_box b={x,0,0,n,1,1};return agx_transfer_map(p,r,0,u,&b,t); }
static void pipe_buffer_unmap(struct pipe_context *p,struct pipe_transfer *t) { (void)p;pipe_resource_reference(&t->resource,NULL);++freed;free(t); }
static void util_draw_indirect(struct pipe_context *pipe,const struct pipe_draw_info *info_in,unsigned drawid_offset,const struct pipe_draw_indirect_info *indirect)
@@DRAW@@
static void draw(struct pipe_context *p,const struct pipe_draw_info *i,unsigned id,const void *ind,const struct pipe_draw_start_count_bias *d,unsigned n) { (void)p;(void)id;assert(!ind && n==1 && d->count==7 && i->instance_count==2);++draws; }
int main(void) {
   uint32_t data[16]={1,0,7,2,0,0};
   struct agx_resource r={0};r.base.target=PIPE_BUFFER;r.bo=data;r.layout.levels=1;
   struct pipe_context ctx={0};ctx.draw_vbo=draw;
   struct pipe_draw_info info={0};
   struct pipe_draw_indirect_info ind={&r.base,&r.base,0,8,16,1,NULL};
   /* Count failure, nonzero-offset argument failure, then successful draw. */
   for (unsigned scenario=1;scenario<=3;++scenario) {
      maps=freed=draws=ctx.faults=0;fail_map=scenario;
      util_draw_indirect(&ctx,&info,0,&ind);
      assert(r.base.refs==0);
      assert(ctx.faults==(scenario<3));assert(draws==(scenario==3));
      assert(freed==(scenario==1?1:2));
   }
   /* Direct caller: NULL + nonzero offset must not become an address. */
   struct pipe_transfer *t=(void *)1;
   maps=freed=ctx.faults=0;fail_map=1;
   assert(!pipe_buffer_map_range(&ctx,&r.base,8,4,PIPE_MAP_READ,&t));
   assert(!t && ctx.faults==1 && freed==1 && r.base.refs==0);
   /* Texture reads share the producer: never detile a failed BO map. */
   r.base.target=2;
   for(unsigned mode=0;mode<2;++mode) {
      compressed=mode;twiddled=!mode;staging.bo=data;
      maps=freed=ctx.faults=detiles=0;fail_map=1;
      assert(!pipe_buffer_map_range(&ctx,&r.base,0,4,PIPE_MAP_READ,&t));
      assert(!t && ctx.faults==1 && freed==1 && r.base.refs==0 && staging.base.refs==0 && detiles==0);
   }
}
'''

class BufferMapFailure(unittest.TestCase):
    def test_failed_map_cannot_reach_indirect_cpu_read(self):
        with tempfile.TemporaryDirectory(prefix='t6b-map-') as tmp:
            out = project(Path(tmp) / 'out')
            if out is None:
                self.skipTest('reference Mesa tree not available')
            body = function_body((out/'src/gallium/drivers/asahi/agx_pipe.c').read_text(), 'agx_transfer_map')
            draw = function_body((local_tree('reference', 'mesa')/'src/gallium/auxiliary/util/u_draw.c').read_text(), 'util_draw_indirect')
            src, exe = Path(tmp)/'test.c', Path(tmp)/'test'
            src.write_text(HARNESS.replace('@@MAP@@', body).replace('@@DRAW@@', draw))
            built = subprocess.run(['clang','-std=c11','-Wall','-Wextra','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run = subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_owner_read_reports_device_hung_without_copying(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Failed owner CPU read must not report recoverable allocation OOM,
        copy data, or leak a returned transfer; malformed successful maps
        without a transfer must not reach unmap.
        """
        root = Path(__file__).resolve().parents[1]
        owner = (root/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        start = owner.index('extern "C" HRESULT AgxD3d12OwnerReadBuffer(')
        end = owner.index('\nextern "C"', start+1)
        harness = r'''
#include <cassert>
#include <cstring>
#include <cstddef>
typedef int HRESULT;
enum { E_INVALIDARG=-1, E_OUTOFMEMORY=-2, DXGI_ERROR_DEVICE_HUNG=-3, S_OK=0, PIPE_MAP_READ=1 };
struct pipe_resource {};
struct pipe_transfer {};
struct pipe_box { int x,width,height,depth; };
struct pipe_context {
 void *(*buffer_map)(pipe_context *,pipe_resource *,int,int,const pipe_box *,pipe_transfer **);
 void (*buffer_unmap)(pipe_context *,pipe_transfer *);
};
struct AGX_D3D12_OWNER { pipe_context *Context; };
struct AGX_D3D12_BUFFER { void *Resource; unsigned long long Bytes; };
struct OwnerLock { OwnerLock(AGX_D3D12_OWNER *) {} };
static unsigned mode,unmaps;
static unsigned word=0xabc123;
static pipe_transfer transfer;
static void *map(pipe_context *,pipe_resource *,int,int,const pipe_box *b,pipe_transfer **t) {
 assert(b->x==4 && b->width==4);*t=mode==2?NULL:&transfer;return mode==0?NULL:&word;
}
static void unmap(pipe_context *,pipe_transfer *t) { assert(t);++unmaps; }
@@OWNER@@
int main() {
 pipe_context p={map,unmap};AGX_D3D12_OWNER o={&p};pipe_resource r;AGX_D3D12_BUFFER b={&r,16};
 for(mode=0;mode<3;++mode) {
   unsigned data=0xdeadbeef;unmaps=0;
   HRESULT hr=AgxD3d12OwnerReadBuffer(&o,&b,4,4,&data);
   assert(hr==(mode==1?S_OK:DXGI_ERROR_DEVICE_HUNG));
   assert(data==(mode==1?word:0xdeadbeef));assert(unmaps==(mode==2?0:1));
 }
}
'''
        with tempfile.TemporaryDirectory(prefix='t6b-owner-read-') as tmp:
            src, exe = Path(tmp)/'test.cpp', Path(tmp)/'test'
            src.write_text(harness.replace('@@OWNER@@',owner[start:end]))
            subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
            run = subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
