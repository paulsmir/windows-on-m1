from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderSubmissionTests(unittest.TestCase):
    def test_portable_exact_packet_lifetime(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "render_submission_test"
            subprocess.run([
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                "-I", str(ROOT / "drivers/apple-agx/shared/include"),
                str(RENDER / "tests" / "render_submission_test.c"),
                str(RENDER / "src" / "render_submission.c"),
                str(RENDER / "src" / "render_gdi_receipt.c"),
                str(RENDER / "src" / "render_dynamic_output.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_submit_queues_exact_prepared_packet_without_completion(self):
        paging = (RENDER / "src" / "paging_windows.c").read_text()
        submit = (RENDER / "src" / "submission_windows.c").read_text()
        scheduler = (RENDER / "src" / "scheduler_windows.c").read_text()
        callbacks = (RENDER / "src" / "callbacks.c").read_text()

        self.assertIn("AdmissionDdiSubmitRender", paging)
        self.assertIn("AppleAgxDmaShadowIsSealedForFence", submit)
        self.assertIn("AdmissionNonPagingPrivateRangeCovers", submit)
        self.assertIn("AdmissionGdiReceiptSubmitWindows", submit)
        self.assertLess(
            submit.rindex("AdmissionGdiReceiptSubmitWindows"),
            submit.index("AdmissionDispatchQueuedWork")
        )
        self.assertIn("AdmissionGdiDescribePreparedRecord", submit)
        self.assertIn("AppleAgxSchedulerQueueFence", submit)
        self.assertIn("AdmissionRenderPacketQueue", submit)
        self.assertNotIn("AppleAgxSchedulerActivateFence", submit)
        self.assertNotIn("AdmissionSchedulerRecordCompletion", submit)
        self.assertNotIn("AppleAgxBackend", submit)
        self.assertNotIn("FAIL2(AdmissionDdiCancelCommand", callbacks)
        self.assertIn("AdmissionRenderPacketDiscardQueued", scheduler)


if __name__ == "__main__":
    unittest.main()
