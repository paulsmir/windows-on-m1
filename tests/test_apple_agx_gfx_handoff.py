from pathlib import Path
import os, subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"

class AppleAgxGfxHandoffTests(unittest.TestCase):
    def test_handoff_lock_suite(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "handoff"
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11",
                "-Wall", "-Wextra", "-Werror", "-pedantic",
                "-fsanitize=address,undefined", "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_gfx_handoff_test.c"),
                str(SHARED / "src" / "apple_agx_gfx_handoff.c"), "-o", str(binary)],
                check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

if __name__ == "__main__": unittest.main()
