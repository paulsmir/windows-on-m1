"""The G4-r3 TDR drill strands only what was armed.

WHAT REAL INVARIANT: the drill is a production-KMD diagnostic that freezes
dispatch until a TDR; it must be inert unless the device value
Wom1TdrDrillAllowed is set, must strand only render jobs of the process that
sent the escape, exactly the armed count, and the drill tool's escape payload
must equal the KMD's ADMISSION_TDR_DRILL (a mismatch silently disarms the
hardware checkpoint). The real escape handler and take function are compiled.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

from test_g4_submit_virtual_replay import function_body

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
TOOL = ROOT / 'drivers/apple-agx/windows/icd/agx_tdr_drill.c'


class TdrDrill(unittest.TestCase):
    def test_drill_is_gated_per_process_and_counted(self):
        backend = (RENDER / 'src/backend_platform_windows.c').read_text()
        body = (SHIM + function_body(backend, 'AdmissionTdrDrillTake') + '\n' +
                function_body(backend, 'AdmissionPlatformTdrDrillEscape') + CASES)
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / 'drill.c'
            program.write_text(body)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-fsanitize=address,undefined', '-I',
                            str(RENDER / 'include'), str(program), '-o',
                            str(Path(tmp) / 'drill')], check=True)
            subprocess.run([str(Path(tmp) / 'drill')], check=True, timeout=20)

    def test_worker_skips_its_poll_only_for_a_drilled_job(self):
        backend = (RENDER / 'src/backend_platform_windows.c').read_text()
        worker = function_body(backend, 'AdmissionPlatformWorker')
        self.assertRegex(worker, r'if \(AdmissionTdrDrillTake\(adapter, &description\)\) \{\s*'
                                 r'drilled = TRUE;')
        self.assertRegex(worker, r'while \(runtime->Backend\.Phase == AppleAgxBackendRuntimeSubmitted &&\s*!drilled &&')
        self.assertIn('L"Wom1TdrDrillAllowed"', backend)

    def test_tool_payload_matches_the_kmd_abi(self):
        header = (RENDER / 'include/render_gpu_fault.h').read_text()
        tool = TOOL.read_text()
        for name in ('ADMISSION_TDR_DRILL_MAGIC', 'ADMISSION_TDR_DRILL_VERSION'):
            kmd = re.search(r'#define ' + name + r' (0x[0-9a-f]+u|\d+u)', header).group(1)
            self.assertIn(f'#define {name} {kmd}', tool)
        layout = r'typedef struct[^{]*\{\s*unsigned int Magic, Version, Bytes, Jobs;\s*\}'
        self.assertRegex(header, layout)
        self.assertRegex(tool, layout)


SHIM = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "render_submission.h"
#include "render_gpu_fault.h"
#define _Use_decl_annotations_
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000Du)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBu)
#define RtlCopyMemory memcpy
#define HandleToULong(h) ((ULONG)(uintptr_t)(h))
typedef int32_t LONG, NTSTATUS;
typedef uint32_t ULONG;
typedef uintptr_t ULONG_PTR;
typedef int BOOLEAN;
typedef void *HANDLE;
typedef struct { ULONG OsProcessId; } ADMISSION_G3_PROCESS;
typedef struct { void *GpuvaG3Process; } ADMISSION_RENDER_CONTEXT;
typedef struct {
  BOOLEAN TdrDrillAllowed;
  volatile LONG TdrDrillPid, TdrDrillJobs, TdrDrillFence;
} ADMISSION_CONTEXT;
typedef struct { void *pPrivateDriverData; ULONG PrivateDriverDataSize; } DXGKARG_ESCAPE;
static ULONG current_pid;
static HANDLE PsGetCurrentProcessId(void) { return (HANDLE)(uintptr_t)current_pid; }
static LONG InterlockedCompareExchange(volatile LONG *p, LONG v, LONG e) {
  LONG o = *p; if (o == e) *p = v; return o; }
static LONG InterlockedExchange(volatile LONG *p, LONG v) { LONG o = *p; *p = v; return o; }
'''

CASES = r'''
static NTSTATUS arm(ADMISSION_CONTEXT *c, ULONG pid, ULONG jobs, ULONG bytes) {
  ADMISSION_TDR_DRILL d = {ADMISSION_TDR_DRILL_MAGIC, ADMISSION_TDR_DRILL_VERSION,
                           sizeof(ADMISSION_TDR_DRILL), jobs};
  DXGKARG_ESCAPE e = {&d, bytes};
  current_pid = pid;
  return AdmissionPlatformTdrDrillEscape(c, &e);
}

int main(void) {
  ADMISSION_CONTEXT adapter;
  ADMISSION_G3_PROCESS tool = {4242u}, dwm = {1232u};
  ADMISSION_RENDER_CONTEXT tool_ctx = {&tool}, dwm_ctx = {&dwm}, paging_ctx = {NULL};
  ADMISSION_RENDER_PACKET_DESCRIPTION job;
  memset(&adapter, 0, sizeof(adapter));
  memset(&job, 0, sizeof(job));

  /* Not allowed: the escape is refused and nothing is stranded. */
  assert(arm(&adapter, 4242u, 1u, sizeof(ADMISSION_TDR_DRILL)) == STATUS_NOT_SUPPORTED);
  job.Fence = 10u; job.ContextToken = (unsigned long long)(uintptr_t)&tool_ctx;
  assert(!AdmissionTdrDrillTake(&adapter, &job));

  adapter.TdrDrillAllowed = TRUE;
  assert(arm(&adapter, 4242u, 1u, sizeof(ADMISSION_TDR_DRILL) - 1u) == STATUS_INVALID_PARAMETER);
  assert(arm(&adapter, 4242u, 0u, sizeof(ADMISSION_TDR_DRILL)) == STATUS_INVALID_PARAMETER);
  assert(arm(&adapter, 4242u, ADMISSION_TDR_DRILL_MAX_JOBS + 1u,
             sizeof(ADMISSION_TDR_DRILL)) == STATUS_INVALID_PARAMETER);
  assert(adapter.TdrDrillJobs == 0);

  /* Armed by the tool: DWM's and the system context's jobs pass. */
  assert(arm(&adapter, 4242u, 2u, sizeof(ADMISSION_TDR_DRILL)) == STATUS_SUCCESS);
  job.Fence = 11u; job.ContextToken = (unsigned long long)(uintptr_t)&dwm_ctx;
  assert(!AdmissionTdrDrillTake(&adapter, &job));
  job.Fence = 12u; job.ContextToken = (unsigned long long)(uintptr_t)&paging_ctx;
  assert(!AdmissionTdrDrillTake(&adapter, &job));
  job.Fence = 13u; job.ContextToken = 0u;
  assert(!AdmissionTdrDrillTake(&adapter, &job));
  /* Exactly the armed count of the tool's jobs. */
  job.Fence = 14u; job.ContextToken = (unsigned long long)(uintptr_t)&tool_ctx;
  assert(AdmissionTdrDrillTake(&adapter, &job) && adapter.TdrDrillFence == 14);
  job.Fence = 15u;
  assert(AdmissionTdrDrillTake(&adapter, &job) && adapter.TdrDrillFence == 15);
  job.Fence = 16u;
  assert(!AdmissionTdrDrillTake(&adapter, &job) && adapter.TdrDrillFence == 15);
  /* Re-arming moves the drill to the new caller. */
  assert(arm(&adapter, 1232u, 1u, sizeof(ADMISSION_TDR_DRILL)) == STATUS_SUCCESS);
  job.Fence = 17u;
  assert(!AdmissionTdrDrillTake(&adapter, &job));
  job.Fence = 18u; job.ContextToken = (unsigned long long)(uintptr_t)&dwm_ctx;
  assert(AdmissionTdrDrillTake(&adapter, &job) && adapter.TdrDrillJobs == 0);
  return 0;
}
'''

if __name__ == '__main__':
    unittest.main()
