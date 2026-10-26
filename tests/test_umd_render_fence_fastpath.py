"""RC47 actual-source replay of satisfied monitored render-fence waits.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A completed render fence
must avoid redundant Windows CPU-wait callbacks without skipping staging
downloads, accepting unfinished work, or writing the read-only fence page.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'


def function(text, name):
    match = re.search(r'(?m)^static [A-Za-z0-9_ *]+?\b' + name +
                      r'\([^;{]*\)\s*\{', text)
    if not match:
        raise AssertionError('missing production function: ' + name)
    start = text.index('{', match.start())
    end, depth = start + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]


PRELUDE = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
using BOOL = int;
using UINT = unsigned;
using UINT64 = uint64_t;
using LONG64 = int64_t;
using ULONG_PTR = uintptr_t;
using HRESULT = int;
using D3DKMT_HANDLE = unsigned;
#define TRUE 1
#define FALSE 0
#define SUCCEEDED(status) ((status) >= 0)
enum { AppleAgxWin32BufferGpuWrite = 8u };
enum { AgxWaitPagingFence = 2, AgxWaitRenderFence = 3 };
struct D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU {
  UINT ObjectCount;
  const D3DKMT_HANDLE *ObjectHandleArray;
  const UINT64 *FenceValueArray;
  void *hAsyncEvent;
  struct { UINT Value; } Flags;
};
struct KERNEL_CALLBACKS {
  HRESULT (*pfnWaitForSynchronizationObjectFromCpuCb)(
      uintptr_t, const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *);
};
struct ADMISSION_UMD_SCREEN_BUFFER {
  BOOL CopyHeld, Direct, SystemDirect, GpuWritten, Mapped, Borrowed;
  UINT Flags;
};
struct ADMISSION_UMD_DEVICE {
  const KERNEL_CALLBACKS *KernelCallbacks;
  struct { uintptr_t handle; } RuntimeDevice;
  D3DKMT_HANDLE PagingSyncObject, RenderSyncObject;
  volatile UINT64 *RenderFenceAddress;
  int ScreenBufferLock;
  ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[2];
  UINT ScreenBufferHighWater;
  UINT64 NextRenderFence, PendingRenderFence;
  UINT PendingRenderSequence;
  BOOL DrawTerminal;
};
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) ((d)->ScreenBufferHighWater)
static void AcquireSRWLockShared(int *) {}
static void ReleaseSRWLockShared(int *) {}
static unsigned waits, starts, ends, reads, downloads, signals, last_site;
static bool fail_wait, fail_download, fail_signal, update_mapping;
static uint64_t advance_to;
static D3DKMT_HANDLE expected_object;
static ADMISSION_UMD_DEVICE *current;
alignas(8) static UINT64 signalled;
static std::string calls;
static unsigned AdmissionUmdWaitBegin(unsigned site) {
  ++starts; last_site = site; return 77;
}
static void AdmissionUmdWaitEnd(unsigned token) {
  assert(token == 77); ++ends;
}
/* Host stand-in for the SDK read-only acquire intrinsic, not a wait. */
static LONG64 ReadAcquire64(const volatile LONG64 *address) {
  ++reads;
  return __atomic_load_n(address, __ATOMIC_ACQUIRE);
}
static HRESULT cpu_wait(uintptr_t handle,
    const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *request) {
  assert(current && handle == current->RuntimeDevice.handle);
  assert(request && request->ObjectCount == 1 && request->ObjectHandleArray);
  assert(*request->ObjectHandleArray == expected_object);
  assert(request->FenceValueArray && *request->FenceValueArray);
  assert(!request->hAsyncEvent && !request->Flags.Value);
  ++waits;
  uint64_t value = *request->FenceValueArray;
  calls += "w" + std::to_string(value) + " ";
  if (fail_wait) return -1;
  if (update_mapping && current->RenderFenceAddress)
    *current->RenderFenceAddress = advance_to ? advance_to : value;
  return 0;
}
static KERNEL_CALLBACKS callbacks = {cpu_wait};
static int transfer_held(ADMISSION_UMD_DEVICE *, bool download) {
  assert(download); ++downloads; calls += "d "; return !fail_download;
}
static int signal_render(ADMISSION_UMD_DEVICE *, uint64_t value) {
  ++signals; calls += "s" + std::to_string(value) + " "; return !fail_signal;
}
static void reset(ADMISSION_UMD_DEVICE *device) {
  *device = {};
  waits = starts = ends = reads = downloads = signals = last_site = 0;
  fail_wait = fail_download = fail_signal = false;
  update_mapping = true; advance_to = 0; calls.clear(); signalled = 0;
  device->KernelCallbacks = &callbacks;
  device->RuntimeDevice.handle = 0x1234;
  device->RenderSyncObject = expected_object = 7;
  device->PagingSyncObject = 9;
  device->RenderFenceAddress = &signalled;
  device->ScreenBufferHighWater = 2;
  current = device;
}
@@FUNCTIONS@@
'''


FASTPATH = r'''
int main() {
  ADMISSION_UMD_DEVICE d;
  const uint64_t target = (UINT64_C(1) << 40) + 5;
  reset(&d); signalled = target;
  assert(wait_object(&d, 7, target));
  assert(waits == 0 && starts == 0 && ends == 0 && reads == 1);
  assert(wait_object(&d, 7, target - 1));
  assert(waits == 0 && reads == 2);

  reset(&d); signalled = target - 1;
  assert(wait_object(&d, 7, target));
  assert(waits == 1 && starts == 1 && ends == 1 && signalled == target);
  assert(last_site == AgxWaitRenderFence);
  assert(wait_object(&d, 7, target) && waits == 1);

  // Validation precedes any read or apparent-completion shortcut.
  reset(&d); signalled = target;
  assert(!wait_object(nullptr, 7, target));
  assert(!wait_object(&d, 0, target) && !wait_object(&d, 7, 0));
  d.KernelCallbacks = nullptr;
  assert(!wait_object(&d, 7, target));
  KERNEL_CALLBACKS missing = {};
  d.KernelCallbacks = &missing;
  assert(!wait_object(&d, 7, target));
  assert(waits == 0 && reads == 0 && starts == 0);

  // Unreadable, terminal, saturated, paging and unrelated-object cases
  // retain Windows callback behavior, including errors and profiler events.
  for (unsigned mode = 0; mode < 6; ++mode) {
    reset(&d); signalled = target; update_mapping = false;
    alignas(8) unsigned char unaligned[16] = {};
    D3DKMT_HANDLE object = 7;
    if (mode == 0) d.RenderFenceAddress = nullptr;
    if (mode == 1) d.RenderFenceAddress = (volatile UINT64 *)(unaligned + 1);
    if (mode == 2) d.DrawTerminal = TRUE;
    if (mode == 3) signalled = UINT64_MAX;
    if (mode == 4) object = d.PagingSyncObject;
    if (mode == 5) object = 11;
    expected_object = object;
    assert(wait_object(&d, object, target));
    assert(waits == 1 && starts == 1 && ends == 1);
    assert(last_site == (mode == 4 ? AgxWaitPagingFence : AgxWaitRenderFence));
    assert(reads == (mode == 3 ? 1u : 0u));
    fail_wait = true;
    assert(!wait_object(&d, object, target));
    assert(waits == 2 && starts == 2 && ends == 2);
  }

  // A read-modify-write "load" would fault on the actual mapping contract.
  reset(&d);
  size_t bytes = (size_t)sysconf(_SC_PAGESIZE);
  void *page = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANON, -1, 0);
  assert(page != MAP_FAILED);
  memcpy(page, &target, sizeof(target));
  assert(mprotect(page, bytes, PROT_READ) == 0);
  d.RenderFenceAddress = (volatile UINT64 *)page;
  assert(wait_object(&d, 7, target) && waits == 0 && reads == 1);
  assert(munmap(page, bytes) == 0);
  puts("RC47 completed fence fast path: PASS");
}
'''


COMPLETION = r'''
int main() {
  ADMISSION_UMD_DEVICE d;
  reset(&d); signalled = d.PendingRenderFence = 5;
  assert(wait_render(&d, 5));
  assert(calls == "d " && waits == 0 && downloads == 1 && signals == 0);
  assert(!d.PendingRenderFence && !d.DrawTerminal);
  assert(wait_render(&d, 5) && calls == "d " && downloads == 1 && waits == 0);

  // This used to enter Windows twice for the same fence: w5 d w5.
  reset(&d); signalled = 4; d.PendingRenderFence = 5;
  assert(wait_render(&d, 5));
  assert(calls == "w5 d " && waits == 1 && downloads == 1 && signals == 0);
  assert(!d.PendingRenderFence && !d.DrawTerminal);

  reset(&d); signalled = 4; d.PendingRenderFence = 5;
  assert(wait_render(&d, 6));
  assert(calls == "w5 d w6 " && waits == 2 && signalled == 6);

  reset(&d); signalled = 4; d.PendingRenderFence = 5; advance_to = 6;
  assert(wait_render(&d, 6));
  assert(calls == "w5 d " && waits == 1 && signalled == 6);

  reset(&d); signalled = 4; d.PendingRenderFence = 5;
  assert(wait_render(&d, 4));
  assert(calls.empty() && d.PendingRenderFence == 5 && downloads == 0);

  reset(&d); signalled = 4; d.PendingRenderFence = 5; fail_wait = true;
  assert(!wait_render(&d, 5));
  assert(calls == "w5 " && downloads == 0 && d.DrawTerminal && !d.PendingRenderFence);

  reset(&d); signalled = d.PendingRenderFence = 5; fail_download = true;
  assert(!wait_render(&d, 5));
  assert(calls == "d " && waits == 0 && d.DrawTerminal && !d.PendingRenderFence);

  // Synchronous staging completion still signals only AFTER its CPU download.
  for (unsigned failure = 0; failure < 3; ++failure) {
    reset(&d); signalled = 8;
    d.ScreenBuffers[0] = {TRUE, FALSE, FALSE, TRUE, TRUE, FALSE,
                         AppleAgxWin32BufferGpuWrite};
    uint64_t returned = 0;
    fail_download = failure == 1; fail_signal = failure == 2;
    int result = finish_submission(&d, 8, 20, &returned);
    assert(waits == 0 && downloads == 1);
    assert(calls == (failure == 1 ? "d " : "d s9 "));
    if (!failure) {
      assert(result == 1 && returned == 9 && d.NextRenderFence == 9 && !d.DrawTerminal);
    } else {
      assert(result == 2 && returned == 0 && d.DrawTerminal);
    }
  }

  // No-download submission remains asynchronous, with no load or callback.
  reset(&d);
  uint64_t returned = 0;
  assert(finish_submission(&d, 12, 40, &returned) == 1);
  assert(returned == 12 && d.PendingRenderFence == 12 && d.PendingRenderSequence == 40);
  assert(calls.empty() && reads == 0);
  puts("RC47 deferred completion ordering: PASS");
}
'''


X86_FALLBACK = r'''
int main() {
  ADMISSION_UMD_DEVICE d;
  reset(&d); signalled = 5;
  assert(wait_object(&d, 7, 5));
  assert(waits == 1 && starts == 1 && ends == 1 && reads == 0);
  reset(&d); signalled = d.PendingRenderFence = 5;
  assert(wait_render(&d, 5));
  assert(calls == "w5 d w5 " && waits == 2 && downloads == 1 && reads == 0);
  puts("RC47 x86 wait fallback: PASS");
}
'''


class RenderFenceFastpath(unittest.TestCase):
    def replay(self, main, win64):
        text = SOURCE.read_text()
        bodies = '\n'.join(function(text, name) for name in (
            'wait_object', 'cpu_quiet', 'download_due',
            'completion_needs_download', 'complete_render',
            'finish_submission', 'wait_render'))
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'fence.cpp'
            executable = Path(directory) / 'fence'
            source.write_text(PRELUDE.replace('@@FUNCTIONS@@', bodies) + main)
            command = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-function', '-fsanitize=address,undefined']
            if win64:
                command += ['-D_WIN64=1']
            built = subprocess.run(command + [str(source), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('PASS', ran.stdout)

    def test_completed_fences_skip_callbacks_with_exact_fallbacks(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Satisfied 64-bit
        fences must skip Windows without reading invalid inputs, writing the
        read-only page, or bypassing paging/terminal/error callback behavior.
        """
        self.replay(FASTPATH, True)

    def test_fast_completion_preserves_download_and_later_fence_order(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Eliminating a
        duplicate callback must preserve exactly-once CPU copy-out, terminal
        errors and waiting for later targets before held storage can retire.
        """
        self.replay(COMPLETION, True)

    def test_32bit_policy_keeps_runtime_waits(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A 32-bit build
        must retain Windows waits instead of trusting an unproved atomic
        read-only 64-bit load. This is branch coverage, not a native ABI build.
        """
        self.replay(X86_FALLBACK, False)


if __name__ == '__main__':
    unittest.main()
