"""G3 VidMm replay compiles the real KMD DDI bodies on the host."""
import subprocess
import sys
import os
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REPLAY = ROOT / "tests/g3_vidmm_replay.py"


class G3VidMmReplayTests(unittest.TestCase):
    def test_recorded_and_projected_sequence(self):
        result = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("all recorded and projected inputs passed", result.stdout)
        self.assertIn("real m1n1 broker dispatch", result.stdout)

    def test_dirty_initial_table_is_cleared_before_real_broker(self):
        env = dict(os.environ, G3_REPLAY_DIRTY_TABLE="1")
        result = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                                env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("real m1n1 broker dispatch", result.stdout)

    def test_pre_pte_address_fix_is_red(self):
        result = subprocess.run([sys.executable, str(REPLAY), "--revision",
                                 "077fad3e~"], cwd=ROOT, text=True,
                                capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("EXP783 level1", result.stderr)
        self.assertIn("c0000141", result.stderr)

    def test_exp783_package_is_red_at_parent_flags(self):
        result = subprocess.run([sys.executable, str(REPLAY), "--revision",
                                 "3b380b66"], cwd=ROOT, text=True,
                                capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("EXP783 level1", result.stderr)
        self.assertIn("c00000bb", result.stderr)

    def test_old_pasid_guard_is_red(self):
        result = subprocess.run([sys.executable, str(REPLAY),
                                 "--function-revision",
                                 "AdmissionDdiCreateProcess=fe007c79~"],
                                cwd=ROOT, text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("EXP776 CreateProcess PASID1: status c000000d", result.stderr)

    def test_old_context_mask_is_red(self):
        result = subprocess.run([sys.executable, str(REPLAY),
                                 "--function-revision",
                                 "AdmissionDdiCreateContext=e4aacfd0~",
                                 "--old-context-flags"], cwd=ROOT,
                                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("EXP778 CreateContext flags5: status c00000bb", result.stderr)

    def test_old_repeat_and_dma_guards_are_red(self):
        command = [sys.executable, str(REPLAY), "--function-revision",
                   "AdmissionG3UpdateParent=e7d9eb39~", "--function-revision",
                   "AdmissionG3UpdateLeaf=e7d9eb39~", "--function-revision",
                   "AdmissionGpuvaG3BuildPagingBuffer=e7d9eb39~"]
        for dma_only in (False, True):
            with self.subTest(dma_only=dma_only):
                env = dict(os.environ)
                if dma_only:
                    env["G3_REPLAY_DMA_ONLY"] = "1"
                result = subprocess.run(command, cwd=ROOT, env=env,
                                        text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("EXP780 level0", result.stderr)
                self.assertIn("c000000d", result.stderr)

    def test_pre_leaf_failure_receipt_is_red(self):
        result = subprocess.run([sys.executable, str(REPLAY),
                                 "--function-revision",
                                 "AdmissionG3UpdateLeaf=941e644f",
                                 "--function-revision",
                                 "AdmissionGpuvaG3BuildPagingBuffer=941e644f"],
                                cwd=ROOT, text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("last_paging_failure.Branch==7", result.stderr)


if __name__ == "__main__":
    unittest.main()
