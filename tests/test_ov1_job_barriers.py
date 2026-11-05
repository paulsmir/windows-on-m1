"""OV1 receipt-only: measure-d3d12-job-barriers, the D3D12 barriers between
consecutive submitted GPU jobs (drivers/apple-agx/d3d12/agx_d3d12_job_barriers.h).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- Barrier logging must never touch the replayed command stream (no command
  slot, unchanged list->commands) and must record nothing while the switch is
  off: the receipt may not change replay or submission order.
- The record keeps the pinned-WDK fields (type, flags, transition states, the
  resource of every barrier kind of both ResourceBarrier entries, null UAV =
  global) at the index of the command it precedes.
- Attribution: a batch submitted inside a command's owner work (Mesa's lazy
  flush of the previous job) takes the gap before that command, one between
  commands takes the barriers since the last command, each gap is reported
  once, and intra-batch gaps never leak into a later boundary.
"""
from pathlib import Path
import hashlib
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
D3D12 = ROOT / 'drivers/apple-agx/d3d12'
DDI = D3D12 / 'agx_d3d12_ddi.cpp'
OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
WDK = ROOT / '.local/reference/wdk26100/d3d12umddi.h'
WDK_SHA256 = 'd338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28'


def block(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def typedef(header, name, kind='struct'):
    m = re.search(r'typedef ' + kind + r' ' + name + r'\s*\{.*?\}\s*' + name + r'\s*;', header, re.S)
    if not m:
        raise AssertionError(name)
    return m.group(0)


ACCUMULATOR = r'''
#include <assert.h>
#include "agx_d3d12_job_barriers.h"
static AGX_D3D12_JOB_BARRIERS j;
static unsigned v[AGX_D3D12_JOB_BARRIER_VALUES];
int main(void) {
  const unsigned a = AgxD3d12JobBarrierId(0x1000), b = AgxD3d12JobBarrierId(0x2000);
  assert(a && b && a != b && AgxD3d12JobBarrierId(0) == 0);
  AgxD3d12JobBarriersInit(&j);
  /* render job: draws, an intra-batch UAV barrier, draws */
  AgxD3d12JobBarrierBegin(&j, 5); AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierAdd(&j, 2, 0, 0, 0, a);
  AgxD3d12JobBarrierBegin(&j, 5); AgxD3d12JobBarrierEnd(&j);
  /* RT -> SRV on two resources (one repeated), then a dispatch whose owner
   * work flushes the render batch (lazy) */
  AgxD3d12JobBarrierAdd(&j, 0, 0, 0x4, 0x40, a);
  AgxD3d12JobBarrierAdd(&j, 0, 0, 0x4, 0x80, b);
  AgxD3d12JobBarrierAdd(&j, 0, 0, 0x10, 0x40, a);
  AgxD3d12JobBarrierBegin(&j, 2);
  AgxD3d12JobBarrierBatch(&j, 1, v);
  assert(v[0] == 1 && v[1] == 0 && v[2] == 3 && v[3] == 3 && v[4] == 0 && v[5] == 0);
  assert(v[6] == 0x14 && v[7] == 0xc0 && v[8] == 2 && v[9] == (a ^ b) && v[10] == 0);
  assert(v[11] == 2 && v[12] == 4); /* trigger: dispatch; total includes the intra-batch UAV barrier */
  /* a second batch in the same flush gets nothing */
  AgxD3d12JobBarrierBatch(&j, 2, v);
  assert(v[0] == 2 && v[1] == 1 && v[2] == 0 && v[11] == 2 && v[12] == 0);
  AgxD3d12JobBarrierEnd(&j);
  /* global UAV barrier and aliasing between commands, then the replay's end
   * submits (no command in flight): the barriers since the last command */
  AgxD3d12JobBarrierAdd(&j, 2, 0, 0, 0, 0);
  AgxD3d12JobBarrierAdd(&j, 3, 0x8, 0, 0, b);
  AgxD3d12JobBarrierAdd(&j, 1, 0, 0, 0, b);
  AgxD3d12JobBarrierAdd(&j, 3, 0x0, 0, 0, b); /* ranged, not aliasing */
  AgxD3d12JobBarrierBatch(&j, 2, v);
  assert(v[1] == 2 && v[2] == 4 && v[3] == 0 && v[4] == 1 && v[5] == 2 && v[8] == 1 && v[9] == b);
  assert(v[10] == 1 && v[11] == ((0x8u) << 16) && v[12] == 4);
  /* an unconsumed gap (no submission in its command) is intra-batch */
  AgxD3d12JobBarrierAdd(&j, 0, 0x1, 0x8, 0x40, a);
  AgxD3d12JobBarrierBegin(&j, 5); AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBegin(&j, 5);
  AgxD3d12JobBarrierBatch(&j, 1, v);
  assert(v[2] == 0 && v[11] == 5 && v[12] == 1);
  /* more resources than the id table: flagged, counted up to the table */
  {
    unsigned i;
    AgxD3d12JobBarrierEnd(&j);
    for (i = 0; i < AGX_D3D12_JOB_BARRIER_IDS + 3u; ++i)
      AgxD3d12JobBarrierAdd(&j, 2, 0, 0, 0, AgxD3d12JobBarrierId(0x10000ull + i * 64ull));
    AgxD3d12JobBarrierBatch(&j, 1, v);
    assert(v[2] == AGX_D3D12_JOB_BARRIER_IDS + 3u && v[8] == AGX_D3D12_JOB_BARRIER_IDS && (v[11] & 0x100u));
  }
  return 0;
}
'''

RECORDER = r'''
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#define APPLE_AGX_D3D12_NATIVE_OWNER 1
using UINT = unsigned; using UINT64 = uint64_t; using SIZE_T = size_t; using BOOL = int; using VOID = void;
#define APIENTRY
struct D3D12DDI_HRESOURCE { void *pDrvPrivate; };
typedef UINT D3D12DDI_RESOURCE_STATES;
/* WDK_TYPES */
extern "C" {
#include "agx_d3d12_job_barriers.h"
}
static void *GetProcessHeap() { return nullptr; }
static void *HeapAlloc(void *, unsigned, size_t n) { return std::malloc(n); }
static void *HeapReAlloc(void *, unsigned, void *p, size_t n) { return std::realloc(p, n); }
static BOOL enabled;
static BOOL AgxD3d12OwnerJobBarriersEnabled() { return enabled; }
static constexpr UINT kListCommands = 1u << 20;
struct AgxD3d12Heap { int id; };
struct AgxD3d12Resource { AgxD3d12Heap *heap; };
struct AgxD3d12JobBarrierRecord;
struct AgxD3d12CommandList {
    UINT commands;
    struct AgxD3d12JobBarrierRecord *job_barriers;
    UINT job_barrier_count, job_barrier_capacity;
    UINT job_barriers_lost; /* OV2 */
};
/* RECORDER */
static void check(bool ok) { if (!ok) std::abort(); }
int main() {
    AgxD3d12Heap h1 = {1}, h2 = {2};
    AgxD3d12Resource r1 = {&h1}, r2 = {&h2}, r3 = {nullptr};
    AgxD3d12CommandList list = {};
    D3D12DDIARG_RESOURCE_BARRIER_0003 old[4] = {};
    old[0].Type = D3D12DDI_RESOURCE_BARRIER_TYPE_TRANSITION;
    old[0].Transition.hResource.pDrvPrivate = &r1;
    old[0].Transition.StateBefore = 0x4;
    old[0].Transition.StateAfter = 0x40;
    old[1].Type = D3D12DDI_RESOURCE_BARRIER_TYPE_UAV;  /* null: global */
    old[2].Type = D3D12DDI_RESOURCE_BARRIER_TYPE_ALIASING;
    old[2].Aliasing.hResourceBefore.pDrvPrivate = &r2;
    old[3].Type = D3D12DDI_RESOURCE_BARRIER_TYPE_UAV;
    old[3].Flags = D3D12DDI_RESOURCE_BARRIER_FLAG_END_ONLY;
    old[3].UAV.hResource.pDrvPrivate = &r3;
    list.commands = 7;
    RecordJobBarriers(&list, 4, old);
    /* OV2: barriers recorded while logging is off make the list unknown */
    check(list.job_barrier_count == 0 && list.commands == 7 && !list.job_barriers &&
          list.job_barriers_lost == 1);
    list.job_barriers_lost = 0;
    enabled = 1;
    RecordJobBarriers(&list, 4, old);
    check(list.job_barriers_lost == 0);
    check(list.commands == 7 && list.job_barrier_count == 4);
    const AgxD3d12JobBarrierRecord *r = list.job_barriers;
    check(r[0].position == 7 && r[0].type == 0 && r[0].before == 0x4 && r[0].after == 0x40 &&
          r[0].id == AgxD3d12JobBarrierId((uintptr_t)&r1));
    check(r[1].type == 2 && r[1].id == 0);
    check(r[2].type == 1 && r[2].before == 0 && r[2].id == AgxD3d12JobBarrierId((uintptr_t)&r2));
    check(r[3].flags == 2 && r[3].id == AgxD3d12JobBarrierId((uintptr_t)&r3));
    /* OV3: the heap a resolve destination is matched by */
    check(r[0].heap == &h1 && r[1].heap == nullptr && r[2].heap == &h2 && r[3].heap == nullptr);
    D3D12DDIARG_RESOURCE_BARRIER_0022 ranged[70] = {};
    for (int i = 0; i < 70; ++i) {
        ranged[i].Type = D3D12DDI_RESOURCE_BARRIER_TYPE_0022_RANGED;
        ranged[i].Flags = D3D12DDI_RESOURCE_BARRIER_FLAG_0022_ALIASING;
        ranged[i].Ranged.hResource.pDrvPrivate = &r2;
    }
    list.commands = 9;
    RecordJobBarriers(&list, 70, ranged);
    check(list.commands == 9 && list.job_barrier_count == 74 && list.job_barrier_capacity >= 74);
    r = list.job_barriers;
    check(r[3].position == 7 && r[4].position == 9 && r[73].type == 3 && r[73].flags == 8 &&
          r[73].id == AgxD3d12JobBarrierId((uintptr_t)&r2));
    std::free(list.job_barriers);
    return 0;
}
'''


def compile_run(source, name, compiler, std, include):
    with tempfile.TemporaryDirectory(prefix='ov1-job-barriers-') as tmp:
        src = Path(tmp) / name
        exe = Path(tmp) / 'run'
        src.write_text(source)
        build = subprocess.run([compiler, std, '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                                '-I', str(include), str(src), '-o', str(exe)],
                               capture_output=True, text=True, timeout=120)
        if build.returncode:
            return build.returncode, build.stdout + build.stderr
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
        return run.returncode, run.stdout + run.stderr


class JobBarriers(unittest.TestCase):
    def test_boundary_attribution(self):
        code, out = compile_run(ACCUMULATOR, 'acc.c', 'clang', '-std=c99', D3D12)
        self.assertEqual(code, 0, out)

    def test_recording_never_touches_the_command_stream(self):
        self.assertEqual(hashlib.sha256(WDK.read_bytes()).hexdigest(), WDK_SHA256)
        wdk = WDK.read_text(encoding='utf-8-sig')
        types = '\n'.join([
            typedef(wdk, 'D3D12DDI_RESOURCE_BARRIER_TYPE', 'enum'),
            typedef(wdk, 'D3D12DDI_RESOURCE_BARRIER_FLAGS', 'enum'),
            typedef(wdk, 'D3D12DDI_RANGE'),
            typedef(wdk, 'D3D12DDI_RESOURCE_TRANSITION_BARRIER_0003'),
            typedef(wdk, 'D3D12DDI_RESOURCE_ALIASING_BARRIER'),
            typedef(wdk, 'D3D12DDI_RESOURCE_UAV_BARRIER'),
            typedef(wdk, 'D3D12DDIARG_RESOURCE_BARRIER_0003'),
            typedef(wdk, 'D3D12DDI_RESOURCE_RANGED_BARRIER_0022'),
            typedef(wdk, 'D3D12DDIARG_RESOURCE_BARRIER_0022'),
        ])
        ddi = DDI.read_text()
        recorder = (block(ddi, 'struct AgxD3d12JobBarrierRecord {') + ';\n' +
                    block(ddi, 'template<class Barrier> static void RecordJobBarriers(') + '\n')
        self.assertNotIn('AppendCommand', recorder)
        fixture = RECORDER.replace('/* WDK_TYPES */', types).replace('/* RECORDER */', recorder)
        code, out = compile_run(fixture, 'rec.cpp', 'clang++', '-std=c++17', D3D12)
        self.assertEqual(code, 0, out)

    def test_wiring(self):
        ddi = DDI.read_text()
        self.assertEqual(ddi.count('RecordJobBarriers(list, count, barriers);'), 2)
        self.assertIn('list->job_barrier_count = 0;', ddi)
        self.assertIn('scratch->job_barriers = nullptr;', ddi)
        # the replay brackets owner work and feeds trailing barriers
        self.assertIn('if (job_command) AgxD3d12OwnerJobCommand(command.kind);', ddi)
        self.assertIn('if (job_command) AgxD3d12OwnerJobCommand(0);', ddi)
        owner = OWNER.read_text()
        self.assertIn('"APPLE_AGX_D3D12_JOB_BARRIERS"', owner)
        self.assertIn('AdmissionUmdDiagnostic("measure-d3d12-job-barriers", S_OK, job, AGX_D3D12_JOB_BARRIER_VALUES);',
                      owner)


if __name__ == '__main__':
    unittest.main()
