"""G3 StartDevice must reject an ordinary resource set before arm or MMIO."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
LIFECYCLE = ROOT / "drivers/apple-agx/render-admission/src/lifecycle.c"


class G3StartResourceGate(unittest.TestCase):
    def test_resource_gate_precedes_arm_and_gpu_access(self):
        source = LIFECYCLE.read_text()
        start = source.index("NTSTATUS AdmissionDdiStartDevice(")
        end = source.index("NTSTATUS AdmissionDdiStopDevice(", start)
        body = source[start:end]
        gate = body.index("AdmissionG3FirmwareResourcesPresent(context)")
        self.assertLess(body.index("DxgkCbGetDeviceInformation"), gate)
        self.assertLess(gate, body.index('AdmissionConsumeGpuvaArm(context, L"G3Armed"'))
        self.assertLess(gate, body.index("AdmissionInterruptStart(context)"))
        self.assertIn("STATUS_DEVICE_NOT_READY", body[gate:body.index('AdmissionConsumeGpuvaArm(context, L"G3Armed"')])


if __name__ == "__main__":
    unittest.main()
