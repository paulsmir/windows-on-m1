"""Real owner bind_stage must not dword-truncate 8/16-bit typed SRV extents.

The actual pinned Asahi element-count helper interprets the captured template;
Mesa's linked format table supplies element sizes. No GPU readback is simulated.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d12_view_mapping import block
from test_dxbc_to_nir import local_tree, host_flags

ROOT=Path(__file__).resolve().parents[1]

class TypedSrvExtent(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build=local_tree('accelerated-desktop-ad03','mesa-build')
        mesa=local_tree('reference','mesa')
        if not build or not mesa:
            raise unittest.SkipTest('host Mesa headers/libraries unavailable')
        owner=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        header=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.h').read_text()
        end=header.index('} AGX_D3D12_SRV;')+len('} AGX_D3D12_SRV;')
        srv=header[header.rfind('typedef struct {',0,end):end]
        asahi=(mesa/'src/gallium/drivers/asahi/agx_state.h').read_text()
        constants=(mesa/'src/asahi/compiler/agx_nir_texture.h').read_text()
        defines=[]
        lines=constants.splitlines()
        for name in ('AGX_TEXTURE_BUFFER_WIDTH','AGX_TEXTURE_BUFFER_MAX_HEIGHT','AGX_TEXTURE_BUFFER_MAX_SIZE'):
            at=next(i for i,line in enumerate(lines) if line.startswith('#define '+name+' '))
            define=[lines[at]]
            while define[-1].endswith(chr(92)):
                at+=1;define.append(lines[at])
            defines.append('\n'.join(define))
        code='\n'.join(defines)
        code+='\n'+block(asahi,'static inline uint32_t\nagx_texture_buffer_size_el(')
        code+='\n'+block(owner,'static bool srv_component_mapping(')
        code+='\n'+block(owner,'static bool uav_dimension_matches(')
        code+='\n'+block(owner,'static int srv_texture_template(')+'\n'+block(owner,'static HRESULT bind_stage(')
        cls.tmp=tempfile.TemporaryDirectory(prefix='d3d12-typed-srv-extent-')
        src=Path(cls.tmp.name)/'extent.cpp';cls.exe=Path(cls.tmp.name)/'extent'
        src.write_text((ROOT/'tests/d3d12_typed_srv_extent_fixture.cpp').read_text().replace('/* OWNER_SRV */',srv).replace('/* PRODUCTION */',code))
        libs=[build/p for p in ('src/util/libmesa_util.a','src/util/libmesa_util_simd.a',
             'src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a')]
        built=subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-fsanitize=address,undefined',
            *host_flags(build),'-I',str(mesa/'src/gallium/include'),'-I',str(ROOT/'drivers/apple-agx/mesa/winsys'),
            '-I',str(ROOT/'drivers/apple-agx/mesa/dxbc'),str(src),'-o',str(cls.exe),*map(str,libs),
            '-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread'],capture_output=True,text=True)
        if built.returncode: raise AssertionError(built.stderr)

    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()

    def check_extent(self, component):
        run=subprocess.run([str(self.exe),str(component)],capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_r8_uint_three_elements_retains_final_byte(self): self.check_extent(1)
    def test_r16_uint_three_elements_retains_final_word(self): self.check_extent(2)
    def test_r32_uint_aligned_three_elements_control(self): self.check_extent(4)
    def test_untyped_three_dword_control(self): self.check_extent(0)
