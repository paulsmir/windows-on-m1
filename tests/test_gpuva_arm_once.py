"""The qualification arm must be durable and single use before GPU access."""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "drivers/apple-agx/render-admission/src/lifecycle.c").read_text()


class GpuvaArmOnceTests(unittest.TestCase):
    def test_both_profiles_consume_arm_before_runtime_start(self):
        start = SOURCE.split("NTSTATUS AdmissionDdiStartDevice(", 1)[1]
        first_gpu = start.index("AdmissionInterruptStart(context)")
        gate = start[:first_gpu]
        self.assertTrue('AdmissionConsumeGpuvaArm(context, L"B1Armed")' in gate,
                        "B1 arm is not consumed before runtime start")
        self.assertTrue('AdmissionConsumeGpuvaArm(context, L"G3Armed")' in gate,
                        "G3 arm is not consumed before runtime start")

    def test_valid_arm_is_deleted_and_flushed_before_success(self):
        self.assertTrue("static BOOLEAN AdmissionConsumeGpuvaArm(" in SOURCE,
                        "single-use arm helper is missing")
        helper = SOURCE.split("static BOOLEAN AdmissionConsumeGpuvaArm(", 1)[1].split("\n#endif", 1)[0]
        self.assertIn("KEY_QUERY_VALUE | KEY_SET_VALUE", helper)
        self.assertLess(helper.index("ZwQueryValueKey"), helper.index("ZwDeleteValueKey"))
        self.assertLess(helper.index("ZwDeleteValueKey"), helper.index("ZwFlushKey"))
        self.assertLess(helper.index("ZwFlushKey"), helper.index("return NT_SUCCESS(status)"))
        self.assertIn("value->Type != REG_DWORD", helper)
        self.assertIn("*(ULONG *)value->Data != 1u", helper)


if __name__ == "__main__":
    unittest.main()
