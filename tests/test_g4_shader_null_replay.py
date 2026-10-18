"""Replay the pinned executable BO copy through the real empty-shader frontend.

The source snippets come from the pinned Mesa inputs after the build-local
Windows projection. BO allocation and mapping are the only injected failures.
"""

import ast
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT.parents[1] / ".local/reference/mesa/src/gallium"
SCRIPT = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


def project_sources():
    state = (REFERENCE / "drivers/asahi/agx_state.c").read_text()
    shader = (REFERENCE / "frontends/d3d10umd/Shader.cpp").read_text()
    device = (REFERENCE / "frontends/d3d10umd/Device.cpp").read_text()
    tree = ast.parse(SCRIPT.read_text())
    projection = next((node for node in tree.body
                       if isinstance(node, ast.FunctionDef) and
                       node.name == "project_shader_failure"), None)
    if projection is None:
        return state, shader, device
    namespace = {}
    exec(compile(ast.Module(body=[projection], type_ignores=[]), str(SCRIPT), "exec"), namespace)
    return namespace["project_shader_failure"](state, shader, device)


def function(text, name):
    marker = "\n" + name + "("
    start = text.rindex(marker) + 1
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


SHIM = r"""
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
using uint = unsigned;
enum mesa_shader_stage { MESA_SHADER_VERTEX, MESA_SHADER_FRAGMENT, MESA_SHADER_GEOMETRY };
enum { PIPE_SHADER_IR_NIR = 1, AGX_BO_EXEC = 1, AGX_BO_LOW_VA = 2 };
struct tgsi_token { int value; };
struct ureg_program { int value; };
struct pipe_shader_state { const tgsi_token *tokens; };
struct agx_device {};
struct agx_bo { char *mapping; };
struct agx_compiled_shader {
   struct { struct { size_t binary_size; } info; unsigned char *binary; } b;
   agx_bo *bo;
};
static int mode, bo_allocs, bo_frees, binary_frees;
static void *current_binary;
static void tracked_free(void *ptr) {
   if (ptr == current_binary) binary_frees++;
   free(ptr);
}
static agx_device device;
static agx_bo *agx_bo_create(agx_device *, size_t size, int, int, const char *) {
   if (mode == 1) return nullptr;
   auto *bo = static_cast<agx_bo *>(calloc(1, sizeof(agx_bo)));
   bo->mapping = mode == 2 ? nullptr : static_cast<char *>(malloc(size));
   bo_allocs++;
   return bo;
}
static void *agx_bo_map(agx_bo *bo) { return bo->mapping; }
static void agx_bo_unreference(agx_device *, agx_bo *bo) {
   if (bo) { free(bo->mapping); free(bo); bo_frees++; }
}
static void release_binary(agx_compiled_shader *compiled) {
   tracked_free(compiled->b.binary);
}
static agx_compiled_shader *compile_executable(bool secondary) {
   agx_device *dev = &device;
   auto *compiled = static_cast<agx_compiled_shader *>(calloc(1, sizeof(agx_compiled_shader)));
   compiled->b.info.binary_size = 28;
   compiled->b.binary = static_cast<unsigned char *>(malloc(28));
   current_binary = compiled->b.binary;
   memset(compiled->b.binary, 0x5a, 28);
#define free tracked_free
#define FREE tracked_free
#include "shader_bo_copy.inc"
#undef FREE
#undef free
}
struct pipe_context {
   void *(*create_fs_state)(pipe_context *, const pipe_shader_state *);
   void *(*create_vs_state)(pipe_context *, const pipe_shader_state *);
   void *(*create_gs_state)(pipe_context *, const pipe_shader_state *);
};
struct Device { pipe_context *pipe; };
static ureg_program *ureg_create(mesa_shader_stage) {
   return static_cast<ureg_program *>(calloc(1, sizeof(ureg_program)));
}
static void ureg_END(ureg_program *) {}
static tgsi_token *ureg_get_tokens(ureg_program *, uint *count) {
   *count = 1; return static_cast<tgsi_token *>(malloc(sizeof(tgsi_token)));
}
static void ureg_destroy(ureg_program *ureg) { free(ureg); }
static void ureg_free_tokens(const tgsi_token *tokens) { free((void *)tokens); }
static void *create_shader(pipe_context *, const pipe_shader_state *) {
   return compile_executable(false);
}
#include "create_empty_shader.inc"
int main() {
   pipe_context pipe{create_shader, create_shader, create_shader};
   Device frontend{&pipe};
   for (mode = 1; mode <= 2; mode++) {
      auto *handle = CreateEmptyShader(&frontend, MESA_SHADER_VERTEX);
      if (handle) return 10 + mode;
   }
   mode = 0;
   auto *handle = static_cast<agx_compiled_shader *>(
      CreateEmptyShader(&frontend, MESA_SHADER_VERTEX));
   if (!handle || !handle->bo || !handle->bo->mapping) return 20;
   if (handle->bo->mapping[0] != 0x5a) return 21;
   agx_bo_unreference(&device, handle->bo);
   release_binary(handle);
   free(handle);
   return bo_allocs == bo_frees && binary_frees == 3 ? 0 : 22;
}
"""


