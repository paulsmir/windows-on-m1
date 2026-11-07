"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
RVA resolution must use the preferred PE image base, accept both sampler
formats, and never choose an export above the sampled RVA or a forwarder.
"""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch
import tempfile

PATH = Path(__file__).resolve().parents[1] / 'tools/agx_sampler_resolve.py'


class ResolverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if PATH.exists():
            spec = importlib.util.spec_from_file_location('resolver', PATH)
            cls.r = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cls.r)

    def setUp(self):
        self.assertTrue(PATH.exists(), 'offline resolver is missing')

    def test_sites(self):
        for line in ['PC 12 ntdll.dll+0x1044', 'LR 7 ntdll.dll+0x1044 CYCLES 900 PCT 90.000']:
            self.assertEqual(self.r.parse_site(line), ('ntdll', 0x1044))
        self.assertIsNone(self.r.parse_site('MODULE 12 ntdll.dll base 0x1000'))
        self.assertIsNone(self.r.parse_site('PC 1 ?+0x1044'))

    def test_exports(self):
        text = '''ImageOptionalHeader {
 ImageBase: 0x180000000
 SizeOfImage: 16384
 ExportTableRVA: 0x3000
 ExportTableSize: 0x100
}
Export {
 Ordinal: 3
 Name: Later
 RVA: 0x2000
}
Export {
 Ordinal: 1
 Name: Wait
 RVA: 0x1040
}
Export {
 Ordinal: 2
 Name: Forwarded
 RVA: 0x3010
}
Export {
 Ordinal: 4
 Name: Missing
 RVA: 0x0
}
'''
        base, size, exports = self.r.parse_image(text)
        self.assertEqual(base + 0x1044, 0x180001044)
        self.assertEqual(size, 0x4000)
        self.assertEqual(self.r.nearest_export(exports, 0x1044), 'Wait+0x4 [nearest export]')
        self.assertEqual(self.r.nearest_export(exports, 0x1040), 'Wait+0x0 [nearest export]')
        self.assertIsNone(self.r.nearest_export(exports, 0x1000))
        self.assertEqual(len(exports), 2)

    def test_bad_headers(self):
        with self.assertRaises(ValueError):
            self.r.parse_image('not a PE header')

    def test_export_alias_is_deterministic(self):
        exports = [(0x1040, 'NtWaitForSingleObject'), (0x1040, 'ZwWaitForSingleObject')]
        self.assertEqual(self.r.nearest_export(exports, 0x1044),
                         'NtWaitForSingleObject+0x4 [nearest export]')

    def test_symbolizer_address_and_explicit_pdb(self):
        with tempfile.TemporaryDirectory() as tmp:
            dll, pdb = Path(tmp) / 'image.dll', Path(tmp) / 'image.pdb'
            dll.touch()
            pdb.touch()
            with patch.object(self.r, 'run_llvm', side_effect=[
                    'ImageBase: 0x180000000\nSizeOfImage: 8192\n',
                    'BusyFunction\nsource.c:42:0\n']) as run:
                image = self.r.Image(dll, pdb)
                self.assertEqual(image.resolve(0x1044), 'BusyFunction')
                self.assertEqual(image.resolve(0x1044), 'BusyFunction')
                self.assertEqual(run.call_count, 2)
                self.assertEqual(run.call_args.args[-1], '0x180001044')
                self.assertIn('--pdb=' + str(pdb.resolve()), run.call_args.args)
                self.assertEqual(image.resolve(0x2000), '?? [outside image]')

    def test_ordinal_only_export_does_not_consume_rva_as_name(self):
        text = 'ImageBase: 0x180000000\nSizeOfImage: 8192\nExport {\n Ordinal: 1\n Name: \n RVA: 0x1040\n}\n'
        self.assertEqual(self.r.parse_image(text)[2], [])
