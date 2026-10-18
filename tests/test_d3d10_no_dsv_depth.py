"""EXP994: with no depth-stencil view bound, D3D10/11 depth and stencil tests
are disabled regardless of the bound depth-stencil state.

On-device self-test (tools/agx-render-selftest, EXP993): every draw produced no
pixels until the app bound an explicit DepthEnable=FALSE state; the runtime's
default state (DepthEnable=TRUE, LESS) reached the AGX ISP with no depth buffer
and background depth 0.0, so every fragment failed the depth test. DWM composes
without a depth buffer, hence the empty desktop. The d3d10umd frontend must
bind a depth/stencil-disabled state while no DSV is bound and restore the
application's state once one is bound.
"""
import ast
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'


def constant(marker):
    values = [n.value for n in ast.walk(ast.parse(SCRIPT.read_text()))
              if isinstance(n, ast.Constant) and isinstance(n.value, str)
              and marker in n.value]
    assert len(values) == 1, (marker, len(values))
    return values[0]


SHIM = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#define APIENTRY
#define LOG_ENTRYPOINT() ((void)0)
struct pipe_resource { int id; };
struct pipe_surface { pipe_resource *texture; };
struct pipe_framebuffer_state { pipe_surface zsbuf; };
struct pipe_stencil_ref { unsigned char ref_value[2]; };
struct pipe_stencil_state { unsigned enabled; };
struct pipe_depth_stencil_alpha_state { unsigned depth_enabled, depth_writemask; pipe_stencil_state stencil[2]; };
struct pipe_context {
  void *(*create_depth_stencil_alpha_state)(pipe_context*, const pipe_depth_stencil_alpha_state*);
  void (*bind_depth_stencil_alpha_state)(pipe_context*, void*);
  void (*set_stencil_ref)(pipe_context*, pipe_stencil_ref);
  void *bound; pipe_stencil_ref ref;
};
struct Device { pipe_context *pipe; pipe_framebuffer_state fb; void *app_dsa; void *no_depth_dsa; };
struct D3D10DDI_HDEVICE { Device *pDrvPrivate; };
struct D3D10DDI_HDEPTHSTENCILSTATE { void *pDrvPrivate; };
static Device *CastDevice(D3D10DDI_HDEVICE h) { return h.pDrvPrivate; }
static pipe_context *CastPipeContext(D3D10DDI_HDEVICE h) { return h.pDrvPrivate->pipe; }
static void *CastPipeDepthStencilState(D3D10DDI_HDEPTHSTENCILSTATE h) { return h.pDrvPrivate; }
static pipe_depth_stencil_alpha_state created;
static int creates;
static void *create(pipe_context*, const pipe_depth_stencil_alpha_state *s) { created = *s; ++creates; return &created; }
static void bind(pipe_context *p, void *s) { p->bound = s; }
static void ref(pipe_context *p, pipe_stencil_ref r) { p->ref = r; }
@@HELPER@@
void APIENTRY SetDepthStencilState(D3D10DDI_HDEVICE hDevice, D3D10DDI_HDEPTHSTENCILSTATE hState, unsigned StencilRef)
{
@@BODY@@
}
int main() {
  pipe_context pipe = {}; pipe.create_depth_stencil_alpha_state = create;
  pipe.bind_depth_stencil_alpha_state = bind; pipe.set_stencil_ref = ref;
  Device dev = {}; dev.pipe = &pipe;
  int app_state = 0; pipe_resource depth = {1};
  D3D10DDI_HDEVICE h = {&dev}; D3D10DDI_HDEPTHSTENCILSTATE s = {&app_state};
  /* Runtime default state, no DSV: depth/stencil must be off. */
  SetDepthStencilState(h, s, 7);
  if (pipe.bound != &created || created.depth_enabled || created.depth_writemask ||
      created.stencil[0].enabled || created.stencil[1].enabled) { puts("app depth state reached ISP without a DSV"); return 1; }
  assert(pipe.ref.ref_value[0] == 7);
  /* A DSV is bound: the application's state applies. */
  dev.fb.zsbuf.texture = &depth; AgxD3d10ApplyDepthStencil(&dev);
  assert(pipe.bound == &app_state);
  /* DSV unbound again: disabled state, created once. */
  dev.fb.zsbuf.texture = nullptr; AgxD3d10ApplyDepthStencil(&dev);
  assert(pipe.bound == &created && creates == 1);
  /* State change while a DSV is bound goes straight through. */
  int other = 0; D3D10DDI_HDEPTHSTENCILSTATE s2 = {&other}; dev.fb.zsbuf.texture = &depth;
  SetDepthStencilState(h, s2, 1); assert(pipe.bound == &other);
  puts("EXP994 no-DSV depth: PASS");
}
'''


class NoDsvDepth(unittest.TestCase):
    def test_depth_stencil_disabled_without_dsv(self):
        helper = constant('AgxD3d10ApplyDepthStencil(Device *pDevice)')
        body = constant('pDevice->app_dsa = state;')
        helper = helper[:helper.index('void APIENTRY')]
        src = SHIM.replace('@@HELPER@@', helper).replace('@@BODY@@', body)
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp) / 't.cpp'; exe = Path(tmp) / 't'
            c.write_text(src)
            built = subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined',
                                    str(c), '-o', str(exe)], text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP994 no-DSV depth: PASS', ran.stdout)

    def test_set_render_targets_reapplies_depth_state(self):
        tail = constant('   pipe->set_framebuffer_state(pipe, &pDevice->fb);\n   AgxD3d10ApplyDepthStencil(pDevice);')
        self.assertIn('AgxD3d10ApplyDepthStencil(pDevice);', tail)


if __name__ == '__main__':
    unittest.main()
