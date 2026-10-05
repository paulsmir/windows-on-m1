"""Exercise Windows capture/consumer expressions with real lifetime helpers."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx/render-admission"


class OutputExpectationLifetimeTests(unittest.TestCase):
    def test_release_cannot_erase_completed_oracle(self):
        # Compile the actual Windows metadata block and late consumer argument.
        # Reverting either to zero/runtime scratch must fail this composition.
        source = (DRIVER / "src/backend_platform_windows.c").read_text()
        capture = re.search(
            r"output.VerificationKind =\s*"
            r"AdmissionBackendOutputVerificationTriangle;.*?"
            r"output.ExpectedColor = .*?;", source, re.S).group(0)
        consumer = re.search(
            r"Output->RenderedBytes,\s*"
            r"AdmissionDynamicOutputLayoutAgxTiled64,\s*([^)]*)\)",
            source).group(1).strip()
        release = re.search(
            r"Runtime->DynamicExpectedForegroundColor = 0u;", source).group(0)
        program = r'''
#include "render_completed_output.h"
#include "render_dynamic_output.h"
#include <assert.h>
#include <string.h>
typedef struct { unsigned DynamicBackgroundColor;
                 unsigned DynamicExpectedForegroundColor; } TEST_RUNTIME;
int main(void) {
  unsigned char pixels[16384] = {0};
  TEST_RUNTIME storage = {0xff112233u, 0x80808080u};
  TEST_RUNTIME *runtime = &storage, *Runtime = &storage;
  ADMISSION_BACKEND_OUTPUT_VIEW output = {0};
  ADMISSION_ALLOCATION_DESCRIPTION description;
  ADMISSION_ALLOCATION_OBJECT owner;
  ADMISSION_COMPLETED_OUTPUT completed;
  ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT snapshot;
  assert(AdmissionAllocationDescribe(16,256,4,1,21,0,&description));
  assert(AdmissionAllocationCreate(&description,&owner));
  output.AllocationCpuAddress = output.RenderedCpuAddress = pixels;
  output.AllocationGpuAddress = output.RenderedGpuAddress = 0x1500000000ULL;
  output.AllocationPhysicalAddress = output.RenderedPhysicalAddress = 0x900000000ULL;
  output.AllocationBytes = 16384;
  output.RenderedBytes = 1024;
  output.AllocationWidth = output.RenderWidth = 16;
  output.AllocationHeight = 256; output.RenderHeight = 16;
  output.AllocationPitch = output.RenderPitch = 64;
  output.AllocationFormat = 21;
  CAPTURE_BLOCK
  AdmissionCompletedOutputInitialize(&completed);
  assert(AdmissionCompletedOutputCapture(&completed,1,271,&output,&owner));
  RELEASE_BLOCK
  assert(AdmissionCompletedOutputMarkReleased(&completed,271));
  assert(AdmissionCompletedOutputMarkReleased(&completed,271));
  assert(completed.ReleaseCount == 1 && completed.CaptureCount == 1);
  const ADMISSION_BACKEND_OUTPUT_VIEW *Output = &completed.View;
  AdmissionDynamicOutputSnapshotInitialize(&snapshot);
  assert(AdmissionDynamicOutputSnapshotCapture(&snapshot,271,completed.Generation,
      Output->RenderedGpuAddress,Output->RenderedPhysicalAddress,pixels,1024,
      AdmissionDynamicOutputLayoutAgxTiled64, CONSUMER));
  assert(snapshot.ExpectedForegroundColor == 0x80808080u);
  storage.DynamicExpectedForegroundColor = 0xff0000ffu;
  assert(completed.View.ExpectedColor == 0x80808080u);
  assert(!AdmissionCompletedOutputCapture(&completed,2,272,&output,&owner));
  assert(completed.View.ExpectedColor == 0x80808080u);
  assert(owner.OpenCount == 1);
  return 0;
}
'''.replace("CAPTURE_BLOCK", capture).replace("RELEASE_BLOCK", release).replace("CONSUMER", consumer)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "lifetime.c"
            path.write_text(program)
            binary = Path(tmp) / "lifetime"
            subprocess.run([
                "clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-I", str(DRIVER / "include"),
                "-I", str(ROOT / "drivers/apple-agx/shared/include"), str(path),
                str(DRIVER / "src/render_completed_output.c"),
                str(DRIVER / "src/render_allocation.c"),
                str(DRIVER / "src/render_qualification.c"),
                str(DRIVER / "src/render_dynamic_output.c"), "-o", str(binary)
            ], check=True)
            subprocess.run([str(binary)], check=True)
