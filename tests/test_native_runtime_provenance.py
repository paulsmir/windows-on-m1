"""A package may only link the native archive built for its source manifest."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
CHECK = ROOT / "drivers/apple-agx/render-admission/scripts/verify-native-runtime-provenance.py"
NS = "http://schemas.microsoft.com/developer/msbuild/2003"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class NativeRuntimeProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.commit = "a" * 40
        self.manifest = self.base / "source-manifest.json"
        self.manifest.write_text(json.dumps({"repository_commit": self.commit}))
        self.archive = self.base / "native_runtime.lib"
        self.archive.write_bytes(b"archive from current source")
        self.resource = self.base / "Resource.cpp"
        self.resource.write_text("general UpdateSubresourceUP")
        self.prepare = self.base / "prepare.json"
        self.prepare.write_text(json.dumps({"overlays": {
            "src/gallium/frontends/d3d10umd/Resource.cpp": {"final_sha256": sha(self.resource)}}}))
        self.result = self.base / "result.json"
        self.props = self.base / "NativeRuntime.props"
        self.write_receipts(self.commit)

    def write_receipts(self, commit):
        result = {"exit": 0, "source_commit": commit,
                  "source_manifest_sha256": sha(self.manifest),
                  "prepared_result_sha256": sha(self.prepare),
                  "library": {"path": str(self.archive), "sha256": sha(self.archive)},
                  "resource_source": {"path": str(self.resource), "sha256": sha(self.resource)}}
        self.result.write_text(json.dumps(result))
        root = ET.Element("Project", xmlns=NS)
        group = ET.SubElement(root, "PropertyGroup")
        for key, val in {"NativeRuntimeLibrary": str(self.archive),
                         "NativeRuntimeSourceCommit": commit,
                         "NativeRuntimeSourceManifestSha256": sha(self.manifest),
                         "NativeRuntimeLibrarySha256": sha(self.archive)}.items():
            ET.SubElement(group, key).text = val
        ET.ElementTree(root).write(self.props)

    def check(self):
        return subprocess.run([sys.executable, str(CHECK), "--source-manifest", str(self.manifest),
                               "--props", str(self.props), "--result", str(self.result),
                               "--prepared-result", str(self.prepare)], capture_output=True, text=True)

    def test_self_built_archive_accepted(self):
        self.assertEqual(self.check().returncode, 0, self.check().stderr)

    def test_foreign_archive_commit_rejected(self):
        self.write_receipts("b" * 40)
        self.assertNotEqual(self.check().returncode, 0)

    def test_unstamped_old_props_rejected(self):
        self.props.write_text('<Project><PropertyGroup><NativeRuntimeLibrary>' +
                              str(self.archive) + '</NativeRuntimeLibrary></PropertyGroup></Project>')
        self.assertNotEqual(self.check().returncode, 0)

    def test_archive_replacement_rejected(self):
        self.archive.write_bytes(b"old runtime")
        self.assertNotEqual(self.check().returncode, 0)

    def test_uncompiled_resource_projection_rejected(self):
        self.resource.write_text("old constant buffer only")
        self.assertNotEqual(self.check().returncode, 0)


if __name__ == "__main__":
    unittest.main()
