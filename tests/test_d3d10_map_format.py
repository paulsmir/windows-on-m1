"""D3D-G10: CPU Map of a texture admits every format the frontend translates.

EXP1290: Map of an R32G32B32A32_UINT staging texture returned E_INVALIDARG
(ResourceMap reject-seterror) because the map gate kept the bring-up list of
ten colour formats and the byte copy families; the runtime removed the
device and every later format-smoke step failed.
Invariant: a texture format is mappable exactly when FormatTranslate gives
it a pipe format (the resource then has a texel layout; a format the
resource stores differently goes through the client map).
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


def helper():
    text = SCRIPT.read_text()
    start = text.index("static bool\nAgxD3d10MapFormat(DXGI_FORMAT format)")
    end = text.index("\n}\n", start) + 3
    return text[start:end]


PROGRAM = r'''
#include <assert.h>
#include <stdbool.h>
typedef enum { DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R16G16_SNORM,
  DXGI_FORMAT_R8_SINT, DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT,
  DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_UNTRANSLATED } DXGI_FORMAT;
enum pipe_format { PIPE_FORMAT_NONE, PIPE_FORMAT_ANY };
static enum pipe_format FormatTranslate(DXGI_FORMAT format, bool depth) {
  (void)depth;
  return format == DXGI_FORMAT_UNKNOWN || format == DXGI_FORMAT_UNTRANSLATED ?
      PIPE_FORMAT_NONE : PIPE_FORMAT_ANY;
}
HELPER
int main(void) {
  for (int f = DXGI_FORMAT_R32G32B32A32_UINT; f <= DXGI_FORMAT_B8G8R8A8_UNORM; ++f)
    assert(AgxD3d10MapFormat((DXGI_FORMAT)f));
  assert(!AgxD3d10MapFormat(DXGI_FORMAT_UNKNOWN));
  assert(!AgxD3d10MapFormat(DXGI_FORMAT_UNTRANSLATED));
  return 0;
}
'''


class D3d10MapFormat(unittest.TestCase):
    def test_every_translated_format_maps(self):
        with tempfile.TemporaryDirectory(prefix="d3d10-map-format-") as tmp:
            source = Path(tmp) / "map.c"
            source.write_text(PROGRAM.replace("HELPER", helper()))
            binary = Path(tmp) / "map"
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                            "-Werror", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
