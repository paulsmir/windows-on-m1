"""EXP1211: every backend queue operation is wired through every layer.

What it catches: the KMD's backend calls APPLE_AGX_BACKEND_IO.Queues.<op>
through a chain of re-composed IO tables -- the G13 queue provider installs
them, the platform provider wraps them (its own context), the composer
wraps those, and the KMD runtime delegates to the composed table. EXP1211
bugchecked (0x116) because RunCompute was added to the backend and the queue
provider but not to the three outer tables: the backend saw NULL, refused
the compute fence in the worker, and the scheduler fault became a TDR whose
reset failed. A member left unassigned in any layer is the same defect.
"""
from pathlib import Path
import os
import re
import unittest

ROOT = Path(os.environ.get("APPLE_AGX_WIRING_ROOT", Path(__file__).resolve().parents[1]))
HEADER = ROOT / "drivers/apple-agx/shared/include/apple_agx_backend_runtime.h"
LAYERS = {
    "queue provider": (ROOT / "drivers/apple-agx/shared/src/apple_agx_g13_queue_provider.c",
                       r"Io->Queues\.{member}\s*="),
    "platform provider": (ROOT / "drivers/apple-agx/shared/src/apple_agx_platform_provider.c",
                          r"queue_backend_io\.Queues\.{member}\s*="),
    "composer": (ROOT / "drivers/apple-agx/shared/src/apple_agx_platform_composer.c",
                 r"Io->Queues\.{member}\s*="),
    "KMD runtime": (ROOT / "drivers/apple-agx/render-admission/src/backend_platform_windows.c",
                    r"runtime->RuntimeIo\.Queues\.{member}\s*="),
}


def queue_members():
    text = HEADER.read_text()
    body = text[:text.index("} Queues;")]
    body = body[body.rindex("struct {"):]
    return re.findall(r"\(\*(\w+)\)\(", body)


class BackendIoQueueWiring(unittest.TestCase):
    def test_header_lists_the_queue_operations(self):
        members = queue_members()
        for member in ("Create", "Destroy", "Run3d", "RunTa", "RunCompute", "Stop", "Reset"):
            self.assertIn(member, members)

    def test_every_layer_assigns_every_queue_operation(self):
        missing = []
        for layer, (path, pattern) in LAYERS.items():
            text = path.read_text()
            for member in queue_members():
                if not re.search(pattern.format(member=member), text):
                    missing.append(f"{layer}: {member}")
        self.assertEqual([], missing)

    def test_platform_provider_rewraps_after_retargeting_the_context(self):
        # The provider copies the queue provider's table, then points its
        # context at itself: every operation must be re-assigned after that,
        # or it would run a queue-provider function with the platform context.
        text = LAYERS["platform provider"][0].read_text()
        retarget = text.index("queue_backend_io.Context = Provider;")
        for member in queue_members():
            assignment = re.search(r"queue_backend_io\.Queues\.%s\s*=" % member, text)
            self.assertIsNotNone(assignment, member)
            self.assertGreater(assignment.start(), retarget, member)


if __name__ == "__main__":
    unittest.main()
