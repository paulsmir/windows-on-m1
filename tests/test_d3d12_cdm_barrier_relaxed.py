"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
RC73 (EXP1991 GravityMark: 174 dispatches per Execute, ~32 us fixed per
dispatch + CDM barrier): D3D12 owner grid launches skip the native CDM
barrier between them unless the application put a ResourceBarrier between
them. The model compiles the projected agx_launch_internal with the real
helper (agx_win32_cdm_barrier.h) and checks, over the encoded CDM streams:

- every pair of launches D3D12 or native Asahi orders (an app barrier or a
  list boundary between them, either launch internal, a flush between them)
  has a barrier between them in the stream (a dropped app barrier is caught);
- every stream that holds a launch ends with a barrier before its terminate;
- without the owner opt-in, for render batches and for compute blits the
  stream is byte-for-byte native (launch, barrier, launch, barrier ...);
- 64 independent dispatches cost one barrier, a 64-step chain separated by
  app barriers costs 64;
- no encoder write, deferred barrier or terminate passes cdm.end;
- the projection, the owner and the DDI wire it at the right places.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import ROOT, local_tree, project
from test_d3d12_owner_allocation_failure import function

WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'
SCRIPTS = ROOT / 'drivers/apple-agx/mesa/scripts'
HELPER = WINSYS / 'agx_win32_cdm_barrier.h'
OWNER = WINSYS / 'agx_d3d12_windows.cpp'
DDI = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'

PRELUDE = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#undef NDEBUG
#include <assert.h>
#define AGX_CDM_LAUNCH_WORD_0_LENGTH 4
#define AGX_CDM_LAUNCH_WORD_1_LENGTH 4
#define AGX_CDM_UNK_G14X_LENGTH 8
#define AGX_CDM_INDIRECT_LENGTH 8
#define AGX_CDM_GLOBAL_SIZE_LENGTH 12
#define AGX_CDM_LOCAL_SIZE_LENGTH 12
#define AGX_CDM_BARRIER_LENGTH 4
#define AGX_CDM_STREAM_TERMINATE_LENGTH 8
#define AGX_MAX_BATCHES 4
#define BITSET_TEST(set, i) ((((set)[(i) / 32]) >> ((i) % 32)) & 1u)
typedef int mesa_shader_stage;
enum agx_chip { AGX_CHIP_G13G, AGX_CHIP_G14X };
struct agx_device { enum agx_chip chip; };
struct pipe_screen { struct agx_device dev; };
struct pipe_context { struct pipe_screen *screen; };
struct agx_grid { unsigned id; };
struct agx_workgroup { unsigned x; };
struct agx_cdm_launch_word_0_packed { uint32_t opaque; };
struct agx_encoder { void *bo; uint8_t *current, *end; };
struct agx_context;
struct agx_batch {
   struct agx_context *ctx;
   struct agx_encoder cdm;
   bool compute;
   bool windows_cdm_owed;
};
struct agx_context {
   struct pipe_context base;
   unsigned windows_cdm_relaxed, windows_cdm_user_launch;
   unsigned windows_cdm_counts[6];
   struct { struct agx_batch slots[AGX_MAX_BATCHES]; uint32_t active[1]; } batches;
   struct { bool active; } compute_blitter;
};
static struct agx_device *agx_device(struct pipe_screen *s) { return &s->dev; }
static struct agx_context *agx_context(struct pipe_context *p) { return (struct agx_context *)p; }
static bool agx_batch_is_compute(struct agx_batch *b) { return b->compute; }

/* Encoded words: launch = 0xA0000000 | id followed by filler up to the
 * largest G14X launch (40 bytes); barrier = 0xBBBBBBBB; terminate = 0xEEEEEEEE x2. */
