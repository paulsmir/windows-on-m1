"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP1987 RC63G failed on hardware with `bad=4095 first=1 got=1,1,64,255`:
every record drew its own quad (BaseVertexLocation, applied by the VDM from
the record) but sampled texel 0, the texel of StartInstanceLocation 0, which
is record 0's value. Record 0's draw parameters are what the CPU template USC
reads, so the hardware ran every GPU record with the template's base
instance. The pre-RC69 stream relied on two things no hardware-validated path
uses: a VDM state block carrying only vertex-shader word 1 (every Mesa encoder
emits word 0, word 1, outputs and unknown together) and a USC uniform load of
the draw parameters straight from GPU-copied argument memory (RC44I validated
only CPU-written arguments).

This executes the production stream kernel on the RC63G probe's own records,
texture coordinates, quads and verdict, then decodes the generated VDM/USC
words with Mesa's unpackers and renders them under three hardware models:
permissive (both forms work), strict-bind (a state block binds a new vertex
shader only in the complete native form) and stale-arguments (a uniform load
from the GPU-copied argument buffer returns its pre-copy zero). The pre-RC69
kernel reproduces the exact hardware readback in the last two models; the
stream must render the correct image under all three.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d10_shader_fields_init import ROOT, local_tree
from test_d3d12_indirect_stream import PROGRAM, WINSYS

PROBE = ROOT / 'drivers/apple-agx/windows/icd/agx_d3d12_probe.c'

