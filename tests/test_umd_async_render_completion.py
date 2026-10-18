"""EXP1082: a render submission returns once queued unless it must download.

EXP1081 DWM: 1.69 ms of every submission (~118 per second, ~12 ms of every
~55 ms frame) was the synchronous completion wait inside submit(); the CPU
built the next batch only afterwards. Invariants:
- with no held GPU-written CPU-visible slot (mapped or shared, not
  presentation-direct), submit's completion queues: it records the pending
  fence, returns that fence and neither waits, downloads nor signals;
- otherwise it completes as before: wait, download, signal internal+1,
  return internal+1 (that slot's staging is current when submit returns);
- the completion set is exactly the set transfer_held(true) downloads;
- the first wait for the pending fence (or a later one) waits for it and
  downloads before returning; earlier waits do not complete it; it runs once;
- completion issues no GPU-signal callback (EXP1066: D3D11 crashed in
  SignalSynchronizationObjectFromGpu2CB when device destruction retired the
  last submission);
- a failed completion is terminal and fails the wait;
- query_render reads the monitored fence without waiting;
- submit completes a still-pending job before this job's uploads.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'


def function(text, name):
    m = re.search(r'(?m)^static [A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
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
enum { AppleAgxWin32BufferGpuWrite = 8u };
struct ADMISSION_UMD_SCREEN_BUFFER { BOOL CopyHeld, Direct, SystemDirect, GpuWritten, Mapped, Borrowed; UINT Flags; };
struct ADMISSION_UMD_DEVICE { int ScreenBufferLock; ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[3];
  UINT ScreenBufferHighWater; D3DKMT_HANDLE RenderSyncObject; volatile UINT64 *RenderFenceAddress;
  UINT64 NextRenderFence, PendingRenderFence; UINT PendingRenderSequence; BOOL DrawTerminal; };
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device) ((Device)->ScreenBufferHighWater)
static void AcquireSRWLockShared(int *) {}
static void ReleaseSRWLockShared(int *) {}
static std::string calls; static bool fail_wait, fail_download;
static int wait_object(ADMISSION_UMD_DEVICE *, D3DKMT_HANDLE, uint64_t f) {
  calls += "w" + std::to_string(f) + " "; return !fail_wait; }
static int transfer_held(ADMISSION_UMD_DEVICE *, bool download) {
  calls += download ? "d " : "u "; return !fail_download; }
static int signal_render(ADMISSION_UMD_DEVICE *, uint64_t f) {
  calls += "s" + std::to_string(f) + " "; return 1; }
@@FUNCS@@
static ADMISSION_UMD_SCREEN_BUFFER due() {
  ADMISSION_UMD_SCREEN_BUFFER s = {TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, AppleAgxWin32BufferGpuWrite};
  return s;
}
int main() {
  volatile UINT64 signalled = 0;
  ADMISSION_UMD_DEVICE d = {};
  d.RenderSyncObject = 7u; d.RenderFenceAddress = &signalled; d.ScreenBufferHighWater = 3;
  uint64_t fence = 0;
  /* Quiet render target: queued, nothing waited, downloaded or signalled. */
  d.ScreenBuffers[0] = due(); d.ScreenBuffers[0].Mapped = FALSE;
  assert(finish_submission(&d, 5, 9, &fence) == 1 && calls.empty());
  assert(fence == 5 && d.PendingRenderFence == 5 && d.PendingRenderSequence == 9 && d.NextRenderFence == 5);
  /* Non-blocking query of the monitored fence. */
  assert(!query_render(&d, 5) && !query_render(&d, 0)); signalled = 5;
  assert(query_render(&d, 5) && !query_render(&d, 6) && calls.empty());
  /* An older fence does not complete the queued job. */
  assert(wait_render(&d, 4) && calls == "w4 " && d.PendingRenderFence == 5);
  calls.clear();
  /* The first wait for it: wait, download, no GPU signal, then the wait. */
  assert(wait_render(&d, 5));
  if (calls != "w5 d w5 ") { printf("order: %s\n", calls.c_str()); return 1; }
  assert(!d.PendingRenderFence && !d.DrawTerminal);
  calls.clear(); assert(wait_render(&d, 5) && calls == "w5 ");
  /* Each CPU-visible GPU-written held slot completes synchronously. */
  ADMISSION_UMD_SCREEN_BUFFER sync_cases[2] = {due(), due()};
  sync_cases[1].Mapped = FALSE; sync_cases[1].Borrowed = TRUE;
  for (auto s : sync_cases) {
    calls.clear(); d.ScreenBuffers[2] = s;
    assert(finish_submission(&d, 8, 3, &fence) == 1);
    assert(calls == "w8 d s9 " && fence == 9 && d.NextRenderFence == 9 && !d.PendingRenderFence);
  }
  /* Direct, system-direct, not written, read-only or not held: queued. */
  ADMISSION_UMD_SCREEN_BUFFER async_cases[5] = {due(), due(), due(), due(), due()};
  async_cases[0].Direct = TRUE; async_cases[1].SystemDirect = TRUE;
  async_cases[2].GpuWritten = FALSE; async_cases[3].Flags = 0u; async_cases[4].CopyHeld = FALSE;
  for (auto s : async_cases) {
    calls.clear(); d.ScreenBuffers[2] = s; d.PendingRenderFence = 0;
    assert(finish_submission(&d, 10, 4, &fence) == 1 && calls.empty() && fence == 10);
    assert(d.PendingRenderFence == 10);
  }
  /* A failed completion is terminal and fails the wait. */
  calls.clear(); d.PendingRenderFence = 11; fail_download = true;
  assert(!wait_render(&d, 12) && d.DrawTerminal && !d.PendingRenderFence && calls == "w11 d ");
  puts("EXP1082 async render completion: PASS");
  return 0;
}
'''


class AsyncRenderCompletion(unittest.TestCase):
    def test_queued_completion_contract(self):
        text = SRC.read_text()
        funcs = '\n'.join(function(text, n) for n in (
            'cpu_quiet', 'download_due', 'completion_needs_download', 'complete_render',
            'finish_submission', 'wait_render', 'query_render'))
        for name in ('download_due', 'finish_submission', 'complete_render', 'query_render'):
            self.assertIn(name + '(', funcs, name + ' is missing')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'; exe = Path(tmp) / 'replay'
            src.write_text(BODY.replace('@@FUNCS@@', funcs))
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP1082 async render completion: PASS', ran.stdout)

    def test_wiring(self):
        text = SRC.read_text()
        held = function(text, 'transfer_held')
        self.assertIn('download ? !download_due(slot)', held)
        self.assertNotIn('signal_render(', function(text, 'complete_render'))
        submit = function(text, 'submit')
        # EXP1093: two submissions may be in flight; submit no longer
        # completes the previous one first (the winsys bounds the depth).
        self.assertNotIn('complete_render(device)', submit)
        self.assertLess(submit.index('signal_render(device,internal)'),
                        submit.index('finish_submission('))
        after = submit[submit.index('finish_submission('):]
        self.assertIn('slot->CopyHeld=FALSE;slot->LastUseFence=*fence;', after)
        self.assertIn('private_escape, query_render};', text)


if __name__ == '__main__':
    unittest.main()
