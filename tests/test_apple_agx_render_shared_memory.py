from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxRenderSharedMemoryTests(unittest.TestCase):
    def test_firmware_visible_render_objects(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_agx_render_shared_memory_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_render_shared_memory_test.c"),
                str(SHARED / "src" / "apple_agx_render_shared_memory.c"),
                str(SHARED / "src" / "apple_agx_render_template.generated.c"),
                str(SHARED / "src" / "apple_agx_render_template_rebase.c"),
                str(SHARED / "src" / "apple_agx_exp208_adapter.c"),
                str(SHARED / "src" / "apple_agx_exp208_gdi.c"),
                str(SHARED / "src" / "apple_agx_exp208_framebuffer.c"),
                str(SHARED / "src" / "apple_agx_gdi.c"),
                str(SHARED / "src" / "apple_agx_relocation.c"),
                str(SHARED / "src" / "apple_agx_memory.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
