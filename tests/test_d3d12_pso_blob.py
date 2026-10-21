"""Execute the production pipeline-state blob validation (C38).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? pfnCreateDeserializedObject
receives an application-supplied cached blob (ID3D12Device::Create*
PipelineState CachedPSO): a truncated, stale-version, foreign or
size-inconsistent blob, or one whose program counts overflow its size, must
be refused before the driver copies its programs; a well-formed blob is
accepted at exactly its own size.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_d3d12_view_mapping import block

ROOT = Path(__file__).resolve().parents[1]
DDI = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'

FIXTURE = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
using UINT = unsigned; using SIZE_T = size_t; using UINT64 = unsigned long long;
struct D3D12DDI_BLEND_DESC { UINT words[83]; };
struct D3D12DDI_RASTERIZER_DESC { UINT words[11]; };
struct D3D12DDI_DEPTH_STENCIL_DESC { UINT words[13]; };
struct D3D12DDIARG_INPUT_ELEMENT_DESC { UINT InputSlot, AlignedByteOffset, Format, InputSlotClass, InstanceDataStepRate, InputRegister; };
static constexpr UINT kPipelineElements = 16;
/* PRODUCTION */
static std::vector<UINT> make(UINT dwords) {
    std::vector<UINT> words((offsetof(AgxD3d12PsoBlob, tokens) + dwords * sizeof(UINT) + 3) / 4);
    auto *b = reinterpret_cast<AgxD3d12PsoBlob *>(words.data());
    b->magic = kPsoBlobMagic; b->version = kPsoBlobVersion;
    b->bytes = static_cast<UINT>(offsetof(AgxD3d12PsoBlob, tokens) + dwords * sizeof(UINT));
    b->vs_dwords = dwords / 2; b->ps_dwords = dwords - dwords / 2; b->elements = 2;
    return words;
}
int main() {
    auto ok = make(40);
    const SIZE_T size = offsetof(AgxD3d12PsoBlob, tokens) + 40 * sizeof(UINT);
    assert(PsoBlobBytes(size, ok.data()) == size);
    assert(!PsoBlobBytes(size, nullptr));
    assert(!PsoBlobBytes(size - 4, ok.data()));                     /* truncated */
    assert(!PsoBlobBytes(offsetof(AgxD3d12PsoBlob, tokens) - 4, ok.data()));
    auto bad = ok; reinterpret_cast<AgxD3d12PsoBlob *>(bad.data())->magic ^= 1;
    assert(!PsoBlobBytes(size, bad.data()));                         /* foreign */
    bad = ok; reinterpret_cast<AgxD3d12PsoBlob *>(bad.data())->version += 1;
    assert(!PsoBlobBytes(size, bad.data()));                         /* stale version */
    bad = ok; reinterpret_cast<AgxD3d12PsoBlob *>(bad.data())->bytes += 4;
    assert(!PsoBlobBytes(size, bad.data()));                         /* header/size disagree */
    bad = ok; reinterpret_cast<AgxD3d12PsoBlob *>(bad.data())->ds_dwords = 1;
    assert(!PsoBlobBytes(size, bad.data()));                         /* programs past the blob */
    bad = ok; reinterpret_cast<AgxD3d12PsoBlob *>(bad.data())->cs_dwords = 0xffffffffu;
    assert(!PsoBlobBytes(size, bad.data()));                         /* 32-bit count overflow */
    bad = ok; reinterpret_cast<AgxD3d12PsoBlob *>(bad.data())->elements = kPipelineElements + 2;
    assert(!PsoBlobBytes(size, bad.data()));                         /* element count */
    return 0;
}
'''


class PsoBlob(unittest.TestCase):
    def test_blob_validation(self):
        ddi = DDI.read_text()
        code = 'static constexpr UINT kSoEntries = 64;\n' + block(ddi, 'struct AgxD3d12StreamOutput {') + ';\n'
        code += ddi[ddi.index('static constexpr UINT kPsoBlobMagic'):ddi.index('struct AgxD3d12PsoBlob {')]
        code += block(ddi, 'struct AgxD3d12PsoBlob {') + ';\n'
        code += block(ddi, 'static SIZE_T PsoBlobBytes(') + '\n'
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'C++ compiler required')
        with tempfile.TemporaryDirectory(prefix='d3d12-pso-blob-') as tmp:
            source, binary = Path(tmp) / 'blob.cpp', Path(tmp) / 'blob'
            source.write_text(FIXTURE.replace('/* PRODUCTION */', code))
            subprocess.run([compiler, '-std=c++17', '-fsanitize=address,undefined', str(source), '-o', str(binary)],
                           check=True, capture_output=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