#define LAUNCH_WORDS 10
#define BARRIER_WORD 0xBBBBBBBBu
#define TERMINATE_WORD 0xEEEEEEEEu
static struct agx_batch *writing; /* the batch whose stream is encoded */
static void check_room(const uint32_t *out, unsigned bytes) {
   assert(writing && (const uint8_t *)out + bytes <= writing->cdm.end && "encoder overflow");
}
static uint32_t *agx_cdm_launch(uint32_t *out, enum agx_chip chip, struct agx_grid grid,
                                struct agx_workgroup wg, struct agx_cdm_launch_word_0_packed l, uint32_t usc) {
   (void)chip; (void)wg; (void)l; (void)usc;
   check_room(out, LAUNCH_WORDS * 4);
   out[0] = 0xA0000000u | grid.id;
   for (unsigned i = 1; i < LAUNCH_WORDS; ++i) out[i] = 0;
   return out + LAUNCH_WORDS;
}
static uint32_t *agx_cdm_barrier(uint32_t *out, enum agx_chip chip) {
   (void)chip; check_room(out, 4); *out = BARRIER_WORD; return out + 1;
}
static void agx_flush_batch_for_reason(struct agx_context *ctx, struct agx_batch *batch, const char *reason);
'''

HARNESS = r'''
/* ---- model of the submitted streams and the required ordering ---- */
#define MAX_EVENTS 200000
enum { EV_LAUNCH, EV_APP, EV_FLUSH };
struct event { int kind; unsigned id; bool internal; struct agx_batch *batch; };
static struct event events[MAX_EVENTS];
static unsigned nevents, next_id = 1;
/* per launch id: stream (submission) index and position of its launch word */
static unsigned stream_of[MAX_EVENTS], pos_of[MAX_EVENTS];
static uint32_t streams[4096][4096];
static unsigned stream_len[4096], nstreams;
static uint8_t storage[AGX_MAX_BATCHES][1u << 16];
static unsigned encoder_bytes = 1u << 16;
static struct pipe_screen screen;
static struct agx_context ctx;
static bool drop_app_barriers; /* RED: a driver that loses app barriers */

static void batch_init(struct agx_batch *b, bool compute) {
   unsigned slot = (unsigned)(b - ctx.batches.slots);
   b->ctx = &ctx; b->compute = compute;
   b->cdm.bo = storage[slot]; b->cdm.current = storage[slot];
   b->cdm.end = storage[slot] + encoder_bytes;
   b->windows_cdm_owed = false; /* agx_batch_init (projected) */
   ctx.batches.active[0] |= 1u << slot;
}

/* agx_flush_compute (projected): settle, terminate, submit. */
static void flush(struct agx_batch *b) {
   writing = b;
   agx_win32_cdm_settle(b, AGX_WIN32_CDM_SETTLED_FLUSH);
   uint32_t *out = (uint32_t *)b->cdm.current;
   check_room(out, AGX_CDM_STREAM_TERMINATE_LENGTH);
   out[0] = out[1] = TERMINATE_WORD;
   b->cdm.current += AGX_CDM_STREAM_TERMINATE_LENGTH;
   unsigned words = (unsigned)((b->cdm.current - (uint8_t *)b->cdm.bo) / 4);
   assert(nstreams < 4096 && words <= 4096);
   memcpy(streams[nstreams], b->cdm.bo, words * 4);
   stream_len[nstreams++] = words;
   events[nevents++] = (struct event){EV_FLUSH, 0, false, b};
   batch_init(b, b->compute);
}
static void agx_flush_batch_for_reason(struct agx_context *c, struct agx_batch *b, const char *reason) {
   (void)c; (void)reason; flush(b);
}

static void encode(struct agx_batch *b, bool internal) {
   unsigned id = next_id++;
   assert(id < 0x10000000u && nevents < MAX_EVENTS);
   events[nevents++] = (struct event){EV_LAUNCH, id, internal, b};
   writing = b;
   struct agx_grid grid = {id};
   agx_launch_internal(b, grid, (struct agx_workgroup){64}, (struct agx_cdm_launch_word_0_packed){0}, 0, 0);
}
/* agx_launch_grid (projected): only the grid's own launch is marked. */
static void user_dispatch(struct agx_batch *b) {
   ctx.windows_cdm_user_launch = ctx.windows_cdm_relaxed && !ctx.compute_blitter.active;
   encode(b, false);
   ctx.windows_cdm_user_launch = false;
}
static void internal_launch(struct agx_batch *b) { encode(b, true); }
static void app_barrier(void) {
   events[nevents++] = (struct event){EV_APP, 0, false, NULL};
   if (!drop_app_barriers) AgxWin32AsahiCdmBarrier(&ctx.base);
}

