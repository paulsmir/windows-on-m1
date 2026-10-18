"""Derived G3 builds rebuild their native archive from the selected commit."""
import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "drivers/apple-agx/render-admission/scripts/mkexp.py"
TEMPLATE = Path("/Users/pavel/public_windows/.local/experiments/EXP853-r133-unpublished/build-kmd.ps1")


class MkexpNativeSourceTests(unittest.TestCase):
    def test_derived_build_self_builds_runtime(self):
        spec = importlib.util.spec_from_file_location("mkexp", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        text = module.render_build_script(TEMPLATE.read_text(), "EXP854B", "EXP854B-r134-provenance",
                                          "f" * 40, "a" * 64, "b" * 64, "854")
        self.assertIn("build-native-asahi-state.py", text)
        self.assertIn("build-asahi-runtime-closure.py", text)
        self.assertIn("--source-manifest $manifest", text)
        self.assertIn("-NativeProvenancePython $py", text)
        self.assertNotIn("EXP810-r96-runtime-build", text)
        self.assertIn("SourceCommit='" + "f" * 40 + "'", text)
        self.assertIn("SourceManifestSha256='" + "a" * 64 + "'", text)
        self.assertIn("NativeArchiveSha256=$nativeResult.library.sha256", text)
        self.assertIn("NativeResourceSourceSha256=$nativeResult.resource_source.sha256", text)
        self.assertNotIn("-Incremental", text)


if __name__ == "__main__":
    unittest.main()
