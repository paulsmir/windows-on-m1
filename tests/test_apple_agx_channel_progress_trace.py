from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class ChannelProgressTraceTests(unittest.TestCase):
    def test_late_channel_consumption_is_host_visible(self):
        guards = (RENDER / "include" / "render_submit_trace.h").read_text()
        header = (RENDER / "include" / "render_admission.h").read_text()
        trace = (RENDER / "src" / "submit_trace_windows.c").read_text()
        worker = (RENDER / "src" / "backend_platform_windows.c").read_text()

        self.assertIn("ADMISSION_BACKEND_CHANNEL_PROGRESS_TAG", guards)
        self.assertIn("AdmissionBackendChannelProgressWord", guards)
        self.assertIn("AdmissionBackendChannelProgressWindows", header)
        self.assertIn("AdmissionBackendChannelProgressWindows", trace)
        poll = worker.index("AppleAgxPlatformProviderPoll(")
        report = worker.index("AdmissionBackendChannelProgressWindows(", poll)
        self.assertGreater(report, poll)
        self.assertIn("channelProgressReported", worker)


if __name__ == "__main__":
    unittest.main()
