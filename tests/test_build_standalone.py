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
        self.assertIn("dist/j313/debug-monitor", result.stdout)
        self.assertNotIn("--m1n1 m1n1_windows/build/m1n1.bin", result.stdout)

    def test_each_debug_wire_mode_has_a_distinct_artifact_directory(self):
        environment = dict(
            os.environ,
            BUILD_STANDALONE_DRY_RUN="1",
            STANDALONE_BUILD_CONTAINER="never",
        )
        expected = {
            "off": "debug-off",
            "uart": "debug-uart",
            "full": "debug-forensic",
            "monitor": "debug-monitor",
        }
        outputs = {}
        for mode, directory in expected.items():
            result = subprocess.run(
                [str(SCRIPT), "--debug-build", "--display", "physical",
                 "--debug", mode],
                env=environment, text=True, capture_output=True, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(f"dist/j313/{directory}", result.stdout)
            outputs[mode] = result.stdout
        self.assertEqual(len(set(outputs.values())), 4)

    def test_dirty_provenance_is_enabled_only_for_debug_builds(self):
        environment = dict(
            os.environ,
            BUILD_STANDALONE_DRY_RUN="1",
            STANDALONE_BUILD_CONTAINER="never",
        )
        debug = subprocess.run(
            [str(SCRIPT), "--debug-build", "--display", "physical", "--debug", "full"],
            env=environment, text=True, capture_output=True, check=False,
        )
        release = subprocess.run(
            [str(SCRIPT), "--release"],
            env=environment, text=True, capture_output=True, check=False,
        )
        self.assertEqual(debug.returncode, 0, debug.stderr)
        self.assertEqual(release.returncode, 0, release.stderr)
        self.assertIn("artifact_manifest.py create --allow-dirty", debug.stdout)
        self.assertNotIn("--allow-dirty", release.stdout)

    def test_full_debug_enables_wfx_invariant_in_both_m1n1_stages_only(self):
        environment = dict(
            os.environ,
            BUILD_STANDALONE_DRY_RUN="1",
            STANDALONE_BUILD_CONTAINER="never",
        )
        full = subprocess.run(
            [str(SCRIPT), "--debug-build", "--display", "physical", "--debug", "full"],
            env=environment, text=True, capture_output=True, check=False,
        )
        release = subprocess.run(
            [str(SCRIPT), "--release"],
            env=environment, text=True, capture_output=True, check=False,
        )
        self.assertEqual(full.returncode, 0, full.stderr)
        self.assertEqual(full.stdout.count("DIAG_TRAP_WFX=1"), 3)
        self.assertEqual(full.stdout.count("RUNTIME_DIAG_VERBOSE=1"), 3)
        self.assertNotIn("DIAG_TRAP_WFX=1", release.stdout)
        self.assertNotIn("RUNTIME_DIAG_VERBOSE=1", release.stdout)

    def test_debug_build_can_disable_apple_input_passthrough_for_ab_testing(self):
        environment = dict(
            os.environ,
            BUILD_STANDALONE_DRY_RUN="1",
            STANDALONE_BUILD_CONTAINER="never",
        )
        disabled = subprocess.run(
            [str(SCRIPT), "--debug-build", "--display", "physical", "--debug", "monitor",
             "--apple-input", "off"],
            env=environment, text=True, capture_output=True, check=False,
        )
        enabled = subprocess.run(
            [str(SCRIPT), "--debug-build", "--display", "physical", "--debug", "monitor"],
            env=environment, text=True, capture_output=True, check=False,
        )

        self.assertEqual(disabled.returncode, 0, disabled.stderr)
        self.assertEqual(disabled.stdout.count("APPLE_INPUT=0"), 3)
        self.assertEqual(enabled.returncode, 0, enabled.stderr)
        self.assertNotIn("APPLE_INPUT=0", enabled.stdout)

    def test_pipeline_builds_a_distinct_plain_chainload_image_after_stages(self):
        environment = dict(
            os.environ,
            BUILD_STANDALONE_DRY_RUN="1",
            STANDALONE_BUILD_CONTAINER="never",
        )
        result = subprocess.run(
            [str(SCRIPT), "--debug-build", "--display", "physical", "--debug", "full"],
            env=environment, text=True, capture_output=True, check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout
        stage1 = output.index("EXTRA_CFLAGS=-DM1N1_STAGE1")
        plain = output.index("build plain chainload m1n1.macho", stage1)
        copied = output.index("copy plain m1n1.macho", plain)
        self.assertLess(stage1, plain)
        self.assertLess(plain, copied)


if __name__ == "__main__":
    unittest.main()
