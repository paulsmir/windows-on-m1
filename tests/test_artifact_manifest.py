import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.artifact_manifest import ManifestError, create_manifest, verify_manifest


class ArtifactManifestTests(unittest.TestCase):
    def test_create_and_verify_records_revisions_profile_and_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", root], check=True)
            subprocess.run(["git", "-C", root, "config", "user.email", "test@example.invalid"], check=True)
            subprocess.run(["git", "-C", root, "config", "user.name", "Test"], check=True)
            (root / "tracked").write_text("source", encoding="utf-8")
            subprocess.run(["git", "-C", root, "add", "tracked"], check=True)
            subprocess.run(["git", "-C", root, "commit", "-qm", "source"], check=True)
            artifacts = root / "dist"
            artifacts.mkdir()
            (artifacts / "boot.bin").write_bytes(b"boot")
            path = create_manifest(root, artifacts, "release", "physical", "off", ["boot.bin"])
            data = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(data["profile"], "release")
            self.assertEqual(data["display"], "physical")
            self.assertEqual(data["debug"], "off")
            self.assertEqual(len(data["root_commit"]), 40)
            verify_manifest(path, expected_profile="release")

    def test_verify_rejects_corruption_and_wrong_profile(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", root], check=True)
            subprocess.run(["git", "-C", root, "config", "user.email", "test@example.invalid"], check=True)
            subprocess.run(["git", "-C", root, "config", "user.name", "Test"], check=True)
            (root / "tracked").write_text("source", encoding="utf-8")
            subprocess.run(["git", "-C", root, "add", "tracked"], check=True)
            subprocess.run(["git", "-C", root, "commit", "-qm", "source"], check=True)
            artifacts = root / "dist"
            artifacts.mkdir()
            (artifacts / "boot.bin").write_bytes(b"boot")
            path = create_manifest(root, artifacts, "debug", "both", "full", ["boot.bin"])
            with self.assertRaises(ManifestError):
                verify_manifest(path, expected_profile="release")
            (artifacts / "boot.bin").write_bytes(b"changed")
            with self.assertRaises(ManifestError):
                verify_manifest(path, expected_profile="debug")

    def test_create_rejects_dirty_tracked_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", root], check=True)
            subprocess.run(["git", "-C", root, "config", "user.email", "test@example.invalid"], check=True)
            subprocess.run(["git", "-C", root, "config", "user.name", "Test"], check=True)
            tracked = root / "tracked"
            tracked.write_text("source", encoding="utf-8")
            subprocess.run(["git", "-C", root, "add", "tracked"], check=True)
            subprocess.run(["git", "-C", root, "commit", "-qm", "source"], check=True)
            tracked.write_text("dirty", encoding="utf-8")
            artifacts = root / "dist"
            artifacts.mkdir()
            (artifacts / "boot.bin").write_bytes(b"boot")
            with self.assertRaises(ManifestError):
                create_manifest(root, artifacts, "release", "physical", "off", ["boot.bin"])


if __name__ == "__main__":
    unittest.main()