class ShaderNullReplay(unittest.TestCase):
    def test_compute_variant_failure_does_not_publish_an_empty_state(self):
        state, _, _ = project_sources()
        shim = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct { int value; } nir_shader;
struct agx_device { int value; };
struct pipe_context { void *screen; };
struct agx_context { int support_lod_bias, robust, any_faults; };
struct pipe_compute_state { int ir_type; void *prog; };
struct agx_uncompiled_shader { void *variants; };
struct agx_compiled_shader { struct { struct { unsigned nr_gprs; } info; } b; };
union asahi_shader_key { int value; };
struct pipe_compute_state_object_info { unsigned max_threads,private_memory,preferred_simd_size,simd_sizes; };
struct occupancy { unsigned max_threads; };
static struct agx_context ctx;
static struct agx_device dev;
static struct agx_compiled_shader compiled;
static int failure=1, deleted, nir_freed;
#define PIPE_SHADER_IR_NIR 1
#define rzalloc(parent,type) ((type *)calloc(1,sizeof(type)))
#define asahi_cs_shader_key_hash 0
#define asahi_cs_shader_key_equal 0
static struct agx_context *agx_context(struct pipe_context *p){(void)p;return &ctx;}
static struct agx_device *agx_device(void *p){(void)p;return &dev;}
static void *agx_screen(void *p){return p;}
static void *_mesa_hash_table_create(void *p,int h,int e){(void)h;(void)e;return p;}
static void agx_shader_initialize(struct agx_device *d,struct agx_uncompiled_shader *s,nir_shader *n,int l,int r){(void)d;(void)s;(void)n;(void)l;(void)r;}
static struct agx_compiled_shader *agx_get_shader_variant(void *s,struct pipe_context *p,void *c,union asahi_shader_key *k){(void)s;(void)p;(void)c;(void)k;return failure?NULL:&compiled;}
static void agx_delete_uncompiled_shader(struct agx_device *d,struct agx_uncompiled_shader *s){(void)d;++deleted;free(s);}
static void ralloc_free(void *n){++nir_freed;free(n);}
static struct occupancy agx_occupancy_for_register_count(unsigned n){(void)n;return (struct occupancy){256};}
static void agx_win32_shader_failed(struct agx_context *c){c->any_faults=1;}
'''
        source = shim + 'static void *\n' + function(state, 'agx_create_compute_state')
        source += '\nstatic void\n' + function(state, 'agx_get_compute_state_info')
        source += r'''
