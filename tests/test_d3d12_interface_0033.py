"""C50 (D3D12-C50-interface-0033.md): the 0033 tables reuse the validated
0012/0003 fills and reassign the retyped members; no hardware involved."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADER = Path('/Volumes/pwdev/public_windows/.local/reference/wdk26100/d3d12umddi.h')
SOURCE = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'


def typed_members(text, name):
    body = re.search(r'typedef struct ' + name + r'\s*\{(.*?)\}\s*' + name + r'\s*;', text, re.S)
    assert body, name
    return re.findall(r'\b(PFN\w+)\s+(pfn\w+)\s*;', body.group(1))


def fill_body(source, table):
    body = re.search(r'static void Fill' + table + r'\(' + table + r' &f\) \{(.*?)\n\}', source, re.S)
    assert body, table
    return body.group(1)


class D3d12Interface0033(unittest.TestCase):
    def setUp(self):
        if not HEADER.exists():
            self.skipTest('pinned WDK header absent')
        self.header = HEADER.read_text(errors='replace')
        self.source = SOURCE.read_text()

    def reassigned(self, table):
        return re.findall(r'AGX_ASSIGN\(' + table + r',\s*(pfn\w+)\s*,', fill_body(self.source, table))

    def changed(self, old, new):
        before = dict((m, t) for t, m in typed_members(self.header, old))
        return {m for t, m in typed_members(self.header, new) if before.get(m) != t}

    def test_every_retyped_core_member_is_reassigned(self):
        # A slot keeping its CORE_0012 entry would be called with the 0033
        # argument structures (the reinterpret_cast prefix fill).
        expected = self.changed('D3D12DDI_DEVICE_FUNCS_CORE_0012', 'D3D12DDI_DEVICE_FUNCS_CORE_0033')
        self.assertEqual(len(expected), 23)
        assigned = self.reassigned('D3D12DDI_DEVICE_FUNCS_CORE_0033')
        self.assertEqual(set(assigned), expected)
        self.assertEqual(len(assigned), len(expected))
        self.assertEqual([m for _, m in typed_members(self.header, 'D3D12DDI_DEVICE_FUNCS_CORE_0012')],
                         [m for _, m in typed_members(self.header, 'D3D12DDI_DEVICE_FUNCS_CORE_0033')])

    def test_every_retyped_or_added_list_member_is_assigned(self):
        expected = self.changed('D3D12DDI_COMMAND_LIST_FUNCS_3D_0003', 'D3D12DDI_COMMAND_LIST_FUNCS_3D_0033')
        self.assertEqual(len(expected), 9)
        assigned = self.reassigned('D3D12DDI_COMMAND_LIST_FUNCS_3D_0033')
        self.assertEqual(set(assigned), expected)
        self.assertEqual(len(assigned), len(expected))

    def test_every_retyped_or_added_0040_member_is_assigned(self):
        # C51: CORE_0040 reuses the CORE_0033 fill (prefix cast) and 3D_0040
        # the 3D_0033 fill; the pool/list/recorder members must be reassigned
        expected = self.changed('D3D12DDI_DEVICE_FUNCS_CORE_0033', 'D3D12DDI_DEVICE_FUNCS_CORE_0040')
        self.assertEqual(len(expected), 10)
        assigned = self.reassigned('D3D12DDI_DEVICE_FUNCS_CORE_0040')
        self.assertEqual(set(assigned), expected)
        self.assertEqual(len(assigned), len(expected))
        expected = self.changed('D3D12DDI_COMMAND_LIST_FUNCS_3D_0033', 'D3D12DDI_COMMAND_LIST_FUNCS_3D_0040')
        self.assertEqual(expected, {'pfnResetCommandList'})
        self.assertEqual(self.reassigned('D3D12DDI_COMMAND_LIST_FUNCS_3D_0040'), ['pfnResetCommandList'])

    def test_every_retyped_or_added_0070_member_is_assigned(self):
        # C52: CORE_0070 reuses the CORE_0040 fill and 3D_0062 the 3D_0040 fill
        expected = self.changed('D3D12DDI_DEVICE_FUNCS_CORE_0040', 'D3D12DDI_DEVICE_FUNCS_CORE_0070')
        self.assertEqual(len(expected), 24)
        assigned = self.reassigned('D3D12DDI_DEVICE_FUNCS_CORE_0070')
        self.assertEqual(set(assigned), expected)
        self.assertEqual(len(assigned), len(expected))
        expected = self.changed('D3D12DDI_COMMAND_LIST_FUNCS_3D_0040', 'D3D12DDI_COMMAND_LIST_FUNCS_3D_0062')
        self.assertEqual(len(expected), 10)
        assigned = self.reassigned('D3D12DDI_COMMAND_LIST_FUNCS_3D_0062')
        self.assertEqual(set(assigned), expected)
        self.assertEqual(len(assigned), len(expected))

    def test_new_interfaces_only_in_the_opt_in_profile(self):
        versions = self.source[self.source.index('static HRESULT APIENTRY GetVersions('):]
        versions = versions[:versions.index('\n}\n')]
        self.assertIn('InterfaceOptIn() == D3D12DDI_SUPPORTED_0071   ? 6u', versions)
        self.assertIn('if (offered == 6u) versions[n++] = D3D12DDI_SUPPORTED_0071;', versions)
        self.assertIn('if (offered >= 5u) versions[n++] = D3D12DDI_SUPPORTED_0042;', versions)
        self.assertIn('if (offered >= 4u) versions[n++] = D3D12DDI_SUPPORTED_0033;', versions)
        self.assertIn('const UINT offered = Negotiated0071() ? 7u : Negotiated0042() ? 4u : Negotiated0033() ? 3u : 2u;',
                      self.source)
        self.assertIn('if (!ShaderModel6Enabled() || GetEnvironmentVariableA("APPLE_AGX_D3D12_DDI"', self.source)

    def test_extended_features_table_is_filled_from_r2(self):
        # EXP1868: the 0033 runtime filled the core and list tables, then
        # asked for table type 8 (EXTENDED_FEATURES, 0x20 bytes) and failed
        # D3D12CreateDevice (E_NOTIMPL) when it was refused.
        fill = self.source[self.source.index('static HRESULT APIENTRY FillDDITable(D3D12DDI_HADAPTER, D3D12DDI_TABLE_TYPE type'):]
        fill = fill[:fill.index('\n}\n')]
        self.assertIn('case D3D12DDI_TABLE_TYPE_0020_EXTENDED_FEATURES:', fill)
        self.assertIn('if (NegotiatedR2())', fill)
        self.assertIn('FillTable<D3D12DDI_EXTENDED_FEATURES_FUNCS_0021>', fill)
        body = fill_body(self.source, 'D3D12DDI_EXTENDED_FEATURES_FUNCS_0021')
        expected = {m for _, m in typed_members(self.header, 'D3D12DDI_EXTENDED_FEATURES_FUNCS_0021')}
        self.assertEqual(set(re.findall(r'f\.(pfn\w+) =', body)), expected)

    def test_r2_caps_answer_only_r2_runtimes(self):
        # EXP1869: the 0033 runtime destroyed the device on an unanswered
        # 1062 (UMD-based queue priority) after the tables; R1 runtimes keep
        # the E_NOTIMPL they were validated with (EXP1866: 1069/1071 refused)
        caps = self.source[self.source.index('static HRESULT APIENTRY GetCaps('):]
        caps = caps[:caps.index('\n}\n')]
        for case in ('0023_UMD_BASED_COMMAND_QUEUE_PRIORITY', '0022_CPU_PAGE_TABLE_FALSE_POSITIVES',
                     '0030_PROTECTED_RESOURCE_SESSION_SUPPORT', 'EXECUTECOMMANDLISTS_PARALLELISM'):
            body = caps[caps.index('case D3D12DDICAPS_TYPE_%s:' % case):]
            body = body[:body.index('break;')]
            self.assertIn('NegotiatedR2()', body, case)
        self.assertIn('static bool NegotiatedR2() { return adapter_object.negotiated >= D3D12DDI_SUPPORTED_0021; }',
                      self.source)

    def test_tables_follow_the_negotiated_interface(self):
        # CORE_0012 and CORE_0033 have one size: the table is chosen by the
        # version CalcPrivateDeviceSize received, not by the size.
        self.assertIn('adapter_object.negotiated = (static_cast<UINT64>(arg->Interface) << 32) | arg->Version;',
                      self.source)
        fill = self.source[self.source.index('static HRESULT APIENTRY FillDDITable('):]
        for table in ('DEVICE_FUNCS_CORE_0070', 'COMMAND_LIST_FUNCS_3D_0062'):
            self.assertIn('hr = Negotiated0071()\n                 ? FillTable<D3D12DDI_%s>' % table, fill)
        for table in ('DEVICE_FUNCS_CORE_0040', 'COMMAND_LIST_FUNCS_3D_0040'):
            self.assertIn(': Negotiated0042()\n                 ? FillTable<D3D12DDI_%s>' % table, fill)
        for table in ('DEVICE_FUNCS_CORE_0033', 'COMMAND_LIST_FUNCS_3D_0033'):
            self.assertIn(': Negotiated0033()\n                 ? FillTable<D3D12DDI_%s>' % table, fill)


if __name__ == '__main__':
    unittest.main()
