"""G2: the KMD's CDM/USC encoder (shared/src/apple_agx_cdm.c).

What it catches: a bit-layout error in the words the KMD writes for its own
compute dispatches (present blt). Such an error is invisible until the GPU
faults or hangs. The encoder is checked against fixed words and, when the
pinned Mesa reference tree is present, against Mesa's own generated packer
(gen_pack.py over src/asahi/genxml/cmdbuf.xml) on the same inputs.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers/apple-agx/shared"
MESA = Path(os.environ.get("APPLE_AGX_MESA_REFERENCE",
                           ROOT.parents[1] / ".local/reference/mesa"))

FIXED = r'''
#include "apple_agx_cdm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  unsigned int w[16]; unsigned char u[64];
  APPLE_AGX_CDM_DISPATCH d; APPLE_AGX_USC_PROGRAM p;
  memset(&d, 0, sizeof(d)); memset(&p, 0, sizeof(p));
  assert(AppleAgxCdmBarrier(w, 1) == 1 && w[0] == 0x600fffffu);
  assert(AppleAgxCdmTerminate(w, 2) == 2 && w[0] == 0x40000000u && w[1] == 0u);
  assert(!AppleAgxCdmTerminate(w, 1));
  d.UniformHalfs = 8; d.TextureStates = 8; d.SamplerStates = 1; d.PipelineOffset = 0x1040;
  d.Global[0] = 1024; d.Global[1] = d.Global[2] = 1;
  d.Local[0] = 128; d.Local[1] = d.Local[2] = 1;
  assert(AppleAgxCdmLaunch(w, 8, &d) == 8);
  /* uniforms 1 group << 1, textures 1 group << 4, samplers 1 << 9,
   * preshader 0 -> 1 group << 12; block type Launch, mode Direct. */
  assert(w[0] == ((1u << 1) | (1u << 4) | (1u << 9) | (1u << 12)));
  assert(w[1] == 0x1040u && w[2] == 1024u && w[5] == 128u && w[7] == 1u);
  d.UniformHalfs = 512; assert(AppleAgxCdmLaunch(w, 8, &d) == 8 && ((w[0] >> 1) & 7u) == 0u);
  d.UniformHalfs = 513; assert(!AppleAgxCdmLaunch(w, 8, &d)); d.UniformHalfs = 8;
  d.PipelineOffset = 0x1044; assert(!AppleAgxCdmLaunch(w, 8, &d)); d.PipelineOffset = 0x1040;
  d.Local[1] = 0; assert(!AppleAgxCdmLaunch(w, 8, &d)); d.Local[1] = 1;
  assert(!AppleAgxCdmLaunch(w, 7, &d));
  assert(AppleAgxUscUniform(u, 8, 0, 8, 0x1500002000ULL) == 8);
  { unsigned long long v; memcpy(&v, u, 8);
    assert(v == (0x1dULL | (8ULL << 20) | ((0x1500002000ULL >> 2) << 26))); }
  assert(!AppleAgxUscUniform(u, 8, 0, 8, 0x1500002002ULL));
  assert(AppleAgxUscUniform(u, 8, 4, 64, 0x10ULL) == 8 && ((u[2] >> 4) | ((u[3] & 3u) << 4)) == 0u);
  p.CodeOffset = 0x2000; p.Gprs = 10;
  assert(AppleAgxUscProgram(u, 64, &p) == 16);
  assert(u[0] == 0x4d && u[4] == 0x0d && u[10] == 0x8d && u[14] == 0x88 && u[15] == 0);
  { unsigned int shared; memcpy(&shared, u, 4); assert(shared == (0x4du | (0x24u << 10))); }
  { unsigned int code; memcpy(&code, u + 6, 4); assert(code == 0x2000u && u[5] == (3u << 2)); }
  assert(u[11] == 2u); /* 10 GPRs -> 2 groups of 8 */
  p.PreambleOffset = 0x2400; assert(AppleAgxUscProgram(u, 64, &p) == 22 && u[14] == 0x38);
  p.CodeOffset = 0; assert(!AppleAgxUscProgram(u, 64, &p));
  puts("cdm fixed: PASS");
  return 0;
}
'''

DIFFERENTIAL = r'''
#include "apple_agx_cdm.h"
/* Mesa's generated packer is not built for this warning set. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#include "agx_pack.h"
#pragma clang diagnostic pop
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void check_launch(unsigned uniforms, unsigned textures, unsigned samplers,
                         unsigned preshader, unsigned pipeline) {
  APPLE_AGX_CDM_DISPATCH d; unsigned int mine[8];
  uint32_t mesa[2];
  memset(&d, 0, sizeof(d));
  d.UniformHalfs = uniforms; d.TextureStates = textures; d.SamplerStates = samplers;
  d.PreshaderGprs = preshader; d.PipelineOffset = pipeline;
  for (int i = 0; i < 3; ++i) { d.Global[i] = 3u + i; d.Local[i] = 1u + i; }
  assert(AppleAgxCdmLaunch(mine, 8, &d) == 8);
  agx_pack(&mesa[0], CDM_LAUNCH_WORD_0, cfg) {
    cfg.uniform_register_count = uniforms; cfg.texture_state_register_count = textures;
    cfg.sampler_state_register_count = samplers; cfg.preshader_register_count = preshader;
    cfg.mode = AGX_CDM_MODE_DIRECT;
  }
  agx_pack(&mesa[1], CDM_LAUNCH_WORD_1, cfg) { cfg.pipeline = pipeline; }
  assert(mine[0] == mesa[0] && mine[1] == mesa[1]);
}
int main(void) {
  unsigned int mine[2]; uint32_t mesa[2] = {0, 0};
  check_launch(8, 8, 1, 0, 0x40);
  check_launch(64, 0, 0, 16, 0x12340);
  check_launch(130, 24, 4, 33, 0xffffffc0u);
  check_launch(512, 256, 7, 256, 0x80);
  assert(AppleAgxCdmBarrier(mine, 1) == 1);
  agx_pack(&mesa[0], CDM_BARRIER, cfg) {
    cfg.unk_0 = cfg.unk_1 = cfg.unk_2 = cfg.usc_cache_inval = cfg.unk_4 = true;
    cfg.unk_5 = cfg.unk_6 = cfg.unk_7 = cfg.unk_8 = cfg.unk_9 = cfg.unk_10 = true;
    cfg.unk_11 = cfg.unk_12 = cfg.unk_13 = cfg.unk_14 = cfg.unk_15 = true;
    cfg.unk_16 = cfg.unk_17 = cfg.unk_18 = cfg.unk_19 = true;
  }
  assert(mine[0] == mesa[0]);
  assert(AppleAgxCdmTerminate(mine, 2) == 2);
  agx_pack(mesa, CDM_STREAM_TERMINATE, cfg);
  assert(mine[0] == mesa[0] && mine[1] == mesa[1]);
  {
    unsigned char u[8]; uint8_t m[8];
    const unsigned halfs[3] = {1, 33, 64}; const unsigned starts[3] = {0, 4, 255};
    for (int i = 0; i < 3; ++i) {
      assert(AppleAgxUscUniform(u, 8, starts[i], halfs[i], 0x1500002000ULL + 4u * i) == 8);
      agx_pack(m, USC_UNIFORM, cfg) {
        cfg.start_halfs = starts[i]; cfg.size_halfs = halfs[i];
        cfg.buffer = 0x1500002000ULL + 4u * i;
      }
      assert(memcmp(u, m, 8) == 0);
    }
  }
  {
    const unsigned gprs[4] = {1, 10, 128, 256}; const unsigned shared[4] = {0, 256, 4096, 65536};
    for (int i = 0; i < 4; ++i) {
      APPLE_AGX_USC_PROGRAM p; unsigned char u[32]; uint8_t m[32]; unsigned n = 0;
      memset(&p, 0, sizeof(p)); p.CodeOffset = 0x4000u + 0x80u * i; p.Gprs = gprs[i];
      p.SharedBytes = shared[i]; p.PreambleOffset = i & 1 ? 0x8000u : 0u;
      unsigned bytes = AppleAgxUscProgram(u, sizeof(u), &p);
      assert(bytes);
      agx_pack(m + n, USC_SHARED, cfg) {
        cfg.layout = AGX_SHARED_LAYOUT_VERTEX_COMPUTE;
        cfg.uses_shared_memory = shared[i] != 0;
        cfg.bytes_per_threadgroup = shared[i] ? shared[i] : 65536;
      }
      n += AGX_USC_SHARED_LENGTH;
      agx_pack(m + n, USC_SHADER, cfg) { cfg.code = p.CodeOffset; cfg.unk_2 = 3; }
      n += AGX_USC_SHADER_LENGTH;
      agx_pack(m + n, USC_REGISTERS, cfg) { cfg.register_count = gprs[i]; }
      n += AGX_USC_REGISTERS_LENGTH;
      if (p.PreambleOffset) {
        agx_pack(m + n, USC_PRESHADER, cfg) { cfg.code = p.PreambleOffset; }
        n += AGX_USC_PRESHADER_LENGTH;
      } else {
        agx_pack(m + n, USC_NO_PRESHADER, cfg);
        n += AGX_USC_NO_PRESHADER_LENGTH;
      }
      assert(bytes == n && memcmp(u, m, n) == 0);
    }
  }
  puts("cdm differential: PASS");
  return 0;
}
'''


def build_and_run(test, program, extra_sources=(), includes=(), defines=()):
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "t.c").write_text(program)
        exe = tmp / "t"
        args = [os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-function", "-fsanitize=address,undefined",
                *[f"-D{d}" for d in defines], "-I", str(SHARED / "include"), "-I", str(tmp),
                *[x for i in includes for x in ("-I", str(i))],
                str(tmp / "t.c"), str(SHARED / "src/apple_agx_cdm.c"), *map(str, extra_sources),
                "-o", str(exe)]
        built = subprocess.run(args, capture_output=True, text=True)
        test.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(exe)], capture_output=True, text=True)
        test.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        return ran.stdout


class AppleAgxCdmEncoder(unittest.TestCase):
    def test_fixed_words(self):
        self.assertIn("cdm fixed: PASS", build_and_run(self, FIXED))

    @unittest.skipUnless((MESA / "src/asahi/genxml/gen_pack.py").exists(),
                         "pinned Mesa reference tree not present")
    def test_matches_mesa_generated_packer(self):
        genxml = MESA / "src/asahi/genxml"
        with tempfile.TemporaryDirectory() as tmp:
            header = subprocess.run(
                ["python3", str(genxml / "gen_pack.py"), str(genxml / "cmdbuf.xml"),
                 str(genxml / "agx_pack_header.h")], capture_output=True, text=True, check=True)
            program = DIFFERENTIAL
            Path(tmp, "agx_pack.h").write_text(header.stdout)
            out = build_and_run(self, program, includes=(
                Path(tmp), MESA / "src", MESA / "include", MESA / "src/asahi/genxml"),
                defines=("HAVE_PTHREAD=1", "HAVE_TIMESPEC_GET=1", "HAVE_STRUCT_TIMESPEC=1"))
        self.assertIn("cdm differential: PASS", out)


if __name__ == "__main__":
    unittest.main()
