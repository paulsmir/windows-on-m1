import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxPlatformProviderHostTest(unittest.TestCase):
    def test_exact_platform_channel_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "platform_provider_test"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer",
                    "-I",
                    str(SHARED / "include"),
                    str(SHARED / "src" / "apple_agx_event_allocator.c"),
                    str(SHARED / "src" / "apple_agx_platform_provider.c"),
                    str(SHARED / "src" / "apple_agx_render_shared_memory.c"),
                    str(SHARED / "src" / "apple_agx_render_template.generated.c"),
                    str(SHARED / "src" / "apple_agx_memory.c"),
                    str(SHARED / "tests" / "apple_agx_platform_provider_test.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                cwd=ROOT,
            )
            completed = subprocess.run(
                [str(executable)], check=True, capture_output=True, text=True, cwd=ROOT
            )
            self.assertIn("apple_agx_platform_provider_test: ok", completed.stdout)


if __name__ == "__main__":
    unittest.main()
