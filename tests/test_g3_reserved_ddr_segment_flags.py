"""EXP837: the R64 firmware-reserved local segment must be described as such."""

from pathlib import Path
import re
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "drivers/apple-agx/render-admission/src/memory_windows.c"


def function_body(source, name):
    match = re.search(r"NTSTATUS\s+" + name + r"\s*\([^;]*?\)\s*\{", source, re.S)
    if match is None:
        raise AssertionError(f"missing production function {name}")
    depth, cursor = 1, match.end()
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[match.start():cursor]


class ReservedDdrSegmentFlags(unittest.TestCase):
    def test_g3_local_segment_reports_firmware_reserved_ddr(self):
        """DXGK_SEGMENTFLAGS forbids PopulatedFromSystemMemory for firmware-reserved
        memory; EXP837 bugchecked in VidMm on the first CPU-visible lock."""
        body = function_body(SOURCE.read_text(), "AdmissionDdiQuerySegment5")
        g3 = body[body.rindex("#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)"):]
        self.assertIn("SegmentDescriptors[1].Flags.PopulatedFromSystemMemory = 0u", g3)
        self.assertIn("SegmentDescriptors[1].Flags.PopulatedByReservedDDRByFirmware = 1u", g3)


if __name__ == "__main__":
    unittest.main()
