import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AppleAgxSubmissionCoordinatorTest(unittest.TestCase):
    def test_submission_coordinator_contract(self):
        sources = [
            "apple_agx_submission_coordinator.c",
            "apple_agx_exp208_dynamic.c",
            "apple_agx_render_provider.c",
            "apple_agx_render_shared_memory.c",
            "apple_agx_relocation.c",
            "apple_agx_render_template.generated.c",
            "apple_agx_exp208_adapter.c",
            "apple_agx_g13_queue_provider.c",
            "apple_agx_g13_queue_runtime.c",
            "apple_agx_g13_codec.c",
            "apple_agx_gdi.c",
            "apple_agx_memory.c",
            "apple_agx_uat.c",
            "apple_agx_uat_publication.c",
            "apple_agx_gfx_handoff.c",
        ]
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "apple_agx_submission_coordinator_test"
            subprocess.run(
                [
                    "clang",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pedantic",
                    "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer",
                    "-Idrivers/apple-agx/shared/include",
                    *[f"drivers/apple-agx/shared/src/{name}" for name in sources],
                    "drivers/apple-agx/shared/tests/apple_agx_submission_coordinator_test.c",
                    "-o",
                    str(output),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run([str(output)], cwd=ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
