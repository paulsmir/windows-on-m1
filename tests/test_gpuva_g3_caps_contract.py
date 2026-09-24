import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "drivers/apple-agx/shared/include"


class GpuvaG3CapsContractTests(unittest.TestCase):
    def test_real_caps_and_rejected_variants(self):
        source = r'''
#include "apple_agx_gpuva_g3_caps.h"
int main(void) {
  APPLE_AGX_GPUVA_G3_CAPS caps = AppleAgxGpuvaG3Caps();
  if (!AppleAgxGpuvaG3CapsValid(&caps, 16u, 0u, 0u)) return 1;
  if (caps.Level[0].IndexBits != 13u ||
      caps.Level[0].SizeBytes != 0x20000u ||
      caps.Level[1].SizeBytes != 0x8000u ||
      caps.Level[2].SizeBytes != 0x4000u) return 2;
  caps.Level[0].SizeBytes = 0x4000u;
  if (AppleAgxGpuvaG3CapsValid(&caps, 16u, 0u, 0u)) return 3;
  caps = AppleAgxGpuvaG3Caps();
  if (AppleAgxGpuvaG3CapsValid(&caps, 16u, 1u, 0u)) return 4;
  if (AppleAgxGpuvaG3CapsValid(&caps, 16u, 0u, 1u)) return 5;
  caps.Level[1].SegmentId = 0u;
  if (AppleAgxGpuvaG3CapsValid(&caps, 16u, 0u, 0u)) return 6;
  caps = AppleAgxGpuvaG3Caps();
  caps.VirtualAddressBits = 40u;
  if (AppleAgxGpuvaG3CapsValid(&caps, 16u, 0u, 0u)) return 7;
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            src = Path(directory) / "caps.c"
            binary = Path(directory) / "caps"
            src.write_text(source)
            subprocess.run([os.environ.get("CC", "/tmp/agx-host-cc"),
                            "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(INCLUDE), str(src), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
