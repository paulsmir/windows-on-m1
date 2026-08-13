import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.artifact_manifest import ManifestError, create_manifest, verify_manifest


class ArtifactManifestTests(unittest.TestCase):
    @staticmethod
    def write_layout(root):
        config = root / "config"
        config.mkdir()
        layout = {
            "layout_version": 1,
            "phys_base": "0x850000000",
            "ram_end": "0xa00000000",
            "virtual_fb_base": "0x85f000000",
            "virtual_fb_width": 2560,
            "virtual_fb_height": 1600,
            "virtual_fb_stride": 10240,
            "cpu_count": 8,
        }
        (config / "j313-guest-layout.json").write_text(
            json.dumps(layout), encoding="utf-8"
        )

    def test_create_and_verify_records_revisions_profile_and_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", root], check=True)
            subprocess.run(["git", "-C", root, "config", "user.email", "test@example.invalid"], check=True)
            subprocess.run(["git", "-C", root, "config", "user.name", "Test"], check=True)
            self.write_layout(root)
            (root / "tracked").write_text("source", encoding="utf-8")
            subprocess.run(["git", "-C", root, "add", "tracked", "config"], check=True)
            subprocess.run(["git", "-C", root, "commit", "-qm", "source"], check=True)
            artifacts = root / "dist"
            artifacts.mkdir()
            (artifacts / "boot.bin").write_bytes(b"boot")
            path = create_manifest(
                root,
                artifacts,
                "release",
                "physical",
                "off",
                ["boot.bin"],
                compiler="Homebrew clang version 22.1.8",
            )
            data = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(data["profile"], "release")
            self.assertEqual(data["display"], "physical")
            self.assertEqual(data["debug"], "off")
            self.assertEqual(data["compiler"], "Homebrew clang version 22.1.8")
            self.assertEqual(data["guest_contract"]["cpu_count"], 8)
            self.assertEqual(data["guest_contract"]["virtual_fb_width"], 2560)
            self.assertEqual(len(data["guest_layout_sha256"]), 64)
            self.assertEqual(len(data["root_commit"]), 40)
            verify_manifest(path, expected_profile="release")
            verify_manifest(
                path,
                expected_profile="release",
                expected_display="physical",
                expected_debug="off",
            )
            with self.assertRaisesRegex(ManifestError, "debug mode mismatch"):
                verify_manifest(path, expected_debug="monitor")

    def test_manifest_records_and_verifies_artifact_roles(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", root], check=True)
            subprocess.run(["git", "-C", root, "config", "user.email", "test@example.invalid"], check=True)
            subprocess.run(["git", "-C", root, "config", "user.name", "Test"], check=True)
            self.write_layout(root)
            subprocess.run(["git", "-C", root, "add", "config"], check=True)
            subprocess.run(["git", "-C", root, "commit", "-qm", "source"], check=True)
            artifacts = root / "dist"
            artifacts.mkdir()
            (artifacts / "m1n1.macho").write_bytes(b"plain")
            path = create_manifest(
                root, artifacts, "debug", "physical", "full", ["m1n1.macho"],
                compiler="clang", artifact_roles={"m1n1.macho": "assisted-chainload"},
            )
            data = verify_manifest(path, expected_profile="debug")
            self.assertEqual(data["artifacts"]["m1n1.macho"]["role"], "assisted-chainload")
            data["artifacts"]["m1n1.macho"]["role"] = "autonomous-stage1"
            path.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(ManifestError, "artifact role mismatch"):
                verify_manifest(
                    path, expected_profile="debug",
                    expected_roles={"m1n1.macho": "assisted-chainload"},
                )

    def test_verify_rejects_corruption_and_wrong_profile(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", root], check=True)
            subprocess.run(["git", "-C", root, "config", "user.email", "test@example.invalid"], check=True)
            subprocess.run(["git", "-C", root, "config", "user.name", "Test"], check=True)
            self.write_layout(root)
            (root / "tracked").write_text("source", encoding="utf-8")
            subprocess.run(["git", "-C", root, "add", "tracked", "config"], check=True)
            subprocess.run(["git", "-C", root, "commit", "-qm", "source"], check=True)
            artifacts = root / "dist"
            artifacts.mkdir()
            (artifacts / "boot.bin").write_bytes(b"boot")
            path = create_manifest(
                root, artifacts, "debug", "both", "full", ["boot.bin"], compiler="clang"
            )
            with self.assertRaises(ManifestError):
                verify_manifest(path, expected_profile="release")
            (artifacts / "boot.bin").write_bytes(b"changed")
            with self.assertRaises(ManifestError):
                verify_manifest(path, expected_profile="debug")

    def test_verify_rejects_wrong_j313_framebuffer_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", root], check=True)
            subprocess.run(["git", "-C", root, "config", "user.email", "test@example.invalid"], check=True)
            subprocess.run(["git", "-C", root, "config", "user.name", "Test"], check=True)
            self.write_layout(root)
            subprocess.run(["git", "-C", root, "add", "config"], check=True)
            subprocess.run(["git", "-C", root, "commit", "-qm", "source"], check=True)
            artifacts = root / "dist"
            artifacts.mkdir()
            (artifacts / "boot.bin").write_bytes(b"boot")
            path = create_manifest(
                root, artifacts, "release", "physical", "off", ["boot.bin"], compiler="clang"
            )
            data = json.loads(path.read_text(encoding="utf-8"))
            data["guest_contract"]["virtual_fb_width"] = 1280
            path.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(ManifestError, "guest contract mismatch"):
                verify_manifest(path, expected_profile="release")

    def test_create_rejects_dirty_tracked_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", root], check=True)
            subprocess.run(["git", "-C", root, "config", "user.email", "test@example.invalid"], check=True)
            subprocess.run(["git", "-C", root, "config", "user.name", "Test"], check=True)
            self.write_layout(root)
            tracked = root / "tracked"
            tracked.write_text("source", encoding="utf-8")
            subprocess.run(["git", "-C", root, "add", "tracked", "config"], check=True)
            subprocess.run(["git", "-C", root, "commit", "-qm", "source"], check=True)
            tracked.write_text("dirty", encoding="utf-8")
            artifacts = root / "dist"
            artifacts.mkdir()
            (artifacts / "boot.bin").write_bytes(b"boot")
            with self.assertRaises(ManifestError):
                create_manifest(
                    root, artifacts, "release", "physical", "off", ["boot.bin"], compiler="clang"
                )

    def test_debug_manifest_can_record_dirty_source_without_weakening_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", root], check=True)
            subprocess.run(["git", "-C", root, "config", "user.email", "test@example.invalid"], check=True)
            subprocess.run(["git", "-C", root, "config", "user.name", "Test"], check=True)
            self.write_layout(root)
            tracked = root / "tracked"
            tracked.write_text("source", encoding="utf-8")
            subprocess.run(["git", "-C", root, "add", "tracked", "config"], check=True)
            subprocess.run(["git", "-C", root, "commit", "-qm", "source"], check=True)
            tracked.write_text("dirty", encoding="utf-8")
            artifacts = root / "dist"
            artifacts.mkdir()
            (artifacts / "boot.bin").write_bytes(b"boot")

            path = create_manifest(
                root, artifacts, "debug", "physical", "full", ["boot.bin"],
                compiler="clang", allow_dirty=True,
            )
            data = verify_manifest(path, expected_profile="debug")
            self.assertTrue(data["source_dirty"])
            self.assertEqual(len(data["root_diff_sha256"]), 64)

            with self.assertRaises(ManifestError):
                create_manifest(
                    root, artifacts, "release", "physical", "off", ["boot.bin"],
                    compiler="clang", allow_dirty=True,
                )


if __name__ == "__main__":
    unittest.main()
