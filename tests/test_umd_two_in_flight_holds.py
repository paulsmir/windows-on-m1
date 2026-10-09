"""EXP1093: slot holds and uploads with two submissions in flight.

With the winsys keeping the newest submission in flight while the next one
is built and submitted, a screen-buffer slot may be named by two in-flight
residency sets. Invariants:
- each retired (or rolled-back) set drops exactly one submission hold, so a
  slot named by two sets stays held until both retire, and a hold never
  underflows;
- a queued submission drops CopyHeld (only the submission being built holds
  it) and records the fence the slot was last used with;
- a slot may join a new submission while the one other in-flight set holds
  it, never while two do;
- an upload into a slot that the other in-flight submission still holds
  waits for that submission's fence before the copy overwrites the canonical
  copy; a download, an unchanged slot or an unheld slot does not wait.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'


def function(text, name):
    m = re.search(r'(?m)^static [A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


BODY = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
typedef int BOOL; typedef unsigned UINT;
#define TRUE 1
#define FALSE 0
struct ADMISSION_UMD_SCREEN_BUFFER { BOOL Active, CopyHeld; uint64_t Token; UINT SubmissionHolds; };
struct ADMISSION_UMD_DEVICE { int ScreenBufferLock; ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[3]; UINT ScreenBufferHighWater; };
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device) ((Device)->ScreenBufferHighWater)
static void AcquireSRWLockExclusive(int *) {}
static void ReleaseSRWLockExclusive(int *) {}
@@FUNCS@@
int main() {
  ADMISSION_UMD_DEVICE d = {};
  d.ScreenBufferHighWater = 3;
  for (UINT i = 0; i < 3; ++i) { d.ScreenBuffers[i].Active = TRUE; d.ScreenBuffers[i].Token = 10 + i; }
  /* Slot 10 is named by the older set {10, 11} and the newest set {10, 12}. */
  d.ScreenBuffers[0].SubmissionHolds = 2; d.ScreenBuffers[1].SubmissionHolds = 1;
  d.ScreenBuffers[2].SubmissionHolds = 1; d.ScreenBuffers[2].CopyHeld = TRUE;
  const uint64_t older[2] = {10, 11}, newest[2] = {10, 12}, absent[1] = {99};
  release_copies(&d, older, 2);
  assert(d.ScreenBuffers[0].SubmissionHolds == 1 && d.ScreenBuffers[1].SubmissionHolds == 0);
  assert(d.ScreenBuffers[2].SubmissionHolds == 1 && d.ScreenBuffers[2].CopyHeld);
  release_copies(&d, absent, 1);
  release_copies(&d, newest, 2);
  for (UINT i = 0; i < 3; ++i) assert(!d.ScreenBuffers[i].SubmissionHolds && !d.ScreenBuffers[i].CopyHeld);
  /* A second release of the same set never underflows. */
  release_copies(&d, newest, 2);
  assert(!d.ScreenBuffers[0].SubmissionHolds && !d.ScreenBuffers[2].SubmissionHolds);
  puts("EXP1093 holds: PASS");
  return 0;
}
'''


class TwoInFlightHolds(unittest.TestCase):
    def test_each_retired_set_drops_one_hold(self):
        text = SRC.read_text()
        funcs = '\n'.join(function(text, n) for n in ('find_slot', 'release_copies'))
        self.assertIn('release_copies(', funcs)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'holds.cpp'; exe = Path(tmp) / 'holds'
            src.write_text(BODY.replace('@@FUNCS@@', funcs))
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP1093 holds: PASS', ran.stdout)

    def test_joining_and_upload_wait(self):
        text = SRC.read_text()
        flat = lambda s: re.sub(r'\s+', '', s)
        resident = flat(function(text, 'make_resident'))
        self.assertIn('slot->SubmissionHolds<=1u', resident)
        self.assertIn('slot->CopyHeld=TRUE;++slot->SubmissionHolds;', resident)
        transfer = flat(function(text, 'transfer_slot'))
        wait = transfer.index('if(!download&&slot->SubmissionHolds>1u&&slot->LastUseFence&&'
                              '!wait_object(device,device->RenderSyncObject,slot->LastUseFence))return0;')
        self.assertLess(transfer.index('if(unchanged)return1;'), wait)
        self.assertLess(wait, transfer.index('AdmissionUmdStagingInvalidate(&slot->Sync);'))
        self.assertLess(wait, transfer.index('APPLE_AGX_G3_COPY_REQUEST'))
        submit = flat(function(text, 'submit'))
        after = submit[submit.index('finish_submission('):]
        self.assertIn('if(finished!=1)returnfinished;', after)
        self.assertLess(after.index('if(finished!=1)returnfinished;'),
                        after.index('if(slot->CopyHeld){slot->CopyHeld=FALSE;slot->LastUseFence=*fence;}'))


if __name__ == '__main__':
    unittest.main()
