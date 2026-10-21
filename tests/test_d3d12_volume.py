"""C21 executes production volume sizing and owner logic with observed callbacks."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_d3d12_view_mapping import block
from d3d12_volume_fixture_dependencies import line_query_dependencies
from test_dxbc_to_nir import local_tree,host_flags
ROOT=Path(__file__).resolve().parents[1]
OWNER=ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
DDI=ROOT/'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'

class VolumeQuery(unittest.TestCase):
    def test_pure_native_size_and_volume_template(self):
        build=local_tree('accelerated-desktop-ad03','mesa-build');mesa=local_tree('reference','mesa')
        if not build or not mesa:self.skipTest('host Mesa unavailable')
        owner=OWNER.read_text();ddi=DDI.read_text()
        funcs=block(owner,'static unsigned block_bytes(')+'\n'
        funcs+=block(owner,'extern "C" unsigned AgxD3d12FormatBlockBytes(')+'\n'
        funcs+=block(owner,'static unsigned texture_block(')+'\n'+block(owner,'extern "C" unsigned AgxD3d12FormatBlock(')+'\n'+block(owner,'extern "C" int AgxD3d12FormatMultisample(')+'\n'
        for name in ['static bool texture1d_template(', 'extern "C" HRESULT AgxD3d12QueryTexture1DLayout(', 'static bool texture3d_template(', 'static bool texture3d_layout(', 'extern "C" HRESULT AgxD3d12QueryTexture3DLayout(', 'extern "C" HRESULT AgxD3d12QueryTexture3D(', 'extern "C" HRESULT AgxD3d12OwnerCreateTexture3D(']:
            if name in owner:funcs+=block(owner,name)+'\n'
        record='typedef struct {unsigned long long Bytes, NativeBytes, DepthStride, MipOffsets[16];} AGX_D3D12_VOLUME_LAYOUT;'
        funcs+=line_query_dependencies(owner)
        funcs+=block(ddi,'static bool VolumeLayoutInfo(')+'\n'+block(ddi,'static bool LineLayoutInfo(')+'\n'
        source=(ROOT/'tests/d3d12_volume_query_fixture.cpp').read_text().replace('/* VOLUME_LAYOUT_RECORD */',record).replace('/* ACTUAL_QUERY */',funcs).replace('/* ACTUAL_TEXTURE_BYTES */',block(ddi,'static UINT64 TextureBytes('))
        with tempfile.TemporaryDirectory(prefix='d3d12-volume-') as tmp:
            path=Path(tmp)/'query.cpp';exe=Path(tmp)/'query';path.write_text(source)
            libs=[build/p for p in ['src/asahi/layout/libasahi_layout.a','src/util/libmesa_util.a','src/util/libmesa_util_simd.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a']]
            run=subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-fsanitize=address,undefined',*host_flags(build),'-I',str(mesa/'src/gallium/include'),'-I',str(ROOT/'drivers/apple-agx/mesa/winsys'),str(path),'-o',str(exe),*map(str,libs),'-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread'],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

class VolumeCopy(unittest.TestCase):
    def test_volume_copy_uses_minified_depth_and_preserves_neighbors(self):
        build=local_tree('accelerated-desktop-ad03','mesa-build');mesa=local_tree('reference','mesa')
        if not build or not mesa:self.skipTest('host Mesa unavailable')
        owner=OWNER.read_text()
        funcs=block(owner,'static unsigned block_bytes(')+'\n'+block(owner,'static unsigned level_extent(unsigned extent, unsigned level) {')+'\n'
        funcs+=block(owner,'static unsigned texture_block(')+'\n'
        if 'static unsigned texture_layer_extent(' in owner:funcs+=block(owner,'static unsigned texture_layer_extent(const struct pipe_resource *resource,unsigned level) {')+'\n'
        funcs+=block(owner,'extern "C" HRESULT AgxD3d12OwnerCopyTexture(')
        header=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.h').read_text()
        end=header.index('} AGX_D3D12_TEXTURE_COPY;')+len('} AGX_D3D12_TEXTURE_COPY;')
        record=header[header.rfind('typedef struct {',0,end):end]
        source=(ROOT/'tests/d3d12_volume_copy_fixture.cpp').read_text().replace('/* ACTUAL_COPY_RECORD */',record).replace('/* ACTUAL_OWNER_COPY */',funcs)
        with tempfile.TemporaryDirectory(prefix='d3d12-volume-copy-') as tmp:
            path=Path(tmp)/'copy.cpp';exe=Path(tmp)/'copy';path.write_text(source)
            libs=[build/p for p in ['src/util/libmesa_util.a','src/util/libmesa_util_simd.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a']]
            run=subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-fsanitize=address,undefined',*host_flags(build),'-I',str(mesa/'src/gallium/include'),str(path),'-o',str(exe),*map(str,libs),'-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread'],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

class VolumeViews(unittest.TestCase):
    def test_volume_srv_full_mip_uav_and_plane_rtv(self):
        build=local_tree('accelerated-desktop-ad03','mesa-build');mesa=local_tree('reference','mesa')
        if not build or not mesa:self.skipTest('host Mesa unavailable')
        ddi=DDI.read_text();header=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.h').read_text()
        end=header.index('} AGX_D3D12_SRV;')+len('} AGX_D3D12_SRV;');srv=header[header.rfind('typedef struct {',0,end):end]
        prefix=(ROOT/'tests/d3d12_view_mapping_fixture.cpp').read_text().split('/* PRODUCTION_VIEW_MAPPING */')[0]
        prefix=prefix.replace('/* PRODUCTION_OWNER_SRV */',srv)
        prefix+='\nenum {kDescriptorUavTexture=8,kDescriptorUavTypedBuffer=9,kDescriptorUavBuffer=2,kDescriptorRtv=4};\n'
        prefix+='''
struct D3D12DDIARG_TEX2D_UNORDERED_ACCESS_VIEW_0002 {UINT MipSlice,FirstArraySlice,ArraySize,PlaneSlice;};
struct D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002 {
 struct {UINT MipSlice,FirstArraySlice,ArraySize;} Tex1D;
 struct {void *pDrvPrivate;} hDrvResource; DXGI_FORMAT Format;UINT ResourceDimension;
 D3D12DDIARG_TEX2D_UNORDERED_ACCESS_VIEW_0002 Tex2D;
 struct {UINT MipSlice,FirstW,WSize;} Tex3D;
 struct {UINT64 FirstElement;UINT NumElements,StructureByteStride,Flags;UINT64 CounterOffsetInBytes;struct {void *pDrvPrivate;} hDrvCounterResource;} Buffer;
};
#define D3D12DDI_BUFFER_UAV_FLAG_RAW 1
struct D3D12DDIARG_CREATE_RENDER_TARGET_VIEW_0002 {
 struct {void *pDrvPrivate;} hDrvResource;DXGI_FORMAT Format;UINT ResourceDimension;
 D3D12DDIARG_TEX2D_UNORDERED_ACCESS_VIEW_0002 Tex2D;
 struct {UINT MipSlice,FirstW,WSize;} Tex3D;
};
'''
        code=block(ddi,'struct AgxD3d12Descriptor {')+';\n'+block(ddi,'static void SetUavCounter(')+'\n'
        if 'static bool VolumeViewRange(' in ddi:code+=block(ddi,'static bool VolumeViewRange(')+'\n'
        for name in ['CreateShaderResourceView','CreateUnorderedAccessView','CreateRenderTargetView']:
            code+=block(ddi,'static VOID APIENTRY '+name+'(')+'\n'
        owner=OWNER.read_text()
        prefix=prefix.replace('static bool uav_image(const AGX_D3D12_UAV *,pipe_image_view *) {return false;}', '')
        prefix+='\nusing LONG=int;struct RECT {LONG left,top,right,bottom;};struct AGX_D3D12_OWNER {pipe_context *Context;unsigned PendingDraws;bool DrawFramebufferValid;};\nstruct OwnerLock {explicit OwnerLock(AGX_D3D12_OWNER *) {}}; /* the owner lock (EXP1824) is not a fixture boundary */\n'
        code+='\n'+block(owner,'static unsigned level_extent(unsigned extent, unsigned level) {')
        code+='\n'+block(owner,'static unsigned texture_layer_extent(const struct pipe_resource *resource,unsigned level) {')
        for marker in ['static bool uav_dimension_matches(', 'static bool uav_image(', 'static bool srv_component_mapping(', 'static HRESULT bind_stage(',
                       'static void clear_view_layers(', 'extern "C" HRESULT AgxD3d12OwnerClearRenderTarget(']:
            code+='\n'+block(owner,marker)
        source=prefix+code+(ROOT/'tests/d3d12_volume_views_fixture.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='d3d12-volume-views-') as tmp:
            path=Path(tmp)/'views.cpp';exe=Path(tmp)/'views';path.write_text(source)
            run=subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-DAPPLE_AGX_D3D12_NATIVE_OWNER','-fsanitize=address,undefined',*host_flags(build),'-I',str(mesa/'src/gallium/include'),'-I',str(ROOT/'drivers/apple-agx/mesa/winsys'),'-I',str(ROOT/'drivers/apple-agx/mesa/dxbc'),str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

class VolumeMetadata(unittest.TestCase):
    def test_native_mip_offsets_block_strides_and_map_refusal(self):
        ddi=DDI.read_text()
        code=block(ddi,'struct AgxD3d12Heap {')+';\n'+block(ddi,'struct AgxD3d12Resource {')+';\n'
        for marker in ['static VOID APIENTRY CheckSubresourceInfo(', 'static VOID APIENTRY CheckExistingResourceAllocationInfo(', 'static HRESULT APIENTRY MapHeap(']:
            code+=block(ddi,marker)+'\n'
        source=(ROOT/'tests/d3d12_volume_metadata_fixture.cpp').read_text().replace('/* ACTUAL_METADATA */',code)
        with tempfile.TemporaryDirectory(prefix='d3d12-volume-metadata-') as tmp:
            path=Path(tmp)/'metadata.cpp';exe=Path(tmp)/'metadata';path.write_text(source)
            run=subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Werror=sign-compare','-fsanitize=address,undefined',str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

class VolumeRecording(unittest.TestCase):
    def test_real_ddi_copy_records_mip_z_and_pitches_without_modulo_alias(self):
        ddi=DDI.read_text()
        types=block(ddi,'struct AgxD3d12TextureCopy {')+';\n'+block(ddi,'struct AgxD3d12TextureRegionCopy {')+';\n'
        code='\n'.join(block(ddi,marker) for marker in ['struct AgxD3d12CopySide {','static bool CopySide(',
            'static bool LineSubresource(', 'static void CopyLineTextureRegion(',
            'static void CopyVolumeTextureRegion(', 'static VOID APIENTRY CopyTextureRegion('])
        code=code.replace('struct AgxD3d12CopySide {','struct AgxD3d12CopySide {',1)
        end=code.index('}')+1
        code=code[:end]+';'+code[end:]
        source=(ROOT/'tests/d3d12_volume_record_fixture.cpp').read_text().replace('/* ACTUAL_RECORD_TYPES */',types).replace('/* ACTUAL_RECORDING */',code)
        with tempfile.TemporaryDirectory(prefix='d3d12-volume-record-') as tmp:
            path=Path(tmp)/'record.cpp';exe=Path(tmp)/'record';path.write_text(source)
            run=subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Werror=sign-compare','-fsanitize=address,undefined',str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
