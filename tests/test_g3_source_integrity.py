import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

from scripts.g3_build_source_manifest import create_manifest


class SourceManifestTest(unittest.TestCase):
    def test_manifest_uses_committed_blobs_and_archives_every_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            source = root / "drivers/apple-agx/shared/src/apple_agx_gpuva_g3_graph.c"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"committed graph\n")
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            subprocess.run(
                ["git", "-C", str(root), "-c", "user.name=Test", "-c", "user.email=test@example.org", "commit", "-qm", "base"],
                check=True,
            )
            commit = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
            manifest, archive = create_manifest(root, commit, root / "manifest.json", root / "sources.zip")
            self.assertEqual([item["path"] for item in manifest["files"]], ["drivers/apple-agx/shared/src/apple_agx_gpuva_g3_graph.c"])
            self.assertEqual(manifest["files"][0]["sha256"], hashlib.sha256(b"committed graph\n").hexdigest())
            with zipfile.ZipFile(archive) as bundle:
                self.assertEqual(bundle.read(manifest["files"][0]["path"]), b"committed graph\n")

            source.write_bytes(b"uncommitted graph\n")
            with self.assertRaisesRegex(ValueError, "working tree differs"):
                create_manifest(root, commit, root / "manifest.json", root / "sources.zip")


if __name__ == "__main__":
    unittest.main()
