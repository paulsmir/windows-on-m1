"""Pinned D3D12 ABI coverage; no hardware or packaged driver is involved."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADER = Path('/Volumes/pwdev/public_windows/.local/reference/wdk26100/d3d12umddi.h')
SOURCE = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
TABLES = ('D3D12DDI_ADAPTERFUNCS', 'D3D12DDI_DEVICE_FUNCS_CORE_0003',
          'D3D12DDI_COMMAND_LIST_FUNCS_3D_0003',
          'D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001')


def members(text, name):
    body = re.search(r'typedef struct ' + name + r'\s*\{(.*?)\}\s*' + name + r'\s*;', text, re.S)
    assert body, name
    return re.findall(r'\b(pfn\w+)\s*;', body.group(1))


class D3d12AbiGate(unittest.TestCase):
    def test_every_negotiated_member_assigned_once(self):
        if not HEADER.exists():
            self.skipTest('pinned WDK header absent')
        source = SOURCE.read_text()
        header = HEADER.read_text(errors='replace')
        for table in TABLES:
            with self.subTest(table=table):
                expected = members(header, table)
                assignments = re.findall(r'AGX_ASSIGN\(' + table + r',\s*(pfn\w+)\s*,', source)
                if table == 'D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001':
                    assignments += re.findall(r'f\.(pfnUnused2?)\s*=\s*nullptr;', source)
                self.assertEqual(set(assignments), set(expected))
                self.assertEqual(len(assignments), len(expected))

    def test_adapter_handle_is_a_driver_object(self):
        # A3 (EXP1343): a NULL driver adapter handle made the runtime refuse
        # the adapter before GetSupportedVersions.
        source = SOURCE.read_text()
        self.assertNotIn('hAdapter.pDrvPrivate = nullptr', source)
        self.assertIn('arg->hAdapter.pDrvPrivate = &adapter_object;', source)
        self.assertIn('adapter_object.runtime = arg->hRTAdapter;', source)

    def test_no_unimplemented_feature_is_advertised(self):
        source = SOURCE.read_text()
        self.assertIn('D3D12DDI_SUPPORTED_0003', source)
        # C1: the development profile (APPLE_AGX_D3D12=1, checked first in
        # OpenAdapter12) answers only the 3D pipeline caps with FL 11_0.
        self.assertEqual(source.count('D3D12DDI_3DPIPELINELEVEL_11_0'), 1)
        self.assertIn('static const D3D12DDI_3DPIPELINELEVEL kProfileLevel = D3D12DDI_3DPIPELINELEVEL_11_0;', source)
        caps = source[source.index('static HRESULT APIENTRY GetCaps('):]
        caps = caps[:caps.index('\n}\n')]
        self.assertEqual(set(re.findall(r'case (D3D12DDICAPS_TYPE_\w+)', caps)),
                         {'D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1', 'D3D12DDICAPS_TYPE_3DPIPELINESUPPORT',
                          'D3D12DDICAPS_TYPE_D3D12_OPTIONS', 'D3D12DDICAPS_TYPE_MEMORY_ARCHITECTURE',
                          'D3D12DDICAPS_TYPE_SHADER', 'D3D12DDICAPS_TYPE_ARCHITECTURE_INFO',
                          'D3D12DDICAPS_TYPE_GPUVA_CAPS', 'D3D12DDICAPS_TYPE_TEXTURE_LAYOUT',
                          'D3D12DDICAPS_TYPE_TEXTURE_LAYOUT_SETS'})
        # nothing optional is claimed (plan minimum: tier 1, no optional features)
        for claim in ('D3D12DDI_RESOURCE_BINDING_TIER_2', 'D3D12DDI_RESOURCE_HEAP_TIER_2',
                      'Doubles = TRUE', 'ROVs = TRUE', 'OutputMergerLogicOp = TRUE',
                      'D3D12DDI_TILED_RESOURCES_TIER_1', 'D3D12DDI_CONSERVATIVE_RASTERIZATION_TIER_1'):
            self.assertNotIn(claim, caps)
        opener = source[source.index('extern "C" HRESULT APIENTRY OpenAdapter12('):]
        self.assertLess(opener.index('APPLE_AGX_D3D12'), opener.index('FillD3D12DDI_ADAPTERFUNCS'))
        self.assertIn('return E_NOTIMPL;', source)
        self.assertIn('pfnSetErrorCb', source)
        self.assertIn('pfnSetCommandListErrorCb', source)
        # EXP1347: a refused DDI leaves an unbudgeted receipt naming its
        # table and member before the runtime sees the error.
        self.assertEqual(source.count('Rejected<decltype'), 1)  # only AGX_REJECT
        stub = source[source.index('struct Rejected<R (APIENTRY *)(A...), Table, Offset>'):]
        self.assertLess(stub.index('AdmissionUmdDiagnostic("measure-d3d12-rejected"'),
                        stub.index('pfnSetErrorCb(runtime_device'))

    def test_package_and_receipts(self):
        package = ROOT / 'drivers/apple-agx/render-admission/umd'
        exports = re.findall(r'(?m)^\s*(OpenAdapter\w+)\s*$',
                             (package / 'AppleAgxRenderAdmissionUmd.def').read_text())
        native = (package / 'AppleAgxRenderAdmissionUmd.vcxproj').read_text()
        wow64 = (package / 'build-umd-x86.ps1').read_text()
        implementations = {
            'OpenAdapter10_2': (r'render-admission\umd\src\umd.c', r'src\umd.c',
                                ROOT / 'drivers/apple-agx/render-admission/umd/src/umd.c'),
            'OpenAdapter12': (r'd3d12\agx_d3d12_ddi.cpp',
                              r'..\..\d3d12\agx_d3d12_ddi.cpp', SOURCE),
        }
        self.assertEqual(set(exports), set(implementations))
        for export, (wow64_unit, native_unit, implementation) in implementations.items():
            with self.subTest(export=export):
                self.assertIn(wow64_unit, wow64)
                self.assertIn(native_unit, native)
                self.assertRegex(implementation.read_text(),
                                 r'\bHRESULT APIENTRY ' + export + r'\s*\(')
        source = SOURCE.read_text()
        opened = source.split('extern "C" HRESULT APIENTRY OpenAdapter12', 1)[1]
        self.assertLess(opened.index('GetEnvironmentVariableA("APPLE_AGX_D3D12"'), opened.index('FillD3D12DDI_ADAPTERFUNCS'))
        for stage in ('d3d12-open-adapter', 'd3d12-versions', 'd3d12-caps',
                      'd3d12-create-device', 'd3d12-close-adapter'):
            with self.subTest(stage=stage):
                self.assertIn(f'AdmissionUmdDiagnostic("{stage}"', source)
