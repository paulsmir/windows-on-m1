"""The closure command must compile its guarded, pinned compiler source."""
from importlib.util import module_from_spec, spec_from_file_location
import hashlib
from pathlib import Path
import tempfile
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "drivers" / "apple-agx" /
          "mesa" / "scripts" / "build-asahi-runtime-closure.py")
ANCHOR = b"#ifndef NDEBUG\n   bool selftest = !dump_shaders;"
GUARD = b"#if !defined(NDEBUG) && !defined(_WIN32)\n   bool selftest = !dump_shaders;"


class CompilerSourceSelectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = spec_from_file_location("native_closure_selection", SCRIPT)
        cls.closure = module_from_spec(spec)
        spec.loader.exec_module(cls.closure)

    def test_pinned_source_is_guarded_in_the_real_compile_command(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            baseline = root / "evidence/src/asahi/compiler/agx_compile.c"
            baseline.parent.mkdir(parents=True)
            baseline.write_bytes(b"start\n" + ANCHOR + b"\nend\n")
            (baseline.parent / "agx_compiler.h").write_bytes(b"pinned ABI header")
            before = hashlib.sha256(baseline.read_bytes()).hexdigest()
            selected, provenance = self.closure.prepare_compiler_selftest_source(
                baseline, root / "out")
            obj = root / "out/objects/106-compiler-agx_compile.obj"
            argv, source_sha256 = self.closure.compile_command(
                Path("clang-cl.exe"), [], [], selected, obj)
            self.assertEqual(Path(argv[argv.index("/c") + 1]), selected)
            self.assertEqual(source_sha256, provenance["after_sha256"])
            self.assertEqual(provenance["before_sha256"], before)
            self.assertIn(GUARD, selected.read_bytes())
            self.assertEqual((selected.parent / "agx_compiler.h").read_bytes(), b"pinned ABI header")
            self.assertEqual(hashlib.sha256(baseline.read_bytes()).hexdigest(), before)
            self.assertIn(str(obj), self.closure.archive_response([obj]))

    def test_anchor_must_be_unique_and_exact(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            baseline = root / "agx_compile.c"
            for contents in (b"no anchor", ANCHOR + b"\n" + ANCHOR,
                             GUARD):
                baseline.write_bytes(contents)
                with self.subTest(contents=contents), self.assertRaises(RuntimeError):
                    self.closure.prepare_compiler_selftest_source(baseline, root / "out")

    def test_windows_line_endings_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            baseline = root / "agx_compile.c"
            baseline.write_bytes(ANCHOR.replace(b"\n", b"\r\n") + b"\r\n")
            selected, provenance = self.closure.prepare_compiler_selftest_source(
                baseline, root / "out")
            self.assertEqual(selected.read_bytes(), GUARD.replace(b"\n", b"\r\n") + b"\r\n")
            self.assertEqual(provenance["before_sha256"], hashlib.sha256(baseline.read_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main()
