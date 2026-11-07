"""Exercise bounded pipeline-library table structure using the actual header."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class WireTablesTest(unittest.TestCase):
    def test_ranges_order_and_zeroes(self):
        with tempfile.TemporaryDirectory() as td:
            exe=Path(td)/'wire-tables'
            build=subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
                '-fno-omit-frame-pointer','-I'+str(ROOT/'drivers/apple-agx/d3d12'),
                str(ROOT/'tests/d3d12_library_wire_tables_fixture.cpp'),'-o',str(exe)],
                capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stderr)
            self.assertIn('WIRE_TABLE_PASS',run.stdout)
            self.assertEqual(run.stderr,'')
