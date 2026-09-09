from pathlib import Path
import importlib.util
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / '.local/reference/mesa'


class FrontendPrepareTests(unittest.TestCase):
    def test_derived_source_is_reproducible_and_cannot_overwrite_control(self):
        spec = importlib.util.spec_from_file_location('prepare', ROOT / 'tools/prepare_apple_agx_d3d10_frontend.py')
        tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(tool)
        with tempfile.TemporaryDirectory() as tmp:
            first, second = Path(tmp) / 'first', Path(tmp) / 'second'
            a = tool.prepare(SOURCE, first)
            b = tool.prepare(SOURCE, second)
            self.assertEqual(a, b)
            self.assertEqual((first / 'Device.cpp').read_bytes(), (second / 'Device.cpp').read_bytes())
            with self.assertRaisesRegex(ValueError, 'already exists'):
                tool.prepare(SOURCE, first)
            with self.assertRaisesRegex(ValueError, 'pinned source tree'):
                tool.prepare(SOURCE, SOURCE / 'invalid-derived-output')
            with self.assertRaisesRegex(ValueError, 'anchor mismatch'):
                tool.transform('State.h', 'not the expected native header')


if __name__ == '__main__':
    unittest.main()
