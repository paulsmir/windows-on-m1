from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py"


class NativeEmptyFlushTests(unittest.TestCase):
    def test_empty_flush_does_not_poison_context(self):
        source = GENERATOR.read_text()
        match = re.search(r"s=body\(s,'agx_flush_batch','''(.*?)'''\)", source, re.S)
        self.assertIsNotNone(match)
        program = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
struct agx_batch { bool active, submitted, initialized, clear, compute; unsigned draws; struct { void *bo; unsigned char *current; } vdm, cdm; };
struct agx_context { bool any_faults; };
struct drm_asahi_cmd_compute { unsigned flags; };
struct drm_asahi_cmd_render;
static unsigned aborts, resets, submissions, compute_submissions;
static bool abort_allowed=true, compute_enter_allowed=true;
static bool agx_batch_is_compute(struct agx_batch *batch) { return batch->compute; }
static bool AgxWin32AsahiBatchComputeEnter(struct agx_batch *batch) { (void)batch; return compute_enter_allowed; }
static bool AgxWin32AsahiBatchComputeLeave(struct agx_batch *batch) { (void)batch; return true; }
static bool AgxWin32AsahiBatchComputeFinalize(struct agx_batch *batch, const void *end) {
  return end == batch->cdm.current;
}
static void agx_flush_compute(struct agx_context *ctx, struct agx_batch *batch,
                              struct drm_asahi_cmd_compute *compute) {
  (void)ctx; (void)batch; compute->flags = 0;
}
static void agx_batch_submit(struct agx_context *ctx, struct agx_batch *batch,
                             struct drm_asahi_cmd_compute *compute,
                             struct drm_asahi_cmd_render *render) {
  (void)ctx; assert(compute && !render); batch->submitted = true; ++compute_submissions;
}
static bool agx_batch_is_active(struct agx_batch *batch) { return batch->active; }
static bool agx_batch_is_submitted(struct agx_batch *batch) { return batch->submitted; }
static bool AgxWin32AsahiBatchAbort(struct agx_batch *batch) { (void)batch; ++aborts; return abort_allowed; }
static void agx_batch_reset(struct agx_context *ctx, struct agx_batch *batch) {
  (void)ctx; ++resets; batch->active=false;
}
static void agx_flush_batch(struct agx_context *ctx, struct agx_batch *batch) {
@@BODY@@
}
int main(void) {
  struct agx_context context={0};
  struct agx_batch batch={.active=true,.draws=0};
  agx_flush_batch(&context,&batch);
  assert(!context.any_faults && !batch.active && aborts==1 && resets==1 && submissions==0);
  batch.active=true; batch.initialized=true; batch.vdm.bo=(void*)1;
  agx_flush_batch(&context,&batch);
  assert(context.any_faults && aborts==2 && resets==2 && submissions==0);
  /* A CDM encoder in a render batch (geometry-shader prerasterization) has
   * no KMD contract yet: the context faults. */
  context.any_faults=false; batch.active=true; batch.initialized=false; batch.vdm.bo=0; batch.cdm.bo=(void*)1;
  agx_flush_batch(&context,&batch);
  assert(context.any_faults && aborts==3 && resets==3 && submissions==0);
  /* G2: a compute-only batch submits one compute command. */
  context.any_faults=false; batch.active=true; batch.compute=true;
  static unsigned char cdm_map[64]; batch.cdm.current=cdm_map+16;
  agx_flush_batch(&context,&batch);
  assert(!context.any_faults && batch.submitted && compute_submissions==1 &&
         aborts==3 && resets==3);
  /* ... and faults the context when the backend refuses its CDM scope. */
  batch.submitted=false; batch.active=true; compute_enter_allowed=false;
  agx_flush_batch(&context,&batch);
  assert(context.any_faults && compute_submissions==1);
  compute_enter_allowed=true; batch.compute=false; batch.submitted=false;
  batch.cdm.bo=0;
  context.any_faults=false; batch.active=true; batch.draws=1; batch.initialized=true;
  agx_flush_batch(&context,&batch);
  assert(context.any_faults && aborts==4 && resets==4 && submissions==0);
  context.any_faults=false; batch.active=true; batch.draws=0; batch.initialized=false; abort_allowed=false;
  agx_flush_batch(&context,&batch);
  assert(context.any_faults && batch.active && aborts==5 && resets==4 && submissions==0);
  context.any_faults=false; batch.active=true; batch.draws=0; batch.initialized=true;
  batch.clear=true; batch.vdm.bo=(void*)1; abort_allowed=true;
  agx_flush_batch(&context,&batch);
  assert(!context.any_faults && batch.active && aborts==5 && resets==4 && submissions==0);
  return 0;
}
'''
        body = match.group(1)
        body = body[:body.index("   struct drm_asahi_cmd_compute compute_storage;")]
        program = program.replace("@@BODY@@", body)
        with tempfile.TemporaryDirectory() as directory:
            source_path = Path(directory) / "empty_flush.c"
            executable = Path(directory) / "empty_flush"
            source_path.write_text(program)
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", str(source_path),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
