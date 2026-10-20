"""Check actual CPU library wire digests using macOS system SHA-256."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class WireDigestTest(unittest.TestCase):
    def test_digests_and_compatibility(self):
        with tempfile.TemporaryDirectory() as td:
            exe=Path(td)/'wire-digest'
            build=subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
                '-fno-omit-frame-pointer','-I'+str(ROOT/'drivers/apple-agx/d3d12'),
                str(ROOT/'tests/d3d12_library_wire_digest_fixture.cpp'),'-o',str(exe)],
                capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(run.returncode,0,run.stderr)
            self.assertIn('WIRE_DIGEST_PASS',run.stdout)
            self.assertEqual(run.stderr,'')
