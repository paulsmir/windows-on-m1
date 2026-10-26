"""Execute the bounded staging-ring ownership algorithm as C and C++.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Upload storage cannot be
reused before its GPU fence retires; readback storage additionally needs the
CPU consumer to release it. Wrap/alignment padding and uncertain submissions
must never permit a live byte range to be overwritten.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "drivers/apple-agx/render-admission/umd/include"

PROGRAM = r'''
#include <assert.h>
#include <string.h>
#include "umd_staging_ring.h"

static void wrap_and_fences(void) {
  ADMISSION_UMD_STAGING_RING ring;
  ADMISSION_UMD_RING_SLICE a, b, c;
  assert(AdmissionUmdRingInit(&ring, 128));
  assert(AdmissionUmdRingReserve(&ring, 80, 16, 0, &a));
  assert(a.Offset == 0 && a.Bytes == 80);
  assert(AdmissionUmdRingCommit(&ring, a.Ticket, 7));
  assert(AdmissionUmdRingReserve(&ring, 32, 16, 0, &b));
  assert(b.Offset == 80 && AdmissionUmdRingCommit(&ring, b.Ticket, 8));
  assert(!AdmissionUmdRingReserve(&ring, 32, 16, 0, &c));
  assert(AdmissionUmdRingRetire(&ring, 6) && ring.Used == 112);
  assert(AdmissionUmdRingRetire(&ring, 7) && ring.Used == 32);
  assert(AdmissionUmdRingReserve(&ring, 48, 32, 0, &c));
  assert(c.Offset == 0 && ring.Used == 96); /* 16-byte wrap pad charged */
  assert(!AdmissionUmdRingCommit(&ring, c.Ticket, 7)); /* timeline regresses */
  assert(AdmissionUmdRingCommit(&ring, c.Ticket, 9));
  assert(AdmissionUmdRingRetire(&ring, 8) && ring.Used == 64);
  assert(!AdmissionUmdRingRetire(&ring, 7));
  assert(AdmissionUmdRingRetire(&ring, 9) && ring.Used == 0);
  assert(AdmissionUmdRingReserve(&ring, 128, 1, 0, &a));
  assert(a.Offset == 0 && AdmissionUmdRingCancel(&ring, a.Ticket));
  assert(ring.Used == 0 && ring.Count == 0);
}

static void cpu_holds_and_cancel(void) {
  ADMISSION_UMD_STAGING_RING ring, before;
  ADMISSION_UMD_RING_SLICE a, b, c;
  assert(AdmissionUmdRingInit(&ring, 128));
  assert(AdmissionUmdRingReserve(&ring, 40, 8, 1, &a));
  assert(!AdmissionUmdRingReleaseCpu(&ring, a.Ticket)); /* not submitted */
  assert(AdmissionUmdRingCommit(&ring, a.Ticket, 1));
  assert(!AdmissionUmdRingReleaseCpu(&ring, a.Ticket)); /* GPU still owns */
  assert(AdmissionUmdRingReserve(&ring, 16, 32, 0, &b));
  assert(b.Offset == 64 && ring.Used == 80);
  assert(AdmissionUmdRingCommit(&ring, b.Ticket, 2));
  before = ring;
  assert(AdmissionUmdRingReserve(&ring, 64, 16, 0, &c) == 0);
  assert(memcmp(&ring, &before, sizeof(ring)) == 0);
  assert(AdmissionUmdRingRetire(&ring, 2));
  assert(ring.Used == 80); /* readback pins the FIFO, including newer entry */
  assert(AdmissionUmdRingReleaseCpu(&ring, a.Ticket));
  assert(ring.Used == 0 && ring.Count == 0);
  assert(!AdmissionUmdRingReleaseCpu(&ring, a.Ticket)); /* stale generation */
  assert(AdmissionUmdRingReserve(&ring, 100, 1, 0, &a));
  assert(AdmissionUmdRingCommit(&ring, a.Ticket, 3));
  assert(AdmissionUmdRingRetire(&ring, 3));
  assert(AdmissionUmdRingReserve(&ring, 80, 1, 0, &b));
  assert(AdmissionUmdRingCommit(&ring, b.Ticket, 4));
  assert(AdmissionUmdRingReserve(&ring, 4, 32, 0, &c));
  assert(ring.Used == 100);
  assert(!AdmissionUmdRingCancel(&ring, b.Ticket));
  assert(AdmissionUmdRingCancel(&ring, c.Ticket));
  assert(ring.Used == 80 && ring.Write == 80);
  assert(!AdmissionUmdRingCommit(&ring, c.Ticket, 5));
  assert(AdmissionUmdRingReserve(&ring, 16, 16, 0, &c));
  assert(c.Offset == 80);
}

static void invalid_and_terminal(void) {
  ADMISSION_UMD_STAGING_RING ring, before;
  ADMISSION_UMD_RING_SLICE s;
  assert(!AdmissionUmdRingInit(0, 128));
  memset(&ring, 0, sizeof(ring));
  assert(!AdmissionUmdRingReserve(&ring, 1, 1, 0, &s));
  assert(!AdmissionUmdRingInit(&ring, 0));
  assert(!AdmissionUmdRingInit(&ring, ADMISSION_UMD_RING_MAX_BYTES + 1u));
  assert(AdmissionUmdRingInit(&ring, 128));
  before = ring;
  assert(!AdmissionUmdRingReserve(&ring, 0, 1, 0, &s));
  assert(!AdmissionUmdRingReserve(&ring, 129, 1, 0, &s));
  assert(!AdmissionUmdRingReserve(&ring, ~0u, 1, 0, &s));
  assert(!AdmissionUmdRingReserve(&ring, 1, 0, 0, &s));
  assert(!AdmissionUmdRingReserve(&ring, 1, 3, 0, &s));
  assert(!AdmissionUmdRingReserve(&ring, 1, 256, 0, &s));
  assert(!AdmissionUmdRingReserve(&ring, 1, 1, 2, &s));
  assert(memcmp(&ring, &before, sizeof(ring)) == 0);
  assert(AdmissionUmdRingReserve(&ring, 16, 1, 0, &s));
  assert(!AdmissionUmdRingCommit(&ring, s.Ticket, 0));
  /* An accepted submission with an unknown signal/wait result is terminal.
   * Even a later numerical completion cannot certify that write's lifetime. */
  AdmissionUmdRingPoison(&ring);
  before = ring;
  assert(!AdmissionUmdRingCancel(&ring, s.Ticket));
  assert(!AdmissionUmdRingCommit(&ring, s.Ticket, 1));
  assert(!AdmissionUmdRingRetire(&ring, ~0ull));
  assert(!AdmissionUmdRingReleaseCpu(&ring, s.Ticket));
  assert(!AdmissionUmdRingReserve(&ring, 1, 1, 0, &s));
  assert(memcmp(&ring, &before, sizeof(ring)) == 0);
  assert(!AdmissionUmdRingDrained(&ring));
  /* Init is creation-only, not a poison reset API. Never call it on live data. */
  assert(AdmissionUmdRingInit(&ring, 128));
  ring.NextTicket = ~0ull;
  assert(!AdmissionUmdRingReserve(&ring, 1, 1, 0, &s));
  ring.NextTicket = 0;
  assert(AdmissionUmdRingReserve(&ring, 1, 1, 0, &s));
  assert(AdmissionUmdRingCommit(&ring, s.Ticket, ~0ull));
  assert(AdmissionUmdRingRetire(&ring, ~0ull));
  assert(AdmissionUmdRingDrained(&ring));
  assert(!AdmissionUmdRingReserve(&ring, 1, 1, 0, &s)); /* exhausted fence domain */
}

static void queued_uploads(void) {
  ADMISSION_UMD_STAGING_RING ring;
  ADMISSION_UMD_RING_SLICE a, b, c;
  assert(AdmissionUmdRingInit(&ring, 128));
  assert(AdmissionUmdRingReserve(&ring, 32, 16, 0, &a));
  assert(AdmissionUmdRingReserve(&ring, 32, 16, 1, &b));
  assert(AdmissionUmdRingReserve(&ring, 32, 16, 0, &c));
  assert(!AdmissionUmdRingCancel(&ring, a.Ticket));
  assert(AdmissionUmdRingCancel(&ring, c.Ticket));
  assert(ring.Pending == 2 && ring.Used == 64);
  assert(!AdmissionUmdRingCommit(&ring, b.Ticket, 1));
  assert(AdmissionUmdRingCommitAll(&ring, 1));
  assert(ring.Pending == 0);
  assert(!AdmissionUmdRingCommitAll(&ring, 2));
  assert(!AdmissionUmdRingCancel(&ring, b.Ticket));
  assert(AdmissionUmdRingRetire(&ring, 1) && ring.Used == 32);
  assert(AdmissionUmdRingReleaseCpu(&ring, b.Ticket));
  assert(AdmissionUmdRingDrained(&ring));
  /* A reservation is not submitted merely because another job completed. */
  assert(AdmissionUmdRingReserve(&ring, 128, 1, 0, &a));
  assert(AdmissionUmdRingRetire(&ring, 5) && ring.Used == 128);
  assert(!AdmissionUmdRingCommitAll(&ring, 5));
  assert(AdmissionUmdRingCommitAll(&ring, 6));
  assert(AdmissionUmdRingRetire(&ring, 6) && AdmissionUmdRingDrained(&ring));
}

static void fence_identity_and_partial_wrap(void) {
  ADMISSION_UMD_STAGING_RING ring, before;
  ADMISSION_UMD_RING_SLICE a, b, c, d;
  assert(AdmissionUmdRingInit(&ring, 257));
  assert(AdmissionUmdRingReserve(&ring, 32, 1, 0, &a));
  assert(AdmissionUmdRingCommit(&ring, a.Ticket, 1));
  assert(AdmissionUmdRingReserve(&ring, 32, 1, 0, &b));
  before = ring;
  assert(!AdmissionUmdRingCommit(&ring, b.Ticket, 1));
  assert(memcmp(&ring, &before, sizeof(ring)) == 0);
  assert(!AdmissionUmdRingCommitAll(&ring, 1));
  assert(memcmp(&ring, &before, sizeof(ring)) == 0);
  assert(AdmissionUmdRingRetire(&ring, 1) && ring.Used == 32);
  assert(AdmissionUmdRingCommitAll(&ring, 2));
  assert(AdmissionUmdRingRetire(&ring, 2) && AdmissionUmdRingDrained(&ring));
  for (unsigned k = 0; k < 1000; ++k) {
    unsigned long long fence = 3ull + 3ull * k;
    assert(AdmissionUmdRingReserve(&ring, 96, 1, 0, &a));
    assert(AdmissionUmdRingCommit(&ring, a.Ticket, fence));
    assert(AdmissionUmdRingReserve(&ring, 96, 1, 1, &b));
    assert(AdmissionUmdRingCommit(&ring, b.Ticket, fence + 1));
    assert(AdmissionUmdRingRetire(&ring, fence));
    assert(AdmissionUmdRingReserve(&ring, 32, 64, 0, &c));
    assert(c.Offset == 192);
    assert(AdmissionUmdRingReserve(&ring, 64, 64, 0, &d));
    assert(d.Offset == 0 && ring.Used == 225);
    assert(AdmissionUmdRingRetire(&ring, fence + 1) && ring.Used == 225);
    assert(AdmissionUmdRingReleaseCpu(&ring, b.Ticket) && ring.Used == 129);
    assert(AdmissionUmdRingCancel(&ring, d.Ticket) && ring.Used == 32);
    assert(AdmissionUmdRingReserve(&ring, 64, 64, 0, &d));
    assert(d.Offset == 0 && ring.Used == 129);
    assert(AdmissionUmdRingCommitAll(&ring, fence + 2));
    assert(AdmissionUmdRingRetire(&ring, fence + 2));
    assert(AdmissionUmdRingDrained(&ring));
  }
  assert(AdmissionUmdRingReserve(&ring, 1, 1, 0, &a));
  assert(AdmissionUmdRingCommit(&ring, a.Ticket, ~0ull));
  assert(!AdmissionUmdRingReserve(&ring, 1, 1, 0, &b));
}

static unsigned rnd_state = 1;
static unsigned rnd(void) { rnd_state = rnd_state * 1664525u + 1013904223u; return rnd_state; }
static void stress(void) {
  ADMISSION_UMD_STAGING_RING ring;
  ADMISSION_UMD_RING_SLICE slices[ADMISSION_UMD_RING_MAX_SLICES];
  unsigned char held[ADMISSION_UMD_RING_MAX_SLICES];
  unsigned n = 0;
  unsigned long long fence = 1;
  assert(AdmissionUmdRingInit(&ring, 1024));
  for (unsigned iter = 0; iter < 20000; ++iter) {
    ADMISSION_UMD_RING_SLICE s;
    unsigned bytes = 1 + rnd() % 200, align = 1u << (rnd() % 7), cpu = rnd() % 2;
    if (AdmissionUmdRingReserve(&ring, bytes, align, cpu, &s)) {
      assert(n < ADMISSION_UMD_RING_MAX_SLICES);
      assert(s.Offset % align == 0 && s.Offset + s.Bytes <= 1024);
      for (unsigned i = 0; i < n; ++i)
        assert(s.Offset + s.Bytes <= slices[i].Offset || slices[i].Offset + slices[i].Bytes <= s.Offset);
      if (rnd() % 5 == 0) assert(AdmissionUmdRingCancel(&ring, s.Ticket));
      else {
        slices[n] = s; held[n++] = (unsigned char)cpu;
        assert(AdmissionUmdRingCommit(&ring, s.Ticket, fence++));
      }
    }
    if (rnd() % 3 == 0) {
      assert(AdmissionUmdRingRetire(&ring, fence++));
      for (unsigned i = 0; i < n; ++i)
        if (held[i]) assert(AdmissionUmdRingReleaseCpu(&ring, slices[i].Ticket));
      n = 0;
      assert(AdmissionUmdRingDrained(&ring));
    }
    assert(ring.Used <= ring.Capacity);
  }
  assert(AdmissionUmdRingRetire(&ring, fence));
  for (unsigned i = 0; i < n; ++i)
    if (held[i]) assert(AdmissionUmdRingReleaseCpu(&ring, slices[i].Ticket));
  assert(AdmissionUmdRingDrained(&ring));
  assert(AdmissionUmdRingInit(&ring, ADMISSION_UMD_RING_MAX_SLICES * 2));
  for (unsigned i = 0; i < ADMISSION_UMD_RING_MAX_SLICES; ++i) {
    ADMISSION_UMD_RING_SLICE s;
    assert(AdmissionUmdRingReserve(&ring, 1, 1, 0, &s));
    assert(AdmissionUmdRingCommit(&ring, s.Ticket, i + 1ull));
  }
  { ADMISSION_UMD_RING_SLICE s; assert(!AdmissionUmdRingReserve(&ring, 1, 1, 0, &s)); }
  assert(AdmissionUmdRingRetire(&ring, ADMISSION_UMD_RING_MAX_SLICES) && AdmissionUmdRingDrained(&ring));
}
int main(void) {
  wrap_and_fences(); cpu_holds_and_cancel(); invalid_and_terminal(); queued_uploads(); fence_identity_and_partial_wrap(); stress();
  return 0;
}
'''


class StagingRingTests(unittest.TestCase):
    def run_language(self, compiler, language):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / ("ring.c" if language == "c" else "ring.cpp")
            source.write_text(PROGRAM)
            binary = Path(tmp) / "ring"
            command = [compiler, "-std=c11" if language == "c" else "-std=c++17",
                       "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                       "-fno-omit-frame-pointer", "-I", str(INCLUDE), str(source), "-o", str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_c_ownership(self):
        self.run_language(os.environ.get("CC", "cc"), "c")

    def test_cpp_ownership(self):
        self.run_language(os.environ.get("CXX", "c++"), "c++")


if __name__ == "__main__":
    unittest.main()
