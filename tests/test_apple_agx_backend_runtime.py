from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BackendRuntimePackageTests(unittest.TestCase):
    def test_backend_runtime_uses_canonical_submission_contract(self):
        header = (
            ROOT
            / "drivers/apple-agx/shared/include/apple_agx_backend_runtime.h"
        ).read_text()

        self.assertIn('#include "apple_agx_submission.h"', header)
        self.assertIn("APPLE_AGX_SUBMISSION Submission;", header)
        self.assertNotIn("APPLE_AGX_BACKEND_SUBMISSION_QUEUE", header)

    def test_runtime_borrows_prepared_exp208_arena(self):
        header = (
            ROOT
            / "drivers/apple-agx/shared/include/apple_agx_backend_runtime.h"
        ).read_text()
        source = (
            ROOT
            / "drivers/apple-agx/shared/src/apple_agx_backend_runtime.c"
        ).read_text()

        self.assertIn("AcquirePrepared", header)
        self.assertIn("AcquirePrepared", source)
        self.assertNotIn("Image.Materialize", source)

    def test_backend_runtime_has_exact_terminal_completion_contract(self):
        header = (
            ROOT
            / "drivers/apple-agx/shared/include/apple_agx_backend_runtime.h"
        ).read_text()

        for status in (
            "AppleAgxBackendCompletionSuccess",
            "AppleAgxBackendCompletionFaulted",
            "AppleAgxBackendCompletionTimedOut",
            "AppleAgxBackendCompletionReset",
            "AppleAgxBackendCompletionCancelled",
        ):
            self.assertIn(status, header)
        self.assertIn("SubmissionFence", header)
        self.assertIn("NodeOrdinal", header)
        self.assertIn("EngineOrdinal", header)

    def test_backend_runtime_is_registered_in_wdk_project(self):
        project = (
            ROOT / "drivers/apple-agx/windows/AppleAgx.vcxproj"
        ).read_text()

        self.assertIn("apple_agx_backend_runtime.c", project)
        self.assertIn("apple_agx_backend_runtime.h", project)

    def test_backend_runtime_lifecycle_and_exact_fence(self):
        shared = ROOT / "drivers/apple-agx/shared"
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_agx_backend_runtime_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(shared / "include"),
                str(shared / "tests" / "apple_agx_backend_runtime_test.c"),
                str(shared / "src" / "apple_agx_backend_runtime.c"),
                str(shared / "src" / "apple_agx_submission.c"),
                str(shared / "src" / "apple_agx_firmware.c"),
                str(shared / "src" / "apple_agx_rtkit.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
