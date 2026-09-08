from pathlib import Path
import json
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / ".local/accelerated-desktop-ad03/mesa-build"
SOURCE = ROOT / "drivers/apple-agx/mesa/compiler-host/agx_vs_fs_fixture.c"
BUILDER = ROOT / "tools/build_apple_agx_vs_fs_fixture.py"


class AppleAgxAd03VsFsFixtureTests(unittest.TestCase):
    def test_driver_lowered_vs_fs_are_deterministic_and_source_sensitive(self):
        """Catches bypassing Asahi UVS/epilog lowering or replaying one shader."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "fixture"
            env = dict(os.environ)
            env["PATH"] = (
                str(ROOT / ".local/tooling/mesa-build-venv/bin") + ":" +
                "/opt/homebrew/opt/llvm/bin:" + env.get("PATH", "")
            )
            subprocess.run([
                "python3", str(BUILDER), "--mesa-build", str(BUILD),
                "--source", str(SOURCE), "--output", str(binary),
            ], cwd=ROOT, env=env, check=True)
            records = []
            for name, variant in (("a", "0"), ("b", "0"), ("c", "1")):
                output = root / name
                output.mkdir()
                run = subprocess.run(
                    [str(binary), str(output), variant], cwd=ROOT,
                    text=True, capture_output=True, check=True,
                )
                records.append((json.loads(run.stdout), output))
            first, repeat, changed = records
            self.assertEqual(first[0], repeat[0])
            self.assertEqual(first[0]["uvs_size"], 8)
            self.assertEqual(first[0]["uvs_user_size"], 4)
            self.assertEqual(first[0]["epilog_loc_written"], 1)
            self.assertGreater(first[0]["pipeline_bytes"], 0)
            self.assertGreater(first[0]["encoder_bytes"], 0)
            self.assertEqual(
                [entry["kind"] for entry in first[0]["relocations"]],
                [
                    "UscShaderOffset32", "UscBufferAddress40",
                    "UscShaderOffset32", "VdmPipelineOffset32",
                    "VdmPipelineOffset32",
                ],
            )
            for name in ("pipeline", "encoder"):
                self.assertEqual(
                    (first[1] / f"{name}.bin").read_bytes(),
                    (changed[1] / f"{name}.bin").read_bytes(),
                )
            for stage in ("vertex", "fragment"):
                self.assertEqual(
                    (first[1] / f"{stage}.bin").read_bytes(),
                    (repeat[1] / f"{stage}.bin").read_bytes(),
                )
                self.assertNotEqual(
                    (first[1] / f"{stage}.bin").read_bytes(),
                    (changed[1] / f"{stage}.bin").read_bytes(),
                )
                assembly = (first[1] / f"{stage}.asm").read_text()
                self.assertTrue(assembly.strip())
                self.assertNotIn("XXX error here", assembly)


if __name__ == "__main__":
    unittest.main()
