"""rc81: the G4 heap-range proof costs O(1) per job once proven.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2011 (rc80 TVB growth, HD: 32 -> 828 blocks): c_kick_ms 2.05 -> 2.61 and
rc_prelude_kick_ms 1.95 -> 2.26 per frame although partial renders fell to 0.
AdmissionG4PrivateGraphAccess proves the named TVB heap (ordinal 2) with
AppleAgxGpuvaG3GraphContainsRangeAccess, a three-lookup walk per 16-KiB page:
6624 pages at 828 blocks, up to 16.6k at 2080, done at submit under the G3
state lock (which the worker's BeginJob waits for) and again at BeginJob.
The test runs the production graph with the fake broker and checks that
AppleAgxGpuvaG3GraphContainsRangeAccessCached:
- answers a repeated proof of the same large range in O(1) lookups;
- is invalidated by every mutation that can change its answer: a leaf
  remap, read-only remap or unmap in a proven leaf table, a parent unlink,
  a root move; and not by a leaf change in another table;
- never caches a failure, never lets a read proof answer a write proof,
  never answers for an uncertain graph, and leaves small ranges uncached.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class RangeProofCache(unittest.TestCase):
    def test_cached_heap_proof_is_constant_and_invalidated_by_mutation(self):
        with tempfile.TemporaryDirectory(prefix="g3-proof-") as tmp:
            binary = Path(tmp) / "proof"
            built = subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-DAPPLE_AGX_G3_LOOKUP_STATS",
                "-I", str(ROOT / "drivers/apple-agx/shared/include"),
                str(ROOT / "tests/apple_agx_gpuva_g3_range_proof_test.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_gpuva_g3_graph.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_gpuva_broker_v5_client.c"),
                "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stderr)

    def test_heap_access_uses_the_cached_proof(self):
        text = (ROOT / "drivers/apple-agx/render-admission/src/gpuva_g3_windows.c").read_text()
        start = text.index("static int AdmissionG4GraphAccess(void *opaque, unsigned long long va,\n"
                           "                                  unsigned int bytes, int write) {")
        self.assertIn("AppleAgxGpuvaG3GraphContainsRangeAccessCached(", text[start:start + 400])


if __name__ == "__main__":
    unittest.main()
