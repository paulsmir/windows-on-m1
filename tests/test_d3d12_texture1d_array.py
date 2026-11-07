"""C24 actual current native-query/create/AIL/views/copy/array NIR regressions.
No GPU/nativeBO/Windows ABI simulation; callback stand-ins count/record only.
"""
from pathlib import Path
import hashlib
import subprocess
import tempfile
import unittest
from test_dxbc_to_nir import ROOT, local_tree, host_flags
from d3d12_texture1d_array_source import native_source, create_source, view_source, copy_source

class Texture1DArray(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=local_tree('accelerated-desktop-ad03','mesa-build');cls.mesa=local_tree('reference','mesa')
        cls.sdk=Path('/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk')
        if not cls.build or not cls.mesa or not cls.sdk.is_dir():raise unittest.SkipTest('pinned Mesa/AIL andMacOSX27 SDK unavailable')
        cls.tmp=tempfile.TemporaryDirectory(prefix='d3d12-texture1d-array-');cls.out=Path(cls.tmp.name)
        cls.tool=Path('/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin')
        cls.common=['-isysroot',str(cls.sdk),'-fsanitize=address,undefined','-fno-omit-frame-pointer',*host_flags(cls.build),
            '-I',str(cls.mesa/'src/gallium/include'),'-I',str(cls.mesa/'src/gallium/auxiliary'),
            '-I',str(ROOT/'drivers/apple-agx/mesa/winsys'),'-I',str(ROOT/'drivers/apple-agx/mesa/dxbc'),'-I',str(ROOT/'tests')]
        cls.libs=[cls.build/p for p in ['src/asahi/layout/libasahi_layout.a','src/util/libmesa_util.a','src/util/libmesa_util_simd.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a']]
        p=cls.out/'native.c';p.write_text(native_source())
        z=subprocess.run([str(cls.tool/'clang'),'-std=c11','-DAPPLE_AGX_GPUVA_WINSYS',*cls.common,'-c',str(p),'-o',str(cls.out/'native.o')],capture_output=True,text=True)
        if z.returncode:cls.tmp.cleanup();raise AssertionError(z.stdout+z.stderr)
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def check(self,name,source,native=False,mesa=True):
        p=self.out/(name+'.cpp');p.write_text(source);exe=self.out/name
        argv=[str(self.tool/'clang++'),'-std=c++17','-include','initializer_list','-DAPPLE_AGX_D3D12_NATIVE_OWNER',*self.common,str(p)]
        if native:argv += [str(self.out/'native.o'),*map(str,self.libs),'-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread']
        argv+=['-o',str(exe)];z=subprocess.run(argv,capture_output=True,text=True);self.assertEqual(z.returncode,0,z.stdout+z.stderr)
        z=subprocess.run([str(exe)],capture_output=True,text=True,timeout=120);self.assertEqual(z.returncode,0,z.stdout+z.stderr);return z.stdout
    def test_actual_native_query_create_reflection_48_cases(self):
        out=self.check('create',create_source(),native=True)
        self.assertIn('cases=48',out)
        self.assertIn('query0BO/device; canonical ARRAY1 and ARRAY2048',out)
    def test_actual_views_count1_array_and_refusals(self):
        self.assertIn('views15refusals',self.check('view',view_source()))
    def test_actual_six_subresource_copy_and_refusals(self):
        self.assertIn('and12refusals',self.check('copy',copy_source()))

class Texture1DArrayShaders(unittest.TestCase):
    def test_actual_fxc_array_dimensions_through_asahi(self):
        b=local_tree('accelerated-desktop-ad03','mesa-build');m=local_tree('reference','mesa')
        sdk=Path('/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk')
        if not b or not m or not sdk.is_dir():self.skipTest('pinned host Mesa/SDK unavailable')
        libs=[b/p for p in ['src/asahi/compiler/libasahi_compiler.a','src/asahi/isa/libagx2_disasm.a','src/asahi/layout/libasahi_layout.a','src/compiler/nir/libnir.a','src/compiler/libcompiler.a','src/util/libmesa_util.a','src/util/libmesa_util_simd.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a']]
        dx=ROOT/'drivers/apple-agx/mesa/dxbc'
        with tempfile.TemporaryDirectory(prefix='texture1d-array-nir-') as temp:
            exe=Path(temp)/'nir'
            argv=['/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang','-std=c11','-isysroot',str(sdk),'-Werror=implicit-function-declaration',*host_flags(b),'-I',str(b/'src/asahi'),'-I',str(m/'src/asahi'),'-I',str(dx),str(ROOT/'tests/d3d12_texture1d_array_shader_fixture.c'),str(dx/'dxbc_to_nir.c'),str(dx/'dxbc_parse.c'),str(dx/'dxbc_minmax.c'),'-o',str(exe),*map(str,libs),'-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread']
            z=subprocess.run(argv,capture_output=True,text=True);self.assertEqual(z.returncode,0,z.stdout+z.stderr)
            for kind,mode,want in [
                ('load','B','13a85e9dd6686f046256f6e2c6fd83f20d0c89b024c5bee08f7692d259425998'),
                ('store','C','1ebabd5ad328cbb2fa13a1471ffe0d6805256bb202c397c11debaa3eb11bb390'),
            ]:
                p=ROOT/f'tests/fixtures/dxbc/c24_texture1d_array_{kind}.dxbc'
                self.assertEqual(hashlib.sha256(p.read_bytes()).hexdigest(),want)
                z=subprocess.run([str(exe),str(p),mode],capture_output=True,text=True,timeout=120)
                self.assertEqual(z.returncode,0,z.stdout+z.stderr)
                self.assertIn('PASS C24_ARRAY'+mode,z.stdout)
                self.assertIn('candidate_ARRAY_metadata=YES rawSSBO_OTHER=YES',z.stdout)
