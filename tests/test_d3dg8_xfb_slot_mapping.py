"""D3D-G8: stream output names the NIR slot tgsi_to_nir gives each output.

Bug caught: the Windows projection rebuilds NIR xfb info from the D3D10
frontend's pipe_stream_output entries; each entry names a TGSI output
register whose semantic must map to the same VARYING_SLOT tgsi_to_nir gave
that register's variable, or stream output captures another varying (or
none). The test compiles the projection's agx_windows_xfb_slot and the
pinned Mesa tgsi_to_nir mapping side by side against the real Mesa headers
and compares every semantic/index pair the projection accepts.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"
MESA = ROOT / ".local/reference/mesa"


def function(text, name):
    start = text.index(name + "(")
    start = text.rindex("\n", 0, text.rindex("\n", 0, start)) + 1
    depth, i = 0, text.index("{", start)
    while True:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
        if depth == 0:
            return text[start:i]


class XfbSlotMapping(unittest.TestCase):
    def test_projection_matches_tgsi_to_nir(self):
        ttn = MESA / "src/gallium/auxiliary/nir/tgsi_to_nir.c"
        if not ttn.exists():
            self.skipTest("pinned Mesa reference is not present")
        projection = SOURCE.read_text()
        helper = function(projection, "agx_windows_xfb_slot")
        upstream = function(ttn.read_text(), "tgsi_varying_semantic_to_slot")
        program = ("#include <stdbool.h>\n#include <stdio.h>\n#include <stdlib.h>\n"
                   "#include \"pipe/p_shader_tokens.h\"\n#include \"compiler/shader_enums.h\"\n" +
                   upstream + "\n" + helper + "\n" +
                   "int main(void) { int checked = 0;\n"
                   "  for (unsigned name = 0; name < TGSI_SEMANTIC_COUNT; ++name)\n"
                   "    for (unsigned index = 0; index < 32; ++index) {\n"
                   "      unsigned slot;\n"
                   "      if (!agx_windows_xfb_slot(name, index, &slot)) continue;\n"
                   "      if (slot != (unsigned)tgsi_varying_semantic_to_slot(name, index)) {\n"
                   "        printf(\"MISMATCH %u/%u\\n\", name, index); return 1; }\n"
                   "      ++checked; }\n"
                   "  printf(\"XFB_SLOTS_MATCH %d\\n\", checked); return checked ? 0 : 1; }\n")
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "slots.c"
            src.write_text(program)
            exe = Path(tmp) / "slots"
            built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-w",
                                    "-I", str(MESA / "src"), "-I", str(MESA / "include"),
                                    "-I", str(MESA / "src/gallium/include"),
                                    str(src), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr[-2000:])
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout)
            self.assertIn("XFB_SLOTS_MATCH", ran.stdout)


if __name__ == "__main__":
    unittest.main()
