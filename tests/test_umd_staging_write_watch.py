"""EXP1083: a write-watched private staging copy skips the change hash.

EXP1081 DWM: the per-submission staging check hashed 31.0 GB (4992 checks,
1.28 ms each, 6.38 s) to learn that mapped slots were unchanged. Private
staging is ordinary process memory, so it is allocated with MEM_WRITE_WATCH.
Invariants:
- a watched copy recorded as synchronized with no page written since is
  unchanged without reading it;
- a written page, an unwatched copy, a temporary VidMm lock or a failed
  GetWriteWatch falls back to the content hash (written-but-equal content is
  still unchanged and cleans the watch; changed content keeps it dirty);
- an invalid record always needs an upload;
- every successful transfer records the hash and then cleans the watch, so
  writes made before the record are never lost after it.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'
INC = ROOT / 'drivers/apple-agx/render-admission/umd/include'


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
#include <cstring>
#include "umd_staging_sync.h"
typedef int BOOL; typedef unsigned UINT; typedef unsigned D3DKMT_HANDLE; typedef unsigned char BYTE;
typedef void *PVOID; typedef uintptr_t ULONG_PTR; typedef unsigned long ULONG; typedef size_t SIZE_T;
typedef long HRESULT;
#define TRUE 1
#define FALSE 0
#define SUCCEEDED(h) ((h) >= 0)
#define FAILED(h) ((h) < 0)
#define EXP1016_START(name) do {} while(0)
#define EXP1016_NOTE(kind,bytes,name) do {} while(0)
static unsigned hashes;
static unsigned long long counting_hash(const void *d, unsigned long long n) {
  ++hashes; return AdmissionUmdStagingHash(d, n); }
#define AdmissionUmdStagingHash counting_hash
struct D3DDDICB_LOCK { D3DKMT_HANDLE hAllocation; struct { unsigned LockEntire, ReadOnly; } Flags; void *pData; };
struct D3DDDICB_UNLOCK { UINT NumAllocations; const D3DKMT_HANDLE *phAllocations; };
static BYTE vidmm[4096];
static HRESULT lock_cb(void *, D3DDDICB_LOCK *l) { l->pData = vidmm; return 0; }
static HRESULT unlock_cb(void *, D3DDDICB_UNLOCK *) { return 0; }
struct CALLBACKS { HRESULT (*pfnLockCb)(void *, D3DDDICB_LOCK *); HRESULT (*pfnUnlockCb)(void *, D3DDDICB_UNLOCK *); };
struct ADMISSION_UMD_SCREEN_BUFFER { BOOL Mapped, Borrowed, WriteWatch; PVOID LockedBase; BYTE *PrivateStaging;
  D3DKMT_HANDLE StagingAllocation; unsigned long long Bytes; ADMISSION_UMD_STAGING_SYNC Sync; };
struct ADMISSION_UMD_DEVICE { const CALLBACKS *KernelCallbacks; struct { void *handle; } RuntimeDevice;
  int ScreenBufferLock; BOOL DrawTerminal; };
static void AcquireSRWLockExclusive(int *) {}
static void ReleaseSRWLockExclusive(int *) {}
static bool dirty, watch_fails; static unsigned resets;
static UINT GetWriteWatch(ULONG, PVOID, SIZE_T, PVOID *pages, ULONG_PTR *count, ULONG *) {
  if (watch_fails) return 1;
  if (!dirty) *count = 0; else { *pages = nullptr; *count = 1; }
  return 0;
}
static UINT ResetWriteWatch(PVOID, SIZE_T) { dirty = false; ++resets; return 0; }
@@FUNCS@@
int main() {
  static BYTE staging[4096];
  CALLBACKS cb = {lock_cb, unlock_cb};
  ADMISSION_UMD_DEVICE device = {&cb, {nullptr}, 0, FALSE};
  ADMISSION_UMD_SCREEN_BUFFER slot = {TRUE, FALSE, TRUE, staging, staging, 5u, sizeof(staging), {}};
  AdmissionUmdStagingRecord(&slot.Sync, AdmissionUmdStagingHash(staging, sizeof(staging)), sizeof(staging));
  hashes = 0;
  /* Clean watch: unchanged, nothing read. */
  assert(staging_unchanged(&device, &slot) == 1 && hashes == 0);
  /* Written with equal content: hashed once, unchanged, watch cleaned. */
  dirty = true; staging[7] = 0;
  assert(staging_unchanged(&device, &slot) == 1 && hashes == 1 && !dirty && resets == 1);
  /* Written with new content: an upload is needed and the watch stays dirty. */
  dirty = true; staging[7] = 9;
  assert(staging_unchanged(&device, &slot) == 0 && hashes == 2 && dirty && resets == 1);
  /* A failed GetWriteWatch counts as written. */
  watch_fails = true; dirty = false;
  assert(staging_unchanged(&device, &slot) == 0 && hashes == 3);
  watch_fails = false;
  /* Unwatched private staging always hashes. */
  slot.WriteWatch = FALSE; dirty = false;
  assert(staging_unchanged(&device, &slot) == 0 && hashes == 4);
  slot.WriteWatch = TRUE;
  /* A mapped slot whose map is not the private staging is not watched. */
  BYTE other[4096] = {1};
  slot.LockedBase = other; dirty = false;
  assert(staging_unchanged(&device, &slot) == 0 && hashes == 5);
  slot.LockedBase = staging;
  /* An invalid record always uploads, without reading. */
  AdmissionUmdStagingInvalidate(&slot.Sync); dirty = false;
  assert(staging_unchanged(&device, &slot) == 0 && hashes == 5);
  puts("EXP1083 staging write watch: PASS");
  return 0;
}
'''


class StagingWriteWatch(unittest.TestCase):
    def test_unwritten_watched_staging_skips_the_hash(self):
        text = SRC.read_text()
        funcs = '\n'.join(function(text, n) for n in (
            'staging_watched', 'staging_written', 'staging_clean', 'staging_unchanged'))
        self.assertIn('staging_written(', funcs, 'write watch check is missing')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'; exe = Path(tmp) / 'replay'
            src.write_text(BODY.replace('@@FUNCS@@', funcs))
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Wno-unused-function',
                                    '-Wno-missing-field-initializers', '-I', str(INC),
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP1083 staging write watch: PASS', ran.stdout)

    def test_record_then_clean_and_watched_allocation(self):
        text = SRC.read_text()
        transfer = function(text, 'transfer_slot')
        self.assertLess(transfer.index('AdmissionUmdStagingRecord('), transfer.index('staging_clean(slot,address)'))
        screen = (ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_win32_screen.c').read_text()
        self.assertIn('MEM_COMMIT | MEM_RESERVE | MEM_WRITE_WATCH', screen)
        self.assertIn('slot->WriteWatch = writeWatch;', screen)


if __name__ == '__main__':
    unittest.main()
