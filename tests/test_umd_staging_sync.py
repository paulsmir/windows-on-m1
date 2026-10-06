"""EXP977: a staged slot is re-uploaded only when its staging bytes changed.

Invariant: after a successful upload or download the canonical GPU-local
allocation equals the staging copy, so an upload may be skipped exactly when the
staging bytes (and size) are unchanged since that synchronization. Any change,
a size change or an invalidated record must force the upload.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
UMD = ROOT / 'drivers/apple-agx/render-admission/umd'

TEST = r'''
#include "umd_staging_sync.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
  ADMISSION_UMD_STAGING_SYNC s = {0};
  unsigned char *buf = calloc(1, 0x10007);
  for (unsigned i = 0; i < 0x10007; ++i) buf[i] = (unsigned char)(i * 37u + 5u);
  unsigned long long h = AdmissionUmdStagingHash(buf, 0x10007);
  /* Never synchronized: upload. */
  assert(AdmissionUmdStagingUploadNeeded(&s, h, 0x10007));
  AdmissionUmdStagingRecord(&s, h, 0x10007);
  /* Unchanged bytes: skip. */
  assert(!AdmissionUmdStagingUploadNeeded(&s, AdmissionUmdStagingHash(buf, 0x10007), 0x10007));
  /* Any single changed byte, in the word body or the tail, forces upload. */
  for (unsigned at = 0; at < 0x10007; at += 0x1001) {
    buf[at] ^= 0x40u;
    assert(AdmissionUmdStagingUploadNeeded(&s, AdmissionUmdStagingHash(buf, 0x10007), 0x10007));
    buf[at] ^= 0x40u;
  }
  buf[0x10006] ^= 1u;
  assert(AdmissionUmdStagingUploadNeeded(&s, AdmissionUmdStagingHash(buf, 0x10007), 0x10007));
  buf[0x10006] ^= 1u;
  /* Same bytes, different extent: upload. */
  assert(AdmissionUmdStagingUploadNeeded(&s, AdmissionUmdStagingHash(buf, 0x10000), 0x10000));
  /* Invalidation (failed/aborted transfer) forces upload. */
  AdmissionUmdStagingInvalidate(&s);
  assert(AdmissionUmdStagingUploadNeeded(&s, h, 0x10007));
  free(buf);
  puts("EXP977 staging sync: PASS");
  return 0;
}
'''


class StagingSync(unittest.TestCase):
    def test_upload_skipped_only_for_unchanged_staging(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 't.c'
            exe = Path(tmp) / 't'
            src.write_text(TEST)
            built = subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-fsanitize=address,undefined', '-I', str(UMD / 'include'),
                                    str(src), '-o', str(exe)], text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP977 staging sync: PASS', ran.stdout)

    def test_transfer_slot_skips_only_uploads_and_records_after_success(self):
        text = (UMD / 'src/umd_gpuva_windows.c').read_text()
        body = text[text.index('static int transfer_slot('):text.index('static int transfer_held(')]
        # The skip is evaluated for uploads only; downloads always run.
        self.assertRegex(body, r'if\(!download\) \{\s*int unchanged=staging_unchanged\(device,slot\);')
        # A transfer that starts invalidates the record before any escape.
        self.assertLess(body.index('AdmissionUmdStagingInvalidate(&slot->Sync);'),
                        body.index('copy_escape(device,payload)'))
        # The record is refreshed only on success, after the chunk loop.
        record = body.index('AdmissionUmdStagingRecord(&slot->Sync,')
        self.assertGreater(record, body.index('offset+=count;'))
        self.assertTrue(re.search(r'if\(success\)\s*AdmissionUmdStagingRecord', body))


if __name__ == '__main__':
    unittest.main()
