"""RC65: per-Execute upload ring for persistent non-native D3D12 heaps.

EXP1985: UPLOAD heaps over the 16 MiB native slice limit (3 x 32 MiB and
3 x 128 MiB in GravityMark) paid one blocking copy escape per dirty 64 KiB
run; every escape waits in the KMD until the process has no job in flight.
RC65 copies the write-watched dirty runs into a transient native ring slice
and records GPU copies into the canonical BO before the Execute's replay.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- WAR: the ring path must never write canonical storage on the CPU, so a GPU
  read still in flight from the previous frame cannot see the new bytes; a
  ring slice is reused only after the copy that read it has completed.
- Map/unmap: dirty bits are consumed only when the copies are reserved; a
  refused reservation, an unmapped or GPU-written heap and an unknown
  submission leave the bytes to the existing escape path (or a full
  restore), never silently clean.
- The upload copy must not mark the heap GpuWritten (no download, no merge
  shadow) and its submission must not upload the same pages again.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_umd_staging_write_watch import function

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'drivers/apple-agx'
INC = DRIVER / 'render-admission/umd/include'
GPU = DRIVER / 'render-admission/umd/src/umd_gpuva_windows.c'
OWNER = DRIVER / 'mesa/winsys/agx_d3d12_windows.cpp'
DDI = DRIVER / 'd3d12/agx_d3d12_ddi.cpp'


def build_and_run(test, source, name, cxx=True):
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / (name + ('.cpp' if cxx else '.c'))
        exe = Path(tmp) / name
        src.write_text(source)
        compiler = ['clang++', '-std=c++17'] if cxx else ['clang', '-std=c11']
        built = subprocess.run(compiler + ['-fsanitize=address,undefined', '-Wall', '-Wextra',
                                           '-I', str(INC), str(src), '-o', str(exe)],
                               capture_output=True, text=True)
        test.assertEqual(built.returncode, 0, built.stderr)
        ran = subprocess.run([str(exe)], capture_output=True, text=True)
        test.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


PLAN = r'''
#include <assert.h>
#include "umd_upload_ring.h"
int main(void) {
  ADMISSION_UMD_UPLOAD_COPY runs[4];
  unsigned long long slice = 7, pages[6];
  /* empty and malformed sets */
  assert(AdmissionUmdUploadPlan(pages, 0u, 4096u, 1u << 20, 65536u, 3u, runs, 4u, &slice) == 0u && slice == 0u);
  pages[0] = 4096; pages[1] = 4096;
  assert(AdmissionUmdUploadPlan(pages, 2u, 4096u, 1u << 20, 65536u, 3u, runs, 4u, &slice) == ~0u);
  pages[0] = 4097;
  assert(AdmissionUmdUploadPlan(pages, 1u, 4096u, 1u << 20, 65536u, 3u, runs, 4u, &slice) == ~0u);
  pages[0] = 1u << 20;
  assert(AdmissionUmdUploadPlan(pages, 1u, 4096u, 1u << 20, 65536u, 3u, runs, 4u, &slice) == ~0u);
  assert(AdmissionUmdUploadPlan(pages, 1u, 3000u, 1u << 20, 65536u, 3u, runs, 4u, &slice) == ~0u);
  /* adjacent pages and a clean gap of <= Gap merge; a larger gap splits */
  pages[0] = 0; pages[1] = 4096; pages[2] = 4096 + 4096 + 65536;
  pages[3] = 4096 + 4096 + 65536 + 4096 + 65536 + 4096;
  assert(AdmissionUmdUploadPlan(pages, 4u, 4096u, 1u << 20, 65536u, 3u, runs, 4u, &slice) == 2u);
  assert(runs[0].Offset == 0 && runs[0].Bytes == 4096u * 3u + 65536u && runs[0].RingOffset == 0u);
  assert(runs[1].Offset == pages[3] && runs[1].Bytes == 4096u && runs[1].Target == 3u);
  assert(runs[1].RingOffset == runs[0].Bytes && slice == runs[0].Bytes + 4096u);
  /* the BO tail is a partial page; following ring offsets stay 16-byte aligned */
  pages[0] = 0; pages[1] = 8192u * 16u;
  assert(AdmissionUmdUploadPlan(pages, 2u, 4096u, 8192u * 16u + 100u, 0u, 1u, runs, 4u, &slice) == 2u);
  assert(runs[1].Bytes == 100u && slice == 4096u + 112u);
  pages[0] = 8192u * 16u - 4096u;
  assert(AdmissionUmdUploadPlan(pages, 2u, 4096u, 8192u * 16u + 100u, 0u, 1u, runs, 4u, &slice) == 1u);
  assert(runs[0].Bytes == 4196u && slice == 4208u);
  /* more runs than the caller's copy capacity is a refusal, not a truncation */
  pages[0] = 0; pages[1] = 1u << 18; pages[2] = 1u << 19;
  assert(AdmissionUmdUploadPlan(pages, 3u, 4096u, 1u << 20, 65536u, 0u, runs, 2u, &slice) == ~0u);
  assert(AdmissionUmdUploadPlan(pages, 3u, 4096u, 1u << 20, 65536u, 0u, runs, 3u, &slice) == 3u);
  return 0;
}
'''

PRELUDE = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include "umd_upload_ring.h"
using BYTE=unsigned char; using UINT=unsigned; using ULONG_PTR=uintptr_t; using DWORD=unsigned;
using SIZE_T=size_t; using PVOID=void *; using BOOL=int; using ULONGLONG=unsigned long long;
#define TRUE 1
#define FALSE 0
#define WRITE_WATCH_FLAG_RESET 1
#define CopyMemory memcpy
static void *GetProcessHeap() { return nullptr; }
static void *HeapAlloc(void *, unsigned, size_t n) { return calloc(1, n); }
struct SYNC { bool Valid; };
static void AdmissionUmdStagingInvalidate(SYNC *s) { s->Valid = false; }
struct ADMISSION_UMD_SCREEN_BUFFER {
  uint64_t Token; BOOL Active, Transition, HeapStaging, WriteWatch, Mapped, Queried, GpuWritten;
  BOOL Borrowed, Direct, SystemDirect, UploadRingPending, CopyHeld;
  BYTE *PrivateStaging, *HeapStagingShadow; void *LockedBase; ULONGLONG Bytes;
  unsigned KernelAllocation; ULONGLONG CanonicalGpuVa; SYNC Sync;
};
struct SRWLOCK { int shared, exclusive; };
struct ADMISSION_UMD_DEVICE {
  ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[4]; UINT ScreenBufferHighWater;
  volatile ULONGLONG *RenderFenceAddress; SRWLOCK ScreenBufferLock;
};
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) ((d)->ScreenBufferHighWater)
static void AcquireSRWLockShared(SRWLOCK *l) { assert(!l->exclusive); ++l->shared; }
static void ReleaseSRWLockShared(SRWLOCK *l) { assert(l->shared); --l->shared; }
static void AcquireSRWLockExclusive(SRWLOCK *l) { assert(!l->shared && !l->exclusive); l->exclusive = 1; }
static void ReleaseSRWLockExclusive(SRWLOCK *l) { assert(l->exclusive); l->exclusive = 0; }
static ADMISSION_UMD_SCREEN_BUFFER *find_slot(ADMISSION_UMD_DEVICE *d, uint64_t token) {
  for (UINT i = 0; i < d->ScreenBufferHighWater; ++i)
    if (d->ScreenBuffers[i].Active && d->ScreenBuffers[i].Token == token) return &d->ScreenBuffers[i];
  return nullptr;
}
/* OS write watch: the dirty set of one heap, unsorted like the real API. */
static BYTE *base; static size_t length;
static std::vector<size_t> dirty;
static unsigned watch_calls, resets;
static void (*between_preview_and_reset)();
static void write_byte(size_t at, BYTE value) { base[at] = value; dirty.push_back(at & ~size_t(4095)); }
static UINT GetWriteWatch(DWORD flags, PVOID ptr, SIZE_T bytes, PVOID *pages, ULONG_PTR *count, DWORD *granularity) {
  assert(ptr == base && bytes == length); ++watch_calls;
  if ((flags & WRITE_WATCH_FLAG_RESET) && between_preview_and_reset) { auto f = between_preview_and_reset; between_preview_and_reset = nullptr; f(); }
  std::sort(dirty.begin(), dirty.end()); dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());
  size_t n = std::min<size_t>(*count, dirty.size());
  for (size_t i = 0; i < n; ++i) pages[i] = base + dirty[dirty.size() - i - 1];
  *count = n; *granularity = 4096;
  if (flags & WRITE_WATCH_FLAG_RESET) { assert(n == dirty.size()); ++resets; dirty.clear(); }
  return 0;
}
@@FUNCTIONS@@
'''

STAGE_MAIN = r'''
static std::vector<BYTE> canonical, ring_memory;
/* The in-order GPU: run the recorded copies after every earlier job. */
static void gpu_execute(const ADMISSION_UMD_COPY_LIST &list) {
  for (unsigned i = 0; i < list.count; ++i)
    memcpy(canonical.data() + list.copies[i].Offset, ring_memory.data() + list.copies[i].RingOffset, list.copies[i].Bytes);
}
static void grow() { write_byte(9 * 4096 + 5, 0x5a); }
int main() {
  length = 64 * 4096; base = (BYTE *)calloc(1, length); canonical.assign(length, 0);
  ring_memory.assign(6 * 4096, 0xcc);
  ADMISSION_UMD_DEVICE d = {}; d.ScreenBufferHighWater = 1;
  ULONGLONG completed = 0; d.RenderFenceAddress = &completed;
  ADMISSION_UMD_SCREEN_BUFFER &s = d.ScreenBuffers[0];
  s.Token = 7; s.Active = s.HeapStaging = s.WriteWatch = s.Mapped = s.Queried = TRUE;
  s.PrivateStaging = base; s.LockedBase = base; s.Bytes = length; s.KernelAllocation = 3; s.CanonicalGpuVa = 1u << 20;
  s.Sync.Valid = true;
  ADMISSION_UMD_UPLOAD_RING ring = {};
  assert(AdmissionUmdRingInit(&ring.Ring, (unsigned)ring_memory.size()));
  ring.Cpu = ring_memory.data();
  ADMISSION_UMD_COPY_LIST list = {};
  /* clean heap: one preview, nothing consumed or recorded */
  assert(upload_ring_stage_slot(&d, 7, 0, &ring, list.copies, 8, &list.count) == 0);
  assert(watch_calls == 1 && !resets && !list.count && !s.UploadRingPending);
  /* persistent pointer stores: two runs into the ring, none into canonical */
  write_byte(4096 + 1, 0x11); write_byte(40 * 4096, 0x22);
  assert(upload_ring_stage_slot(&d, 7, 5, &ring, list.copies, 8, &list.count) == 1);
  assert(resets == 1 && dirty.empty() && s.UploadRingPending && list.count == 2);
  assert(list.copies[0].Offset == 4096 && list.copies[0].Bytes == 4096 && list.copies[0].Target == 5);
  assert(list.copies[1].Offset == 40 * 4096 && list.copies[1].Bytes == 4096);
  assert(ring_memory[list.copies[0].RingOffset + 1] == 0x11 && ring_memory[list.copies[1].RingOffset] == 0x22);
  assert(ring.Slots == 1 && ring.Copies == 2 && ring.Bytes == 8192);
  /* WAR: a previous-frame read still in flight sees the old canonical bytes */
  assert(canonical[4096 + 1] == 0 && canonical[40 * 4096] == 0);
  /* a store after the snapshot stays watched for the next Execute */
  write_byte(4096 + 2, 0x33);
  /* the copy submission is accepted at fence 10; the copy is not a GPU write */
  assert(upload_ring_finish(&d, &ring, 1, 10) == 1);
  assert(!s.UploadRingPending && !s.GpuWritten && s.Sync.Valid && !ring.Disabled);
  gpu_execute(list);
  assert(canonical[4096 + 1] == 0x11 && canonical[40 * 4096] == 0x22);
  /* WAR on the slice: fence 10 still running, the ring is full -> the escape
   * path keeps the dirty page (no reset) and the in-flight slice is untouched */
  const std::vector<BYTE> in_flight = ring_memory;
  list.count = 0; write_byte(41 * 4096, 0x44); write_byte(60 * 4096, 0x45); write_byte(62 * 4096, 0x46);
  upload_ring_retire(&d, &ring);
  assert(upload_ring_stage_slot(&d, 7, 0, &ring, list.copies, 8, &list.count) == 0);
  assert(resets == 1 && dirty.size() == 4 && !list.count && !s.UploadRingPending);
  assert(ring.Fallbacks == 1 && ring.FallbackBytes == 4 * 4096 && (ring.FallbackReasons & ADMISSION_UMD_UPLOAD_FALLBACK_SPACE));
  assert(ring_memory == in_flight);
  /* after completion the slice is reused and the new bytes go through it */
  completed = 10; upload_ring_retire(&d, &ring);
  assert(upload_ring_stage_slot(&d, 7, 0, &ring, list.copies, 8, &list.count) == 1);
  assert(dirty.empty() && list.count == 3 && ring_memory[list.copies[0].RingOffset + 2] == 0x33);
  assert(canonical[4096 + 2] == 0);
  /* an unknown submission: the consumed pages are restored by a full upload */
  assert(upload_ring_finish(&d, &ring, 0, 0) == 1);
  assert(!s.UploadRingPending && !s.Sync.Valid && ring.Disabled);
  assert(upload_ring_stage_slot(&d, 7, 0, &ring, list.copies, 8, &list.count) == 0);
  /* a fresh ring; stores between preview and reset that no longer fit */
  ring = {}; assert(AdmissionUmdRingInit(&ring.Ring, (unsigned)ring_memory.size())); ring.Cpu = ring_memory.data();
  s.Sync.Valid = true; list.count = 0; write_byte(0, 1); write_byte(2 * 4096, 2);
  between_preview_and_reset = grow;
  assert(upload_ring_stage_slot(&d, 7, 0, &ring, list.copies, 8, &list.count) == -1);
  assert(!s.Sync.Valid && !s.UploadRingPending && !list.count && ring.Invalidated == 1);
  assert(ring.Ring.Used == 0 && ring.Ring.Count == 0 && !ring.Ring.Pending);
  /* too many runs for the remaining copy capacity: nothing consumed */
  s.Sync.Valid = true; write_byte(0, 1); write_byte(20 * 4096, 2); write_byte(50 * 4096, 3);
  const unsigned before = resets;
  assert(upload_ring_stage_slot(&d, 7, 0, &ring, list.copies, 2, &list.count) == 0);
  assert(resets == before && dirty.size() == 3 && (ring.FallbackReasons & ADMISSION_UMD_UPLOAD_FALLBACK_COPIES));
  /* ineligible heaps never consume dirty bits */
  BOOL ADMISSION_UMD_SCREEN_BUFFER::*flags[] = {&ADMISSION_UMD_SCREEN_BUFFER::GpuWritten,
      &ADMISSION_UMD_SCREEN_BUFFER::Borrowed, &ADMISSION_UMD_SCREEN_BUFFER::SystemDirect,
      &ADMISSION_UMD_SCREEN_BUFFER::Transition, &ADMISSION_UMD_SCREEN_BUFFER::UploadRingPending};
  for (auto flag : flags) {
    s.*flag = TRUE; const unsigned calls = watch_calls;
    assert(upload_ring_stage_slot(&d, 7, 0, &ring, list.copies, 8, &list.count) == 0 && watch_calls == calls);
    s.*flag = FALSE;
  }
  s.Mapped = FALSE; { const unsigned calls = watch_calls;
    assert(upload_ring_stage_slot(&d, 7, 0, &ring, list.copies, 8, &list.count) == 0 && watch_calls == calls); }
  s.Mapped = TRUE; s.Sync.Valid = false; { const unsigned calls = watch_calls;
    assert(upload_ring_stage_slot(&d, 7, 0, &ring, list.copies, 8, &list.count) == 0 && watch_calls == calls); }
  s.Sync.Valid = true; s.HeapStagingShadow = base; { const unsigned calls = watch_calls;
    assert(upload_ring_stage_slot(&d, 7, 0, &ring, list.copies, 8, &list.count) == 0 && watch_calls == calls); }
  s.HeapStagingShadow = nullptr;
  assert(upload_ring_stage_slot(&d, 8, 0, &ring, list.copies, 8, &list.count) == 0);
  assert(dirty.size() == 3);
  free(ring.Scratch); free(base);
}
'''


class UploadRing(unittest.TestCase):
    def test_plan_coalesces_validates_and_never_truncates(self):
        build_and_run(self, PLAN, 'plan', cxx=False)
        build_and_run(self, PLAN, 'plan_cxx', cxx=True)

    def test_actual_stage_war_fallback_and_finish(self):
        text = GPU.read_text()
        names = ('heap_staging_page_compare', 'upload_ring_eligible', 'upload_ring_plan',
                 'upload_ring_invalidate', 'upload_ring_stage_slot', 'upload_ring_finish',
                 'upload_ring_retire')
        funcs = [function(text, n) for n in names]
        for name, body in zip(names, funcs):
            self.assertTrue(body, 'missing ' + name)
        harness = PRELUDE.replace('@@FUNCTIONS@@', '\n'.join(funcs))
        harness += r'''
struct ADMISSION_UMD_COPY_LIST { ADMISSION_UMD_UPLOAD_COPY copies[8]; unsigned count; };
''' + STAGE_MAIN
        build_and_run(self, harness, 'stage')

    def test_upload_copy_is_not_a_gpu_write_or_a_second_upload(self):
        text = GPU.read_text()
        body = function(text, 'mark_written')
        self.assertTrue(body)
        harness = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
using UINT=unsigned; using BOOL=int;
#define TRUE 1
#define FALSE 0
struct SRWLOCK { int x; };
static void AcquireSRWLockExclusive(SRWLOCK *) {}
static void ReleaseSRWLockExclusive(SRWLOCK *) {}
struct ADMISSION_UMD_SCREEN_BUFFER { uint64_t Token; BOOL Active, Transition, CopyHeld, GpuWritten, UploadRingPending; };
struct ADMISSION_UMD_DEVICE { ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[2]; UINT ScreenBufferHighWater; SRWLOCK ScreenBufferLock; };
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) ((d)->ScreenBufferHighWater)
''' + body + r'''
int main() {
  ADMISSION_UMD_DEVICE d = {}; d.ScreenBufferHighWater = 2;
  d.ScreenBuffers[0] = {1, TRUE, FALSE, TRUE, FALSE, TRUE};
  d.ScreenBuffers[1] = {2, TRUE, FALSE, TRUE, FALSE, FALSE};
  const uint64_t written[] = {1, 2};
  assert(mark_written(&d, written, 2));
  assert(!d.ScreenBuffers[0].GpuWritten && d.ScreenBuffers[1].GpuWritten);
  d.ScreenBuffers[0].UploadRingPending = FALSE;
  assert(mark_written(&d, written, 1) && d.ScreenBuffers[0].GpuWritten);
}
'''
        build_and_run(self, harness, 'written')
        held = function(text, 'transfer_held')
        skip = held[held.index('if(download ? !download_due(slot) :'):held.index('continue;\n    }', held.index('if(download ? !download_due(slot) :'))]
        self.assertIn('slot->UploadRingPending', skip.split('{', 1)[0],
                      'the copy submission must not upload the pages its GPU copy writes')

    def test_owner_records_copies_before_replay_and_flushes_alone(self):
        owner = OWNER.read_text()
        ddi = DDI.read_text()
        from test_umd_deallocated_slot_no_evict import function as extract
        execute = extract(ddi, 'ExecuteCommandLists')
        self.assertLess(execute.index('AgxD3d12OwnerMeasureHeapStaging(owner, TRUE)'),
                        execute.index('AgxD3d12OwnerStageUploads(owner)'))
        self.assertLess(execute.index('AgxD3d12OwnerStageUploads(owner)'), execute.index('ReplayAndSubmit('))
        body = function(owner.replace('extern "C" ', 'static '), 'AgxD3d12OwnerStageUploads')
        self.assertTrue(body, 'missing AgxD3d12OwnerStageUploads')
        # rc94: with APPLE_AGX_D3D12_UPLOAD_RING_ALONE=1 (upload_ring_alone)
        # the RC65 order below holds exactly; test_rc94_upload_ring_merge
        # covers the default, where the leading dispatches join the copies.
        helpers = [function(owner.replace('extern "C" ', 'static '), name)
                   for name in ('upload_ring_report', 'upload_ring_submitted', 'AgxD3d12OwnerUploadBoundary')]
        self.assertTrue(all(helpers), 'missing rc94 upload helpers')
        body = '\n'.join(helpers + [body])
        body = re.sub(r'(?m)^#(if|ifdef|endif|else).*\n', '', body)
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include "umd_upload_ring.h"
using UINT=unsigned; using BOOL=int; using LONGLONG=long long;
#define S_OK 0
#define E_FAIL (-1)
struct pipe_box { int x, y, z, width, height, depth; };
struct pipe_resource { int id; };
struct pipe_context;
static std::vector<int> events; /* 1 copy, 2 flush, 3 finish */
static unsigned long long newest = 4; static bool advance = true, faulted;
static void copy_region(pipe_context *, pipe_resource *dst, unsigned level, unsigned x, unsigned y, unsigned z,
                        pipe_resource *src, unsigned src_level, const pipe_box *box) {
  assert(!level && !y && !z && !src_level && src->id == 99 && dst->id == 10 && x == 4096 && box->x == 256 &&
         box->width == 8192 && box->height == 1 && box->depth == 1);
  events.push_back(1);
}
static void flush(pipe_context *, void *, unsigned) { events.push_back(2); if (advance) ++newest; }
struct pipe_context { decltype(&copy_region) resource_copy_region; decltype(&flush) flush; };
struct ADMISSION_UMD_DEVICE { int x; };
struct UPLOAD_TARGET { pipe_resource *Resource; unsigned long long Token; };
struct AGX_D3D12_OWNER {
  pipe_context *Context; ADMISSION_UMD_DEVICE Runtime; UINT PendingDraws, PendingCopies, PendingDispatches;
  pipe_resource *UploadRingResource; ADMISSION_UMD_UPLOAD_RING UploadRing;
  UPLOAD_TARGET *UploadTargets; UINT UploadTargetCount;
  ADMISSION_UMD_UPLOAD_COPY UploadCopies[ADMISSION_UMD_UPLOAD_RING_COPIES];
  UINT UploadDeferred, UploadDeferredCopies, UploadDeferredJoined, UploadDeferredStageUs;
  unsigned long long UploadDeferredBefore;
};
struct OwnerLock { explicit OwnerLock(AGX_D3D12_OWNER *) {} };
#define AGX_D3D12_UPLOAD_RING_MERGED 8u
static bool upload_ring_alone() { return true; }
static LONGLONG qpc_now() { return 0; }
static UINT qpc_us(LONGLONG) { return 0; }
static unsigned long long AgxWin32AsahiContextNewestFence(pipe_context *) { return newest; }
static int AgxWin32AsahiContextFaulted(pipe_context *) { return faulted; }
static unsigned retired, staged; static int accepted_seen = -1; static unsigned long long fence_seen;
static void AdmissionUmdGpuvaUploadRingRetire(ADMISSION_UMD_DEVICE *, ADMISSION_UMD_UPLOAD_RING *) { ++retired; }
static int AdmissionUmdGpuvaUploadRingStage(ADMISSION_UMD_DEVICE *, unsigned long long token, unsigned target,
    ADMISSION_UMD_UPLOAD_RING *ring, ADMISSION_UMD_UPLOAD_COPY *copies, unsigned capacity, unsigned *count) {
  ++staged; assert(capacity == ADMISSION_UMD_UPLOAD_RING_COPIES && events.empty());
  if (token != 77) return 0;
  copies[*count] = {4096, 256, 8192, target}; ++*count; ++ring->Slots; return 1;
}
static unsigned AdmissionUmdGpuvaUploadRingFinish(ADMISSION_UMD_DEVICE *, ADMISSION_UMD_UPLOAD_RING *, int accepted,
                                                  unsigned long long fence) {
  events.push_back(3); accepted_seen = accepted; fence_seen = fence; return 1;
}
static unsigned receipts;
static void AdmissionUmdDiagnostic(const char *name, long, const UINT *, UINT n) {
  assert(!strcmp(name, "measure-d3d12-upload-ring") && n == 15); ++receipts;
}
''' + body + r'''
int main() {
  pipe_context ctx = {copy_region, flush}; pipe_resource ring = {99}, heap = {10}, other = {11};
  UPLOAD_TARGET targets[] = {{&other, 76}, {&heap, 77}};
  AGX_D3D12_OWNER o = {}; o.Context = &ctx; o.UploadRingResource = &ring; o.UploadTargets = targets; o.UploadTargetCount = 2;
  assert(AdmissionUmdRingInit(&o.UploadRing.Ring, 1u << 20));
  AgxD3d12OwnerStageUploads(&o);
  assert(retired == 1 && staged == 2 && events == std::vector<int>({1, 2, 3}) && accepted_seen == 1 && fence_seen == 5);
  assert(receipts == 1);
  /* no fence for the copies: an unknown submission */
  events.clear(); advance = false; AgxD3d12OwnerStageUploads(&o);
  assert(events == std::vector<int>({1, 2, 3}) && accepted_seen == 0);
  advance = true; faulted = true; events.clear(); AgxD3d12OwnerStageUploads(&o);
  assert(accepted_seen == 0); faulted = false;
  /* recorded-but-unsubmitted work: the copies would join it; keep the escape path */
  events.clear(); staged = 0; o.PendingDispatches = 1; AgxD3d12OwnerStageUploads(&o);
  assert(events.empty() && !staged); o.PendingDispatches = 0;
  /* disabled ring: no work at all */
  o.UploadRing.Disabled = 1; retired = 0; AgxD3d12OwnerStageUploads(&o);
  assert(events.empty() && !staged && !retired);
}
'''
        build_and_run(self, harness, 'owner')


if __name__ == '__main__':
    unittest.main()
