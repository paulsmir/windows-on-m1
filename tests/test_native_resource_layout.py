"""C21: execute original/projected constructor + no-BO query with real AIL.

The BO stub records only native allocation requests. Modifier selection,
resource setup and AIL sizing are actual pinned/projected source, not mocks.
"""
import ast
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_dxbc_to_nir import ROOT, local_tree, host_flags

SCRIPT = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'


def function(text, name):
    marker = '\n' + name + '('
    start = text.index(marker) + 1
    opening = text.index('{', start)
    at, depth = opening + 1, 1
    while depth:
        depth += (text[at] == '{') - (text[at] == '}')
        at += 1
    return text[start:at]


def project(source):
    tree = ast.parse(SCRIPT.read_text())
    f = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'project_resource_layout')
    ns = {}
    exec(compile(ast.Module(body=[f], type_ignores=[]), str(SCRIPT), 'exec'), ns)
    return ns['project_resource_layout'](source)


class NativeResourceLayout(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        if not mesa or not build:
            raise unittest.SkipTest('pinned Mesa/host AIL unavailable')
        cls.original = (mesa / 'src/gallium/drivers/asahi/agx_pipe.c').read_text()
        cls.projected = project(cls.original)
        state = (mesa / 'src/gallium/drivers/asahi/agx_state.h').read_text()
        struct_start = state.index('struct agx_resource {')
        resource = state[struct_start:state.index('\n};', struct_start) + 3]
        declarations = []
        for name in ('agx_resource_setup', 'agx_linear_allowed', 'agx_twiddled_allowed',
                     'agx_compression_allowed', 'agx_select_best_modifier'):
            ret = 'void' if name == 'agx_resource_setup' else 'uint64_t' if name.startswith('agx_select') else 'bool'
            declarations.append('static ' + ret + '\n' + function(cls.original, name))
        declarations.append('static uint64_t\n' + function(cls.original, 'agx_select_modifier_from_list')
                            .replace('agx_select_modifier_from_list(', 'reference_select_modifier_from_list(', 1))
        declarations.append('static struct pipe_resource *\n' + function(cls.original,
            'agx_resource_create_with_modifiers').replace('agx_resource_create_with_modifiers(', 'reference_create(', 1)
            .replace('agx_select_modifier_from_list(', 'reference_select_modifier_from_list('))
        if '\nagx_select_uncompressed_modifier(' in cls.projected:
            declarations.append('static uint64_t\n' + function(cls.projected, 'agx_select_uncompressed_modifier'))
        declarations.append('static uint64_t\n' + function(cls.projected, 'agx_select_modifier_from_list'))
        declarations.append('static bool\n' + function(cls.projected, 'agx_resource_layout_init'))
        if '\nagx_resource_layout_query(' in cls.projected:
            declarations.append('static int\n' + function(cls.projected, 'agx_resource_layout_query'))
        declarations.append('static struct pipe_resource *\n' + function(cls.projected, 'agx_resource_create_with_modifiers'))
        declarations.append('int\n' + function(cls.projected, 'AgxWin32AsahiQueryResourceLayout'))
        declarations.append('static bool\n' + function(cls.original, 'agx_is_format_supported'))
        if '\nagx_uncompressed_query_format_valid(' in cls.projected:
            declarations.append('static bool\n' + function(cls.projected, 'agx_uncompressed_query_format_valid'))
        declarations.append('static int\n' + function(cls.projected, 'agx_query_uncompressed_resource_layout'))
        declarations.append('int\n' + function(cls.projected, 'AgxWin32AsahiQueryUncompressedResourceLayout'))
        fixture = (ROOT / 'tests/native_resource_layout_fixture.c').read_text()
        win32_bo = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_asahi_bo.c').read_text()
        bo_start = win32_bo.index('struct agx_bo *agx_bo_create(')
        shape_start = win32_bo.index('#ifdef APPLE_AGX_GPUVA_WINSYS\n  if(!b->GpuvaReady', bo_start)
        shape_end = win32_bo.index('  cls=', shape_start)
        bo_shape = win32_bo[shape_start:shape_end].replace('return NULL;', 'return 0;')
        fixture = fixture.replace('/* ACTUAL_BO_SHAPE */', bo_shape)
        cls.tmp = tempfile.TemporaryDirectory(prefix='asahi-layout-query-')
        cls.exe = Path(cls.tmp.name) / 'layout'
        source = Path(cls.tmp.name) / 'layout.c'
        notices = cls.original[:cls.original.index('*/')+2] + '\n' + state[:state.index('*/')+2]
        source.write_text(notices + '\n' + fixture.replace('/* ACTUAL_RESOURCE_STRUCT */', resource)
                          .replace('/* ACTUAL_SOURCE */', '\n\n'.join(declarations)))
        libs = [build / p for p in ('src/asahi/layout/libasahi_layout.a',
            'src/util/libmesa_util.a', 'src/util/libmesa_util_simd.a',
            'src/c11/impl/libmesa_util_c11.a', 'src/util/blake3/libblake3.a')]
        r = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-fsanitize=address,undefined', '-DAPPLE_AGX_GPUVA_WINSYS',
            *host_flags(build), '-I', str(mesa / 'src/gallium/include'), '-I', str(mesa / 'src/gallium/auxiliary'),
            '-I', str(ROOT / 'drivers/apple-agx/mesa/winsys'), str(source), '-o', str(cls.exe),
            *map(str, libs), '-L/opt/homebrew/opt/zstd/lib', '-lzstd', '-lz', '-lpthread'],
            capture_output=True, text=True)
        if r.returncode:
            raise AssertionError(r.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_query_matches_original_and_projected_native_layout_without_bo(self):
        r = subprocess.run([str(self.exe)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('query-bo=0', r.stdout)
        print(r.stdout.strip())

    def test_pure_query_refuses_invalid_formats_without_assert_or_device(self):
        r=subprocess.run([str(self.exe),'--pure','--invalid'],capture_output=True,text=True)
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        self.assertIn('invalid-format rejection NONE/RGB96/A8/depth/range/unsupportedRT',r.stdout)

    def test_projection_rejects_changed_prefix(self):
        changed = self.original.replace('templ->depth0 == 1', 'templ->depth0 == 2', 1)
        with self.assertRaises(ValueError):
            project(changed)

    def test_screen_free_explicit_modifier_query_matches_native_without_device_access(self):
        r = subprocess.run([str(self.exe), 'explicit'], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('screen-free=19', r.stdout)
        print(r.stdout.strip())

    def test_projection_rejects_setup_that_starts_using_device(self):
        changed = self.original.replace('   struct pipe_resource *templ = &nresource->base;',
            '   struct pipe_resource *templ = dev ? &nresource->base : NULL;', 1)
        with self.assertRaises(ValueError):
            project(changed)
