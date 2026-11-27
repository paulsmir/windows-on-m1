# SPDX-License-Identifier: MIT
"""B23 H1 (2): the m1n1 broker's denial bounds must equal the KMD's own layout constants.
- HV_AGX_LOCAL_SCANOUT_SCRATCH_BYTES (m1n1 hv_agx_local_reserve.h) == ADMISSION_SCANOUT_SCRATCH_BYTES
  (render-admission/include/render_memory.h);
- HV_AGX_LOCAL_BACKEND_BYTES == APPLE_AGX_LOCAL_RESERVE_BACKEND_BYTES (shared apple_agx_local_reserve_abi.h);
- HV_AGX_LOCAL_PRIVATE_BYTES == APPLE_AGX_LOCAL_RESERVE_PRIVATE_BYTES when the KMD header defines it.
Inputs: M1N1_TREE (m1n1 source with the B23 change) and KMD_TREE (a checkout holding drivers/apple-agx); skipped
when either is absent."""
import os
import re
import unittest
from pathlib import Path

M1N1 = Path(os.environ.get("M1N1_TREE", "/Volumes/pwdev/public_windows/.claude/worktrees/optimistic-williams-06f86e/"
                                        ".local/m1n1-wt-el2time"))
KMD = Path(os.environ.get("KMD_TREE", "/Volumes/pwdev/public_windows/.claude/worktrees/gpu-perf-parity-aaa98a"))


def define(path, name):
    m = re.search(r"#define\s+" + re.escape(name) + r"\s+(?:UINT64_C\()?(0x[0-9a-fA-F]+|\d+)", path.read_text())
    return int(m.group(1), 0) if m else None


@unittest.skipUnless((M1N1 / "src/hv_agx_local_reserve.h").is_file() and
                     (KMD / "drivers/apple-agx/render-admission/include/render_memory.h").is_file(),
                     "m1n1 or KMD tree not present")
class B23DenyConstants(unittest.TestCase):
    def setUp(self):
        self.hv = M1N1 / "src/hv_agx_local_reserve.h"
        self.abi = KMD / "drivers/apple-agx/shared/include/apple_agx_local_reserve_abi.h"
        self.mem = KMD / "drivers/apple-agx/render-admission/include/render_memory.h"

    def test_scratch(self):
        hv = define(self.hv, "HV_AGX_LOCAL_SCANOUT_SCRATCH_BYTES")
        self.assertIsNotNone(hv)
        self.assertEqual(hv, define(self.mem, "ADMISSION_SCANOUT_SCRATCH_BYTES"))

    def test_backend(self):
        kmd = define(self.abi, "APPLE_AGX_LOCAL_RESERVE_BACKEND_BYTES")
        self.assertIsNotNone(kmd)
        self.assertEqual(define(self.hv, "HV_AGX_LOCAL_BACKEND_BYTES"), kmd)

    def test_private(self):
        kmd = define(self.abi, "APPLE_AGX_LOCAL_RESERVE_PRIVATE_BYTES")
        if kmd is None:
            self.skipTest("KMD header names no private-span constant")
        self.assertEqual(define(self.hv, "HV_AGX_LOCAL_PRIVATE_BYTES"), kmd)


if __name__ == "__main__":
    unittest.main()
