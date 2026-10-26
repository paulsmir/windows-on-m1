"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Query resolve acquires a native compute batch, whose cleanup polls must never
turn timeout zero into a GPU wait. Every GPUVA batch must own an immutable,
GPU-only timestamp output before submission, retained until capsule release.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body
from test_g4_batch_begin_keeps_active import PROGRAM, SRC, SPACE, ROOT


class QueryBatchTimeline(unittest.TestCase):
    def replay(self, main):
        source = SRC.read_text()
        functions = '\n'.join(body(source,n) for n in (
            'backend','capsule','batch_refuse','release_lease','AgxWin32AsahiBatchPoll'))
        space_functions = '\n'.join(body(SPACE.read_text(),n) for n in (
            'AgxWin32GpuvaRetire','AgxWin32GpuvaComplete','AgxWin32GpuvaFenceHeld'))
        code = PROGRAM.split('@@FUNCTIONS@@')[0].replace('@@SPACE@@',space_functions)+functions+main
        with tempfile.TemporaryDirectory(prefix='query-batch-') as td:
            p=Path(td);(p/'test.c').write_text(code)
            build=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',
                '-I'+str(ROOT/'drivers/apple-agx/mesa/winsys'),
                '-I'+str(ROOT/'drivers/apple-agx/shared/include'),str(p/'test.c'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stderr)
            run=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)

    def test_zero_timeout_does_not_retire_or_wait_for_pending_submission(self):
        self.replay(r'''
int main(void) {
 AGX_WIN32_ASAHI_BACKEND b={0}; struct pipe_screen screen={{&b}};
 struct agx_context ctx={0};ctx.base.screen=&screen;
 AGX_G4_BATCH g={0};g.Submitted=1;g.Fence=7;
 struct agx_batch batch={0};batch.ctx=&ctx;batch.windows_batch=&g;
 b.Gpuva.Ops.QueryRender=query_render;b.Gpuva.Ops.WaitRender=wait_render;
 completed_fence=6;
 assert(!AgxWin32AsahiBatchPoll(&batch,0));assert(!waits && !g.Retired);
 completed_fence=7;
 assert(AgxWin32AsahiBatchPoll(&batch,0));assert(g.Retired);
 g.Retired=0;g.Fence=8;
 assert(AgxWin32AsahiBatchPoll(&batch,1000));assert(g.Retired && completed_fence==8);
 return 0;
}
''')

if __name__=='__main__':unittest.main()
