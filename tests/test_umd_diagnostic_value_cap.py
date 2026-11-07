"""EXP2202: AdmissionUmdDiagnostic silently drops a record with more values
than its cap, so measure-d3d12-ov2-refusal (24, then 26 values) vanished from
the hardware log while EXP2200's 12-value row was present.  Every literal
call site must stay within the cap, and the cap must fit the line buffer."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WRITER = ROOT / "drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c"
CALL = re.compile(r'AdmissionUmdDiagnostic\(\s*"([A-Za-z0-9_-]+)"\s*,[^;]*?,\s*(\d+)u?\s*\)\s*;', re.S)


class UmdDiagnosticValueCap(unittest.TestCase):
    def writer(self):
        src = WRITER.read_text(encoding="utf-8", errors="replace")
        body = src[src.index("VOID AdmissionUmdDiagnostic(PCSTR Stage"):]
        cap = int(re.search(r"Count > (\d+)u", body).group(1))
        line = int(re.search(r"char line\[(\d+)\];", body).group(1))
        return cap, line

    def test_every_literal_call_fits_the_cap(self):
        cap, _ = self.writer()
        over = []
        for path in (ROOT / "drivers/apple-agx").rglob("*"):
            if path.suffix not in {".c", ".cpp", ".h"} or "/tests/" in path.as_posix():
                continue
            for stage, count in CALL.findall(path.read_text(encoding="utf-8", errors="replace")):
                if int(count) > cap:
                    over.append(f"{path.relative_to(ROOT)}: {stage} {count} > {cap}")
        self.assertEqual(over, [])

    def test_cap_fits_the_line_buffer(self):
        cap, line = self.writer()
        # stage (<= 64) + " hr=0x........ pid=.......... tid=.......... session=..........
        # qpc=...................." (<= 96) + 9 characters per value + NUL
        self.assertLess(64 + 96 + 9 * cap + 1, line)


if __name__ == "__main__":
    unittest.main()
