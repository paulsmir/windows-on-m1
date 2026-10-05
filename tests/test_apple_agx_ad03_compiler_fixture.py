import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / ".local/accelerated-desktop-ad03/mesa-build"
SOURCE = ROOT / "drivers/apple-agx/mesa/compiler-host/agx_shader_fixture.c"
BUILDER = ROOT / "tools/build_apple_agx_shader_fixture.py"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fnv1a64(path):
    value = 0xCBF29CE484222325
    for byte in path.read_bytes():
        value ^= byte
        value = value * 0x100000001B3 & ((1 << 64) - 1)
    return f"0x{value:016x}"


class AppleAgxAd03CompilerFixtureTests(unittest.TestCase):
    def test_pinned_agx_compiler_is_deterministic_and_source_sensitive(self):
        """Catches replaying one captured binary for distinct NIR programs."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "fixture"
            subprocess.run([
                "python3", str(BUILDER), "--mesa-build", str(BUILD),
                "--source", str(SOURCE), "--output", str(binary),
            ], cwd=ROOT, check=True)
            runs = []
            for name, variant in (("a", "0"), ("b", "0"), ("c", "1")):
                output = root / name
                output.mkdir()
                result = subprocess.run(
                    [str(binary), str(output), variant], cwd=ROOT,
                    check=True, text=True, capture_output=True,
                )
                metadata = json.loads(result.stdout)
                self.assertEqual(metadata["schema"], 1)
                self.assertEqual(metadata["variant"], int(variant))
                self.assertGreater(metadata["compute"]["binary_bytes"], 0)
                self.assertNotIn("fragment", metadata)
                runs.append((output, metadata))
            for shader in ("compute",):
                a = runs[0][0] / f"{shader}.bin"
                b = runs[1][0] / f"{shader}.bin"
                c = runs[2][0] / f"{shader}.bin"
                self.assertEqual(digest(a), digest(b))
                self.assertNotEqual(digest(a), digest(c))
                self.assertEqual(fnv1a64(a), runs[0][1][shader]["fnv1a64"])
                self.assertEqual(fnv1a64(c), runs[2][1][shader]["fnv1a64"])


if __name__ == "__main__":
    unittest.main()
