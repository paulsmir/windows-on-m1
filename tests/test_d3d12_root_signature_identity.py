"""Actual setters must preserve bound arguments for the same root identity."""
from pathlib import Path
import hashlib
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def block(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class RootSignatureIdentity(unittest.TestCase):
    def test_redundant_identity_preserves_both_banks(self):
        wdk = ROOT / '.local/reference/wdk26100/d3d12umddi.h'
        self.assertEqual(hashlib.sha256(wdk.read_bytes()).hexdigest(),
                         'd338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28')
        header = wdk.read_text(encoding='utf-8-sig')
        record = re.search(r'typedef struct D3D12DDI_COMMAND_LIST_FUNCS_3D_0003\s*\{(.*?)\}\s*D3D12DDI_COMMAND_LIST_FUNCS_3D_0003', header, re.S)
        members = re.findall(r'\b(pfn\w+)\s*;', record.group(1))
        self.assertEqual(len(members), 51)
        source = (ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp').read_text()
        constants = '\n'.join(re.search(r'static constexpr UINT ' + name + r' = [^;]+;', source).group(0)
                              for name in ('kRootArguments', 'kVertexBuffers'))
        actual = constants + '\n' + block(source, 'struct AgxD3d12CommandList {') + ';\n'
        for marker in ('static AgxD3d12CommandList *ListOf(',
                       'static void ReportRedundantRoot(',
                       'static VOID APIENTRY SetComputeRootSignature(',
                       'static VOID APIENTRY SetGraphicsRootSignature(',
                       'static VOID APIENTRY ClearRootArguments('):
            actual += block(source, marker) + '\n'
        fixture = (ROOT / 'tests/d3d12_root_signature_identity_fixture.cpp').read_text()
        fixture = fixture.replace('CLEAR_ROOT_SLOT', str(members.index('pfnClearRootArguments')))
        fixture = fixture.replace('/* CURRENT_FUNCTIONS */', actual)
        with tempfile.TemporaryDirectory(prefix='d3d12-root-identity-') as directory:
            cpp = Path(directory) / 'root.cpp'
            exe = Path(directory) / 'root'
            cpp.write_text(fixture)
            build = subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17',
                                    '-fsanitize=address,undefined', str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
