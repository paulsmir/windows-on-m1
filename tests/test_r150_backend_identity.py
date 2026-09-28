"""Catch a worker overwriting backend ownership with a process UAT slot.

Compile the actual start/reset initializers and worker construction/submit block
against the real portable runtime. Only firmware/Windows boundaries are doubled.
B1 exercises its separate production submission constructor, not the G3 worker.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers/apple-agx/shared"
ADMISSION = ROOT / "drivers/apple-agx/render-admission"


def replay_source(source=None):
    if source is None:
        source = (ADMISSION / "src/backend_platform_windows.c").read_text()
    initializers = re.findall(
        r"  AppleAgxBackendRuntimeInitialize\(\n#if.*?\n#endif", source, re.S
    )
    if len(initializers) != 2:
        raise ValueError("Expected separate start/reset runtime initializer blocks")
    begin = source.index("  submission.Submission.Kind = AppleAgxSubmissionGdi;")
    end = source.index("  result = AppleAgxBackendRuntimeSubmit(", begin)
    worker = source[begin:source.index(";", end) + 1]
    begin = source.index("  if (!AppleAgxGpuvaB1PrepareSubmission(")
    end = source.index("    return STATUS_INVALID_BUFFER_SIZE;", begin)
    b1 = source[begin:end + len("    return STATUS_INVALID_BUFFER_SIZE;")]
    return HARNESS.replace("@START@", initializers[0]).replace(
        "@RESET@", initializers[1]).replace("@WORKER@", worker).replace("@B1@", b1)


HARNESS = r'''
#include <stdint.h>
#define main ExistingBackendRuntimeTests
#include "apple_agx_backend_runtime_test.c"
#undef main
#include "apple_agx_gpuva_b1_submission.h"
#include "render_memory.h"
#include "render_submission.h"

typedef uintptr_t ULONG_PTR;
typedef unsigned int ULONG;
typedef struct { APPLE_AGX_BACKEND_RUNTIME Backend; int Rtkit, AscIo; } PLATFORM;
typedef struct { int SchedulerFaulted; } ADAPTER;
typedef struct { void *GpuvaG3Process; } ADMISSION_RENDER_CONTEXT;
#define NT_SUCCESS(s) ((s) >= 0)
#define STATUS_INVALID_BUFFER_SIZE (-1)
#define FALSE 0
#define AppleAgxRtkitSessionResultOk 0
#define AdmissionPlatformNowMs() 0
#define AppleAgxRtkitSessionHeartbeat(...) 0
#define InterlockedExchange(p, v) (*(p) = (v))
#define AdmissionFlushGdiReceipt(a) ((void)(a))
#define AdmissionRenderCorrelationWorkerWindows(...) ((void)0)
#define AdmissionPlatformWorkerFinished(r) ((void)(r))
#define AdmissionPrepareG4Manager(r) ((void)(r), 1)
static unsigned int begin_count, resolve_count;
int AdmissionGpuvaG3BeginJob(ADAPTER *a, ADMISSION_RENDER_CONTEXT *c, ULONG f) {
  assert(a && c && c->GpuvaG3Process && f == 77u);
  ++begin_count;
  return 0;
}
static void InitializeStart(PLATFORM *runtime) {
@START@
}
static void InitializeReset(PLATFORM *runtime) {
@RESET@
}
static void Worker(PLATFORM *runtime, ADAPTER *adapter,
    ADMISSION_RENDER_PACKET_DESCRIPTION description,
    APPLE_AGX_BACKEND_SUBMISSION *observed,
    APPLE_AGX_BACKEND_RUNTIME_RESULT *out) {
  APPLE_AGX_BACKEND_SUBMISSION submission = {0};
  APPLE_AGX_BACKEND_RUNTIME_RESULT result;
  int heartbeatResult;
@WORKER@
  *observed = submission;
  *out = result;
}
static APPLE_AGX_BOOL ProbeResolve(void *opaque,
    const APPLE_AGX_BACKEND_SUBMISSION *s, const unsigned char **bytes,
    APPLE_AGX_U32 *count) {
  FAKE_BACKEND *fake = opaque;
  ++resolve_count;
  assert(s->Submission.Fence == 77u && s->Submission.DmaBytes == 16u);
  *bytes = fake->PrivateData + 8u;
  *count = 16u;
  return APPLE_AGX_TRUE;
}
static int B1Prepare(APPLE_AGX_BACKEND_SUBMISSION *out,
    unsigned char *storage_out) {
  APPLE_AGX_BACKEND_SUBMISSION submission = {0};
  unsigned char dma[16] = {0};
  unsigned int written = sizeof(dma), Fence = 77u;
  unsigned char shadowStorage[512];
@B1@
  memcpy(storage_out, shadowStorage, sizeof(shadowStorage));
  submission.PrivateData = storage_out;
  *out = submission;
  return 0;
}
static void Check(void (*initialize)(PLATFORM *), const char *site, int has_process) {
  PLATFORM runtime = {0};
  ADAPTER adapter = {0};
  ADMISSION_RENDER_CONTEXT context = {has_process ? (void *)1 : NULL};
  ADMISSION_RENDER_PACKET_DESCRIPTION description = {0};
  FAKE_BACKEND fake = {0};
  APPLE_AGX_BACKEND_IO io = BackendIo(&fake);
  APPLE_AGX_BACKEND_SUBMISSION submission, bad;
  APPLE_AGX_BACKEND_RUNTIME_RESULT result = AppleAgxBackendRuntimeResultInvalidState;
  unsigned char b1_storage[512];
  unsigned int before, i;
  const unsigned int invalid_identities[] = {0u, 1u, 7u, 62u, 63u, 64u};
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
  const unsigned int owner_identity = 1u;
#else
  const unsigned int owner_identity = 63u;
#endif
  io.Memory.Resolve = ProbeResolve;
  initialize(&runtime);
  fake.Runtime = &runtime.Backend;
  assert(AppleAgxBackendRuntimeStart(&runtime.Backend, &io) == AppleAgxBackendRuntimeResultOk);
  begin_count = resolve_count = 0u;
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
  (void)adapter; (void)context; (void)description; (void)Worker;
  assert(B1Prepare(&submission, b1_storage) == 0);
  result = AppleAgxBackendRuntimeSubmit(&runtime.Backend, &submission);
#else
  (void)b1_storage; (void)B1Prepare;
  description.Fence = 77u;
  description.ContextToken = (ULONG_PTR)&context;
  description.PrivateDataToken = (ULONG_PTR)fake.PrivateData;
  description.PrivateDataBytes = sizeof(fake.PrivateData);
  description.PrivateDataEnd = 24u;
  description.DmaStart = 32u;
  description.DmaEnd = 48u;
  Worker(&runtime, &adapter, description, &submission, &result);
#endif
  printf("%s backend=%llu submission=%llu result=%u resolve=%u relocate=%u run3d=%u runta=%u begin=%u\n",
      site, (unsigned long long)runtime.Backend.ContextIdentity,
      (unsigned long long)submission.ContextIdentity, result, resolve_count,
      CountOperation(&fake, OP_RELOCATE), CountOperation(&fake, OP_RUN_3D),
      CountOperation(&fake, OP_RUN_TA), begin_count);
  fflush(stdout);
  assert(result == AppleAgxBackendRuntimeResultOk);
  assert(runtime.Backend.Phase == AppleAgxBackendRuntimeSubmitted);
  assert(runtime.Backend.PendingSubmission.Submission.Fence == 77u);
  assert(runtime.Backend.PendingJob.TaWorkAddressCount == 2u);
  assert(resolve_count == 1u && CountOperation(&fake, OP_RELOCATE) == 1u);
  assert(CountOperation(&fake, OP_RUN_3D) == 1u && CountOperation(&fake, OP_RUN_TA) == 1u);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  assert(begin_count == (unsigned int)has_process);
#else
  assert(begin_count == 0u);
#endif
  assert(AppleAgxBackendRuntimeStop(&runtime.Backend) == AppleAgxBackendRuntimeResultOk);
  initialize(&runtime);
  assert(AppleAgxBackendRuntimeStart(&runtime.Backend, &io) == AppleAgxBackendRuntimeResultOk);
  before = fake.OperationCount;
  resolve_count = 0u;
  for (i = 0u; i < sizeof(invalid_identities)/sizeof(invalid_identities[0]); ++i) {
    if (invalid_identities[i] == owner_identity) continue;
    bad = submission;
    bad.ContextIdentity = invalid_identities[i];
    assert(AppleAgxBackendRuntimeSubmit(&runtime.Backend, &bad) ==
           AppleAgxBackendRuntimeResultInvalidArgument);
    assert(runtime.Backend.Phase == AppleAgxBackendRuntimeReady);
    assert(runtime.Backend.PendingSubmission.Submission.Fence == 0u);
    assert(runtime.Backend.PendingJob.TaWorkAddressCount == 0u);
    assert(resolve_count == 0u && fake.OperationCount == before);
  }
  assert(AppleAgxBackendRuntimeStop(&runtime.Backend) == AppleAgxBackendRuntimeResultOk);
}
int main(void) {
  Check(InitializeStart, "start", 1);
  Check(InitializeReset, "reset", 1);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  Check(InitializeStart, "start/non-gpuva", 0);
  Check(InitializeReset, "reset/non-gpuva", 0);
#endif
  puts("backend ownership identity: PASS");
  return 0;
}
'''


class R150BackendIdentityTests(unittest.TestCase):
    def test_worker_identity_survives_g3_begin_job_and_runtime_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "identity.c"
            source.write_text(replay_source())
            for profile, define in (("legacy", None),
                                    ("b1", "APPLE_AGX_GPUVA_B1_QUALIFICATION"),
                                    ("g3", "APPLE_AGX_GPUVA_G3_QUALIFICATION")):
                with self.subTest(profile=profile):
                    binary = Path(tmp) / profile
                    command = [os.environ.get("CC", "clang"), "-std=c11",
                               "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                               "-I", str(SHARED / "include"),
                               "-I", str(SHARED / "tests"),
                               "-I", str(ADMISSION / "include")]
                    if define:
                        command.append("-D" + define)
                    command += [str(source)] + [str(SHARED / "src" / (name + ".c")) for name in
                        ("apple_agx_backend_runtime", "apple_agx_submission",
                         "apple_agx_firmware", "apple_agx_rtkit",
                         "apple_agx_gpuva_b1_submission", "apple_agx_dma_shadow")]
                    command += ["-o", str(binary)]
                    built = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    ran = subprocess.run([str(binary)], capture_output=True, text=True, cwd=ROOT)
                    self.assertEqual(ran.returncode, 0, profile + ": " + ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
