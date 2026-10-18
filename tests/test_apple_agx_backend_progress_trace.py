from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"

class BackendProgressTraceTests(unittest.TestCase):
    def test_first_real_progress_owns_success_trace_slot(self):
        guards = (RENDER / "include" / "render_submit_trace.h").read_text()
        header = (RENDER / "include" / "render_admission.h").read_text()
        trace = (RENDER / "src" / "submit_trace_windows.c").read_text()
        worker = (RENDER / "src" / "backend_platform_windows.c").read_text()
        self.assertIn("ADMISSION_BACKEND_PROGRESS_TAG", guards)
        self.assertIn("AdmissionBackendProgressWindows", header)
        self.assertIn("AdmissionBackendProgressWindows", trace)
        self.assertIn("AppleAgxG13QueueProgressHasAdvanced", worker)
        self.assertIn("AdmissionBackendProgressWindows(adapter, &current)", worker)
        self.assertIn("result != AppleAgxBackendRuntimeResultOk", worker)

if __name__ == "__main__":
    unittest.main()
