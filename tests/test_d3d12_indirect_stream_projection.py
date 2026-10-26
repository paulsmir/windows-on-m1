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
            usc=(out/'src/gallium/drivers/asahi/agx_win32_pipeline.inc').read_text()
            self.assertIn('params_offset = b.head - (uint8_t *)t.cpu;',usc)
            self.assertIn('usc_bytes = b.head - (uint8_t *)t.cpu;',usc)
            library=(out/'src/asahi/lib/libagx_shaders.h').read_text()
            self.assertIn('LIBAGX_WIN32_INDIRECT_STREAM,',library)
            self.assertIn('#define libagx_win32_indirect_stream(',library)
            batch=(out/'src/gallium/drivers/asahi/agx_batch.c').read_text()
            self.assertIn('AGX_WIN32_BO_GPU_ONLY | AGX_BO_LOW_VA, false)',batch)
            self.assertIn('agx_pool_cleanup(&batch->windows_stream_pool)',batch)
            self.assertIn('agx_pool_cleanup(&batch->windows_stream_usc_pool)',batch)
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
struct context {bool windows_indirect_stream,active_queries,in_tess,active_draw_without_restart;unsigned pipeline_statistics[PIPE_STAT_QUERY_COUNT];struct {void *shader;} stage[MESA_SHADER_STAGES];};
static bool native(struct context *ctx){
'''+guard+'''return true;}return false;}
int main(void){struct context c={0};c.pipeline_statistics[PIPE_STAT_QUERY_IA_VERTICES]=1;
for(unsigned stream=0;stream<2;++stream)for(unsigned active=0;active<2;++active){c.windows_indirect_stream=stream;c.active_queries=active;assert(native(&c)==(!stream&&active));}}
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
 for(unsigned gpu=0;gpu<2;++gpu){
  struct agx_pool p={0};p.create_flags=gpu?AGX_WIN32_BO_GPU_ONLY:0;maps=backs=0;
  struct agx_bo *owned=NULL;
  struct agx_ptr a=agx_pool_alloc_aligned_with_bo(&p,28*12000+4,64,&owned);
  assert(owned==&bo&&a.gpu==va.addr&&backs==1&&maps==!gpu);
  assert(gpu?!a.cpu:a.cpu==(void *)(uintptr_t)0x10000);
  assert(p.transient_offset==28*12000+4);
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


if __name__=='__main__': unittest.main()
