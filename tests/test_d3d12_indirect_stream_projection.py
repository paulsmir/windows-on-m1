"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
GPU stream projection must select one native draw, preserve the real USC ABI,
link its compiled kernel, and keep GPU outputs unmapped and unpublished.
Compile the complete affected native translation units with real Mesa types.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
import importlib.util
import hashlib
import json
from test_d3d10_shader_fields_init import ROOT, local_tree, project
from test_d3d12_owner_allocation_failure import function
from test_d3d12_indirect_stream import WINSYS

class IndirectStreamProjection(unittest.TestCase):
    def test_encoder_continuation_coordinates_follow_rollover(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A stream must resume in the current encoder chunk, including after
        rollover; using the original BO's address corrupts the VDM branch.
        Execute the projected allocator and rollover with distinct GPU bases.
        """
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            out = project(p/'native')
            header = (out/'src/gallium/drivers/asahi/agx_state.h').read_text()
            encoder = header[header.index('struct agx_encoder {'):].split('};', 1)[0] + '};'
            allocate = function((out/'src/gallium/drivers/asahi/agx_batch.c').read_text(),
                                'struct agx_encoder\nagx_encoder_allocate(')
            rollover = function((out/'src/gallium/drivers/asahi/agx_state.c').read_text(),
                                'static void\nagx_ensure_cmdbuf_has_space(')
            code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define likely(x) (x)
#define AGX_VDM_STREAM_LINK_LENGTH 8
#define AGX_CDM_STREAM_LINK_LENGTH 8
struct agx_ptr {void *cpu; uint64_t gpu;};
struct va {uint64_t addr;};
struct agx_bo {struct va *va; size_t size;};
struct agx_device {unsigned unused;};
struct agx_pool {unsigned unused;};
struct agx_context {bool any_faults;};
static uint8_t initial[0x80000], next[65536];
static struct va address={0x1100040000ull};
static struct agx_bo bo={&address,sizeof(initial)};
static unsigned allocations, jumps, fail;
static struct agx_bo *AgxWin32AsahiEncoderCreate(struct agx_device *d,size_t n,unsigned f,const char *s){(void)d;(void)f;(void)s;assert(n==sizeof(initial));return &bo;}
static void *agx_bo_map(struct agx_bo *b){assert(b==&bo);return initial;}
static void agx_bo_unreference(struct agx_device *d,struct agx_bo *b){(void)d;(void)b;assert(0);}
static struct agx_ptr agx_pool_alloc_aligned(struct agx_pool *p,size_t n,unsigned a){(void)p;assert(n==sizeof(next)&&a==256);++allocations;return fail?(struct agx_ptr){0}:(struct agx_ptr){next,0x1234000000ull};}
static void AgxWin32AsahiMarkContextFault(struct agx_context *c,unsigned site){assert(site);c->any_faults=true;}
static void agx_cs_jump(uint32_t *p,uint64_t target,bool vdm){assert(p==(void *)(initial+sizeof(initial)-16)&&target==0x1234000000ull&&vdm);++jumps;}
''' + encoder + r'''
struct agx_batch {struct agx_context *ctx;struct agx_encoder vdm,cdm;struct agx_pool pool;};
''' + allocate + '\n' + rollover + r'''
int main(void){struct agx_device d={0};struct agx_context c={0};struct agx_batch b={.ctx=&c};
b.vdm=agx_encoder_allocate(&b,&d);
assert(b.vdm.windows_chunk.cpu==initial&&b.vdm.windows_chunk.gpu==0x1100040000ull);
agx_ensure_cmdbuf_has_space(&b,&b.vdm,32);assert(!allocations&&!jumps);
b.vdm.current=initial+sizeof(initial)-16;fail=1;
agx_ensure_cmdbuf_has_space(&b,&b.vdm,32);
assert(c.any_faults&&!jumps&&b.vdm.windows_chunk.cpu==initial);
fail=0;c.any_faults=false;agx_ensure_cmdbuf_has_space(&b,&b.vdm,32);
assert(!c.any_faults&&jumps==1&&b.vdm.current==next&&b.vdm.end==next+sizeof(next));
assert(b.vdm.windows_chunk.cpu==next&&b.vdm.windows_chunk.gpu==0x1234000000ull);
b.vdm.current+=68;agx_ensure_cmdbuf_has_space(&b,&b.vdm,32);
assert(allocations==2&&jumps==1&&b.vdm.windows_chunk.cpu==next&&b.vdm.windows_chunk.gpu==0x1234000000ull);
return 0;}
'''
            (p/'encoder.c').write_text(code)
            r = subprocess.run(['clang', '-std=c11', '-Wall', '-Werror',
                                '-fsanitize=address,undefined', str(p/'encoder.c'),
                                '-o', str(p/'encoder')], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            r = subprocess.run([str(p/'encoder')], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)

    def test_projection_uses_one_draw_and_links_kernel(self):
        with tempfile.TemporaryDirectory() as td:
            out=project(Path(td)/'out')
            state=(out/'src/gallium/drivers/asahi/agx_state.c').read_text()
            wrapper=function(state[state.rindex('static void\nagx_draw_vbo('):],
                             'static void\nagx_draw_vbo(')
            self.assertIn('agx_win32_stream_fallback(',wrapper,
                          'pure indirect draws still recursively encode MaxCount')
            self.assertLess(wrapper.index('agx_win32_stream_fallback('),
                            wrapper.index('agx_win32_draw_indirect_count('))
            self.assertLess(wrapper.index('agx_win32_stream_flush_inputs('),
                            wrapper.index('AgxWin32AsahiBatchEnter('))
            self.assertIn('stream_indirect.draw_count = 1;',wrapper)
            self.assertIn('stream_indirect.indirect_draw_count = NULL;',wrapper)
            self.assertLess(wrapper.index('AgxWin32AsahiBatchEnter('),
                            wrapper.index('ctx->windows_indirect_stream = &stream;'))
            self.assertIn('ctx->windows_indirect_stream = NULL;',wrapper)
            body=function(state,'static void\nagx_draw_vbo_windows_body(')
            self.assertIn('agx_win32_emit_indirect_stream(',body)
            self.assertIn('ctx->dirty |= AGX_DIRTY_VS;',body,
                          'generated USC selection must be invalidated for the next draw')
            # RC69: the stream repeats the template's complete native VS
            # block, captured right after agx_encode_state emits and pads it.
            encode=function(state,'static uint8_t *\nagx_encode_state(')
            vs_block=encode[encode.index('cfg.vertex_shader_word_0_present = true;'):]
            vs_block=vs_block[:vs_block.index('ctx->windows_indirect_stream->vs_state_valid = true;')]
            for needle in ('VDM_STATE_VERTEX_SHADER_WORD_0','VDM_STATE_VERTEX_SHADER_WORD_1',
                           'VDM_STATE_VERTEX_OUTPUTS','VDM_STATE_VERTEX_UNKNOWN',
                           'memset(out, 0, 4);\n      out += 4;',
                           'out - sizeof(ctx->windows_indirect_stream->vs_state)'):
                self.assertIn(needle,vs_block)
            usc=(out/'src/gallium/drivers/asahi/agx_win32_pipeline.inc').read_text()
            self.assertIn('params_offset = b.head - (uint8_t *)t.cpu;',usc)
            self.assertIn('usc_bytes = b.head - (uint8_t *)t.cpu;',usc)
            library=(out/'src/asahi/lib/libagx_shaders.h').read_text()
            self.assertIn('LIBAGX_WIN32_INDIRECT_STREAM,',library)
            self.assertIn('#define libagx_win32_indirect_stream(',library)
            batch=(out/'src/gallium/drivers/asahi/agx_batch.c').read_text()
            self.assertIn('AGX_WIN32_BO_GPU_ONLY | AGX_BO_LOW_VA, false)',batch)
            # RC71: the retired batch gives its outputs back to the context
            self.assertIn('agx_win32_stream_scratch_retire(ctx, &batch->windows_stream_pool, 0)',batch)
            self.assertIn('agx_win32_stream_scratch_retire(ctx, &batch->windows_stream_usc_pool, 1)',batch)
            # Output pools are retained by normal BO references, never sent
            # through the CPU shadow publisher before/after the GPU writes.
            publish=function((WINSYS/'agx_win32_gpuva_batch.c').read_text(),
                             'static int publish_shadows(')
            self.assertNotIn('windows_stream_pool',publish)
            self.assertNotIn('windows_stream_usc_pool',publish)

    def test_native_ia_is_suppressed_only_for_stream(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Native IA must not count the template in addition to GPU stream records,
        especially when the count is zero. Ordinary and disabled-query behavior
        remains native. Execute the projected gate with each combination.
        """
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);out=project(p/'native')
            state=(out/'src/gallium/drivers/asahi/agx_state.c').read_text()
            start=state.index('   /* Increment IA statistics')
            guard=state[state.index('   if (',start):state.index('      uint64_t ptr;',start)]
            code='''#include <stdbool.h>
#include <assert.h>
#include "pipe/p_defines.h"
struct context {bool windows_indirect_stream,windows_query_ia_records,active_queries,in_tess,active_draw_without_restart;unsigned pipeline_statistics[PIPE_STAT_QUERY_COUNT];struct {void *shader;} stage[MESA_SHADER_STAGES];};
static bool native(struct context *ctx){
'''+guard+'''return true;}return false;}
int main(void){struct context c={0};c.pipeline_statistics[PIPE_STAT_QUERY_IA_VERTICES]=1;
/* RC67: records already counted by one batched dispatch are skipped too. */
for(unsigned stream=0;stream<2;++stream)for(unsigned batched=0;batched<2;++batched)for(unsigned active=0;active<2;++active){c.windows_indirect_stream=stream;c.windows_query_ia_records=batched;c.active_queries=active;assert(native(&c)==(!stream&&!batched&&active));}}
'''
            (p/'gate.c').write_text(code)
            mesa=local_tree('reference','mesa')
            r=subprocess.run(['clang','-std=c11','-DHAVE___BUILTIN_CLZ','-I'+str(mesa/'src'),'-I'+str(mesa/'include'),'-I'+str(mesa/'src/gallium/include'),str(p/'gate.c'),'-o',str(p/'gate')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(p/'gate')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)

    def test_generated_bundle_matches_sources_and_both_chip_tables(self):
        script=ROOT/'drivers/apple-agx/mesa/scripts/native-asahi-indirect-stream.py'
        spec=importlib.util.spec_from_file_location('stream_projection',script)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        directory=module.generated(ROOT)  # verifies source/output SHA-256 pins
        build=local_tree('accelerated-desktop-ad03','mesa-build')
        source=(build/'src/asahi/lib/libagx_shaders.c').read_text()
        merged=module.project_library_c(source,ROOT)
        for chip in ('g13g','g13x'):
            table=merged.split('const uint32_t *libagx_'+chip+'[',1)[1]
            table=table[:table.index('};')]
            self.assertIn('[LIBAGX_WIN32_INDIRECT_STREAM] = libagx_win32_indirect_stream_0_'+chip+',',table)
            self.assertIn('[LIBAGX_HELPER]',table)
        # Ordinary compiler bundle remains byte-identical, except insertion of
        # the additional kernel/entry. Repeat on a fresh input, never duplicate.
        self.assertEqual(merged.count('static const uint32_t libagx_win32_indirect_stream_'),2)
        manifest=json.loads((directory/'manifest.json').read_text())
        self.assertEqual(manifest['targets'],['g13g','g13x'])
        mesa=local_tree('reference','mesa')
        self.assertEqual(hashlib.sha256((mesa/'src/asahi/libagx/libagx_dgc.h').read_bytes()).hexdigest(),
                         manifest['inputs']['libagx_dgc.h'])
        self.assertEqual(hashlib.sha256((mesa/'src/compiler/shader_enums.h').read_bytes()).hexdigest(),
                         manifest['inputs']['shader_enums.h'])
        closure=(ROOT/'drivers/apple-agx/mesa/scripts/build-asahi-runtime-closure.py').read_text()
        self.assertIn('stream_module.project_library_c(projected_c.read_text(), args.project)',closure)

    def test_gpu_output_pool_never_maps_or_publishes_cpu_bytes(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);out=project(p/'native')
            allocator=function((out/'src/asahi/lib/pool.c').read_text(),
                               'struct agx_ptr\nagx_pool_alloc_aligned_with_bo(')
            code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#define AGX_WIN32_BO_GPU_ONLY (1u<<29)
#define ALIGN_POT(x,a) (((x)+(a)-1)&~((a)-1))
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define POOL_SLAB_SIZE (256*1024)
#define unlikely(x) (x)
static unsigned util_next_power_of_two(unsigned n){unsigned p=1;while(p<n)p*=2;return p;}
struct va {uint64_t addr;};
struct agx_bo {struct va *va;};
struct agx_pool {struct agx_bo *transient_bo;unsigned transient_offset,create_flags;};
struct agx_ptr {void *cpu;uint64_t gpu;};
static struct va va={0x1100020000ull};static struct agx_bo bo={&va};
static unsigned maps,backs;static bool fail;
static struct agx_bo *agx_pool_alloc_backing(struct agx_pool *p,size_t bytes){assert(bytes);++backs;p->transient_offset=0;p->transient_bo=fail?NULL:&bo;return p->transient_bo;}
static void *agx_bo_map(struct agx_bo *b){assert(b==&bo);++maps;return (void *)(uintptr_t)0x10000;}
'''+allocator+r'''
int main(void){
 const unsigned maxima[]={2400,12000,60000};
 for(unsigned mi=0;mi<3;++mi)for(unsigned gpu=0;gpu<2;++gpu){
  struct agx_pool p={0};p.create_flags=gpu?AGX_WIN32_BO_GPU_ONLY:0;maps=backs=0;
  struct agx_bo *owned=NULL;
  const unsigned bytes=28*maxima[mi]+8+0x800;
  struct agx_ptr a=agx_pool_alloc_aligned_with_bo(&p,bytes,256,&owned);
  assert(owned==&bo&&a.gpu==va.addr&&backs==1&&maps==!gpu);
  assert(gpu?!a.cpu:a.cpu==(void *)(uintptr_t)0x10000);
  assert(p.transient_offset==bytes);
  for(unsigned call=1;call<7;++call){
   a=agx_pool_alloc_aligned_with_bo(&p,bytes,256,&owned);
   assert(owned==&bo && !(a.gpu&255) && p.transient_offset>=bytes);
   if(gpu)assert(!maps&&!a.cpu);
  }
  /* USC clones grow independently, in aligned strides, and remain GPU-only. */
  p=(struct agx_pool){0};p.create_flags=AGX_WIN32_BO_GPU_ONLY;maps=0;
  for(unsigned call=0;call<7;++call){
   a=agx_pool_alloc_aligned_with_bo(&p,128*maxima[mi],64,&owned);
   assert(a.gpu&&!(a.gpu&63)&&owned==&bo&&!a.cpu&&!maps);
   assert(p.transient_offset>=128*maxima[mi]);
  }
 }
 struct agx_pool p={0};p.create_flags=AGX_WIN32_BO_GPU_ONLY;fail=true;
 struct agx_bo *owned=&bo;struct agx_ptr a=agx_pool_alloc_aligned_with_bo(&p,64,64,&owned);
 assert(!a.gpu&&!a.cpu&&!owned);
 return 0;
}
'''
            (p/'pool.c').write_text(code)
            r=subprocess.run(['clang','-std=c11','-Wall','-Werror','-fsanitize=address,undefined',str(p/'pool.c'),'-o',str(p/'pool')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(p/'pool')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)

    def test_complete_projected_translation_units_compile(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Reduced harness types can hide an invalid native field or undeclared
        injected helper. Compile full state/pool/lifecycle and winsys units
        against the actual projection, plus the extended libagx binary table.
        This host compile is not the Windows ABI/package compile gate.
        """
        from test_dxbc_to_nir import host_flags
        mesa=local_tree('reference','mesa');build=local_tree('accelerated-desktop-ad03','mesa-build')
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);out=project(p/'native')
            device=out/'src/asahi/lib/agx_device.h'
            device.write_text(device.read_text().replace(
                '#ifdef _WIN32\n   void *windows_private; /* Windows native allocation owner, never a GPU address */\n#endif',
                '   void *windows_private; /* Host compile platform shim. */'))
            (p/'xf86drm.h').write_text('/* Host declaration for the unused Linux import branch. */\n#pragma once\nint drmSyncobjImportSyncFile(int, unsigned, int);\n')
            includes=[p,out/'src',out/'include',out/'include/drm-uapi',
                      out/'src/asahi/lib',out/'src/asahi/libagx',out/'src/asahi/layout',
                      out/'src/asahi/compiler',out/'src/gallium/drivers/asahi',
                      build/'src/asahi',build/'src/asahi/genxml',build/'src/asahi/libagx',
                      build/'src/poly',build/'src/compiler/nir',
                      mesa/'src/gallium/auxiliary',mesa/'src/gallium/include',mesa/'src/virtio/vdrm',
                      WINSYS,ROOT/'drivers/apple-agx/shared/include']
            flags=['-std=c11','-DAPPLE_AGX_GPUVA_WINSYS','-Wno-c23-extensions',
                   '-Werror=implicit-function-declaration',
                   '-include',str(p/'xf86drm.h'),
                   *['-I'+str(d) for d in includes],*host_flags(build)]
            script=ROOT/'drivers/apple-agx/mesa/scripts/native-asahi-indirect-stream.py'
            spec=importlib.util.spec_from_file_location('stream_projection_compile',script)
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            library=out/'src/asahi/lib/libagx_shaders.c'
            library.write_text(module.project_library_c((build/'src/asahi/lib/libagx_shaders.c').read_text(),ROOT))
            for unit in (out/'src/gallium/drivers/asahi/agx_state.c',
                         out/'src/gallium/drivers/asahi/agx_batch.c',
                         out/'src/asahi/lib/pool.c',library,
                         WINSYS/'agx_win32_asahi_scene.c',
                         WINSYS/'agx_win32_asahi_bo.c'):
                with self.subTest(unit=unit.name):
                    r=subprocess.run(['clang',*flags,'-fsyntax-only',str(unit)],capture_output=True,text=True)
                    self.assertEqual(r.returncode,0,r.stderr)


# IE1a (APPLE_AGX_D3D12_EXPAND_AHEAD, agx_win32_expand_ahead.h): the glue
# harness (production admission/emission, real kernel) with recording stubs.
IE1A_STUBS = {
    'static void agx_batch_add_bo(struct agx_batch *b,struct agx_bo *bo){assert(bo==&output_bos[output_refs++ % 2]);if(fault==10)b->ctx->any_faults=true;}':
    'static struct agx_batch *add_batch[64],*launch_batch,*scratch_batch;static struct agx_bo *add_bo_log[64];static unsigned launch_barrier;\n'
    'static void agx_batch_add_bo(struct agx_batch *b,struct agx_bo *bo){assert(bo&&output_refs<64);add_batch[output_refs]=b;add_bo_log[output_refs++]=bo;}',
    ' assert(barrier==(AGX_BARRIER_ALL|AGX_PREGFX)&&entered==1);++launches;':
    ' assert(entered==1);launch_batch=b;launch_barrier=barrier;++launches;',
    'static struct agx_ptr agx_win32_stream_scratch(struct agx_batch *b,unsigned kind,size_t size,struct agx_bo **bo){\n':
    'static struct agx_ptr agx_win32_stream_scratch(struct agx_batch *b,unsigned kind,size_t size,struct agx_bo **bo){\n scratch_batch=b;\n',
}
IE1A_MAIN = r'''/* IE1a harness main, appended to the glue harness prefix. */
static uint8_t ecdm[2][1024];
static struct agx_bo argbo,cntbo,fbbo;
static void reset_counts(void){
 allocations=reads=launches=entered=leaves=writer_flushes=capacity_flushes=output_refs=0;
 get_batches=expand_opens=template_copies=batch_resets=flushes=aborts=fail_expand_flush=0;
 launch_batch=scratch_batch=NULL;launch_barrier=0;
 for(unsigned i=0;i<64;++i){free(blocks[i]);blocks[i]=NULL;}
}
static void render(struct agx_context *ctx,struct agx_batch *r,struct shader *vs){
 init(ctx,r,vs);r->test_active=true;ctx->windows_stream_feedback=&fbbo;
}
static void expansion(struct agx_context *ctx,struct agx_batch *e,unsigned slab){
 *e=(struct agx_batch){.ctx=ctx,.test_active=true};
 e->cdm=(struct agx_encoder){&ecdm[slab],ecdm[slab],ecdm[slab]+sizeof(ecdm[slab])};
}
static unsigned logged(struct agx_batch *b,struct agx_bo *bo){
 unsigned n=0;for(unsigned i=0;i<output_refs;++i)n+=add_batch[i]==b&&add_bo_log[i]==bo;return n;}
static uint64_t fnv(const void *p,size_t n,uint64_t h){
 const uint8_t *b=p;for(size_t i=0;i<n;++i){h^=b[i];h*=0x100000001b3ull;}return h;}
#include <sys/mman.h>
/* Arguments and count at one fixed address in both runs: the generated records
 * embed the argument address, which ASLR would otherwise move. */
static uint32_t *fixed_words;
#define args fixed_words
#define value fixed_words[64]
static uint8_t tmpl[64];
static struct agx_resource arguments,counter;
static struct pipe_draw_indirect_info in;
static uint8_t *draw(struct agx_context *ctx,struct agx_batch *r,uint8_t *out){
 entered=leaves=0;
 struct agx_win32_indirect_stream stream={&in,(uintptr_t)tmpl,sizeof(tmpl),8,12,MESA_PRIM_TRIANGLES};native_vs(&stream);
 struct agx_draw d=agx_draw_indexed_indirect((uintptr_t)args+4,0x1000,256,AGX_INDEX_SIZE_U32,true);
 return agx_win32_emit_indirect_stream(r,&stream,d,AGX_PRIMITIVE_TRIANGLES,out);
}
int main(int argc,char **argv){
 const bool on=argc>1&&!strcmp(argv[1],"on");
 if(on)setenv("APPLE_AGX_D3D12_EXPAND_AHEAD","1",1);else unsetenv("APPLE_AGX_D3D12_EXPAND_AHEAD");
 assert(agx_win32_expand_ahead_enabled()==on);
 /* Read once: a later environment change never flips a live context. */
 setenv("APPLE_AGX_D3D12_EXPAND_AHEAD",on?"0":"1",1);assert(agx_win32_expand_ahead_enabled()==on);
 assert(agx_win32_expand_ahead_parse("1")&&!agx_win32_expand_ahead_parse("0")&&
        !agx_win32_expand_ahead_parse("11")&&!agx_win32_expand_ahead_parse("")&&
        !agx_win32_expand_ahead_parse(NULL));
 void *page=mmap((void *)0x200000000000ull,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 assert(page==(void *)0x200000000000ull);fixed_words=page;value=3;
 for(unsigned i=0;i<sizeof(tmpl);++i)tmpl[i]=(uint8_t)(i*7u+1u);
 arguments=(struct agx_resource){{128},(uintptr_t)args,&argbo};
 counter=(struct agx_resource){{4},(uintptr_t)&value,&cntbo};
 in=(struct pipe_draw_indirect_info){&arguments.base,&counter.base,4,3,32,0};
 for(unsigned i=0;i<3;++i){args[1+8*i]=3;args[2+8*i]=1;args[4+8*i]=i;}
 forced_capacity=16;
 struct agx_context ctx;struct agx_batch r,e,e2;struct shader vs={0};

 /* 1. One counted ExecuteIndirect. Off: prelude in R's CDM, byte-identical
  * to the validated path. On: dispatch into E; R gets no CDM. */
 reset_counts();render(&ctx,&r,&vs);expansion(&ctx,&e,0);expand_next=&e;
 agx_win32_stream_flush_inputs(&ctx,&in);
 assert(!ctx.any_faults&&writer_flushes==2);
 if(on)assert(get_batches==1&&expand_opens==1&&r.windows_expand==&e&&e.windows_expand_owner==&r);
 else assert(!get_batches&&!expand_opens&&!r.windows_expand&&!e.windows_expand_owner);
 uint32_t *out=(void *)(vdm+16);memset(out,0xff,32);
 assert(draw(&ctx,&r,(void *)out)==(uint8_t *)out+8);
 assert(!ctx.any_faults&&ctx.windows_indirect_path==3&&launches==1&&entered==leaves);
 assert(scratch_batch==&r&&reads==2);           /* R owns, reads and retires the outputs */
 assert(logged(&r,&output_bos[0])==1&&logged(&r,&output_bos[1])==1);
 const struct agx_win32_stream_params *p;
 if(on){
  assert(launch_batch==&e&&launch_barrier==AGX_BARRIER_ALL&&!r.cdm.bo&&!r.cdm.current);
  assert(e.windows_expand_dispatches==1&&template_copies==1&&allocations==4);
  p=blocks[4];
  /* E's own pool holds the template copy the kernel reads. */
  assert(p->usc_template==(uintptr_t)blocks[1]&&!memcmp(blocks[1],tmpl,sizeof(tmpl)));
  /* E lists what it reads and writes for residency, never as written. */
  assert(logged(&e,&argbo)==1&&logged(&e,&cntbo)==1&&logged(&e,&fbbo)==1);
  assert(logged(&e,&output_bos[0])==1&&logged(&e,&output_bos[1])==1&&output_refs==7);
 }else{
  assert(launch_batch==&r&&launch_barrier==(AGX_BARRIER_ALL|AGX_PREGFX)&&r.cdm.bo);
  assert(!template_copies&&allocations==3&&output_refs==2&&!e.windows_expand_dispatches);
  p=blocks[3];assert(p->usc_template==(uintptr_t)tmpl);
 }
 { struct agx_win32_stream_params q=*p;q.usc_template=0;
   assert(q.feedback==(uintptr_t)feedback_words);q.feedback=0; /* a host global */
   uint64_t h=fnv(&q,sizeof(q),0xcbf29ce484222325ull);
   h=fnv(out,8,h);
   h=fnv(blocks[on?2:1],value*agx_win32_stream_record_bytes(4)+AGX_VDM_STREAM_LINK_LENGTH,h);
   h=fnv(blocks[on?3:2],value*agx_win32_stream_usc_stride(sizeof(tmpl),true),h);
   printf("EXPANSION %016llx\n",(unsigned long long)h); }
 if(!on){puts("IE1a harness: PASS");return 0;}

 /* 2. agx_flush_batch(R) submits E strictly first; R then has no link. */
 agx_flush_batch(&ctx,&r);
 assert(flushes==2&&flush_log[0]==&e&&flush_log[1]==&r&&!ctx.any_faults&&!r.windows_expand);
 assert(e.test_submitted&&r.test_submitted);

 /* 3. A failed E submission faults the context and aborts R (1); an E the
  * flush left open faults it as well (2). R is never submitted. */
 for(unsigned mode=1;mode<=2;++mode){
  reset_counts();render(&ctx,&r,&vs);expansion(&ctx,&e,0);expand_next=&e;
  agx_win32_stream_flush_inputs(&ctx,&in);assert(draw(&ctx,&r,vdm+32));
  fail_expand_flush=mode;agx_flush_batch(&ctx,&r);
  assert(ctx.any_faults&&!flushes&&!r.test_submitted&&!e.test_submitted&&aborts==(mode==1?2u:1u));
 }

 /* 4. An E without dispatches is reset, never submitted. */
 reset_counts();render(&ctx,&r,&vs);expansion(&ctx,&e,0);expand_next=&e;
 agx_win32_stream_flush_inputs(&ctx,&in);assert(r.windows_expand==&e);
 agx_flush_batch(&ctx,&r);
 assert(batch_resets==1&&!e.test_active&&!e.test_submitted&&flushes==1&&flush_log[0]==&r);

 /* 5. Statistics queries keep the prelude: the expansion writes them. */
 { uint64_t stat=0;reset_counts();render(&ctx,&r,&vs);expansion(&ctx,&e,0);expand_next=&e;
   ctx.pipeline_statistics[PIPE_STAT_QUERY_IA_VERTICES]=&stat;
   assert(!agx_win32_expand_ahead_allowed(&ctx));
   agx_win32_stream_flush_inputs(&ctx,&in);assert(!get_batches&&!expand_opens&&!r.windows_expand);
   assert(draw(&ctx,&r,vdm+32)&&launch_batch==&r&&launch_barrier==(AGX_BARRIER_ALL|AGX_PREGFX));
   /* inactive queries do not force it */
   ctx.active_queries=false;assert(agx_win32_expand_ahead_allowed(&ctx));
   ctx.active_queries=true;ctx.pipeline_statistics[PIPE_STAT_QUERY_IA_VERTICES]=NULL;
   ctx.cond_query=&stat;assert(!agx_win32_expand_ahead_allowed(&ctx));ctx.cond_query=NULL; }

 /* 6. Two ExecuteIndirects share E; a full E is flushed before entry and
  * a new one opened, both ahead of R. */
 reset_counts();render(&ctx,&r,&vs);expansion(&ctx,&e,0);expand_next=&e;
 agx_win32_stream_flush_inputs(&ctx,&in);assert(draw(&ctx,&r,vdm+32));
 agx_win32_stream_flush_inputs(&ctx,&in);assert(expand_opens==1&&draw(&ctx,&r,vdm+40));
 assert(e.windows_expand_dispatches==2&&launches==2&&!r.cdm.bo);
 e.cdm.current=e.cdm.end-2u*agx_win32_stream_cdm_bytes();expansion(&ctx,&e2,1);expand_next=&e2;
 agx_win32_stream_flush_inputs(&ctx,&in);
 assert(capacity_flushes==1&&flushes==1&&flush_log[0]==&e&&expand_opens==2&&r.windows_expand==&e2);
 assert(draw(&ctx,&r,vdm+48)&&launch_batch==&e2&&e2.windows_expand_dispatches==1);
 agx_flush_batch(&ctx,&r);assert(flushes==3&&flush_log[1]==&e2&&flush_log[2]==&r);

 /* 7. Without room at the draw (or without E) the prelude stays in R. */
 reset_counts();render(&ctx,&r,&vs);expansion(&ctx,&e,0);expand_next=&e;
 agx_win32_stream_flush_inputs(&ctx,&in);e.cdm.current=e.cdm.end-8;
 assert(draw(&ctx,&r,vdm+32)&&launch_batch==&r&&r.cdm.bo&&!e.windows_expand_dispatches);
 reset_counts();render(&ctx,&r,&vs);expand_next=NULL;
 agx_win32_stream_flush_inputs(&ctx,&in);assert(expand_opens==1&&!r.windows_expand);
 assert(draw(&ctx,&r,vdm+32)&&launch_batch==&r&&!ctx.any_faults);

 /* 8. A stale link (E submitted, or its slot reused) is never appended to. */
 reset_counts();render(&ctx,&r,&vs);expansion(&ctx,&e,0);r.windows_expand=&e;e.windows_expand_owner=&e2;
 assert(!agx_win32_expand_linked(&r)&&!r.windows_expand);
 r.windows_expand=&e;e.windows_expand_owner=&r;e.test_active=false;e.test_submitted=true;
 assert(!agx_win32_expand_linked(&r)&&!r.windows_expand);

 /* 9. A render reset with an open E aborts it (E must not write outputs R
  * gave back); E's own cleanup unlinks R. */
 reset_counts();render(&ctx,&r,&vs);expansion(&ctx,&e,0);expand_next=&e;
 agx_win32_stream_flush_inputs(&ctx,&in);assert(draw(&ctx,&r,vdm+32));
 agx_win32_expand_cleanup(&ctx,&r);
 assert(batch_resets==1&&!e.test_active&&!e.test_submitted&&!r.windows_expand&&!e.windows_expand_owner);
 reset_counts();render(&ctx,&r,&vs);expansion(&ctx,&e,0);r.windows_expand=&e;e.windows_expand_owner=&r;
 agx_win32_expand_cleanup(&ctx,&e);assert(!r.windows_expand&&!e.windows_expand_owner&&!batch_resets);

 /* 10. A failure in E's template copy faults the context; nothing launches. */
 reset_counts();render(&ctx,&r,&vs);expansion(&ctx,&e,0);expand_next=&e;
 agx_win32_stream_flush_inputs(&ctx,&in);fault=1; /* first allocation: the template copy */
 assert(!draw(&ctx,&r,vdm+32)&&ctx.any_faults&&!launches&&!entered);fault=0;
 reset_counts();
 puts("IE1a harness: PASS");
 return 0;
}
'''


class ExpandAhead(unittest.TestCase):
    def test_expand_ahead_harness(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        IE1a must leave the default (switch off) prelude unchanged, and with
        APPLE_AGX_D3D12_EXPAND_AHEAD=1 dispatch into the expansion batch E
        (never R's CDM) with the same parameters and GPU-generated stream,
        submit E strictly before R, abort R when E fails, never submit an
        empty E, keep the outputs on R, list E's residency, keep the prelude
        for statistics queries, and abort an open E with its render.
        """
        import test_d3d12_indirect_stream_glue as glue
        mesa=local_tree('reference','mesa');build=local_tree('accelerated-desktop-ad03','mesa-build')
        if not mesa or not build:
            self.skipTest('reference Mesa tree not available')
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)
            out=project(p/'native')
            state=(out/'src/gallium/drivers/asahi/agx_state.h').read_text()
            shader=state[state.index('struct agx_uncompiled_shader {'):]
            info=shader[shader.index('   struct {'):shader.index('   } info;')+10]
            prefix=glue.GLUE[:glue.GLUE.index('int main(void){')].replace(
                'struct shader {struct {unsigned system_values_read[16];} info;};',
                'struct shader {\n'+info+'\n};')
            for old,new in IE1A_STUBS.items():
                self.assertEqual(prefix.count(old),1,old)
                prefix=prefix.replace(old,new)
            (p/'test.c').write_text(glue.PRELUDE+prefix+IE1A_MAIN)
            cmd=['clang','-std=c11','-Werror','-Wno-unused-function','-Wno-c23-extensions',
                 '-fsanitize=address,undefined','-DNDEBUG','-Dstatic_assert=_Static_assert',
                 '-DHAVE___BUILTIN_CLZ','-DHAVE_STRUCT_TIMESPEC','-DHAVE_PTHREAD']
            cmd+=['-I'+str(x) for x in (WINSYS,mesa/'src',mesa/'include',build/'src',
                                      mesa/'src/asahi/libagx',build/'src/asahi/genxml')]
            r=subprocess.run(cmd+[str(p/'test.c'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr[-6000:])
            runs={}
            for mode in ('off','on'):
                r=subprocess.run([str(p/'test'),mode],capture_output=True,text=True)
                self.assertEqual(r.returncode,0,mode+': '+r.stdout+r.stderr[-3000:])
                self.assertIn('IE1a harness: PASS',r.stdout)
                runs[mode]=[l for l in r.stdout.splitlines() if l.startswith('EXPANSION ')]
            # Same parameters (but the template copy's address), VDM link,
            # generated records and USC clones with and without E.
            self.assertEqual(len(runs['off']),1)
            self.assertEqual(runs['off'],runs['on'])

    def test_expand_ahead_projection(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        E must be flushed before R is encoded or aborted, links must start
        empty and be dropped on cleanup, and E must never be ctx->batch.
        """
        with tempfile.TemporaryDirectory() as td:
            out=project(Path(td)/'out')
            asahi=out/'src/gallium/drivers/asahi'
            header=(asahi/'agx_state.h').read_text()
            batch_struct=header[header.index('struct agx_batch {'):].split('};',1)[0]
            self.assertIn('struct agx_batch *windows_expand, *windows_expand_owner;',batch_struct)
            self.assertIn('unsigned windows_expand_dispatches;',batch_struct)
            self.assertLess(header.index('#include "agx_win32_stream_scratch.h"'),
                            header.index('#include "agx_win32_expand_ahead.h"'))
            batch=(asahi/'agx_batch.c').read_text()
            init=function(batch,'static void\nagx_batch_init(')
            for field in ('windows_expand = NULL;','windows_expand_owner = NULL;',
                          'windows_expand_dispatches = 0;'):
                self.assertIn('batch->'+field,init)
            cleanup=function(batch,'static void\nagx_batch_cleanup(')
            self.assertLess(cleanup.index('agx_win32_expand_cleanup(ctx, batch);'),
                            cleanup.index('agx_win32_stream_scratch_retire(ctx, &batch->windows_stream_pool, 0)'))
            opener=function(batch,'struct agx_batch *\nagx_win32_expand_open(')
            self.assertIn('agx_get_batch_for_framebuffer(ctx, &key)',opener)
            self.assertIn('.width = AGX_COMPUTE_BATCH_WIDTH',opener)
            self.assertNotIn('ctx->batch',opener)
            pipe=(asahi/'agx_pipe.c').read_text()
            start=pipe.index('\nagx_flush_batch(struct agx_context *ctx, struct agx_batch *batch)')
            body=pipe[start:pipe.index('\n}\n',start)]
            order=[body.index(s) for s in (
                'agx_win32_expand_flush_first(ctx, batch)',
                'agx_win32_submit_order_writers(',
                'if (!batch->draws && !batch->cdm.bo &&',
                'if (ctx->any_faults || !batch->vdm.bo || !batch->initialized ||',
                'agx_flush_compute(ctx, batch, &compute_storage);',
                'agx_batch_submit(ctx, batch, compute, &render);')]
            self.assertEqual(order,sorted(order))


if __name__=='__main__': unittest.main()
