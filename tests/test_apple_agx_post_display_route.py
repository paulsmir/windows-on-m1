"""The WDDM POST callback may succeed without a POST display mode."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LIFECYCLE = (ROOT / "drivers/apple-agx/render-admission/src/lifecycle.c").read_text()
DISPLAY = (ROOT / "drivers/apple-agx/render-admission/src/display.c").read_text()


class PostDisplayRouteTests(unittest.TestCase):
    def test_kmd_records_callback_and_decision_then_uses_own_scanout(self):
        start = LIFECYCLE.split("NTSTATUS AdmissionDdiStartDevice(", 1)[1]
        self.assertTrue("AppleAgxPostDisplayRoute(" in start)
        self.assertTrue("AdmissionRecordPostDisplay(" in start)
        self.assertTrue("AppleAgxPostDisplayOwnScanout" in start)
        self.assertLess(start.index("AppleAgxPostDisplayRoute("),
                        start.index("AdmissionScanoutStart(context)"))
        self.assertIn("if (context->PostDisplayInformation.Width == 0u)", DISPLAY)

    def test_real_c_route(self):
        with tempfile.TemporaryDirectory(prefix="post-display-route-") as tmp:
            bitcode = Path(tmp) / "route.bc"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-emit-llvm", "-c", "-I",
                str(ROOT / "drivers/apple-agx/shared/include"),
                str(ROOT / "tests/apple_agx_post_display_route_test.c"),
                "-o", str(bitcode),
            ], check=True)
            subprocess.run([os.environ.get("LLI", "lli"), str(bitcode)],
                           check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
