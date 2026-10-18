"""Phase 5a: nothing but the pending-head binder may prepare the render slot
while G4 submissions are pending.

The scheduler dispatches fences in submission order.  A packet prepared into
the empty slot while an older G4 submission is pending holds the slot, and
the pending head (the scheduler's queued fence) can then never bind: the
engine stops until a TDR.  gdi_windows.c prepared GDI packets whenever the
slot was empty; it must report busy until the pending queue drains.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/render-admission/src"


def enclosing_function(text, pos):
    starts = [m.start() for m in re.finditer(r"(?m)^[A-Za-z_][^\n;]*\([^;{]*\)\s*\{", text) if m.start() < pos]
    return text[starts[-1]:pos] if starts else text[:pos]


class RenderSlotRespectsPending(unittest.TestCase):
    def test_every_slot_prepare_checks_the_pending_queue(self):
        offenders = []
        for path in sorted(SRC.glob("*.c")):
            if path.name == "render_submission.c":
                continue
            text = path.read_text()
            for m in re.finditer(r"AdmissionRenderPacketPrepare\(", text):
                head = enclosing_function(text, m.start())
                if "AdmissionG4PendingBindHead" in head.split("(")[0]:
                    continue
                window = text[max(0, m.start() - 700):m.start()]
                if "G4PendingCount == 0u" not in window:
                    offenders.append(f"{path.name}:{text.count(chr(10), 0, m.start()) + 1}")
        self.assertEqual(offenders, [], "slot prepared without checking pending G4 entries")


if __name__ == "__main__":
    unittest.main()
