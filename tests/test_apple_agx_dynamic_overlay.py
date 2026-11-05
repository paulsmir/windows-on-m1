from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx/render-admission"
SHARED = ROOT / "drivers/apple-agx/shared"


class AppleAgxDynamicOverlayTests(unittest.TestCase):
    def test_reserved_overlay_is_bounded_atomic_and_reversible(self):
        """Catches overwrite of EXP208 fixed inputs or stale per-fence state."""
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "render_dynamic_overlay_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(DRIVER / "include"),
                "-I", str(SHARED / "include"),
                str(DRIVER / "tests/render_dynamic_overlay_test.c"),
                str(DRIVER / "src/render_dynamic_overlay.c"),
                str(SHARED / "src/apple_agx_render_template.generated.c"),
                "-o", str(binary),
            ], cwd=ROOT, check=True)
            subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
