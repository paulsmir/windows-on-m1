"""EXP2213: the OV5 TA waiver leaves a waived successor's LeadBarrierAddress at
zero (its only wait is the 3D-queue Barrier in D3LeadBarrierAddress), but
AppleAgxBackendRuntimeSubmitSuccessor refused every successor without a lead
Barrier as ImageFailed; the KMD counted the refused kick as a fault and reset
the engine (5 waivers, 5 resets). The successor lead rule must accept exactly
the waived shape and still refuse a successor with no wait at all."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers/apple-agx/shared"

HARNESS = r'''
#include "apple_agx_backend_runtime.c"
#include <stdio.h>
static APPLE_AGX_BACKEND_JOB_IMAGE job(APPLE_AGX_U64 lead, APPLE_AGX_U64 d3, APPLE_AGX_U32 waived,
                                       APPLE_AGX_U32 compute, APPLE_AGX_U64 ta) {
  APPLE_AGX_BACKEND_JOB_IMAGE j;
  AppleAgxBackendZero(&j, (APPLE_AGX_U32)sizeof(j));
  j.LeadBarrierAddress = lead; j.D3LeadBarrierAddress = d3; j.TaLeadWaived = waived;
  j.ComputeWorkAddressCount = compute; j.TaBarrierAddress = ta;
  return j;
}
int main(void) {
  APPLE_AGX_BACKEND_JOB_IMAGE a = job(0x1000, 0, 0, 0, 0);      /* ordinary successor */
  APPLE_AGX_BACKEND_JOB_IMAGE b = job(0, 0x2000, 1, 0, 0);      /* OV5 waived render */
  APPLE_AGX_BACKEND_JOB_IMAGE c = job(0, 0, 0, 0, 0);           /* no wait at all */
  APPLE_AGX_BACKEND_JOB_IMAGE d = job(0, 0, 1, 0, 0);           /* waived, no 3D wait */
  APPLE_AGX_BACKEND_JOB_IMAGE e = job(0, 0x2000, 0, 0, 0);      /* 3D wait but not waived */
  APPLE_AGX_BACKEND_JOB_IMAGE f = job(0, 0x2000, 1, 1, 0);      /* waived with compute work */
  APPLE_AGX_BACKEND_JOB_IMAGE g = job(0, 0x2000, 1, 0, 0x3000); /* waived with a TA prelude Barrier */
  printf("%d %d %d %d %d %d %d\n", AppleAgxBackendSuccessorLeadValid(&a),
         AppleAgxBackendSuccessorLeadValid(&b), AppleAgxBackendSuccessorLeadValid(&c),
         AppleAgxBackendSuccessorLeadValid(&d), AppleAgxBackendSuccessorLeadValid(&e),
         AppleAgxBackendSuccessorLeadValid(&f), AppleAgxBackendSuccessorLeadValid(&g));
  return 0;
}
'''


class BackendSuccessorWaivedLead(unittest.TestCase):
    def test_submit_successor_uses_the_lead_rule(self):
        src = (SHARED / "src/apple_agx_backend_runtime.c").read_text(encoding="utf-8")
        body = src[src.index("AppleAgxBackendRuntimeSubmitSuccessor("):]
        body = body[:body.index("return AppleAgxBackendRuntimeResultImageFailed;")]
        self.assertIn("!AppleAgxBackendSuccessorLeadValid(&Runtime->SuccessorJob)", body)
        self.assertNotIn("SuccessorJob.LeadBarrierAddress == 0ULL", body)

    def test_lead_rule_accepts_only_the_waived_shape(self):
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp) / "h.c"; exe = Path(tmp) / "h"
            c.write_text(HARNESS, encoding="utf-8")
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", f"-I{SHARED / 'include'}", f"-I{SHARED / 'src'}",
                            "-Wl,-undefined,dynamic_lookup", str(c), "-o", str(exe)],
                           check=True, capture_output=True, text=True)
            out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout.split()
        self.assertEqual(out, ["1", "1", "0", "0", "0", "0", "0"])


if __name__ == "__main__":
    unittest.main()
