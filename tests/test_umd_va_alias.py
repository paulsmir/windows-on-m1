"""C53 B0: tile aliases of slot allocations in reserved GPU VA ranges.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The UMD replaces a slot's
allocation at the same canonical VA (EXP1011 replace_canonical). A tile alias
of that slot in a reserved range must then be mapped again onto the fresh
allocation at the right allocation offset, or the reserved range keeps
pointing at a freed allocation. The alias table must track every map and
free exactly: a partial unmap trims or splits an alias and advances its
offset; a remap replaces the old alias; running out of entries refuses before
anything changes (the mapping is not made unrecorded). C53 B4: the caller
grows the table to Need first, and contiguous maps of one allocation merge
(a merge with a wrong offset would remap tiles onto the wrong heap pages).
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src'

TEST = r'''
#include "umd_va_alias.h"
#include <assert.h>
#include <stdio.h>
#define T 0x10000ull /* one 64 KiB tile */
#define P (T / 4096ull)
static ADMISSION_UMD_VA_ALIAS alias(unsigned long long token, unsigned long long va,
                                    unsigned long long offset_tiles, unsigned long long tiles) {
  ADMISSION_UMD_VA_ALIAS a = {token, va, offset_tiles * P, tiles * P, 1u};
  return a;
}
static const ADMISSION_UMD_VA_ALIAS *at(const ADMISSION_UMD_VA_ALIAS *t, unsigned n, unsigned long long va) {
  for (unsigned i = 0; i < n; ++i) if (t[i].Va == va) return &t[i];
  return 0;
}
int main(void) {
  ADMISSION_UMD_VA_ALIAS t[8];
  unsigned n = 0;
  const unsigned long long base = 0x400000;
  /* tiles 0..7 of the range onto tiles 10..17 of token 5 */
  ADMISSION_UMD_VA_ALIAS a = alias(5, base, 10, 8);
  assert(AdmissionUmdVaAliasAdd(t, &n, 4, &a) && n == 1);
  /* unmapping tiles 2..3 splits it; the tail starts at allocation tile 14 */
  assert(AdmissionUmdVaAliasCut(t, &n, 4, base + 2 * T, 2 * T) && n == 2);
  assert(at(t, n, base)->Pages == 2 * P && at(t, n, base)->OffsetPages == 10 * P);
  assert(at(t, n, base + 4 * T)->Pages == 4 * P && at(t, n, base + 4 * T)->OffsetPages == 14 * P);
  /* remapping tiles 1..4 onto token 9 trims the head, trims the tail */
  a = alias(9, base + T, 0, 4);
  assert(AdmissionUmdVaAliasAdd(t, &n, 4, &a) && n == 3);
  assert(at(t, n, base)->Pages == P && at(t, n, base)->Token == 5);
  assert(at(t, n, base + 5 * T)->Pages == 3 * P && at(t, n, base + 5 * T)->OffsetPages == 15 * P);
  assert(at(t, n, base + T)->Token == 9 && at(t, n, base + T)->Pages == 4 * P);
  /* a full free removes every alias inside */
  assert(AdmissionUmdVaAliasCut(t, &n, 4, base, 8 * T) && n == 0);
  /* capacity: a split that does not fit changes nothing */
  a = alias(5, base, 0, 8);
  assert(AdmissionUmdVaAliasAdd(t, &n, 1, &a) && n == 1);
  assert(!AdmissionUmdVaAliasRoom(t, n, 1, base + T, T));
  a = alias(9, base + T, 0, 1);
  assert(!AdmissionUmdVaAliasAdd(t, &n, 1, &a) && n == 1 && t[0].Pages == 8 * P);
  assert(!AdmissionUmdVaAliasCut(t, &n, 1, base + T, T) && n == 1 && t[0].Pages == 8 * P);
  /* an exact remap of the whole alias needs no extra entry */
  a = alias(9, base, 3, 8);
  assert(AdmissionUmdVaAliasAdd(t, &n, 1, &a) && n == 1 && t[0].Token == 9 && t[0].OffsetPages == 3 * P);
  /* C53 B4: Need counts the entries after an add (inside replaced, split +1) */
  n = 0;
  a = alias(5, base, 0, 8);
  assert(AdmissionUmdVaAliasAdd(t, &n, 4, &a) && n == 1);
  assert(AdmissionUmdVaAliasNeed(t, n, base + T, T) == 3);
  assert(AdmissionUmdVaAliasNeed(t, n, base, 8 * T) == 1);
  assert(AdmissionUmdVaAliasNeed(t, n, base, 0) == 0);
  assert(AdmissionUmdVaAliasCut(t, &n, 4, base, 8 * T) && n == 0);
  /* C53 B4: tiles mapped onto consecutive heap tiles merge into one alias,
   * on either side and across a gap that is filled last */
  a = alias(5, base, 10, 1);
  assert(AdmissionUmdVaAliasAdd(t, &n, 4, &a) && n == 1);
  a = alias(5, base + T, 11, 1);
  assert(AdmissionUmdVaAliasAdd(t, &n, 4, &a) && n == 1 && t[0].Pages == 2 * P);
  a = alias(5, base + 3 * T, 13, 1);
  assert(AdmissionUmdVaAliasAdd(t, &n, 4, &a) && n == 2);
  a = alias(5, base + 2 * T, 12, 1);
  assert(AdmissionUmdVaAliasAdd(t, &n, 4, &a) && n == 1);
  assert(t[0].Va == base && t[0].OffsetPages == 10 * P && t[0].Pages == 4 * P);
  /* no merge across a different allocation offset, token or protection */
  a = alias(5, base + 4 * T, 20, 1);
  assert(AdmissionUmdVaAliasAdd(t, &n, 4, &a) && n == 2);
  a = alias(6, base + 5 * T, 21, 1);
  assert(AdmissionUmdVaAliasAdd(t, &n, 4, &a) && n == 3);
  a = alias(5, base - T, 9, 1); a.Protection = 0u;
  assert(AdmissionUmdVaAliasAdd(t, &n, 4, &a) && n == 4);
  /* a cut inside the merged alias splits it with the right offsets */
  assert(AdmissionUmdVaAliasCut(t, &n, 5, base + T, T) && n == 5);
  assert(at(t, n, base)->Pages == P && at(t, n, base + 2 * T)->OffsetPages == 12 * P &&
         at(t, n, base + 2 * T)->Pages == 2 * P);
  puts("umd_va_alias: PASS");
  return 0;
}
'''


class UmdVaAlias(unittest.TestCase):
    def test_alias_table_tracks_maps_and_frees(self):
        with tempfile.TemporaryDirectory(prefix='umd-va-alias-') as tmp:
            source = Path(tmp) / 'alias.c'
            binary = Path(tmp) / 'alias'
            source.write_text(TEST)
            build = subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-fsanitize=address,undefined', '-I', str(SRC), str(source), '-o', str(binary)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('umd_va_alias: PASS', run.stdout)


if __name__ == '__main__':
    unittest.main()
