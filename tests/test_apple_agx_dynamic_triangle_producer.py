from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (
    ROOT
    / "drivers"
    / "apple-agx"
    / "windows"
    / "one-shot"
    / "apple_agx_dynamic_triangle.c"
)


class AppleAgxDynamicTriangleProducerTests(unittest.TestCase):
    def test_make_resident_fence_wait_is_bounded_and_fails_before_upload(self):
        source = SOURCE.read_text()
        make_resident = source.index("status = D3DKMTMakeResident(&resident)")
        deadline = source.index("pagingDeadline", make_resident)
        timeout = source.index("MakeResident paging fence timeout", deadline)
        first_upload = source.index("status = Upload(", timeout)

        self.assertIn("GetTickCount64() < pagingDeadline", source[deadline:timeout])
        self.assertIn("status = (NTSTATUS)0x00000102L", source[deadline:timeout])
        self.assertIn("goto Cleanup", source[timeout:first_upload])
        self.assertLess(make_resident, deadline)
        self.assertLess(deadline, timeout)
        self.assertLess(timeout, first_upload)


if __name__ == "__main__":
    unittest.main()
