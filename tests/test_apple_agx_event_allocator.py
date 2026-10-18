import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxEventAllocatorTests(unittest.TestCase):
    def test_event_pair_lifetime_is_strict_and_sanitized(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "apple_agx_event_allocator_test"
            command = [
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_event_allocator_test.c"),
                str(SHARED / "src" / "apple_agx_event_allocator.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            completed = subprocess.run(
                [str(binary)], check=True, cwd=ROOT, capture_output=True, text=True
            )
            self.assertEqual(
                completed.stdout.strip(), "apple_agx_event_allocator_test: ok"
            )


if __name__ == "__main__":
    unittest.main()
