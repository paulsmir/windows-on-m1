"""Actual DDI callbacks: absent metadata must preserve public allocation size."""
from pathlib import Path
import subprocess,tempfile,unittest
from d3d12_canonical_allocation_source import render
ROOT=Path(__file__).resolve().parents[1]
class CanonicalAllocation(unittest.TestCase):
 def test_absent_metadata_query_and_existing_compose_without_underflow(self):
  source=render(ROOT/'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp',ROOT/'.local/reference/wdk26100/d3d12umddi.h',ROOT/'tests/d3d12_canonical_allocation_fixture.cpp')
  with tempfile.TemporaryDirectory(prefix='d3d12-allocation-align-') as d:
   p=Path(d)/'actual.cpp';exe=Path(d)/'test';p.write_text(source)
   r=subprocess.run(['clang++','-std=c++17','-Werror=sign-compare','-fsanitize=address,undefined',str(p),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stdout+r.stderr)