/* Decode the streams; return the number of violated orderings. */
static unsigned barriers_total;
static unsigned verify(void) {
   memset(stream_of, 0xff, sizeof(stream_of));
   barriers_total = 0;
   for (unsigned s = 0; s < nstreams; ++s) {
      bool any = false, last_barrier = false;
      for (unsigned w = 0; w < stream_len[s]; ++w) {
         uint32_t v = streams[s][w];
         if ((v & 0xF0000000u) == 0xA0000000u) {
            unsigned id = v & 0x0FFFFFFFu;
            stream_of[id] = s; pos_of[id] = w; any = true; last_barrier = false;
            w += LAUNCH_WORDS - 1;
         } else if (v == BARRIER_WORD) {
            ++barriers_total; last_barrier = true;
         } else if (v == TERMINATE_WORD) {
            assert(w + 2 == stream_len[s] && "terminate ends the stream");
            break;
         } else {
            assert(0 && "unknown CDM word");
         }
      }
      if (any) assert(last_barrier && "a stream with launches ends with a barrier");
   }
   unsigned violations = 0;
   /* For each launch b, the earlier launches of its batch it must follow
    * (other batches are ordered by Gallium's writer/reader batch flushes). */
   for (unsigned j = 0; j < nevents; ++j) {
      if (events[j].kind != EV_LAUNCH) continue;
      bool app = false, flushed = false;
      for (unsigned i = j; i-- > 0;) {
         if (events[i].kind == EV_APP) { app = true; continue; }
         if (events[i].batch != events[j].batch) continue;
         if (events[i].kind == EV_FLUSH) { flushed = true; continue; }
         unsigned a = events[i].id, b = events[j].id;
         bool required = app || flushed || events[i].internal || events[j].internal;
         if (!required) continue;
         assert(stream_of[a] != 0xffffffffu && stream_of[b] != 0xffffffffu);
         if (stream_of[a] != stream_of[b]) { assert(stream_of[a] < stream_of[b]); continue; }
         bool ordered = false;
         for (unsigned w = pos_of[a] + LAUNCH_WORDS; w < pos_of[b]; ++w)
            ordered |= streams[stream_of[a]][w] == BARRIER_WORD;
         if (!ordered) ++violations;
      }
   }
   return violations;
}
static void reset(bool relaxed) {
   memset(&ctx, 0, sizeof(ctx));
   ctx.base.screen = &screen;
   ctx.windows_cdm_relaxed = relaxed;
   nevents = nstreams = 0; next_id = 1; drop_app_barriers = false;
   encoder_bytes = 1u << 16;
   batch_init(&ctx.batches.slots[0], true);  /* compute batch */
   batch_init(&ctx.batches.slots[1], false); /* render batch with a CDM encoder */
}
static unsigned counts[6];
static void receipt(void) { AgxWin32AsahiCdmReceipt(&ctx.base, counts, 6); }

