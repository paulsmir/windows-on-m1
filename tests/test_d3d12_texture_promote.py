"""T5: constant texture handles promoted to texture-state registers.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
With APPLE_AGX_D3D_PROMOTE_TEX=1 a D3D12 compute or fragment shader that
samples a constant texture handle inside a loop loads those descriptors
once, in its preamble, into texture-state registers and samples them as
registers (winsys/agx_win32_d3d_texture_promote.h). The bugs this guards:
- a promoted register that collides with a bindful one: the USC TEXTURE
  word loads registers 0..15 from the bindful table, so a promoted handle
  below 16 samples whichever texture the batch bound there (the r17
  prototype did exactly this);
- a USC register count below the promoted end: the draw or launch then has
  no register for the handle the preamble stores;
- a promotion that changes more than the texture addressing: another
  texture, sampler or control flow;
- a shader outside the gate, or any shader with the switch off, compiling
  to a different binary than before (144a46deb6ed2427 grew by 137
  instructions and 12 spills when promoted without the gate).
The two GravityMark kernels the change is for are checked on their captured
DXBC when the capture directory is present; the synthetic loop shader
covers the same contract without it.

Scope: this is the deterministic contract only (which handles are
promoted, to which registers, the register count, the unchanged binary
outside the gate). tests/dxil_agx_cost_fixture.c compiles through stand-ins
for agx_nir_lower_bindings, agx_nir_lower_sysvals and
agx_nir_layout_uniforms, so its instruction, register, thread and code
size figures are not those of the driver and none is asserted here; the
cost of the change is measured on a production-shaped compile that
reproduces the guest's measure-d3d12-shader-stats rows.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import collections
import os
import re
import subprocess
import sys
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, local_tree, project

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
MESA = ROOT / "drivers/apple-agx/mesa"
WINSYS = MESA / "winsys"
COMPILER = MESA / "scripts/native-asahi-latency-schedule.py"
BINDFUL_REGISTERS = 16

# (capture, fixture flags of the hardware configuration, bindless samples
# left, texture states used). The two counts are properties of the shaders
# (samples of a non-constant handle; distinct constant handles sampled),
# the same through the fixture's stand-in binding pass and through
# agx_nir_lower_bindings.
KERNELS = (
    ("806ee7b1bdb1944a-s5-r0.bin", ("--root-views=e,3", "--soft-fault-loads"), 4, 3),
    ("428487dae22a5191-s4-r0.bin", ("--fs-backend", "--root-views=7,1", "--soft-fault-loads"), 6, 5),
)
CONTROL = ("if", "else", "while", "jmp", "pop_exec", "push_exec")
TEXTURE = ("smp", "img")


def registers(descriptors, units):
    """agx_win32_d3d_texture_state_registers"""
    return max(descriptors, (units + 1) // 2)


class Listing:
    """A fixture run: COST fields, the PROMOTE line, the main function and
    the preamble as (pc, opcode, operands)."""

    def __init__(self, stdout):
        cost = next((l for l in stdout.splitlines() if l.startswith("COST ")), None)
        self.cost = {k: (int(v) if v.isdigit() else v)
                     for k, v in re.findall(r"([a-z-]+) (\S+)", cost[5:])} if cost else None
        promote = re.search(r"^PROMOTE units (\d+) descriptors (\d+) registers (\d+)$", stdout, re.M)
        self.promote = tuple(map(int, promote.groups())) if promote else None
        preamble, main = "", stdout
        if "PREAMBLE BEGIN" in stdout:
            preamble, main = stdout.split("PREAMBLE BEGIN", 1)[1].split("PREAMBLE END", 1)
        self.main, self.preamble = self.rows(main), self.rows(preamble)

    @staticmethod
    def rows(text):
        return [(int(m.group(1), 16), m.group(2), m.group(3).strip()) for m in
                re.finditer(r"^\s*([0-9a-f]+):\s+[0-9a-f]+\s+(\S+)(.*)$", text, re.M)]

    def innermost_loops(self):
        spans = [(pc + int(args.split()[0]), pc) for pc, op, args in self.main
                 if op == "jmp_any" and int(args.split()[0]) < 0]
        return [s for s in spans if not any(o != s and s[0] <= o[0] and o[1] <= s[1] for o in spans)]

    def bindless_samples(self):
        """pcs of the samples that name their texture as heap uniform plus
        offset register"""
        return [pc for pc, op, args in self.main
                if op.startswith("smp") and re.search(r"\bdu\d+, [\^$]?r\d+", args)]

    def bindless_in_innermost_loops(self):
        loops = self.innermost_loops()
        return [pc for pc in self.bindless_samples() if any(a <= pc <= b for a, b in loops)]

    def texture_states(self):
        """register -> uses in the main function"""
        return collections.Counter(int(m.group(1)) for pc, op, args in self.main
                                   for m in [re.search(r"\bts(\d+)\b", args)] if m)

    def stored_states(self):
        """(register, heap uniform, byte offset) of the preamble's
        tex_state_store instructions (disassembled as load tsN)"""
        return [(int(m.group(1)), m.group(2), int(m.group(3))) for pc, op, args in self.preamble if op == "load"
                for m in [re.match(r"ts(\d+), (du\d+), (\d+),", args)] if m]

    def control_flow(self):
        return [op for pc, op, args in self.main if op.startswith(CONTROL)]

    def texture_instructions(self):
        """the texture instructions without the texture operand, the
        places of the other operands (register or preamble uniform) and
        the wait slot"""
        out = []
        for pc, op, args in self.main:
            if not op.startswith(TEXTURE):
                continue
            args = re.sub(r"\bdu\d+, [\^$]?r\d+[lh]?\b|\bts\d+\b", "TEXTURE", args)
            args = re.sub(r"[\^$]?\br\d+[lh]?(_r\d+[lh]?)*|\bu\d+[lh]?\b", "value", args)
            out.append(op + " " + re.sub(r", [ab]$", "", args))
        return out


class Helpers(unittest.TestCase):
    """agx_win32_d3d_texture_promote.h compiled on the host."""

    @classmethod
    def setUpClass(cls):
        mesa = local_tree("reference", "mesa")
        if not mesa:
            raise unittest.SkipTest("reference Mesa tree not available")
        cls.tmp = tempfile.TemporaryDirectory(prefix="d3d12-texture-promote-helpers-")
        source = Path(cls.tmp.name) / "helpers.c"
        source.write_text(r'''#include <stdio.h>
#include <stdlib.h>
#include "agx_win32_d3d_texture_promote.h"
int main(int argc, char **argv)
{
   for (int a = 1; a + 1 < argc; a += 2)
      printf("%u\n", agx_win32_d3d_texture_state_registers((uint32_t)strtoul(argv[a], NULL, 0),
                                                           (uint32_t)strtoul(argv[a + 1], NULL, 0)));
   for (int stage = MESA_SHADER_VERTEX; stage <= MESA_SHADER_COMPUTE; ++stage)
      for (unsigned m = 0; m < 8; ++m)
         if (agx_win32_d3d_promote_textures(m & 1, (mesa_shader_stage)stage, m & 2, m & 4))
            printf("key %s %u\n", stage == MESA_SHADER_COMPUTE ? "compute" :
                                  stage == MESA_SHADER_FRAGMENT ? "fragment" : "other", m);
   return 0;
}
''')
        cls.binary = Path(cls.tmp.name) / "helpers"
        subprocess.run(["clang", "-std=c11", "-Wall", "-Werror", "-I", str(mesa / "src"), "-I", str(mesa / "include"),
                        "-I", str(WINSYS), str(source), "-o", str(cls.binary)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_helpers(self, *pairs):
        args = [str(v) for pair in pairs for v in pair]
        return subprocess.run([str(self.binary), *args], check=True, capture_output=True, text=True).stdout.splitlines()

    def test_register_count_covers_bindful_and_promoted_ranges(self):
        # (bindful descriptors, promoted end in 16-bit units)
        pairs = [(0, 0), (20, 0), (20, 38), (19, 42), (0, 36), (3, 33), (24, 34), (17, 34), (16, 254),
                 (300, 254)]
        out = self.run_helpers(*pairs)
        self.assertEqual([int(v) for v in out[:len(pairs)]], [registers(d, u) for d, u in pairs])
        # no promotion: Gallium's count; the lighting kernel and the planet PS
        self.assertEqual([int(v) for v in out[:4]], [0, 20, 20, 21])
        # an odd unit count rounds up, more than 16 registers are not capped
        self.assertEqual([int(v) for v in out[5:10]], [17, 24, 17, 127, 300])

    def test_only_enabled_application_compute_and_fragment_shaders_get_the_key(self):
        keys = [l for l in self.run_helpers() if l.startswith("key ")]
        # mask 1: enabled, without internal kernel (2) or secondary (4)
        self.assertEqual(keys, ["key fragment 1", "key compute 1"])


class Sources(unittest.TestCase):
    def test_promoted_registers_start_after_the_bindful_ones(self):
        text = COMPILER.read_text()
        base = re.search(r"#define AGX_WIN32_PROMOTE_TEXTURE_BASE (\d+)u", text)
        units = re.search(r"#define AGX_WIN32_TEXTURE_STATE_UNITS +(\d+)u", text)
        self.assertTrue(base and units)
        self.assertEqual(int(base.group(1)), BINDFUL_REGISTERS)
        # the cursor is agx_shader_info::texture_state_count, a uint8_t of
        # 16-bit units, two per register
        self.assertLessEqual(int(units.group(1)), 255)
        self.assertEqual(int(units.group(1)) % 2, 0)
        self.assertGreater(int(units.group(1)), 2 * BINDFUL_REGISTERS)
        self.assertIn("const uint8_t windows_promote_base = (uint8_t)(2u * AGX_WIN32_PROMOTE_TEXTURE_BASE);", text)
        self.assertIn("   if (windows_promote && info->texture_state_count == windows_promote_base)\n"
                      "      info->texture_state_count = 0;", text)
        self.assertIn("windows_promote ? &info->texture_state_count : NULL, NULL);", text)
        self.assertIn(".preamble_storage_size[nir_preamble_class_image] = AGX_WIN32_TEXTURE_STATE_UNITS,", text)
        # the gate precedes the pass's size table: a refused shader is
        # compiled with the count pointer clear
        self.assertIn("      if (ts_count && !agx_win32_promote_texture_states(nir))\n"
                      "         ts_count = NULL;\n\n''' + PROMOTE_SIZES_ANCHOR\n", text)
        self.assertIn("PROMOTE_SIZES_ANCHOR = '''      unsigned sizes[] = {\n", text)

    def test_only_the_d3d12_owner_reads_the_switch(self):
        users = {p.name: p.read_text(errors="replace") for p in WINSYS.iterdir()
                 if p.suffix in (".c", ".cpp", ".h", ".inc") and "D3dPromoteTextures" in p.read_text(errors="replace")}
        self.assertEqual(sorted(users), ["agx_d3d12_windows.cpp", "agx_win32_asahi_bo.h", "agx_win32_d3d_texture_promote.h"])
        owner = users["agx_d3d12_windows.cpp"]
        read = re.search(r'owner->Backend\.D3dPromoteTextures =\s+GetEnvironmentVariableA\("APPLE_AGX_D3D_PROMOTE_TEX", '
                         r"promote_textures,\s+sizeof\(promote_textures\)\) == 1 && promote_textures\[0\] == '1';", owner)
        self.assertTrue(read, "switch read")
        self.assertEqual(owner.count("D3dPromoteTextures"), 1)
        # shaders are compiled through the screen: the field is set first
        self.assertLess(read.end(), owner.index("owner->Screen = AgxWin32AsahiScreenCreateForWindows(", read.end()))
        self.assertEqual(sum(text.count("APPLE_AGX_D3D_PROMOTE_TEX") for name, text in users.items()
                             if name.endswith((".c", ".cpp"))), 1)


class Projection(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="d3d12-texture-promote-projection-")
        out = project(Path(cls.tmp.name) / "out")
        if out is None:
            cls.tmp.cleanup()
            raise unittest.SkipTest("reference Mesa tree not available")
        cls.out = out
        cls.asahi = out / "src/gallium/drivers/asahi"
        cls.state = (cls.asahi / "agx_state.c").read_text()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_gallium_keeps_bindful_registers_below_the_promoted_base(self):
        for path in (self.asahi / "agx_state.h", self.out / "src/asahi/lib/agx_nir_lower_tilebuffer.c"):
            limit = re.search(r"#define AGX_NUM_TEXTURE_STATE_REGS (\d+)", path.read_text())
            self.assertTrue(limit, path.name)
            self.assertEqual(int(limit.group(1)), BINDFUL_REGISTERS, path.name)
        bindings = (self.asahi / "agx_nir_lower_bindings.c").read_text()
        self.assertIn("unsigned idx = (nir_scalar_as_uint(index_scalar) * 2) + offset;\n\n"
                      "         if (idx < AGX_NUM_TEXTURE_STATE_REGS) {", bindings)
        self.assertIn("if (tex->texture_index < AGX_NUM_TEXTURE_STATE_REGS &&", bindings)
        # the bindful table reaches no register from the base up
        word = "MIN2(batch->texture_count[stage], AGX_NUM_TEXTURE_STATE_REGS);"
        self.assertEqual(sum(p.read_text(errors="replace").count(word) for p in self.asahi.iterdir() if p.is_file()), 1)

    def test_only_agx_compile_nir_sets_the_key_from_the_backend_switch(self):
        self.assertIn('#include "agx_win32_d3d_texture_promote.h"', self.state)
        self.assertEqual(self.state.count("promote_textures"), 2)
        body = function_body(self.state, "agx_compile_nir")
        key = re.search(r"const AGX_WIN32_ASAHI_BACKEND \*windows_promote = dev->windows_private;\s+"
                        r"key\.promote_textures = agx_win32_d3d_promote_textures\(\s+"
                        r"windows_promote && windows_promote->D3dPromoteTextures, stage,\s+"
                        r"internal_kernel, secondary\);", body)
        self.assertTrue(key, "key bit")
        # the key is complete before the last NIR pass and the backend
        self.assertLess(key.end(), body.index("NIR_PASS(_, nir, agx_win32_speculate_soft_fault_loads);"))
        self.assertLess(key.end(), body.index("agx_compile_shader_nir(nir, &key, &compiled->b);"))
        for path in self.asahi.iterdir():
            if path.is_file() and path.name != "agx_state.c":
                self.assertNotIn("promote_textures", path.read_text(errors="replace"), path.name)

    def test_fragment_and_compute_words_cover_the_promoted_registers(self):
        counts = re.findall(r"cfg\.texture_state_register_count =\s+([^;]+);", self.state)
        norm = [re.sub(r"\s+", " ", c) for c in counts]
        self.assertEqual(norm, [
            "nr_tex",
            "agx_nr_tex_descriptors(batch, vs)",
            "agx_win32_d3d_texture_state_registers( agx_nr_tex_descriptors(batch, ctx->fs), "
            "ctx->fs->b.info.texture_state_count)",
            "agx_win32_d3d_texture_state_registers( cs ? agx_nr_tex_descriptors(batch, cs) : 0, "
            "cs ? cs->b.info.texture_state_count : 0u)",
        ])
        # the two words launch the shader whose count they use
        fs = self.state.index("ctx->fs->b.info.texture_state_count")
        self.assertIn("ctx->fs->b.info.nr_preamble_gprs", self.state[fs - 2500:fs + 2500])
        body = function_body(self.state, "agx_launch_internal") if "agx_launch_internal(" in self.state else ""
        self.assertNotIn("agx_win32_d3d_texture_state_registers", body)


def run_fixture(binary, source, *flags):
    r = subprocess.run([str(binary), str(source), *flags], capture_output=True, text=True, errors="replace",
                       timeout=600)
    return Listing(r.stdout)


class Compile(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import agx_shader_cost
        cls.tmp = tempfile.TemporaryDirectory(prefix="d3d12-texture-promote-")
        cls.binary = agx_shader_cost.build(cls.tmp.name)
        if cls.binary is None:
            cls.tmp.cleanup()
            raise unittest.SkipTest("host Mesa / dxil-spirv trees absent")
        cls.captures = local_tree("experiments", "EXP2138-nullps", "evidence", "EXP2139-capture", "capture")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def pair(self, source, *flags):
        off = run_fixture(self.binary, source, *flags, "--print-selected")
        on = run_fixture(self.binary, source, *flags, "--promote-tex", "--print-selected", "--print-preamble")
        self.assertTrue(off.cost and on.cost, source)
        self.assertIsNone(off.promote)
        return off, on

    def check_promoted(self, off, on, bindless, states):
        # 1: no bindless sample is left in an innermost loop
        self.assertTrue(off.bindless_in_innermost_loops())
        self.assertEqual(on.bindless_in_innermost_loops(), [])
        self.assertEqual(len(on.bindless_samples()), bindless)
        # 2: promoted registers lie after the bindful ones and inside the
        # register count of the launch, one per table entry
        units, descriptors, count = on.promote
        stored = on.stored_states()
        promoted = sorted(r for r, heap, offset in stored)
        self.assertEqual(promoted, list(range(BINDFUL_REGISTERS, BINDFUL_REGISTERS + states)))
        self.assertEqual(len({(heap, offset) for r, heap, offset in stored}), states)
        self.assertEqual(units, 2 * (promoted[-1] + 1))
        self.assertEqual(count, registers(descriptors, units))
        self.assertTrue(all(min(descriptors, BINDFUL_REGISTERS) <= r < count for r in promoted))
        used, before = on.texture_states(), off.texture_states()
        self.assertEqual({r for r in used if r >= BINDFUL_REGISTERS}, set(promoted))
        self.assertEqual({r: n for r, n in used.items() if r < BINDFUL_REGISTERS}, dict(before))
        self.assertEqual(max(before, default=0) < BINDFUL_REGISTERS, True)
        # 3: the same texture instructions apart from the texture operand,
        # under the same control flow
        self.assertEqual(on.control_flow(), off.control_flow())
        self.assertEqual(on.texture_instructions(), off.texture_instructions())

    def test_synthetic_loop_samples_use_promoted_registers(self):
        off, on = self.pair("--synthetic-loop-samples=3")
        self.assertEqual(len(off.bindless_in_innermost_loops()), 3)
        self.check_promoted(off, on, 0, 3)
        # the three descriptors the bindless samples read, one texture
        # descriptor (24 bytes) apart
        offsets = sorted(offset for r, heap, offset in on.stored_states())
        self.assertEqual([b - a for a, b in zip(offsets, offsets[1:])], [24, 24])
        # registers 16..18: 38 units, no bindful descriptor, 19 registers
        self.assertEqual(on.promote, (38, 0, 19))

    def test_shader_without_a_loop_sample_is_not_promoted(self):
        # constant handles (table and heap), no loop: the gate refuses
        for source in ("--synthetic-array-samples=2", "--synthetic-array-samples=2,heap"):
            off, on = self.pair(source)
            self.assertTrue(off.bindless_samples(), source)
            self.assertEqual(on.cost["binary-fnv"], off.cost["binary-fnv"], source)
            self.assertEqual(on.promote[0], 0, source)
            self.assertEqual(on.promote[2], on.promote[1], source)
            self.assertEqual(on.stored_states(), [], source)

    def test_gravitymark_kernels(self):
        if not self.captures:
            self.skipTest("GravityMark capture not available")
        for name, flags, bindless, states in KERNELS:
            with self.subTest(name):
                off, on = self.pair(self.captures / name, *flags)
                self.check_promoted(off, on, bindless, states)

    def test_other_captures_compile_identically_under_the_switch(self):
        if not self.captures:
            self.skipTest("GravityMark capture not available")
        targets = {name for name, flags, bindless, states in KERNELS}
        names = sorted(p.name for p in self.captures.iterdir()
                       if re.fullmatch(r"[0-9a-f]{16}-s[45]-r\d\.bin", p.name) and p.name not in targets)

        def both(name):
            flags = ["--soft-fault-loads"] + (["--fs-backend"] if "-s4-" in name else [])
            return name, run_fixture(self.binary, self.captures / name, *flags), \
                run_fixture(self.binary, self.captures / name, *flags, "--promote-tex")

        with ThreadPoolExecutor(os.cpu_count() or 4) as pool:
            results = list(pool.map(both, names))
        compiled = 0
        for name, off, on in results:
            if off.cost is None:
                # captures the fixture refuses compile neither way
                self.assertIsNone(on.cost, name)
                continue
            compiled += 1
            self.assertEqual(on.cost["binary-fnv"], off.cost["binary-fnv"], name)
            self.assertEqual(on.promote[0], 0, name)
        self.assertGreater(compiled, 150)


if __name__ == "__main__":
    unittest.main()
