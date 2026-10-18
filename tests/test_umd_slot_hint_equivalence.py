"""EXP1174: verified slot hints return exactly what the linear slot scan returns.

The EXP1174 game-thread profile put ~10% of CS 1.6's frame in linear scans
of the screen-buffer slots (make_resident/evict via find_slot,
allocation_handle, identity). The lookups now consult a direct-mapped hint
(slot index + 1) verified against the slot. Invariant, under random slot
creation, destruction, reuse, transitions, native-BO association and hash
collisions: find_slot, AdmissionUmdScreenFind and identity return the same
slot (or none) as the original linear scans; a stale hint never returns a
freed or reused slot.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
UMD = ROOT / 'drivers/apple-agx/render-admission/umd/src'


def function(text, name):
    m = re.search(r'(?m)^static [A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


PROGRAM = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned UINT; typedef long LONG; typedef int BOOL; typedef uint64_t APPLE_AGX_U64;
typedef uint32_t APPLE_AGX_U32; typedef uintptr_t ULONG_PTR; typedef struct { int x; } SRWLOCK;
#define TRUE 1
#define FALSE 0
#define AcquireSRWLockShared(l) ((void)(l))
#define ReleaseSRWLockShared(l) ((void)(l))
#define ZeroMemory(p, n) memset((p), 0, (n))
@@CACHE@@
#define LIMIT 64u
typedef struct { BOOL Active, Transition; APPLE_AGX_U64 Token, Serial, NativeBoSerial, Bytes;
  const void *NativeBo, *NativeBackend; UINT Flags, ClassId; } ADMISSION_UMD_SCREEN_BUFFER;
typedef struct { ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[LIMIT]; UINT ScreenBufferHighWater;
  volatile LONG TokenSlotHint[ADMISSION_UMD_SLOT_CACHE_SIZE];
  volatile LONG NativeBoSlotHint[ADMISSION_UMD_SLOT_CACHE_SIZE];
  BOOL ScreenClosing; SRWLOCK ScreenBufferLock; APPLE_AGX_U64 OwnerCookie; UINT Win32Generation;
} ADMISSION_UMD_DEVICE;
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device) ((Device)->ScreenBufferHighWater)
typedef struct { ADMISSION_UMD_DEVICE *Device; const void *Backend; } ADMISSION_UMD_ASAHI_OWNER;
typedef struct { APPLE_AGX_U64 Owner, Token, Serial, Bytes; UINT Generation, AllocationIndex, Access; }
  AGX_WIN32_RELOC_ALLOCATION;
enum { AppleAgxWin32BufferGpuRead = 1, AppleAgxWin32BufferGpuWrite = 2,
  AppleAgxWin32AccessRead = 1, AppleAgxWin32AccessWrite = 2, AppleAgxWin32AccessExecute = 4,
  AgxWin32BufferClassShader = 7 };
@@FUNCS@@
static ADMISSION_UMD_SCREEN_BUFFER *linear_token(ADMISSION_UMD_DEVICE *d, APPLE_AGX_U64 t) {
  for (UINT i = 0; i < d->ScreenBufferHighWater; ++i)
    if (d->ScreenBuffers[i].Active && d->ScreenBuffers[i].Token == t) return &d->ScreenBuffers[i];
  return NULL;
}
static ADMISSION_UMD_SCREEN_BUFFER *linear_identity(ADMISSION_UMD_DEVICE *d, const void *key,
                                                    const void *backend, APPLE_AGX_U64 serial) {
  for (UINT i = 0; i < d->ScreenBufferHighWater; ++i) {
    ADMISSION_UMD_SCREEN_BUFFER *b = &d->ScreenBuffers[i];
    if (b->Active && b->NativeBo == key && b->NativeBackend == backend && !b->Transition &&
        (!serial || b->NativeBoSerial == serial)) return b;
  }
  return NULL;
}
static unsigned rng = 7u;
static unsigned next(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }
static ADMISSION_UMD_DEVICE d;
int main(void) {
  static char bos[256];
  const void *backend = &rng;
  ADMISSION_UMD_ASAHI_OWNER owner = { &d, backend };
  APPLE_AGX_U64 next_token = 0, next_serial = 0;
  for (unsigned step = 0; step < 200000u; ++step) {
    unsigned op = next() % 8u, i = next() % LIMIT;
    ADMISSION_UMD_SCREEN_BUFFER *s = &d.ScreenBuffers[i];
    if (op == 0 && !s->Active) {            /* create (tokens are never reused) */
      memset(s, 0, sizeof(*s)); s->Token = ++next_token; s->Serial = ++next_serial; s->Active = TRUE;
      if (i + 1u > d.ScreenBufferHighWater) d.ScreenBufferHighWater = i + 1u;
    } else if (op == 1 && s->Active) {      /* destroy */
      memset(s, 0, sizeof(*s));
    } else if (op == 2 && s->Active) {      /* transition flips */
      s->Transition = !s->Transition;
    } else if (op == 3 && s->Active && !s->NativeBo) {  /* associate, refusing duplicates */
      const void *key = &bos[next() % 256u];
      BOOL dup = FALSE;
      for (UINT k = 0; k < d.ScreenBufferHighWater; ++k)
        if (d.ScreenBuffers[k].Active && d.ScreenBuffers[k].NativeBo == key) dup = TRUE;
      if (!dup) { s->NativeBo = key; s->NativeBackend = backend; s->NativeBoSerial = 1 + next() % 3u; }
    } else if (op == 4 && s->Active) {      /* detach */
      s->NativeBo = NULL; s->NativeBackend = NULL; s->NativeBoSerial = 0;
    }
    /* lookups: live, freed and never-issued tokens; associated and unknown BOs */
    APPLE_AGX_U64 token = next_token ? 1 + next() % (next_token + 2u) : 1;
    assert(find_slot(&d, token) == linear_token(&d, token));
    assert(AdmissionUmdScreenFind(&d, token) == (token ? linear_token(&d, token) : NULL));
    const void *key = &bos[next() % 256u];
    APPLE_AGX_U64 serial = next() % 4u;
    AGX_WIN32_RELOC_ALLOCATION out;
    ADMISSION_UMD_SCREEN_BUFFER *want = linear_identity(&d, key, backend, serial);
    int got = identity(&owner, key, serial, &out);
    assert(got == (want != NULL));
    if (want) assert(out.Token == want->Token && out.Serial == want->Serial);
  }
  puts("PASS");
  return 0;
}
'''


class SlotHintEquivalence(unittest.TestCase):
    def test_hinted_lookups_match_linear_scans(self):
        internal = (UMD / 'umd_internal.h').read_text()
        cache = '\n'.join(re.findall(r'(?ms)^#define ADMISSION_UMD_SLOT_(?:CACHE_SIZE|HASH)\b.*?[^\\]\n', internal))
        self.assertIn('ADMISSION_UMD_SLOT_CACHE_SIZE', cache)
        self.assertIn('ADMISSION_UMD_SLOT_HASH', cache)
        # a small cache forces hint collisions between live slots
        cache = re.sub(r'ADMISSION_UMD_SLOT_CACHE_SIZE \d+u', 'ADMISSION_UMD_SLOT_CACHE_SIZE 8u', cache)
        cache = cache.replace('>> 51', '>> 61')
        funcs = [function((UMD / 'umd_gpuva_windows.c').read_text(), 'find_slot'),
                 function((UMD / 'umd_win32_screen.c').read_text(), 'AdmissionUmdScreenFind'),
                 function((UMD / 'umd_asahi_owner.c').read_text(), 'identity_match'),
                 function((UMD / 'umd_asahi_owner.c').read_text(), 'identity')]
        for f in funcs:
            self.assertTrue(f)
        self.assertIn('TokenSlotHint', funcs[0])
        self.assertIn('TokenSlotHint', funcs[1])
        self.assertIn('NativeBoSlotHint', funcs[3])
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 't.c'
            exe = Path(tmp) / 't'
            src.write_text(PROGRAM.replace('@@CACHE@@', cache).replace('@@FUNCS@@', '\n'.join(funcs)))
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wno-unused-function', str(src), '-o', str(exe)],
                           check=True)
            out = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertIn('PASS', out.stdout)


if __name__ == '__main__':
    unittest.main()
