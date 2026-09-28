"""Execute the private allocation decoder: canonical storage is never lockable."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import sys

ROOT = Path(__file__).resolve().parents[1]


class R145AllocationIntent(unittest.TestCase):
    def test_real_kmd_copy_lifetime_and_provenance(self):
        for profile in ('16', '64'):
            result = subprocess.run([sys.executable, str(ROOT / 'tests/g3_vidmm_replay.py')],
                cwd=ROOT, env=dict(os.environ, G3_REPLAY_R145='1', G3_REPLAY_PROFILE=profile),
                text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('R145 local copy: PASS', result.stdout)

    def test_canonical_and_staging_are_distinct_for_every_native_class(self):
        source = r'''
#include <assert.h>
#include <string.h>
#include "render_win32_transport.h"
int main(void) {
  ADMISSION_WIN32_ALLOCATION_CREATE c = {0};
  ADMISSION_ALLOCATION_DESCRIPTION d;
  unsigned cls, flags;
  c.Magic = ADMISSION_WIN32_ALLOCATION_MAGIC;
  c.Version = 2; c.Bytes = sizeof(c);
  for (unsigned k = 1; k <= 3; ++k) {
    c.ClassId = k;
    c.Flags = AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead;
    for (unsigned pages = 1; pages <= 17; pages += 4) {
      assert(AdmissionAllocationDescribe(pages * 65536, 1, 1,
          0x100, ADMISSION_WIN32_ALLOCATION_FORMAT_A8, 0, &c.Allocation));
      assert(AdmissionWin32AllocationCreateValidate(&c, sizeof(c), &d,
          &cls, &flags) == AdmissionWin32TransportSuccess);
      assert(cls == k && d.CpuVisible == 0 && d.Size == pages * 65536);
      c.Allocation.CpuVisible = 1;
      assert(AdmissionWin32AllocationCreateValidate(&c, sizeof(c), &d,
          &cls, &flags) != AdmissionWin32TransportSuccess);
    }
  }
  assert(AdmissionAllocationDescribe(65536, 1, 1,
      ADMISSION_WIN32_ALLOCATION_STAGING_CPUVISIBLE,
      ADMISSION_WIN32_ALLOCATION_FORMAT_A8, 1, &c.Allocation));
  assert(AdmissionWin32AllocationCreateValidate(&c.Allocation,
      sizeof(c.Allocation), &d, &cls, &flags) == AdmissionWin32TransportSuccess);
  assert(cls == 0 && d.CpuVisible == 1);
  c.Version = 1;
  assert(AdmissionWin32AllocationCreateValidate(&c, sizeof(c), &d,
      &cls, &flags) == AdmissionWin32TransportSuccess);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp) / 'intent.c'
            binary = Path(tmp) / 'intent'
            c.write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11',
                '-Wall', '-Wextra', '-Werror', '-ffunction-sections',
                '-I', str(ROOT / 'drivers/apple-agx/render-admission/include'),
                '-I', str(ROOT / 'drivers/apple-agx/shared/include'), str(c),
                str(ROOT / 'drivers/apple-agx/render-admission/src/render_allocation.c'),
                str(ROOT / 'drivers/apple-agx/render-admission/src/render_win32_transport.c'),
                str(ROOT / 'drivers/apple-agx/shared/src/apple_agx_win32_abi.c'),
                '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
