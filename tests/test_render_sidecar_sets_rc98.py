"""rc98: the compute sidecar's second job-word set and the depth-2 Barriers.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
rc95 U7: a G4 compute job and a render with a v5 compute prelude both build
their RunCompute work (0x8000), microsequence (0x8400), TA Barrier (0x8800)
and timestamp block (0x9000-0x9420) in the one compute sidecar (object 39),
so a depth-2 window can never hold a C->R or R->C pair (refusal 57). rc98
adds set B, the same layout 0x2000 higher in the free range 0x9420-0xc000.

INVARIANTS:
  S1 the default path (JobWordSet 0) writes exactly the bytes the pre-rc98
     builder wrote: a golden digest taken on the base commit 497fd0ff over
     the whole sidecar, the threshold, the outputs and the TA Barrier;
  S2 a set B build writes no byte of set A, and set A no byte of set B; the
     set B words equal set A's except job-word pointers, which move by the
     set delta; statistics, firmware stamp and gpu_buf stay shared;
  S3 set B needs a job-owned preemption buffer (the sidecar's is single),
     and no set beyond B exists;
  S4 parity: without SECOND_SIDECAR every build is set A; with it, a new
     stamp takes the other set than the last build, a rebuild keeps its set,
     so two consecutive sidecar jobs never share their words;
  S5 the Barrier encoder is the validated D3D-G1b layout (it reproduces the
     captured 3D-on-TA WorkCommandBarrier, template object 14) for the TA
     queue on the compute stamp (D3D-G1b), on the 3D stamp (TA_ON_3D) and
     for the compute queue (COMPUTE_BARRIER, Asahi queue/mod.rs:787-820:
     the same raw::Barrier for vdm and cdm barriers); each set has its own
     TA and compute-queue Barrier slots;
  S6 the wait stamps are the queues' firmware stamps: TA = object 9 and
     3D = object 10 (the template's own Barrier and 3D work command point
     there), compute = sidecar 0x8780.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'

# S1: printed by this harness's golden() on the base commit 497fd0ff (before
# rc98), unchanged by every rc98 commit.
GOLDEN_DEFAULT = '0x9b74ca067ffb30d1'

SOURCES = [
    'apple_agx_render_shared_memory.c', 'apple_agx_g13_compute_work.c',
    'apple_agx_render_template.generated.c', 'apple_agx_render_template_rebase.c',
    'apple_agx_exp208_adapter.c', 'apple_agx_exp208_gdi.c',
    'apple_agx_exp208_framebuffer.c', 'apple_agx_gdi.c', 'apple_agx_relocation.c',
    'apple_agx_memory.c', 'apple_agx_exp208_dynamic.c',
]

HARNESS = r'''
#include "apple_agx_render_shared_memory.h"
#include "apple_agx_render_template.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIDE 39u
static unsigned char store[2][40][0x10000];
static APPLE_AGX_RENDER_SHARED_MEMORY_OWNER owners[2];

static unsigned long long fnv(const unsigned char *p, size_t n, unsigned long long h) {
  size_t i;
  for (i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ULL; }
  return h;
}
static APPLE_AGX_U64 u64(const unsigned char *p) { APPLE_AGX_U64 v; memcpy(&v, p, 8); return v; }
static APPLE_AGX_U32 u32(const unsigned char *p) { APPLE_AGX_U32 v; memcpy(&v, p, 4); return v; }

static APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *setup(unsigned k) {
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o = &owners[k];
  unsigned i;
  memset(o, 0, sizeof(*o));
  memset(store[k], 0xcd, sizeof(store[k]));
  o->ObjectCount = 40u;
  for (i = 0; i < 40u; ++i) {
    o->Objects[i].CpuAddress = store[k][i];
    o->Objects[i].Length = 0x10000u;
    o->Objects[i].State = AppleAgxMemoryGpuMapped;
    o->VirtualAddresses[i] = 0x1500000000ULL + (unsigned long long)i * 0x10000ULL;
    o->Objects[i].GpuVirtualAddress = o->VirtualAddresses[i];
  }
  memset(store[k][11], 0, 0x10000);
  store[k][11][8] = 0x42u; /* notifier generation */
  return o;
}

static void input(APPLE_AGX_RENDER_COMPUTE_INPUT *in, int step) {
  memset(in, 0, sizeof(*in));
  in->CdmStreamBase = 0x1500400000ULL; in->CdmStreamBytes = 72u;
  in->Counter = 0x100u + step; in->UscExecutionBase = 0x1100000000ULL;
  in->VmSlot = 2u; in->EventNumber = 2u; in->StampValue = 0x100u + step;
  in->EventSequence = 0x100u + step; in->ClientSequence = step;
  in->PreemptionGpuAddress = step & 1 ? 0x1500600000ULL : 0u;
  in->Timestamps = step >= 2 ? 1u : 0u;
  if (step == 3) { in->HelperProgram = 0x1c0u; in->HelperArgument = 0x1500700000ULL; }
}

/* S1: only fields that existed before rc98 are set (zero = set A). */
static int golden(void) {
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o = setup(0);
  APPLE_AGX_RENDER_COMPUTE_INPUT in;
  APPLE_AGX_RENDER_COMPUTE_OUTPUT out;
  APPLE_AGX_RENDER_BARRIER_INPUT b;
  APPLE_AGX_U64 address = 0, h = 0xcbf29ce484222325ULL;
  int step;
  for (step = 0; step < 4; ++step) {
    input(&in, step); memset(&out, 0, sizeof(out));
    if (!AppleAgxRenderSharedMemoryBuildCompute(o, &in, &out)) return 2;
    h = fnv(store[0][SIDE], 0x10000, h);
    h = fnv(store[0][12], 4, h);
    h = fnv((const unsigned char *)&out.WorkGpuAddress, 8, h);
    h = fnv((const unsigned char *)&out.FirmwareStampGpuAddress, 8, h);
    h ^= out.TimestampCpuAddress ? (unsigned long long)((const unsigned char *)out.TimestampCpuAddress - store[0][SIDE]) : 0xffffULL;
    h *= 0x100000001b3ULL;
    memset(&b, 0, sizeof(b));
    b.WaitStampGpuAddress = out.FirmwareStampGpuAddress;
    b.WaitValue = in.StampValue; b.WaitSlot = in.EventNumber; b.StampSelf = 0x7a000200u + step;
    if (!AppleAgxRenderSharedMemoryBuildComputeBarrier(o, &b, &address)) return 3;
    h = fnv(store[0][SIDE], 0x10000, h);
    h = fnv((const unsigned char *)&address, 8, h);
  }
  printf("GOLDEN 0x%016llx\n", (unsigned long long)h);
  return 0;
}

static int untouched(const unsigned char *p, unsigned from, unsigned to) {
  unsigned i;
  for (i = from; i < to; ++i) if (p[i] != 0xcdu) return 0;
  return 1;
}

#define A_START APPLE_AGX_COMPUTE_WORK_OFFSET
#define A_END (APPLE_AGX_COMPUTE_TIMESTAMP_OFFSET + APPLE_AGX_G13_TIMESTAMP_BLOCK_BYTES)
#define DELTA APPLE_AGX_COMPUTE_JOB_WORD_SET_B_DELTA

/* The set's own words: work + microsequence, the Barrier slots, the
 * timestamp microsequence and block (not the shared statistics/stamp). */
static int job_word(APPLE_AGX_U64 offset) {
  return (offset >= A_START && offset < APPLE_AGX_COMPUTE_STATISTICS_OFFSET) ||
         (offset >= APPLE_AGX_COMPUTE_TA_BARRIER_OFFSET &&
          offset < APPLE_AGX_COMPUTE_QUEUE_BARRIER_OFFSET + APPLE_AGX_G13_BARRIER_BYTES) ||
         (offset >= APPLE_AGX_COMPUTE_TIMESTAMP_MICROSEQUENCE_OFFSET && offset < A_END);
}

/* S2: set B bytes = set A bytes with every job-word pointer moved by DELTA. */
static void same_but_rebased(const unsigned char *a, const unsigned char *b, APPLE_AGX_U64 gpu) {
  unsigned i = A_START;
  while (i < A_END) {
    if (a[i] == b[i + DELTA]) { ++i; continue; }
    {
      unsigned j, found = 0;
      for (j = i >= 7u ? i - 7u : 0u; j <= i && !found; ++j) {
        APPLE_AGX_U64 va = u64(a + j), vb = u64(b + j + DELTA);
        if (job_word(va - gpu) && vb == va + DELTA) {
          found = 1; i = j + 8u;
        }
      }
      if (!found) { printf("RED S2 set B differs at A+%#x\n", i); exit(1); }
    }
  }
}

static void sets(void) {
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *a = setup(0), *b = setup(1);
  APPLE_AGX_RENDER_COMPUTE_INPUT in;
  APPLE_AGX_RENDER_COMPUTE_OUTPUT out;
  const APPLE_AGX_U64 gpu = a->VirtualAddresses[SIDE];
  int step;
  /* layout: set B lies in the free range and below gpu_buf. */
  assert(APPLE_AGX_COMPUTE_JOB_WORD_OFFSET(1u, A_START) >= APPLE_AGX_COMPUTE_JOB_WORD_SET_FREE_START);
  assert(APPLE_AGX_COMPUTE_JOB_WORD_SET_FREE_START == A_END);
  assert(APPLE_AGX_COMPUTE_JOB_WORD_OFFSET(1u, A_END) <= APPLE_AGX_COMPUTE_GPU_BUFFER_OFFSET);
  assert(APPLE_AGX_COMPUTE_QUEUE_BARRIER_OFFSET >= APPLE_AGX_COMPUTE_TA_BARRIER_OFFSET + APPLE_AGX_G13_BARRIER_BYTES &&
         APPLE_AGX_COMPUTE_QUEUE_BARRIER_OFFSET + APPLE_AGX_G13_BARRIER_BYTES <= APPLE_AGX_COMPUTE_TIMESTAMP_MICROSEQUENCE_OFFSET);
  for (step = 0; step < 4; ++step) {
    input(&in, step);
    in.PreemptionGpuAddress = 0x1500600000ULL;
    assert(AppleAgxRenderSharedMemoryBuildCompute(a, &in, &out));
    assert(out.JobWordSet == 0u && out.WorkGpuAddress == gpu + 0x8000u);
    in.JobWordSet = APPLE_AGX_COMPUTE_JOB_WORD_SET_B;
    assert(AppleAgxRenderSharedMemoryBuildCompute(b, &in, &out));
    assert(out.JobWordSet == 1u && out.WorkGpuAddress == gpu + 0xa000u);
    assert(out.FirmwareStampGpuAddress == gpu + APPLE_AGX_COMPUTE_FIRMWARE_STAMP_OFFSET);
    assert(in.Timestamps ? out.TimestampCpuAddress == store[1][SIDE] + 0xb400u
                         : out.TimestampCpuAddress == NULL);
    /* the other set's bytes and the shared words were never written */
    if (!untouched(store[0][SIDE], A_START + DELTA, A_END + DELTA)) { printf("RED S2 set A build wrote set B\n"); exit(1); }
    if (!untouched(store[1][SIDE], A_START, A_END)) { printf("RED S2 set B build wrote set A\n"); exit(1); }
    assert(untouched(store[1][SIDE], APPLE_AGX_COMPUTE_GPU_BUFFER_OFFSET, 0x10000u));
    assert(untouched(store[1][SIDE], 0u, A_START));
    same_but_rebased(store[0][SIDE], store[1][SIDE], gpu);
    /* RunCompute keeps the shared statistics/stamp/queue words */
    assert(u64(store[1][SIDE] + 0xa000u + 0x290u) == gpu + APPLE_AGX_COMPUTE_FIRMWARE_STAMP_OFFSET);
    assert(u64(store[1][SIDE] + 0xa000u + 0x1f0u) ==
           gpu + (in.Timestamps ? 0xb000u : 0xa400u));
    assert(u32(store[1][12]) == u32(store[0][12]));
  }
  /* S3 */
  input(&in, 0); in.JobWordSet = 1u; in.PreemptionGpuAddress = 0u;
  if (AppleAgxRenderSharedMemoryBuildCompute(b, &in, &out)) { printf("RED S3 set B without preemption\n"); exit(1); }
  in.PreemptionGpuAddress = 0x1500600000ULL; in.JobWordSet = 2u;
  assert(!AppleAgxRenderSharedMemoryBuildCompute(b, &in, &out));
}

static void parity(void) {
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o = setup(0);
  APPLE_AGX_RENDER_COMPUTE_INPUT in;
  APPLE_AGX_RENDER_COMPUTE_OUTPUT out;
  APPLE_AGX_U32 set, last = 9u, stamp;
  for (stamp = 0x100u; stamp < 0x108u; ++stamp) {
    assert(AppleAgxRenderSharedMemoryNextJobWordSet(o, stamp, APPLE_AGX_FALSE) == 0u);
    set = AppleAgxRenderSharedMemoryNextJobWordSet(o, stamp, APPLE_AGX_TRUE);
    if (stamp == 0x100u) assert(set == 0u);
    if (set == last) { printf("RED S4 consecutive sidecar jobs share set %u\n", set); exit(1); }
    input(&in, 1); in.StampValue = in.Counter = in.EventSequence = stamp; in.JobWordSet = set;
    assert(AppleAgxRenderSharedMemoryBuildCompute(o, &in, &out));
    /* a rebuild of the same job keeps its words */
    assert(AppleAgxRenderSharedMemoryNextJobWordSet(o, stamp, APPLE_AGX_TRUE) == set);
    last = set;
  }
}

static void barriers(void) {
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o = setup(0);
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *l = AppleAgxRenderTemplateObjectLayouts();
  const APPLE_AGX_EXP208_RELOCATION *r = AppleAgxRenderTemplateRelocations();
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  unsigned char *arena = calloc(1, AppleAgxRenderTemplateBytes());
  APPLE_AGX_RENDER_BARRIER_INPUT in = {0};
  unsigned char bytes[APPLE_AGX_G13_BARRIER_BYTES];
  const unsigned char *captured;
  APPLE_AGX_U64 address = 0, gpu = o->VirtualAddresses[SIDE];
  APPLE_AGX_U32 i, n = AppleAgxRenderTemplateRelocationCount(), seen = 0u, set;
  assert(arena && AppleAgxRenderTemplateMaterialize(arena, AppleAgxRenderTemplateBytes(), &roots));
  captured = arena + l[14].ArenaOffset;
  /* S5: the encoder reproduces the captured G13 Barrier */
  in.WaitStampGpuAddress = u64(captured + 4u); in.WaitValue = u32(captured + 0x0cu);
  in.WaitSlot = u32(captured + 0x10u); in.StampSelf = u32(captured + 0x14u);
  assert(AppleAgxG13BarrierEncode(&in, bytes));
  if (memcmp(bytes, captured, 0x18u) || u32(bytes + 0x18u) != 0xffffbbbbu || u32(bytes + 0x1cu)) {
    printf("RED S5 Barrier layout\n"); exit(1);
  }
  /* S6: the captured Barrier waits on object 9 (TA fw stamp), the 3D work
   * command (object 15) writes object 10 (3D fw stamp) */
  for (i = 0; i < n; ++i) {
    if (r[i].SourceObject == 14u && r[i].SourceOffset == 4u && r[i].TargetObject == 9u) seen |= 1u;
    if (r[i].SourceObject == 15u && r[i].SourceOffset == 0x234u && r[i].TargetObject == 10u) seen |= 2u;
  }
  assert(seen == 3u);
  assert(AppleAgxRenderSharedMemoryFirmwareStampAddress(o, APPLE_AGX_RENDER_STAMP_QUEUE_TA) == o->VirtualAddresses[9]);
  assert(AppleAgxRenderSharedMemoryFirmwareStampAddress(o, APPLE_AGX_RENDER_STAMP_QUEUE_3D) == o->VirtualAddresses[10]);
  assert(AppleAgxRenderSharedMemoryFirmwareStampAddress(o, APPLE_AGX_RENDER_STAMP_QUEUE_COMPUTE) == gpu + 0x8780u);
  assert(AppleAgxRenderSharedMemoryFirmwareStampAddress(o, 3u) == 0u);
  /* each set's slots */
  for (set = 0u; set < 2u; ++set) {
    const APPLE_AGX_U32 d = set ? DELTA : 0u;
    struct { APPLE_AGX_U32 queue, stamp_queue, offset; } c[3] = {
      {APPLE_AGX_RENDER_BARRIER_QUEUE_TA, APPLE_AGX_RENDER_STAMP_QUEUE_COMPUTE, 0x8800u}, /* TA on compute */
      {APPLE_AGX_RENDER_BARRIER_QUEUE_TA, APPLE_AGX_RENDER_STAMP_QUEUE_3D, 0x8800u},      /* TA_ON_3D */
      {APPLE_AGX_RENDER_BARRIER_QUEUE_COMPUTE, APPLE_AGX_RENDER_STAMP_QUEUE_3D, 0x8820u}, /* compute on 3D */
    };
    for (i = 0; i < 3u; ++i) {
      memset(store[0][SIDE], 0xcd, 0x10000);
      memset(&in, 0, sizeof(in));
      in.WaitStampGpuAddress = AppleAgxRenderSharedMemoryFirmwareStampAddress(o, c[i].stamp_queue);
      in.WaitValue = 0x3d000200u; in.WaitSlot = 1u; in.StampSelf = 0x201u; in.JobWordSet = set;
      assert(AppleAgxRenderSharedMemoryBuildLeadingBarrier(o, &in, c[i].queue, &address));
      assert(address == gpu + c[i].offset + d && (address & 0x1fu) == 0u);
      assert(AppleAgxG13BarrierEncode(&in, bytes));
      assert(!memcmp(store[0][SIDE] + c[i].offset + d, bytes, sizeof(bytes)));
      assert(untouched(store[0][SIDE], 0u, c[i].offset + d) &&
             untouched(store[0][SIDE], c[i].offset + d + 0x20u, 0x10000u));
    }
    /* the D3D-G1b prelude Barrier is the set's TA slot */
    in.JobWordSet = set;
    assert(AppleAgxRenderSharedMemoryBuildComputeBarrier(o, &in, &address) && address == gpu + 0x8800u + d);
  }
  in.JobWordSet = 2u;
  assert(!AppleAgxRenderSharedMemoryBuildLeadingBarrier(o, &in, 0u, &address));
  in.JobWordSet = 0u;
  assert(!AppleAgxRenderSharedMemoryBuildLeadingBarrier(o, &in, 2u, &address));
  in.WaitSlot = 0x80u; assert(!AppleAgxG13BarrierEncode(&in, bytes));
  in.WaitSlot = 1u; in.WaitStampGpuAddress |= 2u; assert(!AppleAgxG13BarrierEncode(&in, bytes));
  free(arena);
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "golden")) return golden();
  sets(); parity(); barriers();
  printf("OK\n");
  return 0;
}
'''


def build(tmp, shared=SHARED, harness=HARNESS):
    path = Path(tmp) / 'sets.c'
    path.write_text(harness)
    binary = Path(tmp) / 'sets'
    subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-pedantic', '-fsanitize=address,undefined', '-I', str(shared / 'include'),
                    str(path)] + [str(shared / 'src' / s) for s in SOURCES] +
                   ['-o', str(binary)], check=True)
    return binary


class SidecarJobWordSets(unittest.TestCase):
    def test_default_path_is_byte_identical(self):
        with tempfile.TemporaryDirectory(prefix='rc98-sets-') as tmp:
            out = subprocess.run([str(build(tmp)), 'golden'], capture_output=True,
                                 text=True, timeout=60, check=True).stdout
        self.assertEqual(out.strip(), 'GOLDEN ' + GOLDEN_DEFAULT)

    def test_sets_parity_and_barriers(self):
        with tempfile.TemporaryDirectory(prefix='rc98-sets-') as tmp:
            result = subprocess.run([str(build(tmp))], capture_output=True, text=True,
                                    timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('OK', result.stdout)

    def test_new_queue_starts_with_set_a(self):
        source = (SHARED / 'src/apple_agx_render_shared_memory.c').read_text()
        init = source[source.index('AppleAgxRenderSharedMemoryInitializeComputeQueue('):]
        init = init[:init.index('\n}\n')]
        self.assertIn('Owner->JobWordStamp=0u', init)


if __name__ == '__main__':
    unittest.main()
