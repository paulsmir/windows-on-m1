"""Execute production SRV creation -> replay -> Gallium view construction.

C18 catches the real dropped Shader4ComponentMapping field, including typed
buffers, descriptor copies and descriptor-less root identity. Host callbacks
capture the view actually passed to create_sampler_view; no duplicate decoder.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_dxbc_to_nir import local_tree, host_flags

ROOT = Path(__file__).resolve().parents[1]
DDI = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'


def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth = 1
    at = opening + 1
    while depth:
        depth += (text[at] == '{') - (text[at] == '}')
        at += 1
    return text[start:at]


class ViewMapping(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        if not build:
            raise unittest.SkipTest('host Mesa headers not available')
        ddi, owner = DDI.read_text(), OWNER.read_text()
        replay = block(ddi[ddi.index('static void OwnerBindings('):], 'for (UINT s = 0; s < kSrvSlots; ++s)')
        code = block(ddi, 'struct AgxD3d12Descriptor {') + ';\n'
        code += block(ddi, 'static VOID APIENTRY CreateShaderResourceView(') + '\n'
        code += 'static void Replay(const Bindings &b, OwnerBindings *o) {\n' + replay + '\n}\n'
        if 'static bool srv_component_mapping(' in owner:
            code += block(owner, 'static bool srv_component_mapping(') + '\n'
        code += block(owner, 'static HRESULT bind_stage(') + '\n'
        metadata_arg = ', shader_dimensions' if 'const uint8_t *dimensions' in code else ''
        mesa_format = (local_tree('reference', 'mesa') / 'src/util/format/u_format.c').read_text()
        code += block(mesa_format, 'void util_format_compose_swizzles(') + '\n'
        code += block(mesa_format, 'void util_format_apply_color_swizzle(') + '\n'
        header = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.h').read_text()
        end = header.index('} AGX_D3D12_SRV;') + len('} AGX_D3D12_SRV;')
        owner_srv = header[header.rfind('typedef struct {', 0, end):end]
        fixture = (ROOT / 'tests/d3d12_view_mapping_fixture.cpp').read_text()
        cls.tmp = tempfile.TemporaryDirectory(prefix='d3d12-view-mapping-')
        source = Path(cls.tmp.name) / 'mapping.cpp'
        source.write_text(fixture.replace('/* PRODUCTION_VIEW_MAPPING */', code).replace('/* PRODUCTION_OWNER_SRV */', owner_srv).replace('/* DIMENSIONS_ARGUMENT */', metadata_arg))
        cls.exe = Path(cls.tmp.name) / 'mapping'
        run = subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17',
            '-fsanitize=address,undefined', '-DAPPLE_AGX_D3D12_NATIVE_OWNER',
            *host_flags(build), '-I', str(local_tree('reference', 'mesa') / 'src/gallium/include'), '-I', str(ROOT / 'drivers/apple-agx/mesa/winsys'), '-I', str(ROOT / 'drivers/apple-agx/mesa/dxbc'),
            str(source), '-o', str(cls.exe)], capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(run.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_case(self, mode):
        run = subprocess.run([str(self.exe), mode], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_texture_and_typed_buffer_mapping_survives_creation_copy_replay(self):
        self.run_case('mapping')

    def test_invalid_and_unsupported_raw_structured_maps_refuse(self):
        self.run_case('refusal')

    def test_root_srv_default_is_explicit_identity(self):
        self.run_case('root')

    def test_cube_and_one_element_array_target_and_bounds(self):
        self.run_case('cube')
