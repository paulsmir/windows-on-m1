from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
UMD = ROOT / "drivers/apple-agx/render-admission/umd/src/umd.c"
HEADERS = ROOT / "drivers/apple-agx/render-admission/include"
UMD_HEADERS = ROOT / "drivers/apple-agx/render-admission/umd/include"
ALLOCATION = ROOT / "drivers/apple-agx/render-admission/src/render_allocation.c"


class BltSourceEligibilityTests(unittest.TestCase):
    def test_valid_smaller_backbuffer_reaches_present_path(self):
        source = UMD.read_text()
        match = re.search(
            r"static BOOLEAN AdmissionUmdResourceIsPresentable\([^;]*\)\s*\{",
            source,
        )
        self.assertIsNotNone(match)
        start = source.index("{", match.start())
        depth = 1
        end = start + 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        function = source[match.start():end]
        program = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "direct_flip_contract.h"
typedef int BOOLEAN;
typedef unsigned long ULONG;
typedef unsigned UINT;
typedef unsigned D3DKMT_HANDLE;
typedef uintptr_t D3D10DDI_HRTRESOURCE;
typedef struct _ADMISSION_UMD_RESOURCE {
  ULONG Magic;
  D3D10DDI_HRTRESOURCE RuntimeResource;
  D3DKMT_HANDLE KernelAllocation;
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE DirectFlip;
  void *Retirement;
} ADMISSION_UMD_RESOURCE;
#define FALSE 0
#define ADMISSION_UMD_RESOURCE_MAGIC 0x53455255u
#define D3DDDIFMT_A8R8G8B8 1u
#define D3DDDIFMT_A8B8G8R8 2u
@@FUNCTION@@
int main(void) {
  ADMISSION_UMD_RESOURCE resource={0};
  resource.Magic=ADMISSION_UMD_RESOURCE_MAGIC;
  resource.KernelAllocation=0x40000b80u;
  resource.DirectFlip.Magic=ADMISSION_UMD_DIRECT_FLIP_RESOURCE_MAGIC;
  resource.DirectFlip.Version=ADMISSION_UMD_DIRECT_FLIP_RESOURCE_VERSION;
  resource.DirectFlip.SegmentId=2u;
  resource.DirectFlip.Linear=1u;
  resource.DirectFlip.Displayable=1u;
  assert(AdmissionAllocationDescribe(1024u,1024u,4u,3u,D3DDDIFMT_A8R8G8B8,0u,
      &resource.DirectFlip.Allocation));
  assert(AdmissionUmdResourceIsPresentable(&resource));
  resource.DirectFlip.Allocation.CpuVisible=1u;
  assert(!AdmissionUmdResourceIsPresentable(&resource));
  resource.DirectFlip.Allocation.CpuVisible=0u;
  resource.DirectFlip.SegmentId=0u;
  assert(!AdmissionUmdResourceIsPresentable(&resource));
  resource.DirectFlip.SegmentId=2u;
  resource.KernelAllocation=0u;
  assert(!AdmissionUmdResourceIsPresentable(&resource));
  resource.KernelAllocation=0x40000b80u;
  resource.DirectFlip.Allocation.Pitch=0u;
  assert(!AdmissionUmdResourceIsPresentable(&resource));
  assert(AdmissionAllocationDescribe(2560u,1600u,4u,3u,D3DDDIFMT_A8R8G8B8,0u,
      &resource.DirectFlip.Allocation));
  assert(AdmissionUmdResourceIsPresentable(&resource));
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source_path = Path(directory) / "source.c"
            executable = Path(directory) / "source"
            source_path.write_text(program.replace("@@FUNCTION@@", function))
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I", str(HEADERS),
                "-I", str(UMD_HEADERS), str(source_path), str(ALLOCATION),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
