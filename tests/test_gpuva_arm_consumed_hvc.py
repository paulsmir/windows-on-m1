"""Exercise the versioned m1n1 arm receipt validator without hardware."""
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
M1N1 = ROOT / "m1n1_windows/src"


class ArmConsumedHvcTests(unittest.TestCase):
    def test_versioned_sequence_validation(self):
        source = r'''
#include "hv_guest_ipa_pa.h"
#include <assert.h>
int main(void) {
  hv_guest_ipa_pa_u32 sequence=0;
  assert(hv_guest_arm_consumed_handle(HV_GPUVA_ARM_CONSUMED_HVC_IMMEDIATE,
      ((hv_guest_ipa_pa_u64)HV_GPUVA_ARM_CONSUMED_VERSION << 32) | 93,
      &sequence));
  assert(sequence==93);
  assert(hv_guest_arm_consumed_handle(HV_GPUVA_ARM_CONSUMED_HVC_IMMEDIATE,
      ((hv_guest_ipa_pa_u64)2 << 32) | 93, &sequence));
  assert(sequence==0);
  assert(hv_guest_arm_consumed_handle(HV_GPUVA_ARM_CONSUMED_HVC_IMMEDIATE,
      ((hv_guest_ipa_pa_u64)HV_GPUVA_ARM_CONSUMED_VERSION << 32), &sequence));
  assert(sequence==0);
  assert(!hv_guest_arm_consumed_handle(HV_GUEST_IPA_PA_HVC_IMMEDIATE, 93,
      &sequence));
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            c = Path(directory) / "arm.c"
            binary = Path(directory) / "arm"
            c.write_text(source)
            subprocess.run(["clang", "-std=c11", "-DHV_GUEST_IPA_PA_HOST_TEST",
                            "-I", str(M1N1), str(c),
                            str(M1N1 / "hv_guest_ipa_pa.c"), "-o", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True,
                           text=True)
