"""Execute the real native-arena slice allocator (umd_native_arena.h).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Live native slices back
CPU-visible BOs that the GPU may still read. Two live slices must never
overlap, leave the arena, or break their alignment; a release needs the
last-use fence proof and its exact ticket; released space must be reusable
while older slices stay live (EXP1984: FIFO head blocking mapped extra arenas);
a poisoned arena hands out and releases nothing.
rc99: a placement below a limit never ends above it (ordinary slices must not
take the readback reserve at the top of the first arena), and only the first
arena has a reserve.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
UMD_INCLUDE = ROOT / "drivers/apple-agx/render-admission/umd/include"

PROGRAM = r'''
#include <assert.h>
#include <stdio.h>
#include "umd_native_arena.h"
static ADMISSION_UMD_NATIVE_SPANS s;
static unsigned long long seed = 0x9e3779b97f4a7c15ULL;
static unsigned rnd(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return (unsigned)seed; }
static void check(void) {
  unsigned long long used = 0ULL;
  for (unsigned i = 0; i < s.Count; ++i) {
    const ADMISSION_UMD_RING_SLICE *e = &s.Live[i];
    assert(e->Ticket && e->Bytes && (unsigned long long)e->Offset + e->Bytes <= s.Capacity);
    if (i) assert(s.Live[i - 1].Offset + s.Live[i - 1].Bytes <= e->Offset);
    used += e->Bytes;
  }
  assert(used == s.Used);
}
int main(void) {
  ADMISSION_UMD_RING_SLICE a, b, c, d;
  assert(!AdmissionUmdNativeSpansInit(0, 64));
  assert(!AdmissionUmdNativeSpansInit(&s, 0));
  assert(!AdmissionUmdNativeSpansInit(&s, ADMISSION_UMD_RING_MAX_BYTES + 1u));
  assert(AdmissionUmdNativeSpansInit(&s, 256));
  assert(!AdmissionUmdNativeSpansReserve(&s, 0, 1, &a));
  assert(!AdmissionUmdNativeSpansReserve(&s, 257, 1, &a));
  assert(!AdmissionUmdNativeSpansReserve(&s, 1, 0, &a));
  assert(!AdmissionUmdNativeSpansReserve(&s, 1, 3, &a));
  assert(!AdmissionUmdNativeSpansReserve(&s, 1, 512, &a));
  assert(AdmissionUmdNativeSpansReserve(&s, 64, 64, &a) && a.Offset == 0);
  assert(AdmissionUmdNativeSpansReserve(&s, 64, 64, &b) && b.Offset == 64);
  assert(AdmissionUmdNativeSpansReserve(&s, 128, 64, &c) && c.Offset == 128);
  assert(!AdmissionUmdNativeSpansReserve(&s, 1, 1, &d));
  /* Fence proof and exact ticket. */
  assert(!AdmissionUmdNativeSpansRelease(&s, b.Ticket, 9, 8));
  assert(!AdmissionUmdNativeSpansRelease(&s, 0, 0, 0));
  assert(!AdmissionUmdNativeSpansRelease(&s, 77, 0, 0));
  assert(AdmissionUmdNativeSpansRelease(&s, b.Ticket, 9, 9) && s.Completed == 9);
  assert(!AdmissionUmdNativeSpansRelease(&s, b.Ticket, 9, 9));
  /* The middle hole is reusable while the older and newer slices live. */
  assert(AdmissionUmdNativeSpansReserve(&s, 32, 32, &d) && d.Offset == 64);
  assert(d.Ticket != b.Ticket);
  assert(!AdmissionUmdNativeSpansReserve(&s, 64, 64, &b)); /* 96..128 is only 32 */
  assert(AdmissionUmdNativeSpansReserve(&s, 32, 32, &b) && b.Offset == 96);
  check();
  AdmissionUmdNativeSpansPoison(&s);
  assert(!AdmissionUmdNativeSpansReserve(&s, 1, 1, &a));
  assert(!AdmissionUmdNativeSpansRelease(&s, a.Ticket, 0, 0));
  assert(!AdmissionUmdNativeSpansDrained(&s));
  /* Metadata exhaustion is a clean refusal, not an overwrite. */
  assert(AdmissionUmdNativeSpansInit(&s, ADMISSION_UMD_NATIVE_ARENA_SLICES * 2u));
  for (unsigned i = 0; i < ADMISSION_UMD_NATIVE_ARENA_SLICES; ++i)
    assert(AdmissionUmdNativeSpansReserve(&s, 1, 1, &a));
  assert(!AdmissionUmdNativeSpansReserve(&s, 1, 1, &a));
  check();
  /* Randomized model: live set, alignment and accounting stay exact. */
  {
    ADMISSION_UMD_RING_SLICE live[ADMISSION_UMD_NATIVE_ARENA_SLICES];
    unsigned count = 0, fails = 0;
    unsigned long long fence = 0ULL;
    assert(AdmissionUmdNativeSpansInit(&s, 1u << 20));
    for (unsigned step = 0; step < 200000; ++step) {
      if (count && (rnd() % 3 == 0 || count == ADMISSION_UMD_NATIVE_ARENA_SLICES)) {
        unsigned i = rnd() % count;
        ++fence;
        assert(AdmissionUmdNativeSpansRelease(&s, live[i].Ticket, fence, fence));
        live[i] = live[--count];
      } else {
        unsigned align = 1u << (rnd() % 17);
        unsigned bytes = 1u + rnd() % (1u << (rnd() % 17));
        ADMISSION_UMD_RING_SLICE x;
        if (AdmissionUmdNativeSpansReserve(&s, bytes, align, &x)) {
          assert(x.Bytes == bytes && !(x.Offset & (align - 1u)));
          live[count++] = x;
        } else ++fails;
      }
      check();
      assert(s.Count == count);
    }
    while (count) {
      assert(AdmissionUmdNativeSpansRelease(&s, live[count - 1].Ticket, 0, fence));
      --count;
    }
    assert(AdmissionUmdNativeSpansDrained(&s) && fails);
  }
  /* rc99: limit-bounded first fit and the readback reserve split. */
  assert(AdmissionUmdNativeArenaLimit(0, 4u << 20, 2u << 20, 0) == (2u << 20));
  assert(AdmissionUmdNativeArenaLimit(0, 4u << 20, 2u << 20, 1) == (4u << 20));
  assert(AdmissionUmdNativeArenaLimit(0, 4u << 20, 0u, 0) == (4u << 20));
  assert(AdmissionUmdNativeArenaLimit(0, 4u << 20, 0u, 1) == 0u);
  assert(AdmissionUmdNativeArenaLimit(0, 4u << 20, 8u << 20, 0) == 0u);
  for (unsigned i = 1; i < 4; ++i) {
    assert(AdmissionUmdNativeArenaLimit(i, 8u << 20, 2u << 20, 0) == (8u << 20));
    assert(AdmissionUmdNativeArenaLimit(i, 8u << 20, 2u << 20, 1) == 0u);
  }
  assert(AdmissionUmdNativeSpansInit(&s, 1024));
  assert(!AdmissionUmdNativeSpansReserveBelow(&s, 64, 64, 1025, &a));
  assert(!AdmissionUmdNativeSpansReserveBelow(&s, 64, 64, 32, &a));
  assert(AdmissionUmdNativeSpansReserveBelow(&s, 256, 64, 512, &a) && a.Offset == 0);
  assert(AdmissionUmdNativeSpansReserveBelow(&s, 256, 64, 512, &b) && b.Offset == 256);
  assert(!AdmissionUmdNativeSpansReserveBelow(&s, 64, 64, 512, &c));
  assert(AdmissionUmdNativeSpansReserveBelow(&s, 256, 64, 1024, &c) && c.Offset == 512);
  /* The gap below a slice above the limit is still usable up to the limit. */
  assert(AdmissionUmdNativeSpansRelease(&s, b.Ticket, 0, 0));
  assert(!AdmissionUmdNativeSpansReserveBelow(&s, 256, 64, 384, &b));
  assert(AdmissionUmdNativeSpansReserveBelow(&s, 128, 64, 384, &b) && b.Offset == 256);
  check();
  {
    ADMISSION_UMD_RING_SLICE live[ADMISSION_UMD_NATIVE_ARENA_SLICES];
    unsigned count = 0;
    assert(AdmissionUmdNativeSpansInit(&s, 1u << 20));
    for (unsigned step = 0; step < 100000; ++step) {
      if (count && (rnd() % 3 == 0 || count == ADMISSION_UMD_NATIVE_ARENA_SLICES)) {
        unsigned i = rnd() % count;
        assert(AdmissionUmdNativeSpansRelease(&s, live[i].Ticket, 0, 0));
        live[i] = live[--count];
      } else {
        unsigned align = 1u << (rnd() % 15);
        unsigned bytes = 1u + rnd() % (1u << (rnd() % 15));
        unsigned limit = rnd() % 2 ? (1u << 20) : (1u << 19);
        ADMISSION_UMD_RING_SLICE x;
        if (AdmissionUmdNativeSpansReserveBelow(&s, bytes, align, limit, &x)) {
          assert(x.Offset + x.Bytes <= limit && !(x.Offset & (align - 1u)));
          live[count++] = x;
        }
      }
      check();
    }
  }
  puts("native spans PASS");
  return 0;
}
'''


class NativeArenaSpans(unittest.TestCase):
    def test_real_allocator_invariants_c_and_cpp(self):
        with tempfile.TemporaryDirectory(prefix="umd-native-spans-") as path:
            tmp = Path(path)
            src = tmp / "spans.c"
            src.write_text(PROGRAM)
            for language, std in (("c", "-std=c11"), ("c++", "-std=c++17")):
                exe = tmp / ("spans-" + language.replace("+", "p"))
                build = subprocess.run([
                    "clang", "-x", language, std, "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I", str(UMD_INCLUDE), str(src),
                    "-o", str(exe)] + (["-lc++"] if language == "c++" else []),
                    capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
