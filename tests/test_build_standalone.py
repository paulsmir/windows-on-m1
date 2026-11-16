from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/build-standalone.sh"
G2_WORKFLOW = ROOT / ".github" / "workflows" / "j313-agx-g2-acpi.yml"


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

    def test_release_gpu_visible_recovery_is_a_distinct_inert_profile(self):
        environment = dict(
            os.environ,
            BUILD_STANDALONE_DRY_RUN="1",
            STANDALONE_BUILD_CONTAINER="never",
        )
        result = subprocess.run(
            [str(SCRIPT), "--release", "--agx-g2-profile"],
            cwd="/tmp",
            env=environment,
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("dist/j313/release-agx-g2/boot.bin", result.stdout)
        self.assertIn("BLD_*_J313_AGX_G2_PROFILE=TRUE", result.stdout)
        self.assertIn("--display physical --debug off", result.stdout)
        self.assertIn("--capability agx-g2", result.stdout)
        self.assertNotIn("RUNTIME_DIAG_VERBOSE=1", result.stdout)

    def test_official_ci_publishes_the_complete_release_gpu_visible_recovery(self):
        workflow = G2_WORKFLOW.read_text()
        self.assertIn("build-release-gpu-visible-recovery:", workflow)
        self.assertIn("docker build -t windows-on-m1-build:ci", workflow)
        for source in (workflow, SCRIPT.read_text()):
            self.assertIn("GIT_CONFIG_COUNT=1", source)
            self.assertIn("GIT_CONFIG_KEY_0=safe.directory", source)
            self.assertIn("GIT_CONFIG_VALUE_0=*", source)
        self.assertIn(
            "scripts/build-standalone.sh --release --agx-g2-profile",
            workflow,
        )
        self.assertIn("tools/artifact_manifest.py verify " + "\\", workflow)
        self.assertIn("dist/j313/release-agx-g2/MANIFEST.json", workflow)
        self.assertNotIn("--manifest dist/j313/release-agx-g2", workflow)
        self.assertIn("name: J313-Release-AGX-G2-Recovery", workflow)
        for artifact in (
            "boot.bin",
            "J313_EFI.fd",
            "m1n1.macho",
            "MANIFEST.json",
            "SHA256SUMS",
        ):
            self.assertIn(f"dist/j313/release-agx-g2/{artifact}", workflow)

    def _working(self, *extra, env=None):
        environment = dict(os.environ, BUILD_STANDALONE_DRY_RUN="1",
                           STANDALONE_BUILD_CONTAINER="always", **(env or {}))
        return subprocess.run(
            [str(SCRIPT), "--working-profile", *extra],
            cwd="/tmp", env=environment, text=True, capture_output=True, check=False)

    BENCH_MU_SHA256 = "d9fe3a3483663f355e341c79f52f5a7a6eca25d6e726b9bce9138552765e8238"

    def test_working_profile_packs_the_pinned_bench_fd_with_bench_m1n1_flags(self):
        result = self._working("--mu-fd", "/bench/J313_EFI-mucpc001.fd",
                               "--mu-fd-sha256", self.BENCH_MU_SHA256)
        self.assertEqual(result.returncode, 0, result.stderr)
        out = result.stdout
        # No Mu rebuild and no container: the hardware-validated FD is packed as is.
        self.assertNotIn("stuart_build", out)
        self.assertNotIn("docker", out)
        self.assertIn(f"verify sha256(<mu-fd>) = {self.BENCH_MU_SHA256}", out)
        self.assertIn("dist/j313/working/boot.bin", out)
        flags = ("AGX_LOCAL_RESERVE_V2=1 IOMFB_FULL_OWNER=1 AGX_BROKER_LOCK_SPLIT=1 "
                 "AGX_SCANOUT_TWO_LAYERS=1 CPPC_CAPACITY_SCALE=1")
        for stage in ("EXTRA_CFLAGS=-DM1N1_STAGE0", "EXTRA_CFLAGS=-DM1N1_STAGE1"):
            self.assertIn(f"{flags} {stage}", out)
        self.assertNotIn("RELEASE=1", out)
        self.assertIn("--working-profile --firmware-sha256 " + self.BENCH_MU_SHA256, out)
        self.assertIn("--firmware <mu-fd> --layout config/j313-guest-layout.json", out)
        for capability in ("working-profile", "ans-direct", "agx-g2", "cppc", "expanded-ram"):
            self.assertIn(f"--capability {capability}", out)

    def test_working_profile_debug_variant_has_its_own_artifact_directory(self):
        result = self._working("--mu-fd", "/bench/fd", "--mu-fd-sha256", self.BENCH_MU_SHA256,
                               "--debug", "monitor")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("dist/j313/working-monitor/boot.bin", result.stdout)
        self.assertIn("--debug monitor --working-profile", result.stdout)

    def test_working_profile_requires_a_pinned_fd(self):
        for extra in ((), ("--mu-fd", "/bench/fd"),
                      ("--mu-fd", "/bench/fd", "--mu-fd-sha256", "abc"),
                      ("--mu-fd", "/bench/fd", "--mu-fd-sha256", "G" * 64)):
            with self.subTest(extra=extra):
                result = self._working(*extra)
                self.assertEqual(result.returncode, 2)
                self.assertIn("--mu-fd", result.stderr)

    def test_working_profile_excludes_the_inert_gpu_recovery_profile(self):
        result = self._working("--agx-g2-profile", "--mu-fd", "/bench/fd",
                               "--mu-fd-sha256", self.BENCH_MU_SHA256)
        self.assertEqual(result.returncode, 2)
        self.assertIn("exclusive", result.stderr)

    def test_default_profiles_carry_no_working_profile_flags(self):
        environment = dict(os.environ, BUILD_STANDALONE_DRY_RUN="1",
                           STANDALONE_BUILD_CONTAINER="never")
        result = subprocess.run([str(SCRIPT)], cwd="/tmp", env=environment, text=True,
                                capture_output=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("--working-profile", result.stdout)
        self.assertNotIn("CPPC_CAPACITY_SCALE", result.stdout)
        self.assertIn("stuart_build", result.stdout)

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

    def test_debug_build_can_preserve_explicit_top_level_component_revisions(self):
        environment = dict(
            os.environ,
            BUILD_STANDALONE_DRY_RUN="1",
            STANDALONE_BUILD_CONTAINER="never",
            STANDALONE_PRESERVE_COMPONENTS="1",
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
        self.assertIn("git -C mu submodule update --init --recursive", result.stdout)
        self.assertIn("git -C m1n1_windows submodule update --init --recursive", result.stdout)
        self.assertNotIn("\ngit submodule update --init --recursive\n", result.stdout)

    def test_release_rejects_preserving_unpinned_component_revisions(self):
        environment = dict(
            os.environ,
            BUILD_STANDALONE_DRY_RUN="1",
            STANDALONE_BUILD_CONTAINER="never",
            STANDALONE_PRESERVE_COMPONENTS="1",
        )
        result = subprocess.run(
            [str(SCRIPT), "--release"],
            cwd="/tmp",
            env=environment,
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("requires --debug-build", result.stderr)

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

    def test_wfx_policy_is_not_an_optional_debug_build_flag(self):
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
        self.assertNotIn("DIAG_TRAP_WFX=1", full.stdout)
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
