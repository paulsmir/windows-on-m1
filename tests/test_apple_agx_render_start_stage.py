from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderStartStageTests(unittest.TestCase):
    def test_start_stages_are_distinct_and_not_overwritten_by_teardown(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        lifecycle = (RENDER / "src" / "lifecycle.c").read_text()
        receipts = (RENDER / "src" / "receipts.c").read_text()

        for stage in (
            "AdmissionStartEntered",
            "AdmissionStartDeviceInfo",
            "AdmissionStartInterrupt",
            "AdmissionStartMemory",
            "AdmissionStartBackendImage",
            "AdmissionStartScheduler",
            "AdmissionStartPaging",
            "AdmissionStartPlatform",
            "AdmissionStartPostDisplay",
            "AdmissionStartScanout",
            "AdmissionStartObjects",
            "AdmissionStartComplete",
        ):
            self.assertIn(stage, header)
            self.assertIn(f"AdmissionRecordStartStage(context, {stage}",
                          lifecycle)
        self.assertIn('L"Wom1StartStage"', receipts)
        self.assertIn('L"Wom1StartStatus"', receipts)

        stop = lifecycle[lifecycle.index("AdmissionDdiStopDevice("):
                         lifecycle.index("AdmissionDdiRemoveDevice(")]
        remove = lifecycle[lifecycle.index("AdmissionDdiRemoveDevice("):
                           lifecycle.index("AdmissionDdiQueryAdapterInfo(")]
        self.assertNotIn("AdmissionRecordStartStage", stop)
        self.assertNotIn("AdmissionRecordStartStage", remove)


if __name__ == "__main__":
    unittest.main()
