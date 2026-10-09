"""EXP1102: the J313 GPU DVFS floor patches exactly the base/min P-state fields.

EXP1101 receipts: every DWM job ran at HwDataA actual_pstate 1 (396 MHz).
Invariants:
- in the m1n1-derived HwDataA profile, the patched offsets hold the base and
  minimum performance-state fields (u32 100 at 0x44/0x7d8/0x7e0/0x81c/0x54,
  float 100.0 at 0x860), so the offsets are the fields m1n1 names;
- the floor writes the requested scaled value to exactly those six words
  (min <= base holds) and leaves every other byte of the profile unchanged;
- invalid inputs (misaligned, short, out-of-range scale) are refused;
- the KMD applies it right after materializing HwDataA and fails closed.
"""
from pathlib import Path
import os, subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]
INC = ROOT / 'drivers/apple-agx/shared/include'

PROGRAM = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_hwdata_profile.h"
int main(void) {
  static unsigned int a_words[(AGX_HWDATA_A_BYTES + 3u) / 4u + 4u];
  unsigned char *a = (unsigned char *)a_words, before[AGX_HWDATA_A_BYTES];
  static const unsigned int offsets[6] = {0x44u, 0x7d8u, 0x7e0u, 0x81cu, 0x54u, 0x860u};
  union { float f; unsigned int u; } h; h.f = 100.0f;
  memcpy(a, AgxHwdataAProfile, AGX_HWDATA_A_BYTES);
  for (unsigned i = 0; i < 5; ++i) { unsigned int v; memcpy(&v, a + offsets[i], 4); assert(v == 100u); }
  { unsigned int v; memcpy(&v, a + 0x860u, 4); assert(v == h.u); }
  memcpy(before, a, sizeof(before));
  assert(!AgxHwdataApplyJ313DvfsFloor(a + 1, AGX_HWDATA_A_BYTES, 300u));
  assert(!AgxHwdataApplyJ313DvfsFloor(a, AGX_HWDATA_A_BYTES - 1u, 300u));
  assert(!AgxHwdataApplyJ313DvfsFloor(a, AGX_HWDATA_A_BYTES, 250u));
  assert(!AgxHwdataApplyJ313DvfsFloor(a, AGX_HWDATA_A_BYTES, 700u));
  assert(!memcmp(before, a, sizeof(before)));
  assert(AgxHwdataApplyJ313DvfsFloor(a, AGX_HWDATA_A_BYTES, AGX_HWDATA_J313_DVFS_FLOOR_SCALED));
  h.f = (float)AGX_HWDATA_J313_DVFS_FLOOR_SCALED;
  for (unsigned i = 0; i < AGX_HWDATA_A_BYTES; ++i) {
    int patched = 0;
    for (unsigned k = 0; k < 6; ++k) if (i >= offsets[k] && i < offsets[k] + 4u) patched = 1;
    if (!patched) assert(a[i] == before[i]);
  }
  for (unsigned i = 0; i < 5; ++i) { unsigned int v; memcpy(&v, a + offsets[i], 4); assert(v == AGX_HWDATA_J313_DVFS_FLOOR_SCALED); }
  { unsigned int v; memcpy(&v, a + 0x860u, 4); assert(v == h.u); }
  puts("PASS");
  return 0;
}
'''


class HwdataDvfsFloor(unittest.TestCase):
    def test_floor_patches_only_the_pstate_fields(self):
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / 'floor.c'; exe = Path(d) / 'floor'
            src.write_text(PROGRAM)
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                                    '-Wno-unused-function', '-I', str(INC), str(src), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_kmd_applies_it_after_materialize(self):
        text = (ROOT / 'drivers/apple-agx/render-admission/src/backend_platform_windows.c').read_text()
        mat = text.index('valid=AgxHwdataMaterialize(&runtime->HwdataProfileReceipt')
        floor = text.index('valid=AgxHwdataApplyJ313DvfsFloor(hwdata_a->CpuAddress,hwdata_a->Length,')
        record = text.index('AdmissionRecordHwdataProfile(runtime->Adapter,valid?0u:2u', mat)
        self.assertLess(mat, floor); self.assertLess(floor, record)


if __name__ == '__main__':
    unittest.main()
