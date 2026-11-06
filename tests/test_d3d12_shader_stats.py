"""RC74: compiled AGX shader statistics as D3D12 owner receipts.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The owner turns Asahi's per-part statistics message (agx_compile_nir ->
agx2_stats_util_debug, a format Mesa generates from shader_stats.xml) into the
measure-d3d12-shader-stats receipt. If the parser drifts from Mesa's generated
format the receipt silently disappears; if the tags are wrong a GravityMark
ranking attributes instructions to the wrong pipeline. The real owner code
(shader_stats_message and its scopes) runs here against the real generated
agx2_stats_util_debug of the pinned host Mesa build: every field must arrive in
its slot, a compute compile must carry its shader hash, block and shared size,
a draw's VS/FS parts the pipeline's stage hashes, a part compiled with no tag
(a draw's GS/tessellation helper, a meta shader) must be marked internal, and
other debug messages must be ignored. Also checks the hooks are wired: the
callback with any trace (refusals-only too, RC75), the compute compile and draw_vbo
inside their scopes.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_dxbc_to_nir import local_tree, host_flags

ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'
OWNER = WINSYS / 'agx_d3d12_windows.cpp'
DDI = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'


def owner_stats_block():
    text = OWNER.read_text()
    start = text.index('/* RC74 receipt-only: the AGX statistics of every compiled shader part')
    return text[start:text.index('static void install_shader_stats(', start)]


def function(text, marker):
    start = text.index(marker)
    return text[start:text.index('\n}', start) + 2]


PRELUDE = r'''
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include "compiler/shader_enums.h"
#include "util/u_debug.h"
#include "util/shader_stats.h"
#include "agx_win32_shader_stats.h"
using UINT = unsigned; using PCSTR = const char *; using HRESULT = int;
#define S_OK 0
typedef struct { unsigned long long StatsHash[5]; unsigned StatsBytes[5]; } AGX_D3D12_GRAPHICS;
static std::vector<std::vector<UINT>> receipts;
static void AdmissionUmdDiagnostic(PCSTR stage, HRESULT status, const UINT *v, UINT n) {
  assert(!strcmp(stage, "measure-d3d12-shader-stats") && status == S_OK && n == AGX_WIN32_STATS_COUNT);
  receipts.emplace_back(v, v + n);
}
'''

MAIN = r'''
static struct util_debug_callback callback() {
  struct util_debug_callback cb = {};
  cb.debug_message = shader_stats_message;
  return cb;
}
static struct agx2_stats stats(unsigned base) {
  struct agx2_stats s = {};
  s.instrs = base + 1; s.alu = base + 2; s.fscib = base + 3; s.ic = base + 4; s.code_size = base + 5;
  s.gprs = (uint16_t)(base + 6); s.uniforms = (uint16_t)(base + 7); s.scratch = base + 8;
  s.threads = (uint16_t)(base + 9); s.loops = base + 10; s.spills = base + 11; s.fills = base + 12;
  s.preamble_inst = base + 13;
  return s;
}
static void check_stats(const std::vector<UINT> &r, unsigned base) {
  assert(r[AGX_WIN32_STATS_INSTRS] == base + 1 && r[AGX_WIN32_STATS_ALU] == base + 2);
  assert(r[AGX_WIN32_STATS_CODE] == base + 5 && r[AGX_WIN32_STATS_GPRS] == base + 6);
  assert(r[AGX_WIN32_STATS_SCRATCH] == base + 8 && r[AGX_WIN32_STATS_THREADS] == base + 9);
  assert(r[AGX_WIN32_STATS_LOOPS] == base + 10 && r[AGX_WIN32_STATS_SPILLS] == base + 11);
  assert(r[AGX_WIN32_STATS_FILLS] == base + 12 && r[AGX_WIN32_STATS_PREAMBLE] == base + 13);
}
int main() {
  struct util_debug_callback cb = callback();
  /* compute: tagged by the scope around create_compute_state */
  static const UINT tokens[] = {0x10065, 6, 0x4c495844, 0x100, 0x10, 0}; /* the capture test's program */
  ShaderStatsTag tag = shader_stats_tag(tokens);
  assert(tag.Bytes == 24 && tag.Hash == 0x8f28162bed108d89ull);
  tag.Block = 256 | 1 << 11 | 1 << 22;
  tag.Shared = 4096;
  {
    const ShaderStatsTag *tags[MESA_SHADER_COMPUTE + 1] = {};
    tags[MESA_SHADER_COMPUTE] = &tag;
    ShaderStatsScope scope(tags);
    struct agx2_stats s = stats(1000);
    agx2_stats_util_debug(&cb, "CS", &s);
  }
  assert(receipts.size() == 1);
  const std::vector<UINT> &c = receipts[0];
  check_stats(c, 1000);
  assert(c[AGX_WIN32_STATS_HASH_HI] == 0x8f28162bu && c[AGX_WIN32_STATS_HASH_LO] == 0xed108d89u);
  assert(c[AGX_WIN32_STATS_STAGE] == MESA_SHADER_COMPUTE && c[AGX_WIN32_STATS_BYTES] == 24);
  assert(c[AGX_WIN32_STATS_BLOCK] == (256u | 1u << 11 | 1u << 22) && c[AGX_WIN32_STATS_SHARED] == 4096);
  /* outside any scope: internal */
  {
    struct agx2_stats s = stats(2000);
    agx2_stats_util_debug(&cb, "CS", &s);
  }
  assert(receipts.size() == 2 && receipts[1][AGX_WIN32_STATS_STAGE] == (0x100u | MESA_SHADER_COMPUTE));
  assert(!receipts[1][AGX_WIN32_STATS_HASH_HI] && !receipts[1][AGX_WIN32_STATS_HASH_LO]);
  check_stats(receipts[1], 2000);
  /* a draw: VS and FS carry their stage hashes, a helper CS is internal */
  AGX_D3D12_GRAPHICS graphics = {};
  graphics.StatsHash[0] = 0x1111222233334444ull; graphics.StatsBytes[0] = 400;
  graphics.StatsHash[4] = 0x5555666677778888ull; graphics.StatsBytes[4] = 800;
  {
    const ShaderStatsDraw draw(&graphics);
    struct agx2_stats v = stats(3000), f = stats(4000), h = stats(5000);
    agx2_stats_util_debug(&cb, "VS", &v);
    agx2_stats_util_debug(&cb, "FS", &f);
    agx2_stats_util_debug(&cb, "CS", &h);
    agx2_stats_util_debug(&cb, "GS", &h); /* the pipeline has no GS */
  }
  assert(receipts.size() == 6);
  assert(receipts[2][AGX_WIN32_STATS_HASH_LO] == 0x33334444u && receipts[2][AGX_WIN32_STATS_STAGE] == MESA_SHADER_VERTEX);
  assert(receipts[2][AGX_WIN32_STATS_BYTES] == 400 && !receipts[2][AGX_WIN32_STATS_BLOCK]);
  check_stats(receipts[2], 3000);
  assert(receipts[3][AGX_WIN32_STATS_HASH_HI] == 0x55556666u && receipts[3][AGX_WIN32_STATS_STAGE] == MESA_SHADER_FRAGMENT);
  check_stats(receipts[3], 4000);
  assert(receipts[4][AGX_WIN32_STATS_STAGE] == (0x100u | MESA_SHADER_COMPUTE));
  assert(receipts[5][AGX_WIN32_STATS_STAGE] == (0x100u | MESA_SHADER_GEOMETRY));
  /* the draw scope restored the empty tags */
  {
    struct agx2_stats s = stats(6000);
    agx2_stats_util_debug(&cb, "VS", &s);
  }
  assert(receipts.size() == 7 && receipts[6][AGX_WIN32_STATS_STAGE] == (0x100u | MESA_SHADER_VERTEX));
  /* other messages are not statistics */
  util_debug_message(&cb, PERF_INFO, "CS shader: %u instrs", 5u);
  util_debug_message(&cb, SHADER_INFO, "compiling %s", "variant");
  assert(receipts.size() == 7);
  std::puts("STATS_OK");
  return 0;
}
'''


class ShaderStatsReceipt(unittest.TestCase):
    def test_owner_receipt_round_trips_mesa_generated_statistics(self):
        build = local_tree("accelerated-desktop-ad03", "mesa-build")
        mesa = local_tree("reference", "mesa")
        if not build or not mesa or not (build / 'src/util/shader_stats.h').exists():
            raise unittest.SkipTest("host Mesa build not available")
        code = PRELUDE + owner_stats_block() + MAIN
        with tempfile.TemporaryDirectory(prefix='shader-stats-') as td:
            path = Path(td)
            (path / 'test.cpp').write_text(code)
            flags = [f for f in host_flags(build) if not f.startswith('-std=')]
            built = subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-fsanitize=address,undefined',
                                    *flags, '-I', str(WINSYS), '-I', str(build / 'src'), '-I', str(mesa / 'src'),
                                    '-I', str(mesa / 'include'), str(path / 'test.cpp'), '-o', str(path / 'test'),
                                    str(build / 'src/util/libmesa_util.a'), str(build / 'src/c11/impl/libmesa_util_c11.a'),
                                    str(build / 'src/util/blake3/libblake3.a'), '-L/opt/homebrew/opt/zstd/lib', '-lzstd',
                                    '-lz', '-lpthread'],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr[-4000:])
            run = subprocess.run([str(path / 'test')], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr[-4000:])
            self.assertIn('STATS_OK', run.stdout)

    def test_hooks_are_wired_where_asahi_compiles(self):
        text = OWNER.read_text()
        install = function(text, 'static void install_shader_stats(')
        # RC75: refusals-only traces (EXP2000) carry measure- receipts only
        self.assertIn('!AdmissionUmdMeasureEnabled()', install)
        self.assertNotIn('AdmissionUmdDiagnosticEnabled()', install)
        self.assertIn('pipe->set_debug_callback(pipe, &callback);', install)
        self.assertIn('install_shader_stats(owner->Context);', text)
        compute = function(text, 'extern "C" HRESULT AgxD3d12OwnerCreateComputeFlags(')
        scope = compute.index('ShaderStatsScope stats(tags);')
        self.assertLess(scope, compute.index('compute->State = pipe->create_compute_state(pipe, &state);'))
        draw = function(text, 'extern "C" HRESULT AgxD3d12OwnerDraw(')
        self.assertLess(draw.index('const ShaderStatsDraw stats_draw(graphics);'), draw.index('pipe->draw_vbo('))
        graphics = function(text, 'extern "C" HRESULT AgxD3d12OwnerCreateGraphics(')
        self.assertLess(graphics.index('memset(graphics, 0, sizeof(*graphics));'), graphics.index('graphics->StatsHash[i] = tag.Hash;'))
        # Asahi precompiles VS/FS default variants in create_*_state
        # (agx_create_shader_state): the stage tags must already be in scope
        created = graphics.index('const ShaderStatsDraw stats_create(graphics);')
        self.assertLess(graphics.index('graphics->StatsHash[i] = tag.Hash;'), created)
        self.assertLess(created, graphics.index('graphics->Vs = create_shader('))
        self.assertLess(created, graphics.index('graphics->Fs = desc->Ps ? create_shader('))

    def test_dispatch_receipts_name_their_compute_shader(self):
        # The per-shader ranking joins measure-d3d12-dispatch (6th value) with
        # the statistics' HASH_LO: both must hash the same bytes, the whole
        # CS program (its dword count from the token header).
        text = DDI.read_text()
        compile_fn = function(text, 'static HRESULT CompileCompute(')
        self.assertIn('pso->cs_tag = static_cast<UINT>(agx_win32_shader_hash(pso->cs, '
                      'static_cast<size_t>(pso->cs_dwords) * sizeof(UINT)));', compile_fn)
        self.assertLess(compile_fn.index('pso->cs_tag ='), compile_fn.index('pso->compiled ='))
        dispatch = function(text, 'static void RecordDispatch(')
        self.assertIn('const UINT receipt[] = {refusal, x, y, z, root ? root->parameters : 0u, pso ? pso->cs_tag : 0u};',
                      dispatch)
        self.assertIn('AdmissionUmdDiagnostic("measure-d3d12-dispatch", refusal ? E_NOTIMPL : S_OK, receipt, 6);', dispatch)
        tag = function(OWNER.read_text(), 'static ShaderStatsTag shader_stats_tag(')
        self.assertIn('tag.Bytes = tokens[1] * 4u;', tag)
        self.assertIn('tag.Hash = agx_win32_shader_hash(tokens, tag.Bytes);', tag)


if __name__ == '__main__':
    unittest.main()
