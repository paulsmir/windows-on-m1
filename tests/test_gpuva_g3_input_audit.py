"""WDK 26100 optional inputs must not become G3 admission vetoes."""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/render-admission/src"


class G3InputAuditTests(unittest.TestCase):
    def test_create_device_accepts_os_pasid(self):
        body = (SRC / "callbacks.c").read_text().split(
            "NTSTATUS AdmissionDdiCreateDevice(", 1)[1].split(
            "NTSTATUS AdmissionDdiDestroyDevice(", 1)[0]
        g3 = body.split("#else", 1)[1].split("#endif", 1)[0]
        self.assertNotIn("Args->Pasid != 0", g3)
        self.assertIn("AdmissionGpuvaG3AttachDevice", body)
        self.assertIn("AdmissionRecordGpuvaG3DeviceInput", body)

    def test_update_accepts_driver_protection_metadata(self):
        body = (SRC / "gpuva_g3_paging_windows.c").read_text().split(
            "NTSTATUS AdmissionGpuvaG3BuildPagingBuffer(", 1)[1]
        self.assertNotIn("update->DriverProtection != 0ULL", body)
        self.assertIn("DriverProtection", (SRC / "receipts.c").read_text())

    def test_update_accepts_eviction_notification(self):
        body = (SRC / "gpuva_g3_paging_windows.c").read_text().split(
            "NTSTATUS AdmissionGpuvaG3BuildPagingBuffer(", 1)[1]
        self.assertNotIn("update->Flags.NotifyEviction ||", body)

    def test_g3_context_accepts_wddm32_test_context(self):
        header = (ROOT / "drivers/apple-agx/render-admission/include/render_objects.h").read_text()
        callback = (SRC / "callbacks.c").read_text()
        self.assertIn("ADMISSION_CONTEXT_TEST", header)
        self.assertIn("ADMISSION_CONTEXT_TEST)", header)
        self.assertIn("Args->Flags.TestContext", callback)


if __name__ == "__main__":
    unittest.main()
