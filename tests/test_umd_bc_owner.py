"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Actual CopyTextureRegion upload/readback must use cached CPU-visible UINT
staging and GPU block copies, never map the BC BO, preserve pitch padding,
array layers and 2x2 tails, and propagate staging failures.
RC85: the upload staging map discards the whole staging (no Asahi shadow
copy of its dead contents) within Asahi's 6 MiB copy-shadow bound.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_dxbc_to_nir import ROOT, local_tree, host_flags
from test_d3d12_view_mapping import block

class BcOwner(unittest.TestCase):
    def test_cached_upload_download(self):
        mesa=local_tree('reference','mesa');build=local_tree('accelerated-desktop-ad03','mesa-build')
        self.assertIsNotNone(mesa);self.assertIsNotNone(build)
        owner=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        funcs='\n'.join(block(owner,n) for n in ('static unsigned block_bytes(',
            'static unsigned texture_block(enum pipe_format format, unsigned *bw, unsigned *bh) {',
            'static unsigned level_extent(unsigned extent, unsigned level) {', 'static unsigned texture_layer_extent(const struct pipe_resource *resource,unsigned level) {',
            'static enum pipe_format bc_staging_format(', 'static void gpu_copy_blocks(',
            'static bool staged_upload_discard(', 'static HRESULT staged_upload(', 'static HRESULT staged_download(',
            'extern "C" HRESULT AgxD3d12OwnerCopyTexture('))
        header=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.h').read_text()
        end=header.index('} AGX_D3D12_TEXTURE_COPY;')+len('} AGX_D3D12_TEXTURE_COPY;')
        record=header[header.rfind('typedef struct {',0,end):end]
        fixture=(ROOT/'tests/umd_bc_owner_fixture.cpp').read_text().replace('/* ACTUAL_COPY_RECORD */',record).replace('/* ACTUAL_OWNER_COPY */',funcs)
        with tempfile.TemporaryDirectory(prefix='bc-owner-') as tmp:
            src=Path(tmp)/'copy.cpp';exe=Path(tmp)/'copy';src.write_text(fixture)
            libs=[build/p for p in ('src/util/libmesa_util.a','src/util/libmesa_util_simd.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a')]
            r=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',*host_flags(build),
                '-I'+str(mesa/'src/gallium/include'),str(src),'-o',str(exe),*map(str,libs),
                '-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread'],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
