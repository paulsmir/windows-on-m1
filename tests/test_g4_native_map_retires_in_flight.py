"""EXP1082: the first CPU map of a BO held by the in-flight submission waits.

EXP1082 hardware: with submissions returning once queued, the submission in
flight still held its slots while DWM built the next batch. A first CPU map
of one of those BOs (native_map) was refused by the slot's submission hold,
and the backend failed (reject-batch kind 4, asahi_bo.c native_map) about
every 2.3 s during a GDI window drag. Invariants:
- a BO named by the in-flight residency set is mapped only after that set is
  retired (its completion and copy-hold release run first);
- a BO the in-flight submission does not name maps without waiting;
- a failed retirement fails the backend and leaves the BO unmapped.
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
#define APPLE_AGX_GPUVA_WINSYS 1
typedef struct { uint64_t Allocation; unsigned Bound; } AGX_WIN32_GPUVA_BO;
typedef struct { uint64_t *Held; unsigned HeldCount; uint64_t RenderFence; } AGX_WIN32_GPUVA_SPACE;
typedef struct { int Failed; struct { void *Screen; } Buffers; AGX_WIN32_GPUVA_SPACE Gpuva; } AGX_WIN32_ASAHI_BACKEND;
typedef struct { int unused; } AGX_WIN32_NATIVE_BO;
struct agx_device { void *windows_private; };
struct agx_bo { int refcnt; void *_map; };
struct windows_bo { struct agx_bo Base; AGX_WIN32_NATIVE_BO Backing; AGX_WIN32_ASAHI_BACKEND *Backend;
  AGX_WIN32_GPUVA_BO Gpuva; };
#define AGX_WIN32_ASAHI_FAIL(backend, file) ((backend)->Failed = 1)
enum { AppleAgxWin32BufferCpuWrite = 1, AgxWin32NativeBoSuccess = 0 };
static AGX_WIN32_ASAHI_BACKEND backend;
static unsigned retires, maps; static int refuse_retire;
static char storage[64];
static int held(uint64_t allocation) {
  for (unsigned i = 0; i < backend.Gpuva.HeldCount; ++i)
    if (backend.Gpuva.Held && backend.Gpuva.Held[i] == allocation) return 1;
  return 0;
}
/* The UMD refuses a CPU map of a slot the in-flight submission holds. */
static int AgxWin32NativeBoMap(void *screen, AGX_WIN32_NATIVE_BO *bo, unsigned access, void **address) {
  (void)screen; (void)access;
  struct windows_bo *owner = (struct windows_bo *)((char *)bo - offsetof(struct windows_bo, Backing));
  if (held(owner->Gpuva.Allocation)) return 1;
  ++maps; *address = storage; return AgxWin32NativeBoSuccess;
}
static int AgxWin32GpuvaRetire(AGX_WIN32_GPUVA_SPACE *space, uint64_t fence) {
  if (refuse_retire || !space->Held || fence != space->RenderFence) return 0;
  space->Held = NULL; space->HeldCount = 0; space->RenderFence = 0; ++retires; return 1;
}
@@FUNCTIONS@@
int main(void) {
  struct agx_device device = {&backend};
  uint64_t set[2] = {7, 8};
  struct windows_bo a = {{1, NULL}, {0}, &backend, {7, 1}}, other = {{1, NULL}, {0}, &backend, {9, 1}};
  backend.Gpuva.Held = set; backend.Gpuva.HeldCount = 2; backend.Gpuva.RenderFence = 40;
  /* Not in flight: mapped at once, the submission stays in flight. */
  native_map(&device, &other.Base, NULL);
  assert(other.Base._map == storage && retires == 0 && backend.Gpuva.Held && !backend.Failed);
  /* In flight: retired first, then mapped. */
  native_map(&device, &a.Base, NULL);
  assert(a.Base._map == storage && retires == 1 && !backend.Gpuva.Held && !backend.Failed && maps == 2);
  /* A failed retirement fails the backend and maps nothing. */
  struct windows_bo b = {{1, NULL}, {0}, &backend, {8, 1}};
  backend.Gpuva.Held = set; backend.Gpuva.HeldCount = 2; backend.Gpuva.RenderFence = 41;
  refuse_retire = 1;
  native_map(&device, &b.Base, NULL);
  assert(!b.Base._map && backend.Failed && maps == 2);
  puts("PASS");
  return 0;
}
'''


class NativeMapRetiresInFlight(unittest.TestCase):
    def test_first_map_of_a_held_bo_retires_the_submission(self):
        text = SRC.read_text()
        functions = '\n'.join(function(text, n) for n in ('cache_in_flight', 'native_map'))
        self.assertIn('AgxWin32GpuvaRetire(', function(text, 'native_map'))
        program = PROGRAM.replace('@@FUNCTIONS@@', functions).replace(
            '#include <stdlib.h>', '#include <stdlib.h>\n#include <stddef.h>')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'map.c'
            exe = Path(directory) / 'map'
            path.write_text(program)
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                                    '-Werror', '-Wno-unused-function', '-Wno-missing-field-initializers',
                                    '-fsanitize=address,undefined', str(path), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == '__main__':
    unittest.main()
