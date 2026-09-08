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
            self.assertEqual(first[0]["schema"], 2)
            self.assertEqual(first[0]["uvs_size"], 8)
            self.assertEqual(first[0]["uvs_user_size"], 4)
            self.assertEqual(first[0]["epilog_loc_written"], 1)
            self.assertGreater(first[0]["pipeline_bytes"], 0)
            self.assertGreater(first[0]["encoder_bytes"], 68)
            self.assertGreater(first[0]["ppp_bytes"], 0)
            self.assertEqual(first[0]["draw"], {
                "topology": "triangle-list",
                "vertex_count": 3,
                "instance_count": 1,
                "stream_terminated": True,
            })
            self.assertEqual(first[0]["viewport"], {
                "width": 2560,
                "height": 1600,
                "scissor_count": 1,
                "depth_bias_count": 1,
            })
            self.assertEqual(first[0]["render_pass"], {
                "owner": "EXP208-hardware-proven-3D-skeleton",
                "dynamic_scope": "VDM-PPP-USC",
                "store_pipeline_reused": True,
            })
            self.assertTrue(first[0]["generated_unpack_valid"])
            self.assertEqual(
                [entry["kind"] for entry in first[0]["relocations"]],
                [
                    "UscBufferAddress40", "UscShaderOffset32",
                    "UscBufferAddress40",
                    "UscShaderOffset32", "VdmPipelineOffset32",
                    "PppStateAddress40", "VdmPipelineOffset32",
                ],
            )
            ppp_relocation = first[0]["relocations"][5]
            self.assertEqual(ppp_relocation["target"], "encoder.ppp")
            self.assertEqual(
                first[0]["relocations"][3]["target"], "fragment-linked"
            )
            for name in ("pipeline", "encoder", "scissor", "depth_bias"):
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
            linked_assembly = (first[1] / "fragment-linked.asm").read_text()
            self.assertTrue(linked_assembly.strip())
            self.assertNotIn("XXX error here", linked_assembly)

    def test_generated_triangle_composes_through_production_overlay_contract(self):
        """Catches a fixture that cannot survive the real ABI/DMA/overlay path."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "fixture"
            verifier = root / "integration"
            output = root / "out"
            output.mkdir()
            env = dict(os.environ)
            env["PATH"] = (
                str(ROOT / ".local/tooling/mesa-build-venv/bin") + ":" +
                "/opt/homebrew/opt/llvm/bin:" + env.get("PATH", "")
            )
            subprocess.run([
                "python3", str(BUILDER), "--mesa-build", str(BUILD),
                "--source", str(SOURCE), "--output", str(binary),
            ], cwd=ROOT, env=env, check=True)
            subprocess.run([str(binary), str(output), "0"], cwd=ROOT,
                           text=True, capture_output=True, check=True)
            driver = ROOT / "drivers/apple-agx/render-admission"
            shared = ROOT / "drivers/apple-agx/shared"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(driver / "include"), "-I", str(shared / "include"),
                str(driver / "tests/render_dynamic_fixture_integration_test.c"),
                str(driver / "src/apple_agx_dynamic_job.c"),
                str(driver / "src/render_dynamic_overlay.c"),
                str(driver / "src/render_dynamic_dma.c"),
                str(shared / "src/apple_agx_win32_abi.c"),
                str(shared / "src/apple_agx_render_template.generated.c"),
                "-o", str(verifier),
            ], cwd=ROOT, check=True)
            subprocess.run([
                str(verifier), str(output / "vertex.bin"),
                str(output / "fragment-linked.bin"),
                str(output / "fragment.bin"), str(output / "pipeline.bin"),
                str(output / "encoder.bin"), str(output / "scissor.bin"),
                str(output / "depth_bias.bin"),
            ], cwd=ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
