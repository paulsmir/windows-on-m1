"""M2-f4b: every reported segment carries exactly one budget group.

VidMm refuses an allocation whose segment set mixes budgeted and
non-budgeted segments (dxgmms2 VerifyBudgetGroups); this driver's segment
sets span every reported segment, so a describe path without a group would
refuse allocations."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/render-admission/src/memory_windows.c"


class SegmentBudgetGroupTest(unittest.TestCase):
    def test_every_describe_path_sets_a_group(self):
        text = SOURCE.read_text()
        helper = re.search(r"static VOID AdmissionSegmentBudgetGroup\(.*?\n}\n", text, re.S).group(0)
        self.assertIn("LocalBudgetGroup = Segment->Aperture ? 0u : 1u", helper)
        self.assertIn("NonLocalBudgetGroup = Segment->Aperture ? 1u : 0u", helper)
        for name in ("AdmissionDescribeSegment(", "AdmissionDescribeSegment5("):
            body = re.search(r"static VOID " + re.escape(name) + r".*?\n}\n", text, re.S).group(0)
            self.assertIn("AdmissionSegmentBudgetGroup(Segment, &Descriptor->Flags);", body, name)


if __name__ == "__main__":
    unittest.main()
