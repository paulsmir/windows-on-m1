"""C23 actual native-query/owner/DDI composition and1D DXBC->NIR->Asahi gates.

No GPU simulation: external BO/queue callbacks only observe allocations/records.
Production functions and native query are extracted afresh on every test build.
"""
from pathlib import Path
import hashlib
import re
import subprocess
import tempfile
import unittest
from test_dxbc_to_nir import ROOT,local_tree,host_flags
from d3d12_texture1d_source import native_source,query_source,create_source,view_source,copy_source

class Texture1D(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=local_tree('accelerated-desktop-ad03','mesa-build');cls.mesa=local_tree('reference','mesa')
        cls.sdk=Path('/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk')
        if not cls.build or not cls.mesa or not cls.sdk.is_dir():raise unittest.SkipTest('pinned Mesa/AIL andMacOSX27 SDK unavailable')
        cls.tmp=tempfile.TemporaryDirectory(prefix='d3d12-texture1d-');cls.out=Path(cls.tmp.name)
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
    def test_complete_native_query_and_packed_layout_all_widths(self):
        out=self.check('query',query_source(),native=True)
        self.assertIn('cases=65534 requested_mips=458782 failed=0 BO=0 device=0',out)
    def test_default_create_resolved_layout_and_refusals(self):
        self.assertIn('12refusals beforeEnsureOwner',self.check('create',create_source(),native=True))
    def test_actual_srv_uav_ranges_dimension_and_unused_null_bindings(self):
        self.assertIn('unused1Dnullbindings',self.check('view',view_source()))
    def test_actual_copy_mip_origins_pitches_and_refusals(self):
        self.assertIn('10refusals',self.check('copy',copy_source()))

class Texture1DShaders(unittest.TestCase):
    def test_actual_fxc_dimensions_load_store_through_asahi(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        The pinned FXC bytes must still compile through NIR and Asahi after
        Git normalizes generated header line endings on a clean checkout.
        """
        b=local_tree('accelerated-desktop-ad03','mesa-build');m=local_tree('reference','mesa')
        sdk=Path('/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk')
        if not b or not m or not sdk.is_dir():self.skipTest('pinned host Mesa/SDK unavailable')
        pins=[('load','B','b0f80a2ae47947004b50917cd75d72a0c775b47b806bbfa78949dfc3618d3d0a','9943862a3405c2aa7e32081c0f0514089216f61af5a0fde56e8512ffc1cbf0d2'),('store','C','3ad2a2f66e17aec5833a114efbd15781b0853fc8e7ac0b4cf40ffb1f1e112d30','0e0d1f59cf787bcc4c1daa8be3753fddaf9d01f475a81489be40be6cb5641f4b')]
        libs=[b/p for p in ['src/asahi/compiler/libasahi_compiler.a','src/asahi/isa/libagx2_disasm.a','src/asahi/layout/libasahi_layout.a','src/compiler/nir/libnir.a','src/compiler/libcompiler.a','src/util/libmesa_util.a','src/util/libmesa_util_simd.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a']]
        dx=ROOT/'drivers/apple-agx/mesa/dxbc'
        with tempfile.TemporaryDirectory(prefix='texture1d-shaders-') as temp:
            o=Path(temp);exe=o/'nir';argv=['/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang','-std=c11','-isysroot',str(sdk),'-Werror=implicit-function-declaration',*host_flags(b),'-I',str(b/'src/asahi'),'-I',str(m/'src/asahi'),'-I',str(dx),str(ROOT/'tests/d3d12_texture1d_shader_fixture.c'),str(dx/'dxbc_to_nir.c'),str(dx/'dxbc_parse.c'),str(dx/'dxbc_minmax.c'),'-o',str(exe),*map(str,libs),'-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread']
            z=subprocess.run(argv,capture_output=True,text=True);self.assertEqual(z.returncode,0,z.stdout+z.stderr)
            for kind,mode,header_hash,hlsl_hash in pins:
                h=ROOT/f'drivers/apple-agx/windows/icd/agx_d3d12_probe_cs_texture1d_{kind}.h';data=h.read_bytes()
                # FXC emitted CRLF; Git stores these text headers as LF.
                pinned_text=data.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')
                self.assertEqual(hashlib.sha256(pinned_text).hexdigest(),header_hash)
                source=ROOT/f'tests/fixtures/dxbc/c23_texture1d_{kind}.hlsl';self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(),hlsl_hash)
                a=data.decode().split('const BYTE',1)[1].split('{',1)[1].split('}',1)[0];blob=bytes(map(int,re.findall(r'\d+',a)));self.assertEqual(blob[:4],b'DXBC');p=o/(kind+'.dxbc');p.write_bytes(blob)
                z=subprocess.run([str(exe),str(p),mode],capture_output=True,text=True,timeout=60);self.assertEqual(z.returncode,0,z.stdout+z.stderr);self.assertIn('PASS C23'+mode,z.stdout);self.assertIn('NIR+Asahi',z.stdout)
