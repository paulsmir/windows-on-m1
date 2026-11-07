"""RC75: D3D12 root descriptors reach the shader compile as unchecked slots.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
D3D12 root CBVs/SRVs/UAVs carry no size (Resource Binding spec, "Using
Descriptors Directly in the Root Arguments"), while descriptor-table views
keep D3D11's out-of-bounds rules. The owner marks a stage's slots unchecked
from the pipeline's root signature (DDI RootViews -> owner mark_root_views
-> dxbc_nir_mark_root_views). The real functions run here on the host with
stand-ins for the Windows and NIR calls:
- only CBV/UAV slots whose (space, register) is a root CBV/UAV of the stage
  are marked: a table view (same register in another space, a heap range, a
  root-constant buffer) keeps its checks, or an application's
  out-of-bounds read of a table view would read memory instead of 0;
- compute sees every root parameter, a graphics stage only ALL or its own
  visibility (a pixel-only root CBV is not one of the vertex shader's);
- identity (SM5.0) bindings use the register of space 0 as the slot;
- APPLE_AGX_D3D12_ROOT_BOUNDS=1 keeps every check and still receipts;
- the receipt names the stage, the shader and the masks;
- the pipeline's root views are what the DDI hands to the compute compile,
  its min/max variants and the graphics description.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
HEADER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.h'
DDI = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
DXBC = ROOT / 'drivers/apple-agx/mesa/dxbc'


def function(text, marker):
    start = text.index(marker)
    return text[start:text.index('\n}', start) + 2]


def struct(text, marker):
    start = text.index(marker)
    return text[start:text.index('\n};', start) + 3]


PRELUDE = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
typedef unsigned UINT;
typedef int32_t HRESULT;
typedef unsigned long DWORD;
#define S_OK 0
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#include "dxbc_bindings.h"
enum mesa_shader_stage { MESA_SHADER_VERTEX, MESA_SHADER_TESS_CTRL, MESA_SHADER_TESS_EVAL, MESA_SHADER_GEOMETRY,
                         MESA_SHADER_FRAGMENT, MESA_SHADER_COMPUTE, MESA_SHADER_KERNEL };
enum { D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
       D3D12DDI_ROOT_PARAMETER_TYPE_CBV, D3D12DDI_ROOT_PARAMETER_TYPE_SRV, D3D12DDI_ROOT_PARAMETER_TYPE_UAV };
enum { D3D12DDI_SHADER_VISIBILITY_ALL, D3D12DDI_SHADER_VISIBILITY_VERTEX, D3D12DDI_SHADER_VISIBILITY_HULL,
       D3D12DDI_SHADER_VISIBILITY_DOMAIN, D3D12DDI_SHADER_VISIBILITY_GEOMETRY, D3D12DDI_SHADER_VISIBILITY_PIXEL };
struct nir_shader { int dummy; };
struct AgxCpuRoot { int dummy; };
static const char *g_env;
static DWORD GetEnvironmentVariableA(const char *name, char *out, DWORD size) {
  if (!g_env || strcmp(name, "APPLE_AGX_D3D12_ROOT_BOUNDS")) return 0;
  const DWORD n = (DWORD)strlen(g_env);
  if (n + 1 > size) return n + 1;
  memcpy(out, g_env, n + 1);
  return n;
}
static std::vector<std::vector<UINT>> g_receipts;
static void AdmissionUmdDiagnostic(const char *stage, HRESULT, const UINT *v, unsigned n) {
  if (!strcmp(stage, "measure-d3d12-root-views")) g_receipts.emplace_back(v, v + n);
}
struct ShaderStatsTag { unsigned long long Hash; UINT Bytes, Block, Shared; };
static ShaderStatsTag shader_stats_tag(const UINT *tokens) {
  ShaderStatsTag t = {};
  t.Hash = tokens ? 0x1234abcd00000000ull | tokens[0] : 0;
  return t;
}
static int g_marks;
static uint32_t g_cbv, g_uav;
static bool dxbc_nir_mark_root_views(struct nir_shader *, uint32_t cbv, uint32_t uav) {
  ++g_marks; g_cbv = cbv; g_uav = uav;
  return true;
}
'''

MAIN = r'''
static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
struct Signature { AgxD3d12RootSignature head; AgxD3d12RootParameter more[7]; };
static void param(AgxD3d12RootParameter *p, UINT type, UINT visibility, UINT reg, UINT space) {
  memset(p, 0, sizeof(*p));
  p->type = type; p->visibility = visibility; p->shader_register = reg; p->space = space;
}
static void bind(struct dxbc_binding_map *m, uint32_t kind, uint32_t space, uint32_t reg, uint32_t slot, uint32_t flags = 0) {
  m->bindings[m->count++] = {kind, m->count, space, reg, slot, flags};
}
int main() {
  Signature s = {};
  AgxD3d12RootParameter *p = s.head.parameter;
  param(&p[0], D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, D3D12DDI_SHADER_VISIBILITY_ALL, 0, 0);
  param(&p[1], D3D12DDI_ROOT_PARAMETER_TYPE_UAV, D3D12DDI_SHADER_VISIBILITY_ALL, 1, 0);
  param(&p[2], D3D12DDI_ROOT_PARAMETER_TYPE_CBV, D3D12DDI_SHADER_VISIBILITY_PIXEL, 0, 0);
  param(&p[3], D3D12DDI_ROOT_PARAMETER_TYPE_UAV, D3D12DDI_SHADER_VISIBILITY_VERTEX, 3, 1);
  param(&p[4], D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, D3D12DDI_SHADER_VISIBILITY_ALL, 2, 0);
  param(&p[5], D3D12DDI_ROOT_PARAMETER_TYPE_SRV, D3D12DDI_SHADER_VISIBILITY_ALL, 4, 0);
  s.head.parameters = 6;
  AGX_D3D12_ROOT_VIEWS views;
  RootViews(&s.head, &views);
  CHECK(views.Count == 3); /* the table, root constants and the root SRV are not root views */
  struct dxbc_binding_map map = {};
  map.active = 1;
  bind(&map, DXBC_BINDING_UAV, 0, 1, 0);                    /* root UAV u1 */
  bind(&map, DXBC_BINDING_UAV, 0, 2, 1);                    /* table UAV */
  bind(&map, DXBC_BINDING_UAV, 1, 3, 2);                    /* root UAV u3 space1, vertex-only */
  bind(&map, DXBC_BINDING_UAV, 1, 1, 3);                    /* u1 of another space: a table view */
  bind(&map, DXBC_BINDING_CBV, 0, 0, 0);                    /* root CBV b0, pixel-only */
  bind(&map, DXBC_BINDING_CBV, 0, 2, 1);                    /* root constants b2 */
  bind(&map, DXBC_BINDING_UAV, 0, 1, 4, DXBC_BINDING_HEAP); /* a heap range at u1: a table */
  bind(&map, DXBC_BINDING_SRV, 0, 4, 0);                    /* root SRV: a texel buffer */
  const UINT tokens[2] = {0x77, 2};
  nir_shader nir = {};
  /* no root signature in scope: nothing marked, no receipt */
  mark_root_views(&nir, MESA_SHADER_COMPUTE, &map, tokens);
  CHECK(g_marks == 0 && g_receipts.empty());
  {
    RootViewsScope roots(&views);
    mark_root_views(&nir, MESA_SHADER_COMPUTE, &map, tokens);
    CHECK(g_marks == 1 && g_uav == 0x5u && g_cbv == 0x1u);
    CHECK(g_receipts.size() == 1);
    const std::vector<UINT> want = {MESA_SHADER_COMPUTE, 0x77u, 0x1u, 0x5u, 0x3u, 0xfu, 1u};
    CHECK(g_receipts.back() == want);
    mark_root_views(&nir, MESA_SHADER_VERTEX, &map, tokens);
    CHECK(g_uav == 0x5u && g_cbv == 0u);
    mark_root_views(&nir, MESA_SHADER_FRAGMENT, &map, tokens);
    CHECK(g_uav == 0x1u && g_cbv == 0x1u);
    const int marks = g_marks;
    mark_root_views(&nir, MESA_SHADER_GEOMETRY, &map, tokens);
    CHECK(g_marks == marks + 1 && g_uav == 0x1u && g_cbv == 0u);
    /* SM5.0 identity bindings: the register of space 0 is the slot */
    struct dxbc_binding_map identity = {};
    mark_root_views(&nir, MESA_SHADER_COMPUTE, &identity, tokens);
    CHECK(g_uav == 0x2u && g_cbv == 0x1u);
    /* the opt-out keeps every check and still receipts */
    g_env = "1";
    const int before = g_marks;
    mark_root_views(&nir, MESA_SHADER_COMPUTE, &map, tokens);
    CHECK(g_marks == before && g_receipts.back()[6] == 2u && g_receipts.back()[3] == 0x5u);
    g_env = NULL;
  }
  /* the scope is restored */
  const size_t receipts = g_receipts.size();
  mark_root_views(&nir, MESA_SHADER_COMPUTE, &map, tokens);
  CHECK(g_receipts.size() == receipts);
  if (!failures) printf("ROOT_VIEWS_OK\n");
  return failures != 0;
}
'''


class RootViews(unittest.TestCase):
    def test_root_descriptor_slots_are_marked_per_stage(self):
        owner, header, ddi = OWNER.read_text(), HEADER.read_text(), DDI.read_text()
        views = re.search(r'#define AGX_D3D12_ROOT_VIEWS_MAX \d+\ntypedef struct AGX_D3D12_ROOT_VIEWS \{.*?\} AGX_D3D12_ROOT_VIEWS;',
                          header, re.S).group()
        stages = re.search(r'enum \{ AGX_D3D12_HEAP_VS.*?\};', header, re.S).group()
        slots = '\n'.join(re.search(r'#define %s \d+' % n, header).group()
                          for n in ('AGX_D3D12_CBV_SLOTS', 'AGX_D3D12_UAV_SLOTS'))
        start = owner.index('static thread_local const AGX_D3D12_ROOT_VIEWS *t_root_views;')
        end = owner.index('\n}\n', owner.index('static void mark_root_views(')) + 3
        source = '\n'.join([PRELUDE, slots, stages, views,
                            struct(ddi, 'struct AgxD3d12RootParameter {'),
                            struct(ddi, 'struct AgxD3d12RootSignature {'),
                            function(ddi, 'static void RootViews('),
                            owner[start:end], MAIN])
        with tempfile.TemporaryDirectory(prefix='root-views-') as tmp:
            path = Path(tmp) / 'root_views.cpp'
            path.write_text(source)
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Werror', '-I', str(DXBC), str(path),
                                    '-o', str(Path(tmp) / 'root_views')], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr[-4000:])
            run = subprocess.run([str(Path(tmp) / 'root_views')], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('ROOT_VIEWS_OK', run.stdout)

    def test_the_pipeline_root_views_reach_every_compile(self):
        ddi, owner = DDI.read_text(), OWNER.read_text()
        compute = function(ddi, 'static HRESULT CompileCompute(')
        self.assertIn('RootViews(root, &pso->root_views);', compute)
        self.assertIn('AgxD3d12OwnerCreateComputeRoot(owner, pso->cs, 0u, NULL, &pso->root_views, &pso->compute)', compute)
        graphics = function(ddi, 'static HRESULT CompileGraphics(')
        self.assertLess(graphics.index('memset(&desc, 0, sizeof(desc));'), graphics.index('desc.Root = &pso->root_views;'))
        variant = function(ddi, 'static void *MinmaxVariant(')
        self.assertIn('AgxD3d12OwnerCreateComputeRoot(owner, pso->cs, flags, key.slot[AGX_D3D12_HEAP_CS], &pso->root_views,', variant)
        self.assertIn('AGX_D3D12_GRAPHICS_DESC desc = pso->owner_desc;', variant)
        self.assertIn('CompileCompute(list->device, pso, root)', function(ddi, 'static void RecordDispatch('))
        self.assertIn('CompileGraphics(list->device, pso, list->graphics_root)', ddi)
        self.assertIn('RootViewsScope roots(root);', function(owner, 'extern "C" HRESULT AgxD3d12OwnerCreateComputeRoot('))
        self.assertIn('RootViewsScope roots(desc->Root);', function(owner, 'extern "C" HRESULT AgxD3d12OwnerCreateGraphics('))
        flags = function(owner, 'extern "C" HRESULT AgxD3d12OwnerCreateComputeFlags(')
        self.assertLess(flags.index('translate_shader('), flags.index('mark_root_views(nir, MESA_SHADER_COMPUTE, &compute->Bindings, tokens);'))
        self.assertLess(flags.index('mark_root_views('), flags.index('create_compute_state('))
        shader = function(owner, 'static void *create_shader(')
        self.assertLess(shader.index('translate_shader('), shader.index('mark_root_views(nir, stage, bindings, tokens);'))


if __name__ == '__main__':
    unittest.main()
