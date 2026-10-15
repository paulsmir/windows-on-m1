"""EXP1285/EXP1288: the D3D10 frontend's multisample reporting obeys the
runtime's cast-family rule.

EXP1285-EXP1287: whenever the frontend reported 2x/4x, the runtime refused
every D3D device (D3D11CreateDevice 0x887A0020 before the first resource).
EXP1288 measured why (measure-msaa-check receipts): the runtime walks each
DXGI cast family, typeless parent first, asks sample counts 2..31, and
refuses the device when a member answers levels its typeless parent does
not, or when a read-only member (neither a render nor a depth target)
answers MULTISAMPLE_RENDERTARGET; EXP1289: or any levels.
Bug caught, for every probe policy: a render/depth member whose levels
differ from its parent's, a read-only member with levels, MULTISAMPLE_RENDERTARGET on a read-only member or without 4x
levels, a render/depth member with levels but no MULTISAMPLE_RENDERTARGET,
any multisample answer with the policy off, MULTISAMPLE_LOAD without
SHADER_SAMPLE.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"
WINSYS = ROOT / "drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp"
WDK = ROOT / ".local/reference/wdk26100/d3d10umddi.h"

# DXGI cast families (typeless parent: members); read-only members marked *.
FAMILIES = {
    "R32G32B32A32_TYPELESS": ["R32G32B32A32_FLOAT", "R32G32B32A32_UINT", "R32G32B32A32_SINT"],
    "R16G16B16A16_TYPELESS": ["R16G16B16A16_FLOAT", "R16G16B16A16_UNORM", "R16G16B16A16_UINT",
                              "R16G16B16A16_SNORM", "R16G16B16A16_SINT"],
    "R32G8X24_TYPELESS": ["D32_FLOAT_S8X24_UINT", "*R32_FLOAT_X8X24_TYPELESS", "*X32_TYPELESS_G8X24_UINT"],
    "R10G10B10A2_TYPELESS": ["R10G10B10A2_UNORM", "R10G10B10A2_UINT"],
    "R8G8B8A8_TYPELESS": ["R8G8B8A8_UNORM", "R8G8B8A8_UNORM_SRGB", "R8G8B8A8_UINT",
                          "R8G8B8A8_SNORM", "R8G8B8A8_SINT"],
    "R32_TYPELESS": ["D32_FLOAT", "R32_FLOAT", "R32_UINT", "R32_SINT"],
    "R24G8_TYPELESS": ["D24_UNORM_S8_UINT", "*R24_UNORM_X8_TYPELESS", "*X24_TYPELESS_G8_UINT"],
    "R8G8_TYPELESS": ["R8G8_UNORM", "R8G8_UINT", "R8G8_SNORM", "R8G8_SINT"],
    "R16_TYPELESS": ["R16_FLOAT", "D16_UNORM", "R16_UNORM", "R16_UINT", "R16_SNORM", "R16_SINT"],
    "R8_TYPELESS": ["R8_UNORM", "R8_UINT", "R8_SNORM", "R8_SINT"],
    "B8G8R8A8_TYPELESS": ["B8G8R8A8_UNORM", "B8G8R8A8_UNORM_SRGB"],
    "B8G8R8X8_TYPELESS": ["B8G8R8X8_UNORM", "B8G8R8X8_UNORM_SRGB"],
}


def body_after(text, start_marker):
    start = text.index(start_marker)
    return text[start:text.index("'''", start)]


class MsaaCapsConsistency(unittest.TestCase):
    def test_reporting_obeys_the_cast_family_rule(self):
        if not WDK.exists():
            self.skipTest("pinned WDK headers are not present")
        text = SOURCE.read_text()
        caps = body_after(text, "   /* D3D-G10a probe (APPLE_AGX_D3D_MSAA_CAPS bit 0x40): the screen's")
        quality = body_after(text, "   (void)hDevice;\n   *pNumQualityLevels =")
        winsys = WINSYS.read_text()
        start = winsys.index("enum AgxD3d10CastRole {")
        end = winsys.index("VOID AgxD3d10WindowsMsaaReceipt(")
        policy = winsys[start:end]
        family_names = set(FAMILIES) | {m.lstrip("*") for v in FAMILIES.values() for m in v}
        names = sorted(set(re.findall(r"\bDXGI_FORMAT_\w+", caps + quality)) |
                       {"DXGI_FORMAT_" + n for n in family_names} |
                       {"DXGI_FORMAT_" + n for n in re.findall(r"[PTDR]\((\w+)", policy)})
        names = [n for n in names if n != "DXGI_FORMAT_UNKNOWN"]
        flags = dict(re.findall(r"#define (D3D10_DDI_FORMAT_SUPPORT_\w+)\s+(0x[0-9A-Fa-f]+)",
                                WDK.read_text(errors="replace")))
        program = ["#include <stdio.h>", "typedef unsigned UINT;", "typedef void *D3D10DDI_HDEVICE;",
                   "typedef int BOOL;", "#define TRUE 1", "#define FALSE 0", "#define VOID void",
                   "static UINT probe;",
                   "typedef int bool;", "#define true 1", "#define false 0",
                   "enum pipe_format { PIPE_FORMAT_NONE, PIPE_FORMAT_COLOUR, PIPE_FORMAT_INTEGER, PIPE_FORMAT_DEPTH };",
                   "enum { PIPE_TEXTURE_2D = 2, PIPE_BIND_RENDER_TARGET = 2, PIPE_BIND_SAMPLER_VIEW = 8 };",
                   "struct pipe_screen { bool (*is_format_supported)(struct pipe_screen *, enum pipe_format, unsigned, unsigned, unsigned, unsigned); };",
                   "struct pipe_context { struct pipe_screen *screen; };",
                   "static bool screen_supports(struct pipe_screen *s, enum pipe_format f, unsigned t, unsigned a, unsigned b, unsigned bind)"
                   " { (void)s; (void)t; (void)a; (void)b; (void)bind; return f != PIPE_FORMAT_NONE; }",
                   "static struct pipe_screen the_screen = { screen_supports };",
                   "static struct pipe_context the_pipe = { &the_screen };",
                   "static struct pipe_context *CastPipeContext(D3D10DDI_HDEVICE h) { (void)h; return &the_pipe; }",
                   "static bool util_format_is_pure_integer(enum pipe_format f) { return f == PIPE_FORMAT_INTEGER; }",
                   "static bool util_format_is_depth_or_stencil(enum pipe_format f) { return f == PIPE_FORMAT_DEPTH; }",
                   "static UINT AgxD3d10WindowsMsaaProbe(void) { return probe; }",
                   "static void AgxD3d10WindowsMsaaReceipt(UINT k, UINT f, UINT c, UINT a) "
                   "{ (void)k; (void)f; (void)c; (void)a; }"]
        program += ["#define %s %s" % item for item in flags.items()]
        program.append("typedef enum { DXGI_FORMAT_UNKNOWN = 0, %s, FORMAT_COUNT } DXGI_FORMAT;"
                       % ", ".join(names))
        program.append(policy)
        program.append("static enum pipe_format FormatTranslate(DXGI_FORMAT f, BOOL depth) { (void)depth;"
                       " return f == DXGI_FORMAT_UNKNOWN ? PIPE_FORMAT_NONE :"
                       " (f == DXGI_FORMAT_D32_FLOAT || f == DXGI_FORMAT_D16_UNORM ||"
                       " f == DXGI_FORMAT_D24_UNORM_S8_UINT || f == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ||"
                       " f == DXGI_FORMAT_R24_UNORM_X8_TYPELESS || f == DXGI_FORMAT_X24_TYPELESS_G8_UINT ||"
                       " f == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS || f == DXGI_FORMAT_X32_TYPELESS_G8X24_UINT) ?"
                       " PIPE_FORMAT_DEPTH : (f == DXGI_FORMAT_R8G8B8A8_UINT || f == DXGI_FORMAT_R16_UINT) ?"
                       " PIPE_FORMAT_INTEGER : PIPE_FORMAT_COLOUR; }")
        program.append("static enum pipe_format AgxD3d10LoweredTextureFormat(DXGI_FORMAT f) { (void)f; return PIPE_FORMAT_NONE; }")
        program.append("#define DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM_TEST 0")
        program.append("static void caps(D3D10DDI_HDEVICE hDevice, DXGI_FORMAT Format, UINT *pFormatCaps) {\n"
                       + caps + "\n}")
        program.append("static void quality(D3D10DDI_HDEVICE hDevice, DXGI_FORMAT Format, UINT SampleCount, "
                       "UINT *pNumQualityLevels) {\n" + quality + "\n}")
        rows = []
        for parent, members in FAMILIES.items():
            for member in members:
                rows.append("{DXGI_FORMAT_%s, DXGI_FORMAT_%s, %d}" % (member.lstrip("*"), parent,
                                                                     1 if member.startswith("*") else 0))
            rows.append("{DXGI_FORMAT_%s, DXGI_FORMAT_%s, 0}" % (parent, parent))
        program.append("static const struct { DXGI_FORMAT f, parent; int readonly; } family[] = {\n  "
                       + ",\n  ".join(rows) + "\n};")
        program.append(r'''static UINT levels(DXGI_FORMAT f, UINT n) { UINT q = 0; quality(0, f, n, &q); return q; }
int main(void) {
  int bad = 0, reported = 0;
  for (probe = 0; probe < 0x80; ++probe)
  for (int f = 0; f < FORMAT_COUNT; ++f) {
    UINT c = 0;
    caps(0, (DXGI_FORMAT)f, &c);
    int ms = (c & D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET) != 0;
    UINT q2 = levels((DXGI_FORMAT)f, 2), q4 = levels((DXGI_FORMAT)f, 4);
    reported += ms;
    if (ms && !q4) { printf("MS_WITHOUT_4X probe %x format %d\n", probe, f); ++bad; }
    if (!(probe & 0x30) && (ms || q2 || q4)) { printf("MS_WITH_POLICY_OFF probe %x format %d\n", probe, f); ++bad; }
    if ((c & D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_LOAD) && !(c & D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE)) {
      printf("LOAD_WITHOUT_SAMPLE %d caps %x\n", f, c); ++bad; }
    for (unsigned i = 0; i < sizeof(family) / sizeof(family[0]); ++i) {
      if (family[i].f != (DXGI_FORMAT)f) continue;
      for (UINT n = 2; n < 32; ++n) {
        UINT want = family[i].readonly ? 0 : levels(family[i].parent, n);
        if (levels((DXGI_FORMAT)f, n) != want) {
          printf("MEMBER_DIFFERS probe %x format %d count %u\n", probe, f, n); ++bad; }
      }
      if (family[i].readonly && ms) { printf("READONLY_MS probe %x format %d\n", probe, f); ++bad; }
      if (!family[i].readonly && (q2 || q4) && !ms) { printf("LEVELS_WITHOUT_MS probe %x format %d\n", probe, f); ++bad; }
    }
  }
  if (!reported) { printf("NO_PROBE_REPORTS\n"); ++bad; }
  printf("MSAA_CAPS %s\n", bad ? "BAD" : "CONSISTENT");
  return bad != 0;
}''')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "caps.c"
            src.write_text("\n".join(program))
            exe = Path(tmp) / "caps"
            built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-w", str(src),
                                    "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr[-2000:])
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertIn("MSAA_CAPS CONSISTENT", ran.stdout, ran.stdout[-3000:])


if __name__ == "__main__":
    unittest.main()
