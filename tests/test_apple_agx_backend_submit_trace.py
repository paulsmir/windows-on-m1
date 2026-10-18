from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"

class BackendSubmitTraceTests(unittest.TestCase):
    def test_worker_emits_exact_backend_submit_result(self):
        guards = (RENDER / "include" / "render_submit_trace.h").read_text()
        header = (RENDER / "include" / "render_admission.h").read_text()
        trace = (RENDER / "src" / "submit_trace_windows.c").read_text()
        worker = (RENDER / "src" / "backend_platform_windows.c").read_text()
        self.assertIn("ADMISSION_BACKEND_SUBMIT_RESULT_TAG", guards)
        self.assertIn("AdmissionBackendSubmitResultWindows", header)
        self.assertIn("AdmissionBackendSubmitResultWindows", trace)
        self.assertIn("AdmissionBackendSubmitResultWindows(", worker)
        self.assertIn("runtime->Backend.Phase", worker)

if __name__ == "__main__":
    unittest.main()
