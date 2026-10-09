"""EXP1091: live BOs are indexed by handle for AgxWin32AsahiLookupBo.

EXP1089 CSwitch samples put ~11 % of DWM's composition thread in
AgxWin32AsahiLookupBo walking every registered BO (one shared lock per step)
for each handle a submission references. Invariants of the index:
- every inserted BO is found by its handle until it is removed;
- a removed BO is never returned, and removal keeps every other entry
  reachable (backward-shift deletion with wrap-around, colliding handles);
- the table grows past half full without losing entries;
- AgxWin32AsahiLookupBo consults the index before the owner walk, dispose
  removes the BO before freeing it, and successful create/import insert it.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_asahi_bo.c'


def function(text, name):
    m = re.search(r'(?m)^static [A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


PROGRAM = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef unsigned APPLE_AGX_U32;
struct agx_bo { uint32_t handle; };
struct windows_bo { struct agx_bo Base; int live; };
typedef struct { void **HandleMap; APPLE_AGX_U32 HandleMapCap, HandleMapCount; } AGX_WIN32_ASAHI_BACKEND;
@@FUNCTIONS@@
static uint32_t rng = 12345u;
static uint32_t next(void) { rng = rng * 1664525u + 1013904223u; return rng; }
int main(void) {
  enum { N = 3000 };
  static struct windows_bo bos[N];
  AGX_WIN32_ASAHI_BACKEND b = {0};
  /* Handles collide in the hash on purpose: multiples of a large stride. */
  for (unsigned i = 0; i < N; ++i) bos[i].Base.handle = (i % 7u == 0u) ? (i + 1u) * 0x10000u : i * 3u + 1u;
  for (unsigned round = 0; round < 200000u; ++round) {
    unsigned i = next() % N;
    if (bos[i].live) {
      if (next() & 1u) { handle_map_remove(&b, &bos[i]); bos[i].live = 0; }
    } else {
      assert(handle_map_put(&b, &bos[i])); bos[i].live = 1;
    }
    if ((round & 1023u) == 0u) {
      unsigned live = 0;
      for (unsigned k = 0; k < N; ++k) {
        struct windows_bo *got = handle_map_get(&b, bos[k].Base.handle);
        if (bos[k].live) { assert(got == &bos[k]); ++live; }
        else assert(got == NULL);
      }
      assert(live == b.HandleMapCount && b.HandleMapCount * 2u <= b.HandleMapCap);
    }
  }
  /* Removing an absent BO is a no-op. */
  for (unsigned k = 0; k < N; ++k) if (!bos[k].live) { unsigned c = b.HandleMapCount; handle_map_remove(&b, &bos[k]); assert(c == b.HandleMapCount); break; }
  free(b.HandleMap);
  puts("PASS");
  return 0;
}
'''


class BoHandleMap(unittest.TestCase):
    def test_index_matches_a_reference_model(self):
        text = SRC.read_text()
        functions = '\n'.join(function(text, n) for n in (
            'handle_slot', 'handle_map_place', 'handle_map_put', 'handle_map_remove', 'handle_map_get'))
        self.assertIn('handle_map_remove(', functions)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'map.c'; exe = Path(directory) / 'map'
            path.write_text(PROGRAM.replace('@@FUNCTIONS@@', functions))
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O1', '-Wall', '-Wextra',
                                    '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                                    str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_wiring(self):
        text = SRC.read_text()
        lookup = text[text.index('struct agx_bo *AgxWin32AsahiLookupBo('):]
        self.assertLess(lookup.index('handle_map_get(b,handle)'), lookup.index('b->Ops.NextBo('))
        dispose = function(text, 'dispose')
        self.assertLess(dispose.index('handle_map_remove(b,bo);'), dispose.index('free(bo);'))
        self.assertEqual(text.count('(void)handle_map_put(b,bo);'), 2)


if __name__ == '__main__':
    unittest.main()
