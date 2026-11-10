"""BO census receipts (measure-bo-born / measure-bo-first-ref / measure-bo-census).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
1. Winsys: AgxWin32AsahiCensusRefs maps each batch reference back to its BO
   through &windows_bo.Gpuva; a BO reports its first reference at most once,
   nothing is stamped or reported while the hook is unset, and BOs under 1 MiB
   only feed the teardown aggregate.  A wrong mapping or a re-armed stamp
   would report references of the wrong BO or repeat lines.
2. tools/agx_bo_life.py: born and first-ref lines join on (pid, serial), the
   D3D12 class comes from the create-heap-resource row open on the born line's
   own thread (not another thread's), heap rows label by VA, and the delay
   buckets use the first reference minus the creation time.  A misjoin
   attributes never-referenced bytes to the wrong class.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BO = ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_asahi_bo.c'
TOOL = ROOT / 'tools/agx_bo_life.py'


def function(text, name):
    m = re.search(r'(?m)^(static )?[A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    assert m, name
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


PROGRAM = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef unsigned APPLE_AGX_U32; typedef unsigned long long APPLE_AGX_U64;
#define AGX_WIN32_CENSUS_LINE_BYTES (1ull << 20)
typedef struct { uint64_t Allocation, Va, Bytes; unsigned Bound; } AGX_WIN32_GPUVA_BO;
struct agx_va { uint64_t addr; };
enum agx_bo_flags { AGX_BO_X = 1 };
struct agx_bo { size_t size; uint32_t handle; enum agx_bo_flags flags; };
struct windows_bo { struct agx_bo Base; struct agx_va Coordinate; int Pad; AGX_WIN32_GPUVA_BO Gpuva;
                    int Virtual; unsigned CensusPending; uint32_t CensusSerial; };
typedef struct { APPLE_AGX_U64 CensusSmallBytes, CensusSmallRefBytes;
                 APPLE_AGX_U32 CensusSmall, CensusSmallRef, CensusLarge, CensusLargeRef; } AGX_WIN32_ASAHI_BACKEND;
unsigned (*AgxWin32BoCensusHook)(unsigned event, uint32_t *values, unsigned count);
static unsigned lines[4], serial, last[9];
static unsigned hook(unsigned e, uint32_t *v, unsigned n) {
  ++lines[e]; for (unsigned i = 0; i < n; ++i) last[i] = v[i]; return e == 1u ? ++serial : 0u; }
@@FUNCS@@
int main(void) {
  AGX_WIN32_ASAHI_BACKEND b = {0};
  struct windows_bo big = {{3u << 20, 7u, AGX_BO_X}, {0x1234560000ull}, 0, {1, 0, 0, 1}, 0, 0, 0};
  struct windows_bo small = {{1u << 16, 8u, AGX_BO_X}, {0x10000ull}, 0, {2, 0, 0, 1}, 0, 0, 0};
  const AGX_WIN32_GPUVA_BO *refs[2] = {&big.Gpuva, &small.Gpuva};
  AgxWin32AsahiCensusRefs(&b, refs, 2u, 1u);          /* hook unset: no stamp, no line */
  assert(!lines[2] && !big.CensusPending && !b.CensusLargeRef);
  AgxWin32BoCensusHook = hook;
  census_born(&b, &big, 1u, 1u);
  census_born(&b, &small, 1u, 1u);                    /* < 1 MiB: aggregate only */
  assert(lines[1] == 1u && big.CensusSerial == 1u && last[2] == 48u && last[3] == 0x123456u &&
         last[4] == 0x12u && last[6] == 1u && b.CensusSmall == 1u && b.CensusLarge == 1u);
  AgxWin32AsahiCensusRefs(&b, refs, 2u, 2u);
  assert(lines[2] == 1u && last[0] == 1u && last[1] == 7u && last[3] == 2u);
  AgxWin32AsahiCensusRefs(&b, refs, 2u, 1u);          /* second reference: nothing */
  assert(lines[2] == 1u && b.CensusSmallRef == 1u && b.CensusLargeRef == 1u);
  census_born(&b, &big, 1u, 2u);                      /* cache reuse: a new life */
  AgxWin32AsahiCensusRefs(&b, refs, 1u, 1u);
  assert(lines[1] == 2u && lines[2] == 2u && last[0] == 2u);
  census_teardown(&b);
  assert(lines[3] == 1u && last[0] == 1u && last[1] == 1u && last[2] == 1u && last[4] == 2u && last[5] == 2u);
  puts("bo census: PASS");
  return 0;
}
'''


def line(name, pid, tid, values):
    return '%s hr=0x00000000 pid=%d tid=%d session=1 qpc=1 %s\n' % (
        name, pid, tid, ' '.join('%08x' % v for v in values))


def born(serial, size, va, ms, tid, cls=1, origin=1):
    return line('measure-bo-born', 7, tid, [serial, 0x100 + serial, size >> 16, (va >> 16) & 0xffffffff,
                                            va >> 32, cls, origin, 0, ms])


def ref(serial, size, ms):
    return line('measure-bo-first-ref', 7, 30, [serial, 0x100 + serial, size >> 16, 1, ms])


class BoCensus(unittest.TestCase):
    def test_winsys_census_stamps_once(self):
        text = BO.read_text()
        funcs = '\n'.join(function(text, n) for n in ('census_born', 'AgxWin32AsahiCensusRefs', 'census_teardown'))
        with tempfile.TemporaryDirectory(prefix='bo-census-') as tmp:
            src = Path(tmp) / 'census.c'; exe = Path(tmp) / 'census'
            src.write_text(PROGRAM.replace('@@FUNCS@@', funcs))
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-Wconversion', '-Wsign-compare', '-fsanitize=address,undefined',
                                    str(src), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('bo census: PASS', ran.stdout)

    def test_tool_joins_by_serial_thread_and_va(self):
        M = 1 << 20
        log = ''.join([
            # tid 10 opens a committed-buffer scope; tid 11 creates a driver BO meanwhile
            line('measure-d3d12-create-heap-resource', 7, 10, [1, 256 * 16, 0, 1, 0, 1, 1, 256 * M, 1, 3]),
            born(1, 4 * M, 0x2000000000, 1000, 11, cls=3),
            born(2, 256 * M, 0x1600000000, 1100, 10),
            line('measure-d3d12-heap', 7, 10, [2, 256 * 16, 1, 0x160000, 0x16]),
            # a heap the owner made outside CreateHeapAndResource: labelled by VA
            born(3, 2 * M, 0x1900000000, 1200, 12),
            line('measure-d3d12-heap', 7, 12, [2, 32, 1, 0x190000, 0x19]),
            ref(2, 256 * M, 1100 + 999),     # 0..1 s
            ref(1, 4 * M, 1000 + 70000),     # exactly 70 s: >70s bucket
            ref(1, 4 * M, 1000 + 80000),     # repeat: reported, not counted
        ])
        with tempfile.TemporaryDirectory(prefix='bo-life-') as tmp:
            path = Path(tmp) / 'umd.log'
            path.write_text(log)
            out = subprocess.run([sys.executable, str(TOOL), str(path)], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stderr)
            rows = {l.split('  ')[1].strip(): l for l in out.stdout.splitlines() if l.startswith('  ') and
                    re.match(r'\s+\S.*\s+\d+\s+[\d.]+\s', l)}
            self.assertRegex(rows['committed-buffer'], r'\s1\s+256\.0\s+0/0\.0\s+1/256\.0')
            self.assertRegex(rows['driver Encoder'], r'\s1\s+4\.0\s+0/0\.0\s.*1/4\.0\s*$')
            self.assertRegex(out.stdout, r'heap \(outside CreateHeapAndResource\)\s+1\s+2\.0\s+1/2\.0')
            self.assertIn('serial      3', out.stdout)
            self.assertIn('0 first-ref lines without a born line, 1 repeated', out.stdout)
            empty = Path(tmp) / 'empty.log'
            empty.write_text(line('measure-bo-create', 7, 1, [256, 1, 1, 1, 1]))
            out = subprocess.run([sys.executable, str(TOOL), str(empty)], capture_output=True, text=True)
            self.assertEqual(out.returncode, 1)
            self.assertIn('no measure-bo-born lines', out.stderr)


if __name__ == '__main__':
    unittest.main()
