"""POOL-LADDER: the opt-in 256 KiB rung between the 2 MiB and 64 KiB slab
attempts (render_pool_ladder.h, AdmissionPoolAllocSlab)."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_apple_agx_pool_headroom import function
import test_g4_pool_residency_limit as residency_limit

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"
POOL = RENDER / "src/pool_windows.c"


class PoolLadderTest(unittest.TestCase):
    def test_rung_policy(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        With the switch off the slab must make exactly the pre-POOL-LADDER
        attempts (2 MiB, 64 KiB); with it on the 256 KiB rung runs between
        them; a failed 256 KiB attempt must skip the rung for the next 16
        allocations and then retry it, and a success must not start a
        back-off (an inverted condition would silently disable the rung).
        """
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "render_pool_ladder_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                str(RENDER / "tests" / "render_pool_ladder_test.c"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_switch_reader(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Only REG_DWORD Wom1PoolLadder=1 turns the rung on; absent, mistyped
        or other values keep the production attempts, and the key is closed.
        """
        residency_limit.PoolResidencyLimit.check_registry_reader(
            self, "AdmissionPoolLadderEnabled", "Wom1PoolLadder", 1)
        start = function(POOL.read_text(), "AdmissionPoolDemandStart")
        self.assertIn("pool->PoolLadder = AdmissionPoolLadderEnabled(Context);", start)


if __name__ == "__main__":
    unittest.main()
