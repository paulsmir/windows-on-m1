"""Dxgkrnl may supply a PASID array when creating its system process."""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "drivers/apple-agx/render-admission/src/gpuva_g3_windows.c").read_text()


class G3CreateProcessContractTests(unittest.TestCase):
    def test_system_process_input_is_not_rejected_for_pasid_count(self):
        body = SOURCE.split("NTSTATUS AdmissionDdiCreateProcess(", 1)[1].split(
            "NTSTATUS AdmissionDdiDestroyProcess(", 1)[0]
        self.assertFalse("Args->NumPasid != 0u" in body,
                         "NumPasid is an OS input array count, not an admission veto")
        self.assertTrue("AdmissionRecordGpuvaG3CreateInput(" in body,
                        "first system-process input is not observable")
        self.assertTrue("Args->hKmdProcess = process" in body)
        self.assertTrue("Args->Flags.SystemProcess" in body)


if __name__ == "__main__":
    unittest.main()