MODEL = r'''
#include <math.h>
#include <sys/mman.h>
typedef unsigned UINT;
typedef uint64_t UINT64;
typedef struct {UINT64 IAVertices,IAPrimitives,VSInvocations,GSInvocations,GSPrimitives,
 CInvocations,CPrimitives,PSInvocations,HSInvocations,DSInvocations,CSInvocations;}
 D3D12_QUERY_DATA_PIPELINE_STATISTICS;
@HELPERS@
enum {PERMISSIVE, STRICT_BIND, STALE_ARGUMENTS};
/* One low arena stands in for the 40-bit GPU VA space, so every address the
 * kernel writes into VDM/USC words is decodable and dereferenceable. */
static uint8_t *arena;
static size_t arena_used;
static void *gpu_alloc(size_t bytes, size_t align) {
  arena_used = (arena_used + align - 1) & ~(align - 1);
  void *p = arena + arena_used;
  arena_used += bytes;
  assert(arena_used <= (64u << 20));
  return p;
}
static uint8_t *args_lo, *args_hi;
static uint64_t shader_base;
static float positions[RC63_PIXELS * 8];
static unsigned char instances[RC63_MAX * 4];
static unsigned char rt[64 * 256];
static float depth[RC63_PIXELS];
static unsigned params_uniform;

/* The VS prolog reads base instance from uniform halfs [u + 2, u + 4): the
 * second word of the four-halfword PARAMS load (agx_abi.h). */
static uint32_t bound_base_instance(const uint8_t *usc, unsigned bytes, int mode) {
  for (unsigned at = 0; at + 8 <= bytes; at += 8) {
    struct AGX_USC_UNIFORM u;
    assert(usc[at] == 0x1d && AGX_USC_UNIFORM_unpack(stderr, usc + at, &u));
    if (u.start_halfs != params_uniform) continue;
    assert(u.size_halfs == 4);
    const uint8_t *data = (const uint8_t *)(uintptr_t)u.buffer;
    if (mode == STALE_ARGUMENTS && data >= args_lo && data < args_hi) return 0;
    uint32_t v;
    memcpy(&v, data + 4, 4);
    return v;
  }
  assert(!"bound USC has no PARAMS load");
  return 0;
}
static void draw(UINT vertex, UINT instance) {
  const float *v = positions + vertex * 2;
  float x0 = v[0], x1 = v[0], y0 = v[1], y1 = v[1];
  for (int i = 1; i < 4; ++i) {
    x0 = fminf(x0, v[2 * i]); x1 = fmaxf(x1, v[2 * i]);
    y0 = fminf(y0, v[2 * i + 1]); y1 = fmaxf(y1, v[2 * i + 1]);
  }
  const unsigned char *uv = instances + instance * 4u;
  int tu = (int)floorf((float)uv[0] / 255.0f * 64.0f), tv = (int)floorf((float)uv[1] / 255.0f * 64.0f);
  tu = tu < 0 ? 0 : tu > 63 ? 63 : tu; tv = tv < 0 ? 0 : tv > 63 ? 63 : tv;
  /* Only pixel centres inside the quad's bounds can be covered. */
  int px0 = (int)floorf((x0 + 1.0f) * 32.0f - 0.5f), px1 = (int)ceilf((x1 + 1.0f) * 32.0f - 0.5f);
  int py0 = (int)floorf((1.0f - y1) * 32.0f - 0.5f), py1 = (int)ceilf((1.0f - y0) * 32.0f - 0.5f);
  px0 = px0 < 0 ? 0 : px0; py0 = py0 < 0 ? 0 : py0; px1 = px1 > 63 ? 63 : px1; py1 = py1 > 63 ? 63 : py1;
  for (int y = py0; y <= py1; ++y) for (int x = px0; x <= px1; ++x) {
    const UINT p = (UINT)y * 64u + (UINT)x;
    const float cx = ((float)x + 0.5f) / 32.0f - 1.0f, cy = 1.0f - ((float)y + 0.5f) / 32.0f;
    if (cx < x0 || cx > x1 || cy < y0 || cy > y1 || !(0.0f < depth[p])) continue;
    depth[p] = 0.0f;
    rc63_texel((UINT)tv * 64u + (UINT)tu, rt + (p / 64u) * 256u + (p % 64u) * 4u);
  }
}
/* Walk the generated stream from its first record to the continuation link. */
static unsigned render(const uint32_t *stream, uint64_t continuation, const uint8_t *tmpl,
                       unsigned usc_bytes, int mode, UINT *first) {
  for (UINT p = 0; p < RC63_PIXELS; ++p) {
    unsigned char *px = rt + (p / 64u) * 256u + (p % 64u) * 4u;
    px[0] = 255; px[1] = px[2] = 0; px[3] = 255; depth[p] = 1.0f;
  }
  const uint8_t *bound = tmpl; /* bound by the CPU's native VS block before the link */
  unsigned draws = 0;
  const uint8_t *at = (const uint8_t *)stream;
  for (;;) {
    uint32_t word;
    memcpy(&word, at, 4);
    const unsigned type = word >> 29;
    if (type == AGX_VDM_BLOCK_TYPE_VDM_STATE_UPDATE) {
      struct AGX_VDM_STATE h;
      assert(AGX_VDM_STATE_unpack(stderr, at, &h));
      assert(!h.tessellation_present && !h.tessellation_scale_present);
      unsigned len = 4;
      if (h.restart_index_present) len += 4;
      if (h.vertex_shader_word_0_present) len += 4;
      const uint8_t *pipeline = NULL;
      if (h.vertex_shader_word_1_present) {
        struct AGX_VDM_STATE_VERTEX_SHADER_WORD_1 w1;
        AGX_VDM_STATE_VERTEX_SHADER_WORD_1_unpack(stderr, at + len, &w1);
        pipeline = (const uint8_t *)(uintptr_t)(shader_base + w1.pipeline);
        len += 4;
      }
      if (h.vertex_outputs_present) len += 4;
      if (h.vertex_unknown_present) len += 4;
      const bool native = h.vertex_shader_word_0_present && h.vertex_shader_word_1_present &&
                          h.vertex_outputs_present && h.vertex_unknown_present;
      if (pipeline && (mode != STRICT_BIND || native)) bound = pipeline;
      at += (len + 7u) & ~7u;
    } else if (type == AGX_VDM_BLOCK_TYPE_INDEX_LIST) {
      struct AGX_INDEX_LIST h;
      struct AGX_INDEX_LIST_INDIRECT_BUFFER ib;
      assert(AGX_INDEX_LIST_unpack(stderr, at, &h));
      assert(h.index_buffer_present && h.indirect_buffer_present && h.index_buffer_size_present &&
             !h.index_count_present && !h.instance_count_present && !h.start_present);
      AGX_INDEX_LIST_INDIRECT_BUFFER_unpack(stderr, at + 8, &ib);
      const uint32_t *record = (const uint32_t *)(uintptr_t)(((uint64_t)ib.address_hi << 32) | ib.address_lo);
      /* The VDM reads count/instances/start/base vertex from the record. */
      assert(record[0] == 6 && record[1] == 1 && record[2] == 0);
      draw(record[3], bound_base_instance(bound, usc_bytes, mode));
      ++draws;
      at += 20;
    } else {
      struct AGX_VDM_STREAM_LINK link;
      assert(type == AGX_VDM_BLOCK_TYPE_STREAM_LINK && AGX_VDM_STREAM_LINK_unpack(stderr, at, &link));
      assert((((uint64_t)link.target_hi << 32) | link.target_lo) == continuation && !link.with_return);
      break;
    }
  }
  assert(draws == RC63_MAX);
  return rc63_bad_pixels(rt, first);
}
int main(void) {
  arena = mmap((void *)(uintptr_t)0x3000000000ull, 64u << 20, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANON, -1, 0);
  assert(arena != MAP_FAILED && (uintptr_t)arena + (64u << 20) < (1ull << 39));
  shader_base = (uintptr_t)arena;
  for (UINT q = 0; q < RC63_PIXELS; ++q) rc63_quad(q, positions + q * 8u);
  for (UINT r = 0; r < RC63_MAX; ++r) rc63_uv(r, instances + r * 4u);
  /* DEFAULT argument buffer, as the probe's GPU copy leaves it. */
  uint32_t *args = gpu_alloc(RC63_MAX * 20u, 256);
  for (UINT r = 0; r < RC63_MAX; ++r) rc63_record(r, args + 5u * r);
  args_lo = (uint8_t *)args; args_hi = args_lo + RC63_MAX * 20u;
  uint32_t *count = gpu_alloc(4, 4);
  *count = RC63_MAX;
  /* Native template: VBO base, VBO clamp, then the PARAMS load which
   * agx_upload_draw_params points at record 0 (offset 12, indexed). */
  params_uniform = 12;
  uint8_t *tmpl = gpu_alloc(64, 64);
  struct agx_usc_builder b = agx_usc_builder(tmpl, 64);
  agx_usc_uniform(&b, 0, 8, 0x3000001000ull);
  agx_usc_uniform(&b, 8, 4, 0x3000002000ull);
  const unsigned params_offset = (unsigned)(b.head - tmpl);
  agx_usc_uniform(&b, params_uniform, 4, (uintptr_t)args + 12);
  agx_usc_uniform(&b, 16, 4, 0x3000003000ull);
  const unsigned usc_bytes = (unsigned)(b.head - tmpl);
  /* The CPU VS block agx_encode_state emits for the template. */
  uint32_t vs_state[6] = {0}, *o = vs_state;
  agx_push(o, VDM_STATE, cfg) {
    cfg.vertex_shader_word_0_present = true; cfg.vertex_shader_word_1_present = true;
    cfg.vertex_outputs_present = true; cfg.vertex_unknown_present = true;
  }
  agx_push(o, VDM_STATE_VERTEX_SHADER_WORD_0, cfg) { cfg.uniform_register_count = 64; }
  agx_push(o, VDM_STATE_VERTEX_SHADER_WORD_1, cfg) { cfg.pipeline = (uintptr_t)tmpl - shader_base; }
  agx_push(o, VDM_STATE_VERTEX_OUTPUTS, cfg) { cfg.output_count_1 = 8; cfg.output_count_2 = 8; }
  agx_push(o, VDM_STATE_VERTEX_UNKNOWN, cfg) { cfg.flat_shading_control = AGX_VDM_VERTEX_2; }
#ifdef AGX_WIN32_STREAM_PARAMS_BYTES
  const unsigned usc_stride = agx_win32_stream_usc_stride(usc_bytes, true);
#else
  const unsigned usc_stride = (usc_bytes + 63u) & ~63u;
#endif
  const unsigned slot = agx_win32_stream_record_bytes(4);
  uint32_t *stream = gpu_alloc((size_t)RC63_MAX * slot + 8 + 0x800, 256);
  uint8_t *usc = gpu_alloc((size_t)RC63_MAX * usc_stride, 64);
  uint32_t *indices = gpu_alloc(24, 64);
  const uint64_t continuation = 0x3000004000ull;
  struct agx_win32_stream_params p = {
    .stream = (uintptr_t)stream, .pipelines = (uintptr_t)usc, .continuation = continuation,
    .usc_template = (uintptr_t)tmpl, .arguments = (uintptr_t)args, .count = (uintptr_t)count,
    .index_buffer = (uintptr_t)indices, .max_count = RC63_MAX, .capacity = RC63_MAX, .argument_stride = 20,
    .usc_bytes = usc_bytes, .usc_stride = usc_stride, .params_offset = params_offset,
    .params_uniform = params_uniform, .pipeline_base = (uint32_t)((uintptr_t)usc - shader_base),
    .index_range = 24, .index_size = 4, .restart = 0, .topology = AGX_PRIMITIVE_TRIANGLES,
    .mode = MESA_PRIM_TRIANGLES,
  };
#ifdef AGX_WIN32_STREAM_VS_STATE_WORDS
  memcpy(p.vs_state, vs_state, sizeof(vs_state));
#endif
#ifdef AGX_WIN32_STREAM_PARAMS_BYTES
  p.params_data = agx_win32_stream_params_data(usc_bytes);
#endif
  for (unsigned i = RC63_MAX + 1; i-- > 0;) { cl_global_id.x = i; libagx_win32_indirect_stream(&p); }
  int failed = 0;
  static const char *names[] = {"permissive", "strict-bind", "stale-arguments"};
  for (int mode = PERMISSIVE; mode <= STALE_ARGUMENTS; ++mode) {
    UINT first;
    const unsigned bad = render(stream, continuation, tmpl, usc_bytes, mode, &first);
    const unsigned char *px = rt + (first % RC63_PIXELS / 64u) * 256u + (first % RC63_PIXELS % 64u) * 4u;
    printf("%s readback bad=%u first=%u got=%u,%u,%u,%u\n", names[mode], bad, first, px[0], px[1], px[2], px[3]);
    failed |= bad != 0;
  }
  return failed;
}
'''


