"""EXP1285: the D3D10 frontend's format caps and quality levels agree.

Bug caught: D3D-G5 reported 2x/4x quality levels for depth formats whose
CheckFormatSupport answer had no MULTISAMPLE_RENDERTARGET bit, and
MULTISAMPLE_LOAD for formats the frontend does not sample. With that package
the runtime refused every D3D device (D3D11CreateDevice 0x887A0020
DXGI_ERROR_DRIVER_INTERNAL_ERROR before the first resource) and DWM failed
in dwmcore. For every format: quality levels at 2 or 4 samples exist exactly
when MULTISAMPLE_RENDERTARGET is reported, MULTISAMPLE_RENDERTARGET needs
4x, and MULTISAMPLE_LOAD needs SHADER_SAMPLE.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"
WDK = ROOT / ".local/reference/wdk26100/d3d10umddi.h"


def replacement_after(text, marker):
    """The replacement body that follows an anchor tuple's original text."""
    start = text.index(marker)
    start = text.index("'''", text.index("''','''", start) + 4)
    start = text.rindex("'''", 0, start + 3) + 3
    end = text.index("'''", start)
    return text[start:end]


class MsaaCapsConsistency(unittest.TestCase):
    def test_quality_levels_match_the_multisample_bits(self):
        if not WDK.exists():
            self.skipTest("pinned WDK headers are not present")
        text = SOURCE.read_text()
        caps_start = text.index("   (void)hDevice;\n   if (Format == DXGI_FORMAT_B8G8R8A8_UNORM ||")
        caps = text[caps_start:text.index("'''", caps_start)]
        quality_start = text.index("   (void)hDevice;\n   *pNumQualityLevels =")
        quality = text[quality_start:text.index("'''", quality_start)]
        names = sorted(set(re.findall(r"\bDXGI_FORMAT_\w+", caps + quality)))
        flags = dict(re.findall(r"#define (D3D10_DDI_FORMAT_SUPPORT_\w+)\s+(0x[0-9A-Fa-f]+)",
                                WDK.read_text(errors="replace")))
        program = ["#include <stdio.h>", "typedef unsigned UINT;", "typedef void *D3D10DDI_HDEVICE;",
                   "static int diagnostic_on;",
                   "static int AgxD3d10WindowsDiagnosticSwitch(const char *name) { (void)name; return diagnostic_on; }"]
        program += ["#define %s %s" % item for item in flags.items()]
        program.append("typedef enum { DXGI_FORMAT_UNKNOWN_TEST = 0, %s, FORMAT_COUNT } DXGI_FORMAT;"
                       % ", ".join(names))
        program.append("static void caps(D3D10DDI_HDEVICE hDevice, DXGI_FORMAT Format, UINT *pFormatCaps) {\n"
                       + caps + "\n}")
        program.append("static void quality(D3D10DDI_HDEVICE hDevice, DXGI_FORMAT Format, UINT SampleCount, "
                       "UINT *pNumQualityLevels) {\n" + quality + "\n}")
        program.append(r'''int main(void) {
  int bad = 0;
  for (diagnostic_on = 0; diagnostic_on < 2; ++diagnostic_on)
  for (int f = 0; f <= FORMAT_COUNT + 2; ++f) {
    UINT c = 0, q2 = 0, q4 = 0;
    caps(0, (DXGI_FORMAT)f, &c);
    quality(0, (DXGI_FORMAT)f, 2, &q2);
    quality(0, (DXGI_FORMAT)f, 4, &q4);
    int ms = (c & D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET) != 0;
    if (ms != (q2 || q4)) { printf("MS_MISMATCH %d caps %x q2 %u q4 %u\n", f, c, q2, q4); ++bad; }
    if (ms && !q4) { printf("MS_WITHOUT_4X %d\n", f); ++bad; }
    if ((c & D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_LOAD) && !(c & D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE)) {
      printf("LOAD_WITHOUT_SAMPLE %d caps %x\n", f, c); ++bad; }
  }
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
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertIn("MSAA_CAPS CONSISTENT", ran.stdout, ran.stdout)


if __name__ == "__main__":
    unittest.main()
