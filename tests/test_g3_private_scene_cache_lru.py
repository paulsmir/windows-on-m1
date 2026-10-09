"""EXP1087: the private scene cache admits every reported release (LRU).

EXP1086: with a refusing four-entry cache the per-job private-storage
map/unmap did not fall (360 ms of broker traps per second): entries cached
once were never replaced. Invariants:
- a reported, released, idle scene is always admitted, most recent last;
- scenes not eligible (not reported, queued, quarantined, closing context)
  are not admitted;
- trimming unmaps the least recently cached scenes until at most
  ADMISSION_G3_PRIVATE_SCENE_CACHE remain and keeps the most recent ones;
- a failed unmap stops trimming and reports failure.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c'


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
#include <stdio.h>
#include <stddef.h>
typedef unsigned ULONG; typedef unsigned UINT; typedef unsigned long long ULONGLONG;
typedef unsigned char BOOLEAN;
#define TRUE 1
#define FALSE 0
#define ADMISSION_G3_PRIVATE_SCENE_CACHE 4u
enum { ADMISSION_G3_PRIVATE_STAT_TRIM = 2 };
typedef struct { int GpuvaG3Closing; } ADMISSION_RENDER_CONTEXT;
typedef struct _ADMISSION_G3_PRIVATE_SCENE {
  struct _ADMISSION_G3_PRIVATE_SCENE *Next; ADMISSION_RENDER_CONTEXT *Context;
  ULONG Queued, Submitting, Reported, ReleaseRequested, Quarantined, Cached;
  ULONGLONG CachedAt; int Released;
} ADMISSION_G3_PRIVATE_SCENE;
typedef struct { ULONGLONG PrivateStats[16]; } ADMISSION_G3_STATE;
typedef struct { ADMISSION_G3_STATE *State; ADMISSION_G3_PRIVATE_SCENE *PrivateScenes;
  ULONGLONG PrivateCacheClock; int Poisoned; struct { int Uncertain; } Graph; } ADMISSION_G3_PROCESS;
typedef struct { int unused; } ADMISSION_BACKEND_MEMORY_VIEW;
static int refuse_release;
static BOOLEAN AdmissionG3PrivateReleaseScene(ADMISSION_G3_PROCESS *p,
    ADMISSION_G3_PRIVATE_SCENE *scene, ADMISSION_BACKEND_MEMORY_VIEW *view) {
  ADMISSION_G3_PRIVATE_SCENE **link = &p->PrivateScenes; (void)view;
  if (refuse_release) return FALSE;
  while (*link && *link != scene) link = &(*link)->Next;
  if (*link) *link = scene->Next;
  scene->Released = 1; return TRUE;
}
@@FUNCTIONS@@
int main(void) {
  ADMISSION_G3_STATE state = {{0}};
  ADMISSION_RENDER_CONTEXT ctx = {0}, closing = {1};
  ADMISSION_G3_PROCESS p = {&state, NULL, 0, 0, {0}};
  ADMISSION_BACKEND_MEMORY_VIEW view = {0};
  ADMISSION_G3_PRIVATE_SCENE s[8] = {{0}};
  for (int i = 7; i >= 0; --i) {
    s[i].Context = &ctx; s[i].Reported = 1; s[i].ReleaseRequested = 1;
    s[i].Next = p.PrivateScenes; p.PrivateScenes = &s[i];
  }
  /* Not eligible: not reported, queued, quarantined, closing context. */
  s[6].Reported = 0; s[7].Queued = 1;
  assert(!AdmissionG3PrivateCacheScene(&p, &s[6]) && !AdmissionG3PrivateCacheScene(&p, &s[7]));
  s[5].Quarantined = 1; assert(!AdmissionG3PrivateCacheScene(&p, &s[5])); s[5].Quarantined = 0;
  s[5].Context = &closing; assert(!AdmissionG3PrivateCacheScene(&p, &s[5])); s[5].Context = &ctx;
  /* Six admissions in order 0..5: none refused although the limit is four. */
  for (int i = 0; i < 6; ++i) assert(AdmissionG3PrivateCacheScene(&p, &s[i]) && s[i].Cached);
  assert(s[0].CachedAt < s[5].CachedAt);
  /* Re-admitting a cached scene keeps its order. */
  ULONGLONG at = s[2].CachedAt; assert(AdmissionG3PrivateCacheScene(&p, &s[2]) && s[2].CachedAt == at);
  /* Trim unmaps the two oldest (0, 1) and keeps 2..5. */
  assert(AdmissionG3PrivateTrimCache(&p, &view));
  assert(s[0].Released && s[1].Released && state.PrivateStats[ADMISSION_G3_PRIVATE_STAT_TRIM] == 2);
  for (int i = 2; i < 6; ++i) assert(!s[i].Released && s[i].Cached);
  assert(AdmissionG3PrivateTrimCache(&p, &view) && state.PrivateStats[ADMISSION_G3_PRIVATE_STAT_TRIM] == 2);
  /* A failed unmap stops trimming. */
  s[6].Reported = 1; assert(AdmissionG3PrivateCacheScene(&p, &s[6]));
  refuse_release = 1; assert(!AdmissionG3PrivateTrimCache(&p, &view)); refuse_release = 0;
  assert(!s[2].Released && s[2].Cached);
  puts("PASS");
  return 0;
}
'''


class PrivateSceneCacheLru(unittest.TestCase):
    def test_admits_every_release_and_trims_the_oldest(self):
        text = SRC.read_text()
        functions = '\n'.join(function(text, n) for n in (
            'AdmissionG3PrivateCacheScene', 'AdmissionG3PrivateTrimCache'))
        self.assertIn('AdmissionG3PrivateTrimCache(', functions)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'lru.c'
            exe = Path(directory) / 'lru'
            path.write_text(PROGRAM.replace('@@FUNCTIONS@@', functions))
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                                    '-Werror', '-Wno-unused-function', '-Wno-missing-field-initializers',
                                    '-fsanitize=address,undefined', str(path), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_miss_path_trims_and_receipt_publishes_counters(self):
        text = SRC.read_text()
        escape = text[text.index('NTSTATUS AdmissionGpuvaG3PrivateEscape('):]
        self.assertLess(escape.index('AdmissionG3PrivateTrimCache(p,&view)'),
                        escape.index('AdmissionG3PrivateTables(p,&view'))
        self.assertIn('AdmissionRecordPagingProfile(adapter, &state->Client, state->PrivateStats);', text)


if __name__ == '__main__':
    unittest.main()
