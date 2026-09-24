"""Paging-process CPU updates and Repeat PTE input follow WDK 26100."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"
SHARED = ROOT / "drivers/apple-agx/shared"


class G3PagingBootstrapTests(unittest.TestCase):
    def test_repeat_selects_one_pte_for_the_entire_update(self):
        source = r'''
#include "apple_agx_gpuva_g3_translation.h"
int main(void) {
  unsigned int i;
  for (i = 0; i < 8192u; ++i) {
    if (AppleAgxGpuvaG3PteInputIndex(i, 1u) != 0u) return 1;
    if (AppleAgxGpuvaG3PteInputIndex(i, 0u) != i) return 2;
  }
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            test = Path(temp) / "repeat.c"
            bitcode = Path(temp) / "repeat.bc"
            test.write_text(source)
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-I", str(SHARED / "include"),
                "-emit-llvm", "-c", str(test), "-o", str(bitcode),
            ], check=True, cwd=ROOT)
            subprocess.run([os.environ.get("LLI", "lli"), str(bitcode)],
                           check=True, cwd=ROOT)

    def test_kmd_uses_repeat_and_cpu_physical_mapping(self):
        paging = (RENDER / "src/gpuva_g3_paging_windows.c").read_text()
        gpuva = (RENDER / "src/gpuva_g3_windows.c").read_text()
        callback = (RENDER / "src/paging_windows.c").read_text()
        self.assertNotIn("update->Flags.Repeat ||", paging)
        self.assertEqual(paging.count("AppleAgxGpuvaG3PteInputIndex("), 3)
        self.assertIn("MmGetPhysicalAddress(address->CpuVirtual)", gpuva)
        self.assertNotIn("pointer < base", gpuva)
        self.assertIn("AdmissionRecordGpuvaG3PagingInput(", callback)
        self.assertIn("AdmissionRecordGpuvaG3PagingResult(", callback)


if __name__ == "__main__":
    unittest.main()
