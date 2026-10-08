"""EXP1066: a render submission returns once queued; retirement completes it.

EXP1063 DWM: 4.0 ms of every 5.0 ms submission was the synchronous
completion wait inside submit(), while the GPU job itself took ~1.5 ms
(Wom1JobTiming971) and DWM's CPU work between submissions ~3.3 ms ran
serially after it. The winsys already keeps one submission held until it
retires it through wait_render, so the completion work moves there.
Invariants:
- submit() queues the job and its internal fence signal, records the
  pending fence and returns internal+1 without waiting or downloading;
- the first wait for a fence beyond the pending one waits for the internal
  fence, downloads the held GPU-written slots, signals internal+1 (in that
  order) and only then waits for the requested fence;
- the completion work runs exactly once; later or earlier waits do not
  repeat it;
- a failed completion is terminal and fails the wait.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'


def function(text, name):
    m = re.search(r'(?:static )?[A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


BODY = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
typedef int BOOL; typedef unsigned UINT; typedef unsigned D3DKMT_HANDLE;
typedef unsigned long long UINT64;
#define TRUE 1
#define FALSE 0
struct ADMISSION_UMD_DEVICE { D3DKMT_HANDLE RenderSyncObject; UINT64 PendingRenderFence;
  UINT PendingRenderSequence; BOOL DrawTerminal; };
static std::string calls; static bool fail_wait, fail_download;
static int wait_object(ADMISSION_UMD_DEVICE *, D3DKMT_HANDLE, uint64_t f) {
  calls += "w" + std::to_string(f) + " "; return !fail_wait; }
static int transfer_held(ADMISSION_UMD_DEVICE *, bool download) {
  calls += download ? "d " : "u "; return !fail_download; }
static int signal_render(ADMISSION_UMD_DEVICE *, uint64_t f) {
  calls += "s" + std::to_string(f) + " "; return 1; }
@@FUNCS@@
int main() {
  ADMISSION_UMD_DEVICE d = {7u, 0, 0, FALSE};
  /* Nothing pending: a wait is a plain wait. */
  assert(wait_render(&d, 4) && calls == "w4 ");
  calls.clear(); d.PendingRenderFence = 5; d.PendingRenderSequence = 9;
  /* An older fence does not complete the queued job. */
  assert(wait_render(&d, 5) && calls == "w5 " && d.PendingRenderFence == 5);
  calls.clear();
  assert(wait_render(&d, 6));
  if (calls != "w5 d s6 w6 ") { printf("order: %s\n", calls.c_str()); return 1; }
  assert(!d.PendingRenderFence && !d.DrawTerminal);
  calls.clear(); assert(wait_render(&d, 6) && calls == "w6 ");
  /* A failed completion is terminal and fails the wait. */
  calls.clear(); d.PendingRenderFence = 7; fail_download = true;
  assert(!wait_render(&d, 8) && d.DrawTerminal && !d.PendingRenderFence && calls == "w7 d ");
  puts("EXP1066 deferred render completion: PASS");
}
'''


class DeferredRenderCompletion(unittest.TestCase):
    def test_retirement_completes_the_queued_job_once(self):
        text = SRC.read_text()
        funcs = '\n'.join(function(text, n) for n in ('complete_render', 'wait_render'))
        self.assertIn('complete_render', funcs, 'deferred completion is missing')
        funcs = re.sub(r'#if defined\(APPLE_AGX_EXP907_FRAME_RECEIPT\).*?#endif\n', '', funcs, flags=re.S)
        body = BODY.replace('@@FUNCS@@', funcs)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'; exe = Path(tmp) / 'replay'
            src.write_text(body)
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP1066 deferred render completion: PASS', ran.stdout)

    def test_submit_does_not_wait_for_its_job(self):
        submit = function(SRC.read_text(), 'submit')
        self.assertNotIn('wait_object(', submit)
        self.assertNotIn('transfer_held(device,true)', submit)
        self.assertIn('device->PendingRenderFence=internal;', submit)


if __name__ == '__main__':
    unittest.main()
