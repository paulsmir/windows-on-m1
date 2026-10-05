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
  APPLE_AGX_GPUVA_G3_CAPS caps = AppleAgxGpuvaG3Caps(0u);
  if (!AppleAgxGpuvaG3CapsValid(&caps, 16u, 0u, 0u)) return 1;
  if (caps.Level[0].IndexBits != 13u ||
      caps.Level[0].SizeBytes != 0x20000u ||
      caps.Level[1].SizeBytes != 0x8000u ||
      caps.Level[2].SizeBytes != 0x4000u) return 2;
  caps.Level[0].SizeBytes = 0x4000u;
  if (AppleAgxGpuvaG3CapsValid(&caps, 16u, 0u, 0u)) return 3;
  caps = AppleAgxGpuvaG3Caps(0u);
  if (AppleAgxGpuvaG3CapsValid(&caps, 16u, 1u, 0u)) return 4;
  if (AppleAgxGpuvaG3CapsValid(&caps, 16u, 0u, 1u)) return 5;
  caps.Level[1].SegmentId = 0u;
  if (AppleAgxGpuvaG3CapsValid(&caps, 16u, 0u, 0u)) return 6;
  caps = AppleAgxGpuvaG3Caps(0u);
  caps.VirtualAddressBits = 40u;
  if (AppleAgxGpuvaG3CapsValid(&caps, 16u, 0u, 0u)) return 7;
  caps = AppleAgxGpuvaG3Caps(1u);
  if (caps.Leaf64KBytes != 0x4000u ||
      !AppleAgxGpuvaG3CapsValid(&caps, 16u, 1u, 0u)) return 8;
  caps.Leaf64KBytes = 8192u;
  if (AppleAgxGpuvaG3CapsValid(&caps, 16u, 1u, 0u)) return 23;
  caps = AppleAgxGpuvaG3Caps(1u);
  caps.Leaf64KBytes = 4096u;
  if (AppleAgxGpuvaG3CapsValid(&caps, 16u, 1u, 0u)) return 9;
  caps.Leaf64KBytes = 0u;
  if (AppleAgxGpuvaG3CapsValid(&caps, 16u, 1u, 0u)) return 10;
  {
    APPLE_AGX_GPUVA_G3_ADMISSION_CONTRACT model =
        AppleAgxGpuvaG3AdmissionContract(1u, 64u);
    if (!AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 11;
    model.NodeGpuMmuMask = 0u;
    if (AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 12;
    model = AppleAgxGpuvaG3AdmissionContract(1u, 64u);
    model.MmuCount = 0u;
    if (AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 13;
    model = AppleAgxGpuvaG3AdmissionContract(1u, 64u);
    model.AdapterGpuMmuSupported = 0u;
    if (AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 14;
    model = AppleAgxGpuvaG3AdmissionContract(1u, 64u);
    model.AdapterIoMmuSupported = 1u;
    if (AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 15;
    model = AppleAgxGpuvaG3AdmissionContract(1u, 64u);
    model.PagingNode = 1u;
    if (AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 16;
    model = AppleAgxGpuvaG3AdmissionContract(1u, 64u);
    model.ApertureCount = 2u;
    if (AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 17;
    model = AppleAgxGpuvaG3AdmissionContract(1u, 64u);
    model.LocalUse64KBPages = 0u;
    if (!AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 18;
    model = AppleAgxGpuvaG3AdmissionContract(1u, 64u);
    model.Leaf64KBytes = 0u;
    if (AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 19;
    model = AppleAgxGpuvaG3AdmissionContract(1u, 64u);
    model.Leaf64KBytes = 8192u;
    if (AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 24;
    model = AppleAgxGpuvaG3AdmissionContract(1u, 16u);
    if (!AppleAgxGpuvaG3AdmissionContractValid(&model, 16u) ||
        model.LocalUse64KBPages != 0u ||
        model.SysMem64KBPageSupported != 1u ||
        model.Leaf64KBytes != 0x4000u)
      return 25;
    model.Leaf64KBytes = 0u;
    if (AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 26;
    model = AppleAgxGpuvaG3AdmissionContract(1u, 16u);
    model.LocalUse64KBPages = 1u;
    if (!AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 27;
    model = AppleAgxGpuvaG3AdmissionContract(1u, 16u);
    model.SysMem64KBPageSupported = 0u;
    model.Leaf64KBytes = 0u;
    if (!AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 28;
    model.Leaf64KBytes = 0x4000u;
    if (AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 29;
    model.SysMem64KBPageSupported = 2u;
    if (AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 30;
    model = AppleAgxGpuvaG3AdmissionContract(1u, 64u);
    model.MmuSizeBytes = 0x10000u;
    if (AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 20;
    model = AppleAgxGpuvaG3AdmissionContract(0u, 64u);
    if (!AppleAgxGpuvaG3AdmissionContractValid(&model, 16u) ||
        model.NodeGpuMmuMask != 0u || model.MmuCount != 0u) return 21;
    model.NodeGpuMmuMask = 1u;
    if (AppleAgxGpuvaG3AdmissionContractValid(&model, 16u)) return 22;
  }
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
