"""rc94: the RC65 upload-ring copies share the Execute's first compute job.

EXP2028a (GravityMark D3D12 HD 50k, 4273 steady frames): every Execute
starts with the RC65 ring copies submitted alone, a compute job with 8 us of
GPU work (c_empty) behind 0.31 ms of launch and completion, then the
Execute's leading compute dispatches in a job of their own (pos5, kick
0.24 ms). The copies now wait in the context's compute batch, so those
dispatches join their job, until the first command that is not a plain
dispatch (or the replay's end) flushes it. APPLE_AGX_D3D12_UPLOAD_RING_ALONE=1
restores the RC65 submission.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- GPU order: the copies run before every other command of the Execute. Only
  plain dispatches may join their batch (one CDM stream, a barrier after
  every launch); any other command, a dispatch that may write a copy target
  and the replay's end submit them first.
- Finish (UploadRingPending, ring slices) happens exactly once per staging,
  with the fence of the submission that carried the copies; a submission the
  dispatches caused themselves counts, a missing fence or a fault does not.
- A dispatch writing a copy target must not share their submission:
  mark_written skips UploadRingPending slots, so that write would never be
  downloaded.
- The kill switch keeps the RC65 order (copy, flush, finish).
"""
from pathlib import Path
import re
import unittest

from test_umd_staging_write_watch import function
from test_umd_upload_ring import build_and_run

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'drivers/apple-agx'
OWNER = DRIVER / 'mesa/winsys/agx_d3d12_windows.cpp'
DDI = DRIVER / 'd3d12/agx_d3d12_ddi.cpp'
HEADER = DRIVER / 'mesa/winsys/agx_d3d12_windows.h'


def owner_function(text, name):
    body = function(text.replace('extern "C" ', 'static '), name)
    return re.sub(r'(?m)^#(if|ifdef|ifndef|endif|else).*\n', '', body)


HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include "umd_upload_ring.h"
using UINT=unsigned; using BOOL=int; using LONGLONG=long long;
#define S_OK 0
#define AGX_D3D12_UPLOAD_RING_MERGED 8u
struct pipe_box { int x, y, z, width, height, depth; };
struct pipe_resource { int id; };
struct pipe_context;
static std::vector<int> events; /* 1 copy, 2 flush, 3 finish */
static unsigned long long newest = 4; static bool faulted, alone;
static void copy_region(pipe_context *, pipe_resource *, unsigned, unsigned, unsigned, unsigned,
                        pipe_resource *src, unsigned, const pipe_box *) { assert(src->id == 99); events.push_back(1); }
