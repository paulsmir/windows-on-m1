import os
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


def test_visible_pattern_is_exact_and_frame_identifiable():
    with tempfile.TemporaryDirectory() as directory:
        binary = Path(directory) / "render_visible_scanout_test"
        subprocess.run([
            os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
            "-Werror", "-fsanitize=address,undefined",
            "-I", str(RENDER / "include"), "-I", str(SHARED / "include"),
            str(RENDER / "tests" / "render_visible_scanout_test.c"),
            str(RENDER / "src" / "render_visible_scanout.c"),
            "-o", str(binary),
        ], check=True, cwd=ROOT)
        subprocess.run([str(binary)], check=True, cwd=ROOT)