int main(void) {
   struct agx_batch *compute = &ctx.batches.slots[0];
   screen.dev.chip = AGX_CHIP_G14X;

   /* 64 independent dispatches: one barrier, before the terminate. */
   reset(true);
   for (unsigned i = 0; i < 64; ++i) user_dispatch(&ctx.batches.slots[0]);
   flush(compute);
   assert(!verify() && barriers_total == 1);
   receipt();
   assert(counts[AGX_WIN32_CDM_RELAXED_LAUNCHES] == 64 && counts[AGX_WIN32_CDM_SETTLED_FLUSH] == 1);
   assert(!counts[AGX_WIN32_CDM_NATIVE_LAUNCHES] && !counts[AGX_WIN32_CDM_SETTLED_APP]);

   /* A 64-step chain, each step after an app (UAV) barrier: 64 barriers,
    * each between the producer and its consumer. */
   reset(true);
   for (unsigned i = 0; i < 64; ++i) { user_dispatch(compute); app_barrier(); }
   flush(compute);
   assert(!verify() && barriers_total == 64);
   receipt();
   assert(counts[AGX_WIN32_CDM_SETTLED_APP] == 64 && counts[AGX_WIN32_CDM_APP_BARRIERS] == 64 &&
          !counts[AGX_WIN32_CDM_SETTLED_FLUSH]);
   /* Consecutive app barriers and a barrier before any launch emit nothing extra. */
   reset(true);
   app_barrier(); user_dispatch(compute); app_barrier(); app_barrier(); user_dispatch(compute);
   flush(compute);
   assert(!verify() && barriers_total == 2);

   /* RED: a driver that drops the app barriers is caught by the model. */
   reset(true);
   drop_app_barriers = true;
   for (unsigned i = 0; i < 64; ++i) { user_dispatch(compute); app_barrier(); }
   flush(compute);
   assert(verify() > 0 && barriers_total == 1);

   /* Internal (libagx) launches keep a barrier before and after them. */
   reset(true);
   user_dispatch(compute); user_dispatch(compute); internal_launch(compute); user_dispatch(compute);
   internal_launch(compute); internal_launch(compute); user_dispatch(compute);
   flush(compute);
   assert(!verify());
   /* stream: L L B I B L B I B I B L B */
   assert(barriers_total == 6);

   /* Native: without the opt-in, for compute blits and on render batches. */
   for (unsigned mode = 0; mode < 3; ++mode) {
      reset(mode != 0);
      ctx.compute_blitter.active = mode == 1;
      struct agx_batch *b = mode == 2 ? &ctx.batches.slots[1] : compute;
      for (unsigned i = 0; i < 20; ++i) user_dispatch(b);
      flush(b);
      assert(!verify() && barriers_total == 20 && stream_len[0] == 20 * (LAUNCH_WORDS + 1) + 2);
      for (unsigned i = 0; i < 20; ++i) {
         assert((streams[0][i * (LAUNCH_WORDS + 1)] & 0xF0000000u) == 0xA0000000u);
         assert(streams[0][i * (LAUNCH_WORDS + 1) + LAUNCH_WORDS] == BARRIER_WORD);
      }
      receipt();
      assert(!counts[AGX_WIN32_CDM_RELAXED_LAUNCHES] && counts[AGX_WIN32_CDM_NATIVE_LAUNCHES] == 20);
   }
   /* The mark is one-shot: an internal launch right after a grid is native. */
   reset(true);
   ctx.windows_cdm_user_launch = true; encode(compute, false); internal_launch(compute);
   flush(compute);
   receipt();
   assert(counts[AGX_WIN32_CDM_RELAXED_LAUNCHES] == 1 && counts[AGX_WIN32_CDM_NATIVE_LAUNCHES] == 1);

   /* GravityMark-like random mixes in a tiny encoder: CDM-overfull flushes
    * land everywhere, nothing writes past cdm.end, every required ordering
    * holds, and only barriers between relaxed launches are skipped. */
   unsigned seed = 0x7301u;
   for (unsigned round = 0; round < 400; ++round) {
      reset(true);
      encoder_bytes = 64u + 4u * (round % 97u) + (round & 1u ? 200u : 0u);
      screen.dev.chip = round & 2u ? AGX_CHIP_G14X : AGX_CHIP_G13G;
      unsigned ops = 50 + round % 300;
      for (unsigned k = 0; k < ops; ++k) {
         seed = seed * 1103515245u + 12345u;
         unsigned r = (seed >> 16) % 100u;
         if (r < 70) user_dispatch(compute);
         else if (r < 82) app_barrier();
         else if (r < 92) internal_launch(compute);
         else if (r < 95) flush(compute);
         else { struct agx_batch *render = &ctx.batches.slots[1]; user_dispatch(render); }
      }
      flush(compute); flush(&ctx.batches.slots[1]);
      assert(!verify());
   }
   return 0;
}
'''


def compile_run(test, source, name):
    with tempfile.TemporaryDirectory() as td:
        p = Path(td)
        (p / (name + '.c')).write_text(source)
        cmd = ['clang', '-std=c11', '-Werror', '-Wall', '-Wno-unused-function',
               '-fsanitize=address,undefined', '-Dstatic_assert=_Static_assert',
               '-I' + str(WINSYS), str(p / (name + '.c')), '-o', str(p / name)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        test.assertEqual(r.returncode, 0, r.stderr)
        r = subprocess.run([str(p / name)], capture_output=True, text=True, timeout=300)
        test.assertEqual(r.returncode, 0, r.stderr + r.stdout)


class CdmBarrierRelaxed(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.td = tempfile.TemporaryDirectory()
        cls.out = project(Path(cls.td.name) / 'native')

    @classmethod
    def tearDownClass(cls):
        cls.td.cleanup()

    def projected(self, name):
        self.assertIsNotNone(self.out, 'reference Mesa tree is missing')
        return (self.out / 'src/gallium/drivers/asahi' / name).read_text()

    def test_model_orders_every_app_barrier_and_skips_only_between_relaxed_launches(self):
        self.assertTrue(HELPER.exists(), 'relaxed CDM barrier helper is missing')
        launch = function(self.projected('agx_state.c'), 'static void\nagx_launch_internal(')
        source = (PRELUDE + '#define AGX_WIN32_CDM_BARRIER_ENTRIES 1\n#include "agx_win32_cdm_barrier.h"\n'
                  + launch + HARNESS)
        compile_run(self, source, 'cdm_model')

    def test_projection_wires_the_launch_the_grid_and_the_flush(self):
        state = self.projected('agx_state.c')
        launch = function(state, 'static void\nagx_launch_internal(')
        self.assertIn('agx_win32_cdm_launch(batch, out, grid, wg, launch, usc)', launch)
        self.assertNotIn('agx_cdm_barrier(', launch)
        # The native overflow bound is unchanged (the model proves it suffices).
        self.assertIn('''      AGX_CDM_GLOBAL_SIZE_LENGTH + AGX_CDM_LOCAL_SIZE_LENGTH +
      AGX_CDM_BARRIER_LENGTH;

   if (batch->cdm.current + dispatch_upper_bound >= batch->cdm.end)''', launch)
        self.assertLess(state.index('#include "agx_win32_cdm_barrier.h"'),
                        state.index('static void\nagx_launch_internal('))
        self.assertEqual(state.count('#define AGX_WIN32_CDM_BARRIER_ENTRIES'), 1)
        grid = function(state, 'static void\nagx_launch_grid(')
        mark = grid.index('ctx->windows_cdm_user_launch = ctx->windows_cdm_relaxed && !ctx->compute_blitter.active;')
        self.assertLess(grid.index('libagx_increment_cs_invocations('), mark)
        self.assertLess(grid.index('agx_batch_init_state(batch);'), mark)
        native = grid.index('agx_launch(batch, grid, wg, cs, NULL, MESA_SHADER_COMPUTE,')
        self.assertLess(mark, native)
        self.assertLess(native, grid.index('ctx->windows_cdm_user_launch = false;'))
        self.assertEqual(state.count('windows_cdm_user_launch = ctx->windows_cdm_relaxed'), 1)
        # Every other launch path still reaches the native barrier through
        # agx_launch_internal; no other CDM writer was added.
        self.assertEqual(state.count('agx_cdm_launch('), 0)
        pipe = self.projected('agx_pipe.c')
        flush = function(pipe, 'static void\nagx_flush_compute(')
        self.assertLess(flush.index('agx_win32_cdm_settle(batch, AGX_WIN32_CDM_SETTLED_FLUSH);'),
                        flush.index('CDM_STREAM_TERMINATE'))
        self.assertEqual(pipe.count('CDM_STREAM_TERMINATE'), 1)
        batch = self.projected('agx_batch.c')
        self.assertIn('   batch->clear = 0;\n   batch->windows_cdm_owed = false;\n', batch)
        header = self.projected('agx_state.h')
        self.assertIn('bool windows_cdm_owed;', header)
        self.assertIn('unsigned windows_cdm_relaxed, windows_cdm_user_launch;', header)
        self.assertIn('unsigned windows_cdm_counts[6];', header)
        script = (SCRIPTS / 'build-native-asahi-state.py').read_text()
        self.assertLess(script.index('native-asahi-query-group.py'), script.index('native-asahi-cdm-barrier.py'))

    def test_owner_relaxes_only_its_grid_launches_and_replays_barriers(self):
        owner = OWNER.read_text()
        dispatch = function(owner, 'extern "C" HRESULT AgxD3d12OwnerDispatch(')
        # Each of the two grid launches (indirect records, direct) is
        # bracketed by the relaxed scope, and nothing else in it.
        launches = [m.start() for m in re.finditer(r'pipe->launch_grid\(pipe, &info\);', dispatch)]
        self.assertEqual(len(launches), 2)
        for at in launches:
            before = dispatch.rindex('AgxWin32AsahiCdmRelaxed(pipe, owner->CdmRelaxed);', 0, at)
            self.assertNotIn('bind_heaps(', dispatch[before:at])
            self.assertLess(at, dispatch.index('AgxWin32AsahiCdmRelaxed(pipe, 0);', at))
            self.assertLess(dispatch.index('AgxWin32AsahiCdmRelaxed(pipe, 0);', at), dispatch.index('timing.Mark', at))
        barrier = function(owner, 'extern "C" HRESULT AgxD3d12OwnerBarrier(')
        self.assertIn('AgxWin32AsahiCdmBarrier(owner->Context);', barrier)
        self.assertIn('"APPLE_AGX_D3D12_CDM_BARRIERS"', owner)
        self.assertIn('"measure-d3d12-cdm-barriers"', owner)
        scene = (WINSYS / 'agx_win32_asahi_scene.h').read_text()
        for name in ('AgxWin32AsahiCdmRelaxed', 'AgxWin32AsahiCdmBarrier', 'AgxWin32AsahiCdmReceipt'):
            self.assertLess(scene.index('extern "C"'), scene.index(name))
        self.assertIn('HRESULT AgxD3d12OwnerBarrier(AGX_D3D12_OWNER *Owner, int ListBoundary);',
                      (WINSYS / 'agx_d3d12_windows.h').read_text())

    def test_ddi_records_every_resource_barrier_and_replays_it_in_order(self):
        ddi = DDI.read_text()
        for name in ('static VOID APIENTRY ResourceBarrier(', 'static VOID APIENTRY ResourceBarrier0022('):
            body = function(ddi, name)
            self.assertIn('RecordBarrier(handle, count);', body, name)
        record = function(ddi, 'static void RecordBarrier(')
        self.assertIn('slot->kind = kCommandBarrier;', record)
        replay = function(ddi, 'static HRESULT ReplayAndSubmit(')
        self.assertIn('hr = AgxD3d12OwnerBarrier(owner, 0);', replay)
        self.assertIn('AgxD3d12OwnerBarrier(static_cast<AGX_D3D12_OWNER *>(list->device->owner), 1)', replay)
        # The barrier branch precedes the catch-all dispatch replay.
        self.assertLess(replay.index('command.kind == kCommandBarrier'), replay.index('hr = ReplayDispatch(owner, command.dispatch);'))
        self.assertIsNotNone(re.search(r'kCommandBarrier = 15\b', ddi))


if __name__ == '__main__':
    unittest.main()
