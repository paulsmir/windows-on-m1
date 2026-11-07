"""rc76: the batch-submit perf receipt names its pass kind without digits.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The D3D12 owner's
measure-perf-reason table folds every digit to '#' (agx_d3d12_windows.cpp
owner_perf_note), so EXP2000's "submit ####x### d# c# z#" rows could not tell
a clear-only render pass (0 draws, clear bits) from a one-draw pass. The
submit text must carry the draws bucket as a word and the clear/load masks
and depth as letters, and every distinct mask must give a distinct text.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LIFECYCLE = ROOT / 'drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py'

PROGRAM = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define PIPE_CLEAR_DEPTH (1 << 0)
#define PIPE_CLEAR_STENCIL (1 << 1)
#define PIPE_CLEAR_COLOR0 (1 << 2)
#define PIPE_MAX_COLOR_BUFS 8
struct agx_batch { unsigned clear, load; };
static void letters(const struct agx_batch *batch, char *c, char *l) {
@@LETTERS@@
   strcpy(c, clear_letters);
   strcpy(l, load_letters);
}
int main(void) {
  char c[11], l[11], seen[1024][24];
  struct agx_batch b = {PIPE_CLEAR_COLOR0 | PIPE_CLEAR_DEPTH, (0xfu << 2) | PIPE_CLEAR_DEPTH};
  letters(&b, c, l);
  assert(!strcmp(c, "Za") && !strcmp(l, "bcd"));
  b.clear = 0; b.load = 0;
  letters(&b, c, l);
  assert(!strcmp(c, "-") && !strcmp(l, "-"));
  for (unsigned m = 0; m < 1024; ++m) {
    b.clear = m; b.load = 0;
    letters(&b, c, l);
    for (const char *p = c; *p; ++p) assert(*p < '0' || *p > '9');
    strcpy(seen[m], c);
    for (unsigned k = 0; k < m; ++k) assert(strcmp(seen[k], c));
  }
  return 0;
}
'''


class SubmitReceiptLetters(unittest.TestCase):
    def test_masks_are_letters_and_draws_words(self):
        source = LIFECYCLE.read_text()
        start = source.index('   char clear_letters[11], load_letters[11];')
        end = source.index('   perf_debug_ctx(ctx, "submit %ux%u d%s c%s l%s z%s"', start)
        call = source[end:source.index(');', end)]
        for word in ('"none"', '"one"', '"few"', '"tens"', '"many"', '"y" : "n"'):
            self.assertIn(word, call)
        with tempfile.TemporaryDirectory(prefix='rc76-letters-') as tmp:
            path = Path(tmp) / 'letters.c'
            exe = Path(tmp) / 'letters'
            path.write_text(PROGRAM.replace('@@LETTERS@@', source[start:end]))
            build = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-fsanitize=address,undefined', str(path), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == '__main__':
    unittest.main()
