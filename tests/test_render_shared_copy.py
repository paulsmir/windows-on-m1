"""EXP1099: per-submission template copies into firmware-shared memory.

The firmware-shared objects are mapped MmNonCached (Device memory on ARM64),
so the per-job copies must produce the same bytes as before with fewer
stores, and must not issue a misaligned wide access. Invariants:
- copy_shared_bytes reproduces memcpy for every source/destination
  alignment and length, including lengths shorter than the alignment head;
- every 8-byte access it issues is naturally aligned on both sides;
- bind_relocation_objects and copy_bytes copy through it, with no remaining
  byte-at-a-time copy loop.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/shared/src/apple_agx_render_shared_memory.c'


def function(text, name):
    m = re.search(r'(?m)^static [A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


PROGRAM = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned APPLE_AGX_U32; typedef unsigned long long APPLE_AGX_U64;
static unsigned wide, misaligned;
/* Count the 8-byte accesses through the volatile casts. */
#define volatile
@@FUNCTION@@
#undef volatile
int main(void) {
  static unsigned char src[4200], dst[4200], ref[4200];
  for (unsigned i = 0; i < sizeof(src); ++i) src[i] = (unsigned char)(i * 131u + 7u);
  for (unsigned so = 0; so < 16; ++so)
    for (unsigned d0 = 0; d0 < 16; ++d0)
      for (unsigned n = 0; n < 4096; n += (n < 80 ? 1 : 397)) {
        memset(dst, 0xee, sizeof(dst)); memset(ref, 0xee, sizeof(ref));
        copy_shared_bytes(dst + d0, src + so, n);
        memcpy(ref + d0, src + so, n);
        assert(!memcmp(dst, ref, sizeof(dst)));
      }
  copy_shared_bytes(dst + 8, src + 8, 4096);
  assert(!memcmp(dst + 8, src + 8, 4096));
  puts("PASS");
  return 0;
}
'''


class RenderSharedCopy(unittest.TestCase):
    def test_copy_matches_memcpy(self):
        text = SRC.read_text()
        body = function(text, 'copy_shared_bytes')
        # Instrument the 8-byte store: alignment of both sides.
        loop = 'for (; Bytes - i >= 8u; i += 8u)'
        self.assertIn(loop, body)
        body = body.replace(loop, 'for (; Bytes - i >= 8u; misaligned += (((APPLE_AGX_U64)(Destination + i) | '
                                  '(APPLE_AGX_U64)(Source + i)) & 7ULL) != 0ULL, ++wide, i += 8u)')
        program = PROGRAM.replace('@@FUNCTION@@', body).replace('puts("PASS");', 'assert(wide && !misaligned); puts("PASS");')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'copy.c'; exe = Path(directory) / 'copy'
            path.write_text(program)
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O1', '-Wall', '-Wextra',
                                    '-Werror', '-Wno-unused-function', '-fno-strict-aliasing',
                                    str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_hot_copies_use_it(self):
        text = SRC.read_text()
        bind = function(text, 'bind_relocation_objects')
        self.assertIn('copy_shared_bytes(destination, source, layouts[index].Size);', bind)
        self.assertNotIn('destination[byte] = source[byte];', bind)
        self.assertIn('copy_shared_bytes(Destination,Source,Bytes);', function(text, 'copy_bytes'))


if __name__ == '__main__':
    unittest.main()
