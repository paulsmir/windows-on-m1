"""G3 VidMm replay compiles the real KMD DDI bodies on the host."""
import subprocess
import sys
import os
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REPLAY = ROOT / "tests/g3_vidmm_replay.py"


class G3VidMmReplayTests(unittest.TestCase):
    def test_r154_preempted_private_scene_survives_deferred_release(self):
        for mode in ("teardown", "late-release", "cancel", "uncertain"):
            with self.subTest(mode=mode):
                r = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                    env=dict(os.environ, G3_REPLAY_R137="1", G3_REPLAY_R137_COMBINED="1",
                             G3_REPLAY_R154_RESUBMIT=mode), text=True, capture_output=True)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                self.assertIn("PASS", r.stdout)

    def test_exp856_full_local_paging_bounds(self):
        for profile in ("16", "64"):
            with self.subTest(profile=profile):
                r = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                    env=dict(os.environ, G3_REPLAY_R144="1", G3_REPLAY_PROFILE=profile),
                    text=True, capture_output=True)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                self.assertIn("R144 full-local paging: PASS", r.stdout)

    def test_r139_os_reserved_leaf_spans(self):
        # EXP855B returned 32 MiB; neighbors/high slots prevent a trace allowlist.
        for base in ("0x2000000", "0x6000000", "0xffe000000", "0x7ffe000000"):
            with self.subTest(base=base):
                r = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                    env=dict(os.environ, G3_REPLAY_R137="1",
                             G3_REPLAY_RESERVE_BASE=base), text=True, capture_output=True)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_r139_low_reservation_private_escape_and_completion(self):
        for profile in ("16", "64"):
            for mode in ("G3_REPLAY_R137_ESCAPE", "G3_REPLAY_R137_COMBINED"):
                with self.subTest(profile=profile, mode=mode):
                    env = dict(os.environ, G3_REPLAY_R137="1",
                               G3_REPLAY_RESERVE_BASE="0x2000000", G3_REPLAY_PROFILE=profile)
                    env[mode] = "1"
                    r = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                                       env=env, text=True, capture_output=True)
                    self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_r139_low_private_leaf_shares_root_with_vidmm(self):
        for profile in ("16", "64"):
            r = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                env=dict(os.environ, G3_REPLAY_R135="1", G3_REPLAY_R137_PRIVATE="1",
                         G3_REPLAY_RESERVE_BASE="0x2000000", G3_REPLAY_PROFILE=profile),
                text=True, capture_output=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_r137_uncertain_reset_and_revoke_quarantine(self):
        for fault in ("reset", "revoke"):
            r = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                env=dict(os.environ, G3_REPLAY_R137="1", G3_REPLAY_R137_COMBINED="1",
                         G3_REPLAY_R137_QUARANTINE=fault), text=True, capture_output=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_r137_private_producer_kmd_broker_and_builder(self):
        for profile in ("16", "64"):
            r = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                env=dict(os.environ, G3_REPLAY_R137="1", G3_REPLAY_R137_COMBINED="1",
                         G3_REPLAY_PROFILE=profile), text=True, capture_output=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_r137_production_escape_ownership_and_quota(self):
        for profile in ("16", "64"):
            r = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                env=dict(os.environ, G3_REPLAY_R137="1", G3_REPLAY_R137_ESCAPE="1",
                         G3_REPLAY_PROFILE=profile), text=True, capture_output=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_r137_private_grants_and_parent_lifecycle(self):
        cases = [dict(G3_REPLAY_R137="1")]
        for profile in ("16", "64"):
            cases.append(dict(G3_REPLAY_R135="1", G3_REPLAY_PROFILE=profile))
        for case in cases:
            result = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                env=dict(os.environ, G3_REPLAY_R137_PRIVATE="1", **case),
                text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_r137_reservation_lifetime_and_failures(self):
        result = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
            env=dict(os.environ, G3_REPLAY_R137="1"), text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("R137 reservation: PASS", result.stdout)

    def test_exp854b_empty_current_root_reuse(self):
        for profile, allocation_fault in (("16", ""), ("64", ""), ("16", "1")):
            with self.subTest(profile=profile, allocation_fault=allocation_fault):
                env = dict(os.environ, G3_REPLAY_R135="1", G3_REPLAY_PROFILE=profile,
                           G3_REPLAY_R135_ALLOC=allocation_fault)
                if not allocation_fault:
                    env.pop("G3_REPLAY_R135_ALLOC", None)
                result = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                                        env=env, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("R135 empty root reuse: PASS", result.stdout)

    def test_r134_system_64k_outer_ddi_with_real_broker(self):
        env = dict(os.environ, G3_REPLAY_R134="1")
        result = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                                env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("R134 system 64K outer DDI: PASS", result.stdout)

    def test_exp852_protected_system_group_stays_unpublished(self):
        env = dict(os.environ, G3_REPLAY_R133="1")
        result = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                                env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("R133 protected publication: PASS", result.stdout)

    def test_r132_system_frame_lifetime_and_broker(self):
        for fault in ("G3_REPLAY_R132", "G3_REPLAY_R132_SYNC_ONCE", "G3_REPLAY_R132_TLB"):
            with self.subTest(fault=fault):
                env=dict(os.environ, G3_REPLAY_R132="1")
                env[fault]="1"
                result = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                    env=env, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("R132 system lifetime: PASS", result.stdout)

    def test_exp799_shared_local_leaf_with_real_broker(self):
        env = dict(os.environ, G3_REPLAY_EXP799="1")
        result = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                                env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("EXP799 local leaf", result.stdout)

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

    def test_exp846_freed_leaf_page_reused_as_level1(self):
        env = dict(os.environ, G3_REPLAY_LEVEL_REUSE="unlinked")
        result = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                                env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("EXP846 level reuse", result.stdout)

    def test_exp847_reuse_with_stale_link_from_freed_parent(self):
        env = dict(os.environ, G3_REPLAY_LEVEL_REUSE="stale")
        result = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                                env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("EXP846 level reuse", result.stdout)

    def test_exp786_table_page_as_leaf_backing(self):
        env = dict(os.environ, G3_REPLAY_SELF_TABLE_BACKING="1")
        result = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                                env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_exp788_system_pte_stays_unpublished_without_backing(self):
        env = dict(os.environ, G3_REPLAY_SINGLE_PTE="1")
        result = subprocess.run([sys.executable, str(REPLAY)], cwd=ROOT,
                                env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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
