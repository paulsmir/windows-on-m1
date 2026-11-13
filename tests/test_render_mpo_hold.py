"""HOLD-1 (EXP2375): the pending plane set held while a scan-out commit is
outstanding.  Deterministic contract: two one-layer flips merge into one
commit; a same-layer second flip is refused (DEVICE_BUSY, re-offered by
dxgkrnl); the set is taken exactly once with each accepted PresentId."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx/render-admission"


class RenderMpoHold(unittest.TestCase):
    def test_pending_set_state_machine(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "mpo_hold_test"
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wconversion", "-Wsign-conversion", "-pedantic", "-fsanitize=address,undefined",
                            "-I", str(DRIVER / "include"), str(DRIVER / "tests/mpo_hold_test.c"),
                            str(DRIVER / "src/mpo_hold.c"), "-o", str(binary)], cwd=ROOT, check=True)
            run = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("mpo_hold_test: OK", run.stdout)

    def test_kernel_wires_the_hold_into_set3_and_the_latch(self):
        mpo = (DRIVER / "src/mpo_windows.c").read_text()
        scanout = (DRIVER / "src/scanout_windows.c").read_text()
        self.assertIn("AdmissionMpoHoldAdmit(", mpo)
        # HOLD-2: Set3 decides through the pure AdmissionMpoHoldSet3 and queues the commit DPC;
        # the latch handler and the vertical tick commit through the same routine with their source.
        self.assertIn("AdmissionMpoHoldSet3(", mpo)
        self.assertIn("KeInsertQueueDpc(&Context->MpoCommitDpc", mpo)
        self.assertIn("AdmissionMpoCommitPendingSource(Context, ADMISSION_MPO_TIMING_COMMIT_LATCH)", scanout)
        self.assertIn("AdmissionMpoCommitPendingSource(Context, ADMISSION_MPO_TIMING_COMMIT_TICK)", scanout)
        self.assertIn("AdmissionMpoCommitDpcStop(Context)", scanout)
        self.assertIn("AdmissionMpoCommitDpcStart(Context)", scanout)
        # Set3 never submits a swap itself: the only commit-path call left in Set3 is ForcePlanesOff's plane-off.
        set3 = mpo[mpo.index("_Use_decl_annotations_ NTSTATUS AdmissionMpoSetVidPnSourceAddress3("):mpo.index("_Use_decl_annotations_ VOID AdmissionMpoCommitPending(")]
        self.assertEqual(set3.count("&completion, FALSE)"), 1)
        self.assertIn("Args->PlaneCount == 0u", set3[:set3.index("&completion, FALSE)")])
        project = (ROOT / "drivers/apple-agx/render-admission/AppleAgxRenderAdmission.vcxproj")
        if project.exists():
            self.assertIn("mpo_hold.c", project.read_text())


if __name__ == "__main__":
    unittest.main()
