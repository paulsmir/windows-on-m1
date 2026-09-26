from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "drivers/apple-agx/shared/include"
TEST = ROOT / "tests/fixtures/agx_kmd_local_reserve_test.c"
PHYSICAL = ROOT / "drivers/apple-agx/render-admission/src/physical_memory_windows.c"
RUNTIME = ROOT / "drivers/apple-agx/render-admission/src/memory_runtime_windows.c"
RECEIPTS = ROOT / "drivers/apple-agx/render-admission/src/receipts.c"


class KmdLocalReserveTest(unittest.TestCase):
    def test_start_device_borrows_local_range_while_scratch_keeps_dxgk_allocation(self):
        physical = PHYSICAL.read_text()
        runtime = RUNTIME.read_text()
        self.assertIn("AdmissionPhysicalBorrowLocal", physical)
        self.assertIn("MmMapIoSpaceEx", physical)
        self.assertIn("MmUnmapIoSpace", physical)
        self.assertIn("AppleAgxLocalReserveMatchesResource", physical)
        self.assertIn("DxgkCbCreatePhysicalMemoryObject", physical)
        local_start = runtime.index("AdmissionMemoryStartLocalObject")
        local_end = runtime.index("runtime->LocalReady = TRUE", local_start)
        self.assertIn("AdmissionPhysicalBorrowLocal", runtime[local_start:local_end])
        self.assertNotIn("AppleAgxMemoryAllocateAligned", runtime[local_start:local_end])
        self.assertIn("AdmissionRecordLocalReserve", runtime)
        receipt = RECEIPTS.read_text()
        self.assertIn('L"Wom1LocalReserve"', receipt)
        self.assertIn("ZwFlushKey(key)", receipt)

    def test_broker_receipt_selects_dram_resource_not_equal_sized_sgx_mmio(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "kmd-reserve"
            build = subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall",
                                    "-Wextra", "-Werror", "-I", str(INCLUDE),
                                    str(TEST), "-o", str(binary)], capture_output=True,
                                   text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