static void flush(pipe_context *, void *, unsigned) { events.push_back(2); ++newest; }
struct pipe_context { decltype(&copy_region) resource_copy_region; decltype(&flush) flush; };
struct ADMISSION_UMD_DEVICE { int x; };
typedef struct { void *Resource; } AGX_D3D12_BUFFER;
#define AGX_D3D12_UAV_SLOTS 8
typedef struct { const AGX_D3D12_BUFFER *Buffer; const AGX_D3D12_BUFFER *Counter; } AGX_D3D12_UAV;
typedef struct { const AGX_D3D12_BUFFER *Writes; unsigned WriteCount; } AGX_D3D12_HEAP_BINDING;
enum { AGX_D3D12_HEAP_CS = 5, AGX_D3D12_HEAP_STAGES = 6 };
struct UPLOAD_TARGET { pipe_resource *Resource; unsigned long long Token; };
struct AGX_D3D12_OWNER {
  pipe_context *Context; ADMISSION_UMD_DEVICE Runtime; UINT PendingDraws, PendingCopies, PendingDispatches;
  AGX_D3D12_HEAP_BINDING PendingHeaps[AGX_D3D12_HEAP_STAGES];
  pipe_resource *UploadRingResource; ADMISSION_UMD_UPLOAD_RING UploadRing;
  UPLOAD_TARGET *UploadTargets; UINT UploadTargetCount;
  ADMISSION_UMD_UPLOAD_COPY UploadCopies[ADMISSION_UMD_UPLOAD_RING_COPIES];
  UINT UploadDeferred, UploadDeferredCopies, UploadDeferredJoined, UploadDeferredStageUs;
  unsigned long long UploadDeferredBefore;
};
struct OwnerLock { explicit OwnerLock(AGX_D3D12_OWNER *) {} };
static LONGLONG qpc_now() { return 0; }
static UINT qpc_us(LONGLONG) { return 0; }
static unsigned long long AgxWin32AsahiContextNewestFence(pipe_context *) { return newest; }
static int AgxWin32AsahiContextFaulted(pipe_context *) { return faulted; }
static bool upload_ring_alone() { return alone; }
static void AdmissionUmdGpuvaUploadRingRetire(ADMISSION_UMD_DEVICE *, ADMISSION_UMD_UPLOAD_RING *) {}
static int AdmissionUmdGpuvaUploadRingStage(ADMISSION_UMD_DEVICE *, unsigned long long token, unsigned target,
    ADMISSION_UMD_UPLOAD_RING *ring, ADMISSION_UMD_UPLOAD_COPY *copies, unsigned, unsigned *count) {
  if (token != 77) return 0;
  copies[*count] = {4096, 256, 8192, target}; ++*count; ++ring->Slots; return 1;
}
static int finishes, accepted_seen = -1; static unsigned long long fence_seen;
static unsigned AdmissionUmdGpuvaUploadRingFinish(ADMISSION_UMD_DEVICE *, ADMISSION_UMD_UPLOAD_RING *, int accepted,
                                                  unsigned long long fence) {
  events.push_back(3); ++finishes; accepted_seen = accepted; fence_seen = fence; return 1;
}
static unsigned ring_receipts, merge_receipts, status_seen, joined_seen, reason_seen;
static void AdmissionUmdDiagnostic(const char *name, long, const UINT *v, UINT n) {
  if (!strcmp(name, "measure-d3d12-upload-ring")) { assert(n == 15); ++ring_receipts; status_seen = v[10]; return; }
  assert(!strcmp(name, "measure-d3d12-upload-merge") && n == 4);
  ++merge_receipts; joined_seen = v[0]; reason_seen = v[3];
}
@@FUNCTIONS@@
static void reset() { events.clear(); finishes = 0; ring_receipts = merge_receipts = 0; accepted_seen = -1; }
int main() {
  pipe_context ctx = {copy_region, flush}; pipe_resource ring = {99}, heap = {10}, other = {11};
  UPLOAD_TARGET targets[] = {{&other, 76}, {&heap, 77}};
  AGX_D3D12_OWNER o = {}; o.Context = &ctx; o.UploadRingResource = &ring; o.UploadTargets = targets; o.UploadTargetCount = 2;
  assert(AdmissionUmdRingInit(&o.UploadRing.Ring, 1u << 20));

  /* default: the copies wait for the leading dispatches */
  AgxD3d12OwnerStageUploads(&o);
  assert(events == std::vector<int>({1}) && o.UploadDeferred && !ring_receipts && o.UploadDeferredBefore == 4);
  AgxD3d12OwnerUploadBoundary(&o, 1);              /* a plain dispatch joins */
  assert(events == std::vector<int>({1}) && o.UploadDeferred);
  ++o.UploadDeferredJoined;                        /* AgxD3d12OwnerDispatch counts it */
  AgxD3d12OwnerUploadBoundary(&o, 0);              /* any other command submits them first */
  assert(events == std::vector<int>({1, 2, 3}) && !o.UploadDeferred && finishes == 1);
  assert(accepted_seen == 1 && fence_seen == 5 && ring_receipts == 1 && (status_seen & 9u) == 9u);
  assert(merge_receipts == 1 && joined_seen == 1 && reason_seen == 0);
  AgxD3d12OwnerUploadBoundary(&o, 0);              /* finished once */
  assert(finishes == 1 && events.size() == 3);

  /* a submission the joined dispatches caused carried the copies: no flush */
  reset(); AgxD3d12OwnerStageUploads(&o); ++o.UploadDeferredJoined; ++newest;
  AgxD3d12OwnerUploadBoundary(&o, 1);
  assert(events == std::vector<int>({1, 3}) && accepted_seen == 1 && fence_seen == newest && !o.UploadDeferred);

  /* nothing joined: the RC65 status, no merged bit */
  reset(); AgxD3d12OwnerStageUploads(&o); AgxD3d12OwnerUploadBoundary(&o, 0);
  assert(events == std::vector<int>({1, 2, 3}) && (status_seen & 8u) == 0 && (status_seen & 1u));

  /* a fault is never an accepted copy submission */
  reset(); AgxD3d12OwnerStageUploads(&o); faulted = true; AgxD3d12OwnerUploadBoundary(&o, 1);
  assert(events == std::vector<int>({1, 2, 3}) && accepted_seen == 0); faulted = false;

  /* a staging never outlives the next Execute's staging */
  reset(); AgxD3d12OwnerStageUploads(&o); AgxD3d12OwnerStageUploads(&o);
  assert(events == std::vector<int>({1, 2, 3, 1}) && finishes == 1 && o.UploadDeferred);
  AgxD3d12OwnerUploadBoundary(&o, 0); assert(finishes == 2);

  /* a dispatch that may write a copy target cannot share their submission */
  reset(); AgxD3d12OwnerStageUploads(&o);
  AGX_D3D12_BUFFER hb = {&heap}, ob = {&other};
  AGX_D3D12_UAV uavs[AGX_D3D12_UAV_SLOTS] = {};
  assert(!upload_deferred_written(&o, uavs));
  uavs[3].Buffer = &ob; assert(!upload_deferred_written(&o, uavs));
  uavs[3].Buffer = &hb; assert(upload_deferred_written(&o, uavs));
  uavs[3].Buffer = nullptr; uavs[7].Counter = &hb; assert(upload_deferred_written(&o, uavs));
  uavs[7].Counter = nullptr;
  AGX_D3D12_BUFFER writes[] = {ob, hb};
  o.PendingHeaps[AGX_D3D12_HEAP_CS] = {writes, 1}; assert(!upload_deferred_written(&o, uavs));
  o.PendingHeaps[AGX_D3D12_HEAP_CS] = {writes, 2}; assert(upload_deferred_written(&o, uavs));
  o.PendingHeaps[AGX_D3D12_HEAP_CS] = {nullptr, 0};
  AgxD3d12OwnerUploadBoundary(&o, 2);
  assert(events == std::vector<int>({1, 2, 3}) && reason_seen == 2);
  o.UploadDeferredCopies = 0; uavs[3].Buffer = &hb; assert(!upload_deferred_written(&o, uavs));

  /* kill switch: RC65 copy, flush, finish */
  reset(); alone = true; AgxD3d12OwnerStageUploads(&o);
  assert(events == std::vector<int>({1, 2, 3}) && !o.UploadDeferred && ring_receipts == 1 && !merge_receipts);
  assert((status_seen & 8u) == 0);
  return 0;
}
'''


class UploadRingMerge(unittest.TestCase):
    def test_copies_join_only_leading_dispatches(self):
        text = OWNER.read_text()
        names = ('upload_ring_report', 'upload_ring_submitted', 'upload_deferred_written',
                 'AgxD3d12OwnerUploadBoundary', 'AgxD3d12OwnerStageUploads')
        funcs = [owner_function(text, n) for n in names]
        for name, body in zip(names, funcs):
            self.assertTrue(body, 'missing ' + name)
        build_and_run(self, HARNESS.replace('@@FUNCTIONS@@', '\n'.join(funcs)), 'merge')

    def test_replay_and_dispatch_call_the_boundary(self):
        ddi = DDI.read_text()
        start = ddi.index('static HRESULT ReplayAndSubmit(')
        replay = ddi[start:ddi.index('measure-d3d12-replay-window', start)]
        loop = replay[replay.index('const AgxD3d12Command &command = list->command[c];'):]
        boundary = 'AgxD3d12OwnerUploadBoundary(owner, command.kind == kCommandDispatch ? 1 : 0);'
        self.assertIn(boundary, loop)
        # before any command runs, the clear deferral included
        self.assertLess(loop.index(boundary), loop.index('if ((command.kind == kCommandClear'))
        # the replay's end submits them before the queue submission
        tail = replay[replay.index('QueryPerformanceCounter(&replayed);') - 400:]
        self.assertLess(tail.index('AgxD3d12OwnerUploadBoundary('), tail.index('AgxD3d12OwnerSubmitForQueue('))
        owner = OWNER.read_text()
        dispatch = function(owner.replace('extern "C" ', 'static '), 'AgxD3d12OwnerDispatch')
        self.assertLess(dispatch.index('upload_deferred_written(owner, uavs)'), dispatch.index('bind_stage('))
        self.assertIn('AgxD3d12OwnerUploadBoundary(owner, 2)', dispatch)
        self.assertIn('void AgxD3d12OwnerUploadBoundary(AGX_D3D12_OWNER *Owner, int Dispatch);', HEADER.read_text())
        # the switch is read without the CRT getenv (MSVC /sdl)
        alone = function(owner, 'upload_ring_alone')
        self.assertIn('GetEnvironmentVariableA("APPLE_AGX_D3D12_UPLOAD_RING_ALONE"', alone)


if __name__ == '__main__':
    unittest.main()
