"""Behavioural checks for the packaged Mesa assertion gate."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "drivers/apple-agx/render-admission/scripts/check_no_crt_asserts.py"
BUILD_SCRIPT = ROOT / "drivers/apple-agx/render-admission/scripts/build-driver.ps1"


class CheckNoCrtAssertsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def scan(self, name, payload, *options):
        path = self.root / name
        path.write_bytes(b"MZ" + payload)
        result = subprocess.run([sys.executable, str(TOOL), str(path), *options],
                                capture_output=True, text=True)
        return result, json.loads(result.stdout)

    def test_wide_source_path_rejects_and_allow_still_reports(self):
        source = r"C:\x\src\gallium\auxiliary\nir\tgsi_to_nir.c"
        payload = source.encode("utf-16le") + b"\x00\x00" + "!c->input_var_face".encode("utf-16le")
        rejected, record = self.scan("assert.dll", payload)
        self.assertEqual(rejected.returncode, 1)
        self.assertEqual(record["assert_paths"], 1)
        self.assertEqual(record["examples"], [source])
        self.assertEqual(len(record["sha256"]), 64)
        allowed, record = self.scan("allowed.dll", payload, "--allow")
        self.assertEqual(allowed.returncode, 0)
        self.assertEqual(record["assert_paths"], 1)

    def test_ascii_source_path_does_not_match(self):
        result, record = self.scan("ascii.dll", rb"C:\x\src\file.cpp")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(record["assert_paths"], 0)

    def test_wide_relative_source_paths_reject(self):
        paths = ("../../AD04-d3d10-frontend-build/mesa/src/compiler/nir/nir_builder.c",
                 "src/compiler/nir/nir_opcodes.c")
        payload = b"\x00\x00".join(path.encode("utf-16le") for path in paths)
        result, record = self.scan("relative.dll", payload)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(record["assert_paths"], 2)
        self.assertEqual(record["examples"], list(paths))

    def test_wide_bare_source_name_does_not_match(self):
        result, record = self.scan("bare.dll", "nir.c".encode("utf-16le"))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(record["assert_paths"], 0)

    def test_wide_non_path_does_not_match(self):
        result, record = self.scan("text.dll", "Assertion failed!".encode("utf-16le"))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(record["assert_paths"], 0)

    def test_package_wiring(self):
        script = BUILD_SCRIPT.read_text()
        self.assertIn("[switch]$AllowAssertBuild", script)
        self.assertIn("check_no_crt_asserts.py", script)
        self.assertIn("$umd, $WowUmd, $WowOpenGLIcd", script)


if __name__ == "__main__":
    unittest.main()
