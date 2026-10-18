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
            commit = subprocess.run(
                ["git", "-C", root, "rev-parse", "HEAD"], check=True,
                capture_output=True, text=True,
            ).stdout.strip()
            artifacts = root / "dist"
            artifacts.mkdir()
            (artifacts / "m1n1.macho").write_bytes(
                b"plain##m1n1_ver##" + commit[:7].encode() + b"\0"
            )
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

    def test_manifest_capability_can_be_recorded_and_required(self):
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
                root, artifacts, "debug", "both", "monitor", ["boot.bin"],
                compiler="clang", capabilities=("agx-g2",),
            )
            data = verify_manifest(
                path, expected_profile="debug",
                required_capabilities=("agx-g2",),
            )
            self.assertEqual(data["capabilities"], ["agx-g2"])
            with self.assertRaisesRegex(ManifestError, "missing capability"):
                verify_manifest(path, required_capabilities=("future-gate",))

    def test_create_rejects_stale_or_dirty_embedded_m1n1_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", root], check=True)
            subprocess.run(["git", "-C", root, "config", "user.email", "test@example.invalid"], check=True)
            subprocess.run(["git", "-C", root, "config", "user.name", "Test"], check=True)
            self.write_layout(root)
            (root / "m1n1_windows").mkdir()
            subprocess.run(["git", "init", "-q", root / "m1n1_windows"], check=True)
            subprocess.run(["git", "-C", root / "m1n1_windows", "config", "user.email", "test@example.invalid"], check=True)
            subprocess.run(["git", "-C", root / "m1n1_windows", "config", "user.name", "Test"], check=True)
            (root / "m1n1_windows" / "source").write_text("source", encoding="utf-8")
            subprocess.run(["git", "-C", root / "m1n1_windows", "add", "source"], check=True)
            subprocess.run(["git", "-C", root / "m1n1_windows", "commit", "-qm", "source"], check=True)
            subprocess.run(["git", "-C", root, "add", "config"], check=True)
            subprocess.run(["git", "-C", root, "commit", "-qm", "source"], check=True)
            m1n1_commit = subprocess.run(
                ["git", "-C", root / "m1n1_windows", "rev-parse", "HEAD"],
                check=True, capture_output=True, text=True,
            ).stdout.strip()
            artifacts = root / "dist"
            artifacts.mkdir()

            (artifacts / "m1n1.macho").write_bytes(
                b"##m1n1_ver##" + m1n1_commit[:7].encode() + b"\0"
            )
            manifest = create_manifest(
                root, artifacts, "debug", "both", "monitor",
                ["m1n1.macho"], compiler="clang",
            )
            data = verify_manifest(manifest)
            self.assertEqual(
                data["artifacts"]["m1n1.macho"]["build_tag"],
                m1n1_commit[:7],
            )

            for tag in ("deadbee", "deadbee-dirty"):
                with self.subTest(tag=tag):
                    (artifacts / "m1n1.macho").write_bytes(
                        b"##m1n1_ver##" + tag.encode() + b"\0"
                    )
                    with self.assertRaisesRegex(ManifestError, "m1n1 build identity"):
                        create_manifest(
                            root, artifacts, "debug", "both", "monitor",
                            ["m1n1.macho"], compiler="clang",
                        )

    def test_verify_rejects_tampered_m1n1_build_tag_even_with_matching_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            artifact_dir = Path(directory)
            binary = artifact_dir / "m1n1.macho"
            binary.write_bytes(b"##m1n1_ver##deadbee-dirty\0")
            import hashlib
            data = {
                "format_version": 2,
                "platform": "j313",
                "profile": "debug",
                "display": "both",
                "debug": "monitor",
                "compiler": "clang",
                "guest_layout_sha256": "0" * 64,
                "guest_contract": dict(__import__("tools.artifact_manifest", fromlist=["J313_GUEST_CONTRACT"]).J313_GUEST_CONTRACT),
                "m1n1_windows_commit": "deadbee" + "0" * 33,
                "artifacts": {
                    "m1n1.macho": {
                        "size": binary.stat().st_size,
                        "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                        "role": "assisted-chainload",
                    }
                },
            }
            manifest = artifact_dir / "MANIFEST.json"
            manifest.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(ManifestError, "m1n1 build identity"):
                verify_manifest(manifest)

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
