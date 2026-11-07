"""Opt-in source projection contract backed by the EXP1460/1462 native oracle."""
import hashlib
import os
from pathlib import Path
import runpy
import unittest

ROOT = Path(__file__).resolve().parents[1]
PRISTINE = os.environ.get('AGX_SHADER_INFO_ABI_PRISTINE')


@unittest.skipUnless(PRISTINE, 'explicit pinned Mesa header required')
class ShaderInfoProjection(unittest.TestCase):
    def setUp(self):
        self.source = Path(PRISTINE).read_bytes()
        self.assertEqual(hashlib.sha256(self.source).hexdigest(),
                         '893b5a63f7dd0fbaf1d96953451fb073e35fd38acd8be60a51528e2f37204a09')
        self.project = runpy.run_path(str(ROOT / 'drivers/apple-agx/mesa/scripts/project-shader-info-abi.py'))['project']

    def test_actual_header_emits_the_native_validated_projection(self):
        # This digest is the header measured by the independent native getters,
        # cross-TU and raw-layout oracle, not an expectation derived by project().
        output = self.project(self.source)
        self.assertEqual(hashlib.sha256(output).hexdigest(),
                         '551fc36c52943b400bca9a1c7df094f0972609a1c7ba538ce9d3205bc80188ee')

    def test_unreviewed_header_revision_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'source hash mismatch'):
            self.project(self.source + b'\n')


if __name__ == '__main__':
    unittest.main()