class Rc69StreamDrawParams(unittest.TestCase):
    def test_rc63g_records_render_under_every_binding_model(self):
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        source = PROBE.read_text()
        begin = source.index('#define RC63_MAX')
        helpers = source[begin:source.index('static int rc63_gravitymark_stream(', begin)]
        prelude = PROGRAM[:PROGRAM.index('static void run(')].replace(
            '#include "agx_win32_indirect_stream.cl"',
            '#include "agx_win32_indirect_stream.cl"\n#undef NDEBUG\n#include <assert.h>')
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p / 'rc69.c').write_text(prelude + MODEL.replace('@HELPERS@', helpers))
            cmd = ['clang', '-std=c11', '-O1', '-Werror', '-Wno-unused-function', '-Wno-c23-extensions',
                   '-fsanitize=address,undefined', '-DNDEBUG', '-Dstatic_assert=_Static_assert',
                   '-DHAVE___BUILTIN_CLZ', '-DHAVE_STRUCT_TIMESPEC', '-DHAVE_PTHREAD']
            cmd += ['-I' + str(x) for x in (WINSYS, mesa / 'src', mesa / 'include', build / 'src',
                                          mesa / 'src/asahi/libagx', build / 'src/asahi/genxml')]
            r = subprocess.run(cmd + [str(p / 'rc69.c'), '-lm', '-o', str(p / 'rc69')],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            r = subprocess.run([str(p / 'rc69')], capture_output=True, text=True, timeout=300)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            for model in ('permissive', 'strict-bind', 'stale-arguments'):
                self.assertIn(model + ' readback bad=0 ', r.stdout)


if __name__ == '__main__':
    unittest.main()
