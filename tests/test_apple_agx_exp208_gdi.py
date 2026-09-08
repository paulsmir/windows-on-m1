from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxExp208GdiTests(unittest.TestCase):
    def test_exact_hardware_clear_binding(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_agx_exp208_gdi_test"
            subprocess.run([
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_exp208_gdi_test.c"),
                str(SHARED / "src" / "apple_agx_exp208_gdi.c"),
                str(SHARED / "src" / "apple_agx_exp208_framebuffer.c"),
                str(SHARED / "src" / "apple_agx_gdi.c"),
                str(SHARED / "src" / "apple_agx_memory.c"),
                str(SHARED / "src" / "apple_agx_render_template.generated.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
