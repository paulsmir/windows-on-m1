"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

EXP1925: replacement allocation failed during texture legalization, then the
release UMD passed NULL to agx_flush_writer (ARM64 RVA 0x43fb4). A failed
transition must fault the context without replacing the original BO/layout;
image binding must stop before asserting that the failed conversion succeeded.
Compile the actual build projection, with allocation/copy/flush fault injection.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, project


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#define PIPE_BIND_SHARED 1
#define PIPE_MAX_TEXTURE_LEVELS 4
#define BITSET_FOREACH_SET(i, bits, n) for (i=0;i<n;++i) if ((bits)[0] & (1u<<i))
struct pipe_screen;
struct pipe_resource { unsigned bind; };
struct agx_bo { int contents[4]; };
struct ail_layout { unsigned width_px, height_px; };
struct agx_resource {
   struct pipe_resource base;
   unsigned data_valid[1];
   struct ail_layout layout;
   unsigned modifier;
   struct agx_bo *bo;
};
struct pipe_screen {
   struct pipe_resource *(*resource_create)(struct pipe_screen *, const struct pipe_resource *);
};
struct pipe_context { struct pipe_screen *screen; };
struct agx_context { struct pipe_context base; bool any_faults; };
struct pipe_box { unsigned width, height, depth; };
static struct agx_resource *agx_resource(struct pipe_resource *r) { return (struct agx_resource *)r; }
static struct agx_context *agx_context(struct pipe_context *p) { return (struct agx_context *)p; }
static unsigned u_minify(unsigned n, unsigned level) { return (n>>level)?(n>>level):1; }
static unsigned util_num_layers(struct pipe_resource *r, int level) { (void)r;(void)level;return 1; }
static void u_box_3d(int x,int y,int z,unsigned w,unsigned h,unsigned d,struct pipe_box *b) {
   assert(!x && !y && !z); *b=(struct pipe_box){w,h,d};
}
static unsigned allocations, copies, flushes, frees, fault_site;
static unsigned fail_copy;
static bool fail_alloc, fail_flush;
static struct agx_bo *original_bo;
static void AgxWin32AsahiMarkContextFault(struct agx_context *c,unsigned site) {
   c->any_faults=true; fault_site=site;
}
static struct pipe_resource *create(struct pipe_screen *s,const struct pipe_resource *t) {
   (void)s; ++allocations; if(fail_alloc) return NULL;
   struct agx_resource *r=calloc(1,sizeof(*r));
   r->base=*t; r->layout=(struct ail_layout){64,48}; r->modifier=22;
   r->bo=calloc(1,sizeof(*r->bo));return &r->base;
}
static void agx_resource_copy_region(struct pipe_context *p,struct pipe_resource *dst,
   int level,int x,int y,int z,struct pipe_resource *src,int src_level,struct pipe_box *box) {
   assert(dst && src && !x && !y && !z && level==src_level);
   assert(box->width==u_minify(32,level) && box->height==u_minify(16,level));
   ++copies;
   if(fail_copy==copies) {agx_context(p)->any_faults=true;return;}
   agx_resource(dst)->bo->contents[level]=agx_resource(src)->bo->contents[level];
}
static void agx_flush_writer(struct agx_context *c,struct agx_resource *r,const char *why) {
   assert(r && r->bo && why); ++flushes; if(fail_flush)c->any_faults=true;
}
static void pipe_resource_reference(struct pipe_resource **p,struct pipe_resource *r) {
   assert(!r && *p); ++frees;
   free(agx_resource(*p)->bo);free(*p);*p=NULL;
}
'''

MAIN = r'''
int main(int argc,char **argv) {
   assert(argc==2); unsigned mode=(unsigned)atoi(argv[1]);
   struct pipe_screen screen={create};struct agx_context ctx={{&screen},false};
   struct agx_resource r={.base={0},.data_valid={5},.layout={32,16},.modifier=11};
   r.bo=calloc(1,sizeof(*r.bo)); r.bo->contents[0]=17;r.bo->contents[2]=39;
   original_bo=r.bo;struct pipe_resource templ={2};
   if(mode==0) {fail_alloc=true;r.data_valid[0]=0;} // exact empty-new-texture failure
   if(mode==1) fail_alloc=true; // failure with valid mip levels too
   if(mode==2) ctx.any_faults=true;
   if(mode==3) fail_copy=2;
   if(mode==4) fail_flush=true;
   if(mode==6) r.data_valid[0]=0;
   transition_resource(&ctx.base,&r,&templ);
   if(mode<5) {
      assert(ctx.any_faults && r.bo==original_bo && r.base.bind==0 && r.modifier==11);
      assert(r.bo->contents[0]==17 && r.bo->contents[2]==39);
      assert(r.layout.width_px==32 && r.layout.height_px==16);
      if(mode<2) assert(allocations==1 && !copies && !flushes && !frees && (fault_site>>16)==2);
      if(mode==2) assert(!allocations && !copies && !flushes && !frees);
      if(mode==3) assert(copies==2 && !flushes && frees==1);
      if(mode==4) assert(copies==2 && flushes==1 && frees==1);
   } else {
      assert(!ctx.any_faults && r.bo!=original_bo && r.base.bind==2 && r.modifier==22);
      assert(r.layout.width_px==64 && r.layout.height_px==48);
      assert(allocations==1 && copies==(mode==6?0u:2u) && flushes==1 && frees==1);
      if(mode==5) assert(r.bo->contents[0]==17 && r.bo->contents[2]==39);
   }
   free(r.bo);
}
'''


class TransitionFailure(unittest.TestCase):
    def test_blit_stops_after_failed_legalization(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A failed layout transition must not enter the blitter with an incompatible resource."""
        with tempfile.TemporaryDirectory(prefix='agx-blit-transition-') as tmp:
            out = project(Path(tmp)/'out')
            if out is None:
                self.skipTest('reference Mesa tree not available')
            blit = (out/'src/gallium/drivers/asahi/agx_blit.c').read_text()
            body = function_body(blit, 'agx_blit')
            harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#define ASAHI_BLIT 1
#define ASAHI_DISABLE_RENDER_COND 2
#define UNREACHABLE(s) abort()
struct agx_resource { unsigned format; };
struct pipe_blit_info { struct {struct agx_resource *resource;unsigned format;struct {int z,depth;} box;} src,dst;bool render_condition_enable; };
struct device {void *windows_private;};
struct pipe_context {struct device *screen;};
struct agx_context {struct pipe_context base;bool any_faults;void *blitter;unsigned compute_blitter;};
#define agx_context(p) ((struct agx_context *)(p))
#define agx_resource(r) (r)
#define agx_device(s) (s)
static unsigned legalizations, uses, fail_at;
static bool agx_render_condition_check(struct agx_context *c) {(void)c;return true;}
static void agx_legalize_compression(struct agx_context *c,struct agx_resource *r,unsigned f) {
 (void)r;(void)f; if(++legalizations==fail_at)c->any_faults=true;
}
static bool util_format_is_compressed(unsigned f) {(void)f;return false;}
static bool asahi_compute_blit_supported(const struct pipe_blit_info *i) {(void)i;return false;}
static void asahi_compute_blit(struct pipe_context *p,const struct pipe_blit_info *i,unsigned *b) {(void)p;(void)i;(void)b;++uses;}
static bool util_blitter_is_blit_supported(void *b,const struct pipe_blit_info *i) {(void)b;(void)i;++uses;return true;}
static void util_dump_blit_info(FILE *f,const struct pipe_blit_info *i) {(void)f;(void)i;}
static void agx_flush_writer(struct agx_context *c,struct agx_resource *r,const char *s) {(void)c;(void)r;(void)s;++uses;}
static void agx_blitter_save(struct agx_context *c,void *b,unsigned op) {(void)c;(void)b;(void)op;++uses;}
static void util_blitter_blit(void *b,const struct pipe_blit_info *i,void *x) {(void)b;(void)i;(void)x;++uses;}
static void agx_blit(struct pipe_context *pipe,const struct pipe_blit_info *info)
'''+body+r'''
int main(int argc,char **argv) {
 assert(argc==2);fail_at=atoi(argv[1]);struct device d={&d};struct agx_context c={{&d},false,0,0};
 struct agx_resource r={0};struct pipe_blit_info i={{&r,1},{&r,1},false};
 agx_blit(&c.base,&i);
 if(fail_at) assert(c.any_faults && !uses);
 else assert(!c.any_faults && uses==4);
}
'''
            src=Path(tmp)/'blit.c';exe=Path(tmp)/'blit';src.write_text(harness)
            built=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            for mode in range(3):
                with self.subTest(mode=mode):
                    ran=subprocess.run([str(exe),str(mode)],capture_output=True,text=True)
                    self.assertEqual(ran.returncode,0,ran.stderr)

    def test_image_binding_stops_after_failed_legalization(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Failed decompression must not assert writeable_image or bind a view whose layout was never legalized."""
        with tempfile.TemporaryDirectory(prefix='agx-image-transition-') as tmp:
            out = project(Path(tmp)/'out')
            if out is None:
                self.skipTest('reference Mesa tree not available')
            state = (out/'src/gallium/drivers/asahi/agx_state.c').read_text()
            body = function_body(state, 'agx_set_shader_images')
            harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define AGX_STAGE_DIRTY_IMAGE 1
#define PIPE_IMAGE_ACCESS_DRIVER_INTERNAL 1
#define PIPE_IMAGE_ACCESS_WRITE 2
#define BITFIELD_BIT(x) (1u<<(x))
#define BITFIELD64_MASK(x) ((1ull<<(x))-1)
typedef unsigned mesa_shader_stage;
struct pipe_resource { unsigned dummy; };
struct agx_resource { struct pipe_resource base; struct {bool writeable_image;} layout; };
struct pipe_image_view { struct pipe_resource *resource; unsigned access,shader_access,format; };
struct pipe_context { unsigned dummy; };
struct agx_context {struct pipe_context base; bool any_faults;
   struct {unsigned dirty;uint64_t image_mask;struct pipe_image_view images[32];} stage[1];};
static struct agx_context *agx_context(struct pipe_context *p) {return (struct agx_context *)p;}
static struct agx_resource *agx_resource(struct pipe_resource *p) {return (struct agx_resource *)p;}
static unsigned copies, decompressions, legalizations;
static bool fail;
static void pipe_resource_reference(struct pipe_resource **dst,struct pipe_resource *src) {*dst=src;}
static void util_copy_image_view(struct pipe_image_view *dst,const struct pipe_image_view *src) {
   ++copies;*dst=src?*src:(struct pipe_image_view){0};
}
static void agx_decompress(struct agx_context *c,struct agx_resource *r,const char *s) {
   (void)s;++decompressions;if(fail)c->any_faults=true;else r->layout.writeable_image=true;
}
static void agx_legalize_compression(struct agx_context *c,struct agx_resource *r,unsigned fmt) {
   (void)r;(void)fmt;++legalizations;if(fail)c->any_faults=true;
}
static void agx_set_shader_images(struct pipe_context *pctx,mesa_shader_stage shader,
 unsigned start_slot,unsigned count,unsigned unbind_num_trailing_slots,const struct pipe_image_view *iviews)
'''+body+r'''
int main(int argc,char **argv) {
   assert(argc==2);unsigned mode=atoi(argv[1]);fail=mode<2;
   struct agx_context c={0};struct agx_resource r={0};
   struct pipe_image_view v={&r.base,0,mode==1?0:PIPE_IMAGE_ACCESS_WRITE,1};
   agx_set_shader_images(&c.base,0,0,1,0,&v);
   if(fail) assert(c.any_faults && !copies && !c.stage[0].images[0].resource);
   else assert(!c.any_faults && copies==1 && c.stage[0].images[0].resource==&r.base);
   // Cleanup/unbind must still work after faulting.
   agx_set_shader_images(&c.base,0,0,1,0,NULL);
   assert(!c.stage[0].images[0].resource && !c.stage[0].image_mask);
}
'''
            src=Path(tmp)/'images.c';exe=Path(tmp)/'images';src.write_text(harness)
            built=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            for mode in range(3):
                with self.subTest(mode=mode):
                    ran=subprocess.run([str(exe),str(mode)],capture_output=True,text=True)
                    self.assertEqual(ran.returncode,0,ran.stderr)

    def test_projected_transition_preserves_original_on_failure(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? OOM must not dereference NULL or swap incomplete replacement storage; success still copies all valid levels."""
        with tempfile.TemporaryDirectory(prefix='agx-transition-') as tmp:
            out = project(Path(tmp)/'out')
            if out is None:
                self.skipTest('reference Mesa tree not available')
            pipe = (out/'src/gallium/drivers/asahi/agx_pipe.c').read_text()
            body = function_body(pipe, 'transition_resource')
            src=Path(tmp)/'replay.c'; exe=Path(tmp)/'replay'
            src.write_text(PRELUDE + '\nstatic void transition_resource(struct pipe_context *pctx, '
                           'struct agx_resource *rsrc, struct pipe_resource *templ)\n' + body + MAIN)
            built=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',
                                  str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            for mode in range(7):
                with self.subTest(mode=mode):
                    ran=subprocess.run([str(exe),str(mode)],capture_output=True,text=True)
                    self.assertEqual(ran.returncode,0,ran.stderr)


if __name__ == '__main__':
    unittest.main()
