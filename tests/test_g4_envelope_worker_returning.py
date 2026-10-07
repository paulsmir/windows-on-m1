"""EXP1013: a backend worker still returning must not refuse new work.

EXP1012/1013 receipts: 211 SubmitCommandVirtual rejections, first at branch 7
(EnvelopeState) predicate 13 with runtime predicate 8 (WorkScheduled != 0).
The backend worker reports the completed fence to dxgkrnl before
AdmissionPlatformWorkerFinished clears WorkScheduled, so VidSch submits the
next job inside that window; the failed submission marks the device in error
(EXP1009 ETW VidSchErrorFailedSubmitCommandVirtual), which then leaves its new
allocations unpaged and fails the copy QUERY.
Invariants:
- admission accepts work while only WorkScheduled is set;
- every other runtime refusal (no provider, backend not ready, stopping,
  resetting, ...) is still reported with its predicate;
- dispatch readiness (AdmissionPlatformRuntimeReady) still requires an idle
  worker, so the packet is dispatched by the worker's finished path;
- the G4 envelope uses the admission check, not dispatch readiness.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/src/backend_platform_windows.c'
G3 = ROOT / 'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c'


def function(text, name):
    m = re.search(r'(?m)^_Use_decl_annotations_ BOOLEAN ' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


BODY = r'''
#include <cassert>
#include <cstdio>
typedef unsigned char BOOLEAN; typedef unsigned long ULONG; typedef long LONG;
#define TRUE 1
#define FALSE 0
#define NULL 0
#define _Use_decl_annotations_
enum { AppleAgxBackendRuntimeReady = 3, AppleAgxBackendRuntimeSubmitted = 4 };
static LONG InterlockedCompareExchange(volatile LONG *p, LONG, LONG){ return *p; }
struct BACKEND { int Phase; };
struct ADMISSION_PLATFORM_RUNTIME { BOOLEAN ProviderReady, BackendStarted; BACKEND Backend; void *WorkItem;
  volatile LONG Stopping, Resetting, WorkScheduled; };
struct ADMISSION_CONTEXT { void *PlatformRuntime; };
@@FUNCS@@
int main(){
  ADMISSION_PLATFORM_RUNTIME r={1,1,{AppleAgxBackendRuntimeReady},(void*)1,0,0,0};
  ADMISSION_CONTEXT c={&r}; ULONG why=99;
  assert(AdmissionPlatformRuntimeAcceptsWork(&c,&why) && why==0);
  r.WorkScheduled=1; why=99;
  if(!AdmissionPlatformRuntimeAcceptsWork(&c,&why)){printf("returning worker refused new work\n");return 1;}
  assert(why==0 && !AdmissionPlatformRuntimeReadyEx(&c,&why) && why==8);
  r.Stopping=1; assert(!AdmissionPlatformRuntimeAcceptsWork(&c,&why) && why==6); r.Stopping=0;
  r.Resetting=1; assert(!AdmissionPlatformRuntimeAcceptsWork(&c,&why) && why==7); r.Resetting=0;
  r.Backend.Phase=AppleAgxBackendRuntimeSubmitted; assert(!AdmissionPlatformRuntimeAcceptsWork(&c,&why) && why==4);
  r.Backend.Phase=AppleAgxBackendRuntimeReady; r.ProviderReady=0;
  assert(!AdmissionPlatformRuntimeAcceptsWork(&c,NULL));
  c.PlatformRuntime=NULL; assert(!AdmissionPlatformRuntimeAcceptsWork(&c,&why) && why==1);
  puts("EXP1013 worker returning: PASS");
}
'''


class EnvelopeWorkerReturning(unittest.TestCase):
    def test_admission_accepts_while_worker_returns(self):
        text = SRC.read_text()
        accepts = function(text, 'AdmissionPlatformRuntimeAcceptsWork')
        self.assertTrue(accepts, 'admission readiness without idle-worker requirement is missing')
        ready = function(text, 'AdmissionPlatformRuntimeReadyEx')
        ready = re.sub(r'#if defined\(APPLE_AGX_SUBMIT_QUALIFICATION\).*?#endif\n', '', ready, flags=re.S)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'; exe = Path(tmp) / 'replay'
            src.write_text(BODY.replace('@@FUNCS@@', ready + '\n' + accepts))
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP1013 worker returning: PASS', ran.stdout)

    def test_envelope_uses_admission_check(self):
        g3 = G3.read_text()
        start = g3.index('static NTSTATUS AdmissionG4SubmitVirtualEnvelope(')
        envelope = g3[start:g3.index('ADMISSION_G4_REJECTS(14u', start)]
        macros = re.findall(r'#define ADMISSION_G4_RUNTIME_READY\(\)(?:[^\n]*\\\n)*[^\n]*', envelope)
        self.assertEqual(len(macros), 2)
        for macro in macros:
            self.assertIn('AdmissionPlatformRuntimeAcceptsWork(', macro)


if __name__ == '__main__':
    unittest.main()
