from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCENE = ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_asahi_scene.c"
BATCH = ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c"


def function(source, name):
    match = re.search(r"(?:static )?int " + name + r"\([^;]*\)\s*\{", source)
    if match is None:
        return None
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def compile_and_run(program):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "clear_only.c"
        executable = Path(directory) / "clear_only"
        source.write_text(program)
        subprocess.run([
            os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
            "-Werror", "-fsanitize=address,undefined", str(source),
            "-o", str(executable),
        ], check=True, cwd=ROOT)
        subprocess.run([str(executable)], check=True, cwd=ROOT)


class NativeClearOnlyPresentTests(unittest.TestCase):
    def test_present_flush_submits_clear_only_batch(self):
        extracted = function(SCENE.read_text(), "AgxWin32AsahiContextFlushForPresent")
        self.assertIsNotNone(extracted)
        program = r'''
#include <assert.h>
#include <stddef.h>
struct pipe_context { void (*flush)(struct pipe_context *,void *,unsigned); };
struct agx_batch { void *windows_batch; unsigned draws, clear; };
#define AGX_MAX_BATCHES 2
struct agx_context { struct pipe_context base; struct { struct agx_batch slots[AGX_MAX_BATCHES]; } batches; struct agx_batch *batch; int any_faults; };
static unsigned flushes, aborts, resets;
static struct agx_context *agx_context(struct pipe_context *ctx) { return (struct agx_context *)ctx; }
static int AgxWin32AsahiBatchAbort(struct agx_batch *batch) { (void)batch; ++aborts; return 1; }
static void agx_batch_reset(struct agx_context *ctx,struct agx_batch *batch) { (void)batch; ++resets; ctx->batch=NULL; }
static void pipe_flush(struct pipe_context *ctx,void *fence,unsigned flags) { (void)ctx;(void)fence;(void)flags;++flushes; }
@@FUNCTION@@
int main(void) {
 struct agx_context context={0};
 context.base.flush=pipe_flush;
 struct agx_batch *batch=&context.batches.slots[0];
 batch->windows_batch=batch;batch->clear=1;context.batch=batch;
 assert(AgxWin32AsahiContextFlushForPresent(&context.base));
 assert(flushes==1 && aborts==0 && resets==0);
 batch->clear=0;context.batch=batch;
 assert(AgxWin32AsahiContextFlushForPresent(&context.base));
 assert(flushes==1 && aborts==1 && resets==1);
 return 0;
}
'''
        compile_and_run(program.replace("@@FUNCTION@@", extracted))

    def test_gpuva_finish_accepts_clear_only_work(self):
        extracted = function(BATCH.read_text(), "batch_has_render_work")
        self.assertIsNotNone(extracted)
        program = r'''
#include <assert.h>
struct agx_batch { unsigned draws, clear; };
@@FUNCTION@@
int main(void) {
 struct agx_batch batch={0};
 assert(!batch_has_render_work(&batch));
 batch.clear=1;assert(batch_has_render_work(&batch));
 batch.clear=0;batch.draws=1;assert(batch_has_render_work(&batch));
 return 0;
}
'''
        compile_and_run(program.replace("@@FUNCTION@@", extracted))


if __name__ == "__main__":
    unittest.main()
