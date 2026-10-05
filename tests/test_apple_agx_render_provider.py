import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxRenderProviderTests(unittest.TestCase):
    def test_render_provider_suite(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_agx_render_provider_test"
            command = [
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_render_provider_test.c"),
                str(SHARED / "src" / "apple_agx_render_provider.c"),
                str(SHARED / "src" / "apple_agx_exp208_adapter.c"),
                str(SHARED / "src" / "apple_agx_gdi.c"),
                str(SHARED / "src" / "apple_agx_memory.c"),
                str(SHARED / "src" / "apple_agx_uat.c"),
                str(SHARED / "src" / "apple_agx_uat_publication.c"),
                str(SHARED / "src" / "apple_agx_render_template.generated.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, cwd=ROOT, check=True)
            subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
