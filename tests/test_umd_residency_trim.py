"""M2-f11 (EXP1271): a residency request refused for budget trims idle slots.

Bug caught: under memory pressure pfnMakeResidentCb returned a failure and the
UMD refused the batch at once (EXP1271 pressure step: reject-batch at
agx_win32_gpuva.c line 156, device removed), although Microsoft
(PFND3DDDI_MAKERESIDENTCB) requires the driver to evict NumBytesToTrim bytes of
its residency list and retry until nothing more can be trimmed.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

Invariants: only idle GPU-local or SystemDirect slots lose their residency reference (never a
slot a submission, canonical CPU map, shared owner or presentation still uses,
never a CPU-mapped SystemDirect BO); T13 private CPU storage can survive
canonical eviction; least recently used first; enough bytes to
cover the request, at least one slot; nothing when nothing is eligible.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
UMD = ROOT / 'drivers/apple-agx/render-admission/umd'

TEST = r'''
#include "umd_residency_trim.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static ADMISSION_UMD_TRIM_SLOT idle(unsigned long long bytes, unsigned long long fence) {
  ADMISSION_UMD_TRIM_SLOT s;
  memset(&s, 0, sizeof(s));
  s.Active = 1; s.Resident = 1; s.KernelAllocation = 0x40000000u;
  s.Bytes = bytes; s.LastUseFence = fence;
  return s;
}
int main(void) {
  ADMISSION_UMD_TRIM_SLOT s[12];
  unsigned chosen[12], n;
  unsigned long long covered;
  /* Every exclusion, each on an otherwise idle slot. */
  for (unsigned i = 0; i < 12; ++i) s[i] = idle(32u << 20, 100u + i);
  s[0].Active = 0;          s[1].Resident = 0;     s[2].Transition = 1;
  s[3].CopyHeld = 1;        s[4].Mapped = 1;       s[5].Direct = 1;
  s[6].Bytes = 0;    s[7].Borrowed = 1;     s[8].KmtResidencyHeld = 1;
  s[9].SubmissionHolds = 1; s[10].SourceHolds = 1; s[11].KernelAllocation = 0;
  for (unsigned i = 0; i < 12; ++i) assert(!AdmissionUmdTrimEligible(&s[i]));
  covered = 7;
  assert(AdmissionUmdTrimChoose(s, 12, 1u << 20, chosen, &covered) == 0 && covered == 0);
  /* SystemDirect is evictable only between CPU locks / ownership holds. */
  s[0] = idle(64ull << 20, 1); s[0].SystemDirect = 1;
  assert(AdmissionUmdTrimChoose(s, 1, 32ull << 20, chosen, &covered) == 1);
  assert(chosen[0] == 0 && covered == (64ull << 20));
  int *blocked[] = { &s[0].Mapped, &s[0].Transition, &s[0].CopyHeld,
                    &s[0].Direct, &s[0].Borrowed, &s[0].KmtResidencyHeld };
  for (unsigned i=0; i<sizeof(blocked)/sizeof(blocked[0]); ++i) {
    *blocked[i]=1; assert(!AdmissionUmdTrimEligible(&s[0])); *blocked[i]=0;
  }
  s[0].SubmissionHolds=1; assert(!AdmissionUmdTrimEligible(&s[0]));
  s[0].SubmissionHolds=0;s[0].SourceHolds=1;assert(!AdmissionUmdTrimEligible(&s[0]));
  /* Least recently used first; ties keep slot order; stop once covered. */
  for (unsigned i = 0; i < 6; ++i) s[i] = idle(16u << 20, 0);
  s[0].LastUseFence = 50; s[1].LastUseFence = 10; s[2].LastUseFence = 30;
  s[3].LastUseFence = 10; s[4].LastUseFence = 90; s[5].LastUseFence = 20;
  s[3].SubmissionHolds = 2;   /* in flight: never chosen */
  n = AdmissionUmdTrimChoose(s, 6, 40u << 20, chosen, &covered);
  assert(n == 3 && chosen[0] == 1 && chosen[1] == 5 && chosen[2] == 2);
  assert(covered == (48ull << 20));
  /* An exact cover stops there. */
  n = AdmissionUmdTrimChoose(s, 6, 32u << 20, chosen, &covered);
  assert(n == 2 && covered == (32ull << 20));
  /* NumBytesToTrim 0 still makes progress with one slot. */
  n = AdmissionUmdTrimChoose(s, 6, 0, chosen, &covered);
  assert(n == 1 && chosen[0] == 1 && covered == (16ull << 20));
  /* More than everything: all eligible, oldest first. */
  n = AdmissionUmdTrimChoose(s, 6, 1ull << 40, chosen, &covered);
  assert(n == 5 && chosen[4] == 4 && covered == (80ull << 20));
  puts("M2-f11 residency trim: PASS");
  return 0;
}
'''


class UmdResidencyTrim(unittest.TestCase):
    def test_trim_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'trim.c'
            binary = Path(directory) / 'trim'
            source.write_text(TEST)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                            '-Wextra', '-Werror', '-I', str(UMD / 'include'),
                            str(source), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], check=True,
                                    capture_output=True, text=True)
            self.assertIn('PASS', result.stdout)

    def test_refused_residency_trims_and_retries(self):
        """make_resident must trim and retry on E_OUTOFMEMORY before it
        gives up, and the CPU download path must restore residency first."""
        text = (UMD / 'src/umd_gpuva_windows.c').read_text()

        def section(start, end):
            self.assertTrue(start in text, 'missing ' + start)
            part = text[text.index(start):]
            return part[:part.index(end)]

        self.assertTrue('#include "umd_residency_trim.h"' in text, 'header not included')
        body = section('static int make_resident(', '\nstatic int wait_object(')
        self.assertTrue('residency_request(' in body, 'make_resident bypasses the trim loop')
        helper = section('static HRESULT residency_request(', '\n}\n')
        for needle in ('E_OUTOFMEMORY', 'NumBytesToTrim', 'trim_residency(',
                       'ADMISSION_UMD_TRIM_ROUNDS'):
            self.assertTrue(needle in helper, 'residency_request lacks ' + needle)
        prepare = section('int AdmissionUmdGpuvaPrepareCpuMap(', '\n}\n')
        self.assertTrue('restore_residency(' in prepare,
                        'CPU download of a trimmed slot skips residency')

RECENCY = r"""
#include "umd_residency_trim.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  ADMISSION_UMD_TRIM_SLOT s[3];
  unsigned chosen[3];
  unsigned long long covered;
  memset(s, 0, sizeof(s));
  for (unsigned i = 0; i < 3; ++i) {
    s[i].Active = 1; s[i].Resident = 1; s[i].KernelAllocation = 0x40000000u + i;
    s[i].Bytes = 128ull << 20;
  }
  /* Slot 0: drawn at fence 40, idle since. Slot 1: just created and made
   * resident for its upload (never submitted) while fence 41 was next.
   * Slot 2: drawn at 45 (after slot 1's residency). */
  s[0].LastUseFence = AdmissionUmdTrimRecency(40, 12);
  s[1].LastUseFence = AdmissionUmdTrimRecency(0, 41);
  s[2].LastUseFence = AdmissionUmdTrimRecency(45, 41);
  assert(AdmissionUmdTrimChoose(s, 3, 1, chosen, &covered) == 1 && chosen[0] == 0);
  assert(AdmissionUmdTrimChoose(s, 3, 200ull << 20, chosen, &covered) == 2);
  assert(chosen[0] == 0 && chosen[1] == 1);
  puts("rc86 trim recency: PASS");
  return 0;
}
"""


class UmdTrimRecency(unittest.TestCase):
    def test_new_residency_is_not_least_recent(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        rc86 (EXP2015b: 124 trims, 787 MiB, in GravityMark's first frame):
        LastUseFence is 0 for a slot created and made resident but not yet
        submitted, so the LRU trim chose exactly the resources the app was
        about to upload into or draw with; their restore then evicted the
        next new one. A slot made resident after its last submission must
        rank after every merely idle slot, and every residency site must
        stamp it.
        """
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'recency.c'
            binary = Path(directory) / 'recency'
            source.write_text(RECENCY)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                            '-Wextra', '-Werror', '-I', str(UMD / 'include'),
                            str(source), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], check=True,
                                    capture_output=True, text=True)
            self.assertIn('PASS', result.stdout)
        text = (UMD / 'src/umd_gpuva_windows.c').read_text()
        import re
        sites = [m.start() for m in re.finditer(r'Resident\s*=\s*TRUE', text)]
        self.assertTrue(sites, 'no residency site found')
        for at in sites:
            window = text[at:at + 160]
            self.assertIn('TrimRecency=device->NextRenderFence+1u', window,
                          'residency site without a trim recency stamp: ' +
                          text[max(0, at - 80):at + 40])
        self.assertIn('AdmissionUmdTrimRecency(slot->LastUseFence,slot->TrimRecency)',
                      text, 'trim view ignores the residency recency')


if __name__ == '__main__':
    unittest.main()
