"""EXP999: upload only the 64 KiB chunks of a staging copy that changed.

EXP997 DWM: four reused 512 KiB encoder BOs were uploaded whole 559 times
(293 MB, ~0 % of sampled words non-zero) although Mesa rewrites only a few
KiB per batch; every 64 KiB costs one KMD copy escape (~0.45 ms).
Invariants (per-chunk hashes describe the canonical copy as last synced):
- an unchanged chunk is not transferred; a changed chunk is;
- a fresh slot (no valid chunk record) transfers every chunk;
- a download records the downloaded content, so an unchanged staging copy
  does not bounce back;
- a failed transfer invalidates the chunk record;
- slots larger than the chunk table fall back to whole transfers.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HDR = ROOT / 'drivers/apple-agx/render-admission/umd/include'
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'

BODY = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "umd_staging_sync.h"
#define CH 65536u
static unsigned char staging[8*CH], canonical[8*CH];
static int transfers, fail_at=-1;
/* Mirrors transfer_slot's loop: hash, skip current chunks, record after success. */
static int sync(ADMISSION_UMD_STAGING_CHUNK_SET *set, unsigned long long bytes, int download) {
  int chunked = AdmissionUmdStagingChunked(bytes, CH);
  for (unsigned long long off = 0; off < bytes; off += CH) {
    unsigned i = (unsigned)(off / CH);
    unsigned long long h = 0;
    if (!download && chunked) {
      h = AdmissionUmdStagingHash(staging + off, CH);
      if (AdmissionUmdStagingChunkCurrent(set, i, h)) continue;
    }
    if ((int)i == fail_at) { AdmissionUmdStagingChunksInvalidate(set); return 0; }
    ++transfers;
    if (download) memcpy(staging + off, canonical + off, CH); else memcpy(canonical + off, staging + off, CH);
    if (chunked) AdmissionUmdStagingChunkStore(set, i, download ? AdmissionUmdStagingHash(staging + off, CH) : h);
  }
  if (chunked) AdmissionUmdStagingChunksValidate(set);
  return 1;
}
int main(void) {
  ADMISSION_UMD_STAGING_CHUNK_SET set; memset(&set, 0, sizeof set);
  assert(sync(&set, sizeof staging, 0) && transfers == 8);            /* fresh: all */
  transfers = 0; assert(sync(&set, sizeof staging, 0) && transfers == 0); /* unchanged: none */
  staging[3*CH + 17] = 9; transfers = 0;
  assert(sync(&set, sizeof staging, 0) && transfers == 1 && canonical[3*CH + 17] == 9);
  canonical[5*CH] = 7; transfers = 0;                                 /* GPU wrote, download */
  assert(sync(&set, sizeof staging, 1) && transfers == 8 && staging[5*CH] == 7);
  transfers = 0; assert(sync(&set, sizeof staging, 0) && transfers == 0); /* no bounce */
  staging[0] = 1; staging[7*CH] = 2; fail_at = 7; transfers = 0;
  assert(!sync(&set, sizeof staging, 0) && !set.Valid);
  fail_at = -1; transfers = 0; assert(sync(&set, sizeof staging, 0) && transfers == 8);
  assert(!AdmissionUmdStagingChunked((unsigned long long)CH * (ADMISSION_UMD_STAGING_CHUNKS + 1u), CH));
  puts("EXP999 chunked upload: PASS");
  return 0;
}
'''


class ChunkedUpload(unittest.TestCase):
    def test_only_changed_chunks_transfer(self):
        self.assertIn('AdmissionUmdStagingChunkCurrent', (HDR / 'umd_staging_sync.h').read_text())
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.c'; exe = Path(tmp) / 'replay'
            src.write_text(BODY)
            built = subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-I', str(HDR),
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP999 chunked upload: PASS', ran.stdout)

    def test_transfer_slot_uses_chunk_record(self):
        text = SRC.read_text()
        body = text[text.index('static int transfer_slot('):text.index('static int transfer_held(')]
        for name in ('AdmissionUmdStagingChunked(', 'AdmissionUmdStagingChunkCurrent(',
                     'AdmissionUmdStagingChunkStore(', 'AdmissionUmdStagingChunksValidate(',
                     'AdmissionUmdStagingChunksInvalidate('):
            self.assertIn(name, body)


if __name__ == '__main__':
    unittest.main()
