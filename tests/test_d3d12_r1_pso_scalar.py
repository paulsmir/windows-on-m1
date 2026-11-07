"""Check original typed R1 PSO scalar snapshot without runtime pointer bytes."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class PsoScalarTest(unittest.TestCase):
    def test_semantic_fields_and_no_handles(self):
        with tempfile.TemporaryDirectory() as td:
            exe=Path(td)/'pso-scalar'
            build=subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
                '-fno-omit-frame-pointer','-I'+str(ROOT/'drivers/apple-agx/d3d12'),
                str(ROOT/'tests/d3d12_r1_pso_scalar_fixture.cpp'),'-o',str(exe)],
                capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stderr)
            self.assertIn('PSO_SCALAR_PASS',run.stdout)
            self.assertEqual(run.stderr,'')
