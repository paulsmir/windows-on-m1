"""EXP1060: the per-device buffer table must hold more than 256 live buffers.

EXP1060 receipts: DWM (three instances) and an app failed with
reject-screen-create {active slots 256}, CreateBo result Callback, then a
failed backend; DWM crashed in agx_fast_link writing a shader into the NULL
BO. Invariants:
- far more than 256 buffers can be live at once, and each is found by token;
- every live slot lies below the high-water mark that bounds the scans, also
  after slots are released and reused (lowest free index first);
- every screen-buffer activation takes its slot from AdmissionUmdScreenFreeSlot,
  the only place that raises the mark.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_win32_screen.c'
CONSTRUCTION = ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_construction_address.h'
INTERNAL = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_internal.h'


def function(text, name):
    m = re.search(r'(?:static )?[A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


BODY = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned UINT; typedef int BOOL; typedef uint64_t APPLE_AGX_U64;
#define TRUE 1
#define FALSE 0
#define ADMISSION_UMD_SCREEN_BUFFER_LIMIT @@LIMIT@@u
@@SCAN@@
typedef struct { APPLE_AGX_U64 Token; BOOL Active; } ADMISSION_UMD_SCREEN_BUFFER;
typedef struct { ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[ADMISSION_UMD_SCREEN_BUFFER_LIMIT];
  UINT ScreenBufferHighWater; } ADMISSION_UMD_DEVICE;
@@FUNCS@@
static ADMISSION_UMD_DEVICE device;
static APPLE_AGX_U64 next_token;
static ADMISSION_UMD_SCREEN_BUFFER *create(void) {
  ADMISSION_UMD_SCREEN_BUFFER *slot = AdmissionUmdScreenFreeSlot(&device);
  if (!slot) return NULL;
  memset(slot, 0, sizeof(*slot)); slot->Token = ++next_token; slot->Active = TRUE;
  return slot;
}
static void check_bound(void) {
  for (UINT i = 0; i < ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++i)
    if (device.ScreenBuffers[i].Active) assert(i < device.ScreenBufferHighWater);
}
int main(void) {
  enum { LIVE = 1500 };
  static APPLE_AGX_U64 tokens[LIVE];
  for (UINT i = 0; i < LIVE; ++i) {
    ADMISSION_UMD_SCREEN_BUFFER *slot = create();
    if (!slot) { printf("buffer %u refused\n", i); return 1; }
    tokens[i] = slot->Token;
  }
  assert(device.ScreenBufferHighWater == LIVE);
  for (UINT i = 0; i < LIVE; ++i) assert(AdmissionUmdScreenFind(&device, tokens[i])->Token == tokens[i]);
  /* Release every third buffer, then reuse: lowest free index first. */
  for (UINT i = 0; i < LIVE; i += 3) {
    ADMISSION_UMD_SCREEN_BUFFER *slot = AdmissionUmdScreenFind(&device, tokens[i]);
    memset(slot, 0, sizeof(*slot)); assert(!AdmissionUmdScreenFind(&device, tokens[i]));
  }
  check_bound();
  for (UINT i = 0; i < LIVE; i += 3) { ADMISSION_UMD_SCREEN_BUFFER *slot = create(); assert(slot); tokens[i] = slot->Token; }
  assert(device.ScreenBufferHighWater == LIVE);
  for (UINT i = 0; i < LIVE; ++i) assert(AdmissionUmdScreenFind(&device, tokens[i]));
  /* Growth beyond the old peak raises the mark; capacity ends refusal-only. */
  while (create()) {}
  assert(device.ScreenBufferHighWater == ADMISSION_UMD_SCREEN_BUFFER_LIMIT);
  check_bound();
  assert(!AdmissionUmdScreenFind(&device, 0));
  puts("EXP1060 screen buffer capacity: PASS");
  return 0;
}
'''


class ScreenBufferCapacity(unittest.TestCase):
    def limit(self):
        m = re.search(r'#define AGX_WIN32_CONSTRUCTION_MAX_OBJECTS (\d+)u', CONSTRUCTION.read_text())
        self.assertIsNotNone(m)
        return int(m.group(1))

    def test_many_live_buffers_are_found_below_the_high_water_mark(self):
        text = SRC.read_text()
        funcs = '\n'.join(function(text, n) for n in
                          ('AdmissionUmdScreenFind', 'AdmissionUmdScreenFreeSlot'))
        scan = re.search(r'#define ADMISSION_UMD_SCREEN_BUFFER_SCAN\(Device\).*', INTERNAL.read_text())
        self.assertIsNotNone(scan, 'scan bound macro is missing')
        body = (BODY.replace('@@LIMIT@@', str(self.limit()))
                .replace('@@SCAN@@', scan.group(0)).replace('@@FUNCS@@', funcs))
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'capacity.c'; exe = Path(tmp) / 'capacity'
            src.write_text(body)
            built = subprocess.run(['clang', '-std=c11', '-Wall', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP1060 screen buffer capacity: PASS', ran.stdout)

    def test_every_buffer_activation_takes_a_free_slot(self):
        text = SRC.read_text()
        names = re.findall(r'^(?:static )?[A-Za-z_][A-Za-z0-9_ *]*?\b([A-Za-z_][A-Za-z0-9_]*)\([^;{]*\)\s*\{',
                           text, flags=re.M)
        activating = []
        for name in set(names):
            body = function(text, name)
            if re.search(r'slot->Active\s*=\s*TRUE', body) and 'ADMISSION_UMD_SCREEN_FENCE' not in body \
                    and 'Event' not in body:
                activating.append(name)
                self.assertIn('AdmissionUmdScreenFreeSlot(', body,
                              f'{name} activates a buffer slot without FreeSlot')
        self.assertGreaterEqual(len(activating), 2, activating)


if __name__ == '__main__':
    unittest.main()
