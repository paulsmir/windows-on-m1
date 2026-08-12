from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/build-standalone.sh"


class BuildStandaloneTests(unittest.TestCase):
    def test_no_argument_build_is_the_release_physical_off_profile(self):
        environment = dict(
            os.environ,
            BUILD_STANDALONE_DRY_RUN="1",
            STANDALONE_BUILD_CONTAINER="never",
        )
        result = subprocess.run(
            [str(SCRIPT)],
            cwd="/tmp",
            env=environment,
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("dist/j313/release/boot.bin", result.stdout)
        self.assertIn("--display physical --debug off", result.stdout)

    def test_dry_run_has_the_complete_location_independent_pipeline(self):
        environment = dict(os.environ, BUILD_STANDALONE_DRY_RUN="1")
        result = subprocess.run(
            [str(SCRIPT), "--release"],
            cwd="/tmp",
            env=environment,
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout
        expected_in_order = (
            "git submodule update --init --recursive",
            "stuart_build",
            "BLD_*_AIC_BUILD=FALSE",
            "temporary sibling for dist/j313/release",
            "-DM1N1_STAGE0",
            "dist/j313/release/m1n1-stage0.bin",
            "-DM1N1_STAGE1",
            "dist/j313/release/m1n1-stage1.bin",
            "tools/generate_guest_layout.py --check",
            "tools/pack_boot.py",
            "--stage0-m1n1 dist/j313/release/m1n1-stage0.bin",
            "--stage1-m1n1 dist/j313/release/m1n1-stage1.bin",
            "dist/j313/release/boot.bin",
            "--source-commit <m1n1-source-commit>",
            "--compiler <compiler-identity>",
            "parse_bootstrap",
            "parse_image",
            "SHA256SUMS",
            "MANIFEST.json",
            "publish complete profile atomically to dist/j313/release",
        )
        cursor = 0
        for item in expected_in_order:
            found = output.find(item, cursor)
            self.assertGreaterEqual(found, 0, f"missing or out-of-order command: {item}")
            cursor = found + len(item)
        self.assertNotIn("/Users/pavel", output)

    def test_check_python_rejects_an_incompatible_explicit_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            fake_python = Path(directory) / "python-incompatible"
            fake_python.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
            fake_python.chmod(0o755)
            environment = dict(os.environ, MU_PYTHON=str(fake_python))

            result = subprocess.run(
                [str(SCRIPT), "--check-python"],
                cwd="/tmp",
                env=environment,
                text=True,
                capture_output=True,
                check=False,
            )

        self.assertEqual(result.returncode, 2)
        self.assertIn("requires Python >=3.10 and <3.13", result.stderr)

    def test_check_python_accepts_a_compatible_explicit_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            fake_python = Path(directory) / "python-compatible"
            fake_python.write_text("#!/bin/sh\nprintf '3.12.9\\n'\n", encoding="utf-8")
            fake_python.chmod(0o755)
            environment = dict(os.environ, MU_PYTHON=str(fake_python))

            result = subprocess.run(
                [str(SCRIPT), "--check-python"],
                cwd="/tmp",
                env=environment,
                text=True,
                capture_output=True,
                check=False,
            )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(str(fake_python), result.stdout)
        self.assertIn("3.12.9", result.stdout)

    def test_forced_container_dry_run_precedes_the_inner_pipeline(self):
        environment = dict(
            os.environ,
            BUILD_STANDALONE_DRY_RUN="1",
            STANDALONE_BUILD_CONTAINER="always",
        )
        result = subprocess.run(
            [str(SCRIPT), "--release"],
            cwd="/tmp",
            env=environment,
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout
        expected_in_order = (
            "docker build",
            "Dockerfile.build",
            "docker run",
            "STANDALONE_IN_CONTAINER=1",
            "-v <git-worktree-root>:/work",
            "-w <container-repository-root>",
            "git submodule update --init --recursive",
            "stuart_build",
            "tools/pack_boot.py",
        )
        cursor = 0
        for item in expected_in_order:
            found = output.find(item, cursor)
            self.assertGreaterEqual(found, 0, f"missing or out-of-order command: {item}")
            cursor = found + len(item)

    def test_invalid_container_mode_is_rejected(self):
        environment = dict(
            os.environ,
            BUILD_STANDALONE_DRY_RUN="1",
            STANDALONE_BUILD_CONTAINER="sometimes",
        )
        result = subprocess.run(
            [str(SCRIPT)],
            cwd="/tmp",
            env=environment,
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("STANDALONE_BUILD_CONTAINER must be auto, always, or never", result.stderr)

    def test_dry_run_forwards_standalone_display_and_debug_profile_to_packer(self):
        environment = dict(
            os.environ,
            BUILD_STANDALONE_DRY_RUN="1",
            STANDALONE_BUILD_CONTAINER="never",
        )
        result = subprocess.run(
            [str(SCRIPT), "--debug-build", "--display", "both", "--debug", "full"],
            cwd="/tmp",
            env=environment,
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--display both --debug full", result.stdout)

    def test_dry_run_accepts_and_forwards_monitor_profile(self):
        environment = dict(
            os.environ,
            BUILD_STANDALONE_DRY_RUN="1",
            STANDALONE_BUILD_CONTAINER="never",
        )
        result = subprocess.run(
            [str(SCRIPT), "--debug-build", "--display", "physical", "--debug", "monitor"],
            cwd="/tmp",
            env=environment,
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--display physical --debug monitor", result.stdout)
        self.assertNotIn("--m1n1 m1n1_windows/build/m1n1.bin", result.stdout)


if __name__ == "__main__":
    unittest.main()
