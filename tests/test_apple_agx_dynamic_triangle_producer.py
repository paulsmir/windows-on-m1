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

    def test_presentation_timeout_never_advances_to_hold_or_retirement(self):
        source = SOURCE.read_text()
        first_query = source.index("status = QueryPresentation(")
        hold = source.index("Sleep(15000u)", first_query)
        second_query = source.index("status = QueryPresentation(", hold)
        retirement = source.index("retirement.Magic", second_query)

        self.assertIn(
            "if (status != (NTSTATUS)0)\n    goto Preserve;",
            source[first_query:hold],
        )
        self.assertIn(
            "if (status != (NTSTATUS)0 ||",
            source[second_query:retirement],
        )


if __name__ == "__main__":
    unittest.main()
