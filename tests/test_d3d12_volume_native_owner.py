"""C21 regression: complete actual native layout must cross the owner boundary.

AIL's potential sparse-table extent exists for ordinary images. Leaving that
field zero in an owner-query stand-in hid a real composed admission failure.
"""
import ast
from pathlib import Path
import os
import subprocess
import tempfile
import types
import unittest
import test_native_resource_layout as native
from test_dxbc_to_nir import ROOT,local_tree,host_flags
from test_d3d12_view_mapping import block


def actual_native_source():
    """Reuse existing source-generation infrastructure, stopping before compilation.

    Keep real modifier selection/setup/query/format policy and AIL. The BO/device
    stubs count accesses; no stand-in populates a matching native query record.
    Refuse infrastructure drift instead of executing its build/test suffix.
    """
    path=ROOT/'tests/test_native_resource_layout.py'
    tree=ast.parse(path.read_text())
    cls=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='NativeResourceLayout')
    fn=next(n for n in cls.body if isinstance(n,ast.FunctionDef) and n.name=='setUpClass')
    boundaries=[i for i,n in enumerate(fn.body) if isinstance(n,ast.Assign) and
                any(isinstance(t,ast.Name) and t.id=='libs' for t in n.targets)]
    if len(boundaries)!=1:raise AssertionError('native source-generation boundary changed')
    fn.decorator_list=[];fn.body=fn.body[:boundaries[0]]
    # No subprocess call is allowed in this generation prefix.
    if any(isinstance(n,ast.Attribute) and isinstance(n.value,ast.Name) and
           n.value.id=='subprocess' for n in ast.walk(fn)):
        raise AssertionError('compiler entered native source-generation prefix')
    ns=dict(vars(native))
    exec(compile(ast.fix_missing_locations(ast.Module(body=[fn],type_ignores=[])),str(path),'exec'),ns)
    state=types.SimpleNamespace()
    try:
        ns['setUpClass'](state)
        source=(Path(state.tmp.name)/'layout.c').read_text()
    finally:
        if hasattr(state,'tmp'):state.tmp.cleanup()
    if source.count('int main(')!=1:raise AssertionError('native fixture main drift')
    source=source.replace('int main(','int existing_native_layout_main(',1)
    return source+'\nunsigned c21_native_bo_calls(void){return bo_calls;}\nunsigned c21_native_device_calls(void){return device_accesses;}\n'


def actual_owner_source():
    owner=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
    # Retain only the existing host type/format preamble, never its query stub.
    prefix=(ROOT/'tests/d3d12_volume_query_fixture.cpp').read_text().split('static UINT query_calls=')[0]
    prefix=prefix.replace('#include "asahi/layout/layout.h"\n','')
    prefix=prefix.replace('#define E_INVALIDARG (-1)','#define E_INVALIDARG static_cast<HRESULT>(0x80070057u)')
    prefix=prefix.replace('#define E_NOTIMPL (-2)','#define E_NOTIMPL static_cast<HRESULT>(0x80004001u)')
    header=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.h').read_text()
    end=header.index('} AGX_D3D12_VOLUME_LAYOUT;')+len('} AGX_D3D12_VOLUME_LAYOUT;')
    prefix=prefix.replace('/* VOLUME_LAYOUT_RECORD */',header[header.rfind('typedef struct {',0,end):end])
    funcs='\n'.join(block(owner,m) for m in ['static unsigned block_bytes(',
        'static bool texture3d_template(', 'static bool texture3d_layout(',
        'extern "C" HRESULT AgxD3d12QueryTexture3DLayout('])
    return prefix+'\n'+funcs+'\n'+(ROOT/'tests/d3d12_volume_native_owner_fixture.cpp').read_text()


class CompleteNativeVolumeQuery(unittest.TestCase):
    def test_complete_native_query_passes_ordinary_volume_owner_without_bo(self):
        build=local_tree('accelerated-desktop-ad03','mesa-build');mesa=local_tree('reference','mesa')
        if not build or not mesa:self.skipTest('pinned host Mesa/AIL unavailable')
        # The full C native resource source includes DRM/system headers. Match the
        # accepted EXP1424 compiler repair: always pass an explicit macOS SDK.
        sdk=Path('/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk')
        if not sdk.is_dir():self.skipTest('accepted MacOSX27 SDK unavailable')
        tool=Path('/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin')
        cc=str(tool/'clang');cxx=str(tool/'clang++')
        common=['-isysroot',str(sdk),'-fsanitize=address,undefined','-fno-omit-frame-pointer',
            *host_flags(build),'-I',str(mesa/'src/gallium/include'),'-I',str(mesa/'src/gallium/auxiliary'),
            '-I',str(ROOT/'drivers/apple-agx/mesa/winsys')]
        libs=[build/p for p in ['src/asahi/layout/libasahi_layout.a','src/util/libmesa_util.a',
            'src/util/libmesa_util_simd.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a']]
        with tempfile.TemporaryDirectory(prefix='volume-native-owner-') as temp:
            out=Path(temp);c=out/'native.c';cpp=out/'owner.cpp'
            c.write_text(actual_native_source());cpp.write_text(actual_owner_source())
            commands=[[cc,'-std=c11','-DAPPLE_AGX_GPUVA_WINSYS',*common,'-c',str(c),'-o',str(out/'native.o')],
                [cxx,'-std=c++17',*common,'-c',str(cpp),'-o',str(out/'owner.o')],
                [cxx,'-isysroot',str(sdk),'-fsanitize=address,undefined',str(out/'native.o'),str(out/'owner.o'),
                 *map(str,libs),'-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread','-o',str(out/'native-owner')]]
            for argv in commands:
                p=subprocess.run(argv,capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            p=subprocess.run([str(out/'native-owner')],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            self.assertEqual(p.stdout.count(' PASS'),5,p.stdout)
            self.assertIn('NATIVE_OWNER checks=5 failed=0 BO=0 device=0',p.stdout)
