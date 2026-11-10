"""C-a: a pending RC export must always have an export that clears it.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
RcExportPending bypasses the two-second limit of the job-timing export and is
cleared only by the RC export (RcReceiptEnabled) or the drill receipt
(Depth2Drill.Mode). The OV2 and OV5-waiver note sites set it with both off, so
with Wom1JobDiagnostics every later job opened the device key and wrote the
registry for the rest of the boot. Invariant, with the real
function compiled: any number of export requests with both receipts off
leaves the flag clear (so the two-second limit of the export holds); no site
sets the flag except through the guarded request.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/src/backend_platform_windows.c'


def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start); depth = 1; end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[start:end]


HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
typedef void VOID;
#define TRUE 1
typedef struct {
  int JobDiagnostics, RcReceiptEnabled, RcExportPending;
  struct { unsigned Mode; } Depth2Drill;
} ADMISSION_PLATFORM_RUNTIME;
@@FUNC@@
int main() {
  ADMISSION_PLATFORM_RUNTIME r; memset(&r, 0, sizeof(r));
  r.JobDiagnostics = 1;
  /* what an OV2 / OV5-waiver note did on every refused kick */
  for (int i = 0; i < 15385; ++i) { AdmissionRcExportRequest(&r); assert(!r.RcExportPending); }
  r.RcReceiptEnabled = 1; AdmissionRcExportRequest(&r); assert(r.RcExportPending);
  r.RcExportPending = 0; r.RcReceiptEnabled = 0;
  r.Depth2Drill.Mode = 1; AdmissionRcExportRequest(&r); assert(r.RcExportPending);
  puts("rc export pending invariant: PASS");
}
'''


class RcExportPendingInvariant(unittest.TestCase):
    def test_request_without_a_clearing_export_is_dropped(self):
        text = SRC.read_text()
        harness = HARNESS.replace('@@FUNC@@', function(text, 'static VOID AdmissionRcExportRequest('))
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'pending.cpp'; exe = Path(tmp) / 'pending'
            src.write_text(harness)
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Werror', '-Wno-unused-function',
                                    '-Wno-unused-variable', '-fsanitize=address,undefined',
                                    str(src), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('PASS', ran.stdout)

    def test_only_the_guarded_request_sets_the_flag(self):
        text = SRC.read_text()
        sets = re.findall(r'RcExportPending\s*=\s*TRUE', text)
        self.assertEqual(len(sets), 1)
        self.assertIn('RcExportPending = TRUE', function(text, 'static VOID AdmissionRcExportRequest('))
        # the note sites keep their own receipts and do not request an RC export
        for call in re.finditer(r'AdmissionJobWindowOv(?:2Note|5WaiverNote)\(&runtime->', text):
            end = text.index(';', call.start())
            self.assertNotIn('AdmissionRcExportRequest', text[call.start():end + 60])


if __name__ == '__main__':
    unittest.main()
