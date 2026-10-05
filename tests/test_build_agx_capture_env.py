from pathlib import Path
import hashlib
import json
import os
import subprocess
import unittest
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/build-agx-capture-env.sh"
DOCKERFILE = ROOT / "tools/agx-capture-container/Dockerfile"
VERIFY = ROOT / "tools/verify-agx-capture-env.py"
MESA_COMMIT = "7a4f24061fa56ef7eff12132dd7b1461d5a890d8"
ARTIFACTS = (
    "agx-clear-capture",
    "asahi_dri.so",
    "libEGL.so.1.0.0",
    "libGLESv2.so.2.0.0",
    "libasahi_m1n1_drm_shim.so",
)


class BuildAgxCaptureEnvironmentTests(unittest.TestCase):
    def test_recipe_pins_image_historical_mesa_and_capture_contract(self):
        recipe = DOCKERFILE.read_text(encoding="utf-8")

        required = (
            "ubuntu:22.04@sha256:2edbbc5dc405e9612ba3584ce95480277e3eb374407b5505fe26f17df77c7dbc",
            "https://gitlab.freedesktop.org/asahilina/mesa.git",
            "7a4f24061fa56ef7eff12132dd7b1461d5a890d8",
            "-Dgallium-drivers=asahi",
            "-Dtools=drm-shim",
            "-Dllvm=disabled",
            "-Dglx=disabled",
            "-Dplatforms=",
            "-Degl=enabled",
            "-Dgbm=enabled",
            "-Dshared-glapi=enabled",
            "-Dbuild-tests=false",
            "libasahi_m1n1_drm_shim.so",
            "agx-clear-capture",
            "Acquire::Retries=5",
            "rm -rf /var/lib/apt/lists/*",
            "socat",
            "construct==2.10.70",
            "pyserial==3.5",
        )
        for item in required:
            self.assertIn(item, recipe)

    def test_dry_run_builds_and_exports_only_the_reviewed_artifacts(self):
        result = subprocess.run(
            [str(SCRIPT), "--output", "/tmp/agx-capture-env"],
            cwd="/tmp",
            env=dict(os.environ, AGX_CAPTURE_ENV_DRY_RUN="1"),
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout
        expected_in_order = (
            "docker build",
            "tools/agx-capture-container/Dockerfile",
            "docker create",
            "/opt/agx-capture/export",
            "docker cp",
            "verify-agx-capture-env.py",
            "publish atomically to /tmp/agx-capture-env",
        )
        cursor = 0
        for item in expected_in_order:
            found = output.find(item, cursor)
            self.assertGreaterEqual(found, 0, f"missing or out-of-order step: {item}")
            cursor = found + len(item)
        self.assertNotIn("/Users/pavel", output)

    def test_unknown_option_is_rejected(self):
        result = subprocess.run(
            [str(SCRIPT), "--surprise"],
            cwd="/tmp",
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("unknown option", result.stderr)

    def test_no_cache_dry_run_requests_an_independent_rebuild(self):
        result = subprocess.run(
            [str(SCRIPT), "--output", "/tmp/agx-capture-env-2"],
            cwd="/tmp",
            env=dict(
                os.environ,
                AGX_CAPTURE_ENV_DRY_RUN="1",
                AGX_CAPTURE_ENV_NO_CACHE="1",
            ),
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("docker build --no-cache", result.stdout)


class VerifyAgxCaptureEnvironmentTests(unittest.TestCase):
    def make_bundle(self, root: Path) -> None:
        hashes = {}
        for index, name in enumerate(ARTIFACTS):
            payload = b"\x7fELF" + bytes([index]) * 32
            (root / name).write_bytes(payload)
            hashes[name] = hashlib.sha256(payload).hexdigest()
        manifest = {
            "schema": 1,
            "mesa_commit": MESA_COMMIT,
            "architecture": "aarch64",
            "compiler": "test compiler",
            "artifacts": hashes,
        }
        (root / "manifest.json").write_text(
            json.dumps(manifest), encoding="utf-8"
        )

    def run_verify(self, root: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(VERIFY), str(root)],
            text=True,
            capture_output=True,
            check=False,
        )

    def test_complete_hash_bound_bundle_is_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_bundle(root)
            result = self.run_verify(root)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(MESA_COMMIT, result.stdout)

    def test_single_byte_mutation_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_bundle(root)
            path = root / "libasahi_m1n1_drm_shim.so"
            path.write_bytes(path.read_bytes() + b"x")
            result = self.run_verify(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("hash mismatch", result.stderr)


if __name__ == "__main__":
    unittest.main()
