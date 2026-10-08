"""EXP1065: a NULL input layout binds empty vertex elements.

EXP1065: Notepad and the glyph-atlas probe crashed (c0000005) at
AppleAgxRenderAdmissionUmd.dll +0x3e24c = agx_update_vs+0x1fc, which copies
ctx->attributes->key. D3D allows draws with no input layout (SV_VertexID
only); d3d10umd's update_velems then bound nothing, so Asahi read a NULL
vertex-elements state (or a stale one). Invariants:
- the first draw binds vertex elements even if the app never set a layout;
- a NULL layout binds an empty (zero-element) state;
- a real layout gets its strides and is bound; unchanged state is not rebound.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'

PROGRAM = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
struct pipe_vertex_element { unsigned vertex_buffer_index, src_stride; };
struct cso_velems_state { unsigned count; struct pipe_vertex_element velems[4]; };
typedef struct { struct cso_velems_state state; } ElementLayout;
typedef struct { ElementLayout *element_layout; BOOL velems_changed; BOOL agx_velems_bound;
  unsigned vertex_strides[4]; void *cso; } Device;
static int binds; static unsigned last_count = 99, last_stride;
static void cso_set_vertex_elements(void *cso, const struct cso_velems_state *s) {
  (void)cso; ++binds; last_count = s->count; last_stride = s->count ? s->velems[0].src_stride : 0; }
static void update_velems(Device *pDevice) {
@@BODY@@
}
int main(void) {
  Device d; memset(&d, 0, sizeof d);
  update_velems(&d);   /* never set: the first draw still binds */
  assert(binds == 1 && last_count == 0);
  update_velems(&d);   /* unchanged: no rebind */
  assert(binds == 1);
  ElementLayout layout; memset(&layout, 0, sizeof layout);
  layout.state.count = 1; layout.state.velems[0].vertex_buffer_index = 2;
  d.vertex_strides[2] = 24; d.element_layout = &layout; d.velems_changed = TRUE;
  update_velems(&d);
  assert(binds == 2 && last_count == 1 && last_stride == 24);
  d.element_layout = NULL; d.velems_changed = TRUE;   /* IaSetInputLayout(NULL) */
  update_velems(&d);
  assert(binds == 3 && last_count == 0);
  puts("PASS");
  return 0;
}
'''


class NullInputLayout(unittest.TestCase):
    def test_null_layout_binds_empty_vertex_elements(self):
        text = SRC.read_text()
        marker = "replace_function_body('src/gallium/frontends/d3d10umd/Draw.cpp','update_velems','''"
        self.assertIn(marker, text, 'update_velems is not projected')
        start = text.index(marker) + len(marker)
        body = text[start:text.index("''')", start)]
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'velems.c'; exe = Path(tmp) / 'velems'
            src.write_text(PROGRAM.replace('@@BODY@@', body))
            built = subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_bound_flag_is_a_device_field(self):
        self.assertIn('BOOL agx_velems_bound;', SRC.read_text())


if __name__ == '__main__':
    unittest.main()
