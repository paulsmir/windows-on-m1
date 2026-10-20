"""Exercise the original CPU-only pipeline-library arena envelope under sanitizers."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class WireEnvelopeTest(unittest.TestCase):
    def test_exact_envelope_and_bounds(self):
        with tempfile.TemporaryDirectory() as td:
            exe=Path(td)/'wire-envelope'
            cmd=['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
                 '-fno-omit-frame-pointer','-I'+str(ROOT/'drivers/apple-agx/d3d12'),
                 str(ROOT/'tests/d3d12_library_wire_envelope_fixture.cpp'),'-o',str(exe)]
            build=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stderr)
            self.assertIn('WIRE_ENVELOPE_PASS',run.stdout)
            self.assertEqual(run.stderr,'')
