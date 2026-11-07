"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Briefly waking threads must not have the same weight as busy threads; site
ranking and percentages must use 64-bit cycle deltas, retaining raw counts.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SamplerTests(unittest.TestCase):
    def test_weighted_sites(self):
        source = (ROOT / 'drivers/apple-agx/windows/icd/agx_sampler.c').read_text()
        structs = source[source.index('typedef struct'):source.index('/* Cycle time')]
        funcs = source[source.index('static void count_site'):source.index('static DWORD find_pid')]
        printing = source[source.index('static void print_sites'):source.index('int main')]
        harness = '''#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
typedef unsigned long DWORD;
typedef void *HANDLE;
typedef unsigned long long ULONGLONG;
#define MAX_THREADS 512
#define MAX_MODULES 512
#define MAX_SITES 65536
''' + structs + funcs + printing + '''
int main(void) {
  count_site(pc_sites, &pc_count, -1, 1, 1);
  count_site(pc_sites, &pc_count, -1, 1, 1);
  count_site(pc_sites, &pc_count, -1, 2, 6000000000ULL);
  print_sites(stdout, "PC", pc_sites, pc_count, 6000000002ULL);
  assert(pc_sites[0].rva == 2 && pc_sites[0].count == 1);
  assert(pc_sites[0].cycles == 6000000000ULL);
  assert(pc_sites[1].count == 2 && pc_sites[1].cycles == 2);
  print_sites(stdout, "LR", lr_sites, lr_count, 0);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'test.c'
            src.write_text(harness)
            result = subprocess.run(['cc', str(src), '-o', tmp + '/test'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([tmp + '/test'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('CYCLES 6000000000 PCT 100.000', result.stdout)