int main(void){
  struct pipe_context p={0};struct pipe_compute_state c={1,malloc(sizeof(nir_shader))};
  assert(!agx_create_compute_state(&p,&c));assert(deleted==1 && nir_freed==1);
  struct pipe_compute_state_object_info info={99,99,99,99};
  agx_get_compute_state_info(&p,NULL,&info);
  assert(!info.max_threads && !info.private_memory && !info.preferred_simd_size && !info.simd_sizes && ctx.any_faults);
  failure=0;c.prog=malloc(sizeof(nir_shader));
  void *s=agx_create_compute_state(&p,&c);assert(s && nir_freed==2);
  agx_get_compute_state_info(&p,s,&info);assert(info.max_threads==256 && info.simd_sizes==32);
  agx_delete_uncompiled_shader(&dev,s);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp / 'compute.c').write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11',
                '-Wno-unused-function', str(tmp / 'compute.c'), '-o', str(tmp / 'compute')], check=True)
            subprocess.run([str(tmp / 'compute')], check=True)

    def test_executable_bo_failure_reaches_empty_shader(self):
        state, shader, _ = project_sources()
        start = state.index("   if (compiled->b.info.binary_size && !secondary) {",
                            state.index("agx_compile_nir("))
        end = state.index("   return compiled;", start) + len("   return compiled;")
        copy = state[start:end]
        empty = function(shader, "CreateEmptyShader")
        with tempfile.TemporaryDirectory(prefix="g4-shader-null-") as directory:
            tmp = Path(directory)
            (tmp / "shader_bo_copy.inc").write_text(copy)
            (tmp / "create_empty_shader.inc").write_text("void *\n" + empty)
            (tmp / "replay.cpp").write_text(SHIM)
            binary = tmp / "replay"
            subprocess.run([os.environ.get("CC", "clang"), "-x", "c++", "-std=c++17",
                            "-Wall", "-Wextra", "-Werror", "-I", str(tmp),
                            str(tmp / "replay.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_failure_propagation_to_device_creation(self):
        state, shader, device = project_sources()
        variant = function(state, "agx_compile_variant")
        self.assertIn("if (!compiled)", variant)
        for auxiliary in ("gs_count", "pre_gs", "gs_copy"):
            self.assertIn(f"if (!compiled->{auxiliary})", variant)
        self.assertIn("agx_delete_compiled_shader(dev, compiled)", variant)
        cached = function(state, "agx_get_shader_variant")
        self.assertIn("if (!compiled)", cached)
        self.assertLess(cached.index("if (!compiled)"), cached.index("agx_disk_cache_store"))
        owner = function(state, "agx_create_shader_state")
        self.assertIn("if (!agx_get_shader_variant", owner)
        self.assertIn("agx_delete_uncompiled_shader(dev, so)", owner)
        self.assertNotIn("assert(handle)", function(shader, "CreateEmptyShader"))
        create = function(device, "CreateDevice")
        self.assertIn("if (!pDevice->empty_vs)", create)
        self.assertIn("if (!pDevice->empty_fs)", create)
        self.assertEqual(create.count("AgxD3d10WindowsDestroyDeviceDdi("), 2)
        self.assertIn("DeleteEmptyShader(pDevice, MESA_SHADER_VERTEX, pDevice->empty_vs)", create)

    def test_later_variant_failure_stops_draw_before_shader_use(self):
        state, _, _ = project_sources()
        self.assertIn("if (!*out)", function(state, "agx_update_shader"))
        self.assertIn("agx_win32_shader_failed(ctx)", function(state, "agx_update_shader"))
        self.assertIn("if (!ctx->vs)", function(state, "agx_update_vs"))
        self.assertIn("if (!ctx->fs)", function(state, "agx_update_fs"))
        self.assertIn("if (!shader)", function(state, "agx_build_meta_shader_internal"))
        self.assertIn("(!prolog || !epilog)", function(state, "asahi_fast_link"))
        draw = function(state, "agx_draw_vbo")
        self.assertLess(draw.index("agx_update_vs(batch"), draw.index("if (ctx->any_faults)"))
        self.assertLess(draw.index("agx_update_fs(batch"),
                        draw.index("if (ctx->any_faults)", draw.index("agx_update_fs(batch")))


if __name__ == "__main__":
    unittest.main()
