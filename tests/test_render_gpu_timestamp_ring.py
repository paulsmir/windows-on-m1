"""GPU timestamps: the KMD keeps completed jobs' firmware timestamps by fence.

Invariant caught: the UMD fetches a job's firmware start/end by its
batch's command GPU VA after the batch completed (the UMD never sees
dxgkrnl's submission fence). A record answers only the process that
submitted it and that exact batch; when the command BO was reused, the
newest submission wins; a record needs a fence, a command VA, an end value
and a known kind (render: template objects 28/29, compute: the sidecar
block). Runs render_job_timing.c on the host.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "drivers/apple-agx/render-admission/include"
SOURCE = ROOT / "drivers/apple-agx/render-admission/src/render_job_timing.c"

PROGRAM = r'''
#include <assert.h>
#include <string.h>
#include "render_job_timing.h"
int main(void) {
  static ADMISSION_GPU_TIMESTAMP_RING ring;
  ADMISSION_GPU_TIMESTAMP out;
  const unsigned long long a = 0x1500a00000ULL, b = 0x1500a40000ULL;
  memset(&out, 0, sizeof(out));
  assert(!AdmissionGpuTimestampFind(&ring, 100u, a, &out));
  assert(AdmissionGpuTimestampRecord(&ring, 5u, 100u, a, ADMISSION_GPU_TIMESTAMP_RENDER, 0x10ULL, 0x20ULL, 0x99ULL));
  assert(AdmissionGpuTimestampFind(&ring, 100u, a, &out) && out.Start == 0x10ULL &&
         out.End == 0x20ULL && out.Kind == ADMISSION_GPU_TIMESTAMP_RENDER && out.Fence == 5u &&
         out.Qpc == 0x99ULL);
  /* another process or batch does not see it */
  assert(!AdmissionGpuTimestampFind(&ring, 101u, a, &out));
  assert(!AdmissionGpuTimestampFind(&ring, 100u, b, &out));
  /* the batch's newer submission (its command BO reused) wins */
  assert(AdmissionGpuTimestampRecord(&ring, 9u, 100u, a, ADMISSION_GPU_TIMESTAMP_COMPUTE, 0x30ULL, 0x40ULL, 0x99ULL));
  assert(AdmissionGpuTimestampFind(&ring, 100u, a, &out) && out.Fence == 9u &&
         out.Kind == ADMISSION_GPU_TIMESTAMP_COMPUTE && out.End == 0x40ULL);
  /* a slot's newer fence replaces it */
  assert(AdmissionGpuTimestampRecord(&ring, 9u + ADMISSION_GPU_TIMESTAMP_CAPACITY, 100u, b,
                                     ADMISSION_GPU_TIMESTAMP_RENDER, 0x50ULL, 0x60ULL, 0x99ULL));
  assert(AdmissionGpuTimestampFind(&ring, 100u, a, &out) && out.Fence == 5u);
  assert(AdmissionGpuTimestampFind(&ring, 100u, b, &out) && out.End == 0x60ULL);
  /* malformed records are refused */
  assert(!AdmissionGpuTimestampRecord(&ring, 0u, 100u, a, ADMISSION_GPU_TIMESTAMP_RENDER, 1u, 2u, 0x99ULL));
  assert(!AdmissionGpuTimestampRecord(&ring, 6u, 100u, 0u, ADMISSION_GPU_TIMESTAMP_RENDER, 1u, 2u, 0x99ULL));
  assert(!AdmissionGpuTimestampRecord(&ring, 6u, 100u, a, ADMISSION_GPU_TIMESTAMP_RENDER, 1u, 0u, 0x99ULL));
  assert(!AdmissionGpuTimestampRecord(&ring, 6u, 100u, a, 3u, 1u, 2u, 0x99ULL));
  /* calibration: the newest job of any process, End and Qpc only */
  assert(AdmissionGpuTimestampRecord(&ring, 200u, 7u, b, ADMISSION_GPU_TIMESTAMP_RENDER, 0x70ULL, 0x80ULL, 0x77ULL));
  assert(AdmissionGpuTimestampFind(&ring, 100u, 0u, &out) && out.Fence == 200u &&
         out.End == 0x80ULL && out.Qpc == 0x77ULL && !out.Start && !out.Kind &&
         !out.ProcessId && !out.CommandVa);
  return 0;
}
'''


class GpuTimestampRing(unittest.TestCase):
    def test_records_by_fence_and_process(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ring.c"
            exe = Path(directory) / "ring"
            path.write_text(PROGRAM)
            built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                                    "-Werror", "-fsanitize=address,undefined", "-I", str(INCLUDE),
                                    str(path), str(SOURCE), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
